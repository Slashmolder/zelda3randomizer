#!/usr/bin/env python3
"""Generate assets/rando/pot_key_depth.gen.yaml - the per-location / per-room /
per-key-pot small-key requirements that pot_shuffle adds when a dungeon's pot
keys become first-class checks (add-rando-pot-sanity task #25).

Source of truth: run `./zelda3 --dump-key-depth key_depth.txt` first. That prover
walks DoorExplore_Core over the VANILLA door graph and emits, per region /
location / room / key-drop:
  * `depth=`    WORST-CASE small-key doors to GUARANTEE reaching it, and
  * `mindepth=` SHORTEST-PATH small-key doors to reach it.

Two key modes need two different values once the pot keys are items:

  WILD keys - the keys live anywhere in the WORLD, so you must HOLD the worst
    case before reaching (collect externally, enter loaded). Use `depth` (worst),
    CAPPED at the pooled key count (chest + pot_keys); the nonpot enemy/guard
    drops stay free in-dungeon so they never raise the external hold-N. Emitted
    as `full`.

  DUNGEON keys - the keys are collected IN-CONTEXT en route, so the requirement
    is the graduated `mindepth` (shortest path = exactly the keys needed for a
    known layout). Uncapped: a deep location can need more than chest+pot_keys,
    and the nonpot drops are FREE-GRANTED into the assumed inventory by
    rando_placement.c (replicating pots-off "drops free") so HAS_AMOUNT stays
    satisfiable. Emitted as `dungeon`.

rando_logic_gen.py wraps each affected location / pot:
  <vanilla cur> AND (NOT POT_KEYS_WILD     OR HAS_AMOUNT(X, full))
               AND (NOT POT_KEYS_DUNGEON OR HAS_AMOUNT(X, dungeon))
so pots-off (both ops false) stays byte-identical and each key mode tightens its
own term.

Pots resolve only to a ROOM, but a room can span door-table regions of differing
depth (8 of the key-pot rooms do). The DUNGEON value therefore splits:
  * KEY pots get their EXACT region mindepth (`pot_keys`) - over-gating a key pot
    is circular (you need the key it holds to reach it) -> spurious refuse; under-
    gating strands. Joined to the prover DROP line by the key's door region.
  * LOOT / EMPTY pots get the room-MAX mindepth (`pot_rooms.dungeon`) - they hold
    no key so over-gating is harmless (it only delays the check), and room-max is
    >= every pot's true depth so it never strands.
"""
import argparse
import difflib
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]  # assets/scripts/ -> repo root
DUMP = ROOT / "key_depth.txt"
OUT = ROOT / "assets" / "rando" / "pot_key_depth.gen.yaml"
POTS = ROOT / "assets" / "rando" / "pots.gen.yaml"

# door-table dungeon index -> SmallKey item suffix
SK = {
    0: "HyruleCastleEscape", 1: "EasternPalace", 2: "DesertPalace",
    3: "TowerOfHera", 4: "HyruleCastleTower", 5: "PalaceOfDarkness",
    6: "SwampPalace", 7: "SkullWoods", 8: "ThievesTown", 9: "IcePalace",
    10: "MiseryMire", 11: "TurtleRock", 12: "GanonsTower",
}
SUF2D = {v: k for k, v in SK.items()}

# Cross-check table - the EXACT per-key-pot dungeon mindepth, verified by hand
# from the prover DROP regions + the door-rando key_drop_data 'Pot' rooms
# (PotShuffle.py). Keyed (door-table dungeon, engine room). The auto-join below
# MUST reproduce these; a mismatch fails the build (catches a dump change or a
# pots.gen.yaml rebind drifting the join).
KEYPOT_MINDEPTH = {
    (2, 0x63): 0, (2, 0x53): 1, (2, 0x43): 2,            # Desert: Tiles1/Beamos/Tiles2
    (1, 0xba): 0,                                          # EP Dark Square
    (6, 0x35): 3, (6, 0x36): 3, (6, 0x37): 2,             # Swamp Trench2/Hookshot/Trench1
    (6, 0x38): 1,                                          # Swamp PotRow
    (7, 0x56): 0,                                          # Skull 2 West Lobby
    (8, 0xab): 2, (8, 0xbc): 1,                            # Thieves SpikeSwitch/Hallway
    (9, 0x9f): 2,                                          # Ice Many Pots
    (10, 0xa1): 0, (10, 0xb3): 0,                          # Mire Fishbone/Spikes
    (12, 0x7b): 1, (12, 0x8b): 0, (12, 0x9b): 0,          # GT StarPits/Cross/DoubleSwitch
}
# Key pots whose door region is NOT in their engine room's ROOM-line set, so the
# DROP-region-in-room auto-join can't reach them. Verified values; the assert vs
# KEYPOT_MINDEPTH guards.
KEYPOT_OVERRIDE = {(12, 0x9b): 0}
KEYPOT_ORPHAN_FULL = {}


