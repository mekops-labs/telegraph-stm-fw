# The clock of the device

Two clocks run on the device. The DS3231 on the STM32 keeps its time through a
power cycle, because a battery holds it. The engine on the edge MCU has no
battery-backed clock, thus it starts at the year 1970. The wapp `tg-time` keeps
the two together. It is a peer of [the broker](broker.md) with the name
`time`.

## Which clock the wapp writes

The wapp reads three nodes of the engine clock, `/dev/rtc/main/source`,
`status` and `time`, and it asks the STM32 for its state. It then decides:

| The engine clock | The wapp |
| :--- | :--- |
| Has no source (`none`), a source the wapp does not know, or reads invalid | Writes the time of the STM32 to the engine, with the source `rtc`, when that time is plausible. |
| Has the source `sntp`, `server` or `manual` | Sends its time to the STM32 when the two differ by more than 2 s, when the STM32 holds no plausible time or does not answer, or when the offset is due. |
| Has the source `rtc` | Writes neither clock. It sends the time and the offset to the STM32 only when the offset is due. |

The wapp never writes the engine clock back when the control plane or a person
set it, because those sources are the more accurate ones. Sheriff sets that
clock from the time of the control plane and from SNTP.

A time of the STM32 is plausible from 2025-01-01 to 2099-12-31. A DS3231 that
lost its battery reads the year 2000, thus the wapp does not take that time.

## The offset

The DS3231 keeps UTC, and the panels show the local time. The offset between
the two travels in the same frame as the time (`SET_TIME`, see
[the protocol](ipc-protocol.md)), and the STM32 keeps it in its flash. A write
with the same values does nothing, thus a repeat costs no erase cycle.

The wapp reads the offset from a config mount at `/etc/tg-time.conf`:

```
offset_min=60
```

The value is in minutes from UTC, from -840 to 840, and its default is 0. A
value that is not a number or above the limit gives 0, and the wapp writes a
line to its console. The wapp sends the offset at its start and after each
reset of the STM32. It detects a reset when the count of the accepted frames in
the state falls.

Note: a change of the season needs a new value in the config and a restart of
the wapp. The wapp does not follow a rule of the time zone.

## The grants

| Grant | Why |
| :--- | :--- |
| `{"name":"rtc","options":"devices=main,set"}` | The wapp reads the engine clock and writes it. |
| The peer `time` in the arguments of `tg-broker` | The pipes `/dev/pipe/tg-time-req` and `tg-time-rsp`. |

The peer asks for no frame that nobody requested.

## The test

```sh
make wapp-test WANTED=<path to a wanted-cli>
```

The script `wapps/tests/time.sh` runs three cases on a host build of the engine:
the engine takes the time of the STM32, the STM32 takes the time of the engine,
and the STM32 resets and takes the offset again. The host build needs
`CONFIG_WANTED_VFS_UART=y` and `CONFIG_WANTED_VFS_RTC=y`. The test loads
`wapps/tests/fakeclock.c` into the engine, thus it never moves the clock of the
host.

The policy is pure code in `wapps/lib/timepolicy.c`, and `make test` runs its
unit tests with the host compiler.
