#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
#
# Prove tg-time on a host build of the WANTED engine, without the hardware. A
# pty pair carries the link, stm32stub.py answers as the STM32 does and keeps a
# clock, and the wsh supervisor sets the clock of the engine where a case needs
# it. Three cases run: the engine takes the time of the STM32, the STM32 takes
# the time of the engine, and the STM32 resets and takes the offset again.
#
# Usage: WANTED=<path to wanted-cli> wapps/tests/time.sh
#
# Note: the engine of that build needs CONFIG_WANTED_VFS_UART=y and
# CONFIG_WANTED_VFS_RTC=y, and its supervisor is wsh.
#
# Note: the engine sets the wall clock of its host, and this test must not move
# that clock. It loads fakeclock.c into the engine, which keeps the clock that
# the test sets, thus the test needs no permission and the host keeps its time.

set -u

HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
WANTED=${WANTED:-}
WORK=${WORK:-/tmp/tg-time-test}

# The lag of an STM32 that holds an old time. Every case runs at the present
# time, because a step of the clock by days disturbs the timers of the engine
# that count in wall time. T0 is set again at the start of each case.
LAG=3600
T0=0

if [ -z "$WANTED" ] || [ ! -x "$WANTED" ]; then
    echo "FAIL: set WANTED to a wanted-cli of a host build"
    exit 1
fi

SUPERVISOR=${SUPERVISOR:-$(cd "$(dirname "$WANTED")/../.." && pwd)/wasm/supervisor/wsh/supervisor.tar}
if [ ! -f "$SUPERVISOR" ]; then
    echo "FAIL: no wsh supervisor at $SUPERVISOR (set SUPERVISOR)"
    exit 1
fi

# The engine runs with this shim in front of its clock.
FAKECLOCK=${TMPDIR:-/tmp}/tg-fakeclock-$$.so
if ! cc -shared -fPIC -o "$FAKECLOCK" "$HERE/fakeclock.c" -ldl; then
    echo "FAIL: cannot build the clock shim"
    exit 1
fi
trap 'rm -f "$FAKECLOCK"' EXIT

failures=0

# run_case <name> <stub clock> <stub reset at> <engine setup command or ''>
#          <loops>
# Leaves the output of the engine in $WORK/out and the log of the stub in
# $WORK/stub.log.
run_case() {
    local name=$1 stub_lag=$2 reset_at=$3 setup=$4 loops=$5
    local stub_clock=$((T0 - stub_lag))

    echo "=== $name"
    pkill -x wanted-cli >/dev/null 2>&1
    rm -rf "$WORK"
    mkdir -p "$WORK/registry"

    for w in tg-broker tg-time; do
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

    cat > "$WORK/config.json" <<CFG
{
  "system": {"privileged": true},
  "supervisor": {
    "imagePath": "$SUPERVISOR",
    "params": {
      "console": {"in": {"name": "platform"}, "out": {"name": "platform"},
                  "err": {"name": "platform"}},
      "drivers": [{"name": "wanted"},
                  {"name": "rtc", "options": "devices=main,set"}]
    }
  }
}
CFG

    cd "$WORK" || exit 1
    nohup socat pty,raw,echo=0,link="$WORK/ptyA" pty,raw,echo=0,link="$WORK/ptyB" \
        >/dev/null 2>&1 &
    local socat_pid=$!
    sleep 1
    local A B
    A=$(readlink -f ptyA)
    B=$(readlink -f ptyB)

    STUB_TIME=$stub_clock STUB_RESET_AT=$reset_at \
        nohup python3 "$HERE/stm32stub.py" "$B" > "$WORK/stub.log" 2>&1 &
    local stub_pid=$!
    sleep 1

    local BCFG TCFG
    BCFG='{"console":{"in":{"name":"null"},"out":{"name":"platform"},"err":{"name":"platform"}},"drivers":[{"name":"uart","options":"port=1,dev='"$A"',baud=460800,format=8N1"}],"args":["time"]}'
    TCFG='{"console":{"in":{"name":"null"},"out":{"name":"platform"},"err":{"name":"platform"}},"drivers":[{"name":"rtc","options":"devices=main,set"}],"envs":["TELEGRAPH_TIME_INTERVAL_S=1","TELEGRAPH_TIME_LOOPS='"$loops"'"]}'

    {
        sleep 1; echo "create tg-broker"
        sleep 1; echo "set_config tg-broker $BCFG"
        sleep 1; echo "start tg-broker"
        [ -n "$setup" ] && { sleep 1; echo "$setup"; }
        sleep 2; echo "create tg-time"
        sleep 1; echo "set_config tg-time $TCFG"
        sleep 1; echo "start tg-time"
        sleep $((loops + 6))
        echo "cat /dev/rtc/main/source"
        sleep 1
        echo "cat /dev/rtc/main/time"
        sleep 2
    } | LD_PRELOAD="$FAKECLOCK" timeout 60 "$WANTED" config.json \
        > "$WORK/out" 2>&1

    kill "$stub_pid" "$socat_pid" >/dev/null 2>&1
    pkill -x wanted-cli >/dev/null 2>&1
    cd "$HERE" || exit 1
    cat "$WORK/out"
    echo "--- the stub"
    cat "$WORK/stub.log"
}

