# The display in Home Assistant

The wapp `tg-hass` holds an outbound socket to an MQTT broker, publishes the
discovery of the entities, and translates their commands into
[the request set of the display](display.md). It reaches neither the broker of
the link nor the STM32, thus adding Home Assistant to a board is adding one
wapp to its desired state, and removing it is dropping that wapp.

## What Home Assistant shows

| Entity | Platform | What it does |
| :--- | :--- | :--- |
| Main panel | `text` | the text of the large panel |
| Sub panel | `text` | the text of the small panel |
| Digit brightness | `number` | 0 turns the digits off, 8 is the full level |
| Panel brightness | `number` | the same for both panels |
| Temperature | `sensor` | what the DS3231 reads, in °C |
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
| `telegraph/telegraph-01/main` | what that entity reads back, retained |
| `telegraph/telegraph-01/sub/set`, `…/sub` | the same for the sub panel |
| `telegraph/telegraph-01/brightness/digits/set`, `…/digits` | the digits |
| `telegraph/telegraph-01/brightness/panels/set`, `…/panels` | the panels |
| `telegraph/telegraph-01/state` | the readings, retained |

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
