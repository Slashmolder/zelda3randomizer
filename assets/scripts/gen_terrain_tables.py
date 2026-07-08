#!/usr/bin/env python3
"""Grass/rock drop shuffle: terrain registry generator (Phase 1: measurement).

Consumes the engine ground-truth dump produced by `zelda3 --dump-terrain-table`
(see Overworld_DumpTerrainTable in src/overworld.c and the openspec change
add-rando-grass-rock-shuffle, design.md D2/D4) and:

  1. Intersects object presence across ALL state profiles (vanilla, +event
     overlays, inverted, inverted+events, pre-rescue): an object must be
     present WITH IDENTICAL CLASS in every profile or it is EXCLUDED and
     reported (missable-check vector -- spec requirement "Terrain check
     registry").
  2. Applies the structural exclusions: secret data byte >= 0x80 (tile-below
     reveals: holes/water/stairs/portals) keeps vanilla behavior; signs (S)
     and dash-only bonk piles (D) are out of scope.
  3. Reconciles every kOverworldSecrets entry (S lines) against the object
     set: each secret is either carried by a registered object, structural,
     or sits on a non-consumable tile (e.g. dig spots) -- anything else is a
     hard failure.
  4. Reports per-class / per-axis / per-world counts and the location-ID
     capacity math (tasks.md 1.6 gate: the owner locks the ID base and any
     kRandoLocationCapacity decision from these numbers BEFORE Phase 2 binds
     them).

Phase 2 will extend this script to emit assets/rando/terrain.gen.yaml
(region-bound, predicate-carrying registry rows) -- the counting core here is
that emitter's foundation.

Usage:
  python assets/scripts/gen_terrain_tables.py [--dump assets/rando/terrain_dump.gen.txt]
"""

import argparse
import collections
import os
import sys

# Engine glove requirement per liftable attr class, ground-truthed from
# src/tile_detect.c kTile50data = {0x54,0x52,0x50,0x51,0x53,0x55,0x56} (attr ->
# interaction index) composed with src/player.c:5904
# kGetBestActionToPerformOnTile_a = {0,1,0,0,2,1,2} (index -> required glove
# level; lift allowed when level <= link_item_gloves):
#   0x50 bush = 0 (bare), 0x51 bush-2 = 0 (bare), 0x52 small rock = 1 (Power
#   Glove), 0x53 small dark rock = 2 (Titan's Mitt), 0x54 sign = 0,
#   0x55 big pile = 1 (Glove), 0x56 big dark pile = 2 (Mitt).
# 0x57 = dash-only bonk class (excluded), 0x40 = thick grass (cut, no lift).
GLOVE_BY_ATTR = {0x50: 0, 0x51: 0, 0x52: 1, 0x53: 2, 0x54: 0, 0x55: 1, 0x56: 2}

# kind letter -> (class name, axis, in_scope)
KIND_INFO = {
    "B": ("bush_lw", "grass", True),
    "b": ("bush_dw", "grass", True),
    "G": ("thick_grass", "grass", True),
    "R": ("rock_light", "rock", True),       # map16 0x20f, attr 0x52 (Glove)
    "r": ("rock_heavy", "rock", True),       # map16 0x239, attr 0x53 (Mitt)
    "P": ("rock_pile_light", "rock", True),  # 2x2 pile, 0x36d origin, attr 0x55
    "Q": ("rock_pile_heavy", "rock", True),  # 2x2 pile, 0x23b origin, attr 0x56
    "D": ("bonk_pile", None, False),         # dash-only; future tier, excluded
    "S": ("sign", None, False),              # excluded per owner scope
}

# Dump profiles (see --dump-terrain-table). Indices partition into two
# WORLD-STATE GROUPS whose within-seed latch states must each be internally
# consistent but which need NOT agree across the inverted boundary (Inverted is
# a different overworld, not a within-seed latch). Registration + world_state_
# filter assignment happens per group in main(); see NONINV/INV there.
#   0 vanilla, 1 +events, 4 pre-rescue  -> NON-INVERTED (open/standard/retro)
#   2 inverted, 3 inverted+events       -> INVERTED

