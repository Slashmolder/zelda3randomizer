#!/usr/bin/env python3
"""Regenerate assets/rando/custom_item_gfx.png from local custom art.

The PNG is the committed source for the `kRandoCustomItemGfx` asset. It is a
16-pixel-wide indexed-colour sheet of stacked 16x16 sprite cells; cell N is
custom-item-gfx entry N. Keep the order in lockstep with the
`kRandoCustomGfx_*` enum in src/rando/rando.h:

  entry 0: Triforce Piece
  entry 1: 1/2 Magic decanter
  entry 2: 1/4 Magic decanter
  entry 3: reserved (gfx 0x83 Rupoor is a recolored vanilla tile, no blob cell)
  entry 4: reserved (gfx 0x84 Cucco loads from vanilla sheet 80, no blob cell)
  entry 5: Soul (shared icon for all Soul_* items, add-enemy-souls)
  entry 6: Key Ring (shared icon for all dungeon Key Rings)
  entry 7: Skeleton Key

Pixel values are the asset data: each pixel's palette index is the SNES 4bpp
pixel value. The embedded RGB palette is authored for preview only and is
ignored by the compiler.

Provenance: the sprite pixel art is custom z3randomizer art, distributed under
the MIT license (Copyright 2016, 2017 Equilateral IT). It is retained with
attribution in NOTICE. This generator does not use ROM-derived palette words or
item-offset tables.

Usage: python assets/scripts/gen_custom_item_gfx_png.py
"""

import os
import sys

from PIL import Image


REPO_ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT_PATH = os.path.join(REPO_ROOT, "assets", "rando", "custom_item_gfx.png")

# Symbolic local art source. Symbols map to 4bpp palette indices; "." is
# transparent. The compiler consumes only the resulting indices.
PIXELS = {
    ".": 0,
    "w": 9,
    "g": 11,
    "l": 12,
    "x": 13,
    "y": 14,
    "z": 15,
}

