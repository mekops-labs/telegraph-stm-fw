# Telegraph Firmware

Firmware for the Telegraph LED matrix clock and notification display.

Telegraph is a device with two MCUs. An STM32F105RB (the HAZK-03 module)
drives all the display hardware. An edge MCU next to it holds the network
stack. A binary protocol with frames connects the two MCUs over a UART.

**This repository holds the STM32 side.** It uses
[Apache NuttX](https://nuttx.apache.org/). It also holds the shared IPC
library for both sides.

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

- **The system clock is 36 MHz. It must use the HSI oscillator.** The external
  crystal makes the STM32F105 on this board stop. Thus the clock path is
  `HSI/2 -> PLL x9 -> 36 MHz`. A divider of 2 keeps APB1 below its maximum of
  36 MHz. Do not enable the HSE.
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

### Board configuration

The board directory is `boards/hazk03-stm32f105rb/`. The build keeps it
**outside the NuttX tree**. NuttX finds it through
`CONFIG_ARCH_BOARD_CUSTOM_DIR` in the defconfig. Thus the build does not modify
the submodules, and a rebase stays simple.

The file `stm32_clockconfig.c` replaces the standard clock setup. The
connectivity-line code in the NuttX tree always drives the PLL from the HSE
input. This board cannot use that input.

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

## License

The license is Apache-2.0. Refer to [LICENSE](LICENSE) and [NOTICE](NOTICE).

Note: every `.c` and `.h` file outside `third_party/` starts with the line
`/* SPDX-License-Identifier: Apache-2.0 */`.