# Location-id base (Phase-1 owner gate, tasks.md 1.6, corrected during Phase-2
# validation): the enemy-check block runs 1400..2633 — the original "ends at
# 1999" reading came from a `grep "id: 1[0-9]*"` that could not match ids
# >= 2000, and the resulting base of 2048 COLLIDED with live enemy-check ids
# (caught by the corpus: enemy-drop-all digests moved; rando_logic_gen now
# hard-fails duplicate ids). 3072 leaves the enemy family ~438 ids of growth
# (boss/scripted phases pending) and keeps terrain (3943 rows) under the 8192
# capacity: 3072..7014.
TERRAIN_BASE_ID = 3072

# ---------------------------------------------------------------------------
# Screen -> logic-region binding for ALL parent areas (0x00-0x7F).
#
# Grounding: every screen also covered by the REVIEWED enemy-check table
# (OVERWORLD_REGIONS, assets/scripts/gen_enemy_check_tables.py:511) uses that
# binding (source "table"). Screens that table does not cover are bound by
# geography/mirror derivation (source "derived") and MUST be review-confirmed
# before the terrain tiers activate. Split screens (ledges / fences / rivers /
# Book-gated desert floor) are flagged with a note: the DEFAULT binding may
# under-gate a sub-area, so before Phase 3 flips the axes on, every flagged
# screen needs a terrain_logic_overrides.yaml entry (or a reviewed "no override
# needed" verdict) — design.md D6, never-under-gate. Phase 2 keeps terrain
# locations inert at every placement site, so these bindings have zero
# placement effect until then.
#
# Entry: screen -> (region, source, split_note_or_None)
SCREEN_REGIONS = {
    # --- Light World ---
    0x00: ("LightWorld_NorthWest", "table", None),            # Lost Woods (big)
    0x02: ("LightWorld_NorthWest", "table", None),            # Lumberjack
    0x03: ("LightWorld_DeathMountain_West", "table",
           "DM ledges; lower foothill vs upper screen sub-areas"),
    0x05: ("LightWorld_DeathMountain_West", "table",
           "spans the West/East DM boundary (hookshot gap); east half is "
           "LightWorld_DeathMountain_East"),
    0x07: ("LightWorld_DeathMountain_East", "derived",
           "upper East-DM screen; ledge sub-areas"),
    0x0a: ("LightWorld_DeathMountain_West", "table", None),   # DM entrance foothill
    0x0f: ("LightWorld_NorthEast", "table",
           "Zora river banks; far bank may need flippers"),
    0x10: ("LightWorld_NorthWest", "table", None),
    0x11: ("LightWorld_NorthWest", "table", None),
    0x12: ("LightWorld_NorthWest", "table", None),
    0x13: ("LightWorld_NorthWest", "table", None),
    0x14: ("LightWorld_NorthWest", "table", None),
    0x15: ("LightWorld_South", "table", None),
    0x16: ("LightWorld_NorthEast", "table", None),
    0x17: ("LightWorld_NorthEast", "table", None),
    0x18: ("LightWorld_NorthWest", "table", None),            # Kakariko (big)
    0x1a: ("LightWorld_NorthWest", "table", None),
    0x1b: ("LightWorld_NorthEast", "derived",
           "castle grounds (big): enemy table binds HCE for Standard "
           "pre-rescue guards; terrain uses the OW region — confirm "
           "Standard rain-state reachability treatment"),
    0x1d: ("LightWorld_NorthEast", "derived",
           "castle east side; same HCE nuance as 0x1b"),
    0x1e: ("LightWorld_NorthEast", "table", None),            # Eastern ruins (big)
    0x22: ("LightWorld_NorthWest", "table", None),            # Smithy
    0x25: ("LightWorld_NorthEast", "table", None),
    0x28: ("LightWorld_NorthWest", "table",
           "race-game fenced strip; confirm walk-in access"),
    0x29: ("LightWorld_NorthWest", "table", None),
    0x2a: ("LightWorld_South", "derived", None),              # Haunted grove
    0x2b: ("LightWorld_South", "table", None),
    0x2c: ("LightWorld_South", "table", None),
    0x2d: ("LightWorld_NorthEast", "table",
           "river-split screen; far-bank objects may need flippers"),
    0x2e: ("LightWorld_NorthEast", "table", None),
    0x2f: ("LightWorld_NorthEast", "table", None),
    0x30: ("LightWorld_South", "table",
           "Desert (big): raised desert floor/ledge is Book-gated; walkable "
           "south strip is South — needs pos-level override review"),
    0x32: ("LightWorld_South", "table", None),
    0x33: ("LightWorld_South", "table", None),
    0x34: ("LightWorld_South", "table", None),
    0x35: ("LightWorld_South", "table", None),                # Lake Hylia (big)
    0x37: ("LightWorld_South", "table", None),
    0x3a: ("LightWorld_South", "table", None),
    0x3b: ("LightWorld_South", "table", None),
    0x3c: ("LightWorld_South", "table", None),
    0x3f: ("LightWorld_South", "table", None),
    # --- Dark World ---
    0x40: ("DarkWorld_NorthWest", "table", None),             # Skull Woods (big)
    0x42: ("DarkWorld_NorthWest", "table", None),
    0x43: ("DarkWorld_DeathMountain_West", "derived",
           "mirror of 0x03; DM ledge sub-areas"),
    0x45: ("DarkWorld_DeathMountain_East", "derived",
           "mirror of 0x05; floating-island / hookshot sub-areas"),
    0x47: ("DarkWorld_DeathMountain_East", "derived",
           "Turtle Rock screen; TR ledge is medallion/mirror-gated"),
    0x4a: ("DarkWorld_DeathMountain_West", "table",
           "Bumper cave ledge is a mirror-gated sub-area"),
    0x4f: ("DarkWorld_NorthEast", "table", None),
    0x50: ("DarkWorld_NorthWest", "table", None),
    0x51: ("DarkWorld_NorthWest", "table", None),
    0x52: ("DarkWorld_NorthWest", "table", None),
    0x53: ("DarkWorld_NorthWest", "table", None),
    0x54: ("DarkWorld_NorthWest", "table", None),
    0x55: ("DarkWorld_South", "table", None),
    0x56: ("DarkWorld_NorthEast", "table", None),
    0x57: ("DarkWorld_NorthEast", "table", None),
    0x58: ("DarkWorld_NorthWest", "table", None),             # Village of Outcasts (big)
    0x5a: ("DarkWorld_NorthWest", "table", None),
    0x5b: ("DarkWorld_NorthEast", "table",
           "pyramid (big): upper pyramid ledge is fall-in/Aga-gated"),
    0x5d: ("DarkWorld_NorthEast", "table", None),
    0x5e: ("DarkWorld_NorthEast", "table", None),             # DW eastern ruins (big)
    0x62: ("DarkWorld_NorthWest", "derived",
           "mirror of 0x22; hammer-peg yard is Hammer+Mitt-gated"),
    0x65: ("DarkWorld_NorthEast", "table", None),
    0x68: ("DarkWorld_NorthWest", "derived",
           "mirror of 0x28 (digging-game area) — confirm vs ALTTPR NW"),
    0x69: ("DarkWorld_NorthWest", "derived", None),
    0x6a: ("DarkWorld_South", "derived", None),               # Stumpy grove
    0x6b: ("DarkWorld_South", "table", None),
    0x6c: ("DarkWorld_South", "table", None),
    0x6d: ("DarkWorld_NorthEast", "table", None),
    0x6e: ("DarkWorld_NorthEast", "table", None),
    0x6f: ("DarkWorld_NorthEast", "table", None),
    0x70: ("DarkWorld_Mire", "table", None),                  # Mire (big)
    0x72: ("DarkWorld_South", "table", None),
    0x73: ("DarkWorld_South", "table", None),
    0x74: ("DarkWorld_South", "table", None),
    0x75: ("DarkWorld_South", "table", None),                 # Lake ice (big)
    0x77: ("DarkWorld_South", "table", None),
    0x7a: ("DarkWorld_Mire", "table", None),
    0x7b: ("DarkWorld_South", "table", None),
    0x7c: ("DarkWorld_South", "table", None),
    0x7f: ("DarkWorld_South", "table", None),
}

