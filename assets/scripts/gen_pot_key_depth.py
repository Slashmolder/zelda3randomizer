#!/usr/bin/env python3
"""Generate assets/rando/pot_key_depth.gen.yaml — the per-location WILD-keys
worst-case small-key requirement for the pot-bearing dungeons
(add-rando-pot-sanity task #25).

Source of truth: run `./zelda3 --dump-key-depth key_depth.txt` first. That prover
tool walks DoorExplore_Core (the real reachability engine) over the vanilla door
graph and emits, per location, the WORST-CASE number of small-key doors that
must be open to guarantee reaching it (`LOC ... depth=`). Once pot_shuffle turns
a dungeon's pot keys into first-class items, a deep location can require more
keys than the vanilla `cur` value (which counted only the placed keys, drops
free). Under WILD keys those keys live anywhere in the world, so you must HOLD
the worst case before reaching — a static requirement that is correct (no
strand) and achievable (collect externally, enter loaded; capped at the
dungeon's real key count).

This script joins the dump with the current logic YAMLs and emits exactly the
locations whose worst-case `full` exceeds their vanilla `cur`. rando_logic_gen.py
wraps each one:

    <vanilla predicate> AND (NOT POT_KEYS_WILD OR HAS_AMOUNT(SmallKey_X, full))

so pots-off / dungeon-keys keep the vanilla branch (byte-identical) and only
wild + pots-on tightens. Regenerate whenever the door tables or pots change
(the depths are structural facts derived from committed data, not hand-authored).
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]  # assets/scripts/ -> repo root
DUMP = ROOT / "key_depth.txt"
OUT = ROOT / "assets" / "rando" / "pot_key_depth.gen.yaml"

# door-table dungeon index -> SmallKey item suffix
SK = {
    0: "HyruleCastleEscape", 1: "EasternPalace", 2: "DesertPalace",
    3: "TowerOfHera", 4: "HyruleCastleTower", 5: "PalaceOfDarkness",
    6: "SwampPalace", 7: "SkullWoods", 8: "ThievesTown", 9: "IcePalace",
    10: "MiseryMire", 11: "TurtleRock", 12: "GanonsTower",
}
# Dungeons that actually have key pots (HC EP DP SP TT IP MM GT). The others
# (ToH, Agahnim, PoD, Skull Woods, Turtle Rock) carry no pot keys, so pot_shuffle
# never raises their requirement — leave them untouched.
POT_DUNGEONS = {0, 1, 2, 6, 8, 9, 10, 12}


def main() -> int:
    if not DUMP.exists():
        sys.exit(f"missing {DUMP}; run `./zelda3 --dump-key-depth key_depth.txt` first")

    # full[name] = (worst_case_depth, dungeon_index); chest[d] from DUNGEON rows.
    full = {}
    chest = {}
    for ln in DUMP.read_text(encoding="utf-8").splitlines():
        m = re.match(r"DUNGEON (\d+) chest=(\d+) ", ln)
        if m:
            chest[int(m.group(1))] = int(m.group(2))
            continue
        m = re.match(r'LOC (\d+) loc=\d+ region=\d+ depth=(-?\d+) name="([^"]*)"', ln)
        if m:
            full[m.group(3)] = (int(m.group(2)), int(m.group(1)))

    # pot_keys[d] = number of key pots in dungeon d (pots.gen.yaml vanilla_item).
    # The WILD requirement is CAPPED at the pooled key count (chest + pot_keys):
    # that is the number of keys the player must HOLD externally, since the
    # nonpot drop keys (drop - pot_keys) stay free in-dungeon and auto-collect in
    # context. Capping keeps HAS_AMOUNT satisfiable by the pool alone (no nonpot
    # free-grant) and is safe — chest+pot_keys >= the true external requirement.
    suffix2d = {v: k for k, v in SK.items()}
    pot_keys = {d: 0 for d in SK}
    pots_txt = (ROOT / "assets" / "rando" / "pots.gen.yaml").read_text(encoding="utf-8")
    for suf in re.findall(r"vanilla_item:\s*SmallKey_(\w+)", pots_txt):
        if suf in suffix2d:
            pot_keys[suffix2d[suf]] += 1
    cap = {d: chest.get(d, 0) + pot_keys.get(d, 0) for d in SK}

    # cur[name] = current placed-key requirement from the STANDARD logic (the
    # vanilla wildKeys value the fork already ships). Inverted variants share the
    # internal door depth, so the same `full` applies to them by name.
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
                curloc = m.group(1)
                continue
            if curloc and "can_reach:" in ln:
                n = 0
                for _, v in am.findall(ln):
                    n = max(n, int(v))
                for _ in hi.findall(ln):
                    n = max(n, 1)
                cur[curloc] = n

    rows = []
    for name in sorted(full):
        depth, d = full[name]
        if d not in POT_DUNGEONS or depth < 0:
            continue
        depth = min(depth, cap[d])  # external (held) requirement under wild keys
        if depth <= cur.get(name, 0):
            continue  # pots do not raise this location's requirement
        rows.append((name, f"SmallKey_{SK[d]}", depth))

    if not rows:
        sys.exit("no affected locations found — is key_depth.txt stale?")

    # Pot rooms: the MAX worst-case depth per room (a pot's exact sub-region is
    # not known, so max never under-gates), capped at the pooled key count. A pot
    # behind key doors must require those keys under wild too, or a progression
    # item placed there strands at runtime — and --generate-seed cannot catch it
    # (placer + verifier share the loose predicate). rando_logic_gen.load_pots
    # appends `AND (NOT POT_KEYS_WILD OR HAS_AMOUNT(item, full))` to each pot
    # whose room is listed.
    room_depth = {}  # (dungeon, room) -> max worst-case depth
    for ln in DUMP.read_text(encoding="utf-8").splitlines():
        m = re.match(r"ROOM room=0x([0-9a-f]+) region=\d+ depth=(-?\d+) dungeon=(\d+)", ln)
        if m:
            d, room, dep = int(m.group(3)), int(m.group(1), 16), int(m.group(2))
            if d in POT_DUNGEONS and dep >= 0:
                room_depth[(d, room)] = max(room_depth.get((d, room), 0), dep)
    pot_rooms = []
    for (d, room), dep in sorted(room_depth.items()):
        capped = min(dep, cap[d])
        if capped > 0:
            pot_rooms.append((room, f"SmallKey_{SK[d]}", capped))

    out = [
        "# GENERATED by assets/scripts/gen_pot_key_depth.py — do not edit by hand.",
        "# Source: ./zelda3 --dump-key-depth (prover worst-case under vanilla doors).",
        "# Per-location WILD-keys small-key requirement for pot-bearing dungeons",
        "# (add-rando-pot-sanity task #25). rando_logic_gen.py wraps each location:",
        "#   <vanilla predicate> AND (NOT POT_KEYS_WILD OR HAS_AMOUNT(item, full))",
        "format_version: 1",
        "locations:",
    ]
    for name, item, full_n in rows:
        out.append(f'  - name: "{name}"')
        out.append(f"    item: {item}")
        out.append(f"    full: {full_n}")
    out.append("# Per-ROOM pot requirement (load_pots wraps each pot in the room).")
    out.append("pot_rooms:")
    for room, item, full_n in pot_rooms:
        out.append(f"  - room: 0x{room:02x}")
        out.append(f"    item: {item}")
        out.append(f"    full: {full_n}")
    OUT.write_text("\n".join(out) + "\n", encoding="utf-8")
    print(f"wrote {OUT} ({len(rows)} locations, {len(pot_rooms)} pot rooms)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
