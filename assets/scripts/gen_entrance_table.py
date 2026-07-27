#!/usr/bin/env python3
"""Emit the C cave-interior table for entrance shuffle from entrance_registry.yaml.

This is a ONE-SHOT authoring helper, NOT a build-time codegen step (the output
is committed as part of src/rando/shuffle_entrance.c and hand-reviewed). Re-run
it and paste the block if entrance_registry.yaml changes:

    python assets/scripts/gen_entrance_table.py

The table is the data home for Stage 1 cave shuffle: each interior carries its
room, vanilla logic region (by NAME, resolved at runtime via
Rando_FindRegionByName so numeric region-id drift can't silently corrupt it),
its overworld entrance-ids, its overworld DOOR ROWS, the location ids resident
inside it, and — for the eight cave-resident shops — the base location id of the
shop that lives behind its door.

The door rows are the pool's identity unit — several entries legitimately share
an entrance id (the four Dark World shop doors all load room 0x10F through id
0x60), so the runtime resolves a door's pool entry by row, not by id. The
invariants that keeps sound are asserted here rather than left to review; the
row values themselves are cross-checked against the vanilla asset tables by
assets/scripts/gen_entrance_door_rows.py --check (this script never reads the
asset blob, so "no row dropped" is checked there, where the tables live).
"""
import sys, os, yaml

HERE = os.path.dirname(os.path.abspath(__file__))
REG = os.path.join(HERE, "..", "rando", "entrance_registry.yaml")

# Fall-hole entrance ids. Rando_RecordEnteredFallhole has no door row available
# (a fall sets which_entrance directly), so it must key on the entrance id — which
# is only unambiguous while no id in this range is claimed by two entries.
FALLHOLE_LO, FALLHOLE_HI = 0x76, 0x81

# The 27 regular shop slots occupy location_registry ids 237..263 in nine
# consecutive 3-slot blocks. Eight of the nine shops are cave interiors and are
# resolved through the entrance layout; the ninth (Light World Death Mountain,
# base 255, room 0x0FF) is not a cave and keeps the static room-only table row,
# so it must never appear as a cave entry's shop_loc_base.
SHOP_LOC_FIRST, SHOP_LOC_COUNT = 237, 27
SHOP_NON_CAVE_BASE = 255
NO_SHOP = 0xFFFF  # kCaveInteriors sentinel for "this door hosts no shop"


