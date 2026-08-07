#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
#
# Prove the HTTP surface of the display on a host build of the WANTED engine.
# A pty pair carries the link and stm32stub.py answers as the STM32 does, thus
# the routes run without the hardware.
#
# Usage: WANTED=<path to wanted-cli> wapps/tests/http.sh
#
# Note: the engine of that build needs CONFIG_WANTED_VFS_UART=y and
# CONFIG_WANTED_VFS_SOCKET_LISTEN=y.

set -u

HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
WANTED=${WANTED:-}
WORK=${WORK:-/tmp/tg-http-test}
PORT=${PORT:-18080}

if [ -z "$WANTED" ] || [ ! -x "$WANTED" ]; then
    echo "FAIL: set WANTED to a wanted-cli of a host build"
    exit 1
fi

SUPERVISOR=${SUPERVISOR:-$(cd "$(dirname "$WANTED")/../.." && pwd)/wasm/supervisor/wsh/supervisor.tar}

pkill -x wanted-cli >/dev/null 2>&1
rm -rf "$WORK"
mkdir -p "$WORK/registry"

for w in tg-broker tg-display; do
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

BCFG='{"console":{"in":{"name":"null"},"out":{"name":"platform"},"err":{"name":"platform"}},"drivers":[{"name":"uart","options":"port=1,dev='"$A"',baud=460800,format=8N1"}],"args":["display"]}'
DCFG='{"console":{"in":{"name":"null"},"out":{"name":"platform"},"err":{"name":"platform"}},"sockets":[{"name":"http","address":"tcp://127.0.0.1:'"$PORT"'","role":"listen","backlog":2,"max_conns":2}]}'

{
  sleep 1; echo "create tg-broker"
  sleep 1; echo "set_config tg-broker $BCFG"
  sleep 1; echo "start tg-broker"
  sleep 2; echo "create tg-display"
  sleep 1; echo "set_config tg-display $DCFG"
  sleep 1; echo "start tg-display"
  sleep 12
} | timeout 45 "$WANTED" config.json > "$WORK/engine.log" 2>&1 &
engine_pid=$!

# The wapps need a moment before the port answers.
sleep 9

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

check "GET /state" '"firmware"' \
      "$(curl -s -m 5 "http://127.0.0.1:$PORT/state")"
check "PUT /display/main" '"ok":true' \
      "$(curl -s -m 5 -X PUT --data-binary 'hello' \
             "http://127.0.0.1:$PORT/display/main")"
check "PUT /display/sub/scroll" '"ok":true' \
      "$(curl -s -m 5 -X PUT --data-binary 'a longer message' \
             "http://127.0.0.1:$PORT/display/sub/scroll")"
check "PUT /brightness" '"ok":true' \
      "$(curl -s -m 5 -X PUT --data-binary '4 6' \
             "http://127.0.0.1:$PORT/brightness")"
check "PUT /brightness rejects a level" '"error":"level"' \
      "$(curl -s -m 5 -X PUT --data-binary '99' \
             "http://127.0.0.1:$PORT/brightness")"
check "PUT /clock" '"ok":true' \
      "$(curl -s -m 5 -X PUT --data-binary "$(date +%s) 120" \
             "http://127.0.0.1:$PORT/clock")"
check "DELETE /display" '"ok":true' \
      "$(curl -s -m 5 -X DELETE "http://127.0.0.1:$PORT/display")"
check "a scroll takes its rate from the query" '"ok":true' \
      "$(curl -s -m 5 -X PUT --data-binary 'faster' \
             "http://127.0.0.1:$PORT/display/main/scroll?period=30&step=2")"
check "a query of zero is refused" '"error":"query"' \
      "$(curl -s -m 5 -X PUT --data-binary 'x' \
             "http://127.0.0.1:$PORT/display/main/scroll?period=0")"
check "an unknown route" '"error":"no route"' \
      "$(curl -s -m 5 "http://127.0.0.1:$PORT/nothing")"

wait "$engine_pid" 2>/dev/null
kill "$stub_pid" "$socat_pid" >/dev/null 2>&1
pkill -x wanted-cli >/dev/null 2>&1

echo "--- the engine"
grep -aE "display:|broker:" "$WORK/engine.log" | tail -5

if [ "$fail" -eq 0 ]; then
    echo "PASS: $pass routes"
    exit 0
fi

echo "FAIL: $fail of $((pass + fail)) routes"
exit 1