def write_lf(path: Path, text: str):
    with path.open("w", encoding="utf-8", newline="\n") as f:
        f.write(text)


def check_fresh(path: Path, expected: str) -> int:
    want = expected.encode("utf-8")
    have = path.read_bytes() if path.exists() else b""
    if have == want:
        print(f"gen_pot_key_depth: OK {path} is fresh")
        return 0

    print(f"gen_pot_key_depth: ERROR: {path} is stale; run "
          f"`python assets/scripts/gen_pot_key_depth.py` to refresh the local artifact.",
          file=sys.stderr)
    if have.replace(b"\r\n", b"\n") == want:
        print("gen_pot_key_depth: drift is line-ending-only (expected LF).",
              file=sys.stderr)
    else:
        have_text = have.decode("utf-8", "replace")
        for line in difflib.unified_diff(
                have_text.splitlines(),
                expected.splitlines(),
                fromfile=str(path),
                tofile=f"{path} (regenerated)",
                lineterm=""):
            print(line, file=sys.stderr)
    return 1


def parse_dump():
    """Returns (chest, drop, loc, room_regions, drops_ordered).
    loc[name]   = (worst, mindepth, dungeon)
    room_regions[(dungeon, room)] = [(region, worst, mindepth)]
    drops_ordered[dungeon] = [(region, worst, mindepth, name)]  (kDoorTblDropKeys order)
    """
    chest, drop = {}, {}
    loc = {}
    room_regions = {}
    drops_ordered = {}
    for ln in DUMP.read_text(encoding="utf-8").splitlines():
        m = re.match(r"DUNGEON (\d+) chest=(\d+) drop=(\d+) ", ln)
        if m:
            chest[int(m[1])] = int(m[2]); drop[int(m[1])] = int(m[3]); continue
        m = re.match(r'LOC (\d+) loc=\d+ region=\d+ depth=(-?\d+) mindepth=(-?\d+) name="([^"]*)"', ln)
        if m:
            loc[m[4]] = (int(m[2]), int(m[3]), int(m[1])); continue
        m = re.match(r"ROOM room=0x([0-9a-f]+) region=(\d+) depth=(-?\d+) mindepth=(-?\d+) dungeon=(\d+)", ln)
        if m:
            room_regions.setdefault((int(m[5]), int(m[1], 16)), []).append(
                (int(m[2]), int(m[3]), int(m[4]))); continue
        m = re.match(r'DROP (\d+) region=(\d+) depth=(-?\d+) mindepth=(-?\d+) name="([^"]*)"', ln)
        if m:
            drops_ordered.setdefault(int(m[1]), []).append(
                (int(m[2]), int(m[3]), int(m[4]), m[5])); continue
    return chest, drop, loc, room_regions, drops_ordered


def parse_pots():
    """fork pots: list of dicts {id, room, vanilla_item, is_key, dungeon}."""
    pots = []
    cur = None
    for ln in POTS.read_text(encoding="utf-8").splitlines():
        m = re.match(r"- id: (\d+)\s*$", ln)
        if m:
            if cur:
                pots.append(cur)
            cur = {"id": int(m[1])}
        elif cur is not None:
            mm = re.match(r"  room: (\d+)", ln)
            if mm:
                cur["room"] = int(mm[1])
            mm = re.match(r"  vanilla_item: (\S+)", ln)
            if mm:
                cur["vi"] = mm[1]
    if cur:
        pots.append(cur)
    out = []
    for p in pots:
        vi = p.get("vi", "")
        m = re.match(r"SmallKey_(\w+)", vi)
        if m and m[1] in SUF2D:
            out.append({"id": p["id"], "room": p["room"],
                        "item": vi, "dungeon": SUF2D[m[1]]})
    return out


