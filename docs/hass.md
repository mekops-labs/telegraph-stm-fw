# The display in Home Assistant

The wapp `tg-hass` holds an outbound socket to an MQTT broker, publishes the
discovery of the entities, and translates their commands into
[the request set of the display](display.md). It reaches neither the broker of
the link nor the STM32, thus adding Home Assistant to a board is adding one
wapp to its desired state, and removing it is dropping that wapp.

## What Home Assistant shows

| Entity | Platform | What it does |
| :--- | :--- | :--- |
| Main panel | `text` | the text of the large panel; `\n` makes two lines; empty gives the panel back to the board |
| Sub panel | `text` | the text of the small panel; empty shows the day of the week |
| External temperature | `text` | a decimal degree for the temperature digits; empty returns them to the DS3231 |
| Digit brightness | `number`, config | 0 turns the digits off, 8 is the full level |
| Panel brightness | `number`, config | the same for both panels |
| Display off at, Display on at | `time`, config | the period without light; equal values disable it |
| Temperature | `sensor` | what the DS3231 reads, in °C, to one decimal |
| Frames, CRC errors, Resyncs | `sensor`, diagnostic | the counters of the link |
| Display firmware | `sensor`, diagnostic | the version the STM32 reports |

The readings ride one document on the state topic, and each entity reads a
field of it. **Every reading is a request that crosses the link the scan loop
of the STM32 shares**, thus the cadence is a load decision — the counters
above are the instrument for it.

The device carries the identifier the control plane publishes for it, thus
Home Assistant shows one Telegraph and not two — the sensors of
[Deputy](https://gitlab.com/mekops/wanted/deputy) and these entities on one
device page.

**The board is the source of truth for the settings.** The adapter reads the
levels, the period without light and the outside temperature from the board at
its start and with every reading, and it publishes them as the read-backs. A
restart of the adapter thus shows what a person set before, and a change to one
entity keeps its partner: the request to the board carries both levels, and both
times of the period. A command that arrives while the settings are unknown is
dropped. **A read-back follows the board**: a command the board refuses, or
one that is dropped, publishes nothing, and the next reading restores what the
board holds. A disabled period shows as two equal times.

**An empty text gives the panel back to the board.** The sub panel then shows
the day of the week, and the main panel stays empty. A text of the main panel
turns the two characters `\n` into a line feed, thus Home Assistant, whose
input is one line, can set two lines. The board takes 64 bytes of text and a
Polish letter takes two, thus the adapter cuts a longer text at a character
boundary, logs the cut, and Home Assistant offers 32 characters for the main
panel. The read-back of a panel is the document `{"text":"…"}`, never an empty
message: an empty retained message would delete the read-back, and Home
Assistant would show the entity as unknown after its own restart.
Home Assistant shows an empty text, and an entity whose read-back it cannot
read, as `unknown` in the field of the entity. The panel is not affected, and a
text set from Home Assistant replaces the read-back. A read-back published as
plain text by an earlier version reads this way until the next text is set.

**The outside temperature is a number or nothing.** The entity takes `-12.5`,
`21` or an empty value, from -99.0 to 199.9. Anything else leaves the board
as it was and the next reading shows the true value. The digits round to whole
degrees, and a value of three characters alternates its parts every 3 s. The
value lives in the RAM of the board: a reset of the STM32 clears it, a restart
of the adapter does not. The state document carries it as `ext_temperature`,
empty when the board holds none.

**Availability comes from the last will.** The broker publishes `offline` on
the availability topic the moment the session drops, thus a board that loses
power is unavailable in Home Assistant within the keepalive rather than at the
next missed report.

## The topics

With the defaults, and a device named `telegraph-01`:

| Topic | Direction |
| :--- | :--- |
| `homeassistant/device/telegraph-01/config` | the discovery, retained |
| `telegraph/telegraph-01/availability` | `online`, or `offline` from the will |
| `telegraph/telegraph-01/main/set` | the text of the main panel |
| `telegraph/telegraph-01/main` | what that entity reads back, as `{"text":"…"}`, retained |
| `telegraph/telegraph-01/sub/set`, `…/sub` | the same for the sub panel |
| `telegraph/telegraph-01/brightness/digits/set`, `…/digits` | the digits |
| `telegraph/telegraph-01/brightness/panels/set`, `…/panels` | the panels |
| `telegraph/telegraph-01/dnd/start/set`, `…/start` | the minute the display stops |
| `telegraph/telegraph-01/dnd/end/set`, `…/end` | the minute it starts again |
| `telegraph/telegraph-01/temperature/ext/set` | the outside temperature |
| `telegraph/telegraph-01/state` | the readings and the outside temperature, retained |

## The configuration

The endpoint of the broker is the address of the socket grant. Everything else
comes from the environment of the desired state, thus one image at one digest
serves two boards.

```json
{
  "name": "tg-hass",
  "policy": {
    "Sockets": [{"Name": "mqtt", "Address": "tcp://broker.lan:1883"}]
  },
  "envs": ["HASS_DEVICE=telegraph-01", "HASS_NAME=Telegraph"]
}
```

| Variable | Default | What it names |
| :--- | :--- | :--- |
| `HASS_DEVICE` | `telegraph` | the device identifier, shared with the control plane |
| `HASS_NAME` | `Telegraph` | what Home Assistant calls the device |
| `HASS_PREFIX` | `telegraph` | the prefix of the topics above |
| `HASS_DISCOVERY` | `homeassistant` | the discovery prefix of Home Assistant |
| `HASS_USER`, `HASS_PASS` | unset | the credentials of the broker |
| `TELEGRAPH_CLIENT` | `hass` | the name of this client of the display |
| `TELEGRAPH_SOCKET` | `mqtt` | the socket grant to use |

**The four variables above are every one the board allows**, thus the cadence
of the readings rides a config mount instead:

```json
"config": [{"path": "/etc/tg-hass.conf", "content": "interval_s=30"}]
```

A board with no such mount takes 30 seconds, and a value below 5 is raised to
5. The supervisor turns that entry into the mount the wapp reads; a launch
config names the mount itself (`{"name":"config","path":"/etc/tg-hass.conf",
"options":"interval_s=30"}`).

The deployment of this repository carries the credentials as `MQTT_USER` and
`MQTT_PASS` of the make invocation, thus no file here holds them:

```sh
make deploy REGISTRY=<host:port> BROKER=<host:port> \
     MQTT_USER=<user> MQTT_PASS=<password>
```

**A password in `envs` is signed, not encrypted.** It is readable in the store
of the control plane, in the twin its API serves, and in the persisted state on
the flash of the device. An anonymous broker, or a token issued per device, is
the answer until the desired state carries a secret.

## What it needs of the engine

The wapp opens its socket non-blocking, since it waits for a message of the
broker and still keeps its own keepalive. A build of the engine older than that
support blocks in the read and the broker drops the session.

## Testing it without hardware

`wapps/tests/hass.sh` runs a broker in a container, the wapps on a host build
of the engine, and a program that answers as the STM32 does:

```sh
WANTED=<path to a wanted-cli> wapps/tests/hass.sh
```

It asserts the discovery, the availability, a command reaching the board, the
entity reading back what was set, and the will marking the device offline.
