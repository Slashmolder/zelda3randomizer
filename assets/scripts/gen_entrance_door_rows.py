#!/usr/bin/env python3
"""Derive each cave-interior's overworld DOOR ROWS from zelda3_assets.dat.

The cave-entrance pool's identity unit is the overworld door row, not the
entrance id: four Dark World shop doors load room 0x10F through entrance id
0x60 and two load room 0x112 through 0x58, so an entrance-id lookup cannot tell
them apart. `entrance_registry.yaml` therefore records `door_rows` per entry,
and this script derives them from the vanilla tables rather than having anyone
hand-transcribe 70 indices.

Emits a YAML fragment (one `door_rows:` line per interior, in registry order)
plus a report of which entries carry several rows — the split candidates.

    python assets/scripts/gen_entrance_door_rows.py [--assets zelda3_assets.dat]

The blob layout mirrors LoadAssets() in src/main.c: 48-byte signature, u32 asset
count at +80, u32 extra at +84, a count-long u32 size table at +88, then
4-byte-aligned payloads. Asset 126 is kOverworld_Entrance_Id (u8 per door row),
asset 11 is kEntranceData_rooms (u16 per entrance id).
"""
from __future__ import annotations

import argparse
import os
import struct
import sys

import yaml

HERE = os.path.dirname(os.path.abspath(__file__))
REG = os.path.join(HERE, "..", "rando", "entrance_registry.yaml")
N_ASSETS = 166
ASSET_ENTRANCE_ID = 126
ASSET_ENTRANCE_ROOMS = 11


def load_assets(path: str) -> list[bytes]:
    data = open(path, "rb").read()
    count = struct.unpack_from("<I", data, 80)[0]
    if count != N_ASSETS:
        sys.exit(f"{path}: asset count {count}, expected {N_ASSETS}")
    offset = 88 + N_ASSETS * 4 + struct.unpack_from("<I", data, 84)[0]
    out = []
    for i in range(N_ASSETS):
        size = struct.unpack_from("<I", data, 88 + i * 4)[0]
        offset = (offset + 3) & ~3
        out.append(data[offset:offset + size])
        offset += size
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--assets", default="zelda3_assets.dat")
    args = ap.parse_args()
    if not os.path.exists(args.assets):
        sys.exit(f"{args.assets} not found — run from a checkout with extracted assets")

    assets = load_assets(args.assets)
    ent_id = assets[ASSET_ENTRANCE_ID]
    rooms_b = assets[ASSET_ENTRANCE_ROOMS]
    rooms = struct.unpack_from("<%dH" % (len(rooms_b) // 2), rooms_b, 0)

    with open(REG, "r", encoding="utf-8") as f:
        interiors = yaml.safe_load(f)["interiors"]

    # entrance id -> registry entry index. Several ids may map to one entry; an
    # id belonging to two entries is a registry bug, so flag it.
    eid_to_entry: dict[int, int] = {}
    for idx, it in enumerate(interiors):
        for e in it.get("entrance_ids") or []:
            if e in eid_to_entry:
                sys.exit(f"entrance id 0x{e:02X} claimed by entries "
                         f"{eid_to_entry[e]} and {idx}")
            eid_to_entry[e] = idx

    rows_by_entry: dict[int, list[int]] = {}
    for lx in range(len(ent_id)):
        idx = eid_to_entry.get(ent_id[lx])
        if idx is not None:
            rows_by_entry.setdefault(idx, []).append(lx)

    print("# --- door_rows fragment (registry order) ---")
    total = 0
    for idx, it in enumerate(interiors):
        rows = rows_by_entry.get(idx, [])
        total += len(rows)
        print(f"  # {idx:2d} {it['interior_id']}")
        print("    door_rows: [%s]" % ", ".join(f"0x{r:02X}" for r in rows))

    print()
    print(f"# {total} cave door rows across {len(interiors)} entries")
    print("# --- entries carrying several door rows (split candidates) ---")
    for idx, it in enumerate(interiors):
        rows = rows_by_entry.get(idx, [])
        if len(rows) < 2:
            continue
        room = (it.get("rooms") or [0])[0]
        print(f"#  entry {idx:2d} {it['interior_id']:<24} room 0x{room:03X}")
        for r in rows:
            eid = ent_id[r]
            print(f"#      row 0x{r:02X}  ALTTPR door 0x{r + 1:02X}  "
                  f"entrance id 0x{eid:02X}  room 0x{rooms[eid]:03X}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