# expect <file> <pattern> <what>
expect() {
    if grep -q "$2" "$1"; then
        echo "ok: $3"
    else
        echo "FAIL: $3"
        failures=$((failures + 1))
    fi
}

# expect_not <file> <pattern> <what>
expect_not() {
    if grep -q "$2" "$1"; then
        echo "FAIL: $3"
        failures=$((failures + 1))
    else
        echo "ok: $3"
    fi
}

# Whether a number is the time of the case, within a minute.
near_t0() {
    local t=$1
    [ $((T0 - t)) -le 60 ] && [ $((t - T0)) -le 60 ]
}

# The engine holds no time, and the STM32 holds one: the engine takes it, and
# the wapp then sends the offset back, which is due at the start.
T0=$(date +%s)
run_case "the engine takes the time of the STM32" 0 0 "" 3
expect "$WORK/out" "set the engine clock to [0-9]* from the STM32" \
    "the wapp wrote the time of the STM32 to the engine"
expect "$WORK/out" "^rtc" "the source of the engine reads rtc"
expect "$WORK/stub.log" "set time [0-9]* offset 0" \
    "the wapp sent the offset to the STM32"

# A person sets the engine clock, and the STM32 holds an hour less: the engine
# wins.
T0=$(date +%s)
run_case "the STM32 takes the time of the engine" $LAG 0 \
    "write /dev/rtc/main/time $T0 manual" 3
expect "$WORK/stub.log" "set time [0-9]* offset 0" \
    "the wapp sent the time of the engine to the STM32"
sent=$(sed -n 's/^stub: set time \([0-9]*\) offset.*/\1/p' "$WORK/stub.log" | head -1)
if [ -n "$sent" ] && near_t0 "$sent"; then
    echo "ok: the STM32 took the time of the engine"
else
    echo "FAIL: the STM32 took '$sent', not the time of the engine"
    failures=$((failures + 1))
fi
expect_not "$WORK/out" "from the STM32" \
    "the wapp did not write the engine clock back"
expect "$WORK/out" "^manual" "the source of the engine still reads manual"

# The clocks agree. The STM32 resets after three frames, and its counter falls:
# the wapp sends the time and the offset again.
T0=$(date +%s)
run_case "the STM32 resets and takes the offset again" 0 3 \
    "write /dev/rtc/main/time $T0 manual" 5
sent=$(grep -c "set time" "$WORK/stub.log")
if [ "$sent" -ge 2 ]; then
    echo "ok: the offset went out at the start and again after the reset"
else
    echo "FAIL: the offset went out $sent time(s), expected 2 or more"
    failures=$((failures + 1))
fi
expect "$WORK/stub.log" "stub: reset" "the stub reset"

if [ "$failures" -eq 0 ]; then
    echo "PASS: tg-time moves the time between the engine and the STM32"
    exit 0
fi

echo "FAIL: $failures check(s) failed"
exit 1
