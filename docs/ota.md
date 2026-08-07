# The firmware of the STM32, over the air

The STM32 takes its firmware through the bootloader of its ROM, over the same
wire that carries [the protocol](ipc-protocol.md). The wapp `tg-ota` does that
from the edge MCU: it carries the image in its own package, drives BOOT0 and
NRST through a `gpio` grant, and takes the line from [the broker](broker.md)
for the duration.

**The firmware is a file of the wapp, thus a new firmware is a new version of
that wapp.** Nothing in the control plane knows the STM32 exists: deploying
`tg-ota` deploys the firmware behind it.

## What it does

1. Reads `/firmware.bin` and `/firmware.version` from its own package.
2. Asks the board for its state through the broker. A version equal to
   `/firmware.version` ends the run, thus a restart of the wapp writes nothing.
3. Takes raw mode from the broker at 57600 8E1, which the bootloader needs.
4. Holds BOOT0 high, pulses NRST, and sends the synchronisation byte.
5. Reads the identifier of the part, erases the flash, and writes the image in
   blocks of 256 bytes.
6. Drops BOOT0, pulses NRST again, gives the line back at 460800 8N1, and asks
   for the state once more. The version it reads is the result of the run.

The image is read a block at a time. A wapp holds 64 KiB of linear memory, thus
a buffer of the whole firmware would not fit beside the rest of it.

## The grants

```json
{"drivers": [{"name": "gpio",
              "options": "pins=boot0:4:out,nrst:5:out:init=1:drive=od"}]}
```

`init=1` is mandatory on NRST. Without it the pin drives low as the grant opens
it and the STM32 stays in reset for as long as the wapp runs. Open-drain is the
preferred mode: the pull-up of the target sets the idle level.

The wapp takes no `uart` grant. The broker holds the port, and raw mode is what
gives this wapp the line — thus one wapp holds the port and the display keeps
working between the runs of the firmware.

## The build

```sh
make ota-image    # build the STM32 firmware and stage it into the wapp
make wapp-images  # package every wapp, this one with its firmware inside
```

`ota-image` copies the image and the version of the current build into
`wapps/tg-ota/root/`, which the packaging step puts at the root of the wapp.
Those files are build outputs and the repository does not hold them.

Note: the version comes from `git describe`, and the comparison is exact. Thus
a build from a tree with local changes carries the suffix `-dirty`, never
matches the board, and writes the flash at every start.
