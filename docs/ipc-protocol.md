# IPC Protocol

This document describes the protocol between the two MCUs of Telegraph.

The STM32F105RB drives the display hardware. An edge MCU next to it holds the
network stack. A UART connects the two parts.

The library in `ipc/` implements the frames. The code is freestanding C99. It
calls no allocator, and it needs no RTOS. Thus both MCUs and the host tests
compile the same source.

## Physical layer

| Property | Value |
| :--- | :--- |
| STM32 pins | PA9 (TX) and PA10 (RX), USART1 |
| Edge MCU pins | UART1, refer to the board documentation |
| Format | 8N1 |
| Rate | 115200 for bring-up, then 460800. Refer to the note below |
| Flow control | credits in the protocol, refer to [Flow control](#flow-control) |

The two MCUs have 3.3 V logic. Thus the link needs no level shifter. Keep a
ground wire in the same cable.

**The two directions do not have the same limit.** The direction from the
STM32 to the edge MCU is correct at 2 Mbps. The opposite direction fails at
921600, and it is correct at 460800. Test each direction after a change of
the cable.

The module gives no RTS or CTS pin. Thus the protocol carries the flow
control.

The same two pins carry the AN3155 protocol of the system bootloader. The edge
MCU sets BOOT0 and NRST to select between the two functions. AN3155 uses 57600
8E1, thus the edge MCU changes the port format for a flash operation.

### The console shares the UART

There is one UART between the parts. A serial console and this protocol cannot
both own it.

Thus the firmware has two configurations:

- `nsh` gives the interactive shell on USART1. Use it for bring-up.
- `ipc` gives this protocol on USART1. It has no serial console.

Note: the `ipc` configuration sends the messages of the system log as LOG
frames. Thus a diagnostic message stays available without a console.

## The protocol is experimental

The two MCUs always come from the same source, thus a change that breaks the
earlier layout needs no step of the version. **The version of the protocol
stays 1 through this period.** A reader must not take that value as a mark of
an unchanged layout.

The changelog of the firmware gives each change of the layout.

## Frame

All 16-bit fields are little-endian.

| Offset | Size | Field |
| :--- | :--- | :--- |
| 0 | 1 | SOF, always `0xAA` |
| 1 | 2 | LEN |
| 3 | 1 | OPCODE |
| 4 | 2 | CORRELATION\_ID |
| 6 | n | PAYLOAD |
| 6+n | 2 | CRC16 |

`LEN` counts the payload bytes only. Thus the length of the full frame is
`LEN` plus 8.

The maximum payload is 1024 bytes. A build changes this limit with
`-DIPC_MAX_PAYLOAD=n`. Give the same limit to both MCUs. A receiver with a
smaller limit rejects the large frames of the sender.

### CRC

The CRC is CRC-16/CCITT-FALSE. The properties are:

- Polynomial `0x1021`
- Initial value `0xFFFF`
- No reflection of the input or of the output
- No final exclusive-or

The CRC covers the bytes from `LEN` to the end of the payload. The `SOF` byte
is a constant, thus it adds no information.

Note: the check value of this variant is `0x29B1` for the string `123456789`.

The library uses a table of 16 entries. A table of 256 entries costs 512 bytes
of flash for a gain near two times. A loop for each bit costs near eight times
more cycles.

## Correlation IDs

A request carries a correlation ID. The response repeats that ID. Thus the
sender matches each response to its request.

The value `0x0000` marks a push frame. A push frame answers no request. The
STM32 transmits these frames without a request.

Note: the broker on the edge MCU gives each caller its own IDs. Thus more than
one caller shares the UART, and the callers need no lock between them.

## Opcodes

| Opcode | Direction | Description |
| :--- | :--- | :--- |
| `0x01` | edge to STM32 | Set the RTC |
| `0x02` | edge to STM32 | Set the text of a panel |
| `0x04` | edge to STM32 | Set the brightness |
| `0x05` | edge to STM32 | Animate a rectangle, or give the names of the sprites |
| `0x07` | edge to STM32 | Stop the animations |
| `0x08` | edge to STM32 | Set the pixels of a rectangle |
| `0x0A` | edge to STM32 | Correct the temperature |
| `0x0C` | edge to STM32 | Write one part of a file |
| `0x0D` | edge to STM32 | Clear a panel, or both |
| `0x14` | edge to STM32 | List a directory |
| `0x15` | edge to STM32 | Read one part of a file |
| `0x16` | edge to STM32 | Remove a file, or a directory that holds no entry |
| `0x17` | edge to STM32 | Create a directory |
| `0x0F` | edge to STM32 | Change the rate of an animation |
| `0x13` | edge to STM32 | Take a font from the flash, or give the names of the fonts |
| `0x0B` | edge to STM32 | Set the period without light |
| `0x10` | edge to STM32 | Request the state |
| `0x11` | STM32 to edge | The state |
| `0x12` | STM32 to edge | A log line, a push frame |
| `0x20` | edge to STM32 | Start the flash mode |
| `0xF0` | STM32 to edge | ACK and the credits |
| `0xF1` | STM32 to edge | NACK and an error code |

### `0x01` Set the RTC

| Offset | Size | Field |
| :--- | :--- | :--- |
| 0 | 4 | The Unix time in seconds, UTC |
| 4 | 2 | The minutes of the local time from UTC, signed, optional |

A payload of 4 bytes sets the time only. A payload of 6 bytes also sets the
offset.

The STM32 writes the DS3231. A battery holds that device, thus the time stays
correct after a power interruption.

**The RTC keeps UTC. The offset changes the panels only.** Thus a change of
the season needs no change of the RTC, and the `0x11` frame always gives UTC.

The board keeps the offset in its flash. Thus the edge MCU sends it one time,
and not after each reset of the board.

The reply is an ACK.

## The panel is an argument, not an opcode

Every operation that names a panel takes it as the **first byte of its
payload**: `00` for the main panel and `01` for the sub panel. Thus one opcode
serves both, and an operation needs no opcode of its own for each of them.

### `0x02` Set the text of a panel

| Offset | Size | Field |
| :--- | :--- | :--- |
| 0 | 1 | The panel |
| 1 | 1 | The attributes |
| 2 | n | The text in UTF-8, without a terminator |

A payload of the panel alone clears that panel.

The bits 0 and 1 of the attributes give the place of the text across the
panel:

| Value | Place |
| :--- | :--- |
| 0 | The middle |
| 1 | The left |
| 2 | The right |

The bits 2 and 3 give the place of the text down the panel:

| Value | Place |
| :--- | :--- |
| 0 | The middle |
| 1 | The top |
| 2 | The bottom |

**A text in the middle clears the whole panel first. A text at the top or the
bottom clears its own rows alone**, thus two of them give two lines. The font
of 5 by 7 takes 10 rows of a panel of 14, so two lines of it do not fit; a
compact font from the flash does.

The other bits are 0. Any other value gets a NACK with the code `0x03`.

The font of the firmware holds the ASCII table. The extended font in the flash
holds the other letters. A character that neither font holds gives a space.

**A text wider than its panel scrolls.** The board compares the rendered width
against the panel and starts the same window/source scroll that `0x05` uses,
at a fixed speed. A text that fits stays static.

**With a font whose line height leaves room for a second line, an overflowing
text wraps at a word instead of scrolling across.** A word wider than the
panel breaks mid-word. A wrapped text that still overflows the panel height
scrolls down by rows instead, each line centered the same way a static one is.
Whether a font leaves room for a second line follows only from its own line
height against the panel height, so the font of 5 by 7 keeps the single-line
scroll above; the compact font gets the wrap.

The reply is an ACK.

### `0x04` Set the brightness

| Offset | Size | Field |
| :--- | :--- | :--- |
| 0 | 1 | The brightness of the digits, 0 to 8 |
| 1 | 1 | The brightness of the panels, 0 to 8, optional |

A payload of 1 byte gives the same level to both devices.

The value 0 turns the device off. The values 1 to 8 give eight levels of
brightness, from the dimmest to the full level.

The TM1629A has its own control for the digits. The panels have no such
control, thus the driver makes the on-time of each row shorter. The level 8 is
the full on-time.

Note: a value above 8 gets a NACK with the code `0x03`.

The reply is an ACK.

### `0x05` Animate a rectangle

| Offset | Size | Field |
| :--- | :--- | :--- |
| 0 | 1 | The panel |
| 1 | 1 | The column of the left edge |
| 1 | 1 | The row of the top edge |
| 2 | 1 | The width |
| 3 | 1 | The height |
| 4 | 1 | The flags |
| 5 | 2 | The period of one step, in milliseconds |
| 7 | 1 | The step, in pixels |
| 8 | 1 | The width of the source |
| 9 | 1 | The height of the source |
| 10 | n | The source |

The board keeps a source larger than the rectangle, and it moves a window over
that source. The window moves by the step every period, and it returns to the
start at the end of the source.

**A step of one pixel gives a scroll. A step of the width of the rectangle
gives the frames of a sprite**, because the window then jumps from one frame to
the next. Thus one mechanism carries both, and the edge MCU sends no frame for
each step.

| Flag | Value | Meaning |
| :--- | :--- | :--- |
| `IPC_ANIM_VERTICAL` | `0x01` | The window moves down instead of across |
| `IPC_ANIM_TEXT` | `0x02` | The source is a text in UTF-8, not pixels |
| `IPC_ANIM_FILE` | `0x04` | The source is a sprite in the flash of the board |

**Give a text at least 11 rows of height.** A letter takes 7 rows, and the cell
of the font takes 10: two rows above the letter carry a mark such as an acute,
and one row below carries a mark such as an ogonek. A rectangle shorter than 11
rows loses the top mark, thus a letter such as `ń` scrolls without its accent.
The panel has 14 rows, so a full-height rectangle always fits.

With `IPC_ANIM_TEXT` the board draws the text into the source itself, and the
width and the height of the source are 0. **A message that scrolls thus costs
one frame of the protocol, and not one frame for each step.**

The board pads the drawn text with a blank gap of 3 character-widths before
the source wraps back to its own start. Without that gap the wrap reads as
the text running into itself.

Without that flag the source is a bitmap. The pixels go row by row, each row
starts at a byte, and bit 7 of a byte is the pixel at the left. The payload
must hold exactly `((source width + 7) / 8) * source height` bytes.

The source of the main panel holds 512 bytes and the sub panel holds 128. A
larger source gets a NACK with the code `0x02`.

Note: a period of 0 gets a NACK with the code `0x03`. A width, a height, or a
step of 0 also gets that NACK, except with `IPC_ANIM_FILE` (below).

With `IPC_ANIM_FILE` the source is a sprite in the flash. The body is then the
name of that sprite, such as `heart`. The file carries the size of the source,
the step and the direction, thus the payload gives none of them. **A payload
without a body gives the names of the sprites**, refer to
[The names of the assets](#the-names-of-the-assets).

**A width or a height of 0 takes the frame size from the file.** The width
becomes the file's step, and the height becomes the file's own height. A
caller therefore cannot state a window that mismatches the sprite's frame.

The reply is an ACK.

### `0x13` Take a font from the flash

The payload is the name of a font, such as `compact`. **An empty payload gives
the names of the fonts**, refer to
[The names of the assets](#the-names-of-the-assets).

A font carries its own cell. The font of the firmware takes 7 rows for a
letter, thus one line fills a panel of 14 rows. The compact font takes 5 rows
in a cell of 7, thus **two lines fit**.

A file that is absent or not a font gets a NACK with the code `0x05`.

The reply is an ACK.

### The names of the assets

The board keeps each kind of asset in one place, and it takes one format for
each kind. Thus a request names an asset alone, without a directory and without
an ending.

| Kind | Place | Ending | Opcode |
| :--- | :--- | :--- | :--- |
| Font | `/assets/fonts` | `.tgf` | `0x13` |
| Sprite | `/assets/animations` | `.tgs` | `0x05` with `IPC_ANIM_FILE` |

A request without a name asks for the names that the board holds. The reply
carries the opcode of that request and its correlation ID, and its payload is
those names with a newline after each one. An empty payload thus means that the
board holds no asset of that kind.

Note: a name that holds `/` gets a NACK with the code `0x03`. Thus a request
reaches no file outside the place of its kind.

Note: `0x0C` writes a file, and its payload stays a full path. That opcode
carries every kind of file, thus it needs one.

## Storage

The edge MCU reads and writes two file systems over the link: `/assets` on the
flash of the board, and `/media`, which holds the mass storage device of the
USB port when one is present. The opcodes `0x0C`, `0x14`, `0x15`, `0x16` and
`0x17` serve both.

**Every path must start with `/assets` or with `/media`, and a path holding
`..` gets a NACK with the code `0x03`.** A root matches a whole element of the
path, thus `/assetsx` is outside it. The settings record, the raw block devices
and the rest of the file tree therefore stay out of reach of the link.

A path holds 64 characters at most.

The file system of the USB device is FAT, and a name there holds 32 characters.
A longer name gets a NACK with the code `0x05`.

**A name on that device matches by case.** Thus a caller takes each name from
the reply of `0x14` and gives it back unchanged. A name that another system
wrote in the 8.3 form is upper case, and a request in lower case reaches no
file.

### `0x0C` Write one part of a file

| Offset | Size | Field |
| :--- | :--- | :--- |
| 0 | 1 | The flags |
| 1 | 1 | The length of the path |
| 2 | n | The path |
| 2+n | m | The data |

The flag `0x01` makes the file empty, and the flag `0x02` closes it. A file of
one part alone carries both flags. The board keeps one file open, and a first
part closes a file that an earlier transfer left open. Every part of one file
must carry the same path.

The board creates the directory of the file when that directory is absent.

The reply is an ACK.

### `0x14` List a directory

| Offset | Size | Field |
| :--- | :--- | :--- |
| 0 | 2 | The ordinal of the first entry |
| 2 | n | The path |

The reply carries the opcode `0x14`:

| Offset | Size | Field |
| :--- | :--- | :--- |
| 0 | 2 | The ordinal of the first entry that the reply leaves out |
| 2 | n | The entries |

Each entry is:

| Offset | Size | Field |
| :--- | :--- | :--- |
| 0 | 1 | The kind: 0 for a file, 1 for a directory |
| 1 | 4 | The size, and 0 for a directory |
| 5 | 1 | The length of the name |
| 6 | n | The name |

A reply carries as many entries as one frame holds. The ordinal of the reply
then names the first entry that it leaves out, thus a directory of any length
takes as many requests as it needs. The value `0xFFFF` states that no entry
remains.

### `0x15` Read one part of a file

| Offset | Size | Field |
| :--- | :--- | :--- |
| 0 | 4 | The offset in the file |
| 4 | 2 | The number of bytes |
| 6 | n | The path |

The reply carries the opcode `0x15`:

| Offset | Size | Field |
| :--- | :--- | :--- |
| 0 | 4 | The offset in the file |
| 4 | n | The data |

One reply gives 512 bytes at most, and a larger request takes that value. A
reply shorter than the request holds the end of the file, and a reply of the
offset alone states that the offset is at or past that end. Thus a caller reads
until it takes such a reply.

### `0x16` Remove a file or a directory, `0x17` create a directory

The payload of each is the path alone. The reply is an ACK, or a NACK with the
code `0x05` when the operation fails — a directory that holds an entry, for
one.

### `0x07` Stop the animations

The payload names a panel, or it is empty for both. Each rectangle keeps the pixels of its last step.

The reply is an ACK.

### `0x08` Set the pixels of a rectangle

| Offset | Size | Field |
| :--- | :--- | :--- |
| 0 | 1 | The panel |
| 1 | 1 | The column of the left edge |
| 2 | 1 | The row of the top edge |
| 3 | 1 | The width |
| 4 | 1 | The height |
| 5 | n | The pixels |

The pixels go row by row, and each row starts at a byte. Bit 7 of a byte is the
pixel at the left. Thus one row takes `(width + 7) / 8` bytes, and the payload
takes that many bytes for each row.

**The rectangle changes those pixels alone.** The rest of the panel keeps its
content, thus one part of a panel carries a clock while another part carries a
notification. A rectangle that goes past an edge loses the part outside.

Note: the panel holds two images. A write changes the image that the scan does
not read, and the scan takes the new one between two images. Thus no image ever
shows one part of a change.

Note: a width or a height of 0 gets a NACK with the code `0x03`, and a payload
that does not match the rectangle gets one with the code `0x02`.

The reply is an ACK.

### `0x0D` Clear a panel

| Payload | Effect |
| :--- | :--- |
| empty | Both panels |
| `00` | The main panel alone |
| `01` | The sub panel alone |

The panel loses every pixel.

**The animation of that panel stops as well.** An animation that kept its steps
would draw over the panel again at its next one, thus the panel would not stay
empty.

The reply is an ACK.

### `0x0F` Change the rate of an animation

| Offset | Size | Field |
| :--- | :--- | :--- |
| 0 | 1 | The panel, 0 for the main one and 1 for the sub one |
| 1 | 2 | The period of one step, in milliseconds |
| 3 | 1 | The step in pixels, or 0 to keep the one in use |

The animation keeps its source and its place. **Thus the rate changes without
the cost of sending that source again**, which matters for a text that the
board drew into a source of its own.

Note: a panel without an animation, or a period of 0, gets a NACK with the code
`0x03`.

The reply is an ACK.

### `0x0A` Correct the temperature

| Offset | Size | Field |
| :--- | :--- | :--- |
| 0 | 2 | The correction in tenths of a degree Celsius, signed |

The board adds this value to each reading of the DS3231. Thus the panels and
the state frame give the same value.

The reply is an ACK.

### `0x0B` Set the period without light

| Offset | Size | Field |
| :--- | :--- | :--- |
| 0 | 2 | The minute of the local day that stops the light |
| 2 | 2 | The minute that starts the light again |

The display gives no light between these two minutes. A start after the end
goes through midnight. Thus a start of 1380 and an end of 360 stop the light
from 23:00 until 06:00.

The value `0xFFFF` for the start stops this function.

Note: the period does not change the brightness of the settings. Thus the
display takes its previous levels at the end of the period.

Note: a minute of 1440 or above gets a NACK with the code `0x03`.

The reply is an ACK.

### `0x10` Request the state, `0x11` the state

The request has an empty payload. The reply is a `0x11` frame with the same
correlation ID. The first 12 bytes are always present:

| Offset | Size | Field |
| :--- | :--- | :--- |
| 0 | 4 | The Unix time of the RTC |
| 4 | 2 | The temperature in tenths of a degree Celsius, signed |
| 6 | 2 | The count of the accepted frames |
| 8 | 2 | The count of the CRC errors |
| 10 | 1 | The count of the resynchronization operations |
| 11 | 1 | The version of the protocol, currently 1 |
| 12 | n | The version of the firmware, text without a terminator |

The length of the version text is the length of the payload less 12. The
longest text is 32 bytes.

The version comes from the git tags of the firmware. A build from a tag gives
that tag, such as `v0.1.0`. A build from a later commit adds the count of the
commits and the short hash. A build from a tree with local changes adds the
suffix `-dirty`. A repository without a tag gives the short hash alone.

Note: the edge MCU compares this text with the version of the image that it
holds. Thus it writes the firmware only when the two differ. A `-dirty` text
never matches a released image, thus such a build always gets written again.

Note: a `0x11` frame gets no ACK. The frame is itself the reply.

### `0x12` A log line

The payload holds text without a terminator. The correlation ID is `0x0000`.

The STM32 transmits this frame without a request. Thus the edge MCU gets the
diagnostic output of a build that has no console.

### `0x20` Start the flash mode

This opcode is a reservation. The STM32 answers with a NACK and the code
`0x06`.

Note: a flash operation uses the system bootloader. The edge MCU holds BOOT0
and it applies a reset. Thus the application firmware needs no support.

### `0xF0` ACK

The payload holds one byte. This byte is the credit count. Refer to
[Flow control](#flow-control).

### `0xF1` NACK

The payload holds one error code:

| Code | Meaning |
| :--- | :--- |
| `0x01` | The receiver has no such opcode |
| `0x02` | The payload has the wrong length |
| `0x03` | A field holds an invalid value |
| `0x04` | The receiver cannot accept the work |
| `0x05` | The operation started, and it failed |
| `0x06` | The build has no support for this opcode |

The sender transmits the frame again after a NACK with the code `0x04`. Do not
transmit the frame again after the other codes. These codes show an error in
the frame itself.

## Flow control

Each ACK carries a credit count. **One credit gives 64 bytes of the receive
buffer.** A frame with a length above 64 bytes thus costs more than one
credit.

The protocol has no initial grant. Thus a sender starts with one frame, and it
learns the true capacity from the first ACK.

Obey these rules:

1. Transmit a frame, and subtract its cost in credits.
2. If the count is zero, stop until the next ACK.
3. Read the credit count of each ACK, and use that value.

The STM32 calculates the count from the free space of its receive buffer.

**A sender that ignores the credits loses data.** A test of 200 frames without
the credits lost near two thirds of them. The same test with the credits sent
500 frames with no loss.

## Recovery from an error

The receiver hunts for the `SOF` byte. An incorrect CRC, or a `LEN` above the
maximum, makes the parser discard one byte. The parser then finds the next
`SOF` and tries again.

Thus a payload byte with the value `0xAA` does not hide the frame that comes
after it.

### The idle timeout is necessary

Corrupt data sometimes gives a false `SOF` with a `LEN` that is possible. The
parser then waits for a frame that no sender transmits. A good frame behind it
stays in the buffer.

Detect an idle receive line, then call `ipc_parser_timeout()`. This function
takes out the complete frames, and it discards the remainder.

Note: a period of three frame times without a byte is sufficient. The STM32
uses 20 ms.

**A receiver without this timeout stops on line noise. It does not recover.**

## Library

```c
#include <telegraph/ipc.h>

static void on_frame(void *arg, const struct ipc_frame_s *frame);

struct ipc_parser_s *parser = malloc(sizeof(struct ipc_parser_s));

ipc_parser_init(parser);
ipc_parser_push(parser, buf, len, on_frame, NULL);
```

The structure `ipc_parser_s` holds a full frame. Thus put it on the heap, and
not on the stack of a task.

The payload of a frame points into the buffer of the parser. The data stays
valid only during the callback. Copy the data to keep it.

### Tests

```sh
make test
```

The tests use Unity, and they run with the host compiler.
