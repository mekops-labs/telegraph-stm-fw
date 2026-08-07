# The display over HTTP

The wapp `tg-display` serves the panels, the digits and the clock of the STM32
on a listening socket of the engine. It holds no hardware grant of its own: it
reaches the board through [the broker](broker.md), and the engine binds the
port for it.

```json
{"sockets": [{"name": "http", "address": "tcp://0.0.0.0:8080",
              "role": "listen", "backlog": 2, "max_conns": 2}]}
```

The launch config of the broker names this wapp as one of its peers, thus
`"args": ["display"]`.

## The routes

| Method | Path | Body | Result |
| :--- | :--- | :--- | :--- |
| `GET` | `/state` | — | the clock, the temperature, the counts of the link and the version of the firmware |
| `PUT` | `/display/main` | the text | the text on the main panel |
| `PUT` | `/display/sub` | the text | the text on the sub panel |
| `PUT` | `/display/main/scroll` | the text | the text moves across the main panel |
| `PUT` | `/display/sub/scroll` | the text | the same on the sub panel |
| `PUT` | `/brightness` | `<digits> [panels]` | 0 turns a device off, and 8 is the full level |
| `PUT` | `/clock` | `<seconds> [minutes]` | the Unix time in UTC, and the offset of the local time |
| `DELETE` | `/display` | — | both panels lose their pixels |

A scroll takes its rate from the query: `?period=<milliseconds>&step=<pixels>`,
which default to 60 and 1.

```sh
curl -s http://<address>:8080/state
curl -s -X PUT --data-binary 'hello' http://<address>:8080/display/main
curl -s -X PUT --data-binary 'wapp' \
     'http://<address>:8080/display/sub/scroll?period=40'
```

**A text longer than its panel scrolls by itself**, thus `/display/main` covers
most of what a caller wants and the scroll routes exist for the rate alone.

## The answers

Every route answers JSON. A request the board accepted gives `{"ok":true}`.

| Status | Meaning |
| :--- | :--- |
| `200` | the board did it |
| `400` | the request itself is wrong: a level out of range, a query of zero |
| `404` | no such route |
| `409` | the board refused it, and the body names the reason |
| `504` | the board gave no reply, thus the broker gave up after its retries |

A `409` carries the code of the board and its meaning, such as
`{"ok":false,"nack":2,"error":"too long for this panel"}`. **That one is the
answer a caller meets in practice:** the board renders a moving text into a
source of 512 bytes for the main panel and half of that for the sub panel, thus
a text of about 40 characters fills the first and about 10 fills the second.

## The network

The radio is up only while a wapp holds it. The engine seeds `wifi-connect`,
which takes `WIFI_SSID` and `WIFI_PASS` from its launch config and grants
itself `/dev/wifi`. A board of a deployment joins at boot instead, through the
configuration of its own image.
