# Changelog

All notable changes to this project go into this file.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
This project follows [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- The USB port reads a mass storage device. The board mounts its FAT file
  system at `/media`.
- The edge MCU lists a directory, reads a file, removes an entry and creates a
  directory, on the flash of the board and on the USB device alike. Opcodes
  `0x14` to `0x17`.
- Long file names on the USB device, up to 32 characters. A name there matches
  by case.
- A serial device of the class CDC/ACM on the USB port reaches the edge MCU as
  a channel. Opcodes `0x30` to `0x34`.
- A write to a channel carries a sequence, so a request sent again after a lost
  reply adds no bytes to the stream a second time.

### Changed

- A task of its own waits on each channel of the USB port and gives its bytes
  to the server through a ring. The server no longer reads a channel between
  two waits on the link, and a device that the board opens stays open.

### Fixed

- A path from the edge MCU must start with `/assets` or `/media`, so the
  settings and the raw devices stay out of reach of the link.
- The work queues run below the row shifter. A device that streams over the USB
  port used to interrupt a row and leave both panels showing random pixels.
- The server of the protocol serves the link before a channel of the USB port,
  and no path through its loop spins. A wait that returned at once used to stop
  the link and the display together.
- A channel of the USB port carries its bytes at a usable rate. That rate came
  from a delay of the class which only a reader that blocks avoids.
- A channel of the USB port no longer repeats a character. The class took one
  byte a second time whenever its receive buffer filled in the middle of a
  packet, and the fix is in the NuttX submodule.

## [0.3.0] - 2026-07-30

### Added

- `SET_TEXT` scrolls a text that overflows its panel, instead of truncating it.
- A sprite's animation window may take its width and height from the sprite's
  own file. The peer's `play` command gains a short form that omits them.
- A scrolling text carries a blank gap, so it does not run into itself on wrap.
- An overflowing `SET_TEXT` wraps across the compact font's two lines at a
  word boundary, and scrolls down by rows if the wrap itself still overflows.

### Changed

- The system clock runs at 72 MHz from the board's 25 MHz crystal, twice the
  previous rate and the source of the USB host's 48 MHz clock. `HAZK03_CLOCK_HSE`
  reverts to the internal 36 MHz oscillator.

### Fixed

- A lost ACK no longer stops the link; the sender now recovers a credit after
  a wait that no ACK ends.

## [0.2.0] - 2026-07-28

### Added

- The board formats the assets partition when its mount fails, so a new
  board needs no manual setup step.
- A font or sprite request with no name returns the names the board holds,
  so the edge MCU can query the board's inventory.
- Animation on both panels: the board scans a window over a source larger
  than the panel. A one-pixel step scrolls, and a panel-width step advances
  a sprite frame. One mechanism drives both, so the edge MCU sends a single
  frame for a scroll of any length. A source larger than its panel returns
  a NACK instead of overflowing.
- Text sources render to a bitmap on the board using the extended font, so
  a scrolling message costs one protocol frame regardless of length.
- Double-buffered images per panel: a write updates the buffer the scan is
  not reading, and the scan swaps buffers between frames, so no partial
  update reaches the panel. Opcodes `0x08` and `0x09` write a pixel
  rectangle into a buffer.
- A compact font (`compact`) that fits two lines on a 14-row panel, and two
  built-in sprites, a beating heart and a shining sun. The board reads all
  three from flash, so none cross the link during playback.
- Requests retry up to three times on no reply, keeping the same
  correlation ID so a late reply still resolves the request. Every
  protocol operation is idempotent, so a retry causes no harm.
- The matrix scan now runs from a timer interrupt instead of a busy-wait
  thread. Idle CPU time rose from near 0% to near 77%.
- One pass now writes a row to both panels, since they share the clock,
  strobe, and output-enable lines and differ only in the data line.
  Neither panel blanks while the other panel's row loads.

### Changed

- **The settings record now carries a version and a length.** New fields
  append to the struct, so the board reads an older-firmware record as a
  prefix of the new struct and defaults the missing fields. A firmware
  update loses no setting.
- This layout replaces the 0.1.1 layout once. A board upgrading from 0.1.1
  starts from default settings.
- **Font and sprite requests now use a name only, not a path.** Each asset
  type has one directory and one format, so the name needs no directory or
  extension. A name containing a separator returns a NACK.
- **The panel is now the payload's first byte, not an opcode choice.** Each
  opcode pair collapses to one opcode, and clearing a panel becomes one
  operation with an argument. The rule applies to every panel-addressed
  operation.
- The row-transfer thread now runs below the protocol task. The prior order
  let a frame burst fill the receive buffer behind a row wait, dropping
  every frame past that point.
- The extended font's cell holds 10 rows for a 7-row letter: 2 rows above
  carry diacritics like an acute, 1 row below carries diacritics like an
  ogonek. A scroll rectangle under 11 rows clips these marks.
- NuttX supports both a make build and a CMake build per board. This
  repository builds via make only, so the stale CMake files for this board
  are removed.

### Fixed

- The animation thread derived seconds from a count of 20 ms waits instead
  of the clock. Wait jitter caused drift, and the digits occasionally
  missed a second. The thread now reads the clock on every wait and
  updates the digits on each second change.
- The flasher tool opened the link at 115200 baud while the board's
  USART1 runs at 460800 baud, so the board answered no request. The
  protocol header now carries the baud rate, and the board's server
  refuses to build if its configuration disagrees.

## [0.1.1] - 2026-07-28

### Added

- W25Q32 serial flash on SPI1, in two partitions: the first two sectors
  hold the settings record, and the remaining sectors hold a SmartFS file
  system at `/assets` for fonts, icons, and animations.
- Settings persist across power loss: the store holds the local-time
  offset, the two brightness levels, the temperature correction, and the
  do-not-light period.
- A two-record store: writes target the record not in use, so a power
  loss preserves the prior record.
- Opcode `0x0A` sets a temperature correction, in tenths of a degree.
- Opcode `0x0B` sets a do-not-light period, such as 23:00-06:00. The
  display resumes its prior brightness levels when the period ends.
- Opcode `0x0C` writes a file into the assets store, creating its
  directory if needed.
- The panels show board-generated content until the edge MCU sends text: a
  greeting on the main panel, the day of the week on the sub panel.
- An extended font at `/assets/fonts/default.tgf` for non-ASCII letters.
  Its cell holds 10 rows for a 7-row letter: 2 rows above carry diacritics
  like an acute, 1 row below carries diacritics like an ogonek, so a
  letter keeps its full height.
- `tools/mkfont.py` builds this font via `make font`, deriving glyph
  shapes from the firmware's ASCII table so both fonts share one shape
  set.
- The Makefile builds firmware for both MCUs and flashes the edge MCU over
  the air.

### Fixed

- The `nsh` build now links. The font sources were scoped to the protocol
  build only, so `nsh`, which also shows the version, failed to link
  without them.

### Changed

- The edge MCU sends the local-time offset once, not after every board
  reset.
- **Panel text now starts with an attributes byte.** Bits 0-1 select
  horizontal alignment, where a value of 0 centers text and earlier
  versions always left-aligned it. This is a breaking layout change, but
  the protocol is experimental and both MCUs build from one source, so its
  version stays 1.
- Panel text is UTF-8. A character missing from both fonts renders as a
  space.

## [0.1.0] - 2026-07-28

The first release. The firmware drives the HAZK-03 module's display
hardware and answers a binary protocol on the edge MCU's UART.

### Added

#### Board support

- NuttX board support for the HAZK-03 module with the STM32F105RB.
- Clock tree runs from the internal 36 MHz oscillator. Enabling the
  external crystal hangs this part, so firmware never starts it.
- PA13 released from the debug port, since it carries the sub panel's
  serial data.
- Two configs: `nsh` gives an interactive shell, `ipc` gives the protocol
  with no console.

#### Display

- A TM1629A driver for the 12 clock digits.
- An SM1626D driver for the 70x14 main panel and the 21x14 sub panel.
- A DS3231 driver for time and temperature, bit-banged on pins PC6 and
  PC7.
- The battery-backed DS3231 is the system clock, so time persists across a
  reset.
- A scan loop that refreshes the panel image, at 200 us per row.
- A 5x7 font and a panel text-drawing function.
- Brightness control for digits and panels: 0 is off, 1-8 give eight
  levels.

#### Protocol

- A shared framing library in `ipc/`, freestanding C99, so both MCUs and
  the host tests compile the same source.
- CRC-16/CCITT-FALSE on every frame.
- Correlation IDs, so a caller matches each response to its request.
- Credit-based flow control in each ACK: one credit grants 64 bytes of
  receive-buffer space.
- Recovery from a false start-of-frame byte, and an idle timeout that
  clears a partial frame.
- A USART1 server with these operations: set time, set panel text, set
  brightness, request state, and a log line as a push frame.
- The RTC keeps UTC. The time-set frame carries a minute offset, so a
  season change needs no RTC write.
- The state frame carries the firmware version.
- A protocol document in `docs/ipc-protocol.md`.

#### Version

- Version comes from git tags. A build script writes it into a header at
  each build.
- The main panel shows the version for three seconds after a reset.
- A tree with local changes gets the suffix `-dirty`.

#### Tools and build

- A container image with the `arm-none-eabi` toolchain, so a host needs
  only podman and git.
- A Makefile that builds both MCU images and flashes the edge MCU over
  the air.
- The edge-MCU flasher in `tools/hazk-flasher/`: writes the STM32 via the
  system bootloader, and provides a web interface, a network bridge to
  the target, and a protocol peer.
- Host tests for the framing library, using the Unity framework.

### Known limitations

- The link runs at 460800 baud. At 921600 baud, STM32-bound frames are
  almost entirely lost.
- One UART carries both console and protocol, so a build provides only
  one.
- The board keeps no settings. The edge MCU resends the UTC offset after
  every reset. *(0.1.1 adds a settings store.)*
- The main panel shows 11 characters, the sub panel shows 3, and longer
  text is truncated.
- The scan loop is CPU-driven, not DMA-driven.
- The firmware has no animation support and reads no USB device.

[0.3.0]: https://github.com/mekops-labs/telegraph-stm-fw/releases/tag/v0.3.0
[0.2.0]: https://github.com/mekops-labs/telegraph-stm-fw/releases/tag/v0.2.0
[0.1.1]: https://github.com/mekops-labs/telegraph-stm-fw/releases/tag/v0.1.1
[0.1.0]: https://github.com/mekops-labs/telegraph-stm-fw/releases/tag/v0.1.0
