# The USB port over HTTP

The wapp `tg-usb` serves the USB port of the STM32 on a listening socket of the
engine. It holds no hardware grant of its own: the board owns the port, and
this wapp reaches it through [the broker](broker.md).

```json
{"sockets": [{"name": "http", "address": "tcp://0.0.0.0:8081",
              "role": "listen", "backlog": 2, "max_conns": 2}]}
```

The launch config of the broker names this wapp as one of its peers **and the
push frame it follows**, thus `"args": ["usb:0x33"]`. Without the opcode the
board's channel reads reach nobody.

## The devices

`GET /usb/devices` lists what the port holds.

```json
{"devices":[{"channel":0,"kind":"serial","name":"ttyACM0"},
            {"channel":255,"kind":"storage","name":"sda1"}]}
```

A serial device carries the number of the channel that the write, read and
subscribe routes name. A mass storage device carries 255 and no channel: the
storage routes reach it by path instead.

## The channel of a serial device

| Method | Path | Body | Result |
| :--- | :--- | :--- | :--- |
| `POST` | `/usb/<channel>/subscribe` | — | the board opens the device and starts pushing its reads |
| `DELETE` | `/usb/<channel>/subscribe` | — | the board stops and closes it |
| `POST` | `/usb/<channel>/write` | the bytes | the bytes reach the device |
| `GET` | `/usb/<channel>/read` | — | what the device has said since the last read |

```sh
curl -s -X POST http://<address>:8081/usb/0/subscribe
curl -s -X POST --data-binary 'AT\r' http://<address>:8081/usb/0/write
curl -s http://<address>:8081/usb/0/read
```

A read answers `application/octet-stream` and empties the buffer, thus two
reads never give the same bytes twice. The buffer holds 1024 bytes per channel
and drops its oldest bytes when a client reads slower than the device writes;
`X-Telegraph-Dropped` counts what went. A read of a channel that no subscribe
opened answers `409`.

**A write is the one operation here that a repeat does not leave unchanged.**
This wapp carries a sequence number for each channel, and it advances that
number only on an ACK — thus a caller that repeats a write after a timeout adds
its bytes once. The board forgets the sequence when a channel opens or closes,
which is why a subscribe resets it.

## The files of a mass storage device

The same routes reach the flash of the board, because the board treats both as
one file tree. Every path must sit under `/media` (the USB device) or `/assets`
(the flash), and a path holding `..` is refused.

| Method | Path | Body | Result |
| :--- | :--- | :--- | :--- |
| `GET` | `/storage?path=<dir>` | — | the entries of a directory |
| `GET` | `/storage/file?path=<file>` | — | one part of a file |
| `POST` | `/storage/file?path=<file>` | the bytes | one part of a file, on the way in |
| `DELETE` | `/storage/file?path=<file>` | — | the file goes |
| `POST` | `/storage/dir?path=<dir>` | — | the directory appears |

A listing answers `{"next":<index>,"entries":[…]}`. A directory longer than one
reply takes another request with `?index=<next>`; `next` is 65535 once no entry
remains.

A read takes `?offset=` and `?length=`, answers `application/octet-stream` and
reports the offset it served in `X-Telegraph-Offset`. A body shorter than the
requested length holds the end of the file, and an empty body says the offset
is at or past it — thus a caller reads until it takes one.

A write takes `?first=` and `?last=`, both 1 by default, so a file that fits
one request needs no flags. A larger file goes in parts: `?first=1&last=0`,
then `?first=0&last=0` for each middle part, then `?first=0&last=1`. **The
board keeps one file open at a time**, so two transfers cannot interleave.

```sh
# back a settings file up from the flash onto the USB device
curl -s 'http://<address>:8081/storage/file?path=/assets/fonts/default.tgf' \
     -o default.tgf
curl -s -X POST --data-binary @default.tgf \
     'http://<address>:8081/storage/file?path=/media/default.tgf'
```

## The answers

Every route that carries no bytes of a device answers JSON, and the statuses
are those of [the display](display-http.md#the-answers), plus `413` for a body
that one part cannot hold.