# Class -> (short label for names, tool-gate macro or None)
CLASS_GATES = {
    "bush_lw": ("Bush", None),
    "bush_dw": ("Bush", None),
    "thick_grass": ("Grass", "CanCutGrass()"),
    "rock_light": ("RockL", "CanLiftRocks()"),
    "rock_heavy": ("RockH", "CanLiftDarkRocks()"),
    "rock_pile_light": ("PileL", "CanLiftRocks()"),
    "rock_pile_heavy": ("PileH", "CanLiftDarkRocks()"),
}


def parse_dump(path):
    areas = {}          # scr -> "small"|"big"
    secrets = []        # (scr, pos, data)
    objs = {}           # (scr,pos) -> dict(kind=..., m16=..., attrs=..., content=..., profiles=set())
    unknowns = []       # (prof, scr, pos, m16, attrs)
    with open(path, "r", encoding="utf-8") as f:
        for ln in f:
            ln = ln.strip()
            if not ln or ln.startswith("#"):
                continue
            parts = ln.split()
            tag = parts[0]
            if tag == "A":
                areas[int(parts[1], 16)] = parts[2]
            elif tag == "S":
                secrets.append((int(parts[1], 16), int(parts[2], 16), int(parts[3])))
            elif tag == "T":
                prof = int(parts[1])
                scr, pos, m16 = int(parts[2], 16), int(parts[3], 16), int(parts[4], 16)
                kind = parts[5]
                attrs = tuple(int(x, 16) for x in parts[6:10])
                content = int(parts[10])
                key = (scr, pos)
                rec = objs.get(key)
                if rec is None:
                    # by_prof[prof] = (kind, m16, content). The registration
                    # decision is PER WORLD-STATE GROUP (see main): an object is
                    # valid in the world_states whose within-seed profiles all
                    # agree on its class — NOT the intersection across the
                    # inverted boundary (inverted is a different overworld, not a
                    # within-seed latch, so requiring inverted-presence wrongly
                    # dropped Light-World bushes from every non-inverted seed).
                    rec = {"attrs": attrs, "by_prof": {}}
                    objs[key] = rec
                rec["by_prof"][prof] = (kind, m16, content)
            elif tag == "U":
                unknowns.append((int(parts[1]), int(parts[2], 16), int(parts[3], 16),
                                 int(parts[4], 16), tuple(int(x, 16) for x in parts[5:9])))
    return areas, secrets, objs, unknowns


