#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
#
# Prove the broker on a host build of the WANTED engine, without the hardware.
# A pty pair carries the link, stm32stub.py answers as the STM32 does, and the
# wapp tg-probe drives a request, a frame that no request asked for, and raw
# mode.
#
# Usage: WANTED=<path to wanted-cli> wapps/tests/roundtrip.sh
#
# Note: the engine of that build needs CONFIG_WANTED_VFS_UART=y, and its
# supervisor is wsh. The registry of the run is a directory of this script.

set -u

HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
WANTED=${WANTED:-}
WORK=${WORK:-/tmp/tg-broker-test}

if [ -z "$WANTED" ] || [ ! -x "$WANTED" ]; then
    echo "FAIL: set WANTED to a wanted-cli of a host build"
    exit 1
fi

SUPERVISOR=${SUPERVISOR:-$(cd "$(dirname "$WANTED")/../.." && pwd)/wasm/supervisor/wsh/supervisor.tar}
if [ ! -f "$SUPERVISOR" ]; then
    echo "FAIL: no wsh supervisor at $SUPERVISOR (set SUPERVISOR)"
    exit 1
fi

pkill -x wanted-cli >/dev/null 2>&1
rm -rf "$WORK"
mkdir -p "$WORK/registry"

for w in tg-broker tg-probe; do
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

BCFG='{"console":{"in":{"name":"null"},"out":{"name":"platform"},"err":{"name":"platform"}},"drivers":[{"name":"uart","options":"port=1,dev='"$A"',baud=460800,format=8N1"}],"args":["probe:0x03"]}'
PCFG='{"console":{"in":{"name":"null"},"out":{"name":"platform"},"err":{"name":"platform"}},"envs":["TELEGRAPH_FULL=1"]}'

out=$({
  sleep 1; echo "create tg-broker"
  sleep 1; echo "set_config tg-broker $BCFG"
  sleep 1; echo "start tg-broker"
  sleep 2; echo "create tg-probe"
  sleep 1; echo "set_config tg-probe $PCFG"
  sleep 1; echo "start tg-probe"
  sleep 8
} | timeout 40 "$WANTED" config.json 2>&1)

kill "$stub_pid" "$socat_pid" >/dev/null 2>&1
pkill -x wanted-cli >/dev/null 2>&1

echo "$out"
echo "--- the stub"
cat "$WORK/stub.log"

# The probe reports what it received. Three replies are the state, the entry
# into raw mode and the exit from it; two pushes are the log line of the board
# and the bytes that came back through raw mode.
if echo "$out" | grep -q "probe: 3 replies, 2 pushes"; then
    echo "PASS: the broker carries requests, replies, pushes and raw mode"
    exit 0
fi

echo "FAIL: the probe did not complete the round trip"
exit 1
