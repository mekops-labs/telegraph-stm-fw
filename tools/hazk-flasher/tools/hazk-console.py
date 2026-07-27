#!/usr/bin/env python3
"""Host driver for the hazk-flasher USB serial console.

This program identifies or flashes the STM32 target through the ESP32 bridge.
It needs no WiFi. The commands are:

    hazk-console.py identify
    hazk-console.py flash firmware.bin
    hazk-console.py monitor

Note: the program uses termios, not pyserial. Thus it runs with a standard
Python installation.
"""

import argparse
import os
import sys
import termios
import time

CHUNK = 256
DEFAULT_PORT = "/dev/ttyACM0"


class Link:
    def __init__(self, port, baud=115200):
        self.fd = os.open(port, os.O_RDWR | os.O_NOCTTY)
        attrs = termios.tcgetattr(self.fd)
        iflag, oflag, cflag, lflag, _ispeed, _ospeed, cc = attrs

        # Set the port to 8N1. Disable the flow control and the modem
        # control lines.
        iflag = 0
        oflag = 0
        lflag = 0
        cflag = termios.CS8 | termios.CREAD | termios.CLOCAL
        cc = list(cc)
        cc[termios.VMIN] = 0
        cc[termios.VTIME] = 0
        speed = getattr(termios, f"B{baud}")

        termios.tcsetattr(
            self.fd, termios.TCSANOW,
            [iflag, oflag, cflag, lflag, speed, speed, cc],
        )
        # Wait for the USB port to become stable. Then remove the start-up
        # message from the buffer.
        time.sleep(0.3)
        termios.tcflush(self.fd, termios.TCIOFLUSH)
        self.buf = b""

    def close(self):
        os.close(self.fd)

    def write(self, data):
        while data:
            n = os.write(self.fd, data)
            data = data[n:]

    def readline(self, timeout=10.0):
        """Return one line. Return None after the timeout."""
        deadline = time.monotonic() + timeout
        while True:
            if b"\n" in self.buf:
                line, self.buf = self.buf.split(b"\n", 1)
                return line.decode("utf-8", "replace").strip()
            if time.monotonic() > deadline:
                return None
            try:
                chunk = os.read(self.fd, 4096)
            except BlockingIOError:
                chunk = b""
            if chunk:
                self.buf += chunk
            else:
                time.sleep(0.005)

    def command(self, cmd):
        self.buf = b""
        termios.tcflush(self.fd, termios.TCIFLUSH)
        self.write(cmd.encode() + b"\n")


def wait_for(link, timeout=15.0, echo=True):
    """Read the lines until an OK, ERR or READY reply arrives."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        line = link.readline(timeout=deadline - time.monotonic())
        if line is None:
            break
        if echo and line:
            print(f"  {line}")
        if line.startswith(("OK", "ERR", "READY")):
            return line
    return None


def cmd_identify(link, _args):
    link.command("id")
    reply = wait_for(link)
    if reply is None:
        print("FAILED: no reply from flasher", file=sys.stderr)
        return 1
    return 0 if reply.startswith("OK") else 1


def cmd_flash(link, args):
    with open(args.binary, "rb") as f:
        image = f.read()
    total = len(image)
    print(f"Flashing {args.binary} ({total} bytes)")

    link.command(f"flash {total}")
    reply = wait_for(link, timeout=20.0)
    if reply is None or not reply.startswith("READY"):
        print(f"FAILED: flasher not ready ({reply})", file=sys.stderr)
        return 1

    sent = 0
    while sent < total:
        chunk = image[sent:sent + CHUNK]
        link.write(chunk)
        sent += len(chunk)

        # The device sends an ACK for each block that it writes. It sends OK
        # for the last block.
        #
        # Note: the host controls the rate. Thus the fast USB port does not
        # send more data than the 57600 baud link accepts.
        line = link.readline(timeout=15.0)
        if line is None:
            print(f"\nFAILED: timeout after {sent} bytes", file=sys.stderr)
            return 1
        if line.startswith("ERR"):
            print(f"\nFAILED: {line}", file=sys.stderr)
            return 1
        if line.startswith("OK"):
            print(f"\n{line}")
            return 0
        print(f"\r  {sent}/{total} bytes", end="", flush=True)

    print("\nFAILED: image consumed without final OK", file=sys.stderr)
    return 1


def cmd_monitor(link, args):
    print("Monitoring (Ctrl-C to stop)")
    deadline = time.monotonic() + args.seconds if args.seconds else None
    try:
        while deadline is None or time.monotonic() < deadline:
            line = link.readline(timeout=1.0)
            if line:
                print(line, flush=True)
    except KeyboardInterrupt:
        pass
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("-p", "--port", default=DEFAULT_PORT)
    sub = parser.add_subparsers(dest="cmd", required=True)

    sub.add_parser("identify", help="sync with the STM32 bootloader and read its chip ID")

    p_flash = sub.add_parser("flash", help="write a .bin to the STM32 at 0x08000000")
    p_flash.add_argument("binary")

    p_mon = sub.add_parser("monitor", help="print target serial output")
    p_mon.add_argument("-s", "--seconds", type=float, default=0)

    args = parser.parse_args()
    link = Link(args.port)
    try:
        handler = {"identify": cmd_identify, "flash": cmd_flash, "monitor": cmd_monitor}
        return handler[args.cmd](link, args)
    finally:
        link.close()


if __name__ == "__main__":
    sys.exit(main())