def keypot_dungeon_depth(kp, room_regions, drops_ordered):
    """Exact dungeon mindepth for one key pot via its door region.

    Join: the prover DROP line whose region lives in this pot's engine room is
    the key's region; take its mindepth. If the region is not in the room set,
    use the reviewed override."""
    d, room = kp["dungeon"], kp["room"]
    if (d, room) in KEYPOT_OVERRIDE:
        return KEYPOT_OVERRIDE[(d, room)]
    region_set = {r for (r, _w, _m) in room_regions.get((d, room), [])}
    hits = [(reg, md) for (reg, _w, md, _n) in drops_ordered.get(d, [])
            if reg in region_set and md >= 0]
    if len(hits) == 1:
        return hits[0][1]
    if len(hits) > 1:
        sys.exit(f"key pot d={d} room=0x{room:02x}: {len(hits)} DROP regions in room "
                 f"{[h for h in hits]} - ambiguous join")
    # No DROP region in the room (single-depth room with no separate drop entry):
    # the room is unambiguous, so any region's mindepth is the answer.
    mds = {md for (_r, _w, md) in room_regions.get((d, room), []) if md >= 0}
    if len(mds) == 1:
        return next(iter(mds))
    sys.exit(f"key pot d={d} room=0x{room:02x}: no DROP join and ambiguous room "
             f"mindepths {sorted(mds)} - needs a KEYPOT_OVERRIDE")


def parse_cur():
    cur = {}
    am = re.compile(r"HAS_AMOUNT\(SmallKey_(\w+),\s*(\d+)\)")
    hi = re.compile(r"HAS_ITEM\(SmallKey_(\w+)\)")
    curloc = None
    files = [ROOT / "assets" / "rando" / "logic.yaml"]
    files += sorted((ROOT / "assets" / "rando" / "logic_parts").glob("*.yaml"))
    for f in files:
        for ln in f.read_text(encoding="utf-8").splitlines():
            m = re.match(r'\s*-?\s*id:\s*"([^"]*)"', ln)
            if m:
                curloc = m[1]; continue
            if curloc and "can_reach:" in ln:
                n = 0
                for _, v in am.findall(ln):
                    n = max(n, int(v))
                for _ in hi.findall(ln):
                    n = max(n, 1)
                cur[curloc] = n
    return cur


