# HAZK-03 Flasher (ESP32)

This project makes an ESP32 into a firmware flasher and a serial bridge for STM32 microcontrollers. It uses the STM32 system bootloader on a UART. A USB serial console or a web interface controls the flash operation.

## Features

*   **USB serial console:** identify and flash the target over USB without a network. Refer to [Serial console](#serial-console).
*   **Flash operation from a browser:** send a `.bin` file from a browser to the STM32. This function needs the WiFi credentials.
*   **Serial bridge:** between flash operations, the target output goes to the USB port of the ESP32. With WiFi, it also goes to a TCP port.
*   **Automatic entry to the bootloader:** the flasher sets the `BOOT0` and `NRST` pins to start the bootloader.
*   **Indication of the progress:** the flasher shows the progress of the transfer and the status on the LED.

## Hardware Connections

The default target is the **Seeed XIAO ESP32-S3**. A second environment keeps the wiring of the **Waveshare ESP32 One**.

Note: the pins come from build flags in `platformio.ini`. Thus a new board needs only a new environment.

### Seeed XIAO ESP32-S3 (`env:xiao_esp32s3`, default)

| XIAO pin | GPIO | STM32 pin | Function |
| :--- | :--- | :--- | :--- |
| D0 | 1 | PA10 (USART1_RX) | UART TX (ESP32 sends to STM32) |
| D1 | 2 | PA9 (USART1_TX) | UART RX (ESP32 receives from STM32) |
| D3 | 4 | BOOT0 | Bootloader control |
| D4 | 5 | NRST | Reset control (open-drain) |
| onboard | 21 | — | Status LED (active LOW) |
| GND | — | GND | Common ground |

The firmware does not use UART0 on D6/D7. The ROM of the ESP32-S3 writes boot messages on that port at each reset. Those messages corrupt the bootloader sequence on the STM32.

Note: the debug console uses the native USB-Serial/JTAG peripheral. Thus it needs no pins on the header.

Note: the firmware does not use GPIO3 on D2, because it is a strapping pin.

### Waveshare ESP32 One (`env:waveshare_esp32_one`)

| ESP32 Pin | STM32 Pin | Function |
| :--- | :--- | :--- |
| IO 13 | RX | UART TX (ESP32 sends to STM32) |
| IO 14 | TX | UART RX (ESP32 receives from STM32) |
| IO 23 | BOOT0 | Bootloader Control |
| IO 18 | NRST | Reset Control |
| IO 21 | LED | Status LED |
| GND | GND | Common Ground |

## Setup & Installation

### 1. Prerequisites
*   Visual Studio Code
*   PlatformIO Extension

### 2. Configuration (optional)
The WiFi is optional. Without the credentials, the firmware builds without the network stack. The USB serial console then controls all functions.

To enable the web interface, the OTA and the TCP serial bridge, make a `credentials.ini` file in the project directory. Git ignores this file.
```ini
[secret]
build_flags =
    -D WIFI_SSID='"YourSSID"'
    -D WIFI_PASS='"YourPassword"'
```

### 3. Build and upload
Open the project in VS Code with PlatformIO, then select **Upload**. As an alternative, build in a container and use `esptool`:

```sh
podman build -t hazk-pio -f Containerfile .
podman run --rm --userns=keep-id --security-opt label=disable \
    -v "$PWD:/project" -v "$HOME/.cache/hazk-pio:/pio" \
    hazk-pio pio run -e xiao_esp32s3
```

Note: the same image builds the STM32 target firmware. For that, mount the other project at `/project`.

## Serial console

The USB port is a command channel with lines. The rate is 115200 baud. This path needs no network.

| Command | Effect |
| :--- | :--- |
| `id` | Reset into the bootloader, sync, and report the chip ID |
| `run` | Reset the target into normal run mode |
| `boot` | Reset into the bootloader and stay there |
| `flash <size>` | Erase, then accept `<size>` raw bytes and write them from `0x08000000` |
| `help` | List commands |

Each reply starts with `OK`, `ERR` or `INFO`. Thus a program can read the replies.

Note: during a `flash` command the device sends an ACK for each block of 256 bytes. Thus the host controls the rate, and the fast USB link does not send more data than the 57600 baud bootloader accepts.

`tools/hazk-console.py` drives all of this:

```sh
./tools/hazk-console.py identify
./tools/hazk-console.py flash ../hazk-fw/.pio/build/genericSTM32F105RB/firmware.bin
./tools/hazk-console.py monitor
```

If the console is idle, the target output goes to the USB port. Thus the `monitor` command also shows the log of the target.

## Web interface

Requires WiFi credentials.

1.  **Power Up:** Connect the ESP32 and the STM32 target.
2.  **Connect:** Open the Serial Monitor (Baud 115200) to see the assigned IP address.
3.  **Web Interface:** Navigate to `http://<ESP32-IP>/` in your web browser.
4.  **Identify:** Click "Identify Target" to verify wiring and chip detection.
5.  **Flash:** Select your STM32 firmware (`.bin`) and click "Flash Firmware".

## Development

The `Containerfile` file gives a PlatformIO build environment. It uses the amd64 architecture only, because the toolchains are binaries for the host architecture.

Note: the project also includes a Dev Container configuration for VS Code.

## License
MIT
