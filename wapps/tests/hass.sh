#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
#
# Prove the Home Assistant surface of the display on a host build of the WANTED
# engine. A broker runs in a container, a pty pair carries the link and
# stm32stub.py answers as the STM32 does, thus this needs no hardware and no
# Home Assistant.
#
# Usage: WANTED=<path to wanted-cli> wapps/tests/hass.sh
#
# Note: the engine of that build needs CONFIG_WANTED_VFS_UART=y and four wapp
# slots, and a socket read that honours O_NONBLOCK. Without the last one the
# adapter parks in the read of the broker and publishes no reading. The broker
# image is eclipse-mosquitto.
#
# Note: the cadence rides a config mount. The desired state of the control
# plane carries it as `config: [{path, content}]`, which the supervisor turns
# into the mount below; a launch config names that mount itself.

set -u

HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
WANTED=${WANTED:-}
WORK=${WORK:-/tmp/tg-hass-test}
PORT=${PORT:-11883}
ENGINE=${ENGINE:-podman}
BROKER_IMAGE=${BROKER_IMAGE:-docker.io/library/eclipse-mosquitto:2}
DEVICE=${DEVICE:-tg-test}

if [ -z "$WANTED" ] || [ ! -x "$WANTED" ]; then
    echo "FAIL: set WANTED to a wanted-cli of a host build"
    exit 1
fi

SUPERVISOR=${SUPERVISOR:-$(cd "$(dirname "$WANTED")/../.." && pwd)/wasm/supervisor/wsh/supervisor.tar}

pkill -x wanted-cli >/dev/null 2>&1
$ENGINE rm -f tg-broker-mqtt >/dev/null 2>&1
rm -rf "$WORK"
mkdir -p "$WORK/registry"

for w in tg-broker tg-display tg-hass; do
    if [ ! -f "$REPO/wapps/$w/$w.wasm" ]; then
        echo "FAIL: $w is not built (run 'make wapps')"
        exit 1
    fi
    s=$(mktemp -d)
    cp "$REPO/wapps/$w/$w.wasm" "$s/app.wasm"
    tar --format=ustar --owner=0 --group=0 --mtime='1970-01-01 00:00:00 UTC' \
        -C "$s" -cf "$WORK/registry/$w@0.0.1-1.wapp" app.wasm
    rm -rf "$s"
done

# An open broker, on the loopback of this host.
printf 'listener %s 0.0.0.0\nallow_anonymous true\n' "$PORT" > "$WORK/mosquitto.conf"
$ENGINE run -d --rm --name tg-broker-mqtt --network host \
    -v "$WORK/mosquitto.conf:/mosquitto/config/mosquitto.conf:ro,z" \
    "$BROKER_IMAGE" >/dev/null 2>&1
sleep 2

mqtt() { $ENGINE run --rm --network host "$BROKER_IMAGE" "$@"; }

cat > "$WORK/config.json" <<EOF
{
  "system": {},
  "supervisor": {
    "imagePath": "$SUPERVISOR",
    "params": {
      "privileged": true,
      "console": {"in": {"name": "platform"}, "out": {"name": "platform"},
                  "err": {"name": "platform"}},
      "drivers": [{"name": "wanted"}]
    }
  }
}
EOF

cd "$WORK"
nohup socat pty,raw,echo=0,link="$WORK/ptyA" pty,raw,echo=0,link="$WORK/ptyB" \
    >/dev/null 2>&1 &
socat_pid=$!
sleep 1
A=$(readlink -f ptyA)
B=$(readlink -f ptyB)

STUB_SECONDS=${STUB_SECONDS:-100} STUB_DIGITS=2 STUB_PANELS=3 STUB_SLEEP=1320 \
STUB_WAKE=360 nohup python3 "$HERE/stm32stub.py" "$B" > "$WORK/stub.log" 2>&1 &
stub_pid=$!
sleep 1

BCFG='{"console":{"in":{"name":"null"},"out":{"name":"platform"},"err":{"name":"platform"}},"drivers":[{"name":"uart","options":"port=1,dev='"$A"',baud=460800,format=8N1"}],"args":["display"]}'
DCFG='{"console":{"in":{"name":"null"},"out":{"name":"platform"},"err":{"name":"platform"}},"args":["hass"]}'
HCFG='{"console":{"in":{"name":"null"},"out":{"name":"platform"},"err":{"name":"platform"}},"sockets":[{"name":"mqtt","address":"tcp://127.0.0.1:'"$PORT"'"}],"envs":["HASS_DEVICE='"$DEVICE"'","HASS_PREFIX=telegraph"],"mounts":[{"name":"config","path":"/etc/tg-hass.conf","options":"interval_s=5"}]}'

