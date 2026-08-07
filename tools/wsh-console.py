#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Drive the shell of the edge MCU over its USB console.

The engine of the edge MCU takes its commands on the USB-Serial/JTAG port, and
a wapp of the device starts with three of them. This script waits for the board
to settle, sends each argument as one line, and prints everything the board
says.

Usage:
  tools/wsh-console.py "status"
  SETTLE=25 tools/wsh-console.py "create tg-ota" "set_config tg-ota {...}" \\
      "start tg-ota"

Environment:
  PORT    the device, /dev/ttyACM0 by default
  SETTLE  seconds to wait before the first command, 3 by default. A board that
          has just started runs its bring-up tests first.
  TAIL    seconds to keep reading after the last command, 10 by default. A run
          of the OTA wapp needs far more.
"""
import os
import select
import sys
import termios
import time

PORT = os.environ.get("PORT", "/dev/ttyACM0")
SETTLE = float(os.environ.get("SETTLE", "3"))
TAIL = float(os.environ.get("TAIL", "10"))

# The gap between two commands. The shell reads one line at a time, and a burst
# loses the lines behind the first.
GAP = 1.5


def raw(fd):
    """Take the line discipline out of the way.

    A fresh port echoes, thus every byte the board prints returns to it as a
    command of its own.
    """
    mode = termios.tcgetattr(fd)
    iflag, oflag, cflag, lflag, ispeed, ospeed, cc = mode
    iflag &= ~(termios.IGNBRK | termios.BRKINT | termios.PARMRK |
               termios.ISTRIP | termios.INLCR | termios.IGNCR |
               termios.ICRNL | termios.IXON)
    oflag &= ~termios.OPOST
    lflag &= ~(termios.ECHO | termios.ECHONL | termios.ICANON |
               termios.ISIG | termios.IEXTEN)
    termios.tcsetattr(fd, termios.TCSANOW,
                      [iflag, oflag, cflag, lflag, ispeed, ospeed, cc])
    termios.tcflush(fd, termios.TCIFLUSH)


def main(cmds):
    fd = os.open(PORT, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    raw(fd)
    started = time.time()
    sent = 0
    last = 0.0

    while True:
        ready, _, _ = select.select([fd], [], [], 0.2)
        if ready:
            try:
                chunk = os.read(fd, 4096)
            except BlockingIOError:
                chunk = b""
            if chunk:
                sys.stdout.write(chunk.decode("utf-8", "replace"))
                sys.stdout.flush()

        now = time.time()
        if now - started > SETTLE and sent < len(cmds) and now - last > GAP:
            os.write(fd, (cmds[sent] + "\n").encode())
            sys.stdout.write("\n>>> %s\n" % cmds[sent])
            sys.stdout.flush()
            sent += 1
            last = now

        if sent >= len(cmds) and now - last > TAIL:
            break

    os.close(fd)


if __name__ == "__main__":
    main(sys.argv[1:])
