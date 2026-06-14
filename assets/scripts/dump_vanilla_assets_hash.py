#!/usr/bin/env python3
"""Dump kVanillaAssetsHash from the current zelda3_assets.dat.

Reads ``zelda3_assets.dat`` (from the repo root, or a path passed via
``--asset-file``), computes its SHA-256, and writes the hash + sets
``kVanillaAssetsHashKnown = 1`` in ``src/rando/vanilla_assets_hash.h``.

Usage:
  python assets/scripts/dump_vanilla_assets_hash.py
  python assets/scripts/dump_vanilla_assets_hash.py --asset-file=path/to/assets.dat

Per the spec scenario ``randomizer-placement / CLI --assets-must-be-vanilla
refuses non-vanilla blobs``, this constant is the source-of-truth for what
"vanilla" means at build time. Regenerate after any change to
``assets/restool.py`` that affects the output blob layout — and bump
``kGeneratorVersion`` if that change affects placement.
"""
from __future__ import annotations

import argparse
import hashlib
import os
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent.parent
DEFAULT_ASSETS = REPO / "zelda3_assets.dat"
HEADER = REPO / "src" / "rando" / "vanilla_assets_hash.h"


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--asset-file", type=Path, default=DEFAULT_ASSETS,
                        help="path to zelda3_assets.dat")
    parser.add_argument("--header", type=Path, default=HEADER,
                        help="output header path")
    parser.add_argument("--placeholder", action="store_true",
                        help="emit an all-zeros, kVanillaAssetsHashKnown=0 header "
                             "without reading any asset file. Lets a ROM-less build "
                             "(e.g. CI) compile; the vanilla-asset gate stays inert "
                             "until a real hash is baked in.")
    args = parser.parse_args(argv)

    if args.placeholder:
        digest = bytes(32)
        known = 0
        print("placeholder:  all-zeros (kVanillaAssetsHashKnown=0)")
    else:
        if not args.asset_file.exists():
            print(f"ERROR: asset file not found: {args.asset_file}", file=sys.stderr)
            print("  Extract assets first per README.md "
                  "(python assets/restool.py --extract-from-rom),", file=sys.stderr)
            print("  or pass --placeholder for a ROM-less (inert) header.", file=sys.stderr)
            return 1
        data = args.asset_file.read_bytes()
        digest = hashlib.sha256(data).digest()
        known = 1
        print(f"asset file:   {args.asset_file}  ({len(data)} bytes)")
        print(f"SHA-256:      {digest.hex()}")

    rows = []
    for i in range(0, 32, 8):
        chunk = digest[i:i + 8]
        rows.append("  " + ", ".join(f"0x{b:02x}" for b in chunk) + ",")
    body = "\n".join(rows)

    content = f"""// vanilla_assets_hash.h — pinned SHA-256 of the vanilla zelda3_assets.dat
// produced by `assets/restool.py --extract-from-rom` against the original
// US ROM (SHA-256 66871d66be19ad2c34c927d6b14cd8eb6fc3181965b6e517cb361f7316009cfb).
//
// Regenerate via:
//   python assets/scripts/dump_vanilla_assets_hash.py            (real hash)
//   python assets/scripts/dump_vanilla_assets_hash.py --placeholder  (inert)
// after any asset-pipeline change that affects the output blob layout.

#ifndef ZELDA3_RANDO_VANILLA_ASSETS_HASH_H_
#define ZELDA3_RANDO_VANILLA_ASSETS_HASH_H_

#include "../types.h"

#define kVanillaAssetsHashKnown {known}

static const uint8 kVanillaAssetsHash[32] = {{
{body}
}};

#endif  // ZELDA3_RANDO_VANILLA_ASSETS_HASH_H_
"""
    # Atomic write (same rationale as assets/rando_logic_gen.py's atomic_write_text):
    # vanilla_assets_hash.h is an order-only codegen prereq the Makefile glob-builds
    # against, so a parallel `make -j` must never read it mid-write. Write a sibling
    # temp, fsync, then os.replace() (atomic rename) — a reader sees a complete file.
    # newline default → bytes byte-for-byte identical to the prior write_text.
    _tmp = args.header.with_name(args.header.name + ".tmp")
    with open(_tmp, "w", encoding="utf-8") as _f:
        _f.write(content)
        _f.flush()
        os.fsync(_f.fileno())
    os.replace(_tmp, args.header)
    print(f"wrote:        {args.header}")
    if known:
        print(f"  kVanillaAssetsHashKnown is now 1 — --assets-must-be-vanilla active.")
    else:
        print(f"  kVanillaAssetsHashKnown is 0 — vanilla-asset gate inert (placeholder).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