{
  sleep 1; echo "create tg-broker"
  sleep 1; echo "set_config tg-broker $BCFG"
  sleep 1; echo "start tg-broker"
  sleep 2; echo "create tg-display"
  sleep 1; echo "set_config tg-display $DCFG"
  sleep 1; echo "start tg-display"
  sleep 2; echo "create tg-hass"
  sleep 1; echo "set_config tg-hass $HCFG"
  sleep 1; echo "start tg-hass"
  sleep 70
} | timeout 130 "$WANTED" config.json > "$WORK/engine.log" 2>&1 &
engine_pid=$!

# The wapps need a moment before the broker holds their messages.
sleep 14

pass=0
fail=0
check() {
    local name=$1 expect=$2 got=$3
    if printf '%s' "$got" | grep -q "$expect"; then
        echo "ok   $name"
        pass=$((pass + 1))
    else
        echo "FAIL $name: expected /$expect/, got: $got"
        fail=$((fail + 1))
    fi
}

check "the device is discovered" '"cmds\?"\|"cmps"' \
      "$(mqtt mosquitto_sub -h 127.0.0.1 -p "$PORT" \
             -t "homeassistant/device/$DEVICE/config" -C 1 -W 5 2>&1)"
check "the discovery names the panels" '"Main panel"' \
      "$(mqtt mosquitto_sub -h 127.0.0.1 -p "$PORT" \
             -t "homeassistant/device/$DEVICE/config" -C 1 -W 5 2>&1)"
check "the DND entities are config, not the default list" \
      '"dnd_start":{"p":"time".*"ent_cat":"config"' \
      "$(mqtt mosquitto_sub -h 127.0.0.1 -p "$PORT" \
             -t "homeassistant/device/$DEVICE/config" -C 1 -W 5 2>&1)"
check "the device is available" 'online' \
      "$(mqtt mosquitto_sub -h 127.0.0.1 -p "$PORT" \
             -t "telegraph/$DEVICE/availability" -C 1 -W 5 2>&1)"

mqtt mosquitto_pub -h 127.0.0.1 -p "$PORT" \
     -t "telegraph/$DEVICE/main/set" -m "hello from HA" >/dev/null 2>&1
sleep 3

check "a command reaches the board" 'opcode 0x13' "$(cat "$WORK/stub.log")"
check "the entity reads back what was set" '{"text":"hello from HA"}' \
      "$(mqtt mosquitto_sub -h 127.0.0.1 -p "$PORT" \
             -t "telegraph/$DEVICE/main" -C 1 -W 5 2>&1)"

mqtt mosquitto_pub -h 127.0.0.1 -p "$PORT" \
     -t "telegraph/$DEVICE/brightness/panels/set" -m "6" >/dev/null 2>&1
sleep 3

check "a brightness command reaches the board" 'opcode 0x11' \
      "$(cat "$WORK/stub.log")"
check "a panel level keeps the board's digit level" 'set brightness 2 6' \
      "$(cat "$WORK/stub.log")"

mqtt mosquitto_pub -h 127.0.0.1 -p "$PORT" \
     -t "telegraph/$DEVICE/dnd/start/set" -m "22:00" >/dev/null 2>&1
sleep 3

check "a DND command reaches the board" 'opcode 0x12' \
      "$(cat "$WORK/stub.log")"
check "the DND entity reads back what was set" '22:00' \
      "$(mqtt mosquitto_sub -h 127.0.0.1 -p "$PORT" \
             -t "telegraph/$DEVICE/dnd/start" -C 1 -W 5 2>&1)"

# The readings: one document on the state topic, which every sensor entity of
# the discovery reads a field of.
state=$(mqtt mosquitto_sub -h 127.0.0.1 -p "$PORT" \
             -t "telegraph/$DEVICE/state" -C 1 -W 8 2>&1)
check "the board publishes its readings" '"temperature"' "$state"
check "the readings carry the counters of the link" '"resyncs"' "$state"
check "the readings carry the firmware of the display" '"firmware"' "$state"
check "the discovery names the temperature entity" '"dev_cla":"temperature"' \
      "$(mqtt mosquitto_sub -h 127.0.0.1 -p "$PORT" \
             -t "homeassistant/device/$DEVICE/config" -C 1 -W 5 2>&1)"

