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


def is_pinnable(rows, ups) -> bool:
    """Can upstream DECIDE this entry's region?

    Only when every one of its door rows carries an upstream declaration AND
    they all name the same region. Both halves are load-bearing, and each was
    added because the looser rule produced a false positive on an entry whose
    region is independently proven correct by Entrance_SelfCheck (2):

      * SPLIT coverage is normal, not a bug. A pool entry groups every door that
        loads one interior room, and those doors can sit in different regions —
        `pond_of_happiness` spans six across both worlds. The pool is coarser
        than upstream's per-door model on purpose, so "declared is not one of
        them" proves nothing.
      * PARTIAL coverage cannot decide either, because upstream's declaration
        for a door is a TakeAny/shop REPURPOSING of that door, not a statement
        about the interior's primary entrance. `hype_cave`, `thief_hideout` and
        `pond_of_wishing` each have one door carrying an upstream take-any in a
        region their verified location bindings disagree with — the other,
        undeclared door is the canonical one.

    That leaves the single-door (or unanimous multi-door) entries, which is
    exactly the class the general_store_1 drift fixed at kGeneratorVersion 159
    and the chest_shell_game drift fixed at 164 both belonged to.

    THE one definition of this rule; gen_entrance_upstream_regions.py imports it
    rather than restating it, so the reporting tool and the CI gate can never
    disagree about what counts as decidable.
    """
    if not rows or not ups:
        return False
    if {int(u["door"]) - 1 for u in ups} != set(rows):
        return False
    return len({u["region"] for u in ups}) == 1