def glove_level(kind, attrs):
    """Required glove level for a rock object, from its map8 attr classes."""
    lift_attrs = [a for a in attrs if a in GLOVE_BY_ATTR]
    if not lift_attrs:
        return None
    levels = {GLOVE_BY_ATTR[a] for a in lift_attrs}
    if len(levels) != 1:
        return ("MIXED", tuple(sorted(levels)))
    return levels.pop()


def main():
    ap = argparse.ArgumentParser()
    repo = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
    ap.add_argument("--dump", default=os.path.join(repo, "assets", "rando", "terrain_dump.gen.txt"))
    ap.add_argument("--emit", nargs="?", const=os.path.join(repo, "assets", "rando", "terrain.gen.yaml"),
                    default=None, help="write the terrain registry YAML (default path when bare)")
    args = ap.parse_args()

    areas, secrets, objs, unknowns = parse_dump(args.dump)
    print(f"parsed: {len(areas)} parent areas, {len(secrets)} secret entries, "
          f"{len(objs)} distinct (screen,pos) objects, {len(unknowns)} unknown-liftable lines")

    if unknowns:
        # tasks.md 1.3: any liftable-attr map16 outside the engine id sets must
        # be triaged, not silently classified.
        uniq = sorted({(m16, attrs) for (_p, _s, _pos, m16, attrs) in unknowns})
        print(f"\nWARNING: unknown liftable map16 ids ({len(uniq)} unique):")
        for m16, attrs in uniq[:20]:
            print(f"  m16={m16:03x} attrs={['%02x' % a for a in attrs]}")

    # --- per-world-state-group presence/class registration ---
    #
    # The dump profiles split into two WORLD-STATE GROUPS whose within-seed
    # latching states must each be internally consistent, but which are NOT
    # required to agree with each other (inverted rewrites the overworld):
    #   NON-INVERTED (open/standard/retro): {0 vanilla, 1 +events, 4 pre-rescue}
    #   INVERTED:                            {2 inverted, 3 inverted+events}
    # An object is registered for a group iff it is present with IDENTICAL class
    # in ALL of that group's profiles (the real missable-check invariant, MED-5,
    # applied within the group). It then carries a world_state_filter so
    # placement + reachability (both honor the field) keep it inactive in the
    # other world_state — no strand. Objects consistent-and-equal in BOTH groups
    # are universal (no filter). This fixes the bug where requiring all-5 dropped
    # ~30 non-inverted-only + ~10 inverted-only legitimate objects.
    NONINV = (0, 1, 4)
    INV = (2, 3)
    NONINV_STATES = ["open", "standard", "retro"]
    INV_STATES = ["inverted"]

    def group_class(rec, group):
        """Return the shared (kind, content) if rec is present in every profile
        of `group` with one class AND one map16 id, else None."""
        bp = rec["by_prof"]
        if not all(p in bp for p in group):
            return None
        kinds = {bp[p][0] for p in group}
        if len(kinds) != 1:
            return None
        # map16 must also agree across the group. Same kind but different m16 is
        # a distinct tile masquerading as the same object — the pre-refactor rule
        # flagged it as a conflict; keep that guard (currently a no-op on the
        # data, but it fails closed if a future latch state re-tiles an object).
        if len({bp[p][1] for p in group}) != 1:
            return None
        # content from the group's base profile (first) — drives structural exclude.
        return (bp[group[0]][0], bp[group[0]][2])

    registrable = {}
    excluded = collections.Counter()
    excluded_detail = collections.defaultdict(list)
    for key, rec in sorted(objs.items()):
        scr, pos = key
        noni = group_class(rec, NONINV)
        invg = group_class(rec, INV)
        if noni is None and invg is None:
            excluded["absent-or-inconsistent-in-both-groups"] += 1
            excluded_detail["absent-or-inconsistent-in-both-groups"].append(
                (scr, pos, sorted(rec["by_prof"])))
            continue
        # Prefer the non-inverted registration (the common world_states); the
        # inverted-only case registers inverted. A class COLLISION across the
        # boundary (both groups consistent but different classes) registers the
        # non-inverted class with the non-inverted filter and drops the inverted
        # variant (rare — a handful of tiles the overlay repurposes).
        if noni is not None:
            kind, content = noni
            wsf = [] if (invg is not None and invg[0] == kind) else NONINV_STATES
            collide = invg is not None and invg[0] != kind
        else:
            kind, content = invg
            wsf = INV_STATES
            collide = False
        cls, axis, in_scope = KIND_INFO[kind]
        if not in_scope:
            excluded["out-of-scope-" + cls] += 1
            continue
        if content >= 0x80:
            excluded["structural-secret"] += 1
            excluded_detail["structural-secret"].append((scr, pos, kind, content))
            continue
        if collide:
            excluded["inverted-variant-dropped-at-collision"] += 1
        registrable[key] = (cls, axis, {"attrs": rec["attrs"], "content": content,
                                        "world_state_filter": wsf})

    # --- secrets reconciliation (tasks.md 1.4) ---
    obj_keys = set(objs.keys())
    unmatched = []
    structural = 0
    for scr, pos, data in secrets:
        if (scr, pos) in obj_keys:
            continue
        if data >= 0x80:
            structural += 1
            continue
        unmatched.append((scr, pos, data))
    print(f"\nsecrets reconciliation: {len(secrets)} entries; "
          f"{sum(1 for s in secrets if (s[0], s[1]) in obj_keys)} on consumable objects, "
          f"{structural} structural off-object, {len(unmatched)} item secrets on "
          f"non-consumable tiles (stay vanilla: dig-spot class)")
    for scr, pos, data in unmatched[:15]:
        print(f"  off-object item secret: screen {scr:02x} pos {pos:04x} data {data}")

    # --- counts ---
    print("\n=== registrable objects (present+identical in ALL profiles) ===")
    by_cls = collections.Counter()
    by_axis = collections.Counter()
    by_world = collections.Counter()
    by_glove = collections.Counter()
    per_screen = collections.Counter()
    with_secret = 0
    for (scr, pos), (cls, axis, rec) in registrable.items():
        by_cls[cls] += 1
        by_axis[axis] += 1
        by_world["DW" if scr >= 0x40 else "LW"] += 1
        per_screen[scr] += 1
        if rec["content"] >= 0:
            with_secret += 1
        if axis == "rock":
            by_glove[(cls, glove_level(cls, rec["attrs"]))] += 1
    by_wsf = collections.Counter()
    for (scr, pos), (cls, axis, rec) in registrable.items():
        wsf = rec["world_state_filter"]
        by_wsf["universal" if not wsf else "+".join(wsf)] += 1
    for cls in sorted(by_cls):
        print(f"  {cls:14s} {by_cls[cls]:5d}")
    print(f"  {'TOTAL':14s} {sum(by_cls.values()):5d}   "
          f"(grass axis {by_axis['grass']}, rock axis {by_axis['rock']}; "
          f"LW {by_world['LW']}, DW {by_world['DW']}; "
          f"{with_secret} carry a vanilla item secret)")
    print("\n  rock glove classification (class, glove level 1=Glove 2=Mitt):")
    for k in sorted(by_glove, key=str):
        print(f"    {k}: {by_glove[k]}")
    print("\n  world_state_filter split:")
    for k in sorted(by_wsf, key=str):
        print(f"    {k:24s} {by_wsf[k]}")
    print("\n  exclusions:")
    for k in sorted(excluded):
        print(f"    {k}: {excluded[k]}")
    for k in ("absent-or-inconsistent-in-both-groups",):
        for row in excluded_detail[k][:10]:
            print(f"      {k}: {row}")

    busiest = per_screen.most_common(5)
    print("\n  busiest screens: " + ", ".join(f"{s:02x}:{n}" for s, n in busiest))

    # --- registry emission (tasks.md 2.1/2.2) ---
    if args.emit:
        emit_registry(args.emit, registrable, areas)
    else:
        total = len(registrable)
        print(f"\n(measurement only — pass --emit to write terrain.gen.yaml; "
              f"ids would run {TERRAIN_BASE_ID}..{TERRAIN_BASE_ID + total - 1})")
    return 0