pub() { mqtt mosquitto_pub -h 127.0.0.1 -p "$PORT" -t "telegraph/$DEVICE/$1" -m "$2" >/dev/null 2>&1; }
sub() { mqtt mosquitto_sub -h 127.0.0.1 -p "$PORT" -t "telegraph/$DEVICE/$1" -C 1 -W 6 2>&1; }

# The board holds the settings, and the adapter shows them: the stub was seeded
# with levels 2 and 3 and a period from 22:00 to 06:00. The commands above set
# the panel level to 6 and the start of the period to 22:00.
check "the digit level read back is the board's" '^2$' "$(sub brightness/digits)"
check "the panel level read back is the last one set" '^6$' "$(sub brightness/panels)"
check "the period start read back is the board's" '22:00' "$(sub dnd/start)"
check "the period end read back is the board's" '06:00' "$(sub dnd/end)"

# A digit level keeps the panel level, which the adapter took from the board.
pub brightness/digits/set 7
sleep 3
check "a digit level keeps the panel level" 'set brightness 7 6' \
      "$(cat "$WORK/stub.log")"

# The outside temperature.
pub temperature/ext/set "-12.5"
sleep 3
check "the outside temperature reaches the board in tenths" 'set exttemp -125' \
      "$(cat "$WORK/stub.log")"
check "the state document carries the outside temperature" \
      '"ext_temperature":"-12.5"' "$(sub state)"
pub temperature/ext/set "abc"
sleep 3
check "text is refused and the board keeps its value" \
      'is not a number in range' "$(grep -a 'hass:' "$WORK/engine.log")"
check "the state document still carries the old value" \
      '"ext_temperature":"-12.5"' "$(sub state)"
pub temperature/ext/set ""
sleep 3
check "an empty value clears the outside temperature" 'clear exttemp' \
      "$(cat "$WORK/stub.log")"
check "the state document shows it empty" '"ext_temperature":""' "$(sub state)"
check "the discovery names the outside temperature entity" \
      '"External temperature"' \
      "$(mqtt mosquitto_sub -h 127.0.0.1 -p "$PORT" \
             -t "homeassistant/device/$DEVICE/config" -C 1 -W 5 2>&1)"

# Empty panel texts give the panels back to the board.
pub main/set ""
sleep 3
check "an empty main text releases the main panel" 'release panel 0' \
      "$(cat "$WORK/stub.log")"
check "an empty text reads back as a document, not an empty message" \
      '{"text":""}' "$(sub main)"
pub sub/set ""
sleep 3
check "an empty sub text releases the sub panel" 'release panel 1' \
      "$(cat "$WORK/stub.log")"

# A line break: the two characters become a line feed on the main panel.
pub main/set 'Dom\nOK'
sleep 3
check "the two characters \\n become a line feed" '446f6d0a4f4b' \
      "$(cat "$WORK/stub.log")"

# A text of 40 two-byte letters is cut to 64 bytes, never refused.
long=$(python3 -c "print('\u017c' * 40)")
pub main/set "$long"
sleep 3
check "a long text is cut to the 64 bytes the board takes" \
      'text hex \(c5bc\)\{32\}$' "$(grep -a 'text hex' "$WORK/stub.log" | tail -1)"
check "the cut is logged" 'was cut to 64 bytes' "$(grep -a 'hass:' "$WORK/engine.log")"
check "a level reads back as the board took it" '^7$' "$(sub brightness/digits)"
check "the panel entities read their text from a document" \
      '"val_tpl":"{{value_json.text}}"' \
      "$(mqtt mosquitto_sub -h 127.0.0.1 -p "$PORT" \
             -t "homeassistant/device/$DEVICE/config" -C 1 -W 5 2>&1)"

check "the cadence comes from the config mount" 'every 5 s' \
      "$(grep -a 'hass:' "$WORK/engine.log")"

wait "$engine_pid" 2>/dev/null

# The wapp leaves without a DISCONNECT when the engine stops, thus the broker
# publishes the will.
check "the will marks the device offline" 'offline' \
      "$(mqtt mosquitto_sub -h 127.0.0.1 -p "$PORT" \
             -t "telegraph/$DEVICE/availability" -C 1 -W 5 2>&1)"

kill "$stub_pid" "$socat_pid" >/dev/null 2>&1
pkill -x wanted-cli >/dev/null 2>&1
$ENGINE rm -f tg-broker-mqtt >/dev/null 2>&1

echo "--- the engine"
grep -aE "hass:|display:|broker:" "$WORK/engine.log" | tail -6

if [ "$fail" -eq 0 ]; then
    echo "PASS: $pass checks"
    exit 0
fi

echo "FAIL: $fail of $((pass + fail)) checks"
exit 1
