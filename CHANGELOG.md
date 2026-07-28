# Changelog

All notable changes to this project go into this file.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
This project follows [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.1.0] - 2026-07-28

The first release. The firmware drives the display hardware of the HAZK-03
module, and it answers a binary protocol on the UART of the edge MCU.

### Added

#### Board support

- NuttX board support for the HAZK-03 module with the STM32F105RB.
- A clock tree from the internal oscillator at 36 MHz. The external crystal
  makes this part stop, thus the firmware never starts it.
- A release of the pin PA13 from the debug port. That pin carries the serial
  data of the sub panel.
- Two configurations. The `nsh` configuration gives an interactive shell. The
  `ipc` configuration gives the protocol and no console.

#### Display

- A TM1629A driver for the 12 clock digits.
- An SM1626D driver for the 70x14 main panel and the 21x14 sub panel.
- A DS3231 driver for the time and the temperature. The bus is software driven
  on the pins PC6 and PC7.
- The battery-backed DS3231 is the system clock, thus the time keeps its value
  through a reset.
- A scan loop that holds the image on the panels. The time for one row is
  200 us.
- A 5x7 font, and a function that draws text on a panel.
- Brightness control for the digits and the panels. The value 0 turns a device
  off. The values 1 to 8 give eight levels.

#### Protocol

- A shared framing library in `ipc/`. The code is freestanding C99, thus both
  MCUs and the host tests compile the same source.
- CRC-16/CCITT-FALSE on every frame.
- Correlation IDs, thus a caller matches a response to its request.
- Flow control from credits in each ACK. One credit gives 64 bytes of the
  receive buffer.
- Recovery from a false start-of-frame byte, and an idle timeout that clears a
  partial frame.
- A server on USART1 with these operations: set the time, set the text of each
  panel, set the brightness, request the state, and a log line as a push frame.
- The RTC keeps UTC. The frame that sets the time takes an offset in minutes,
  thus a change of the season needs no write to the RTC.
- The state frame carries the version of the firmware.
- A protocol document in `docs/ipc-protocol.md`.

#### Version

- The version comes from the git tags. A script writes it into a header at each
  build.
- The main panel shows the version for three seconds after a reset.
- A tree with local changes gets the suffix `-dirty`.

#### Tools and build

- A container image with the `arm-none-eabi` toolchain, thus a host needs only
  podman and git.
- A Makefile that builds the images of both MCUs, and sends the image of the
  edge MCU over the air.
- The flasher of the edge MCU in `tools/hazk-flasher/`. It writes the STM32
  through the system bootloader, and it gives a web interface, a network
  bridge to the target, and a peer for the protocol.
- Host tests of the framing library with the Unity framework.

### Known limitations

- The link runs at 460800 baud. At 921600 the direction to the STM32 loses
  almost all frames.
- One UART carries the console and the protocol, thus a build gives one or the
  other.
- The board keeps no settings. The edge MCU sends the UTC offset again after
  each reset of the board.
- The main panel shows 11 characters, and the sub panel shows 3. Longer text
  loses its end.
- The scan loop uses the CPU. It does not use DMA.
- The firmware gives no animation, and it reads no USB device.

[0.1.0]: https://github.com/mekops-labs/telegraph-stm-fw/releases/tag/v0.1.0