def load_overrides(path):
    """Parse the committed reviewed overrides (terrain_logic_overrides.yaml).

    Returns screen -> {confirmed, region, gate, ranges:[(lo,hi,region,gate)]}.
    Uses PyYAML when available; the file is small and optional."""
    if not os.path.isfile(path):
        return {}
    import yaml  # PyYAML is a repo tooling dependency (requirements.txt)
    with open(path, encoding="utf-8") as f:
        doc = yaml.safe_load(f) or {}
    out = {}
    for e in doc.get("overrides") or []:
        scr = int(e["screen"], 0) if isinstance(e["screen"], str) else int(e["screen"])
        out[scr] = {
            "confirmed": e.get("confirmed"),
            "region": e.get("region"),
            "gate": e.get("gate"),
            "ranges": [(int(r["lo"], 0) if isinstance(r["lo"], str) else int(r["lo"]),
                        int(r["hi"], 0) if isinstance(r["hi"], str) else int(r["hi"]),
                        r.get("region"), r.get("gate"))
                       for r in (e.get("ranges") or [])],
        }
    return out


def emit_registry(out_path, registrable, areas):
    """Write assets/rando/terrain.gen.yaml (gitignored local registry).

    Row shape mirrors pots.gen.yaml: rando_logic_gen.load_terrain() merges the
    rows into the location registry + logic binding and emits terrain_lookup.h
    (whose FNV digest over (screen, pos, id) is the activation-guard value —
    computed there, not here, exactly like the pot digest). IDs are the
    (screen, pos) sort rank from TERRAIN_BASE_ID — any object-set change
    renumbers, which is a kGeneratorVersion bump + registry-digest change.
    """
    # Hard-fail on unbound object-bearing screens (fail-closed, mirroring the
    # enemy table's contract).
    missing = sorted({scr for (scr, _pos) in registrable} - set(SCREEN_REGIONS))
    if missing:
        sys.exit(f"gen_terrain_tables: object-bearing screens with no "
                 f"SCREEN_REGIONS binding: {['%02x' % s for s in missing]} — "
                 f"bind + review before emitting.")

    overrides = load_overrides(os.path.join(
        os.path.dirname(os.path.abspath(out_path)), "terrain_logic_overrides.yaml"))

    rows = []
    axis_counts = collections.Counter()
    for rank, ((scr, pos), (cls, axis, rec)) in enumerate(sorted(registrable.items())):
        region, source, split = SCREEN_REGIONS[scr]
        label, tool_gate = CLASS_GATES[cls]
        world_gate = "TerrainWorldGateDW()" if scr >= 0x40 else "TerrainWorldGateLW()"
        extra_gate = None
        ov = overrides.get(scr)
        if ov:
            if ov.get("region"):
                region = ov["region"]
            extra_gate = ov.get("gate")
            for lo, hi, r_region, r_gate in ov["ranges"]:
                if lo <= pos <= hi:
                    if r_region:
                        region = r_region
                    if r_gate:
                        extra_gate = r_gate
                    break
        can_reach = f"{world_gate} AND {tool_gate}" if tool_gate else world_gate
        if extra_gate:
            can_reach = f"({can_reach}) AND ({extra_gate})"
        rows.append({
            "id": TERRAIN_BASE_ID + rank,
            "name": f"{region} {label} S{scr:02X} P{pos:04X}",
            "screen": scr,
            "pos": pos,
            "cls": cls,
            "axis": axis,
            "secret": rec["content"],   # vanilla secret code, -1 = none (provenance)
            "region": region,
            "can_reach": can_reach,
            "world_state_filter": rec["world_state_filter"],
        })
        axis_counts[axis] += 1

    # A screen with a reviewed override entry (or explicit `confirmed:` verdict)
    # leaves the worklist; the rest of the derived/split set remains open.
    review = [(s, r, src, note) for s, (r, src, note) in sorted(SCREEN_REGIONS.items())
              if (src == "derived" or note) and s not in overrides]
    lines = [
        "_generated_by: assets/scripts/gen_terrain_tables.py (do not hand-edit)",
        "# add-rando-grass-rock-shuffle terrain registry. Gitignored local",
        "# artifact (mirrored by setup_worktree.py with the dump as a complete",
        "# set). vanilla_item is Nothing for every row: suppressed vanilla",
        "# secrets are NOT pool items (design D8), and the ITEM_Nothing pin in",
        "# rando_placement.c is pot-type-scoped so these rows are never pinned.",
        f"base_id: {TERRAIN_BASE_ID}",
        "axis_counts:",
        f"  grass: {axis_counts['grass']}",
        f"  rock: {axis_counts['rock']}",
        "# Region-binding review worklist (design D6): 'derived' bindings and",
        "# split-screen notes MUST be resolved (terrain_logic_overrides.yaml or",
        "# a reviewed no-override verdict) before Phase 3 activates the tiers.",
        "region_review:",
    ]
    for s, r, src, note in review:
        lines.append(f"- {{screen: 0x{s:02X}, region: {r}, source: {src}"
                     + (f", note: '{note}'" if note else "") + "}")
    lines.append("locations:")
    for r in rows:
        # world_state_filter: emitted only when the object is NOT universal (keeps
        # the YAML compact). '[]' / absent means active in every world_state.
        wsf = r["world_state_filter"]
        wsf_field = (f", world_state_filter: [{', '.join(wsf)}]") if wsf else ""
        lines.append(
            f"- {{id: {r['id']}, name: '{r['name']}', screen: 0x{r['screen']:02X}, "
            f"pos: 0x{r['pos']:04X}, cls: {r['cls']}, axis: {r['axis']}, "
            f"secret: {r['secret']}, region: {r['region']}, "
            f"can_reach: '{r['can_reach']}'{wsf_field}}}")
    with open(out_path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    print(f"\nemitted {len(rows)} terrain locations "
          f"(ids {TERRAIN_BASE_ID}..{TERRAIN_BASE_ID + len(rows) - 1}; "
          f"grass {axis_counts['grass']}, rock {axis_counts['rock']}) -> {out_path}")
    print(f"region-review worklist: {len(review)} screens "
          f"({sum(1 for _s, _r, src, _n in review if src == 'derived')} derived, "
          f"{sum(1 for _s, _r, _src, n in review if n)} split-flagged)")
    # HARD-FAIL on any unresolved derived/split screen (design D6 / spec:
    # "the generator hard-fails when a screen is flagged split-but-unreviewed").
    # A `derived` binding or a split-screen note that has no
    # terrain_logic_overrides.yaml entry (region/gate/ranges or an explicit
    # `confirmed:` verdict) ships its default whole-screen region silently — an
    # under-gate softlock vector on any FUTURE object surfacing there. Every
    # current screen is reviewed, so this stays green now and guards the future.
    if review:
        screens = ", ".join(f"0x{s:02X}" for s, _r, _src, _n in review)
        sys.exit(f"gen_terrain_tables: {len(review)} derived/split screen(s) "
                 f"lack a reviewed terrain_logic_overrides.yaml entry: "
                 f"{screens}. Add a region/gate/ranges override or an explicit "
                 f"`confirmed:` verdict before emitting (over-gate when unsure).")


if __name__ == "__main__":
    sys.exit(main())
