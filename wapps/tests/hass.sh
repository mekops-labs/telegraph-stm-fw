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
# slots. The broker image is eclipse-mosquitto.

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
  "system": {"privileged": true},
  "supervisor": {
    "imagePath": "$SUPERVISOR",
    "params": {
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

nohup python3 "$HERE/stm32stub.py" "$B" > "$WORK/stub.log" 2>&1 &
stub_pid=$!
sleep 1

BCFG='{"console":{"in":{"name":"null"},"out":{"name":"platform"},"err":{"name":"platform"}},"drivers":[{"name":"uart","options":"port=1,dev='"$A"',baud=460800,format=8N1"}],"args":["display"]}'
DCFG='{"console":{"in":{"name":"null"},"out":{"name":"platform"},"err":{"name":"platform"}},"args":["hass"]}'
HCFG='{"console":{"in":{"name":"null"},"out":{"name":"platform"},"err":{"name":"platform"}},"sockets":[{"name":"mqtt","address":"tcp://127.0.0.1:'"$PORT"'"}],"envs":["HASS_DEVICE='"$DEVICE"'","HASS_PREFIX=telegraph"]}'

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
  sleep 20
} | timeout 70 "$WANTED" config.json > "$WORK/engine.log" 2>&1 &
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
check "the device is available" 'online' \
      "$(mqtt mosquitto_sub -h 127.0.0.1 -p "$PORT" \
             -t "telegraph/$DEVICE/availability" -C 1 -W 5 2>&1)"

mqtt mosquitto_pub -h 127.0.0.1 -p "$PORT" \
     -t "telegraph/$DEVICE/main/set" -m "hello from HA" >/dev/null 2>&1
sleep 3

check "a command reaches the board" 'opcode 0x13' "$(cat "$WORK/stub.log")"
check "the entity reads back what was set" 'hello from HA' \
      "$(mqtt mosquitto_sub -h 127.0.0.1 -p "$PORT" \
             -t "telegraph/$DEVICE/main" -C 1 -W 5 2>&1)"

mqtt mosquitto_pub -h 127.0.0.1 -p "$PORT" \
     -t "telegraph/$DEVICE/brightness/panels/set" -m "6" >/dev/null 2>&1
sleep 3

check "a brightness command reaches the board" 'opcode 0x11' \
      "$(cat "$WORK/stub.log")"

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
