#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

"""Build a sprite for the panels of Telegraph.

A sprite holds its frames side by side in one bitmap. The board moves a window
over that bitmap, and a step of the width of one frame thus gives the frames in
turn.

The file starts with a header of 10 bytes:

    magic  u32   "TGS1"
    width  u16   the width of the whole bitmap
    height u16   its height
    step   u8    the width of one frame
    flags  u8    bit 0 moves the window down instead of across

The pixels follow. They go row by row, each row starts at a byte, and bit 7 of
a byte is the pixel at the left.
"""

import argparse
import math
import pathlib
import struct

MAGIC = 0x31534754  # "TGS1"
SIZE = 13           # one frame is this many pixels each way
                    #
                    # Note: an odd number gives a middle pixel. With an even
                    # one the middle falls between two pixels, and the rays of
                    # the sun then land on one side or the other.


def heart(scale):
    """Give one frame of a heart at the given scale."""
    grid = [[False] * SIZE for _ in range(SIZE)]

    for py in range(SIZE):
        for px in range(SIZE):
            # Put the middle of the pixel into the range of the curve.
            x = ((px + 0.5) / SIZE * 2.0 - 1.0) * 1.4 / scale
            y = (1.0 - (py + 0.5) / SIZE * 2.0) * 1.4 / scale
            y += 0.25 / scale

            t = x * x + y * y - 1.0
            grid[py][px] = (t * t * t - x * x * y * y * y) <= 0.0

    return grid


def sun(rays, phase):
    """Give one frame of a sun with rays of the given length."""
    grid = [[False] * SIZE for _ in range(SIZE)]
    mid = (SIZE - 1) / 2.0
    core = 3.1

    for py in range(SIZE):
        for px in range(SIZE):
            dx = px - mid
            dy = py - mid
            if math.hypot(dx, dy) <= core:
                grid[py][px] = True

    # Eight rays leave the core. Each one is a short line of pixels.
    for i in range(8):
        angle = (math.pi / 4.0) * i + phase
        for step in range(rays):
            r = core + 1.0 + step
            x = int(round(mid + math.cos(angle) * r))
            y = int(round(mid + math.sin(angle) * r))
            if 0 <= x < SIZE and 0 <= y < SIZE:
                grid[y][x] = True

    return grid


def pack(frames, step, vertical=False):
    """Put the frames side by side and give the bytes of the file."""
    w = SIZE * len(frames)
    h = SIZE
    stride = (w + 7) // 8
    bits = bytearray(stride * h)

    for index, grid in enumerate(frames):
        for py in range(SIZE):
            for px in range(SIZE):
                if grid[py][px]:
                    x = index * SIZE + px
                    bits[py * stride + x // 8] |= 0x80 >> (x % 8)

    head = struct.pack("<IHHBB", MAGIC, w, h, step, 1 if vertical else 0)
    return head + bytes(bits)


def show(frames):
    """Print the frames, so that a person checks the shapes."""
    for py in range(SIZE):
        print("  ".join("".join("#" if g[py][px] else "." for px in range(SIZE))
                        for g in frames))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sprite", choices=["heart", "sun"])
    parser.add_argument("-o", "--output", type=pathlib.Path, required=True)
    parser.add_argument("--show", action="store_true")
    args = parser.parse_args()

    if args.sprite == "heart":
        # The heart grows and falls back, thus it beats.
        frames = [heart(s) for s in (0.82, 0.95, 1.05, 0.95)]
    else:
        # The rays grow and turn, thus the sun shines.
        frames = [sun(r, p) for r, p in
                  ((1, 0.0), (2, 0.0), (3, math.pi / 8), (2, math.pi / 8))]

    data = pack(frames, SIZE)

    if args.show:
        show(frames)

    args.output.write_bytes(data)
    print(f"{args.output}: {len(frames)} frames of {SIZE}x{SIZE}, "
          f"{len(data)} bytes")


if __name__ == "__main__":
    main()