def check_upstream_regions(interiors) -> int:
    """Pin every entry's region against upstream where upstream can decide.

    Entrance_SelfCheck (2) cross-validates `region` against an entry's own
    locations, so the location-less entries had nothing checking them at all —
    which is where the general_store_1 drift fixed at kGeneratorVersion 159 hid,
    and where the chest_shell_game drift found by this check hid.

    Entries that DO have locations are covered too, deliberately. Their
    self-check compares the registry against our own generated logic table, so
    the two agree by construction if both drifted together; upstream is an
    INDEPENDENT third source. mimic_cave is the worked example — registry,
    upstream and the logic table all say LightWorld_DeathMountain_East while
    ow_graph says West, and the tie is broken by the fact that SCREEN_REGIONS
    documents 0x05 as a split screen whose east half is East.

    `upstream_regions` is derived from the ALTTPR PHP by
    gen_entrance_upstream_regions.py and committed, so this runs with no
    upstream checkout. Two rules:

      * every location-less entry MUST carry the field (an omitted one is a new
        entry that slipped in unpinned, not a pass);
      * `region` is ENFORCED only when upstream can actually decide — every door
        covered and all rows agreeing. Partial or split coverage is normal and
        proves nothing; see is_pinnable() in the generator for the three entries
        whose verified regions a looser rule would have flagged.
    """
    bad = 0
    for idx, it in enumerate(interiors):
        ups = it.get("upstream_regions")
        if ups is None:
            print(f"  entry {idx:2d} {it['interior_id']:<36} has no upstream_regions — "
                  f"run assets/scripts/gen_entrance_upstream_regions.py --emit",
                  file=sys.stderr)
            bad += 1
            continue
        if not is_pinnable(it.get("door_rows") or [], ups):
            continue                      # upstream cannot decide — see docstring
        want = ups[0]["region"]
        if it.get("region") != want:
            src = ", ".join(sorted(u["src"] for u in ups))
            print(f"  entry {idx:2d} {it['interior_id']:<36} declares "
                  f"{it.get('region')} but every one of its doors is upstream's "
                  f"{want} ({src})", file=sys.stderr)
            bad += 1
    return bad


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--assets", default="zelda3_assets.dat")
    ap.add_argument("--check", action="store_true",
                    help="validate instead of emitting; non-zero exit on a violation")
    ap.add_argument("--allow-missing-assets", action="store_true",
                    help="skip (exit 0) when the asset blob is absent, for assetless profiles")
    args = ap.parse_args()
    have_assets = os.path.exists(args.assets)
    if not have_assets and not args.allow_missing_assets:
        sys.exit(f"{args.assets} not found — run from a checkout with extracted assets")

    # Only SOME of what follows needs the asset blob. The registry's self
    # consistency (every entry declares door rows; no two entries claim the same
    # row) and the upstream region pin compare committed YAML against committed
    # YAML — they are valid with no assets at all.
    #
    # This used to return 0 the moment the blob was missing, which meant public
    # CI — which runs on a ROM-less checkout — never executed the upstream region
    # pin. That is exactly the drift class kGen 159/164 had to fix, so the guard
    # for it was inert on the only machine that mattered. Run the YAML-only half
    # unconditionally and skip just the table cross-checks.
    if have_assets:
        assets = load_assets(args.assets)
        ent_id = assets[ASSET_ENTRANCE_ID]
        area = assets[ASSET_ENTRANCE_AREA]
        rooms_b = assets[ASSET_ENTRANCE_ROOMS]
        rooms = struct.unpack_from("<%dH" % (len(rooms_b) // 2), rooms_b, 0)
    else:
        ent_id = area = rooms = None

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
            if ent_id is not None and r >= len(ent_id):
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
            if ent_id is not None and ent_id[r] not in eids:
                print(f"  entry {idx:2d} {it['interior_id']:<36} claims door row "
                      f"0x{r:02X}, but that row's entrance id is 0x{ent_id[r]:02X} "
                      f"and the entry declares {sorted('0x%02X' % e for e in eids)}",
                      file=sys.stderr)
                bad += 1

    # Completeness: every overworld door row whose entrance id belongs to SOME
    # registry entry must be claimed by exactly one. This is what catches a row
    # silently dropped when an entry is split.
    all_eids = {e for it in interiors for e in (it.get("entrance_ids") or [])}
    if ent_id is not None:
        for lx in range(len(ent_id)):
            if ent_id[lx] in all_eids and lx not in row_owner:
                print(f"  door row 0x{lx:02X} (entrance id 0x{ent_id[lx]:02X}) is a cave "
                      f"door but no entry claims it", file=sys.stderr)
                bad += 1

    if args.check:
        if area is not None:
            bad += check_world_consistency(interiors, rows_by_entry, area)
        bad += check_upstream_regions(interiors)   # committed YAML only — always runs
        if bad:
            print(f"gen_entrance_door_rows: {bad} problem(s) — see above.",
                  file=sys.stderr)
            return 1
        if not have_assets:
            recorded = sum(1 for it in interiors if it.get("upstream_regions"))
            enforced = sum(1 for it in interiors
                           if is_pinnable(it.get("door_rows") or [],
                                          it.get("upstream_regions") or []))
            print(f"gen_entrance_door_rows: OK (assetless) — {len(row_owner)} door rows "
                  f"partitioned across {len(interiors)} interiors with no collisions; "
                  f"upstream regions recorded for {recorded}/{len(interiors)} entries and "
                  f"ENFORCED on the {enforced} upstream can decide. Entrance-id, world "
                  f"and completeness cross-checks need {args.assets} and were SKIPPED.")
            return 0
        enforced = sum(1 for it in interiors
                       if is_pinnable(it.get("door_rows") or [],
                                      it.get("upstream_regions") or []))
        recorded = sum(1 for it in interiors if it.get("upstream_regions"))
        print(f"gen_entrance_door_rows: OK — {len(row_owner)} cave door rows partitioned "
              f"across {len(interiors)} interiors, every row's entrance id and world "
              f"consistent with the vanilla tables; upstream regions recorded for "
              f"{recorded}/{len(interiors)} entries and ENFORCED on the {enforced} "
              f"upstream can decide.")
        return 0
    if bad:
        print(f"gen_entrance_door_rows: {bad} problem(s) — see above.", file=sys.stderr)
        return 1

    if not have_assets:
        # Emitting the fragment reads the vanilla entrance tables; there is
        # nothing to emit without them. (--check has its own assetless result.)
        print(f"gen_entrance_door_rows: SKIPPED — {args.assets} not present, "
              f"cannot emit the door_rows fragment without the vanilla tables")
        return 0

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
