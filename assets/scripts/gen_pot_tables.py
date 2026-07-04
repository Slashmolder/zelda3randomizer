#!/usr/bin/env python3
"""gen_pot_tables.py — enumerate every liftable dungeon pot from the engine
ground-truth dump and emit the local pot-location source the rando codegen
consumes (assets/rando/pots.gen.yaml).

This is the pot analog of chest_data.py, but the geometry is extracted from the
ENGINE (zelda3 --dump-pot-table) rather than re-deriving the multi-pass dungeon
object stream in Python — see openspec add-rando-pot-sanity design D2. Pipeline:

  zelda3 --dump-pot-table  ->  assets/rando/pot_dump.gen.txt   (gitignored)
            (P room pos4 content  |  D room neighbor...)
  gen_pot_tables.py        ->  assets/rando/pots.gen.yaml      (gitignored)
  rando_logic_gen.py reads pots.gen.yaml -> location_ids.h / logic_data.c /
            pot_lookup.h  (room,pos -> loc)  at build time (no dump needed).

Per-pot work:
  * classify the kDungeonSecrets content byte (loot / key / creature / structural
    / empty) -> tier membership + vanilla item (D2 per-content-byte policy);
  * bind a logic REGION (D8): chest rooms are authoritative; the rest flood-fill
    through RECIPROCAL doors from chest/boss anchors (vanilla doors stay within
    one dungeon); residual/ambiguous rooms REQUIRE a reviewed entry in
    pot_logic_overrides.yaml or the build fails;
  * assign a deterministic location id = rank in the (room, pos4) sort, from
    POT_BASE_ID. NOT append-only: inserting/removing a pot renumbers every
    higher-sorted pot, so any pot-set change is a kGeneratorVersion bump.

Invariants asserted (fail the build, not ship wrong data): unique (room,pos4)
across all pots; nested tiers keys<=contents<=all; every in-scope pot has a
region; exhaustive content classification; no excluded room emits a pot;
every chest room's flooded region equals its chest region (cross-validation).
"""
from __future__ import annotations
import argparse, collections, difflib, os, re, subprocess, sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "assets"))
import yaml  # noqa: E402
import chest_data  # noqa: E402

POT_BASE_ID = 328  # first pot id after the 0..327 registry; id = (room,pos4) sort rank (not append-only)

# Engine room 0x016 is an alias: the chest registry's room-0x16 rows are
# Pyramid Fairy, while the liftable pot/key in the pot dump belongs to Swamp
# Palace Waterway. Do not let chest anchors bind this pot room to an overworld
# cave; pot_logic_overrides.yaml carries the reviewed Swamp binding.
CHEST_ROOM_ALIAS_EXCLUSIONS = {0x16}

# --- content byte -> classification (D2 per-content-byte policy) ----------------
# loot content -> (kind, vanilla_item). 'rupee/bomb/arrow/heart/magic' are the
# Contents/All-tier checks; SmallKey is the Keys tier (item is dungeon-specific,
# filled in from the room's region). Anything not here is creature/structural/
# empty and handled below; an UNKNOWN content byte that appears in-scope FAILS.
LOOT_ITEM = {
    1:  "Rupee1",      # green rupee
    7:  "Rupee5",      # blue rupee
    9:  "Arrow10",     # arrows
    10: "Bombs3",      # bombs
    11: "HeartRefill", # heart
    12: "SmallMagic",  # magic
    13: "SmallMagic",  # big magic (no larger refill item; SmallMagic is the junk analog)
}
KEY_CONTENT = 8                 # small key (vanilla_item = SmallKey_<region>)
CREATURE_CONTENT = {2, 3, 14, 15, 16, 17, 18}  # rock-crab/bee/cucco/soldiers/...
RANDOM_CONTENT = 4              # nondeterministic at spawn — exclude
# content >= 0x80 -> structural (hole/warp/staircase/bombable/switch) — exclude.

# boss arenas (kBossRoom, shuffle_boss.c) + pinned-boss env rooms coincide with
# IP/TR/TT boss rooms; Agahnim/Ganon arenas.
BOSS_ROOMS = {200, 51, 7, 90, 6, 41, 172, 222, 144, 164}
EXCLUDED_ROOMS = BOSS_ROOMS | {
    0x00,   # Ganon arena
    0x0d,   # Agahnim 2 arena
    0x20,   # Agahnim 1 arena (Hyrule Castle Tower top)
    # Room 0x104 (Link's House, shared with the glitch-only Chris-Houlihan "Top
    # Secret Room" via entrance 130) is INCLUDED by owner request — its 3
    # HeartRefill pots are sphere-0 reachable via the start; the secret-room
    # context shows the same room + pots. Previously a D1 exclusion.
}

