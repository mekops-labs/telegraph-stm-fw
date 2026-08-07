# The broker of the link

The edge MCU runs the WANTED engine, and that engine gives one UART to one
wapp. The wapp `tg-broker` holds that grant. Every other wapp of the device
reaches the STM32 through it.

A peer speaks the frames of [the protocol](ipc-protocol.md) over two named
pipes of the engine. The broker gives each request an identifier of its own on
the link, and it puts the identifier of the peer back into the reply. Thus two
peers send requests at the same time and neither of them coordinates with the
other.

## The pipes

| Path | Direction |
| :--- | :--- |
| `/dev/pipe/tg-<peer>-req` | the peer writes its requests |
| `/dev/pipe/tg-<peer>-rsp` | the peer reads its replies |

The launch config names the peers as the arguments of the broker. One argument
has the form `<name>[:<opcode>...]`, and each opcode after the name asks for
the frames of that kind that no request asked for.

```json
"args": ["display", "usb:0x33", "ota"]
```

The engine holds eight named pipes, thus the broker serves four peers.

A pipe holds 4096 bytes. The broker discards a frame when a peer leaves its
pipe full for two seconds, and it writes a line to its console for that frame.

## What the broker does with a frame

- **A request** takes the next identifier of the link. The broker holds one
  request at a time, and while that request waits for its reply it reads no
  pipe. The requests of the other peers stay in their pipes and go in the order
  they arrived, thus the backpressure needs no queue. A peer that writes two
  requests into one pipe without waiting takes a NACK with `IPC_ERR_BUSY` for
  the second one, because both leave that pipe together.
- **A reply** carries the identifier of the link. The broker finds the peer
  that waits for it, puts the identifier of that peer back, and writes the
  frame to its pipe.
- **A frame with the identifier 0** asks nothing. The broker writes it to every
  peer that names its opcode, and it prints a log frame that no peer follows.
- **A request without a reply** goes again after 200 ms, three times. The peer
  then gets a NACK with `IPC_ERR_FAILED`. The link loses about one frame in ten
  thousand and such a loss carries no error of its own, thus this retry keeps
  that loss away from the peer.

## Raw mode

The STM32 takes its firmware over the same wire, through the bootloader of its
ROM. That protocol carries no frames and it runs at another rate, thus a peer
asks the broker for the line itself.

| Opcode | Payload | Meaning |
| :--- | :--- | :--- |
| `0xE0` | `[baud u32] [databits u8] [parity u8] [stopbits u8]` | take raw mode |
| `0xE0` | empty | leave raw mode |
| `0xE1` | the bytes | both directions, without a change |

The parity is one of the characters `N`, `E` or `O`. The broker sets the line,
answers with an ACK, and from that moment it writes the payload of every `0xE1`
frame to the line and carries what the line holds back the same way. A request
of another peer takes a NACK with `IPC_ERR_BUSY` until the peer leaves this
mode, and the line then returns to 460800 8N1.

Note: the settings of the line discard the receive buffer of the driver. Thus
a peer that enters or leaves raw mode loses whatever the line held.

## The names

The engine holds a name of 14 characters, thus every wapp of this device takes
the prefix `tg-`. The name of a peer is the part after that prefix: the wapp
`tg-display` is the peer `display`, and its pipes are `tg-display-req` and
`tg-display-rsp`.

## The build

```sh
make wapps        # compile each wapp under wapps/ to wasm32-wasi
make wapp-images  # package each of them as build/wapps/<name>@<version>-1.wapp
```

The compiler is the wapp SDK of the engine, in its own container image. A wapp
takes one page of linear memory, which is the smallest envelope the engine
offers, and it holds the shared IPC library of this repository.

## The board of the edge MCU

The engine image of the edge MCU comes from the WANTED engine repository, with
the profile `s3-telegraph`: the serial-port driver is built in, wsh is the
supervisor, and the wapps of this directory are seeded into the registry from
the firmware. That build takes the path of this directory:

```sh
OTA_PROFILE=s3-telegraph TELEGRAPH_WAPPS=<this repo>/wapps just build
```

The shell of the engine takes its commands on the USB console, and
`tools/wsh-console.py` sends them:

```sh
SETTLE=25 tools/wsh-console.py "create tg-broker" "set_config tg-broker {...}" \
    "start tg-broker"
```

The launch config of the broker names the port and its pins:

```json
{"drivers":[{"name":"uart","options":"port=1,tx=1,rx=2,baud=460800,format=8N1"}],
 "args":["probe:0x03"]}
```

Note: wsh keeps no wapp across a reset of the edge MCU. The registry survives,
thus the three commands above start the wapp again and nothing needs another
flash. A board of a deployment runs the supervisor of the control plane
instead, which starts its wapps by itself.

Note: a full flash of the edge MCU erases the registry, thus a new build of a
wapp reaches the board that way. A seed leaves a ref the registry already
holds, and an erase of the flash is what removes it.

## The test

The wapp `tg-probe` proves the broker without the hardware: it sends a request,
reads the reply, waits for a frame that no request asked for, and takes raw mode
and leaves it again. A host build of the engine runs both wapps against a pty
and a program that answers as the STM32 does.

With `TELEGRAPH_TEXT` in its environment the probe also puts that text on the
main panel, which is the check the eye makes on the real hardware. Without
`TELEGRAPH_FULL` it asks for the state alone, because the board sends a log
frame at its own reset alone and echoes no byte of raw mode.
