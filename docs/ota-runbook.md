# Updating the edge MCU without a cable

The edge MCU takes a new engine image from the control plane, over Wi-Fi. The
image carries the supervisor with it, thus this is also how a change to the
supervisor reaches the board. **USB is the recovery path, not the update path.**

Verified on the bench: a change to the supervisor reached the board as
`download → digest verified → staged → reboot → confirmed`, with no cable.

## What has to be running

| Piece | Where | Why |
| :--- | :--- | :--- |
| Deputy | a host of the LAN | holds the desired state and the firmware intent |
| An HTTP server | the same host | serves the image the device fetches |
| An OCI registry | the LAN | holds the wapp images |

The board reaches Deputy at the address its launch config names (`manager`),
and the registry at the address of the `registry` socket. Both are in the
board's own configuration, thus a move of either needs a new image.

## The steps

```sh
# 1. build the engine image with the supervisor of that build inside it
cd <wanted-engine>
OTA_PROFILE=s3-telegraph-sheriff just build     # in the ESP-IDF container

# 2. serve it
cp platform/esp-idf/project/build/wanted-esp-idf.bin /srv/wanted.bin
python3 -m http.server 8000 --bind 0.0.0.0      # from /srv

# 3. tell the device to take it
deputy device firmware push \
    --version "$(strings /srv/wanted.bin | grep -oE '[0-9]+\.[0-9]+\.[0-9]+\+g[0-9a-f]+\.[0-9]+' | head -1)" \
    --digest "sha256:$(sha256sum /srv/wanted.bin | cut -d' ' -f1)" \
    --source "http://<host>:8000/wanted.bin" \
    urn:wanted:telegraph-01

# 4. watch it land
deputy device show urn:wanted:telegraph-01      # Engine.OTAStatus and Engine.Version
```

`OTAStatus` reads 1 while the image comes down, 2 once it is staged, and 3 when
the device has booted it and confirmed. A 4 means it booted and did not
confirm: the device reverted, `OTAFailedVersion` names the image, and that
version never runs again — cut a new one rather than re-cutting the same.

**The version has to change, and it comes from a configure step.** The build
stamps `<semver>+g<commit>.<timestamp>` at configure time, thus a rebuild alone
keeps the old string and the device sees no new firmware. Touch
`platform/esp-idf/components/wanted_engine/CMakeLists.txt` to force it.

## The wapps take another path

The wapps of the device live in its registry, and the control plane installs
them from an OCI registry:

```sh
cd <telegraph-fw>
make ota-image                    # the STM32 firmware into the wapp that writes it
make deploy REGISTRY=<host>:5000  # push the images, then the desired state
```

Refer to [the broker](broker.md) for what each wapp is granted.

## Reading the logs of a board with no console

The wapp `tg-logs` takes a log mount of the engine and a listening socket, thus
what any wapp printed is readable from the network:

```sh
curl http://<address>:8081/            # the wapps that have a log
curl http://<address>:8081/supervisor  # what the supervisor printed
```

The supervisor's own diagnostics reach it because its launch config routes the
`err` slot to `log` — `std.debug.print` writes there. **This wapp is seeded into
the firmware**, not delivered from the registry: the questions it answers are
the ones a board with a broken registry path raises.

Note: a log is a ring of 2 KiB, and its node gives the whole ring in one read
and then EOF. A reader that asks for less gets the oldest bytes alone, which
looks like a log that never moves.

## When USB is the answer

- **The supervisor cannot reach the control plane.** A supervisor that fails to
  fetch cannot be told about a new image, thus a change that breaks fetching
  takes the cable back. This has happened twice, both times as `fetch skipped —
  out of heap for the response buffer`: once from a heap 2 KiB short, once when
  the desired state grew and the fetch buffers no longer fit beside it. **There
  is no remote recovery from it**, which is why the heap of the supervisor now
  carries a deliberate margin.
- **A bootloader or a partition table changed.** The image the control plane
  installs goes into an app slot, and nothing else.

```sh
esptool.py -c esp32s3 -p /dev/ttyACM0 -b 460800 \
    --before default_reset --after hard_reset write_flash 0x0 \
    dist/esp-idf/wanted-esp32s3-merged.bin
```

Note: a full flash keeps `/data`, thus the Wi-Fi credentials and the state of
the supervisor survive it. `erase_flash` first is what removes them, and it
also empties the registry of its wapps.
