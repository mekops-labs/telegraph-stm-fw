# The request set of the display

The wapp `tg-display` owns what the board shows: the composition of a panel,
the digits and the clock. It reaches the STM32 through
[the broker](broker.md), and it serves that as a request set of its own.

An adapter of an outside protocol — [HTTP](display-http.md) today, another
tomorrow — translates its protocol into these requests. Such an adapter holds
no UART grant, names no opcode of the link, and knows nothing of the STM32.
Adding one is adding a wapp to the desired state.

## The pipes

| Path | Direction |
| :--- | :--- |
| `/dev/pipe/tgd-<client>-req` | the client writes its requests |
| `/dev/pipe/tgd-<client>-rsp` | the client reads its replies |

The launch config names the clients as the arguments of the display:

```json
"args": ["rest", "hass"]
```

A launch config that names none gets that pair. The display serves four
clients, and the prefix `tgd-` keeps its pipes apart from the broker's.

## The frames

The framing is [the one of the link](ipc-protocol.md) — the same encoder, the
same parser and the same CRC. The client chooses the correlation ID of its
request and the display repeats it. The display sends no frame that a request
did not ask for.

| Opcode | Request | Payload |
| :--- | :--- | :--- |
| `0x40` | the state of the board | — |
| `0x42` | text on a panel | `[panel] [attributes] [text]` |
| `0x43` | text that moves | `[panel] [period u16] [step] [text]` |
| `0x44` | the brightness | `[digits] [panels]` |
| `0x45` | the clock | `[epoch u32] [offset i16]` |
| `0x46` | clear both panels | — |
| `0x47` | the period without light | `[start u16] [end u16]` |

The state comes back as opcode `0x41`, carrying the payload of the board's own
state frame. Every other request takes an ACK, or a NACK with one byte of
`IPC_ERR_*`. A `IPC_ERR_BUSY` means the board gave no reply in time; it is the
one code that says nothing about the request.

The header `wapps/include/telegraph/display.h` carries the offsets and the
values, and `wapps/lib/dspclient.c` carries the client side of the exchange.
An adapter names that file in `EXTRA_SRCS` and writes none of it again.

## What belongs where

The display holds what only it can hold: which panel is which, how wide each
one is, how a text becomes a movement of the board, and the timeout towards
the STM32. An adapter holds its own protocol and nothing else.

A period or a step of 0 takes the default of the display (60 ms, 1 pixel), and
a step wider than its panel is cut to that width.
