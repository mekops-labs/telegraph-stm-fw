# Telegraph Firmware

Firmware for the Telegraph LED matrix clock and notification display.

Telegraph is a device with two MCUs. An STM32F105RB (the HAZK-03 module)
drives all the display hardware. An edge MCU next to it holds the network
stack. A binary protocol with frames connects the two MCUs over a UART.

**This repository holds the STM32 side.** It uses
[Apache NuttX](https://nuttx.apache.org/). It also holds the shared IPC
library for both sides.

`CHANGELOG.md` gives the content of each release.

## Display hardware

| Device | Role | Interface |
| :--- | :--- | :--- |
| TM1629A | 7-segment clock digits | 3-wire serial (PB3/PB4/PB5) |
| SM1626D | 70x14 main matrix, 21x14 sub matrix | Shift register (PB12–PB15, PA13) |
| DS3231 | RTC and ambient temperature | Bit-banged I²C (PC6/PC7) |

### Mandatory constraints

The list below gives properties of the board. Each one is verified on the
hardware. An error in one of them makes the board stop, or gives a corrupt
display.

- **The board carries a crystal of 25 MHz, and the clock comes from it.** The
  path is `HSE 25 MHz -> PREDIV2 /5 -> PLL2 x8 -> PREDIV1 /5 -> PLL x9`, thus
  a SYSCLK of 72 MHz. A divider of 2 keeps APB1 at its maximum of 36 MHz, and
  the flash takes two wait states.

  The USB host needs this path. That peripheral takes a clock of exactly
  48 MHz, and a PLL on the crystal is its only source. The internal oscillator
  reaches 36 MHz and no higher, because its divider before the PLL is fixed at
  two and the largest multiplier on this part is nine.

  The option `HAZK03_CLOCK_HSE` selects the source. Without it the clock comes
  from the internal oscillator at 36 MHz, and the USB host has no clock. A
  build for that path also needs `BOARD_LOOPSPERMSEC` at half its present
  value, because the short waits of the drivers count instructions.

  Note: a build with the wrong crystal frequency drives the PLL far outside
  its range and the part stops. The value of 25 MHz is measured on the
  hardware and not read from a marking.
- **PA13 needs a release from the debug port.** This pin carries the serial
  data of the sub-screen. Thus the firmware writes `AFIO->MAPR` to disable JTAG
  and SWD before it configures the pin as a GPIO.

  Note: after that step a hardware debugger cannot connect. A reset makes the
  debug port available again.
- **Software drives the DS3231 bus.** The pins PC6 and PC7 have no connection
  to an I²C peripheral on this part. A change to a hardware I²C needs a new
  board.
- **The matrix scan must not stop.** The panel keeps an image only during a
  scan. There are 16 rows, and the time for one row is 200 µs. The frame rate
  is thus 312 Hz.
- **Nothing runs above the thread that shifts the rows.** That thread takes the
  priority 95, and a thread above it interrupts a row in the middle. Both panels
  share the clock, the strobe and the output-enable lines, thus such an
  interruption leaves random pixels on both of them.

  The work queues therefore take priorities below it: `SCHED_HPWORKPRIORITY` is
  70 and `SCHED_LPWORKPRIORITY` is 50. The classes of the USB host run their
  work there, and a device that streams keeps those queues busy.

  Note: the two values must differ by 16 or more. `SCHED_LPWORKPRIOMAX` comes
  from `SCHED_HPWORKPRIORITY` less 16, and the build stops when the low priority
  is above that limit.
- **The TM1629A digit positions are not adjacent.** The digits use the shift
  register bits `{0..9, 12, 13}`. The bits 10 and 11 drive decorative LEDs.

## Build

The container image holds the toolchain. Thus a host needs only podman (or
docker) and git.

```sh
git submodule update --init --depth 1   # NuttX + nuttx-apps
make image                              # toolchain container
make build                              # configure + build
```

The repository holds the firmware of both MCUs. The target `make build` gives
the STM32 image. The target `make flasher` gives the image of the edge MCU in
`tools/hazk-flasher/`, and `make all` gives both.

The command `make help` gives the other targets. These are `menuconfig`,
`savedefconfig`, `clean` and `shell`.

Note: every target runs in the container. The image holds the `arm-none-eabi`
cross toolchain and the packages that NuttX needs. The build uses ccache.

Note: the edge MCU has its own toolchain, thus it has its own image. The
target `make flasher-image` builds it.

### Version

The version of the firmware comes from the git tags. The script
`tools/genversion.sh` writes it into a generated header at every build, thus
the repository does not hold that header.

A build from a tag gives that tag, such as `v0.1.0`. A build from a later
commit adds the count of the commits and the short hash. A tree with local
changes gets the suffix `-dirty`. A repository without a tag gives the short
hash alone.

The main panel shows the version for three seconds after a reset. A `0x11`
state frame also carries it, thus the edge MCU reads the version of the
running image. Refer to [IPC library](#ipc-library).

Note: the panel holds 11 characters, thus a longer version loses its end.

### The flash of the module

A Winbond W25Q32 of 4 MB sits on SPI1. One erase sector is 4096 bytes, and the
driver gives blocks of 256 bytes. Thus one erase sector is 16 blocks.

| Partition | Sectors | Size | Content |
| :--- | :--- | :--- | :--- |
| settings | 0 to 1 | 8 KB | Two records, one for each sector |
| assets | 2 to 1023 | 4088 KB | A SmartFS file system at `/assets` |

**The settings partition is not a file system.** Each sector holds one record
with a check value. A write goes to the sector that the board does not use,
and the board then takes the record with the higher sequence number. Thus a
loss of power during a write keeps the record from before that write.

The cost of one write is one erase cycle on one sector. A write with the same
values does nothing, thus a setting that does not change takes no cycle. The
part gives at least 100 000 cycles for each sector, and the two sectors take
the writes in turn. **The store thus accepts at least 200 000 changes.** A
change comes from a person or from the edge MCU, thus that number is far above
the life of the device.

Note: a new field of the settings joins the end of the structure, and never
the middle. The board reads a record of an older firmware as the first bytes
of the new structure, and the fields that the record lacks take their
defaults. Thus a step of the firmware loses no setting.

**The assets partition carries a file system**, because the edge MCU sends its
content and the number and the size of those files change. SmartFS gives the
wear levelling. A new board has no file system there, thus the board makes an
empty one at the first start.

Note: the file system costs near 13 KB of RAM for this partition. That is the
largest single user of memory on the board.

### Fonts

The firmware holds a font of 5x7 for the ASCII table. That font is the
fallback, thus a board with an empty flash keeps a legible display.

The extended font holds the letters outside the ASCII table, and the board
reads it from `/assets/fonts/default.tgf` in its flash. Its cell has 10 rows
for a letter of 7 rows: two rows above the letter carry a mark such as an
acute, and one row below carries a mark such as an ogonek. Thus a letter keeps
its full height.

```sh
make font                   # build it, and print the command that sends it
```

The tool `tools/mkfont.py` takes the shapes of the ASCII table from the source
of the firmware, and it puts a mark on some of them. Thus the two fonts keep
one set of shapes.

Note: a text of the protocol is in UTF-8. A character that neither font holds
gives a space.

**The compact font gives two lines.** The font of 5x7 takes 10 rows of a panel
of 14, thus one line fills it. The compact font takes 5 rows for a letter in a
cell of 7, and two of those fit.

```sh
make compactfont            # build it, and print the command that sends it
make sprites                # build the sprites for the animation
```

The board takes a font from the flash at any time, thus one panel shows a large
line and later two small ones.

A request names a font alone, such as `compact`, and a sprite alone, such as
`heart`. The board keeps the fonts in `/assets/fonts` and the sprites in
`/assets/animations`, and it takes one format for each kind. Thus the name
needs neither a directory nor an ending. **A request without a name gives the
names that the board holds.**

### Board configuration

The board directory is `boards/hazk03-stm32f105rb/`. The build keeps it
**outside the NuttX tree**. NuttX finds it through
`CONFIG_ARCH_BOARD_CUSTOM_DIR` in the defconfig. Thus the build does not modify
the submodules, and a rebase stays simple.

The file `stm32_clockconfig.c` replaces the standard clock setup. The
connectivity-line code in the NuttX tree waits for each PLL without a limit,
thus a clock that never comes up stops the boot with no sign of the cause.
Every wait in this file has an end, and a failure leaves the internal
oscillator running.

**The board builds through `configure.sh` and make.** NuttX also carries a
CMake build, and a board in its tree gives a file for each of the two. This
board gives the make file alone, because `make build` is the only path that the
repository uses. A second list of the sources goes stale without a build that
reads it.

### The USB host port

The OTG FS peripheral drives the module's USB connector as a host. The board
enumerates a mass storage device and mounts its FAT file system at `/media`,
thus the edge MCU reads and writes that device over the protocol. Refer to
[IPC protocol](docs/ipc-protocol.md).

The peripheral takes a clock of exactly 48 MHz, and a PLL on the crystal is its
only source. A build without `HAZK03_CLOCK_HSE` therefore carries no USB host.

The pins PA11 and PA12 carry the two data lines. The peripheral also has a VBUS
input on PA9 and an identifier line on PA10, and those two pins carry the UART
of the edge MCU — a host takes the identifier from its role, and the core takes
an internal VBUS state, thus neither pin belongs to this peripheral here. The
option `STM32_OTGFS_VBUS_CONTROL` must stay off: it calls for a GPIO that drives
a power switch, and the module carries no such switch.

`FAT_LFN` gives the long name of a file, thus a drive that a person prepares on
a computer keeps its names. One name holds 32 characters, which is the limit of
`NAME_MAX` and the limit of SmartFS as well.

Note: with that option a name matches by case. A name that another system wrote
in the 8.3 form is upper case, thus a request in lower case reaches no file.

Note: NuttX flags `FAT_LFN` with a patent notice from Microsoft. Refer to the
`NOTICE` file of that project.

The port also takes a serial device of the class CDC/ACM, which the board gives
to the edge MCU as a channel. A device with a serial chip of a vendor — an FTDI
part, a CP2102, a CH340 — needs a driver that NuttX does not carry, thus it
gives no channel.

Note: the build takes the reduced protocol of that class, which uses the two
bulk endpoints of the data interface alone. The compliant protocol depends on
`SERIAL_OFLOWCONTROL`, and a UART driver alone selects that option. A device
that follows the standard works either way, and the endpoint of its
notifications stays unused.

## IPC library

The directory `ipc/` holds the protocol that connects the two MCUs. The code
is freestanding C99. It calls no allocator, and it needs no RTOS. Thus the
STM32 firmware, the edge MCU and the host tests all compile the same source.

Every frame has this form. All 16-bit fields are little-endian.

| Offset | Size | Field |
| :--- | :--- | :--- |
| 0 | 1 | SOF, always `0xAA` |
| 1 | 2 | LEN, the length of the payload |
| 3 | 1 | OPCODE |
| 4 | 2 | CORRELATION\_ID |
| 6 | n | PAYLOAD |
| 6+n | 2 | CRC16 |

The CRC is CRC-16/CCITT-FALSE. It covers the bytes from LEN to the end of the
payload. The SOF is a constant, thus it adds no information.

The link has no RTS/CTS signal. An ACK frame carries a credit count, and this
count is the only flow control. A sender with zero credits stops until the
next ACK.

### Idle timeout

The receiver must detect an idle line, and it must then call
`ipc_parser_timeout()`.

Note: corrupt data sometimes gives a false SOF with a length that is possible.
The parser then waits for a frame that no sender transmits. A good frame
behind it stays in the buffer until the timeout removes the false candidate.

### Tests

```sh
make test
```

The tests use [Unity](https://www.throwtheswitch.org/unity). They run with the
host compiler, thus they need no hardware.

### Formatting

```sh
make lint-format   # reject any drift, the check CI would run
make format-fix    # reformat boards/ and ipc/ in place
```

The style is `clang-format` with the config in `.clang-format`, the same
style as the `wanted-engine` repository. `third_party/` is vendored NuttX
and stays as upstream ships it.

### Static analysis

```sh
make tidy       # clang-tidy the host-buildable ipc/ sources
make cppcheck   # cppcheck boards/ and ipc/
```

`tidy` covers `ipc/` only: `boards/` needs the ARM cross toolchain and the
full NuttX header tree, and clang-tidy has no compilation database for that
build. `cppcheck` parses source directly and covers both directories; it
resolves NuttX's headers through the same generated `nuttx/config.h` and
`arch/board`/`arch/chip` symlinks a real build uses, so it needs `configure`
to have run at least once.

`ipc/` holds its own `.clang-tidy`, with `InheritParentConfig: true` layering
`readability-identifier-length` and `readability-magic-numbers` on top of the
root config. `ipc/` is the shared, portable framing library both MCUs and the
host tests compile, so it is held to a stricter bar than the board-specific
code in `boards/`. `make tidy` passes no `--config-file`, so clang-tidy finds
the nearest `.clang-tidy` for each file on its own.

## License

The license is Apache-2.0. Refer to [LICENSE](LICENSE) and [NOTICE](NOTICE).

Note: every `.c` and `.h` file outside `third_party/` starts with the line
`/* SPDX-License-Identifier: Apache-2.0 */`.