def main(argv=None) -> int:
    global DUMP, OUT, POTS
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--dump", default=str(DUMP),
                    help="key-depth dump from `zelda3 --dump-key-depth`")
    ap.add_argument("--out", default=str(OUT),
                    help="generated pot key-depth YAML path")
    ap.add_argument("--pots", default=str(POTS),
                    help="pot table YAML path")
    ap.add_argument("--check", action="store_true",
                    help="verify pot_key_depth.gen.yaml is byte-identical to regenerated output")
    args = ap.parse_args(argv)
    DUMP = Path(args.dump)
    OUT = Path(args.out)
    POTS = Path(args.pots)

    if not DUMP.exists():
        sys.exit(f"missing {DUMP}; run `./zelda3 --dump-key-depth key_depth.txt` first")
    chest, drop, loc, room_regions, drops_ordered = parse_dump()
    keypots = parse_pots()
    cur = parse_cur()

    # pot_keys[d] = number of fork key pots in dungeon d (post-rebind).
    pot_keys = {d: 0 for d in SK}
    for kp in keypots:
        pot_keys[kp["dungeon"]] += 1
    # Dungeons whose deep locations gain a key requirement once pots are items.
    POT_KEY_DUNGEONS = {d for d in SK if pot_keys[d] > 0}
    cap = {d: chest.get(d, 0) + pot_keys.get(d, 0) for d in SK}  # wild hold-N cap

    # Per-key-pot EXACT dungeon mindepth (+ cross-check vs the verified table).
    pk_rows = []
    for kp in sorted(keypots, key=lambda x: x["id"]):
        dep = keypot_dungeon_depth(kp, room_regions, drops_ordered)
        want = KEYPOT_MINDEPTH.get((kp["dungeon"], kp["room"]))
        if want is None:
            sys.exit(f"key pot id={kp['id']} d={kp['dungeon']} room=0x{kp['room']:02x} "
                     f"missing from KEYPOT_MINDEPTH cross-check table")
        if dep != want:
            sys.exit(f"key pot id={kp['id']} d={kp['dungeon']} room=0x{kp['room']:02x}: "
                     f"auto-join {dep} != verified {want}")
        orphan_full = KEYPOT_ORPHAN_FULL.get((kp["dungeon"], kp["room"]))
        pk_rows.append((kp["id"], kp["item"], dep, orphan_full))
    if len(pk_rows) != len(KEYPOT_MINDEPTH):
        sys.exit(f"found {len(pk_rows)} key pots but cross-check table has "
                 f"{len(KEYPOT_MINDEPTH)} - stale table after a rebind?")

    # Locations: full (wild, capped) and/or dungeon (min) where each tightens cur.
    loc_rows = []
    for name in sorted(loc):
        worst, mindepth, d = loc[name]
        if d not in POT_KEY_DUNGEONS or worst < 0:
            continue
        c = cur.get(name, 0)
        full = min(worst, cap[d])
        item = f"SmallKey_{SK[d]}"
        fields = {}
        if full > c:
            fields["full"] = full
        if mindepth > c:
            fields["dungeon"] = mindepth
        if fields:
            loc_rows.append((name, item, fields))

    # pot_rooms: full = wild room worst (capped); dungeon = room-MAX mindepth.
    # Wild covers EVERY dungeon with active pots (incl. no-key dungeons whose loot
    # pots sit behind key doors); the dungeon term only the pot-key dungeons.
    room_rows = []
    # Dungeons whose pot rooms need key-depth wraps. Keep every pot-key dungeon
    # included by construction; excluding one lets loot/empty pots in that dungeon
    # miss their WILD/DUNGEON key-depth terms after a room rebind.
    POT_DUNGEONS = (set(SK) - {3, 4, 5, 11}) | POT_KEY_DUNGEONS
    for (d, room) in sorted(room_regions):
        if d not in POT_DUNGEONS:
            continue
        worst = max((w for (_r, w, _m) in room_regions[(d, room)] if w >= 0), default=-1)
        roommax_min = max((m for (_r, _w, m) in room_regions[(d, room)] if m >= 0), default=-1)
        item = f"SmallKey_{SK[d]}"
        fields = {}
        if worst >= 0:
            full = min(worst, cap[d])
            if full > 0:
                fields["full"] = full
        if d in POT_KEY_DUNGEONS and roommax_min > 0:
            fields["dungeon"] = roommax_min
        if fields:
            room_rows.append((room, item, fields))

    # Free-grant of NONPOT drops per pot-key dungeon (= drop_cnt - pot_keys),
    # consumed by rando_placement.c via generated pot_nonpot_drop_counts.h.
    free = {d: drop.get(d, 0) - pot_keys[d] for d in POT_KEY_DUNGEONS}

    out = [
        "# GENERATED by assets/scripts/gen_pot_key_depth.py - do not edit by hand.",
        "# Source: ./zelda3 --dump-key-depth (prover worst-case + min-depth, vanilla doors).",
        "# add-rando-pot-sanity task #25 - small-key requirements pot_shuffle adds.",
        "#   full    = WILD hold-N worst case (capped at chest+pot_keys).",
        "#   dungeon = in-context MIN-depth (per-key-pot exact in pot_keys; room-max for loot).",
        "format_version: 2",
        "locations:",
    ]
    for name, item, fields in loc_rows:
        out.append(f'  - name: "{name}"')
        out.append(f"    item: {item}")
        if "full" in fields:
            out.append(f"    full: {fields['full']}")
        if "dungeon" in fields:
            out.append(f"    dungeon: {fields['dungeon']}")
    out.append("# Per-ROOM pot requirement (load_pots wraps every pot in the room).")
    out.append("# full -> all pots (wild); dungeon -> loot/empty pots (key pots use pot_keys).")
    out.append("pot_rooms:")
    for room, item, fields in room_rows:
        out.append(f"  - room: 0x{room:02x}")
        out.append(f"    item: {item}")
        if "full" in fields:
            out.append(f"    full: {fields['full']}")
        if "dungeon" in fields:
            out.append(f"    dungeon: {fields['dungeon']}")
    out.append("# Per-KEY-POT exact dungeon mindepth (overrides pot_rooms.dungeon for the key pot).")
    out.append("# `full` appears only for ORPHAN pots whose room has no pot_rooms wild entry.")
    out.append("pot_keys:")
    for pid, item, dep, orphan_full in pk_rows:
        out.append(f"  - id: {pid}")
        out.append(f"    item: {item}")
        if orphan_full is not None:
            out.append(f"    full: {orphan_full}")
        out.append(f"    dungeon: {dep}")
    out.append("# Non-pot small-key drops that stay free in DUNGEON key mode.")
    out.append("# Count = door-rando drop total - fork key-pot count.")
    out.append("nonpot_drops:")
    for d in sorted(free):
        if free[d] <= 0:
            continue
        out.append(f"  - item: SmallKey_{SK[d]}")
        out.append(f"    count: {free[d]}")
    out_text = "\n".join(out) + "\n"
    if args.check:
        return check_fresh(OUT, out_text)

    write_lf(OUT, out_text)
    print(f"wrote {OUT}: {len(loc_rows)} locations, {len(room_rows)} pot rooms, "
          f"{len(pk_rows)} key pots")
    print("pot_keys per dungeon:", {SK[d]: pot_keys[d] for d in POT_KEY_DUNGEONS})
    print("NONPOT free-grant (drop-pot_keys) for rando_placement.c:",
          {SK[d]: free[d] for d in sorted(free) if free[d] > 0})
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
