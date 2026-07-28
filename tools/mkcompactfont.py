#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

"""Build the compact font of the panels.

The font of the firmware takes 7 rows for a letter, thus one line fills a panel
of 14 rows. This font takes 5 rows for a letter in a cell of 7: one row above
carries a mark such as an acute, and one row below carries a mark such as an
ogonek. **Two of these lines fit a panel.**

The file has the same shape as the one that tools/mkfont.py writes, and the
header carries the cell. Thus the board reads either font.
"""

import argparse
import pathlib
import struct
import sys

MAGIC = 0x31464754  # "TGF1"
WIDTH = 3
ROWS = 7
ASCENT = 1
LETTER = 5

# Each glyph is 5 rows of 3 columns. A '#' is a pixel.
GLYPHS = {
    " ": ("...", "...", "...", "...", "..."),
    "!": (".#.", ".#.", ".#.", "...", ".#."),
    '"': ("#.#", "#.#", "...", "...", "..."),
    "'": (".#.", ".#.", "...", "...", "..."),
    "(": ("..#", ".#.", ".#.", ".#.", "..#"),
    ")": ("#..", ".#.", ".#.", ".#.", "#.."),
    "+": ("...", ".#.", "###", ".#.", "..."),
    ",": ("...", "...", "...", ".#.", "#.."),
    "-": ("...", "...", "###", "...", "..."),
    ".": ("...", "...", "...", "...", ".#."),
    "/": ("..#", "..#", ".#.", "#..", "#.."),
    "0": ("###", "#.#", "#.#", "#.#", "###"),
    "1": (".#.", "##.", ".#.", ".#.", "###"),
    "2": ("###", "..#", "###", "#..", "###"),
    "3": ("###", "..#", "###", "..#", "###"),
    "4": ("#.#", "#.#", "###", "..#", "..#"),
    "5": ("###", "#..", "###", "..#", "###"),
    "6": ("###", "#..", "###", "#.#", "###"),
    "7": ("###", "..#", "..#", "..#", "..#"),
    "8": ("###", "#.#", "###", "#.#", "###"),
    "9": ("###", "#.#", "###", "..#", "###"),
    ":": ("...", ".#.", "...", ".#.", "..."),
    ";": ("...", ".#.", "...", ".#.", "#.."),
    "<": ("..#", ".#.", "#..", ".#.", "..#"),
    "=": ("...", "###", "...", "###", "..."),
    ">": ("#..", ".#.", "..#", ".#.", "#.."),
    "?": ("###", "..#", ".##", "...", ".#."),
    "A": ("###", "#.#", "###", "#.#", "#.#"),
    "B": ("##.", "#.#", "##.", "#.#", "##."),
    "C": ("###", "#..", "#..", "#..", "###"),
    "D": ("##.", "#.#", "#.#", "#.#", "##."),
    "E": ("###", "#..", "##.", "#..", "###"),
    "F": ("###", "#..", "##.", "#..", "#.."),
    "G": ("###", "#..", "#.#", "#.#", "###"),
    "H": ("#.#", "#.#", "###", "#.#", "#.#"),
    "I": ("###", ".#.", ".#.", ".#.", "###"),
    "J": ("..#", "..#", "..#", "#.#", "###"),
    "K": ("#.#", "#.#", "##.", "#.#", "#.#"),
    "L": ("#..", "#..", "#..", "#..", "###"),
    "M": ("#.#", "###", "###", "#.#", "#.#"),
    "N": ("##.", "#.#", "#.#", "#.#", "#.#"),
    "O": ("###", "#.#", "#.#", "#.#", "###"),
    "P": ("###", "#.#", "###", "#..", "#.."),
    "Q": ("###", "#.#", "#.#", "###", "..#"),
    "R": ("###", "#.#", "##.", "#.#", "#.#"),
    "S": ("###", "#..", "###", "..#", "###"),
    "T": ("###", ".#.", ".#.", ".#.", ".#."),
    "U": ("#.#", "#.#", "#.#", "#.#", "###"),
    "V": ("#.#", "#.#", "#.#", "#.#", ".#."),
    "W": ("#.#", "#.#", "###", "###", "#.#"),
    "X": ("#.#", "#.#", ".#.", "#.#", "#.#"),
    "Y": ("#.#", "#.#", ".#.", ".#.", ".#."),
    "Z": ("###", "..#", ".#.", "#..", "###"),
    "[": ("###", "#..", "#..", "#..", "###"),
    "]": ("###", "..#", "..#", "..#", "###"),
    "_": ("...", "...", "...", "...", "###"),
    "°": ("##.", "##.", "...", "...", "..."),
}

# A letter of the Polish alphabet takes the shape of a letter above, plus a
# mark. The rows of the mark are outside the letter.
MARKED = [
    (0x0104, "A", "ogonek"), (0x0105, "a", "ogonek"),
    (0x0106, "C", "acute"),  (0x0107, "c", "acute"),
    (0x0118, "E", "ogonek"), (0x0119, "e", "ogonek"),
    (0x0141, "L", "stroke"), (0x0142, "l", "stroke"),
    (0x0143, "N", "acute"),  (0x0144, "n", "acute"),
    (0x00D3, "O", "acute"),  (0x00F3, "o", "acute"),
    (0x015A, "S", "acute"),  (0x015B, "s", "acute"),
    (0x0179, "Z", "acute"),  (0x017A, "z", "acute"),
    (0x017B, "Z", "dot"),    (0x017C, "z", "dot"),
]


def columns(art, mark=None):
    """Give the columns of one cell. Bit 0 is the row at the top."""
    cols = [0] * WIDTH

    for row, line in enumerate(art):
        for col in range(WIDTH):
            if line[col] == "#":
                cols[col] |= 1 << (row + ASCENT)

    if mark == "acute":
        cols[2] |= 1 << 0
    elif mark == "dot":
        cols[1] |= 1 << 0
    elif mark == "ogonek":
        cols[1] |= 1 << (ROWS - 1)
    elif mark == "stroke":
        # A line through the letter, on the column of its stem.
        cols[0] |= 1 << (ASCENT + 2)

    return cols


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("-o", "--output", type=pathlib.Path, required=True)
    parser.add_argument("--show", action="store_true")
    args = parser.parse_args()

    entries = {}

    for ch, art in GLYPHS.items():
        entries[ord(ch)] = columns(art)

        # A small letter takes the shape of the capital one. The cell has no
        # room for two shapes, and the text stays readable.
        if "A" <= ch <= "Z":
            entries[ord(ch.lower())] = columns(art)

    for cp, base, mark in MARKED:
        art = GLYPHS[base.upper()]
        entries[cp] = columns(art, mark)

    out = bytearray()
    out += struct.pack("<IHBBBBBB", MAGIC, len(entries), WIDTH, ROWS,
                       ASCENT, 0, 0, 0)

    for cp in sorted(entries):
        out += struct.pack("<H", cp)
        out += struct.pack("<%dH" % WIDTH, *entries[cp])

    if len(entries) > 128:
        sys.exit(f"{len(entries)} characters, the limit of the board is 128")

    if args.show:
        for cp in (ord("A"), ord("g"), 0x0144, 0x0105, 0x017C):
            cols = entries[cp]
            print(f"U+{cp:04X}")
            for row in range(ROWS):
                print("  " + "".join(
                    "#" if cols[c] & (1 << row) else "." for c in range(WIDTH)))

    args.output.write_bytes(out)
    print(f"{args.output}: {len(entries)} characters in a cell of "
          f"{WIDTH}x{ROWS}, {len(out)} bytes")


if __name__ == "__main__":
    main()
