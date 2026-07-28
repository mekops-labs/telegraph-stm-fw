# Changelog

All notable changes to this project go into this file.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
This project follows [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- The board makes an empty file system on the partition of the assets when the
  mount fails. Thus a new board needs no step by hand.

### Changed

- **The record of the settings carries a version and a length.** A field joins
  the end of the structure alone, thus the board reads a record of an older
  firmware as the first bytes of the new structure. The fields that the record
  lacks take their defaults. A step of the firmware thus loses no setting.
- This layout replaces the one of 0.1.1, and it does so one time. A board that
  comes from 0.1.1 starts from the default settings.

## [0.1.1] - 2026-07-28

### Added

- The W25Q32 serial flash on SPI1, in two partitions. The first two sectors
  keep the settings. The sectors that come after them keep a SmartFS file
  system at `/assets` for the fonts, the icons and the animations.
- The board keeps the settings through a loss of power. The store holds the
  offset of the local time, the two brightness levels, the correction of the
  temperature and the period without light.
- A store with two records. A write goes to the record that the board does not
  use, thus a loss of power keeps the record from before that write.
- The operation `0x0A` corrects the temperature, in tenths of a degree.
- The operation `0x0B` sets a period without light, such as 23:00 until 06:00.
  The period does not change the brightness, thus the display takes its
  previous levels at the end of the period.
- The operation `0x0C` writes a file into the assets, and it makes the
  directory of that file.
- The panels carry the content of the board until the edge MCU sends a text.
  The main panel gives a greeting, and the sub panel gives the day of the week.
- An extended font at `/assets/fonts/default.tgf`, for the letters outside the
  ASCII table. Its cell has 10 rows for a letter of 7 rows: two rows above the
  letter carry a mark such as an acute, and one row below carries a mark such
  as an ogonek. Thus a letter keeps its full height.
- The tool `tools/mkfont.py` builds that font, and the target `make font` calls
  it. The tool takes the shapes of the ASCII table from the source of the
  firmware, thus the two fonts keep one set of shapes.
- The Makefile builds the firmware of both MCUs, and it sends the firmware of
  the edge MCU over the air.

### Fixed

- The configuration with the shell now links. The panel shows the version in
  each configuration, and the font was in the group of files for the protocol
  alone.

### Changed

- The edge MCU sends the offset of the local time one time, and not after each
  reset of the board.
- **The texts of the panels now start with a byte of attributes.** The bits 0
  and 1 of that byte give the place of the text across the panel. A byte of 0
  puts the text in the middle, and earlier versions always put it at the left.
  This change breaks the earlier layout. The protocol is experimental and both
  MCUs come from the same source, thus its version stays 1.
- A text of the panels is in UTF-8. A character that neither font holds gives a
  space.

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
  each reset of the board. *(0.1.1 adds a store.)*
- The main panel shows 11 characters, and the sub panel shows 3. Longer text
  loses its end.
- The scan loop uses the CPU. It does not use DMA.
- The firmware gives no animation, and it reads no USB device.

[0.1.1]: https://github.com/mekops-labs/telegraph-stm-fw/releases/tag/v0.1.1
[0.1.0]: https://github.com/mekops-labs/telegraph-stm-fw/releases/tag/v0.1.0