# boss room -> region (region-known flood anchors even though arenas have no pot)
BOSS_REGION = {
    200: "EasternPalace", 51: "DesertPalace", 7: "TowerOfHera",
    90: "PalaceOfDarkness", 6: "SwampPalace", 41: "SkullWoods",
    172: "ThievesTown", 222: "IcePalace", 144: "MiseryMire", 164: "TurtleRock",
}


# dungeon names with a SmallKey_<name> item — a key pot's region (which may be a
# sub-region like "GanonsTower_Lobby") maps to its base dungeon for the key item.
DUNGEON_NAMES = (
    "HyruleCastleEscape", "EasternPalace", "DesertPalace", "TowerOfHera",
    "HyruleCastleTower", "PalaceOfDarkness", "SwampPalace", "SkullWoods",
    "ThievesTown", "IcePalace", "MiseryMire", "TurtleRock", "GanonsTower",
)


def die(msg: str):
    print(f"gen_pot_tables: ERROR: {msg}", file=sys.stderr)
    sys.exit(2)


def write_lf(path: Path, text: str):
    with path.open("w", encoding="utf-8", newline="\n") as f:
        f.write(text)


def check_fresh(path: Path, expected: str) -> int:
    want = expected.encode("utf-8")
    have = path.read_bytes() if path.exists() else b""
    if have == want:
        print(f"gen_pot_tables: OK {path} is fresh", file=sys.stderr)
        return 0

    print(f"gen_pot_tables: ERROR: {path} is stale; run "
          f"`python assets/scripts/gen_pot_tables.py` to refresh the local artifact.",
          file=sys.stderr)
    if have.replace(b"\r\n", b"\n") == want:
        print("gen_pot_tables: drift is line-ending-only (expected LF).",
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


def _room_hex_to_int(room_hex):
    return int(str(room_hex), 16)


def _check_row_field(row, field, expected, label) -> bool:
    got = row.get(field)
    if got == expected:
        return True
    print(f"gen_pot_tables: ERROR: {label} {field} = {got!r}, expected {expected!r}",
          file=sys.stderr)
    return False


def check_override_static(overrides_path: Path) -> int:
    """Source-only validation for committed pot_logic_overrides.yaml.

    Public CI does not have the ROM-derived pots.gen.yaml, so it cannot run the
    full row freshness check. This catches the committed-file failures that do
    not require the local registry: malformed room ids, unknown regions,
    duplicate split positions, and contradictory override sections.
    """
    if not overrides_path.is_file():
        print(f"gen_pot_tables: ERROR: missing {overrides_path}", file=sys.stderr)
        return 1
    doc = yaml.safe_load(overrides_path.read_text(encoding="utf-8")) or {}
    valid = valid_regions()
    ok = True
    room_sections = collections.defaultdict(list)

    def parse_room(section: str, key) -> int | None:
        nonlocal ok
        try:
            room = _room_hex_to_int(key)
        except Exception as e:
            print(f"gen_pot_tables: ERROR: {section} room {key!r} is not hex: {e}",
                  file=sys.stderr)
            ok = False
            return None
        room_sections[room].append(section)
        return room

    for room_hex, region in (doc.get("room_region", {}) or {}).items():
        parse_room("room_region", room_hex)
        if region not in valid:
            print(f"gen_pot_tables: ERROR: room_region {room_hex!r} uses unknown "
                  f"region {region!r}", file=sys.stderr)
            ok = False

    for room_hex in (doc.get("room_can_reach", {}) or {}):
        parse_room("room_can_reach", room_hex)

    for room_hex, reason in (doc.get("room_free", {}) or {}).items():
        parse_room("room_free", room_hex)
        if not str(reason or "").strip():
            print(f"gen_pot_tables: ERROR: room_free {room_hex!r} needs a reason",
                  file=sys.stderr)
            ok = False

    for room_hex, clusters in (doc.get("pot_room_split", {}) or {}).items():
        room = parse_room("pot_room_split", room_hex)
        seen_pos = set()
        if not clusters:
            print(f"gen_pot_tables: ERROR: pot_room_split {room_hex!r} has no clusters",
                  file=sys.stderr)
            ok = False
            continue
        for idx, cl in enumerate(clusters):
            region = cl.get("region")
            if region not in valid:
                print(f"gen_pot_tables: ERROR: pot_room_split {room_hex!r}[{idx}] "
                      f"uses unknown region {region!r}", file=sys.stderr)
                ok = False
            pos = cl.get("pos4") or []
            if not pos:
                print(f"gen_pot_tables: ERROR: pot_room_split {room_hex!r}[{idx}] "
                      f"has no pos4 list", file=sys.stderr)
                ok = False
            for p in pos:
                try:
                    pi = int(p)
                except Exception as e:
                    print(f"gen_pot_tables: ERROR: pot_room_split {room_hex!r}[{idx}] "
                          f"pos4 {p!r} is not an integer: {e}", file=sys.stderr)
                    ok = False
                    continue
                if pi in seen_pos:
                    print(f"gen_pot_tables: ERROR: pot_room_split {room_hex!r} "
                          f"duplicates pos4 0x{pi:04x}", file=sys.stderr)
                    ok = False
                seen_pos.add(pi)
        if room is not None and "room_region" in room_sections[room]:
            print(f"gen_pot_tables: ERROR: room 0x{room:03x} cannot use both "
                  f"room_region and pot_room_split", file=sys.stderr)
            ok = False

    for room, sections in sorted(room_sections.items()):
        if "room_free" in sections and "room_can_reach" in sections:
            print(f"gen_pot_tables: ERROR: room 0x{room:03x} cannot be both "
                  f"room_free and room_can_reach", file=sys.stderr)
            ok = False

    if ok:
        print(f"gen_pot_tables: OK explicit pot overrides are source-valid",
              file=sys.stderr)
        return 0
    return 1


def check_override_sync(out_path: Path, overrides_path: Path) -> int:
    """ROM-free freshness guard for explicit pot_logic_overrides.yaml entries.

    Full pots.gen.yaml regeneration needs ROM-derived artifacts for chest anchors
    and pot geometry. CI does not have those, but it can still prove the
    local table reflects explicit overrides, including pot_room_split. That
    is the class that caused room 0x036 to drift.
    """
    if not out_path.is_file():
        print(f"gen_pot_tables: ERROR: missing {out_path}", file=sys.stderr)
        return 1
    if not overrides_path.is_file():
        print(f"gen_pot_tables: ERROR: missing {overrides_path}", file=sys.stderr)
        return 1
    pots_doc = yaml.safe_load(out_path.read_text(encoding="utf-8")) or {}
    rows = list(pots_doc.get("pots", []) or [])
    by_room = collections.defaultdict(list)
    by_ident = {}
    ok = True
    for row in rows:
        room, pos4 = int(row["room"]), int(row["pos4"])
        by_room[room].append(row)
        by_ident[(room, pos4)] = row

    ov = yaml.safe_load(overrides_path.read_text(encoding="utf-8")) or {}
    room_region = ov.get("room_region", {}) or {}
    room_can_reach = {str(k): v for k, v in (ov.get("room_can_reach", {}) or {}).items()}
    split_rooms = set()
    checked = 0

    for room_hex, clusters in (ov.get("pot_room_split", {}) or {}).items():
        room = _room_hex_to_int(room_hex)
        split_rooms.add(room)
        expected_pos = set()
        if room not in by_room:
            print(f"gen_pot_tables: ERROR: pot_room_split room 0x{room:03x} has no "
                  f"generated pot rows", file=sys.stderr)
            ok = False
            continue
        for cl in clusters or []:
            region = cl["region"]
            can_reach = cl.get("can_reach")
            for p in cl["pos4"]:
                pos4 = int(p)
                expected_pos.add(pos4)
                row = by_ident.get((room, pos4))
                label = f"pot_room_split room 0x{room:03x} pos 0x{pos4:04x}"
                if row is None:
                    print(f"gen_pot_tables: ERROR: {label} is missing from {out_path}",
                          file=sys.stderr)
                    ok = False
                    continue
                ok &= _check_row_field(row, "region", region, label)
                ok &= _check_row_field(row, "can_reach", can_reach, label)
                checked += 1
        actual_pos = {int(r["pos4"]) for r in by_room[room]}
        if actual_pos != expected_pos:
            print(f"gen_pot_tables: ERROR: pot_room_split room 0x{room:03x} covers "
                  f"{sorted(expected_pos)}, but committed rows are {sorted(actual_pos)}",
                  file=sys.stderr)
            ok = False

    for room_hex, gate in room_can_reach.items():
        room = _room_hex_to_int(room_hex)
        if room in split_rooms:
            continue
        expected = gate
        room_rows = by_room.get(room, [])
        if not room_rows:
            print(f"gen_pot_tables: ERROR: room_can_reach 0x{room:03x} has no "
                  f"generated pot rows", file=sys.stderr)
            ok = False
            continue
        for row in room_rows:
            label = f"room_can_reach 0x{room:03x} pos 0x{int(row['pos4']):04x}"
            ok &= _check_row_field(row, "can_reach", expected, label)
            checked += 1

    for room_hex, region in room_region.items():
        room = _room_hex_to_int(room_hex)
        if room in split_rooms:
            continue
        room_rows = by_room.get(room, [])
        if not room_rows:
            print(f"gen_pot_tables: ERROR: room_region 0x{room:03x} has no "
                  f"generated pot rows", file=sys.stderr)
            ok = False
            continue
        for row in room_rows:
            label = f"room_region 0x{room:03x} pos 0x{int(row['pos4']):04x}"
            ok &= _check_row_field(row, "region", region, label)
            if row.get("kind") == "key":
                dungeon = region_to_dungeon(region)
                if dungeon is not None:
                    ok &= _check_row_field(row, "vanilla_item", f"SmallKey_{dungeon}", label)
            checked += 1

    if ok:
        print(f"gen_pot_tables: OK explicit pot overrides match {out_path} "
              f"({checked} row checks)", file=sys.stderr)
        return 0
    return 1


def check_registry_id_budget(registry_path: Path):
    """The generated pot ids start at POT_BASE_ID; the base registry must stay below it."""
    doc = yaml.safe_load(registry_path.read_text(encoding="utf-8")) or {}
    max_id = -1
    max_name = ""
    for row in doc.get("locations", []) or []:
        if int(row.get("id", -1)) > max_id:
            max_id = int(row.get("id", -1))
            max_name = str(row.get("name", ""))
    if max_id >= POT_BASE_ID:
        die(f"{registry_path} uses id {max_id} ({max_name!r}), but pot ids start "
            f"at {POT_BASE_ID}. Move POT_BASE_ID above the base registry or "
            f"renumber before generating pots.")


def region_to_dungeon(region):
    """'GanonsTower_Lobby' -> 'GanonsTower'; None if not a dungeon region."""
    for d in DUNGEON_NAMES:
        if region == d or region.startswith(d + "_"):
            return d
    return None


def load_dump(path: Path, binary: str | None):
    if not path.is_file():
        if not binary:
            die(f"{path} missing and no --binary given to regenerate it. Run "
                f"`zelda3 --dump-pot-table {path}` (with assets present) first.")
        print(f"gen_pot_tables: regenerating {path} via {binary}", file=sys.stderr)
        subprocess.run([binary, "--dump-pot-table", str(path)], check=True, cwd=REPO)
    pots = []          # (room, pos4, content)
    out_edges = collections.defaultdict(set)
    dark_rooms = set()  # 'K' lines: hdr[0]&1 -> pots need (Lamp OR CanDarkRoomNav)
    for line in path.read_text().splitlines():
        if line.startswith("#") or not line.strip():
            continue
        t = line.split()
        if t[0] == "P":
            pots.append((int(t[1], 16), int(t[2], 16), int(t[3])))
        elif t[0] == "D":
            r = int(t[1], 16)
            for nb in t[2:]:
                out_edges[r].add(int(nb, 16))
        elif t[0] == "K":
            dark_rooms.add(int(t[1], 16))
    return pots, out_edges, dark_rooms


def valid_regions():
    """The set of REGION ids defined in the logic graph (logic.yaml +
    logic_parts, skip inverted/). A pot's region MUST be one of these or
    rando_logic_gen.py encodes region_id=0xFFFF (silent sphere-0 bypass)."""
    out = set()
    files = [REPO / "assets/rando/logic.yaml"]
    files += [p for p in sorted((REPO / "assets/rando/logic_parts").rglob("*.yaml"))
              if "inverted" not in p.parts]
    for fp in files:
        doc = yaml.safe_load(fp.read_text()) or {}
        for r in doc.get("regions", []) or []:
            if r.get("id"):
                out.add(r["id"])
    return out


def logic_loc_region_canreach():
    """name -> (region, can_reach) from the LOGIC graph (logic.yaml then
    logic_parts/*.yaml sorted, last-wins) — the SAME source rando_logic_gen.py
    binds region_id from. The registry's `region:` is descriptive-only and uses
    coarse bare dungeon names (e.g. "GanonsTower"); the real region is the logic
    sub-region (e.g. "GanonsTower_Lobby"), so anchors MUST come from here. We skip
    logic_parts/inverted/** (a separate world-state overlay)."""
    out = {}
    files = [REPO / "assets/rando/logic.yaml"]
    parts = sorted((REPO / "assets/rando/logic_parts").rglob("*.yaml"))
    files += [p for p in parts if "inverted" not in p.parts]
    for fp in files:
        doc = yaml.safe_load(fp.read_text()) or {}
        for loc in doc.get("locations", []) or []:
            nm = loc.get("id") or loc.get("name")
            if nm and loc.get("region"):
                out[nm] = (loc["region"], loc.get("can_reach"))
    return out


def chest_room_anchors():
    """room -> set of (region, can_reach) tuples of its chests. A room with ONE
    distinct tuple is a UNIFORM anchor (its pots inherit that region+predicate,
    D8); a room with multiple tuples is non-uniform -> a reviewed override."""
    name2 = logic_loc_region_canreach()
    room2 = collections.defaultdict(set)
    for (room, _ord, name, *_rest) in chest_data.get_chest_lookup_rows():
        if room in CHEST_ROOM_ALIAS_EXCLUSIONS:
            continue
        if name in name2:
            room2[room].add(name2[name])   # (region, can_reach)
    return room2


def derive_regions(pot_rooms, out_edges, overrides, override_gates):
    """room -> (region, can_reach, source). source in {chest, boss, flood,
    flood2, vstair, override}. A uniform chest room carries its chest's predicate
    (D8 inheritance); flooded chest-LESS rooms default to can_reach=None (TRUE,
    assumed reachable on dungeon entry — gated chest-less rooms must be added to
    the overrides' room_can_reach, verified by playtest)."""
    room2 = chest_room_anchors()
    anchor = {r: next(iter(s)) for r, s in room2.items() if len(s) == 1}  # r -> (region, can_reach)
    # NOTE: boss rooms are deliberately NOT region anchors. They have no pots, and
    # their bare dungeon name (BOSS_REGION) is not always a valid LOGIC region
    # (e.g. "MiseryMire"/"TurtleRock" don't exist — only *_Lobby), so anchoring on
    # them propagated an invalid region_id=0xFFFF into boss-flooded rooms. Those
    # rooms now flood from chest anchors (valid sub-regions) or surface as reviewed
    # overrides — exactly the D8 contract.

    # reciprocal door adjacency only (drops one-way exit doors that bridge dungeons)
    recip = collections.defaultdict(set)
    for r, ns in out_edges.items():
        for n in ns:
            if r in out_edges.get(n, ()):
                recip[r].add(n); recip[n].add(r)
    alladj = collections.defaultdict(set)
    for r, ns in out_edges.items():
        for n in ns:
            alladj[r].add(n); alladj[n].add(r)

    # Pass 1 — reciprocal-door flood (clean dungeon core, no exit-bridge leaks).
    region_of = {r: (rc[0], rc[1], "chest" if r in room2 else "boss") for r, rc in anchor.items()}
    conflicts = {}
    frontier = collections.deque(anchor)
    while frontier:
        r = frontier.popleft()
        for n in recip[r]:
            if n not in region_of:
                region_of[n] = (region_of[r][0], None, "flood"); frontier.append(n)
            elif region_of[n][0] != region_of[r][0] and n not in anchor:
                conflicts.setdefault(n, set()).update({region_of[n][0], region_of[r][0]})

    # Pass 2 — extend into still-unassigned rooms via ANY door edge, but ONLY when
    # every already-assigned door-neighbor agrees on the region.
    changed = True
    while changed:
        changed = False
        for r in pot_rooms:
            if r in region_of or r in conflicts:
                continue
            regs = {region_of[n][0] for n in alladj[r] if n in region_of}
            if len(regs) == 1:
                region_of[r] = (next(iter(regs)), None, "flood2"); changed = True
            elif len(regs) > 1:
                conflicts[r] = regs

    # Pass 3 — inter-room stairs run VERTICALLY: a room whose up (R-0x10) and down
    # (R+0x10) neighbors BOTH resolve to the same region is almost certainly it.
    changed = True
    while changed:
        changed = False
        for r in pot_rooms:
            if r in region_of and r not in conflicts:
                continue
            up = region_of.get(r - 0x10, (None,))[0]
            dn = region_of.get(r + 0x10, (None,))[0]
            if up is not None and up == dn:
                region_of[r] = (up, None, "vstair"); conflicts.pop(r, None); changed = True

    # Non-uniform chest rooms (chests disagree on region OR predicate) can't be
    # auto-inherited -> force a reviewed override (D8 "non-uniform" case).
    for r, s in room2.items():
        if len(s) > 1:
            conflicts.setdefault(r, set()).update(reg for reg, _cr in s)

    # reviewed overrides win over everything (region + optional per-room gate)
    for room_hex, reg in (overrides or {}).items():
        room = int(str(room_hex), 16)
        region_of[room] = (reg, override_gates.get(str(room_hex)), "override")
        conflicts.pop(room, None)

    # A room_can_reach override WITHOUT a matching room_region applies its gate to
    # the room's already-flooded region — for gating a chest-LESS sub-area the flood
    # resolved correctly (e.g. a dark / key-locked dungeon interior) without a
    # redundant region re-bind. The room MUST have a resolved region.
    for room_hex, gate in (override_gates or {}).items():
        if room_hex in (overrides or {}):
            continue  # already applied above with its explicit region
        room = int(str(room_hex), 16)
        if room not in region_of:
            die(f"room_can_reach override for 0x{room:03x} has no resolved region "
                f"— add a room_region override too")
        region_of[room] = (region_of[room][0], gate, "override_gate")

    # cross-validate: every uniform chest room's region must match the flood
    bad = [(hex(r), region_of.get(r, (None,))[0], next(iter(s))[0])
           for r, s in room2.items()
           if len(s) == 1 and region_of.get(r, (None,))[0] != next(iter(s))[0]]
    if bad:
        die(f"chest-room cross-validation FAILED (flood disagrees with chest region): {bad}")
    return region_of, conflicts


def classify(content):
    """-> (in_scope, tier, kind, item_or_None). tier in {keys, contents, all}."""
    if content == -1:
        return True, "all", "empty", None            # empty pot (All tier only)
    if content == KEY_CONTENT:
        return True, "keys", "key", None              # item filled from region
    if content in LOOT_ITEM:
        return True, "contents", "loot", LOOT_ITEM[content]
    if content in CREATURE_CONTENT or content == RANDOM_CONTENT or content >= 0x80:
        return False, None, "excluded", None          # creature/random/structural
    die(f"unclassifiable content byte {content} appears in an in-scope pot — add "
        f"a policy in LOOT_ITEM/CREATURE_CONTENT or it would ship a wrong item")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--dump", default=str(REPO / "assets/rando/pot_dump.gen.txt"))
    ap.add_argument("--overrides", default=str(REPO / "assets/rando/pot_logic_overrides.yaml"))
    ap.add_argument("--out", default=str(REPO / "assets/rando/pots.gen.yaml"))
    ap.add_argument("--binary", default=None, help="zelda3 path to (re)generate the dump")
    ap.add_argument("--report", action="store_true", help="print residual rooms + exit, don't emit")
    ap.add_argument("--check", action="store_true",
                    help="verify --out is byte-identical to regenerated output")
    ap.add_argument("--check-overrides", action="store_true",
                    help="verify explicit pot_logic_overrides.yaml entries match --out")
    ap.add_argument("--check-overrides-static", action="store_true",
                    help="source-only validation for pot_logic_overrides.yaml")
    args = ap.parse_args()

    if args.check_overrides_static:
        return check_override_static(Path(args.overrides))

    if args.check_overrides:
        return check_override_sync(Path(args.out), Path(args.overrides))

    check_registry_id_budget(REPO / "assets/rando/location_registry.yaml")

    pots, out_edges, dark_rooms = load_dump(Path(args.dump), args.binary)
    overrides, override_gates, room_free = {}, {}, {}
    pot_split, split_rooms = {}, set()  # (room,pos4) -> (region, can_reach); shared-cave per-cluster split
    ovp = Path(args.overrides)
    if ovp.is_file():
        doc = yaml.safe_load(ovp.read_text()) or {}
        overrides = doc.get("room_region", {}) or {}        # room_hex -> region
        override_gates = {str(k): v for k, v in (doc.get("room_can_reach", {}) or {}).items()}
        # Reviewed-FREE rooms the leak GUARD would otherwise flag — reachable via a
        # hole / portal / real door off a TRUE() chest that the grid-adjacency dump
        # can't see (room_hex -> one-line reason). The guard skips these and HARD-
        # FAILS on any un-listed, un-gated leak, so a NEW flag = a real regression.
        room_free = {str(k): v for k, v in (doc.get("room_free", {}) or {}).items()}
        # add-rando-pot-sanity (audit) — per-cluster region/gate for SHARED cave
        # interiors whose pots reach via DIFFERENT overworld entrances, each with its
        # own logic (e.g. room 0x11b = refill_cave_1: 8 pots via the Graveyard-Ledge
        # mirror spot, 6 via the Cave-45 / DW-south mirror spot). The room-level
        # room_region can't carry two reachability classes, so each cluster lists its
        # pos4 set + region + can_reach. room_hex -> [{region, can_reach, pos4:[...]}].
        for room_hex, clusters in (doc.get("pot_room_split", {}) or {}).items():
            rint = int(str(room_hex), 16)
            split_rooms.add(rint)
            for cl in (clusters or []):
                for p in cl["pos4"]:
                    pot_split[(rint, int(p))] = (cl["region"], cl.get("can_reach"))

    # group pots by room; drop excluded rooms + out-of-scope content
    pot_rooms = sorted({r for (r, _p, _c) in pots})
    region_of, conflicts = derive_regions(pot_rooms, out_edges, overrides, override_gates)

    inscope = []  # (room, pos4, content, tier, kind, item)
    for (room, pos4, content) in pots:
        if room in EXCLUDED_ROOMS:
            continue
        in_scope, tier, kind, item = classify(content)
        if not in_scope:
            continue
        inscope.append((room, pos4, content, tier, kind, item))

    # residual = in-scope pot rooms with no confident region (unassigned OR a
    # flagged conflict). These MUST get a reviewed pot_logic_overrides.yaml entry.
    inscope_rooms = {r for (r, _p, _c, _t, _k, _i) in inscope}
    residual = sorted(
        r for r in inscope_rooms
        if r not in split_rooms and (r not in region_of or r in conflicts)
    )
    if args.report or residual:
        room_pots = collections.Counter(r for (r, _p, _c, _t, _k, _i) in inscope)
        ent = (REPO / "assets/tables.py").read_text()
        m = re.search(r'kEntranceNames\s*=\s*"""(.*?)"""', ent, re.S)
        named = {}
        for line in (m.group(1).splitlines() if m else []):
            mm = re.match(r'\[Room #(\d+)\]\s*--\s*(.+)', line.strip())
            if mm:
                named[int(mm.group(1))] = mm.group(2)
        print(f"=== {len(residual)} in-scope pot rooms need a reviewed region "
              f"(pot_logic_overrides.yaml room_region:) ===", file=sys.stderr)
        for r in residual:
            cand = f" candidates={sorted(conflicts[r])}" if r in conflicts else ""
            print(f"  0x{r:03x}: {room_pots[r]:2d} pots  {named.get(r, ''):42s}{cand}", file=sys.stderr)
        if args.report:
            return
        if residual:
            die(f"{len(residual)} pot rooms unresolved — add them to {args.overrides} "
                f"(room_region:) per design D8, or they would silently sphere-0.")

    # assign ids = rank in the (room, pos4) sort from POT_BASE_ID; emit rows.
    # NOT append-only: inserting/removing a pot renumbers higher ids, so a
    # pot-set change must bump kGeneratorVersion (enforced by the corpus regen).
    inscope.sort(key=lambda x: (x[0], x[1]))
    # uniqueness of identity
    seen = set()
    rows = []
    dungeon_room_names = {}
    valid = valid_regions()
    excluded_keys = 0
    for (room, pos4, content, tier, kind, item) in inscope:
        if (room, pos4) in seen:
            die(f"duplicate pot identity (room 0x{room:03x}, pos4 0x{pos4:04x})")
        seen.add((room, pos4))
        if (room, pos4) in pot_split:
            region, can_reach = pot_split[(room, pos4)]
        elif room in split_rooms:
            die(f"pot (room 0x{room:03x}, pos4 0x{pos4:04x}) is in a pot_room_split room "
                f"but no cluster lists its pos4 — every split-room pot must be covered")
        else:
            region, can_reach, _src = region_of[room]
        if region not in valid:
            die(f"pot room 0x{room:03x} bound to region {region!r} which is NOT a "
                f"defined logic region — rando_logic_gen would encode 0xFFFF "
                f"(silent sphere-0). Fix the {region_of[room][2]} source.")
        # add-rando-pot-sanity — a Dark World OVERWORLD pot needs the "not a bunny"
        # gate: without the Moon Pearl you are a bunny in the DW and cannot LIFT
        # anything. The region is reachable as a bunny (you walk in through a
        # portal), but lifting the pot is not — exactly like every base DW location,
        # which all require Moon Pearl. Chest-bearing DW rooms already inherit this
        # from their chest's predicate; only the chest-less DW caves/houses fell
        # back to TRUE() (the too-early bug). Apply the standard ALTTPR pattern.
        # (Open/Standard. Inverted's DW is the bunny-free home world, so this
        # slightly OVER-restricts inverted pots — a known follow-up; inverted +
        # pot_shuffle is currently untested.)
        if region.startswith("DarkWorld") and (not can_reach or can_reach.strip() == "TRUE()"):
            can_reach = "(HAS_ITEM(MoonPearl) OR CanPearlBypass())"
        # add-rando-pot-sanity — a DARK room (engine hdr[0]&1, dump 'K' line) is
        # unreachable without light: AND the Lamp gate onto whatever reach predicate
        # the room already has. Chest-less dark rooms default to TRUE() -> just the
        # Lamp gate (fixes the HCE-sewer / EP-interior sphere-0 leak); a DW dark room
        # keeps its Moon Pearl gate too. Skip when the predicate already requires the
        # Lamp (a chest-BEARING dark room inherits it from its chest's predicate).
        # Link's House carries the engine dark bit for the opening sequence, but
        # the room is lit for normal randomizer play; do not push its heart pots
        # behind Lamp.
        if room != 0x104 and room in dark_rooms and "Lamp" not in (can_reach or ""):
            dark_pred = "(HAS_ITEM(Lamp) OR CanDarkRoomNav())"
            can_reach = dark_pred if (not can_reach or can_reach.strip() == "TRUE()") \
                else f"({can_reach}) AND {dark_pred}"
        if kind == "key":
            dungeon = region_to_dungeon(region)
            if dungeon is None:
                # A small-key pot outside any dungeon region (the Pyramid Fairy
                # room 0x16 is the lone case in US data) can't be a dungeon key
                # location and doesn't fit the key economy (D7) — exclude it; the
                # room's other (loot) pots stay in scope.
                print(f"gen_pot_tables: NOTE excluding key pot room 0x{room:03x} "
                      f"pos 0x{pos4:04x} — region {region!r} is not a dungeon",
                      file=sys.stderr)
                seen.discard((room, pos4)); excluded_keys += 1
                continue
            item = f"SmallKey_{dungeon}"
        name = f"{region} Pot R{room:03X} P{pos4:04X}"
        rows.append({
            "id": POT_BASE_ID + len(rows), "name": name, "region": region, "type": "Pot",
            "room": room, "pos4": pos4, "content": content, "tier": tier,
            "kind": kind, "vanilla_item": item,
            **({"can_reach": can_reach} if can_reach else {}),
        })

    # add-rando-pot-sanity — per-room reachability GUARD (backstop for the chest-
    # less-room sphere-0 leak class: a pot room that flooded into a dungeon region
    # but, being chest-LESS, defaulted to TRUE(), while it is actually reachable
    # only past an in-dungeon gate — the placer treats it as sphere-0 and can
    # strand progression there). A room with NO gate whose reciprocal-door /
    # vertical-stair neighbors that hold a base CHEST are ALL gated is such a leak;
    # surface it with the neighbors' predicates (the weakest is the override to
    # author). Overworld regions have no in-dungeon gates and are skipped.
    _chest = {r: next(iter(s)) for r, s in chest_room_anchors().items() if len(s) == 1}
    def _is_gated(cr):  # a None/TRUE() chest is a FREE entrance, not a gate
        return cr is not None and str(cr).strip() != "TRUE()"
    _radj = collections.defaultdict(set)
    for _r, _ns in out_edges.items():
        for _n in _ns:
            if _r in out_edges.get(_n, ()):
                _radj[_r].add(_n); _radj[_n].add(_r)
    _guard = {}
    for _row in rows:
        if "can_reach" in _row or region_to_dungeon(_row["region"]) is None:
            continue
        _room = _row["room"]
        if _room in _guard or f"{_room:03x}" in room_free:
            continue
        _nbrs = _radj[_room] | {_room - 0x10, _room + 0x10}
        # IN-REGION base-chest neighbors only (a cross-region grid-adjacency edge is
        # not a real in-dungeon path); a leak = EVERY such neighbor is gated.
        _cn = [n for n in _nbrs if n in _chest and _chest[n][0] == _row["region"]]
        if _cn and all(_is_gated(_chest[n][1]) for n in _cn):
            _guard[_room] = (_row["region"], {f"0x{n:03x}": _chest[n][1] for n in _cn})
    if _guard:
        print(f"=== POT GUARD: {len(_guard)} chest-less TRUE() pot room(s) reachable "
              f"ONLY via gated chests — sphere-0 LEAK ===", file=sys.stderr)
        for _room in sorted(_guard):
            _reg, _ev = _guard[_room]
            print(f"  0x{_room:03x} ({_reg}) <- {_ev}", file=sys.stderr)
        die(f"{len(_guard)} un-triaged pot-room leak(s) — for each, add a "
            f"room_can_reach: gate (the weakest neighbor predicate, +1 key if the "
            f"connecting door is a vanilla key door) OR, if it is genuinely reachable "
            f"via a hole/portal the grid-adjacency dump can't see, add it to "
            f"room_free: with a reason. See {args.overrides}.")

    # 0x100+ cave/house rooms are especially prone to false confidence: many
    # share engine rooms, cached exits, or one-way arrivals that the reciprocal
    # flood cannot model. Require each in-scope cave/house pot room to be
    # explicitly reviewed in pot_logic_overrides.yaml, or split by pos4.
    _explicit_rooms = {_room_hex_to_int(k) for k in overrides}
    _unreviewed_1xx = sorted({
        int(_row["room"]) for _row in rows
        if int(_row["room"]) >= 0x100 and
           int(_row["room"]) not in _explicit_rooms and
           int(_row["room"]) not in split_rooms
    })
    if _unreviewed_1xx:
        for _room in _unreviewed_1xx:
            print(f"gen_pot_tables: ERROR: 0x{_room:03x} is a cave/house pot room "
                  f"without an explicit room_region or pot_room_split review",
                  file=sys.stderr)
        die(f"{len(_unreviewed_1xx)} cave/house pot room(s) rely on cross-room "
            f"flood; bind them in {args.overrides}.")

    # tier nesting assertion: keys subset contents subset all (by membership rule)
    keys_n = sum(1 for r in rows if r["kind"] == "key")
    contents_n = sum(1 for r in rows if r["tier"] in ("keys", "contents"))
    all_n = len(rows)
    assert keys_n <= contents_n <= all_n, (keys_n, contents_n, all_n)

    out = {
        "_generated_by": "assets/scripts/gen_pot_tables.py (do not hand-edit)",
        "tier_counts": {"keys": keys_n, "contents": contents_n, "all": all_n},
        "pots": rows,
    }
    out_text = yaml.safe_dump(out, sort_keys=False, width=200, default_flow_style=False)
    out_path = Path(args.out)
    if args.check:
        return check_fresh(out_path, out_text)

    write_lf(out_path, out_text)
    print(f"gen_pot_tables: {all_n} pots (keys {keys_n} / contents {contents_n} / "
          f"all {all_n}) -> {args.out}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