def check(interiors):
    """Assert the data invariants the door-row keying depends on. Returns errors."""
    errs = []

    # (a) Door rows partition: every entry has at least one, none claimed twice.
    #     ("None dropped" needs the asset table — see gen_entrance_door_rows.py.)
    owner = {}
    for idx, it in enumerate(interiors):
        rows = it.get("door_rows") or []
        if not rows:
            errs.append(f"entry {idx} ({it['interior_id']}) has no door_rows")
        for r in rows:
            if r in owner:
                errs.append(f"door row 0x{r:02X} claimed by entries "
                            f"{owner[r]} ({interiors[owner[r]]['interior_id']}) and "
                            f"{idx} ({it['interior_id']})")
            else:
                owner[r] = idx

    # (b) Location lists disjoint. The per-location region-override store is
    #     last-write-wins and Entrance_CaveInteriorOfLocation is location-keyed,
    #     so a location in two entries would silently resolve to one of them.
    loc_owner = {}
    for idx, it in enumerate(interiors):
        for l in it.get("locations") or []:
            if l in loc_owner:
                errs.append(f"location {l} listed by entries "
                            f"{loc_owner[l]} ({interiors[loc_owner[l]]['interior_id']}) "
                            f"and {idx} ({it['interior_id']})")
            else:
                loc_owner[l] = idx

    # (c) Entries sharing an entrance id must declare the same room — the id
    #     determines the room through kEntranceData_rooms, so a disagreement
    #     means one of the two rows is mis-attributed. (The converse is NOT an
    #     invariant: two different ids may load one room, e.g. rows 0x57/0x59
    #     reach room 0x112 through ids 0x58/0x5A.)
    #
    # (d) An entrance id shared by two entries must not be a fall-hole id, since
    #     the fall-hole path can only key on the id.
    by_eid = {}
    for idx, it in enumerate(interiors):
        for e in it.get("entrance_ids") or []:
            by_eid.setdefault(e, []).append(idx)
    for e, idxs in sorted(by_eid.items()):
        rooms = {(interiors[i].get("rooms") or [None])[0] for i in idxs}
        if len(rooms) > 1:
            errs.append(f"entrance id 0x{e:02X} is shared by entries {idxs} but they "
                        f"declare different rooms {sorted(hex(r) for r in rooms)}")
        if len(idxs) > 1 and FALLHOLE_LO <= e <= FALLHOLE_HI:
            errs.append(f"entrance id 0x{e:02X} is shared by entries {idxs} AND lies in "
                        f"the fall-hole range 0x{FALLHOLE_LO:02X}-0x{FALLHOLE_HI:02X}; "
                        f"Rando_RecordEnteredFallhole keys on the id alone and could "
                        f"not tell them apart")

    # (e) shop_loc_base is well-formed AND its three slots are bound as this
    #     entry's locations. The binding is the load-bearing half: the runtime
    #     resolves a shop from the DESTINATION entry's base, while the placer
    #     learns which region reaches it from Entrance_ApplyRegionOverrides
    #     walking that entry's location_ids. A base without its locations would
    #     hand out a slot the logic still binds to its vanilla region.
    #     Disjointness across entries falls out of (b).
    shop_owner = {}
    for idx, it in enumerate(interiors):
        base = it.get("shop_loc_base")
        if base is None:
            continue
        name = it["interior_id"]
        if base == SHOP_NON_CAVE_BASE:
            errs.append(f"entry {idx} ({name}) claims shop base {base}, the NON-CAVE "
                        f"Light World Death Mountain shop (room 0x0FF); that shop is "
                        f"room-only and must stay in the static table")
        if not (SHOP_LOC_FIRST <= base < SHOP_LOC_FIRST + SHOP_LOC_COUNT) or \
                (base - SHOP_LOC_FIRST) % 3 != 0:
            errs.append(f"entry {idx} ({name}) shop_loc_base {base} is not a shop-block "
                        f"base (expected {SHOP_LOC_FIRST}+3k, k < {SHOP_LOC_COUNT // 3})")
            continue
        if base in shop_owner:
            errs.append(f"shop base {base} claimed by entries {shop_owner[base]} and {idx}")
        else:
            shop_owner[base] = idx
        locs = list(it.get("locations") or [])
        want = [base, base + 1, base + 2]
        if not all(w in locs for w in want):
            errs.append(f"entry {idx} ({name}) declares shop_loc_base {base} but its "
                        f"locations {locs} do not bind slots {want}; without the binding "
                        f"Entrance_ApplyRegionOverrides cannot rebind the shop's region")
    return errs


def main():
    with open(REG, "r", encoding="utf-8") as f:
        data = yaml.safe_load(f)
    interiors = data["interiors"]

    errs = check(interiors)
    if errs:
        for e in errs:
            print(f"gen_entrance_table: {e}", file=sys.stderr)
        return 1

    # entrance-id / door-row / location-id arrays per interior
    ent_arrays = []
    row_arrays = []
    loc_arrays = []
    rows_c = []
    for idx, it in enumerate(interiors):
        eids = it["entrance_ids"]
        drows = it["door_rows"]
        locs = it.get("locations") or []
        ename = f"kEnt_ids_{idx}"
        rname = f"kEnt_rows_{idx}"
        lname = f"kEnt_locs_{idx}" if locs else "0"
        ent_arrays.append(
            f"static const uint8 {ename}[{len(eids)}] = {{ "
            + ", ".join(f"0x{e:02X}" for e in eids) + " };")
        row_arrays.append(
            f"static const uint8 {rname}[{len(drows)}] = {{ "
            + ", ".join(f"0x{r:02X}" for r in sorted(drows)) + " };")
        if locs:
            loc_arrays.append(
                f"static const uint16 {lname}[{len(locs)}] = {{ "
                + ", ".join(str(l) for l in locs) + " };")
        rooms = it["rooms"]
        room = rooms[0]
        region = it.get("region") or "0"
        region_c = "NULL" if region in (None, "0") else f"\"{region}\""
        base = it.get("shop_loc_base")
        base_c = "0xFFFF" if base is None else str(base)
        rows_c.append(
            f'  {{ /*{idx:2d}*/ "{it["interior_id"]}", 0x{room:03X}, {region_c}, '
            f'{ename}, {len(eids)}, {rname}, {len(drows)}, {lname}, {len(locs)}, '
            f'{base_c} }},')
    print("\n".join(ent_arrays))
    print()
    print("\n".join(row_arrays))
    print()
    print("\n".join(loc_arrays))
    print()
    print(f"#define kEntranceCaveInteriorCount {len(interiors)}")
    print("static const RandoCaveInterior kCaveInteriors[kEntranceCaveInteriorCount] = {")
    print("\n".join(rows_c))
    print("};")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