ENTRIES = [
    ("TriforcePiece", [
        "................",
        ".......x........",
        "......xyx.......",
        "......xyx.......",
        ".....xyyyx......",
        ".....xyyyx......",
        "....xyyyyyx.....",
        "....xyyyyyx.....",
        "...xyyyyyyyx....",
        "...xyyyyyyyx....",
        "..xyyyyyyyyyx...",
        "..xyyyyyyyyyx...",
        ".xyyyyyyyyyyyx..",
        ".xyyyyyyyyyyyx..",
        "xyyyyyyyyyyyyyx.",
        "xxxxxxxxxxxxxxx.",
    ]),
    ("HalfMagic", [
        "................",
        ".xxxxxx.....x...",
        "xgglllgx...xwx..",
        "xggglggx..xwwx..",
        ".xxxxxx....xwx..",
        "..xggx.....xwx..",
        "..xglx.....xwx..",
        "..xglx....xxxxx.",
        ".xxxxxx..xwwwwwx",
        ".xzzyzx...xxxxx.",
        "xgxxxxgx..xwwwx.",
        "xggggggx...xxwx.",
        "xgllllgx..xwwwx.",
        "xggllggx..xwxx..",
        ".xggggx...xwwwx.",
        "..xxxx.....xxx..",
    ]),
    ("QuarterMagic", [
        "................",
        ".xxxxxx.....x...",
        "xgglllgx...xwx..",
        "xggglggx..xwwx..",
        ".xxxxxx....xwx..",
        "..xggx.....xwx..",
        "..xglx.....xwx..",
        "..xglx....xxxxx.",
        ".xxxxxx..xwwwwwx",
        ".xzzyzx...xxxxx.",
        "xgxxxxgx..xwxwx.",
        "xggggggx..xwxwx.",
        "xgllllgx..xwwwx.",
        "xggllggx...xxwx.",
        ".xggggx.....xwx.",
        "..xxxx.......x..",
    ]),
    # Blob cells 3/4 are RESERVED padding: gfx ids 0x83 (Rupoor) and 0x84
    # (Cucco) are special-cased in Rando_EnsureRecvItemSlotGfx (recolored
    # vanilla tiles, not blob cells), but the blob loader maps gfx -> cell as
    # (gfx & 0x7f), so later blob-backed entries must keep their cell index in
    # lockstep with their kRandoCustomGfx_* id.
    ("ReservedRupoor", ["................"] * 16),
    ("ReservedCucco", ["................"] * 16),
    # Entry 5: shared soul-item icon (add-enemy-souls) — gfx 0x85 for every
    # Soul_* registry item. Original art (spirit wisp), MIT like the sheet.
    ("Soul", [
        "................",
        "......xxx.......",
        ".....xwwlx......",
        "....xwwwwlx.....",
        "....xwwwwlx.....",
        "...xwwwwwwlx....",
        "...xwxwwxwlx....",
        "...xwxwwxwlx....",
        "..xwwwwwwwwlx...",
        "..xlwwwwwwllx...",
        "..xlwwwwwwlx....",
        "...xlwwwwlx.....",
        "....xlwlwlx.....",
        ".....xlxlx......",
        "......x.x.......",
        "................",
    ]),
    # Entry 6: one shared gold ring for every dungeon-specific Key Ring.
    ("KeyRing", [
        "................",
        "......xxxx......",
        "....xxyyyyxx....",
        "...xyyyyyyyyx...",
        "..xyyyxxxxyyyx..",
        "..xyyx....xyyx..",
        ".xyyx......xyyx.",
        ".xyyx......xyyx.",
        ".xyyx......xyyx.",
        "..xyyx....xyyx..",
        "..xyyyxxxxyyyx..",
        "...xyyyyyyyyx...",
        "....xxyyyyxx....",
        "......xxxx......",
        ".......xyx......",
        "........x.......",
    ]),
    # Entry 7: pale ancient key with a skull-like bow, visually distinct from
    # the gold ring and the vanilla small/big-key bundles.
    ("SkeletonKey", [
        "......xxxx......",
        "....xxwwwwxx....",
        "...xwwxwwxwwx...",
        "...xwwwwwwwwx...",
        "....xwwwwwwx....",
        ".....xwxxwx.....",
        "......xwwx......",
        "......xwwx......",
        "......xwwx......",
        "......xwwxxx....",
        "......xwwx......",
        "......xwwxxx....",
        "......xwwx......",
        "......xwwx......",
        ".......xx.......",
        "................",
    ]),
]

# Authored preview palette. Entries 9 and 11..15 are used by the current art;
# the others are distinct debug colours for accidental indices.
PREVIEW_PALETTE_RGB = [
    (255, 0, 255),    # 0: transparent preview
    (255, 255, 255),
    (214, 80, 80),
    (168, 92, 46),
    (246, 156, 92),
    (42, 42, 46),
    (184, 154, 255),
    (76, 112, 210),
    (0, 0, 0),
    (255, 255, 255),
    (210, 68, 56),
    (64, 130, 74),
    (122, 206, 106),
    (32, 32, 36),
    (252, 210, 55),
    (174, 121, 36),
]


def parse_cell(name, rows):
    if len(rows) != 16:
        raise ValueError("%s must have 16 rows" % name)
    out = []
    for y, row in enumerate(rows):
        if len(row) != 16:
            raise ValueError("%s row %d must be 16 pixels" % (name, y))
        try:
            out.append([PIXELS[ch] for ch in row])
        except KeyError as e:
            raise ValueError("%s row %d has unknown symbol %r" %
                             (name, y, e.args[0])) from None
    return out


def main(argv):
    if argv:
        raise SystemExit("usage: python assets/scripts/gen_custom_item_gfx_png.py")

    img = Image.new("P", (16, 16 * len(ENTRIES)), 0)
    pal = [channel for rgb in PREVIEW_PALETTE_RGB for channel in rgb]
    img.putpalette(pal + [0] * (768 - len(pal)))

    for n, (name, rows) in enumerate(ENTRIES):
        cell = parse_cell(name, rows)
        for y in range(16):
            for x in range(16):
                img.putpixel((x, n * 16 + y), cell[y][x])
        print("entry %d: %s" % (n, name))

    img.save(OUT_PATH)
    print("wrote", OUT_PATH)


if __name__ == "__main__":
    main(sys.argv[1:])
