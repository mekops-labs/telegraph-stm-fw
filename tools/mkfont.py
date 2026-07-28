#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

"""Build the extended font of the panels.

The font of the firmware holds the ASCII table in a cell of 5x7. This tool
takes those shapes, it adds a mark to some of them, and it writes a file for
the flash of the board.

The cell of the file has 10 rows. Two rows above the letter carry a mark such
as an acute, and one row below carries a mark such as an ogonek. Thus a letter
keeps its full height.
"""

import argparse
import pathlib
import re
import struct
import sys

MAGIC = 0x31464754  # "TGF1"
WIDTH = 5
ROWS = 10
ASCENT = 2
MAX_GLYPHS = 96

# The rows of the marks inside the cell.
ROW_ACUTE_HIGH = 0
ROW_ACUTE_LOW = 1
ROW_OGONEK = ROWS - 1

# Each letter gives the character of the ASCII table that carries its shape,
# and the mark that goes with it.
POLISH = [
    (0x0104, "A", "ogonek"),   # A with ogonek
    (0x0105, "a", "ogonek"),
    (0x0106, "C", "acute"),    # C with acute
    (0x0107, "c", "acute"),
    (0x0118, "E", "ogonek"),   # E with ogonek
    (0x0119, "e", "ogonek"),
    (0x0141, "L", "stroke"),   # L with stroke
    (0x0142, "l", "stroke"),
    (0x0143, "N", "acute"),    # N with acute
    (0x0144, "n", "acute"),
    (0x00D3, "O", "acute"),    # O with acute
    (0x00F3, "o", "acute"),
    (0x015A, "S", "acute"),    # S with acute
    (0x015B, "s", "acute"),
    (0x0179, "Z", "acute"),    # Z with acute
    (0x017A, "z", "acute"),
    (0x017B, "Z", "dot"),      # Z with dot above
    (0x017C, "z", "dot"),
]

# The last entry of the table carries no comma, thus the comma is optional.
GLYPH_RE = re.compile(
    r"\{\s*((?:0x[0-9a-fA-F]{2}\s*,\s*){4}0x[0-9a-fA-F]{2})\s*\}\s*,?\s*/\*(.*?)\*/"
)


def read_base_font(path):
    """Take the shapes of the ASCII table from the source of the firmware."""
    text = path.read_text(encoding="utf-8")
    body = text.split("g_font5x7[][FONT5X7_WIDTH] =", 1)
    if len(body) != 2:
        sys.exit(f"{path}: the table of the font is absent")

    glyphs = {}
    code = 0x20
    for match in GLYPH_RE.finditer(body[1]):
        cols = [int(v, 16) for v in match.group(1).split(",")]
        glyphs[chr(code)] = cols
        code += 1

    if code - 1 != 0x7E:
        sys.exit(f"{path}: the table ends at {code - 1:#04x}, not at 0x7e")

    return glyphs


def add_mark(cols, mark):
    """Put a mark into the cell, and give the columns of 10 rows."""
    out = [c << ASCENT for c in cols]

    if mark == "acute":
        # A short line that goes up to the right, above the letter.
        out[2] |= 1 << ROW_ACUTE_LOW
        out[3] |= 1 << ROW_ACUTE_HIGH
    elif mark == "dot":
        out[2] |= 1 << ROW_ACUTE_LOW
    elif mark == "ogonek":
        # A tail below the letter, at the right of its centre.
        out[3] |= 1 << ROW_OGONEK
    elif mark == "stroke":
        # A line through the letter. The column of the stem carries it.
        stem = 1 if any(out) else 0
        for col in range(WIDTH):
            if out[col]:
                stem = col
                break
        out[stem] |= 1 << (ASCENT + 3)
        if stem + 1 < WIDTH:
            out[stem + 1] |= 1 << (ASCENT + 2)
    else:
        sys.exit(f"the mark {mark} is unknown")

    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", type=pathlib.Path,
                        default=pathlib.Path(__file__).parent.parent /
                        "boards/hazk03-stm32f105rb/src/font5x7.c",
                        help="the source that holds the ASCII table")
    parser.add_argument("-o", "--output", type=pathlib.Path, required=True)
    args = parser.parse_args()

    base = read_base_font(args.base)

    entries = []
    for cp, letter, mark in POLISH:
        if letter not in base:
            sys.exit(f"the letter {letter} is absent from the base font")
        entries.append((cp, add_mark(base[letter], mark)))

    entries.sort(key=lambda e: e[0])

    if len(entries) > MAX_GLYPHS:
        sys.exit(f"{len(entries)} characters, the limit is {MAX_GLYPHS}")

    out = bytearray()
    out += struct.pack("<IHBBBBBB", MAGIC, len(entries), WIDTH, ROWS,
                       ASCENT, 0, 0, 0)
    for cp, cols in entries:
        out += struct.pack("<H", cp)
        out += struct.pack("<%dH" % WIDTH, *cols)

    args.output.write_bytes(out)
    print(f"{args.output}: {len(entries)} characters, {len(out)} bytes")


if __name__ == "__main__":
    main()
