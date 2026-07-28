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
| `0x02` | edge to STM32 | Set the text of the main panel |
| `0x03` | edge to STM32 | Set the text of the sub panel |
| `0x04` | edge to STM32 | Set the brightness |
| `0x0A` | edge to STM32 | Correct the temperature |
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

### `0x02` and `0x03` Set the text of a panel

| Offset | Size | Field |
| :--- | :--- | :--- |
| 0 | 1 | The attributes |
| 1 | n | The text in UTF-8, without a terminator |

An empty payload clears the panel.

The bits 0 and 1 of the attributes give the place of the text across the
panel:

| Value | Place |
| :--- | :--- |
| 0 | The middle |
| 1 | The left |
| 2 | The right |

The other bits are 0. Any other value gets a NACK with the code `0x03`.

The font of the firmware holds the ASCII table. The extended font in the flash
holds the other letters. A character that neither font holds gives a space.

The main panel holds 11 characters, and the sub panel holds 3. A longer text
keeps its start, and it loses its end.

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
| 11 | 1 | The version of the protocol, currently 2 |
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
