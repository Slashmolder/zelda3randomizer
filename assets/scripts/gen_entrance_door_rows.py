#!/usr/bin/env python3
"""Validate each cave-interior's overworld DOOR ROWS against zelda3_assets.dat.

The cave-entrance pool's identity unit is the overworld door row, not the
entrance id: four Dark World shop doors load room 0x10F through entrance id
0x60 and two load room 0x112 through 0x58, so an entrance-id lookup cannot tell
them apart. `entrance_registry.yaml` therefore records `door_rows` per entry.

Those four interiors are now SPLIT one entry per door row, which means several
entries legitimately share an entrance id and the id can no longer be used to
derive the rows — the split itself is the one fact the asset tables cannot
express, so it lives in the registry. Everything else is still checked against
the vanilla tables rather than trusted:

  * every declared row's entrance id must be one of its entry's entrance_ids;
  * no row may be claimed by two entries;
  * the declared rows must be EXACTLY the set of overworld door rows whose
    entrance id belongs to some registry entry (none dropped, none invented);
  * an entry's declared region must be in the same world as its doors.

    python assets/scripts/gen_entrance_door_rows.py [--assets zelda3_assets.dat]
    python assets/scripts/gen_entrance_door_rows.py --check   # CI form

Without --check it re-emits the `door_rows:` fragment in registry order (rows
sorted, taken from the registry's own partition but verified against the tables)
plus a report of the entries that still carry several rows.

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
ASSET_ENTRANCE_AREA = 124


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


def check_world_consistency(interiors, rows_by_entry, area: bytes) -> int:
    """Fail if an entry's declared region is in the opposite world from its doors.

    Entrance_ApplyRegionOverrides uses an entry's `region` as the SOURCE region:
    whatever interior the shuffle puts behind that door has its locations rebound
    to it. A light-world region on a dark-world door therefore claims dark-world
    content is reachable from the light world.

    Entrance_SelfCheck cross-validates `region` against the entry's own
    locations, but ONLY for entries that HAVE locations — the empty ones are
    unguarded, which is where the one real instance of this hid
    (`general_store_1` declared LightWorld_NorthWest for the Dark World Forest
    Shop's door). This catches the cross-world case cheaply; a within-world
    error still needs the location cross-check or an upstream comparison.
    """
    bad = 0
    for idx, it in enumerate(interiors):
        region = it.get("region")
        if not region:
            continue
        declared_dark = str(region).startswith("DarkWorld")
        worlds = set()
        for r in rows_by_entry.get(idx, []):
            worlds.add((struct.unpack_from("<H", area, r * 2)[0] & 0x40) != 0)
        if not worlds or declared_dark in worlds:
            continue
        bad += 1
        where = "DARK" if worlds == {True} else ("LIGHT" if worlds == {False} else "MIXED")
        print(f"  entry {idx:2d} {it['interior_id']:<26} declares {region} "
              f"but every door is in the {where} world", file=sys.stderr)
    return bad


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--assets", default="zelda3_assets.dat")
    ap.add_argument("--check", action="store_true",
                    help="validate instead of emitting; non-zero exit on a violation")
    ap.add_argument("--allow-missing-assets", action="store_true",
                    help="skip (exit 0) when the asset blob is absent, for assetless profiles")
    args = ap.parse_args()
    if not os.path.exists(args.assets):
        if args.allow_missing_assets:
            print(f"gen_entrance_door_rows: SKIPPED — {args.assets} not present")
            return 0
        sys.exit(f"{args.assets} not found — run from a checkout with extracted assets")

    assets = load_assets(args.assets)
    ent_id = assets[ASSET_ENTRANCE_ID]
    area = assets[ASSET_ENTRANCE_AREA]
    rooms_b = assets[ASSET_ENTRANCE_ROOMS]
    rooms = struct.unpack_from("<%dH" % (len(rooms_b) // 2), rooms_b, 0)

    with open(REG, "r", encoding="utf-8") as f:
        interiors = yaml.safe_load(f)["interiors"]

    # The registry's declared partition of door rows. Several entries may share
    # an entrance id (the shop splits), so the rows come from the file — but
    # every one of them is cross-checked against the asset tables below.
    rows_by_entry: dict[int, list[int]] = {}
    row_owner: dict[int, int] = {}
    bad = 0
    for idx, it in enumerate(interiors):
        rows = sorted(it.get("door_rows") or [])
        rows_by_entry[idx] = rows
        if not rows:
            print(f"  entry {idx:2d} {it['interior_id']:<36} declares no door_rows",
                  file=sys.stderr)
            bad += 1
        eids = set(it.get("entrance_ids") or [])
        for r in rows:
            if r >= len(ent_id):
                print(f"  entry {idx:2d} {it['interior_id']:<36} door row 0x{r:02X} is "
                      f"past the end of kOverworld_Entrance_Id ({len(ent_id)} rows)",
                      file=sys.stderr)
                bad += 1
                continue
            if r in row_owner:
                print(f"  door row 0x{r:02X} claimed by entries {row_owner[r]} "
                      f"({interiors[row_owner[r]]['interior_id']}) and {idx} "
                      f"({it['interior_id']})", file=sys.stderr)
                bad += 1
                continue
            row_owner[r] = idx
            if ent_id[r] not in eids:
                print(f"  entry {idx:2d} {it['interior_id']:<36} claims door row "
                      f"0x{r:02X}, but that row's entrance id is 0x{ent_id[r]:02X} "
                      f"and the entry declares {sorted('0x%02X' % e for e in eids)}",
                      file=sys.stderr)
                bad += 1

    # Completeness: every overworld door row whose entrance id belongs to SOME
    # registry entry must be claimed by exactly one. This is what catches a row
    # silently dropped when an entry is split.
    all_eids = {e for it in interiors for e in (it.get("entrance_ids") or [])}
    for lx in range(len(ent_id)):
        if ent_id[lx] in all_eids and lx not in row_owner:
            print(f"  door row 0x{lx:02X} (entrance id 0x{ent_id[lx]:02X}) is a cave "
                  f"door but no entry claims it", file=sys.stderr)
            bad += 1

    if args.check:
        bad += check_world_consistency(interiors, rows_by_entry, area)
        if bad:
            print(f"gen_entrance_door_rows: {bad} problem(s) — see above.",
                  file=sys.stderr)
            return 1
        print(f"gen_entrance_door_rows: OK — {len(row_owner)} cave door rows partitioned "
              f"across {len(interiors)} interiors, every row's entrance id and world "
              f"consistent with the vanilla tables.")
        return 0
    if bad:
        print(f"gen_entrance_door_rows: {bad} problem(s) — see above.", file=sys.stderr)
        return 1

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
