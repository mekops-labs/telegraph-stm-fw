# Telegraph Firmware

Firmware for the Telegraph LED matrix clock and notification display.

Telegraph is a dual-MCU device. An STM32F105RB (the HAZK-03 module) drives all
display hardware; an edge MCU alongside it hosts the network stack. The two are
joined by a framed binary protocol over UART. **This repository holds the STM32
side**, built on [Apache NuttX](https://nuttx.apache.org/), plus the shared IPC
library both sides compile against.

## Display hardware

| Device | Role | Interface |
| :--- | :--- | :--- |
| TM1629A | 7-segment clock digits | 3-wire serial (PB3/PB4/PB5) |
| SM1626D | 70x14 main matrix, 21x14 sub matrix | Shift register (PB12–PB15, PA13) |
| DS3231 | RTC and ambient temperature | Bit-banged I²C (PC6/PC7) |

### Constraints that are not negotiable

These are properties of the board, verified against it. Getting any of them
wrong produces a hang or a visibly corrupt display.

- **The system clock is 36 MHz and must run from HSI.** The STM32F105 on this
  board hangs when the external crystal is enabled, so the clock tree is
  `HSI/2 -> PLL x9 -> 36 MHz`, with APB1 divided by 2 to stay within its 36 MHz
  ceiling. Never enable HSE.
- **PA13 needs a JTAG reclaim.** It carries the sub-screen's serial data, so
  `AFIO->MAPR` must disable JTAG/SWD before the pin is configured as GPIO. A
  hardware debugger cannot attach after that point until the next reset — plan
  debug sessions around it.
- **The DS3231 I²C is bit-banged.** PC6/PC7 do not map to an I²C peripheral on
  this part; moving to hardware I²C would need board rework.
- **The matrix scan must be continuous.** The display persists an image only
  while it is being scanned: 16 rows at 200 µs per row, a 312 Hz frame rate.
- **TM1629A digit positions are not contiguous.** Digits map to shift register
  bits `{0..9, 12, 13}`; bits 10 and 11 drive decorative LEDs.

## Build

The toolchain is standardized in a container image, so a host needs only
podman (or docker) and git.

```sh
podman build -t telegraph-fw -f Containerfile .
podman run --rm -it --userns=keep-id --security-opt label=disable \
    -v "$PWD:/src" telegraph-fw bash
```

The image carries the `arm-none-eabi` cross toolchain and the NuttX configure
prerequisites. Builds are cached through ccache.

## License

Apache-2.0 — see [LICENSE](LICENSE) and [NOTICE](NOTICE). Every `.c`/`.h` file
outside `third_party/` starts with `/* SPDX-License-Identifier: Apache-2.0 */`.
