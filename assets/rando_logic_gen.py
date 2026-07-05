#!/usr/bin/env python3
"""rando_logic_gen.py — Phase A logic codegen.

Per `randomizer-logic / Logic YAML schema and worked examples` and tasks.md
§3.5. Reads:

  - assets/rando/op_registry.yaml          (task 3.4 — op-code IDs)
  - assets/rando/item_registry.yaml        (task 3.2 — item IDs)
  - assets/rando/location_registry.yaml    (task 3.1 — location IDs)
  - assets/rando/logic.schema.yaml         (task 3.5a — schema)
  - assets/rando/macros.yaml               (task 3.4a — named macros) [optional]
  - assets/rando/logic.yaml                (task 3.3 — graph)          [optional]

Emits:

  - src/rando/location_ids.h               (#define LOC_<name> <id>)
  - src/rando/item_ids.h                   (#define ITEM_<name> <id>)
  - src/rando/logic_data.c                 (LocationDef[], RegionDef[],
                                             EdgeDef[], predicate streams)

Discipline (per CLAUDE.md claim-grounding and randomizer-core / Byte-order pin):
  - Deterministic output: iteration uses sorted IDs.
  - No `rand`, no wall-clock, no float.
  - All multi-byte integers in emitted byte streams are little-endian.
  - Phase B ops (OP_TRICK / OP_DIFFICULTY_AT_LEAST / OP_GLITCH_LEVEL_AT_LEAST)
    rejected in Phase A logic.yaml by the well-formedness pass (task 3.10).

Bytecode format (recursively-evaluable, no length prefixes — recursion depth
is bounded by the inline-complexity check):

  HAS_ITEM       -> <op:u8> <item_id:u16_le>
  HAS_AMOUNT     -> <op:u8> <item_id:u16_le> <n:u8>
  HAS_ANY_OF     -> <op:u8> <count:u8> <id:u16_le>×count
  HAS_ANY_COUNT  -> <op:u8> <count:u8> <id:u16_le>×count <n:u8>
  WORLDSTATE_EQ  -> <op:u8> <state:u8>
  GOAL_EQ        -> <op:u8> <goal:u8>
  GOAL_REQUIRES_DUNGEON -> <op:u8> <dungeon:u8>
  DUNGEON_CLEARED -> <op:u8> <dungeon:u8>
  REGION_REACHABLE -> <op:u8> <region_id:u16_le>
  HAS_PRIZE      -> <op:u8> <prize:u8>
  MEDALLION_OPENS -> <op:u8> <entrance:u8>
  ITEM_IS        -> <op:u8> <item_id:u16_le>
  NOT            -> <op:u8> <child>
  AND            -> <op:u8> <count:u8> <child>×count
  OR             -> <op:u8> <count:u8> <child>×count

  Vacuous AND (count=0) is TRUE; vacuous OR (count=0) is FALSE. The DSL
  literals TRUE() / FALSE() compile to those forms.

The well-formedness pass (task 3.10) asserts well-formedness conditions
listed in logic.schema.yaml `well_formedness:`.
"""

from __future__ import annotations

import argparse
import dataclasses
import os
import re
import struct
import sys
from pathlib import Path

import yaml  # PyYAML; required by CLAUDE.md (`pip install -r requirements.txt`)

# Generated chest table + ALTTPR chest-name data (tasks.md S6.3).
# See assets/chest_data.py for the source-of-truth snapshot and provenance.
sys.path.insert(0, str(Path(__file__).resolve().parent))
import chest_data  # noqa: E402  -- after sys.path mutation


# ---------------------------------------------------------------------------
# Atomic output write
# ---------------------------------------------------------------------------
def atomic_write_text(path: Path, text: str) -> None:
    """Write `text` to `path` ATOMICALLY: write a sibling temp file, flush+fsync,
    then os.replace() (an atomic rename on the same filesystem).

    Why this matters: the Makefile glob-builds these codegen outputs as ordinary
    sources, and under `make -j` a compile can read an output file. Path.write_text
    truncates-then-writes in place, so it leaves a window where the file is empty
    or partial — and a concurrent `gcc -c logic_data.c` (or a TU #including it)
    that reads during that window produces a SMALLER binary with a TRUNCATED logic
    graph (symptom: most corpus seeds fail with wrong placement digests; it is
    timing-dependent, so it masquerades as a real regression). With the atomic
    rename a reader sees EITHER the old complete file or the new complete file —
    never a mid-write one — so that race is structurally impossible. (Also defends
    against a double-codegen race: each writer renames a complete file, last wins.)

    newline is left at the default (None) to byte-for-byte match the previous
    Path.write_text(... encoding="utf-8") behavior on every platform (LF on POSIX,
    CRLF on Windows) — so emitted bytes, and thus digests, are unchanged.
    """
    if path.exists():
        try:
            if path.read_text(encoding="utf-8") == text:
                return
        except UnicodeDecodeError:
            pass
    tmp = path.with_name(path.name + ".tmp")
    with open(tmp, "w", encoding="utf-8") as f:
        f.write(text)
        f.flush()
        os.fsync(f.fileno())
    os.replace(tmp, path)


# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------
REPO = Path(__file__).resolve().parent.parent
RANDO_ASSETS = REPO / "assets" / "rando"
RANDO_SRC = REPO / "src" / "rando"
DUNGEON_SUFFIXES = [
    "HyruleCastleEscape", "EasternPalace", "DesertPalace", "TowerOfHera",
    "HyruleCastleTower", "PalaceOfDarkness", "SwampPalace", "SkullWoods",
    "ThievesTown", "IcePalace", "MiseryMire", "TurtleRock", "GanonsTower",
]


# ---------------------------------------------------------------------------
# Registry loading
# ---------------------------------------------------------------------------
@dataclasses.dataclass
class OpDef:
    id: int
    name: str
    phase: str
    operands: list
    contexts: list


@dataclasses.dataclass
class ItemDef:
    id: int
    name: str
    category: str
    max_count: int = 1
    dispatch: str = ""


@dataclasses.dataclass
class LocationDef:
    id: int
    name: str
    region: str
    type: str
    vanilla_item: str
    world_state_filter: list = dataclasses.field(default_factory=list)
    dungeon_item_class: str | None = None
    can_reach: str = "TRUE()"
    can_place: str = "TRUE()"
    always_allow: str = "FALSE()"
    source: str = ""


@dataclasses.dataclass
class MacroDef:
    name: str
    parameters: list
    body: str
    source: str = ""


@dataclasses.dataclass
class RegionDef:
    id: str
    name: str
    dungeon: str | None = None
    parent: str | None = None
    world_state_filter: list = dataclasses.field(default_factory=list)
    source: str = ""


@dataclasses.dataclass
class EdgeDef:
    from_: str
    to: str
    predicate: str
    one_way: bool = False
    source: str = ""


def load_yaml(path: Path) -> dict:
    with open(path, "r", encoding="utf-8") as f:
        return yaml.safe_load(f)


def load_ops(path: Path) -> dict[str, OpDef]:
    doc = load_yaml(path)
    out = {}
    for raw in doc.get("ops", []):
        op = OpDef(
            id=raw["id"],
            name=raw["name"],
            phase=raw.get("phase", "A"),
            operands=raw.get("operands", []),
            contexts=raw.get("contexts", []),
        )
        # Strip the "OP_" prefix for DSL parser keys (DSL uses bare op names like HAS_ITEM)
        bare = op.name[3:] if op.name.startswith("OP_") else op.name
        out[bare] = op
    return out


def load_items(path: Path) -> dict[str, ItemDef]:
    doc = load_yaml(path)
    out = {}
    for raw in doc.get("items", []):
        it = ItemDef(
            id=raw["id"],
            name=raw["name"],
            category=raw.get("category", ""),
            max_count=raw.get("max_count", 1),
            dispatch=raw.get("dispatch", ""),
        )
        out[it.name] = it
    return out


def load_locations(path: Path) -> dict[str, LocationDef]:
    doc = load_yaml(path)
    out = {}
    for raw in doc.get("locations", []):
        loc = LocationDef(
            id=raw["id"],
            name=raw["name"],
            region=raw.get("region", ""),
            type=raw.get("type", "Chest"),
            vanilla_item=raw.get("vanilla_item", ""),
            world_state_filter=raw.get("world_state_filter", []),
            dungeon_item_class=raw.get("dungeon_item_class"),
            source=raw.get("source", ""),
        )
        # Map location.name -> a canonical id token usable in #define LOC_<id>.
        # We use the YAML location name lowered + non-alphanumerics replaced with _.
        out[loc.name] = loc
    return out


def _pot_key_info(room, pot_id, pot_room_wrap, pot_key_wrap):
    """Return (item, full, dungeon) pot-key requirements for one pot.

    A KEY pot uses its EXACT per-pot dungeon depth (and its own wild `full` when
    the room has no entry); a loot/empty pot uses the room's wild `full` and
    room-max dungeon.
    """
    room_w = pot_room_wrap.get(room)
    key_w = pot_key_wrap.get(pot_id)
    item = (room_w[0] if room_w else None) or (key_w[1] if key_w else None)
    if item is None:
        return None, None, None
    # WILD: room-max `full` for every pot; the key pot's own `full` only backfills
    # when the room has no wild entry (keeps clean rooms byte-identical under wild).
    full = room_w[1] if room_w else None
    if full is None and key_w is not None:
        full = key_w[2]
    # DUNGEON: key pot -> exact per-pot depth; loot/empty -> room-max.
    dungeon = key_w[3] if key_w is not None else (room_w[2] if room_w else None)
    return item, full, dungeon


def _suppress_base_key_terms_under_dungeon(expr, item, dungeon):
    """Let generated DUNGEON pot-key depth replace inherited same-key gates.

    Some pot rows inherit a coarse room predicate such as HAS_AMOUNT(SmallKey_GT,
    4), but the pot-key-depth table knows the exact in-context requirement for
    this pot/room. Wrap same-dungeon small-key predicates so they are preserved
    outside DUNGEON pot-key mode and suppressed inside it; the generated
    POT_KEYS_DUNGEON tail then supplies the exact requirement, including zero.
    """
    if item is None or dungeon is None:
        return expr

    item_pat = re.escape(item)

    def repl(match):
        return f"(POT_KEYS_DUNGEON() OR {match.group(0)})"

    expr = re.sub(rf"HAS_AMOUNT\(\s*{item_pat}\s*,\s*\d+\s*\)", repl, expr)
    expr = re.sub(rf"HAS_ITEM\(\s*{item_pat}\s*\)", repl, expr)
    return expr


def _strip_door_vanilla_key_terms(expr: str, dungeon: int | None) -> str:
    """Door-pot bridge predicates must not inherit vanilla same-dungeon key locks.

    The bridge asks the door oracle whether the door-shuffled region containing
    the pot is reachable. Any vanilla SmallKey/BigKey terms from the pot room's
    original predicate would double-count or contradict that oracle, so strip
    only terms for this same dungeon before compiling the bridge predicate.
    """
    if dungeon is None or dungeon < 0 or dungeon >= len(DUNGEON_SUFFIXES):
        return expr
    suffix = DUNGEON_SUFFIXES[dungeon]
    for item in (f"SmallKey_{suffix}", f"BigKey_{suffix}"):
        item_pat = re.escape(item)
        expr = re.sub(rf"HAS_AMOUNT\(\s*{item_pat}\s*,\s*\d+\s*\)", "TRUE()", expr)
        expr = re.sub(rf"HAS_ITEM\(\s*{item_pat}\s*\)", "TRUE()", expr)
    return expr


def _pot_key_terms_for(item, full, dungeon):
    """Build the trailing pot-key gate for one generated key-depth row."""
    t = ""
    if full is not None and full > 0:
        t += f" AND (NOT POT_KEYS_WILD() OR HAS_AMOUNT({item}, {full}))"
    if dungeon is not None and dungeon > 0:
        t += f" AND (NOT POT_KEYS_DUNGEON() OR HAS_AMOUNT({item}, {dungeon}))"
    return t


def _pot_min_tier(kind: str | None, vanilla_item: str) -> int:
    if kind == "empty" or vanilla_item == "Nothing":
        return 3  # All
    if vanilla_item.startswith("SmallKey_"):
        return 1  # Keys
    return 2      # Contents


def _fnv32_u8(h: int, b: int) -> int:
    h ^= b & 0xFF
    return (h * 0x01000193) & 0xFFFFFFFF


def _fnv32_u16(h: int, v: int) -> int:
    h = _fnv32_u8(h, v)
    return _fnv32_u8(h, v >> 8)


def _fnv32_u32(h: int, v: int) -> int:
    h = _fnv32_u16(h, v)
    return _fnv32_u16(h, v >> 16)


def _door_pot_bridge_digest(rows: list[dict]) -> int:
    h = 0x811C9DC5
    for r in rows:
        h = _fnv32_u16(h, r["loc_id"])
        h = _fnv32_u8(h, r["dungeon"])
        h = _fnv32_u8(h, r["min_tier"])
        h = _fnv32_u8(h, r["flags"])
        h = _fnv32_u16(h, r["drop_index"])
        h = _fnv32_u8(h, len(r["regions"]))
        for region in r["regions"]:
            h = _fnv32_u16(h, region)
        h = _fnv32_u32(h, len(r["pred"]))
        for b in r["pred"]:
            h = _fnv32_u8(h, b)
    return h


def _pot_registry_digest(rows: list[tuple[int, int, int]]) -> int:
    if not rows:
        return 0
    h = 0x811C9DC5
    for room, pos4, locid in sorted(rows, key=lambda r: (r[0], r[1], r[2])):
        h = _fnv32_u16(h, room)
        h = _fnv32_u16(h, pos4)
        h = _fnv32_u16(h, locid)
    return h


def load_pots(path: Path, logic_regions: dict[str, RegionDef] | None = None):
    """Load assets/rando/pots.gen.yaml (local registry from gen_pot_tables.py).

    Returns (name -> LocationDef, [(room, pos4, loc_id)], bridge_source_rows).
    Each pot LocationDef carries id/name/region/type=Pot/vanilla_item AND
    can_reach (D8 inheritance), so it feeds BOTH the location registry
    (location_ids.h / kRandoLocations) and the logic binding (region_id +
    can_reach) - it is merged into both `locations` and `logic_loc_preds` in
    main(). pots.gen.yaml is a gitignored local artifact, so public/assetless
    builds emit no pot locations; generation fails closed if a user enables a
    pot tier without rebuilding from local pot codegen."""
    if not path.exists():
        print(f"WARNING: {path} not found - emitting NO pot locations. "
              f"Run assets/scripts/run_rando_local_checks.py with ROM assets "
              f"to enable pot shuffle in this build.", file=sys.stderr)
        return {}, [], []
    depth_tbl = RANDO_ASSETS / "pot_key_depth.gen.yaml"
    if not depth_tbl.exists():
        raise RuntimeError(
            f"{path} exists but {depth_tbl} is missing. This partial local pot "
            f"artifact set would emit pot locations without POT_KEYS_WILD/"
            f"POT_KEYS_DUNGEON key-depth wraps or door+pot bridge rows. Run "
            f"assets/scripts/run_rando_local_checks.py --prepare-only with a "
            f"fresh binary, or remove {path} for an assetless build."
        )
    doc = load_yaml(path)
    # add-rando-pot-sanity task #25: small-key requirements pot_shuffle adds once a
    # dungeon's pot keys are first-class checks. A pot behind key doors must require
    # those keys or a progression item placed there strands. pots-off keeps both
    # POT_KEYS_WILD and POT_KEYS_DUNGEON false, so the wrap collapses to the vanilla
    # predicate (byte-identical). pot_rooms gives every pot in the room a WILD `full`
    # term and loot/empty pots a DUNGEON room-max term; a KEY pot uses its EXACT
    # per-pot dungeon depth (pot_keys), plus its own `full` when the room has no wild
    # entry (the floor-bit Waterway orphan). See gen_pot_key_depth.py.
    pot_room_wrap = {}     # room (int) -> (item, full|None, dungeon_depth|None)
    pot_key_wrap = {}      # pot id (int) -> (dungeon, item, full|None, dungeon_depth, door_region, drop_index)
    door_room_wrap = {}    # room (int) -> (dungeon, [door_region ids])
    dt = load_yaml(depth_tbl)
    if int(dt.get("format_version", 0) or 0) < 3:
        sys.exit(f"{depth_tbl}: stale format_version; regenerate with "
                 "assets/scripts/gen_pot_key_depth.py so door+pot bridge rows "
                 "and drop-key indices are available.")
    door_pot_room_rows = dt.get("door_pot_rooms")
    if not door_pot_room_rows:
        sys.exit(f"{depth_tbl}: missing door_pot_rooms bridge rows; regenerate "
                 "with assets/scripts/gen_pot_key_depth.py after "
                 "--dump-key-depth.")
    for r in dt.get("pot_rooms", []) or []:
        pot_room_wrap[int(r["room"])] = (
            r["item"],
            int(r["full"]) if "full" in r else None,
            int(r["dungeon"]) if "dungeon" in r else None,
        )
    for r in dt.get("pot_keys", []) or []:
        if "door_region" in r and "depth" not in r:
            sys.exit(f"{depth_tbl}: stale pot_keys row id={r.get('id')} uses "
                     "`dungeon` as a depth; regenerate with "
                     "assets/scripts/gen_pot_key_depth.py")
        pot_key_wrap[int(r["id"])] = (
            int(r["dungeon"]),
            r["item"],
            int(r["full"]) if "full" in r else None,
            int(r["depth"]) if "depth" in r else int(r["dungeon"]),
            int(r["door_region"]),
            int(r["drop_index"]),
        )
    for r in door_pot_room_rows or []:
        door_room_wrap[int(r["room"])] = (
            int(r["dungeon"]),
            [int(x) for x in r.get("regions", []) or []],
        )
    out, rows, bridge_rows = {}, [], []
    for p in doc.get("pots", []) or []:
        # add-rando-pot-sanity: empty pots carry the ITEM_Nothing filler as their
        # vanilla item. The generated pots.gen.yaml records empties as
        # `kind: empty` + `vanilla_item: null`; map that to "Nothing" here so the
        # generated kRandoLocations row has vanilla_item_id == ITEM_Nothing (148).
        # That id is the unambiguous empty-pot signal the placer keys on
        # (`pot_active`, the §3b ITEM_Nothing pre-pass) — id 0 is ProgressiveSword,
        # so a null→0 mapping would be indistinguishable from a real item.
        vanilla_item = p.get("vanilla_item") or ""
        if (p.get("kind") == "empty") or not vanilla_item:
            vanilla_item = "Nothing"
        base_can_reach = p.get("can_reach") or "TRUE()"
        can_reach = base_can_reach
        item, full, dungeon = _pot_key_info(
            int(p["room"]), int(p["id"]), pot_room_wrap, pot_key_wrap)
        can_reach = _suppress_base_key_terms_under_dungeon(can_reach, item, dungeon)
        terms = _pot_key_terms_for(item, full, dungeon) if item is not None else ""
        if terms:
            can_reach = f"({can_reach}){terms}"
        loc = LocationDef(
            id=p["id"], name=p["name"], region=p.get("region", ""), type="Pot",
            vanilla_item=vanilla_item,
            can_reach=can_reach,
            can_place="TRUE()", always_allow="FALSE()", source="gen_pot_tables.py",
        )
        out[loc.name] = loc
        rows.append((int(p["room"]), int(p["pos4"]), int(p["id"])))
        min_tier = _pot_min_tier(p.get("kind"), vanilla_item)
        flags = 0
        if vanilla_item != "Nothing":
            flags |= 0x01  # kDoorPot_KeySource
        if vanilla_item.startswith("SmallKey_"):
            flags |= 0x02  # kDoorPot_KeyPot
        if vanilla_item == "Nothing":
            flags |= 0x04  # kDoorPot_Empty

        dungeon = 0xFF
        regions = []
        drop_index = 0xFFFF
        if flags & 0x02:
            kw = pot_key_wrap.get(int(p["id"]))
            if kw is None and depth_tbl.exists():
                sys.exit(f"pot bridge: key pot id={p['id']} has no pot_keys row in {depth_tbl}")
            if kw is not None:
                dungeon, _item, _full, _depth, door_region, drop_index = kw
                regions = [door_region]
        else:
            rw = door_room_wrap.get(int(p["room"]))
            if rw is not None:
                dungeon, regions = rw
        if regions and dungeon != 0xFF:
            bridge_can_reach = _strip_door_vanilla_key_terms(base_can_reach, dungeon)
            bridge_rows.append({
                "loc_id": int(p["id"]),
                "name": p["name"],
                "dungeon": dungeon,
                "regions": sorted(set(regions)),
                "base_can_reach": bridge_can_reach,
                "min_tier": min_tier,
                "flags": flags,
                "drop_index": drop_index,
            })
    return out, rows, bridge_rows


def load_cave_source_predicates(path: Path) -> list[tuple[str, str]]:
    """Return entrance-shuffle cave source predicates in registry order."""
    if not path.exists():
        return []
    doc = load_yaml(path) or {}
    rows = []
    for interior in doc.get("interiors", []) or []:
        rows.append((
            str(interior.get("interior_id", "")),
            str(interior.get("can_enter") or "TRUE()"),
        ))
    return rows


def build_door_pot_bridge(bridge_sources: list[dict], compile_src) -> tuple[list[dict], int]:
    rows: list[dict] = []
    for src in sorted(bridge_sources, key=lambda r: r["loc_id"]):
        pred = compile_src(src["base_can_reach"],
                           f"pot bridge {src['loc_id']} {src['name']}")
        row = dict(src)
        row["pred"] = pred
        rows.append(row)
    return rows, _door_pot_bridge_digest(rows) if rows else 0


def load_macros(path: Path | None) -> dict[str, MacroDef]:
    if path is None or not path.exists():
        return {}
    doc = load_yaml(path)
    out = {}
    for raw in doc.get("macros", []):
        m = MacroDef(
            name=raw["name"],
            parameters=raw.get("parameters", []) or [],
            body=raw["body"],
            source=raw.get("source", ""),
        )
        out[m.name] = m
    return out


def _merge_logic_doc(doc, regions, edges, loc_preds, macros, source_file,
                     world_state_overrides=None, world_state_id=None,
                     world_state_edges=None):
    """Merge a single parsed logic YAML doc into the cumulative dicts.

    Per-file conflict policy: regions / macros are keyed by id/name; a later
    file silently overrides an earlier one (the codegen reports duplicates as
    warnings via well-formedness). Edges are appended. Location predicates
    are keyed by id; later wins.

    Phase B Slice 2 (add-rando-inverted-world-state): when ``world_state_id``
    is not None, location predicates and edges from this file are stored in
    ``world_state_overrides[loc_id][world_state_id]`` and
    ``world_state_edges[world_state_id]`` instead of the base maps. The base
    ``loc_preds`` / ``edges`` only receive entries from files with
    ``world_state_id is None`` (i.e., the Standard/Open/Retro path).
    Regions and macros still merge into the global maps regardless of
    world_state (they're identity-shared across world states by design).
    """
    if doc is None:
        return
    for raw in doc.get("regions", []) or []:
        r = RegionDef(
            id=raw["id"],
            name=raw.get("name", raw["id"]),
            dungeon=raw.get("dungeon"),
            parent=raw.get("parent"),
            world_state_filter=raw.get("world_state_filter", []),
            source=raw.get("source", ""),
        )
        # Don't let a world-state-specific YAML overwrite a base region
        # with a "stub" entry that would lose the parent / dungeon
        # binding. Only insert if not already present.
        if world_state_id is None or r.id not in regions:
            regions[r.id] = r
    for raw in doc.get("edges", []) or []:
        edge = EdgeDef(
            from_=raw["from"],
            to=raw["to"],
            predicate=raw.get("predicate", "TRUE()"),
            one_way=raw.get("one_way", False),
            source=raw.get("source", ""),
        )
        if world_state_id is None:
            edges.append(edge)
        else:
            world_state_edges.setdefault(world_state_id, []).append(edge)
    for raw in doc.get("locations", []) or []:
        loc = LocationDef(
            id=0,  # numeric id comes from registry
            name=raw.get("name", raw["id"]),
            region=raw.get("region", ""),
            type=raw.get("type", "Chest"),
            vanilla_item=raw.get("vanilla_item", ""),
            can_reach=raw.get("can_reach", "TRUE()"),
            can_place=raw.get("can_place", "TRUE()"),
            always_allow=raw.get("always_allow", "FALSE()"),
            source=raw.get("source", ""),
        )
        if world_state_id is None:
            loc_preds[raw["id"]] = loc
        else:
            world_state_overrides.setdefault(raw["id"], {})[world_state_id] = loc
    for raw in doc.get("macros", []) or []:
        macros[raw["name"]] = MacroDef(
            name=raw["name"],
            parameters=raw.get("parameters", []) or [],
            body=raw["body"],
            source=raw.get("source", ""),
        )


# Phase B Slice 2 — world-state numeric IDs matching the C-side enum
# (src/rando/rando_settings.h `kWorldState_*`). Used to bucket YAML files
# from `logic_parts/inverted/**` into the Inverted override map.
kWorldState_Open = 0
kWorldState_Standard = 1
kWorldState_Inverted = 2
kWorldState_Retro = 3


def _world_state_id_for_path(path: Path) -> int | None:
    """Derive the world_state override key from a logic_parts file path.

    `logic_parts/inverted/**/*.yaml` → kWorldState_Inverted
    `logic_parts/*.yaml`              → None (base / Standard / shared across
                                              all world states)

    Future world-state-specific subdirs (e.g., `logic_parts/retro/`) plug
    into the same convention.
    """
    parts = path.parts
    if "inverted" in parts:
        return kWorldState_Inverted
    # Future: if "retro" in parts: return kWorldState_Retro
    return None


def load_logic(path: Path | None):
    """Load the optional logic.yaml plus every file under
    `assets/rando/logic_parts/**/*.yaml` (sorted for deterministic merge order).
    Returns (regions, edges, location_predicates, macros, world_state_overrides, world_state_edges).

    Files later in sort order override earlier files for regions / locations /
    macros at the BASE level. Phase B Slice 2: files under
    `logic_parts/inverted/**` are routed to the `world_state_overrides`
    map keyed by location id → {world_state_id: LocationDef}. The base
    `loc_preds` only holds Standard/Open/Retro predicates. The runtime
    consults the override map when `settings.world_state == Inverted`.

    Cross-region edges from Inverted YAML go into `world_state_edges` and
    are emitted as Inverted-specific edges in `logic_data.c`. The base
    `edges` list is unchanged.
    """
    regions: dict[str, RegionDef] = {}
    edges: list[EdgeDef] = []
    loc_preds: dict[str, LocationDef] = {}
    macros: dict[str, MacroDef] = {}
    # location_id → {world_state_id: LocationDef}
    world_state_overrides: dict[str, dict[int, LocationDef]] = {}
    # world_state_id → list[EdgeDef]
    world_state_edges: dict[int, list[EdgeDef]] = {}

    # Load the main logic.yaml first (lowest priority).
    if path is not None and path.exists():
        _merge_logic_doc(
            load_yaml(path), regions, edges, loc_preds, macros, str(path),
            world_state_overrides=world_state_overrides,
            world_state_id=None,
            world_state_edges=world_state_edges,
        )

    # Load every file under assets/rando/logic_parts/**/*.yaml (sorted).
    # Phase B Slice 2: recurse so `inverted/**/*.yaml` is picked up; route
    # those entries to the world_state_overrides / world_state_edges maps
    # via `_world_state_id_for_path`.
    parts_dir = RANDO_ASSETS / "logic_parts"
    if parts_dir.exists() and parts_dir.is_dir():
        for part in sorted(parts_dir.rglob("*.yaml")):
            ws_id = _world_state_id_for_path(part)
            _merge_logic_doc(
                load_yaml(part), regions, edges, loc_preds, macros, str(part),
                world_state_overrides=world_state_overrides,
                world_state_id=ws_id,
                world_state_edges=world_state_edges,
            )

    _apply_pot_key_terms(loc_preds, world_state_overrides)
    return regions, edges, loc_preds, macros, world_state_overrides, world_state_edges


def _apply_pot_key_terms(loc_preds, world_state_overrides):
    """add-rando-pot-sanity task #25: wrap each pot-bearing dungeon location's
    can_reach with the small-key requirements pot_shuffle adds — the WILD
    worst-case (`full`, held externally) and the in-context DUNGEON worst-case
    (`dungeon`). The dungeon term matters because the vanilla `cur` assumes the
    pot keys drop FREE; once they are items the requirement RISES to the
    conservative all-orders depth.
    pots-off leaves both POT_KEYS_WILD and POT_KEYS_DUNGEON false, so the wrap
    collapses to the vanilla predicate (byte-identical).

    Driven by the generated assets/rando/pot_key_depth.gen.yaml (from
    `./zelda3 --dump-key-depth`); applies to the base Standard/Open/Retro
    predicate AND every world-state (Inverted) override of the same location id
    (the internal door depth is world-state-independent)."""
    table_path = RANDO_ASSETS / "pot_key_depth.gen.yaml"
    if not table_path.exists():
        # This gitignored local artifact is absent in public/assetless builds,
        # which also omit pots.gen.yaml and skip pot-shuffle corpus rows. A
        # partial local build with pots.gen.yaml present fails closed in
        # load_pots() below so pot-shuffle binaries cannot silently miss these
        # wraps.
        print(f"WARNING: {table_path} MISSING — pot-key depth wraps NOT applied; "
              f"pot_shuffle key-mode seeds may strand. Regenerate with "
              f"gen_pot_key_depth.py.", file=sys.stderr)
        return
    doc = load_yaml(table_path)
    missing = []
    for row in doc.get("locations", []):
        name, item = row["name"], row["item"]
        full = int(row["full"]) if "full" in row else None
        dungeon = int(row["dungeon"]) if "dungeon" in row else None
        tail = _pot_key_terms_for(item, full, dungeon)
        if not tail and dungeon is None:
            continue
        applied = False
        if name in loc_preds:
            loc_preds[name].can_reach = _suppress_base_key_terms_under_dungeon(
                loc_preds[name].can_reach, item, dungeon)
            if tail:
                loc_preds[name].can_reach = f"({loc_preds[name].can_reach}){tail}"
            applied = True
        for ld in world_state_overrides.get(name, {}).values():
            ld.can_reach = _suppress_base_key_terms_under_dungeon(
                ld.can_reach, item, dungeon)
            if tail:
                ld.can_reach = f"({ld.can_reach}){tail}"
            applied = True
        if not applied:
            missing.append(name)
    if missing:
        sample = ", ".join(repr(n) for n in missing[:8])
        if len(missing) > 8:
            sample += f", ... (+{len(missing) - 8} more)"
        raise RuntimeError(
            f"{table_path}: pot_key_depth location(s) match no logic location: "
            f"{sample}. Regenerate or fix the stale table before codegen."
        )


# ---------------------------------------------------------------------------
# Predicate DSL parser
# ---------------------------------------------------------------------------
# Grammar (per logic.schema.yaml `predicate_expression`):
#
#   predicate := or_expr
#   or_expr   := and_expr ( "OR" and_expr )*
#   and_expr  := not_expr ( "AND" not_expr )*
#   not_expr  := "NOT" not_expr | atom
#   atom      := op_call | macro_call | "TRUE()" | "FALSE()" | "(" predicate ")"
#   op_call   := OP_NAME "(" arg_list? ")"
#   macro_call:= MacroName "(" arg_list? ")"
#   arg_list  := arg ("," arg)*
#   arg       := identifier | int_literal | list_literal
#   list_literal := "[" identifier ("," identifier)* "]"


# AST node forms:
#   ("op", op_name, [args])
#   ("macro", name, [args])
#   ("and", [child, ...])
#   ("or", [child, ...])
#   ("not", child)
#   ("true",)
#   ("false",)


TOKEN_PATTERNS = [
    (r"\s+", None),                       # whitespace
    (r"AND\b", "AND"),
    (r"OR\b", "OR"),
    (r"NOT\b", "NOT"),
    (r"TRUE\b", "TRUE_LIT"),
    (r"FALSE\b", "FALSE_LIT"),
    # Comparison ops: compile-time pseudo-ops that resolve to TRUE/FALSE during
    # macro expansion (substituted with literal integer args). See `fold_compares`.
    (r"EQ\b", "CMP_EQ"),
    (r"NE\b", "CMP_NE"),
    (r"LT\b", "CMP_LT"),
    (r"LE\b", "CMP_LE"),
    (r"GT\b", "CMP_GT"),
    (r"GE\b", "CMP_GE"),
    (r"\(", "LPAREN"),
    (r"\)", "RPAREN"),
    (r"\[", "LBRACK"),
    (r"\]", "RBRACK"),
    (r",", "COMMA"),
    (r"\d+", "INT"),
    (r"[A-Za-z_][A-Za-z0-9_]*", "IDENT"),
]


class ParseError(Exception):
    pass


def tokenize(text: str) -> list[tuple[str, str]]:
    pos = 0
    tokens = []
    while pos < len(text):
        for pattern, kind in TOKEN_PATTERNS:
            m = re.match(pattern, text[pos:])
            if m:
                if kind is not None:
                    tokens.append((kind, m.group(0)))
                pos += m.end()
                break
        else:
            raise ParseError(f"unexpected character at {pos}: {text[pos:pos+20]!r}")
    return tokens


class Parser:
    def __init__(self, tokens):
        self.tokens = tokens
        self.pos = 0

    def peek(self, offset=0):
        if self.pos + offset >= len(self.tokens):
            return ("EOF", "")
        return self.tokens[self.pos + offset]

    def eat(self, kind=None):
        if self.pos >= len(self.tokens):
            raise ParseError(f"unexpected end of input (wanted {kind})")
        tok = self.tokens[self.pos]
        if kind is not None and tok[0] != kind:
            raise ParseError(f"expected {kind} at token {self.pos}, got {tok}")
        self.pos += 1
        return tok

    def parse(self):
        node = self.or_expr()
        if self.pos != len(self.tokens):
            raise ParseError(f"trailing tokens at {self.pos}: {self.tokens[self.pos:]}")
        return node

    def or_expr(self):
        children = [self.and_expr()]
        while self.peek()[0] == "OR":
            self.eat("OR")
            children.append(self.and_expr())
        if len(children) == 1:
            return children[0]
        return ("or", children)

    def and_expr(self):
        children = [self.not_expr()]
        while self.peek()[0] == "AND":
            self.eat("AND")
            children.append(self.not_expr())
        if len(children) == 1:
            return children[0]
        return ("and", children)

    def not_expr(self):
        if self.peek()[0] == "NOT":
            self.eat("NOT")
            child = self.not_expr()
            return ("not", child)
        return self.comparison()

    def comparison(self):
        left = self.atom()
        kind = self.peek()[0]
        if kind in {"CMP_EQ", "CMP_NE", "CMP_LT", "CMP_LE", "CMP_GT", "CMP_GE"}:
            self.eat(kind)
            right = self.atom()  # comparison RHS is also an atom (ident/int/expr)
            return ("cmp", kind, left, right)
        return left

    def atom(self):
        tok = self.peek()
        if tok[0] == "LPAREN":
            self.eat("LPAREN")
            node = self.or_expr()
            self.eat("RPAREN")
            return node
        if tok[0] == "TRUE_LIT":
            self.eat("TRUE_LIT")
            self.eat("LPAREN")
            self.eat("RPAREN")
            return ("true",)
        if tok[0] == "FALSE_LIT":
            self.eat("FALSE_LIT")
            self.eat("LPAREN")
            self.eat("RPAREN")
            return ("false",)
        if tok[0] == "INT":
            return ("int", int(self.eat("INT")[1]))
        if tok[0] == "IDENT":
            name = self.eat("IDENT")[1]
            # If followed by LPAREN, this is an op-call or macro-call.
            # Otherwise it's a bare identifier (parameter reference).
            if self.peek()[0] == "LPAREN":
                self.eat("LPAREN")
                args = []
                if self.peek()[0] != "RPAREN":
                    args.append(self.arg())
                    while self.peek()[0] == "COMMA":
                        self.eat("COMMA")
                        args.append(self.arg())
                self.eat("RPAREN")
                if name.startswith("OP_"):
                    bare = name[3:]
                    return ("op", bare, args)
                return ("call", name, args)
            return ("ident", name)
        raise ParseError(f"unexpected token at atom: {tok}")

    def arg(self):
        tok = self.peek()
        if tok[0] == "LBRACK":
            self.eat("LBRACK")
            items = []
            if self.peek()[0] != "RBRACK":
                items.append(self.eat()[1])
                while self.peek()[0] == "COMMA":
                    self.eat("COMMA")
                    items.append(self.eat()[1])
            self.eat("RBRACK")
            return ("list", items)
        if tok[0] == "INT":
            return ("int", int(self.eat("INT")[1]))
        if tok[0] == "IDENT":
            return ("ident", self.eat("IDENT")[1])
        raise ParseError(f"unexpected token in arg: {tok}")


def parse_predicate(text: str):
    return Parser(tokenize(text)).parse()


# ---------------------------------------------------------------------------
# Op-name resolution
# ---------------------------------------------------------------------------
# Predicate "call" nodes are either ops or macros. We resolve in a second pass
# once we know the op registry and macro set.


def resolve_calls(ast, ops: dict[str, OpDef], macros: dict[str, MacroDef]):
    """Convert ("call", name, args) -> ("op", name, args) or ("macro", name, args)."""
    if not isinstance(ast, tuple):
        return ast
    if ast[0] == "call":
        _, name, args = ast
        new_args = [resolve_calls(a, ops, macros) if isinstance(a, tuple) and a[0] in ("call", "op", "macro", "and", "or", "not", "true", "false", "cmp") else a for a in args]
        if name in ops:
            return ("op", name, new_args)
        if name in macros:
            return ("macro", name, new_args)
        # Treat unknown UPPER_SNAKE as op for well-formedness errors downstream.
        if name.isupper() or "_" in name:
            return ("op", name, new_args)
        return ("macro", name, new_args)
    if ast[0] in ("and", "or"):
        return (ast[0], [resolve_calls(c, ops, macros) for c in ast[1]])
    if ast[0] == "not":
        return ("not", resolve_calls(ast[1], ops, macros))
    if ast[0] == "cmp":
        return ("cmp", ast[1], resolve_calls(ast[2], ops, macros), resolve_calls(ast[3], ops, macros))
    if ast[0] == "op":
        return ("op", ast[1], [resolve_calls(a, ops, macros) if isinstance(a, tuple) and a[0] in ("call",) else a for a in ast[2]])
    return ast


# ---------------------------------------------------------------------------
# Macro expansion
# ---------------------------------------------------------------------------
def expand_macros(ast, macros: dict[str, MacroDef], parsed_macro_bodies: dict[str, object], ops: dict[str, OpDef], depth: int = 0, expanding: set = None):
    """Recursively expand macro calls. Detects cycles."""
    if expanding is None:
        expanding = set()
    if depth > 32:
        raise ParseError(f"macro expansion exceeded depth 32 (cycle?)")
    if not isinstance(ast, tuple):
        return ast
    kind = ast[0]
    if kind == "macro":
        _, name, args = ast
        if name not in parsed_macro_bodies:
            raise ParseError(f"unknown macro {name!r}")
        if name in expanding:
            raise ParseError(f"macro cycle: {expanding} -> {name}")
        macro = macros[name]
        body_ast = parsed_macro_bodies[name]
        if len(args) != len(macro.parameters):
            # Allow arity mismatch only if macro is parameterless and call passes nothing.
            if not (len(macro.parameters) == 0 and len(args) == 0):
                raise ParseError(f"macro {name} expects {len(macro.parameters)} arg(s), got {len(args)}")
        # Substitute parameters in body_ast.
        param_map = dict(zip(macro.parameters, args))
        substituted = substitute_params(body_ast, param_map)
        # Fold compile-time comparisons (turn `min_level EQ 1` -> TRUE/FALSE).
        folded = fold_compares(substituted)
        # Recursively expand any inner macros, then simplify.
        expanded = expand_macros(folded, macros, parsed_macro_bodies, ops, depth + 1, expanding | {name})
        return simplify(expanded)
    if kind in ("and", "or"):
        return (kind, [expand_macros(c, macros, parsed_macro_bodies, ops, depth, expanding) for c in ast[1]])
    if kind == "not":
        return ("not", expand_macros(ast[1], macros, parsed_macro_bodies, ops, depth, expanding))
    if kind == "op":
        return ast  # ops don't contain sub-predicates as args; args are item/region IDs etc.
    return ast


def substitute_params(ast, param_map: dict):
    """Replace ident args matching parameter names with their bound values.

    The bound value for a parameter is whatever the caller passed: an ident, an
    int, or a list. We replace IN PLACE within the AST tree.
    """
    if not isinstance(ast, tuple):
        return ast
    kind = ast[0]
    if kind in ("op", "macro"):
        head = ast[0]
        name = ast[1]
        new_args = []
        for a in ast[2]:
            if isinstance(a, tuple) and a[0] == "ident" and a[1] in param_map:
                # Replace ident with the bound value.
                new_args.append(param_map[a[1]])
            else:
                new_args.append(a)
        return (head, name, new_args)
    if kind in ("and", "or"):
        return (kind, [substitute_params(c, param_map) for c in ast[1]])
    if kind == "not":
        return ("not", substitute_params(ast[1], param_map))
    if kind == "cmp":
        _, op, lhs, rhs = ast
        new_lhs = param_map[lhs[1]] if isinstance(lhs, tuple) and lhs[0] == "ident" and lhs[1] in param_map else substitute_params(lhs, param_map)
        new_rhs = param_map[rhs[1]] if isinstance(rhs, tuple) and rhs[0] == "ident" and rhs[1] in param_map else substitute_params(rhs, param_map)
        return ("cmp", op, new_lhs, new_rhs)
    if kind == "ident":
        if ast[1] in param_map:
            return param_map[ast[1]]
    return ast


def fold_compares(ast, errors: list = None):
    """Evaluate ("cmp", op, lhs, rhs) when both sides are integer literals; replace with TRUE/FALSE.

    If a side is still an unbound ident, emit a warning and treat the comparison
    as TRUE (so the surrounding macro-body branch isn't accidentally lost). This
    matches the agent-authored macro convention where ungrounded parameters
    indicate "Phase A doesn't use this branch — assume true and let later passes
    drop the dead code."
    """
    if errors is None:
        errors = []
    if not isinstance(ast, tuple):
        return ast
    kind = ast[0]
    if kind == "cmp":
        _, op, lhs, rhs = ast
        lhs = fold_compares(lhs, errors)
        rhs = fold_compares(rhs, errors)
        if isinstance(lhs, tuple) and lhs[0] == "int" and isinstance(rhs, tuple) and rhs[0] == "int":
            l, r = lhs[1], rhs[1]
            result = {
                "CMP_EQ": l == r,
                "CMP_NE": l != r,
                "CMP_LT": l < r,
                "CMP_LE": l <= r,
                "CMP_GT": l > r,
                "CMP_GE": l >= r,
            }[op]
            return ("true",) if result else ("false",)
        # Unbound ident on at least one side — a macro-authoring error (a
        # parameter was never grounded by the caller). Defaulting to TRUE is the
        # DANGEROUS direction: it silently collapses both arms of a level/count
        # comparison and grants the stricter logic (e.g. silver-arrow capability)
        # for free, which could certify an unbeatable seed as beatable. Raise a
        # hard error (collected into all_errors) instead of the old silent
        # over-permissive TRUE — so --strict (CI) FAILS the build; a non-strict
        # local build at least degrades to a WARN + FALSE arm.
        # DORMANT today: every call site passes a literal shorthand
        # (CanShootArrowsL1 / CanShootSilvers / HasSwordN), so this never fires.
        raise ParseError(
            f"unbound comparison: {op} {lhs!r} {rhs!r} "
            "(a macro parameter was not grounded by the caller)")
    if kind in ("and", "or"):
        return (kind, [fold_compares(c, errors) for c in ast[1]])
    if kind == "not":
        return ("not", fold_compares(ast[1], errors))
    if kind == "op":
        return ast
    return ast


def simplify(ast):
    """Constant-fold TRUE/FALSE through AND/OR/NOT to drop dead branches.

    Used after macro expansion + compare folding to clean up macros like
    `(branch_A AND TRUE) OR (branch_B AND FALSE)` -> `branch_A`.
    """
    if not isinstance(ast, tuple):
        return ast
    kind = ast[0]
    if kind == "and":
        children = [simplify(c) for c in ast[1]]
        # If any FALSE, whole AND is FALSE.
        if any(c == ("false",) for c in children):
            return ("false",)
        # Drop TRUE children.
        children = [c for c in children if c != ("true",)]
        if not children:
            return ("true",)
        if len(children) == 1:
            return children[0]
        return ("and", children)
    if kind == "or":
        children = [simplify(c) for c in ast[1]]
        if any(c == ("true",) for c in children):
            return ("true",)
        children = [c for c in children if c != ("false",)]
        if not children:
            return ("false",)
        if len(children) == 1:
            return children[0]
        return ("or", children)
    if kind == "not":
        child = simplify(ast[1])
        if child == ("true",):
            return ("false",)
        if child == ("false",):
            return ("true",)
        return ("not", child)
    return ast


# ---------------------------------------------------------------------------
# Bytecode encoder
# ---------------------------------------------------------------------------
def encode_predicate(ast, ops: dict[str, OpDef], items: dict[str, ItemDef], regions: dict[str, RegionDef], locations: dict[str, LocationDef]) -> bytes:
    """Encode a fully macro-expanded AST to the bytecode stream."""
    out = bytearray()
    _emit(ast, out, ops, items, regions, locations)
    return bytes(out)


WORLD_STATES = {"open": 0, "standard": 1, "inverted": 2, "retro": 3}
GOALS = {"ganon": 0, "fast_ganon": 1, "dungeons": 2, "pedestal": 3, "triforce_hunt": 4, "ganonhunt": 5, "completionist": 6}

# ---------------------------------------------------------------------------
# Boss-shuffle runtime (OP_CAN_KILL_BOSS) dispatch tables.
#
# BOSS_KILL_BODIES is indexed by boss-pool index — this order is the CONTRACT
# shared with src/rando/shuffle_boss.c (the kBoss_* enum) and the runtime boss
# assignment that BossShuffle_ComputeAssignment produces. Each body reuses the
# canonical per-dungeon CanKill<Boss> macro, so OP_CAN_KILL_BOSS(<Dungeon>) with
# boss_shuffle OFF compiles to the SAME reachability as the inline macro it
# replaces (default placement byte-identical). `world` is a free ident the
# CanKill* macros accept for arity parity but never read (CanExtendMagic /
# CanMeltThings ignore it) — identical to how the location predicates pass it.
BOSS_KILL_BODIES = [
    "CanKillArmosKnights()",     # 0  kBoss_ArmosKnights
    "CanKillLanmolas(world)",    # 1  kBoss_Lanmolas
    "CanKillMoldorm()",          # 2  kBoss_Moldorm
    "CanKillAgahnim2()",         # 3  kBoss_Agahnim   (pinned; Aga1≈Aga2 kill rule)
    "CanKillHelmasaurKing(world)",  # 4  kBoss_HelmasaurKing
    "CanKillArrghus(world)",     # 5  kBoss_Arrghus
    "CanKillMothula(world)",     # 6  kBoss_Mothula
    "CanKillBlind(world)",       # 7  kBoss_Blind
    "CanKillKholdstare(world)",  # 8  kBoss_Kholdstare
    "CanKillVitreous(world)",    # 9  kBoss_Vitreous
    "CanKillTrinexx(world)",     # 10 kBoss_Trinexx
    "CanKillAgahnim2()",         # 11 kBoss_Agahnim2  (pinned)
]

# dungeon-id (HCE=0..GT=12) → vanilla boss-pool index. MIRRORS shuffle_boss.c
# kBossVanilla; 0xFF = no boss (HCE) / unused. Used by OP_CAN_KILL_BOSS when no
# per-seed boss assignment is installed (NULL context). Logic_SelfCheck
# cross-checks this against shuffle_boss.c's vanilla map at runtime.
DUNGEON_VANILLA_BOSS = [
    0xFF,  # 0  HyruleCastleEscape (no boss)
    0,     # 1  EasternPalace      → ArmosKnights
    1,     # 2  DesertPalace       → Lanmolas
    2,     # 3  TowerOfHera        → Moldorm
    3,     # 4  HyruleCastleTower  → Agahnim   (pinned)
    4,     # 5  PalaceOfDarkness   → HelmasaurKing
    5,     # 6  SwampPalace        → Arrghus
    6,     # 7  SkullWoods         → Mothula
    7,     # 8  ThievesTown        → Blind
    8,     # 9  IcePalace          → Kholdstare
    9,     # 10 MiseryMire         → Vitreous
    10,    # 11 TurtleRock         → Trinexx
    11,    # 12 GanonsTower        → Agahnim2  (pinned)
]
# Phase B Slice 4 §56 — operand lookup tables for the three Phase B ops.
# Difficulty enum mirrors `ItemPoolDifficulty` in `src/rando/rando_settings.h`.
# Glitch levels mirror the `logic` axis: NoGlitches=0, OverworldGlitches=1,
# MajorGlitches=2, HybridMG=3, NoLogic=4.
DIFFICULTY_LEVELS = {"easy": 0, "normal": 1, "hard": 2, "expert": 3}
# Phase B swordless — OP_MODEWEAPONS_EQ operand. Mirrors `ModeWeapons` in
# src/rando/rando_settings.h. `swordless` is the only Phase B-exposed value
# beyond randomized/assured, but all four map for completeness.
MODE_WEAPONS = {"randomized": 0, "assured": 1, "vanilla": 2, "swordless": 3}
GLITCH_LEVELS = {
    "no_glitches": 0,
    "overworld_glitches": 1,
    "major_glitches": 2,
    "hybrid_major_glitches": 3,
    "no_logic": 4,
}
# Populated by load_tricks(op_registry) at codegen startup; keys are trick
# ids (e.g. "boots-clip"), values are bit positions (0..7).
TRICK_BITS: dict[str, int] = {}

# §12.6 — ROM-version verification status. The string→int encoding MUST match
# the `RandoRomVerStatus` enum in src/rando/rando_logic.h.
ROM_VER_STATUS = {
    "untested-on-us10": 0,
    "verified-us10": 1,
    "cross-version": 2,
    "jp10-only": 3,
    "us10-different": 4,
}
# Populated by load_trick_status(op_registry) at codegen startup; keys are
# trick ids in BOTH kebab (`boots-clip`) and snake (`boots_clip`) form (snake is
# what OP_TRICK predicate bodies use), values are the rom_version_status string.
# Used by well_formedness to reject jp10-only tricks in predicate bodies (§12.6.5).
TRICK_STATUS: dict[str, str] = {}


def load_trick_status(path: Path) -> tuple[list[dict], dict[str, str]]:
    """Parse `tricks:` rom_version_status (§12.6.1).

    Returns (rows, id_to_status):
      rows = [{bit, id, status_int}] sorted by bit — for emitting kRandoTrickStatus[].
      id_to_status = {kebab_id: status_str, snake_id: status_str} — for
        well_formedness jp10-only rejection (predicate bodies use the snake id).
    """
    doc = load_yaml(path)
    rows: list[dict] = []
    id_to_status: dict[str, str] = {}
    for raw in doc.get("tricks", []):
        bit = raw["bit"]
        canonical = raw["id"]
        status = raw.get("rom_version_status", "untested-on-us10")
        if status not in ROM_VER_STATUS:
            raise ParseError(
                f"trick {canonical!r}: rom_version_status {status!r} not in "
                f"{sorted(ROM_VER_STATUS)}")
        rows.append({"bit": bit, "id": canonical, "status_int": ROM_VER_STATUS[status]})
        id_to_status[canonical] = status
        snake = canonical.replace("-", "_")
        if snake != canonical:
            id_to_status[snake] = status
    rows.sort(key=lambda r: r["bit"])
    return rows, id_to_status


def load_glitch_levels(path: Path) -> list[dict]:
    """Parse the `glitch_levels:` table (§12.6.2).

    Returns [{level, id, status_int}] sorted by level — for emitting
    kRandoGlitchLevelStatus[]. `level` is the settings.logic value.
    """
    doc = load_yaml(path)
    rows: list[dict] = []
    for raw in doc.get("glitch_levels", []):
        level = raw["level"]
        gid = raw["id"]
        if gid in GLITCH_LEVELS and GLITCH_LEVELS[gid] != level:
            raise ParseError(
                f"glitch_level {gid!r}: level {level} disagrees with the "
                f"codegen GLITCH_LEVELS map ({GLITCH_LEVELS[gid]})")
        status = raw.get("rom_version_status", "untested-on-us10")
        if status not in ROM_VER_STATUS:
            raise ParseError(
                f"glitch_level {gid!r}: rom_version_status {status!r} not in "
                f"{sorted(ROM_VER_STATUS)}")
        rows.append({"level": level, "id": gid, "status_int": ROM_VER_STATUS[status]})
    rows.sort(key=lambda r: r["level"])
    return rows


def load_tricks(path: Path) -> dict[str, int]:
    """Parse the `tricks:` table in op_registry.yaml — map id → bit position.

    Phase B §56: the codegen emits the bit position as the OP_TRICK operand;
    runtime tests `(settings.tricks & (1 << bit)) != 0`. Trick widths cap at
    8 (uint8 settings field); per the registry, only bits 0-7 are valid.

    The registry ids use kebab-case (`boots-clip`) to match the user-facing
    `--settings=tricks=...` CSV grammar. The DSL tokenizer accepts only
    `[A-Za-z_][A-Za-z0-9_]*` (no hyphens), so we ALSO register the
    snake-case variant (`boots_clip`) at the same bit. Predicate authors
    use the snake form in `OP_TRICK(name)`; settings CSV stays kebab.
    """
    doc = load_yaml(path)
    out: dict[str, int] = {}
    for raw in doc.get("tricks", []):
        bit = raw["bit"]
        if not (0 <= bit < 8):
            raise ParseError(f"trick {raw.get('id')!r}: bit {bit} out of range 0..7")
        canonical = raw["id"]
        out[canonical] = bit
        snake = canonical.replace("-", "_")
        if snake != canonical:
            out[snake] = bit
    return out


def _emit(ast, out: bytearray, ops, items, regions, locations):
    kind = ast[0]
    if kind == "true":
        # Vacuous AND (count=0) is true.
        out.append(ops["AND"].id)
        out.append(0)
        return
    if kind == "false":
        # Vacuous OR (count=0) is false.
        out.append(ops["OR"].id)
        out.append(0)
        return
    if kind == "and":
        out.append(ops["AND"].id)
        out.append(len(ast[1]))
        for c in ast[1]:
            _emit(c, out, ops, items, regions, locations)
        return
    if kind == "or":
        out.append(ops["OR"].id)
        out.append(len(ast[1]))
        for c in ast[1]:
            _emit(c, out, ops, items, regions, locations)
        return
    if kind == "not":
        out.append(ops["NOT"].id)
        _emit(ast[1], out, ops, items, regions, locations)
        return
    if kind == "op":
        _, name, args = ast
        if name not in ops:
            raise ParseError(f"unknown op {name!r}")
        op = ops[name]
        out.append(op.id)
        _emit_operands(name, args, out, items, regions)
        return
    raise ParseError(f"cannot encode AST kind {kind!r}")


def _emit_u16le(out: bytearray, v: int):
    out += struct.pack("<H", v)


def _emit_operands(op_name: str, args, out: bytearray, items, regions):
    if op_name == "HAS_ITEM":
        out += struct.pack("<H", _resolve_item(args[0], items))
    elif op_name == "HAS_AMOUNT":
        out += struct.pack("<H", _resolve_item(args[0], items))
        out.append(_resolve_int(args[1]))
    elif op_name == "HAS_ANY_OF":
        lst = _resolve_list(args[0])
        out.append(len(lst))
        for it in lst:
            out += struct.pack("<H", _resolve_item(("ident", it), items))
    elif op_name == "HAS_ANY_COUNT":
        lst = _resolve_list(args[0])
        out.append(len(lst))
        for it in lst:
            out += struct.pack("<H", _resolve_item(("ident", it), items))
        out.append(_resolve_int(args[1]))
    elif op_name == "WORLDSTATE_EQ":
        out.append(WORLD_STATES[_resolve_ident(args[0])])
    elif op_name == "GOAL_EQ":
        out.append(GOALS[_resolve_ident(args[0])])
    elif op_name == "GOAL_REQUIRES_DUNGEON":
        out.append(_resolve_dungeon_id(_resolve_ident(args[0])))
    elif op_name == "DUNGEON_CLEARED":
        out.append(_resolve_dungeon_id(_resolve_ident(args[0])))
    elif op_name == "REGION_REACHABLE":
        out += struct.pack("<H", _resolve_region(args[0], regions))
    elif op_name == "HAS_PRIZE":
        out.append(_resolve_prize_id(_resolve_ident(args[0])))
    elif op_name == "MEDALLION_OPENS":
        out.append(_resolve_entrance_id(_resolve_ident(args[0])))
    elif op_name == "CAN_KILL_BOSS":
        # operand = dungeon id (HCE=0..GT=12); the runtime resolves the boss
        # currently assigned to that dungeon and evaluates its kill predicate.
        out.append(_resolve_dungeon_id(_resolve_ident(args[0])))
    elif op_name == "ITEM_IS":
        out += struct.pack("<H", _resolve_item(args[0], items))
    elif op_name == "TRICK":
        # Phase B §56: resolve named trick → bit position via TRICK_BITS table
        # (populated by load_tricks at codegen startup).
        name = _resolve_ident(args[0])
        if name not in TRICK_BITS:
            raise ParseError(
                f"OP_TRICK references unknown trick {name!r}; "
                f"known: {sorted(TRICK_BITS.keys())}")
        out.append(TRICK_BITS[name])
    elif op_name == "DIFFICULTY_AT_LEAST":
        name = _resolve_ident(args[0])
        if name not in DIFFICULTY_LEVELS:
            raise ParseError(
                f"OP_DIFFICULTY_AT_LEAST: unknown difficulty {name!r}; "
                f"known: {sorted(DIFFICULTY_LEVELS.keys())}")
        out.append(DIFFICULTY_LEVELS[name])
    elif op_name == "GLITCH_LEVEL_AT_LEAST":
        name = _resolve_ident(args[0])
        if name not in GLITCH_LEVELS:
            raise ParseError(
                f"OP_GLITCH_LEVEL_AT_LEAST: unknown glitch level {name!r}; "
                f"known: {sorted(GLITCH_LEVELS.keys())}")
        out.append(GLITCH_LEVELS[name])
    elif op_name == "MODEWEAPONS_EQ":
        name = _resolve_ident(args[0])
        if name not in MODE_WEAPONS:
            raise ParseError(
                f"OP_MODEWEAPONS_EQ: unknown mode.weapons {name!r}; "
                f"known: {sorted(MODE_WEAPONS.keys())}")
        out.append(MODE_WEAPONS[name])
    elif op_name == "INSTANT_FLUTE":
        if args:
            raise ParseError("OP_INSTANT_FLUTE takes no operands")
    elif op_name == "POT_KEYS_ON":
        if args:
            raise ParseError("OP_POT_KEYS_ON takes no operands")
    elif op_name == "POT_KEYS_WILD":
        if args:
            raise ParseError("OP_POT_KEYS_WILD takes no operands")
    elif op_name == "POT_KEYS_DUNGEON":
        if args:
            raise ParseError("OP_POT_KEYS_DUNGEON takes no operands")
    else:
        raise ParseError(f"no operand-emit rule for op {op_name!r}")


def _resolve_item(arg, items):
    if isinstance(arg, tuple) and arg[0] == "ident":
        nm = arg[1]
    elif isinstance(arg, str):
        nm = arg
    else:
        raise ParseError(f"item arg expected ident, got {arg!r}")
    if nm not in items:
        raise ParseError(f"unknown item ID {nm!r}")
    return items[nm].id


def _resolve_int(arg):
    if isinstance(arg, tuple) and arg[0] == "int":
        return arg[1]
    if isinstance(arg, int):
        return arg
    raise ParseError(f"int arg expected, got {arg!r}")


def _resolve_list(arg):
    if isinstance(arg, tuple) and arg[0] == "list":
        return arg[1]
    raise ParseError(f"list arg expected, got {arg!r}")


def _resolve_ident(arg):
    if isinstance(arg, tuple) and arg[0] == "ident":
        return arg[1]
    if isinstance(arg, str):
        return arg
    raise ParseError(f"ident arg expected, got {arg!r}")


def _resolve_region(arg, regions):
    name = _resolve_ident(arg)
    if name in regions:
        # Stable IDs assigned by sorted order of region declaration.
        idx = sorted(regions.keys()).index(name)
        return idx
    # Unknown region name — almost always a translation typo (e.g.
    # "DesertPalace" instead of "DesertPalace_Lobby"). The old behavior emitted a
    # _stable_hash16 fallback that is essentially never a valid region index, so
    # the predicate's region term was PERMANENTLY FALSE at runtime — silently
    # stranding a location/edge, surfacing only in playtest. The "--strict
    # promotes to fail" claim was itself stale: the WARN was a bare stderr print,
    # never collected into all_errors, so --strict did NOT catch it. Raise a hard
    # parse error (now collected into all_errors) so --strict (CI) FAILS on a
    # typoed region; a non-strict local build degrades to a WARN + FALSE term —
    # still better than the old silent never-valid hash. The committed
    # predicates all resolve, so this never fires on an unmodified tree.
    raise ParseError(
        f"OP_REGION_REACHABLE references unknown region {name!r}; "
        f"did you mean one of: {sorted(regions.keys())[:6]}"
        f"{'...' if len(regions) > 6 else ''}?")


def _resolve_dungeon_id(name: str) -> int:
    dungeons = ["HyruleCastleEscape", "EasternPalace", "DesertPalace", "TowerOfHera",
                "HyruleCastleTower", "PalaceOfDarkness", "SwampPalace", "SkullWoods",
                "ThievesTown", "IcePalace", "MiseryMire", "TurtleRock", "GanonsTower"]
    if name not in dungeons:
        raise ParseError(f"unknown dungeon {name!r}; valid: {dungeons}")
    return dungeons.index(name)


def _resolve_prize_id(name: str) -> int:
    prizes = ["Prize_GreenPendant", "Prize_RedPendant", "Prize_BluePendant",
              "Prize_Crystal1", "Prize_Crystal2", "Prize_Crystal3", "Prize_Crystal4",
              "Prize_Crystal5", "Prize_Crystal6", "Prize_Crystal7"]
    if name not in prizes:
        raise ParseError(f"unknown prize {name!r}")
    return prizes.index(name)


def _resolve_entrance_id(name: str) -> int:
    entrances = ["MiseryMire_Entrance", "TurtleRock_Entrance"]
    if name not in entrances:
        raise ParseError(f"unknown medallion-affected entrance {name!r}; valid: {entrances}")
    return entrances.index(name)


def _stable_hash16(s: str) -> int:
    h = 0
    for ch in s:
        h = (h * 1315423911 + ord(ch)) & 0xFFFF
    return h


# ---------------------------------------------------------------------------
# Well-formedness checks (task 3.10)
# ---------------------------------------------------------------------------
def well_formedness(ast, ops, items, regions, locations, context: str, phase_a_only: bool = False, errors: list = None):
    """Walk AST, check:
    - referenced items / regions / dungeons exist;
    - OP_ITEM_IS only in can_place context;
    - (phase_a_only=True only) no Phase B ops in Phase A predicates.
    Phase B Slice 4 §56 flipped the default to allow trick/difficulty/
    glitch ops; the strict-Phase-A gate remains available for callers
    that explicitly opt in.
    Returns the list of error strings (in-place modifications).
    """
    if errors is None:
        errors = []
    if not isinstance(ast, tuple):
        return errors
    kind = ast[0]
    if kind == "op":
        _, name, args = ast
        if name not in ops:
            errors.append(f"unknown op {name!r}")
            return errors
        op = ops[name]
        if phase_a_only and op.phase == "B":
            errors.append(f"Phase B op {name!r} not allowed in Phase A predicate ({context})")
        if name == "ITEM_IS" and context not in ("can_place", "always_allow"):
            errors.append(f"OP_ITEM_IS only allowed in placement-context predicates (can_place / always_allow), used in {context}")
        # §12.6.5 — a predicate MUST NOT reference a trick whose
        # rom_version_status is jp10-only (confirmed NOT to work on US 1.0).
        if name == "TRICK" and args:
            tname = _resolve_ident(args[0])
            if TRICK_STATUS.get(tname) == "jp10-only":
                errors.append(
                    f"OP_TRICK({tname}) references a jp10-only trick (does not "
                    f"work on US 1.0); remove it from {context} or change its "
                    f"rom_version_status in op_registry.yaml")
        # Validate operand references (best-effort)
        if name in ("HAS_ITEM", "HAS_AMOUNT", "ITEM_IS"):
            it = _resolve_ident(args[0]) if args else None
            if it and it not in items:
                errors.append(f"unknown item {it!r} in {context}")
        if name in ("HAS_ANY_OF", "HAS_ANY_COUNT"):
            if args:
                lst = _resolve_list(args[0]) if isinstance(args[0], tuple) and args[0][0] == "list" else []
                for it in lst:
                    if it not in items:
                        errors.append(f"unknown item {it!r} in {context}")
    if kind in ("and", "or"):
        for c in ast[1]:
            well_formedness(c, ops, items, regions, locations, context, phase_a_only, errors)
    if kind == "not":
        well_formedness(ast[1], ops, items, regions, locations, context, phase_a_only, errors)
    return errors


# ---------------------------------------------------------------------------
# Codegen emitters
# ---------------------------------------------------------------------------
HEADER_BANNER = """\
// AUTO-GENERATED by assets/rando_logic_gen.py. DO NOT EDIT.
// Regenerate by running: python assets/rando_logic_gen.py
//
// Source artifacts:
//   - assets/rando/op_registry.yaml
//   - assets/rando/item_registry.yaml
//   - assets/rando/location_registry.yaml
//   - assets/rando/logic.yaml (optional; empty graph if missing)
//   - assets/rando/macros.yaml (optional)
"""


def sanitize_for_define(s: str) -> str:
    """Convert a free-form name to a valid C identifier suffix.

    Replaces non-alphanumeric chars with underscore; collapses runs.
    """
    out = re.sub(r"[^A-Za-z0-9]+", "_", s).strip("_")
    return out


def emit_location_ids(locations: dict[str, LocationDef], path: Path):
    lines = [HEADER_BANNER, "", "#ifndef ZELDA3_RANDO_LOCATION_IDS_H_", "#define ZELDA3_RANDO_LOCATION_IDS_H_", "", "// Stable numeric location IDs from assets/rando/location_registry.yaml.", "// Renaming a location does NOT change its numeric ID (append-only registry).", ""]
    for loc in sorted(locations.values(), key=lambda l: l.id):
        token = sanitize_for_define(loc.name)
        lines.append(f"#define LOC_{token} {loc.id}")
    max_id = max(l.id for l in locations.values()) if locations else 0
    lines.append("")
    lines.append(f"#define LOC__COUNT {max_id + 1}")
    lines.append("")
    # Every location-id-keyed array/bitmap across the randomizer module is sized
    # by kRandoLocationCapacity (rando_logic.h), and the reachability/sphere
    # bitsets in rando_logic.c follow it. A registry append that pushes a
    # location id past that ceiling must fail the BUILD, not silently OOB-index a
    # bitset / truncate the digest / drop a tracker row at runtime. Keep this
    # 2048 in lockstep with kRandoLocationCapacity in src/rando/rando_logic.h.
    lines.append("_Static_assert(LOC__COUNT <= 2048,")
    lines.append('               "location id space exceeds kRandoLocationCapacity '
                 '(2048) in rando_logic.h — grow both together");')
    lines.append("")
    lines.append("#endif  // ZELDA3_RANDO_LOCATION_IDS_H_")
    atomic_write_text(path, "\n".join(lines) + "\n")


def emit_item_ids(items: dict[str, ItemDef], path: Path):
    lines = [HEADER_BANNER, "", "#ifndef ZELDA3_RANDO_ITEM_IDS_H_", "#define ZELDA3_RANDO_ITEM_IDS_H_", "", "// Stable numeric item IDs from assets/rando/item_registry.yaml.", "// Used by Rando_OnLocationCheck, predicate VM, placement table.", ""]
    for it in sorted(items.values(), key=lambda i: i.id):
        token = sanitize_for_define(it.name)
        lines.append(f"#define ITEM_{token} {it.id}")
    max_id = max(i.id for i in items.values()) if items else 0
    lines.append("")
    lines.append(f"#define ITEM__COUNT {max_id + 1}")
    lines.append("")
    lines.append("#endif  // ZELDA3_RANDO_ITEM_IDS_H_")
    atomic_write_text(path, "\n".join(lines) + "\n")


def _write_empty_chest_lookup(path: Path) -> None:
    """Emit an empty chest_lookup.h (no assets extracted, e.g. CI).

    1-element sentinel array (never read, _COUNT is 0) for portable C89.
    chest_lookup() bounds its search by _COUNT, so every lookup returns 0xFFFF.
    """
    lines = [
        HEADER_BANNER,
        "",
        "// chest_lookup.h — EMPTY: no vanilla chest table was available at",
        "// codegen time (assets/rando/chest_table.gen.bin absent — no ROM",
        "// extracted, e.g. a CI build). chest_lookup() returns 0xFFFF for every",
        "// (room, ordinal), so all chests fall through to vanilla. Re-run asset",
        "// extraction and rebuild to populate this table for a playable build.",
        "",
        "#ifndef ZELDA3_RANDO_CHEST_LOOKUP_H_",
        "#define ZELDA3_RANDO_CHEST_LOOKUP_H_",
        "",
        "#include \"../types.h\"",
        "#include \"location_ids.h\"",
        "",
        "typedef struct RandoChestLookupEntry {",
        "  uint16 room;     // 0..319 dungeon room index",
        "  uint8  ordinal;  // 0..5; in-room chest index = (tile - 0x58)",
        "  uint16 loc_id;   // LOC_*",
        "} RandoChestLookupEntry;",
        "",
        "// Unused sentinel (kRandoChestLookup_COUNT == 0 — never indexed).",
        "static const RandoChestLookupEntry kRandoChestLookup[1] = { { 0, 0, 0 } };",
        "",
        "#define kRandoChestLookup_COUNT 0",
        "",
        "#endif  // ZELDA3_RANDO_CHEST_LOOKUP_H_",
    ]
    atomic_write_text(path, "\n".join(lines) + "\n")


def emit_pot_lookup(rows, path: Path) -> int:
    """Emit src/rando/pot_lookup.h — sorted (dungeon_room, tile_position) -> LOC.

    The runtime pot dispatch (Dungeon_GetPotLocation) binary-searches this for the
    pot's stable (room, pos4) identity. Mirrors chest_lookup.h. `rows` come from
    load_pots() (pots.gen.yaml); sorted by (room, pos4) so the search is correct."""
    rows = sorted(rows, key=lambda r: (r[0], r[1]))
    digest = _pot_registry_digest(rows)
    lines = [
        HEADER_BANNER, "",
        "// pot_lookup.h — sorted (dungeon_room_index, tile_position) -> LOC_* for",
        "// the runtime pot dispatch (Dungeon_GetPotLocation). Generated from",
        "// local assets/rando/pots.gen.yaml via assets/scripts/gen_pot_tables.py.",
        "",
        "#ifndef ZELDA3_RANDO_POT_LOOKUP_H_",
        "#define ZELDA3_RANDO_POT_LOOKUP_H_",
        "",
        "#include \"../types.h\"",
        "",
        "typedef struct RandoPotLookupEntry {",
        "  uint16 room;    // 0..319 dungeon room index",
        "  uint16 pos4;    // tile_position (dsto*2 | 0x2000 BG-half), == RevealPotItem's match key",
        "  uint16 loc_id;  // LOC_*",
        "} RandoPotLookupEntry;",
        "",
    ]
    if rows:
        lines.append("static const RandoPotLookupEntry kRandoPotLookup[] = {")
        for (room, pos4, locid) in rows:
            lines.append(f"  {{ 0x{room:04x}, 0x{pos4:04x}, {locid} }},")
        lines.append("};")
    else:
        lines.append("// EMPTY: pots.gen.yaml absent. Active pot-shuffle generation fails closed.")
        lines.append("static const RandoPotLookupEntry kRandoPotLookup[1] = { { 0, 0, 0 } };")
    lines += [
        "",
        f"#define kRandoPotLookup_COUNT {len(rows)}",
        f"#define kRandoPotRegistryCount {len(rows)}",
        f"#define kRandoPotRegistryDigest 0x{digest:08x}u",
        "",
        "#endif  // ZELDA3_RANDO_POT_LOOKUP_H_",
    ]
    atomic_write_text(path, "\n".join(lines) + "\n")
    return len(rows)


def load_pot_nonpot_drop_counts(path: Path) -> list[tuple[str, int]]:
    """Load the generated non-pot small-key free-grant rows.

    Missing path is the public/assetless build: no pot registry, no pot shuffle,
    and an empty table is correct. If a local key-depth artifact exists but lacks
    the generated section, fail closed rather than silently omitting the
    DUNGEON-key free-grants.
    """
    if not path.exists():
        return []
    doc = load_yaml(path)
    rows = doc.get("nonpot_drops")
    if rows is None:
        raise RuntimeError(
            f"{path} is missing nonpot_drops. Regenerate it with "
            f"assets/scripts/gen_pot_key_depth.py so pot_nonpot_drop_counts.h "
            f"can be derived from the same key-depth artifact."
        )
    out = []
    seen = set()
    for raw in rows or []:
        item = raw["item"]
        count = int(raw["count"])
        if count <= 0:
            raise RuntimeError(f"{path}: nonpot_drops entry {item!r} has non-positive count {count}")
        if item in seen:
            raise RuntimeError(f"{path}: duplicate nonpot_drops item {item!r}")
        seen.add(item)
        out.append((item, count))
    return out


def emit_pot_nonpot_drop_counts(rows, items: dict[str, ItemDef], path: Path) -> int:
    """Emit src/rando/pot_nonpot_drop_counts.h.

    This keeps the C placer's free-grant table derived from the same local
    key-depth artifact that generates POT_KEYS_DUNGEON/POT_KEYS_WILD gates.
    """
    rows = sorted(rows, key=lambda r: items[r[0]].id if r[0] in items else 0xFFFF)
    lines = [
        HEADER_BANNER, "",
        "// pot_nonpot_drop_counts.h — non-pot small-key drops that remain free",
        "// under DUNGEON keys + active pot shuffle. Generated from",
        "// assets/rando/pot_key_depth.gen.yaml via gen_pot_key_depth.py.",
        "",
        "#ifndef ZELDA3_RANDO_POT_NONPOT_DROP_COUNTS_H_",
        "#define ZELDA3_RANDO_POT_NONPOT_DROP_COUNTS_H_",
        "",
        "#include \"../types.h\"",
        "#include \"item_ids.h\"",
        "",
        "typedef struct RandoPotNonpotDropCount {",
        "  uint16 item_id;",
        "  uint8 count;",
        "} RandoPotNonpotDropCount;",
        "",
    ]
    if rows:
        lines.append("static const RandoPotNonpotDropCount kPotNonpotDropCounts[] = {")
        for item, count in rows:
            if item not in items:
                raise RuntimeError(f"pot_nonpot_drop_counts: unknown item {item!r}")
            if count > 0xFF:
                raise RuntimeError(f"pot_nonpot_drop_counts: count for {item!r} exceeds uint8")
            lines.append(f"  {{ ITEM_{item}, {count} }},")
        lines.append("};")
    else:
        lines.append("// EMPTY: pot_key_depth.gen.yaml absent or no non-pot drops need free-granting.")
        lines.append("static const RandoPotNonpotDropCount kPotNonpotDropCounts[1] = { { 0, 0 } };")
    lines += [
        "",
        f"#define kPotNonpotDropCounts_COUNT {len(rows)}",
        "",
        "#endif  // ZELDA3_RANDO_POT_NONPOT_DROP_COUNTS_H_",
    ]
    atomic_write_text(path, "\n".join(lines) + "\n")
    return len(rows)


def emit_chest_lookup(locations: dict[str, LocationDef], path: Path) -> int:
    """Emit src/rando/chest_lookup.h — (dungeon_room, chest_ordinal) -> LOC_*.

    Per tasks.md S6.3 / audit.md S0.3.5+S0.3.7. Cross-references the embedded
    vanilla ROM chest table (assets/chest_data.py) with the canonical
    chest-name catalog at assets/rando/location_registry.yaml to produce a
    flat C array indexed by (room_index, chest_ordinal) for the runtime's
    Rando_ChestDispatch hook at src/player.c:3847.

    The runtime walks kDungeonRoomChests via the same N-th-room-match
    iteration as the in-game OpenChestForItem at src/dungeon.c:5765, so the
    `chest_ordinal` here matches `tile - 0x58` at the dispatch site.

    Returns the number of rows emitted (for diagnostic logging).
    """
    # Validate against the catalog (not the table), so name/type/coverage drift
    # is caught in CI, where the table artifact is absent.
    DEFERRED_CHEST_NAMES = {"Chest Game"}  # §6.8 minigame path
    catalog = chest_data.CHEST_NAME_BY_ROM_ADDR
    catalog_names = {name for (name, _ctype) in catalog.values()}

    # Forward: every catalog chest name resolves to a Chest/BigChest registry entry.
    unknown = []
    for addr, (name, ctype) in catalog.items():
        loc = locations.get(name)
        if loc is None:
            unknown.append((name, addr))
            continue
        if loc.type not in ("Chest", "BigChest"):
            raise RuntimeError(
                "chest_lookup: '%s' (loc %d) has registry type %r != Chest/BigChest"
                % (name, loc.id, loc.type)
            )
    if unknown:
        msg = ", ".join("%s @ 0x%04X" % (n, a) for n, a in unknown[:5])
        raise RuntimeError(
            "chest_lookup: %d chest name(s) absent from location_registry.yaml: %s%s"
            % (len(unknown), msg, " ..." if len(unknown) > 5 else "")
        )

    # Reverse: every registry Chest/BigChest must be in the catalog (minus the
    # deferred Chest Game), else its chest would silently fall through to vanilla.
    missing_in_catalog = []
    for loc in locations.values():
        if loc.type not in ("Chest", "BigChest"):
            continue
        if loc.name in DEFERRED_CHEST_NAMES:
            continue
        if loc.name not in catalog_names:
            missing_in_catalog.append((loc.name, loc.id))
    if missing_in_catalog:
        msg = ", ".join("%s (loc %d)" % (n, i) for n, i in missing_in_catalog[:5])
        raise RuntimeError(
            "chest_lookup: %d registry chest(s) absent from CHEST_NAME_BY_ROM_ADDR: %s%s"
            " — add them with PHP source provenance."
            % (len(missing_in_catalog), msg,
               " ..." if len(missing_in_catalog) > 5 else "")
        )

    # Rows need the generated chest table; absent in CI -> empty lookup (the
    # runtime falls through to vanilla for every chest).
    rows = chest_data.get_chest_lookup_rows()
    if not rows:
        # chest_table.gen.bin is absent. In a ROM-less / CI build this is
        # EXPECTED (the runtime falls through to vanilla for every chest). But in
        # a PLAYABLE build — one where zelda3_assets.dat has been extracted — an
        # empty table SILENTLY makes every chest grant its vanilla item:
        # placement/spoiler stay correct but no chest resolves to its placed item
        # → an unbeatable seed when progression sits in a chest. The two
        # artifacts come from the SAME extraction, so the split state (assets
        # present, chest bin missing — e.g. a stale incremental build) is always
        # a bug. Fail closed in that context instead of fail-open.
        import sys as _sys
        # REPO (module-global, derived from __file__) is cwd-INDEPENDENT, so the
        # guard works regardless of where the codegen is invoked from — a
        # cwd-relative probe would fail OPEN from another cwd.
        if (REPO / "zelda3_assets.dat").exists():
            _sys.stderr.write(
                "ERROR: chest_table.gen.bin is missing but zelda3_assets.dat is "
                "present — a playable build would grant EVERY chest its vanilla "
                "item (unbeatable when progression is in a chest). Re-run asset "
                "extraction (python assets/restool.py --extract-from-rom) to "
                "regenerate chest_table.gen.bin, then rebuild.\n")
            _sys.exit(2)  # hard build failure (caller reads the return as a count)
        _write_empty_chest_lookup(path)
        print(
            "  NOTE: chest_lookup.h emitted EMPTY (chest_table.gen.bin absent; "
            "expected in a ROM-less / CI build). Run asset extraction for a "
            "playable build."
        )
        return 0

    # Sort by (room, ordinal) so the C-side binary search has a sorted key.
    rows.sort(key=lambda r: (r[0], r[1]))

    lines = [
        HEADER_BANNER,
        "",
        "// chest_lookup.h — (dungeon_room_index, chest_ordinal) -> LOC_* table.",
        "//",
        "// Hooked from src/player.c:3847 Link_PerformOpenChest via",
        "// Rando_ChestDispatch in src/rando/rando.c. `chest_ordinal` is the",
        "// 0-based in-room chest index `tile - 0x58` matching src/dungeon.c:5765",
        "// OpenChestForItem's iteration (N-th matching room in",
        "// kDungeonRoomChests).",
        "//",
        "// Source: in-room ordinals from the vanilla chest table (168 x 3 bytes),",
        "// read at codegen time from the generated chest_table.gen.bin; chest",
        "// names from ALTTPR PHP app/Region/Standard/**.php, cross-checked against",
        "// location_registry.yaml.",
        "",
        "#ifndef ZELDA3_RANDO_CHEST_LOOKUP_H_",
        "#define ZELDA3_RANDO_CHEST_LOOKUP_H_",
        "",
        "#include \"../types.h\"",
        "#include \"location_ids.h\"",
        "",
        "typedef struct RandoChestLookupEntry {",
        "  uint16 room;     // 0..319 dungeon room index",
        "  uint8  ordinal;  // 0..5; in-room chest index = (tile - 0x58)",
        "  uint16 loc_id;   // LOC_*",
        "} RandoChestLookupEntry;",
        "",
        "// %d entries, sorted by (room, ordinal). 164 of 165 ALTTPR chest" % len(rows),
        "// locations (Chest Game @ ROM 0xEDA8 is the minigame path; dispatch is",
        "// tasks.md S6.8). 4 ROM chest-table entries have no ALTTPR location",
        "// (one entry each in rooms 91, 126, 179, 301 - small-key/rupee chests",
        "// that ALTTPR does not expose as shuffleable; rooms 126 and 179 each",
        "// have ONE mapped chest at ord 0 plus the unmapped ord-1). The unmapped",
        "// entries fall through to vanilla at runtime via chest_lookup()'s",
        "// 0xFFFF return.",
        "static const RandoChestLookupEntry kRandoChestLookup[] = {",
    ]
    for room, ord_, name, ctype, item, big, addr in rows:
        loc = locations[name]
        token = sanitize_for_define(name)
        big_mark = "BigChest" if big else "Chest"
        lines.append(
            "  { %3d, %d, LOC_%s },  // ROM 0x%04X item=0x%02X %s '%s'"
            % (room, ord_, token, addr, item, big_mark, name)
        )
    lines.append("};")
    lines.append("")
    lines.append("#define kRandoChestLookup_COUNT %d" % len(rows))
    lines.append("")
    lines.append("#endif  // ZELDA3_RANDO_CHEST_LOOKUP_H_")
    atomic_write_text(path, "\n".join(lines) + "\n")
    return len(rows)


def emit_icon_atlas(icon_atlas_path: Path, out_header: Path) -> int:
    """Emit src/rando/icon_atlas.h — kHashIconAtlas[N] tile-index table.

    Per tasks.md S9.4b: the 5-icon visual hash widget computes
    `index_i = SHA-256(share_string_binary)[i] mod N` for i in 0..4, then
    emits 5 OAM tiles using kHashIconAtlas[index_i] as the tile index.

    The atlas YAML defines a curated icon list (whatever pool the YAML
    pins; not constrained to 32). Bumping the atlas changes the icon
    output for every existing share string and therefore advances
    kGeneratorVersion (per randomizer-ui spec).

    Returns the entry count (for diagnostic logging).
    """
    if not icon_atlas_path.exists():
        raise RuntimeError(
            "emit_icon_atlas: missing source YAML at %s" % icon_atlas_path
        )
    with open(icon_atlas_path, "r", encoding="utf-8") as f:
        atlas_doc = yaml.safe_load(f)
    icons = atlas_doc.get("icons", [])
    if not icons:
        raise RuntimeError(
            "emit_icon_atlas: %s has no 'icons:' entries — needs at least 1"
            % icon_atlas_path
        )
    entries = []
    for i, entry in enumerate(icons):
        if not isinstance(entry, dict):
            raise RuntimeError(
                "emit_icon_atlas: entry %d in %s is not a mapping" % (i, icon_atlas_path)
            )
        if "tile" not in entry:
            raise RuntimeError(
                "emit_icon_atlas: entry %d (%r) missing required 'tile' key"
                % (i, entry.get("name", "(unnamed)"))
            )
        tile = entry["tile"]
        if not isinstance(tile, int) or tile < 0 or tile > 0xff:
            raise RuntimeError(
                "emit_icon_atlas: entry %d (%r) tile %r out of 0..0xff range"
                % (i, entry.get("name", "(unnamed)"), tile)
            )
        name = entry.get("name", "icon_%d" % i)
        entries.append((name, tile))

    lines = [
        HEADER_BANNER,
        "",
        "// icon_atlas.h - 5-icon visual hash widget tile table.",
        "//",
        "// Per tasks.md S9.4b / randomizer-ui spec: the widget computes",
        "//   index_i = SHA-256(share_string_binary)[i] mod kHashIconAtlasSize",
        "// for i in 0..4 and emits 5 OAM tiles via kHashIconAtlas[index_i].",
        "//",
        "// CRITICAL: the hash input is share_string_binary, NOT settings_hash.",
        "// Deriving from settings_hash gives every seed with the same settings",
        "// the same icons (architectural error caught in spec round 5).",
        "//",
        "// Source: assets/rando/icon_atlas.yaml. Atlas size and entry order",
        "// is the registry pin per 'randomizer-ui / Atlas size is registry-",
        "// pinned'. Bumping the atlas advances kGeneratorVersion.",
        "",
        "#ifndef ZELDA3_RANDO_ICON_ATLAS_H_",
        "#define ZELDA3_RANDO_ICON_ATLAS_H_",
        "",
        "#include \"../types.h\"",
        "",
        "// %d entries; index_i is masked by kHashIconAtlasSize at runtime." % len(entries),
        "static const uint8 kHashIconAtlas[] = {",
    ]
    for i, (name, tile) in enumerate(entries):
        lines.append("  0x%02x,  // [%d] %s" % (tile, i, name))
    lines.append("};")
    lines.append("")
    lines.append("#define kHashIconAtlasSize %d" % len(entries))
    lines.append("")
    lines.append("#endif  // ZELDA3_RANDO_ICON_ATLAS_H_")
    atomic_write_text(out_header, "\n".join(lines) + "\n")
    return len(entries)


def emit_direct_grant_icons(
    icons_yaml_path: Path,
    items: dict[str, ItemDef],
    out_header: Path,
) -> int:
    """Emit src/rando/direct_grant_icons.h — kDirectGrantIcons[ITEM__COUNT] table.

    Phase B Slice 9 — add-rando-confirmation-icons. The §7.6 direct-grant
    helper looks up the granted item id in this table. gfx=0 entries mean
    "audio-only" and the helper falls back to sound + HUD only (Phase A
    behavior). gfx!=0 entries spawn an icon-receipt ancilla that DMAs the
    item's animated-sprite bundle (exactly like the vanilla receive-item
    pickup) and draws OAM chars 0x24/0x34.

    Each entry pins three values, all derived from the vanilla receive-item
    tables in src/misc.c / src/sprite_main.c, indexed by the LttP receive code
    whose kMemoryLocationToGiveItemTo cell matches the direct-grant's
    destination RAM cell:

      gfx       - DecodeAnimatedSpriteTile_variable() index = kReceiveItemGfx
                  [lttp]. gfx == 0 is the AUDIO-ONLY sentinel (no icon).
      big       - the OAM size byte = kReceiveItem_Tab1[lttp] (0 -> 8x16
                  two-tile, 2 -> single tile).
      oam_flags - the OAM palette/priority byte = (kWishPond2_OamFlags[lttp]
                  * 2) | 0x30, matching Ancilla_ReceiveItem_Draw.

    The table is dense (sized to ITEM__COUNT) and indexed by item id so
    callers can do `kDirectGrantIcons[item_id]` without a search.

    Returns the count of items WITH a non-zero gfx (for diagnostic logging).
    """
    entries: dict[int, tuple[int, int, int, str]] = {}  # id -> (gfx, big, oam, name)
    if icons_yaml_path.exists():
        with open(icons_yaml_path, "r", encoding="utf-8") as f:
            doc = yaml.safe_load(f) or {}
        raw = doc.get("icons", {}) or {}
        if not isinstance(raw, dict):
            raise RuntimeError(
                "emit_direct_grant_icons: %s 'icons:' must be a mapping"
                % icons_yaml_path
            )
        for name, entry in raw.items():
            if name not in items:
                raise RuntimeError(
                    "emit_direct_grant_icons: %s references unknown item %r"
                    % (icons_yaml_path, name)
                )
            if not isinstance(entry, dict):
                raise RuntimeError(
                    "emit_direct_grant_icons: entry %r is not a mapping" % name
                )
            gfx = entry.get("gfx", 0)
            big = entry.get("big", 0)
            oam_flags = entry.get("oam_flags", 0)
            for fld, v in (("gfx", gfx), ("big", big), ("oam_flags", oam_flags)):
                if not isinstance(v, int) or v < 0 or v > 0xff:
                    raise RuntimeError(
                        "emit_direct_grant_icons: entry %r %s %r out of 0..0xff"
                        % (name, fld, v)
                    )
            entries[items[name].id] = (gfx, big, oam_flags, name)

    item_count = (max(it.id for it in items.values()) + 1) if items else 0

    lines = [
        HEADER_BANNER,
        "",
        "// direct_grant_icons.h - per-item icon table for the §7.6 direct-grant",
        "// confirmation ancilla (Phase B Slice 9 — add-rando-confirmation-icons).",
        "//",
        "// Indexed by item id. gfx=0 entries cause the helper to fall back to",
        "// audio + HUD only (Phase A behavior preserved). gfx!=0 entries spawn",
        "// the kAncillaType_RandoIconReceipt ancilla, which DMAs the item's",
        "// receive-animation sprite bundle and draws it above Link.",
        "//",
        "// Source: assets/rando/direct_grant_icons.yaml. gfx/big/oam_flags are",
        "// derived from the vanilla receive-item tables (kReceiveItemGfx,",
        "// kReceiveItem_Tab1, kWishPond2_OamFlags) indexed by the item's LttP",
        "// receive code, so each icon matches the vanilla pickup animation.",
        "",
        "#ifndef ZELDA3_RANDO_DIRECT_GRANT_ICONS_H_",
        "#define ZELDA3_RANDO_DIRECT_GRANT_ICONS_H_",
        "",
        "#include \"../types.h\"",
        "",
        "typedef struct DirectGrantIconEntry {",
        "  uint8 gfx;        // DecodeAnimatedSpriteTile_variable index; 0 = audio-only.",
        "  uint8 big;        // OAM size byte (0 = 8x16 two-tile, 2 = single tile).",
        "  uint8 oam_flags;  // OAM palette/priority byte.",
        "} DirectGrantIconEntry;",
        "",
        "// Indexed by item id. ITEM__COUNT-sized so direct subscripting is safe.",
        "static const DirectGrantIconEntry kDirectGrantIcons[%d] = {" % item_count,
    ]
    for it in sorted(items.values(), key=lambda i: i.id):
        if it.id in entries:
            gfx, big, oam_flags, name = entries[it.id]
            lines.append(
                "  [%d] = { 0x%02x, 0x%02x, 0x%02x },  // %s"
                % (it.id, gfx, big, oam_flags, name)
            )
    lines.append("};")
    lines.append("")
    lines.append("#endif  // ZELDA3_RANDO_DIRECT_GRANT_ICONS_H_")

    atomic_write_text(out_header, "\n".join(lines) + "\n")
    return sum(1 for (gfx, _b, _o, _name) in entries.values() if gfx != 0)


def emit_logic_data(
    locations: dict[str, LocationDef],
    regions: dict[str, RegionDef],
    edges: list[EdgeDef],
    location_predicates: dict[str, dict[str, bytes]],
    edge_predicates: list[bytes],
    path: Path,
    items: dict[str, ItemDef] | None = None,
    logic_loc_preds: dict[str, LocationDef] | None = None,
    compiled_overrides: dict[int, list[tuple[str, dict[str, bytes]]]] | None = None,
    compiled_world_state_edges: dict[int, list[tuple[EdgeDef, bytes]]] | None = None,
    trick_status_rows: list[dict] | None = None,
    glitch_status_rows: list[dict] | None = None,
    boss_kill_predicates: list[bytes] | None = None,
    dungeon_vanilla_boss: list[int] | None = None,
    cave_source_predicates: list[bytes] | None = None,
    door_vm_preds: list[bytes] | None = None,
    door_portal_rows: list[tuple] | None = None,
    door_pot_rows: list[dict] | None = None,
    door_pot_bridge_digest: int = 0,
):
    out = [HEADER_BANNER, "", "#include \"../types.h\"", "#include \"rando_logic.h\"", "#include \"location_ids.h\"", "#include \"item_ids.h\"", ""]
    # Predicate stream — concatenated; LocationDef references offset+length.
    stream = bytearray()
    location_offsets = {}
    for loc in sorted(locations.values(), key=lambda l: l.id):
        preds = location_predicates.get(loc.name, {})
        cr = preds.get("can_reach", b"\x0c\x00")  # default TRUE = AND with 0 children = 0x0c 0x00
        cp = preds.get("can_place", b"\x0c\x00")
        aa = preds.get("always_allow", b"\x0d\x00")  # default FALSE = OR with 0 children = 0x0d 0x00
        location_offsets[loc.id] = {
            "can_reach": (len(stream), len(cr)),
        }
        stream += cr
        location_offsets[loc.id]["can_place"] = (len(stream), len(cp))
        stream += cp
        location_offsets[loc.id]["always_allow"] = (len(stream), len(aa))
        stream += aa
    edge_offsets = []
    for ep in edge_predicates:
        edge_offsets.append((len(stream), len(ep)))
        stream += ep

    # Phase B Slice 2 — per-world-state location predicate overrides.
    # For each (world_state_id, loc_name, region_str_or_None, encoded) tuple
    # in compiled_overrides, append the three encoded predicates to the
    # shared `stream` and record their offsets. Capture a per-override
    # region_id when the world-state YAML moves the location (e.g.,
    # Ether Tablet East↔West in Inverted).
    # world_state_id → list[ (loc_id, region_override_id, cr_off, cp_off, aa_off) ]
    override_offsets: dict[int, list[tuple[int, int, tuple[int, int], tuple[int, int], tuple[int, int]]]] = {}
    sorted_region_ids_for_overrides = sorted(regions.keys()) if regions else []
    rid_for_overrides = {rid: i for i, rid in enumerate(sorted_region_ids_for_overrides)}
    # Map location_id → base region index (from logic_loc_region resolution
    # used in the base predicate emission above). Used to
    # detect when an override's region matches the base — in that case we
    # emit the 0xFFFF sentinel so the runtime takes the "no region change"
    # fast path. Avoids the 100%-overridden anti-pattern where the
    # sentinel path is dead code.
    base_region_idx_by_loc_id: dict[int, int] = {}
    for _loc in sorted(locations.values(), key=lambda l: l.id):
        _off = location_offsets.get(_loc.id, {})
        # Reconstruct what the base emission picked: same lookup as the
        # main loop in `emit_logic_data` (logic_loc_region → registry
        # default → 0xFFFF).
        _region_name = (logic_loc_preds or {}).get(_loc.name)
        _region_name = _region_name.region if _region_name else None
        if _region_name and _region_name in rid_for_overrides:
            base_region_idx_by_loc_id[_loc.id] = rid_for_overrides[_region_name]
        else:
            base_region_idx_by_loc_id[_loc.id] = 0xFFFF
    if compiled_overrides:
        for ws_id, entries in compiled_overrides.items():
            for loc_name, region_str, encoded in entries:
                if loc_name not in locations:
                    continue  # already warned upstream
                loc_id = locations[loc_name].id
                # Resolve region_override. 0xFFFF means "no region change"
                # (use base region from kRandoLocations). Else the override
                # moves the location to a different region for this
                # world state.
                override_region_id = 0xFFFF
                if region_str is not None and region_str in rid_for_overrides:
                    candidate_idx = rid_for_overrides[region_str]
                    base_idx = base_region_idx_by_loc_id.get(loc_id, 0xFFFF)
                    # only record an override when it differs
                    # from the base region. Matching cases emit 0xFFFF so
                    # the runtime sentinel path is exercised.
                    if candidate_idx != base_idx:
                        override_region_id = candidate_idx
                cr = encoded.get("can_reach", b"\x0c\x00")
                cp = encoded.get("can_place", b"\x0c\x00")
                aa = encoded.get("always_allow", b"\x0d\x00")
                cr_off = (len(stream), len(cr)); stream += cr
                cp_off = (len(stream), len(cp)); stream += cp
                aa_off = (len(stream), len(aa)); stream += aa
                override_offsets.setdefault(ws_id, []).append(
                    (loc_id, override_region_id, cr_off, cp_off, aa_off)
                )

    # Phase B Slice 2 — per-world-state edge predicates (Inverted overworld
    # edges, mirror-back paths). Append to the same predicate stream.
    # world_state_id → list[ (from_region_idx, to_region_idx, one_way, (off, len)) ]
    ws_edge_offsets: dict[int, list[tuple[int, int, int, tuple[int, int]]]] = {}
    if compiled_world_state_edges:
        sorted_region_ids = sorted(regions.keys()) if regions else []
        rid_index = {rid: i for i, rid in enumerate(sorted_region_ids)}
        for ws_id, entries in compiled_world_state_edges.items():
            for edge_def, encoded_pred in entries:
                ep_offset = (len(stream), len(encoded_pred))
                stream += encoded_pred
                from_idx = rid_index.get(edge_def.from_, 0xFFFF)
                to_idx = rid_index.get(edge_def.to, 0xFFFF)
                ws_edge_offsets.setdefault(ws_id, []).append(
                    (from_idx, to_idx, 1 if edge_def.one_way else 0, ep_offset)
                )

    # Boss-shuffle runtime — per-boss kill predicates (OP_CAN_KILL_BOSS). Append
    # to the same stream; kRandoBossKillPred[] references (offset, length).
    boss_kill_offsets: list[tuple[int, int]] = []
    for enc in (boss_kill_predicates or []):
        boss_kill_offsets.append((len(stream), len(enc)))
        stream += enc

    # Door shuffle — Vm-leaf predicates (door_predicates.gen.json order) +
    # portal seeding gate predicates. Appended to the same stream.
    door_vm_offsets: list[tuple[int, int]] = []
    for enc in (door_vm_preds or []):
        door_vm_offsets.append((len(stream), len(enc)))
        stream += enc
    door_portal_offsets: list[tuple] = []
    for (dgn, is_drop, door_region, fork_region, enc, pname) in (door_portal_rows or []):
        off, length = (len(stream), len(enc)) if enc else (0, 0)
        stream += enc
        door_portal_offsets.append((dgn, is_drop, door_region, fork_region, off, length, pname))
    door_pot_offsets: list[dict] = []
    door_pot_regions: list[int] = []
    for row in (door_pot_rows or []):
        enc = row.get("pred", b"")
        off, length = (len(stream), len(enc)) if enc else (0, 0)
        stream += enc
        out_row = dict(row)
        out_row["region_first"] = len(door_pot_regions)
        out_row["pred_off"] = off
        out_row["pred_len"] = length
        door_pot_regions.extend(row.get("regions", []))
        door_pot_offsets.append(out_row)

    # Cave entrance-shuffle source gates (entrance_registry.yaml order). These
    # are AND-ed into any cave/dungeon target reached through the source cave
    # slot. len 0 means the source region alone is enough.
    cave_source_offsets: list[tuple[int, int]] = []
    for enc in (cave_source_predicates or []):
        off, length = (len(stream), len(enc)) if enc else (0, 0)
        stream += enc
        cave_source_offsets.append((off, length))

    # Emit the stream as a uint8 array.
    out.append("// Predicate bytecode stream — concatenated per the encoding documented in")
    out.append("// assets/rando_logic_gen.py. Locations and edges reference (offset, length).")
    if not stream:
        stream = bytes([0])  # avoid zero-length array
    out.append(f"const uint8 kRandoPredicateStream[{len(stream)}] = {{")
    rows = []
    for i in range(0, len(stream), 16):
        chunk = stream[i:i+16]
        rows.append("  " + ", ".join(f"0x{b:02x}" for b in chunk) + ",")
    out.extend(rows)
    out.append("};")
    out.append(f"const uint32 kRandoPredicateStreamSize = {len(stream)};")
    out.append("")

    # Boss-shuffle runtime — OP_CAN_KILL_BOSS dispatch tables.
    out.append("// Boss-shuffle runtime — per-boss kill predicate table (OP_CAN_KILL_BOSS).")
    out.append("// Indexed by boss-pool index (kBoss_* in src/rando/shuffle_boss.c).")
    out.append("// Type definition: rando_logic.h::RandoBossKillPred.")
    if boss_kill_offsets:
        out.append(f"const RandoBossKillPred kRandoBossKillPred[{len(boss_kill_offsets)}] = {{")
        for bi, (off, length) in enumerate(boss_kill_offsets):
            out.append(f"  {{ {off}, {length} }},  // boss {bi}")
        out.append("};")
        out.append(f"const uint32 kRandoBossKillPredCount = {len(boss_kill_offsets)};")
    else:
        out.append("const RandoBossKillPred kRandoBossKillPred[1] = { { 0, 0 } };")
        out.append("const uint32 kRandoBossKillPredCount = 0;")
    out.append("")

    # Cave entrance-shuffle source predicates (entrance_registry.yaml order).
    out.append("// Cave entrance-shuffle source gates (entrance_registry.yaml interiors order).")
    if cave_source_offsets:
        out.append(f"const RandoCaveSourcePred kRandoCaveSourcePreds[{len(cave_source_offsets)}] = {{")
        for idx, (off, length) in enumerate(cave_source_offsets):
            out.append(f"  {{ {off}, {length} }},  // cave interior {idx}")
        out.append("};")
        out.append(f"const uint32 kRandoCaveSourcePredsCount = {len(cave_source_offsets)};")
    else:
        out.append("const RandoCaveSourcePred kRandoCaveSourcePreds[1] = { { 0, 0 } };")
        out.append("const uint32 kRandoCaveSourcePredsCount = 0;")
    out.append("")

    # Door shuffle — Vm-pred + portal-gate tables (rando_logic.h types).
    out.append("// Door-shuffle Vm-leaf predicates (door_predicates.gen.json order).")
    if door_vm_offsets:
        out.append(f"const RandoDoorVmPred kDoorVmPreds[{len(door_vm_offsets)}] = {{")
        for off, length in door_vm_offsets:
            out.append(f"  {{ {off}, {length} }},")
        out.append("};")
        out.append(f"const uint32 kDoorVmPredsCount = {len(door_vm_offsets)};")
    else:
        out.append("const RandoDoorVmPred kDoorVmPreds[1] = { { 0, 0 } };")
        out.append("const uint32 kDoorVmPredsCount = 0;")
    out.append("")
    out.append("// Door-shuffle portal seeding gates (door_portals.yaml).")
    if door_portal_offsets:
        out.append(f"const RandoDoorPortalGate kDoorPortalGates[{len(door_portal_offsets)}] = {{")
        for (dgn, is_drop, door_region, fork_region, off, length, pname) in door_portal_offsets:
            out.append(f"  {{ {dgn}, {is_drop}, {door_region}, {fork_region}, {off}, {length} }},  // {pname}")
        out.append("};")
        out.append(f"const uint32 kDoorPortalGatesCount = {len(door_portal_offsets)};")
    else:
        out.append("const RandoDoorPortalGate kDoorPortalGates[1] = { { 0, 0, 0, 0xFFFF, 0, 0 } };")
        out.append("const uint32 kDoorPortalGatesCount = 0;")
    out.append("")
    out.append("// Door x pot-shuffle bridge rows (generated from local pot artifacts).")
    if door_pot_offsets:
        out.append(f"const RandoDoorPotLocation kRandoDoorPotLocations[{len(door_pot_offsets)}] = {{")
        for r in door_pot_offsets:
            out.append(
                "  { %d, %d, %du, %d, 0x%04x, %d, %d, 0x%02x, %d },  // %s"
                % (r["loc_id"], r["region_first"], r["pred_off"], r["pred_len"],
                   r["drop_index"], r["dungeon"], r["min_tier"], r["flags"],
                   len(r.get("regions", [])), r["name"])
            )
        out.append("};")
        out.append(f"const uint16 kRandoDoorPotRegions[{len(door_pot_regions)}] = {{")
        for i in range(0, len(door_pot_regions), 12):
            chunk = door_pot_regions[i:i + 12]
            out.append("  " + ", ".join(str(r) for r in chunk) + ",")
        out.append("};")
        out.append(f"const uint32 kRandoDoorPotLocationsCount = {len(door_pot_offsets)};")
    else:
        out.append("const RandoDoorPotLocation kRandoDoorPotLocations[1] = { { 0, 0, 0, 0, 0xFFFF, 0, 0, 0, 0 } };")
        out.append("const uint16 kRandoDoorPotRegions[1] = { 0xFFFF };")
        out.append("const uint32 kRandoDoorPotLocationsCount = 0;")
    out.append(f"const uint32 kRandoDoorPotBridgeDigest = 0x{door_pot_bridge_digest & 0xFFFFFFFF:08x}u;")
    out.append("")
    out.append("// dungeon-id (HCE=0..GT=12) -> vanilla boss-pool index (0xFF = no boss).")
    out.append("// Mirrors src/rando/shuffle_boss.c kBossVanilla; OP_CAN_KILL_BOSS fallback")
    out.append("// when no per-seed boss assignment is installed.")
    dvb = list(dungeon_vanilla_boss) if dungeon_vanilla_boss else [0xFF] * 13
    out.append(f"const uint8 kRandoDungeonVanillaBoss[{len(dvb)}] = {{")
    out.append("  " + ", ".join(f"0x{b & 0xFF:02x}" for b in dvb) + ",")
    out.append("};")
    out.append("")

    # LocationDef table — typedef lives in rando_logic.h; emit only the array.
    out.append("// Location table — one row per location_registry.yaml entry.")
    out.append("// Type definition: rando_logic.h::RandoLocationDef.")
    out.append(f"const RandoLocationDef kRandoLocations[{len(locations) or 1}] = {{")
    if not locations:
        out.append("  {0, 0, 0xFFFF, 0, 0, 0, 0, 0, 0, 0, 0, 0xff},  // placeholder for zero-length array compatibility")
    else:
        sorted_region_ids = sorted(regions.keys()) if regions else []
        # logic.yaml's location entries carry an optional `region:` field.
        # The location_registry.yaml's `region:` field is descriptive only
        # (free-form string, not necessarily a logic.yaml region id) — we
        # prefer logic.yaml when present.
        logic_loc_region = {name: lp.region for name, lp in (logic_loc_preds or {}).items()}
        for loc in sorted(locations.values(), key=lambda l: l.id):
            offsets = location_offsets[loc.id]
            cr_off, cr_len = offsets["can_reach"]
            cp_off, cp_len = offsets["can_place"]
            aa_off, aa_len = offsets["always_allow"]
            type_id = _location_type_id(loc.type)
            ws_mask = _world_state_mask(loc.world_state_filter)
            # Resolve vanilla_item_id from the items registry.
            vanilla_id = 0
            if items and loc.vanilla_item and loc.vanilla_item in items:
                vanilla_id = items[loc.vanilla_item].id
            elif loc.vanilla_item:
                # Reference to an unknown item — emit 0 but flag in the comment.
                pass
            # Resolve region_id. The logic.yaml may have declared a region for
            # this location; resolve to numeric index. 0xFFFF = not declared.
            # The well-formedness pass in main() warns about non-allowlisted
            # types that hit the 0xFFFF branch — that's the silent-bypass
            # guard preventing the King Zora region-binding regression.
            region_name = logic_loc_region.get(loc.name)
            if region_name and region_name in sorted_region_ids:
                region_id = sorted_region_ids.index(region_name)
            else:
                region_id = 0xFFFF
            out.append(f"  {{ {loc.id}, {vanilla_id}, 0x{region_id:04x}, 0, {cr_off}u, {cr_len}, {cp_off}u, {cp_len}, {aa_off}u, {aa_len}, {type_id}, 0x{ws_mask:02x} }},  // {loc.name} (vanilla: {loc.vanilla_item}, region: {region_name or '-'})")
    out.append("};")
    out.append(f"const uint32 kRandoLocationsCount = {len(locations)};")
    out.append("")
    # RegionDef table — typedef lives in rando_logic.h.
    out.append("// Region table — one row per logic.yaml `regions:` entry. Phase A may emit empty.")
    out.append("// Type definition: rando_logic.h::RandoRegionDef.")
    region_list = sorted(regions.values(), key=lambda r: r.id)
    if region_list:
        out.append(f"const RandoRegionDef kRandoRegions[{len(region_list)}] = {{")
        for r in region_list:
            rid = sorted(regions.keys()).index(r.id)
            pid = sorted(regions.keys()).index(r.parent) if r.parent and r.parent in regions else 0xFFFF
            did = _dungeon_id_or_ff(r.dungeon)
            ws = _world_state_mask(r.world_state_filter)
            out.append(f"  {{ {rid}, 0x{pid:04x}, 0x{did:02x}, 0x{ws:02x} }},  // {r.id}")
        out.append("};")
    else:
        out.append("const RandoRegionDef kRandoRegions[1] = { {0, 0xFFFF, 0xFF, 0} };  // placeholder")
    out.append(f"const uint32 kRandoRegionsCount = {len(region_list)};")
    out.append("")
    # EdgeDef table — typedef lives in rando_logic.h.
    out.append("// Edge table — one row per logic.yaml `edges:` entry.")
    out.append("// Type definition: rando_logic.h::RandoEdgeDef.")
    if edges:
        out.append(f"const RandoEdgeDef kRandoEdges[{len(edges)}] = {{")
        for i, e in enumerate(edges):
            from_idx = sorted(regions.keys()).index(e.from_) if e.from_ in regions else 0xFFFF
            to_idx = sorted(regions.keys()).index(e.to) if e.to in regions else 0xFFFF
            poff, plen = edge_offsets[i]
            out.append(f"  {{ 0x{from_idx:04x}, 0x{to_idx:04x}, {poff}u, {plen}, {1 if e.one_way else 0}, 0 }},  // {e.from_} -> {e.to}")
        out.append("};")
    else:
        out.append("const RandoEdgeDef kRandoEdges[1] = { {0xFFFF, 0xFFFF, 0, 0, 0, 0} };  // placeholder")
    out.append(f"const uint32 kRandoEdgesCount = {len(edges)};")
    out.append("")

    # ----- Start region per world-state -----
    out.append("// Start region per world_state. Indexed by WorldState enum:")
    out.append("// Open=0, Standard=1, Inverted=2, Retro=3.")
    sorted_region_ids = sorted(regions.keys()) if regions else []
    # Pinned mapping (Phase A): Open/Standard/Retro start in LinksHouse;
    # Inverted starts in LinksHouse_Inverted. Falls back to 0xFFFF if not
    # declared in logic.yaml — caller treats as "empty graph".
    start_region_names = {
        0: "LinksHouse",            # Open
        1: "LinksHouse",            # Standard
        # Phase B Slice 2 §50 — Inverted starts at LinksHouse_Inverted,
        # declared in `inverted/DarkWorld/South.yaml`. A trivial edge
        # LinksHouse_Inverted → DarkWorld_South provides the overworld
        # entry. Earlier Phase A1 stopgap of DarkWorld_South as the start
        # region is replaced — the new structure mirrors Standard's
        # LinksHouse → LightWorld_South split and reserves room for
        # future Inverted entrance shuffles (Phase C).
        2: "LinksHouse_Inverted",
        3: "LinksHouse",            # Retro
    }
    starts = []
    for ws in [0, 1, 2, 3]:
        nm = start_region_names[ws]
        if nm in sorted_region_ids:
            starts.append(sorted_region_ids.index(nm))
        else:
            starts.append(0xFFFF)
    start_csv = ", ".join(f"0x{s:04x}" for s in starts)
    out.append(f"const uint16 kRandoStartRegionByWorldState[4] = {{ {start_csv} }};")
    out.append("")

    # ----- Region-name lookup table for Rando_FindRegionByName -----
    out.append("// Region-name → region-id lookup. Linear scan; only used by")
    out.append("// authoring-time helpers (Rando_FindRegionByName).")
    out.append("typedef struct RandoRegionNameEntry {")
    out.append("  const char *name;")
    out.append("  uint16 id;")
    out.append("} RandoRegionNameEntry;")
    out.append("")
    if sorted_region_ids:
        out.append(f"static const RandoRegionNameEntry kRandoRegionNames[{len(sorted_region_ids)}] = {{")
        for rid in sorted_region_ids:
            idx = sorted_region_ids.index(rid)
            out.append(f"  {{ \"{rid}\", {idx} }},")
        out.append("};")
        out.append(f"static const uint32 kRandoRegionNamesCount = {len(sorted_region_ids)};")
    else:
        out.append("static const RandoRegionNameEntry kRandoRegionNames[1] = { {\"\", 0} };")
        out.append("static const uint32 kRandoRegionNamesCount = 0;")
    out.append("")
    out.append("uint16 Rando_FindRegionByName(const char *name) {")
    out.append("  if (name == NULL) return 0xFFFF;")
    out.append("  for (uint32 i = 0; i < kRandoRegionNamesCount; i++) {")
    out.append("    const char *a = kRandoRegionNames[i].name;")
    out.append("    const char *b = name;")
    out.append("    while (*a && *a == *b) { a++; b++; }")
    out.append("    if (*a == 0 && *b == 0) return kRandoRegionNames[i].id;")
    out.append("  }")
    out.append("  return 0xFFFF;")
    out.append("}")
    out.append("")
    # Reverse lookup: region_id → name. Used by text spoiler region grouping.
    out.append("const char *Rando_GetRegionName(uint16 region_id) {")
    out.append("  for (uint32 i = 0; i < kRandoRegionNamesCount; i++) {")
    out.append("    if (kRandoRegionNames[i].id == region_id) return kRandoRegionNames[i].name;")
    out.append("  }")
    out.append("  return \"(unbound)\";")
    out.append("}")
    out.append("")
    # Location name table — used by text spoiler for human-readable rows.
    # Indexed parallel to kRandoLocations (sorted by id).
    if locations:
        out.append(f"static const char *kRandoLocationNames[{len(locations)}] = {{")
        for loc in sorted(locations.values(), key=lambda l: l.id):
            safe = loc.name.replace('"', '\\"')
            out.append(f"  \"{safe}\",  // id {loc.id}")
        out.append("};")
        out.append(f"static const uint32 kRandoLocationNamesCount = {len(locations)};")
    else:
        out.append("static const char *kRandoLocationNames[1] = { \"\" };")
        out.append("static const uint32 kRandoLocationNamesCount = 0;")
    out.append("")
    out.append("const char *Rando_GetLocationName(uint16 location_id) {")
    out.append("  for (uint32 i = 0; i < kRandoLocationsCount; i++) {")
    out.append("    if (kRandoLocations[i].id == location_id) {")
    out.append("      if (i < kRandoLocationNamesCount) return kRandoLocationNames[i];")
    out.append("      return \"(unnamed)\";")
    out.append("    }")
    out.append("  }")
    out.append("  return \"(unknown)\";")
    out.append("}")
    out.append("")

    # Item registry ID → vanilla Link_ReceiveItem dispatch code (LttP item code).
    # 0xFF = no vanilla dispatch (progressive items, dungeon items, prize items,
    # virtual items). Indexed by registry item_id.
    if items:
        max_item_id = max(i.id for i in items.values())
        # Build vanilla code map.
        dispatch_codes = [0xFF] * (max_item_id + 1)
        for it in items.values():
            d = it.dispatch
            if isinstance(d, str) and d.startswith("vanilla:"):
                try:
                    code = int(d[len("vanilla:"):], 0)
                    if 0 <= code <= 0xFF:
                        dispatch_codes[it.id] = code
                except ValueError:
                    pass
        out.append("// Registry item_id → vanilla Link_ReceiveItem dispatch code (0xFF = no")
        out.append("// vanilla path — progressive/dungeon/prize/virtual items). Used by §6")
        out.append("// grant-site dispatch wrappers to translate from rando placement ids")
        out.append("// to the LttP receive-item codes that the existing game code expects.")
        out.append(f"static const uint8 kRandoItemVanillaDispatch[{max_item_id + 1}] = {{")
        for i in range(0, len(dispatch_codes), 16):
            chunk = dispatch_codes[i:i + 16]
            out.append("  " + ", ".join(f"0x{c:02x}" for c in chunk) + ",")
        out.append("};")
        out.append(f"static const uint16 kRandoItemVanillaDispatchCount = {max_item_id + 1};")
        out.append("")
        out.append("uint8 Rando_VanillaItemForRegistryId(uint16 registry_item_id) {")
        out.append("  if (registry_item_id >= kRandoItemVanillaDispatchCount) return 0xFF;")
        out.append("  return kRandoItemVanillaDispatch[registry_item_id];")
        out.append("}")
        out.append("")

        # Item-name table — used by the spoiler writer for human-readable rows.
        # Indexed by registry item_id.
        name_by_id = [None] * (max_item_id + 1)
        for it in items.values():
            name_by_id[it.id] = it.name
        out.append("// Item registry ID → human-readable name. Used by the spoiler writer.")
        out.append(f"static const char *kRandoItemNames[{max_item_id + 1}] = {{")
        for i, nm in enumerate(name_by_id):
            safe = (nm or f"item_{i}").replace('"', '\\"')
            out.append(f"  \"{safe}\",  // id {i}")
        out.append("};")
        out.append(f"static const uint16 kRandoItemNamesCount = {max_item_id + 1};")
        out.append("")
        out.append("const char *Rando_GetItemName(uint16 item_id) {")
        out.append("  if (item_id >= kRandoItemNamesCount) return \"(unknown)\";")
        out.append("  return kRandoItemNames[item_id];")
        out.append("}")
        out.append("")

    # ----- Phase B Slice 2 — per-world-state predicate overrides -----
    # Emit one array per non-default world state that has overrides. The
    # array is sorted by location_id for binary-search lookup at runtime.
    # An empty/no-override world state emits a 1-entry placeholder so the
    # extern symbol exists for the C ABI.
    out.append("// Phase B Slice 2 — per-world-state location predicate overrides.")
    out.append("// Each non-Standard world state with Inverted-style overrides emits its own")
    out.append("// table. Sorted by location_id for binary-search lookup.")
    out.append("// Empty tables retain a 1-entry placeholder so the symbol always exists.")
    out.append("")
    for ws_id, ws_name in [(2, "Inverted"), (3, "Retro")]:
        entries = override_offsets.get(ws_id, [])
        entries_sorted = sorted(entries, key=lambda t: t[0])
        if entries_sorted:
            out.append(f"const RandoLocationPredOverride kRandoLocationPredOverrides_{ws_name}[{len(entries_sorted)}] = {{")
            for loc_id, region_override, (cr_o, cr_l), (cp_o, cp_l), (aa_o, aa_l) in entries_sorted:
                out.append(
                    "  { %d, 0x%04x, %d, %d, %d, %d, %d, %d }, " %
                    (loc_id, region_override, cr_o, cr_l, cp_o, cp_l, aa_o, aa_l)
                )
            out.append("};")
            out.append(f"const uint32 kRandoLocationPredOverrides_{ws_name}Count = {len(entries_sorted)};")
        else:
            # emit 0xFFFF in the region_override slot so the
            # placeholder matches the "no region change" sentinel rather
            # than accidentally binding loc 0 to region 0 when a future
            # entry is added without auditing the placeholder.
            out.append(f"const RandoLocationPredOverride kRandoLocationPredOverrides_{ws_name}[1] = {{ {{0, 0xFFFF, 0, 0, 0, 0, 0, 0}} }};")
            out.append(f"const uint32 kRandoLocationPredOverrides_{ws_name}Count = 0;")
        out.append("")

    # Per-world-state edges (Inverted-only entries).
    out.append("// Phase B Slice 2 — per-world-state edges (Inverted overworld + dungeon entries).")
    for ws_id, ws_name in [(2, "Inverted")]:
        entries = ws_edge_offsets.get(ws_id, [])
        if entries:
            out.append(f"const RandoEdgeDef kRandoEdges_{ws_name}[{len(entries)}] = {{")
            for from_idx, to_idx, one_way, (off, length) in entries:
                out.append(
                    "  { %d, %d, %d, %d, %d, 0 }, " %
                    (from_idx, to_idx, off, length, one_way)
                )
            out.append("};")
            out.append(f"const uint32 kRandoEdges_{ws_name}Count = {len(entries)};")
        else:
            out.append(f"const RandoEdgeDef kRandoEdges_{ws_name}[1] = {{ {{0, 0, 0, 0, 0, 0}} }};")
            out.append(f"const uint32 kRandoEdges_{ws_name}Count = 0;")
        out.append("")

    # -----------------------------------------------------------------------
    # §12.6 — ROM-version verification status tables. The runtime consults
    # these so the spoiler can warn when a seed enables a trick / glitch level
    # not yet confirmed to behave on the US 1.0 ROM. Struct + enum are declared
    # in rando_logic.h.
    # -----------------------------------------------------------------------
    tr = trick_status_rows or []
    out.append("// §12.6 — ROM-version verification status (op_registry.yaml tricks/glitch_levels).")
    out.append(f"const RandoTrickStatus kRandoTrickStatus[{max(len(tr), 1)}] = {{")
    if tr:
        for r in tr:
            out.append('  { %d, %d, "%s" },' % (r["bit"], r["status_int"], r["id"]))
    else:
        out.append('  { 0, 0, "" },')
    out.append("};")
    out.append(f"const uint32 kRandoTrickStatusCount = {len(tr)};")
    out.append("")
    gl = glitch_status_rows or []
    out.append(f"const RandoGlitchLevelStatus kRandoGlitchLevelStatus[{max(len(gl), 1)}] = {{")
    if gl:
        for r in gl:
            out.append('  { %d, %d, "%s" },' % (r["level"], r["status_int"], r["id"]))
    else:
        out.append('  { 0, 0, "" },')
    out.append("};")
    out.append(f"const uint32 kRandoGlitchLevelStatusCount = {len(gl)};")
    out.append("")

    atomic_write_text(path, "\n".join(out) + "\n")


def _location_type_id(t: str) -> int:
    # APPEND-ONLY: existing indices 0-13 are baked into kRandoLocations bytes
    # in src/rando/logic_data.c. Adding new types past 13 preserves them.
    types = ["Chest", "BigChest", "Npc", "Standing", "Pedestal", "Dash", "Dig", "Drop",
             "Fountain", "Trade", "Prize_Crystal", "Prize_Pendant", "Prize_Event", "Medallion",
             "Shop",        # 14 — Phase B Slice 3a Retro shop purchase slot
             "ShopUpgrade", # 15 — Phase B Slice 3a Capacity Upgrade (identity-placed)
             "TakeAny",     # 16 — Phase B Slice 3b Retro take-any cave slot (per-seed active subset)
             "Pot"]         # 17 — add-rando-pot-sanity dungeon pot (per-tier active subset)
    if t not in types:
        return 0
    return types.index(t)


def _world_state_mask(wsf: list) -> int:
    if not wsf:
        return 0  # 0 = all world-states (the common case)
    mask = 0
    for ws in wsf:
        # Strict — typoed names like "retros" silently demoted to Open (bit 0)
        # under the prior `.get(ws, 0)` default, invalidating every Open digest
        # at once. Raise loudly instead.
        if ws not in WORLD_STATES:
            raise ParseError(
                f"unknown world_state {ws!r} in world_state_filter — "
                f"valid: {sorted(WORLD_STATES.keys())}"
            )
        mask |= 1 << WORLD_STATES[ws]
    return mask


def _dungeon_id_or_ff(name) -> int:
    if not name:
        return 0xFF
    try:
        return _resolve_dungeon_id(name)
    except ParseError:
        return 0xFF


# ---------------------------------------------------------------------------
# Main driver
# ---------------------------------------------------------------------------
def main(argv=None):
    p = argparse.ArgumentParser(description="Generate rando C data and headers")
    p.add_argument("--strict", action="store_true", help="Fail on any well-formedness warning")
    p.add_argument("--out-headers", default=str(RANDO_SRC), help="Destination for emitted headers (default: src/rando/)")
    p.add_argument("--out-data", default=str(RANDO_SRC), help="Destination for emitted logic_data.c (default: src/rando/)")
    p.add_argument("--logic", default=None, help="Path to logic.yaml (optional)")
    p.add_argument("--macros", default=None, help="Path to macros.yaml (optional)")
    args = p.parse_args(argv)

    ops_path = RANDO_ASSETS / "op_registry.yaml"
    items_path = RANDO_ASSETS / "item_registry.yaml"
    locs_path = RANDO_ASSETS / "location_registry.yaml"
    schema_path = RANDO_ASSETS / "logic.schema.yaml"
    macros_path = Path(args.macros) if args.macros else (RANDO_ASSETS / "macros.yaml")
    logic_path = Path(args.logic) if args.logic else (RANDO_ASSETS / "logic.yaml")

    ops = load_ops(ops_path)
    # Phase B §56 — populate the OP_TRICK operand lookup. The codegen now
    # resolves `OP_TRICK(boots-clip)` to the bit-position operand byte;
    # without this load, every trick reference would fail well-formedness.
    global TRICK_BITS, TRICK_STATUS
    TRICK_BITS = load_tricks(ops_path)
    # §12.6 — ROM-version verification status for tricks + glitch levels.
    trick_status_rows, TRICK_STATUS = load_trick_status(ops_path)
    glitch_status_rows = load_glitch_levels(ops_path)
    items = load_items(items_path)
    locations = load_locations(locs_path)
    if not schema_path.exists():
        print(f"WARNING: {schema_path} not found", file=sys.stderr)
    macros = load_macros(macros_path)
    (logic_regions, logic_edges, logic_loc_preds, logic_macros,
     world_state_overrides, world_state_edges) = load_logic(logic_path)

    # add-rando-pot-sanity: merge the generated local pot registry into BOTH the
    # location set (ids / kRandoLocations rows) and the logic binding (region_id +
    # can_reach), then emit pot_lookup.h. Pot LOCs grow kRandoLocationsCount but
    # stay OUT of placement until a tier selects them — rando_placement.c skips
    # LOCTYPE_Pot in the open-location + junk-pad loops (mirroring inactive
    # Take-Any), so pot-shuffle off is placement-byte-identical (design D9).
    pot_locs, pot_lookup_rows, door_pot_sources = load_pots(
        Path("assets/rando/pots.gen.yaml"), logic_regions)
    locations.update(pot_locs)
    logic_loc_preds.update(pot_locs)

    # Merge macros from macros.yaml and logic.yaml (logic.yaml takes precedence).
    all_macros = {**macros, **logic_macros}

    # Parse macro bodies into ASTs (resolve calls / nested macros after all macros are loaded).
    parsed_macro_bodies = {}
    macro_errors = []
    for name, m in all_macros.items():
        try:
            ast = parse_predicate(m.body)
            ast = resolve_calls(ast, ops, all_macros)
            parsed_macro_bodies[name] = ast
        except ParseError as e:
            macro_errors.append(f"macro {name}: parse error: {e}")

    if macro_errors:
        for err in macro_errors:
            print(f"ERROR: {err}", file=sys.stderr)
        if args.strict:
            sys.exit(1)

    # ----- Well-formedness checks (task 3.10) -----
    all_errors = []

    ids_to_names: dict[int, list[str]] = {}
    for loc in locations.values():
        ids_to_names.setdefault(loc.id, []).append(loc.name)
    for loc_id, names in sorted(ids_to_names.items()):
        if len(names) > 1:
            all_errors.append(
                f"location id {loc_id} is assigned to multiple locations: "
                f"{', '.join(repr(n) for n in names)}"
            )

    # 1. Detect logic.yaml location overrides that don't match any registry entry —
    #    these would be silently dropped without this check, masking translation
    #    typos (e.g., underscored vs spaced names). Per the agent-discovered
    #    silent-drop bug.
    registry_loc_names = set(locations.keys())
    for override_id in logic_loc_preds.keys():
        if override_id not in registry_loc_names:
            all_errors.append(
                f"logic.yaml location override id={override_id!r} does not "
                f"match any entry in assets/rando/location_registry.yaml — "
                f"this predicate override is being dropped silently."
            )

    # 2. Detect orphan regions (parent references that don't resolve).
    if logic_regions:
        region_ids = set(logic_regions.keys())
        for r in logic_regions.values():
            if r.parent and r.parent not in region_ids:
                all_errors.append(
                    f"region {r.id!r} declares parent {r.parent!r} which is not "
                    f"a known region in logic.yaml."
                )

    # 3. Detect edges referencing unknown regions.
    if logic_regions:
        region_ids = set(logic_regions.keys())
        for i, e in enumerate(logic_edges):
            if e.from_ not in region_ids:
                all_errors.append(
                    f"edge[{i}] {e.from_!r} -> {e.to!r}: 'from' region not declared."
                )
            if e.to not in region_ids:
                all_errors.append(
                    f"edge[{i}] {e.from_!r} -> {e.to!r}: 'to' region not declared."
                )

    # 4. Warn on locations in logic.yaml that don't carry any explicit predicate
    #    (currently impossible — the loader fills defaults — but documents intent).
    #    Skipped: every loaded location has all three predicate fields.

    # 5. Detect Phase A logic.yaml location overrides that reference a logic.yaml
    #    region not in the logic.yaml regions list.
    if logic_regions:
        region_ids = set(logic_regions.keys())
        for raw_id, override in logic_loc_preds.items():
            if override.region and override.region not in region_ids:
                all_errors.append(
                    f"logic.yaml location {raw_id!r} declares region {override.region!r} "
                    f"which is not in logic.yaml regions."
                )

    # 6. Silent-region-bypass guard. A location with no `region:` field
    #    in logic.yaml or logic_parts/* gets encoded with region_id=0xFFFF,
    #    which makes it bypass region-membership reachability entirely
    #    (the predicate VM treats it as "always-reachable region"). That
    #    is CORRECT for the allowlisted types — Medallion is a generation-
    #    time config slot (not a chest), Shop locations are gated by
    #    world-state filter rather than region, CapacityUpgrade follows
    #    Shop. For ordinary chests/NPCs/standing-items it is WRONG: the
    #    location becomes reachable from sphere 0 regardless of any
    #    entry-edge predicate (e.g., losing the Standard-mode RescuedZelda
    #    gate on the LightWorld_NorthEast entry edge).
    #
    #    This bug pattern landed once when removing the
    #    logic_parts/45_lightworld_northeast.yaml duplicate-override
    #    silently dropped the region binding on King Zora et al.
    #    Warn at codegen time so the next such regression
    #    fails the build instead of being caught by playtest.
    # Types that don't require a `region:` binding because their reachability
    # gating happens via a different mechanism:
    #   - Medallion: generation-time config slot (medallion-shuffle), not a
    #     pool placement; reachability isn't checked.
    #   - Shop: gated by world_state_filter=[retro], not by region predicate.
    #   - ShopUpgrade: same as Shop (capacity-upgrade slots in Retro mode).
    #   - TakeAny: Phase B Slice 3b Retro take-any cave slots. Like Shop, these
    #     are gated by world_state_filter=[retro] and an additional per-seed
    #     "active" gate in the placer (only 9 of 62 slots emit per seed). They
    #     are reward-pinned by role, never assumed-fill targets, so region
    #     reachability isn't used to place into them. (Consistent with the 3a
    #     Shop precedent; see add-rando-retro-takeany/design.md §D4, §5.)
    #   - Prize_Event: game-event-style logic-affecting sites (Zelda rescue,
    #     Agahnim 1/2, Ganon, Bomb Merchant). Most carry a region binding in
    #     logic.yaml or logic_parts, but Bomb Merchant — Inverted-only and
    #     deferred to Slice 2 — does not yet; allowlisting Prize_Event lets
    #     the Bomb Merchant warning stay quiet until Inverted logic_parts
    #     land and its DarkWorld_South region binding is wired.
    REGION_OPTIONAL_TYPES = {"Medallion", "Shop", "ShopUpgrade", "TakeAny", "Prize_Event"}
    if logic_regions:
        region_ids = set(logic_regions.keys())
        for loc_name, loc in locations.items():
            override = logic_loc_preds.get(loc.name) or logic_loc_preds.get(loc_name)
            has_region = bool(override and override.region and override.region in region_ids)
            if not has_region and loc.type not in REGION_OPTIONAL_TYPES:
                all_errors.append(
                    f"location {loc.name!r} (type {loc.type}) has no `region:` "
                    f"binding — encoded as region_id=0xFFFF which bypasses "
                    f"region-membership reachability. Either bind a region in "
                    f"logic.yaml / logic_parts, or add `{loc.type}` to "
                    f"REGION_OPTIONAL_TYPES if this is a config-slot-style "
                    f"location that genuinely doesn't have a region."
                )

    # Compile location predicates (only those that have overrides in logic.yaml).
    location_predicates = {}
    for loc_name, loc in locations.items():
        # logic.yaml's predicate overrides take precedence; default to TRUE() / FALSE() otherwise.
        overrides = logic_loc_preds.get(loc.name) or logic_loc_preds.get(loc_name)
        cr_src = overrides.can_reach if overrides else "TRUE()"
        cp_src = overrides.can_place if overrides else "TRUE()"
        aa_src = overrides.always_allow if overrides else "FALSE()"
        encoded = {}
        for label, src in [("can_reach", cr_src), ("can_place", cp_src), ("always_allow", aa_src)]:
            try:
                ast = parse_predicate(src)
                ast = resolve_calls(ast, ops, all_macros)
                ast = expand_macros(ast, all_macros, parsed_macro_bodies, ops)
                errs = well_formedness(ast, ops, items, logic_regions, locations, label)
                if errs:
                    for e in errs:
                        all_errors.append(f"location {loc_name}.{label}: {e}")
                encoded[label] = encode_predicate(ast, ops, items, logic_regions, locations)
            except ParseError as e:
                all_errors.append(f"location {loc_name}.{label}: parse error: {e}")
                encoded[label] = b"\x0d\x00"  # safe default: FALSE
        location_predicates[loc_name] = encoded

    # Compile edge predicates.
    edge_predicates = []
    for i, e in enumerate(logic_edges):
        try:
            ast = parse_predicate(e.predicate)
            ast = resolve_calls(ast, ops, all_macros)
            ast = expand_macros(ast, all_macros, parsed_macro_bodies, ops)
            errs = well_formedness(ast, ops, items, logic_regions, locations, f"edge[{i}]")
            if errs:
                for err in errs:
                    all_errors.append(f"edge[{i}] {e.from_}->{e.to}: {err}")
            edge_predicates.append(encode_predicate(ast, ops, items, logic_regions, locations))
        except ParseError as ex:
            all_errors.append(f"edge[{i}]: parse error: {ex}")
            edge_predicates.append(b"\x0d\x00")  # safe default: FALSE

    # Phase B Slice 2 — compile per-world-state location predicate overrides.
    # Output shape: world_state_id → list[ (loc_name, region_str_or_None, encoded_dict) ]
    # Each loc_name MUST be present in `locations` (registry); unknown names
    # are dropped with a WARN (same well-formedness contract as the base path).
    # `region_str_or_None` is the override's region (from the world-state YAML);
    # None means "no region change vs the base" — the codegen emits 0xFFFF.
    compiled_overrides: dict[int, list[tuple[str, str | None, dict[str, bytes]]]] = {}
    for loc_name_or_id, ws_map in world_state_overrides.items():
        # Resolve the override key to a registry location name. Override
        # YAMLs key by location id (e.g., "Eastern Palace - Compass Chest");
        # the registry uses the same canonical names.
        if loc_name_or_id not in locations:
            all_errors.append(
                f"world-state override for {loc_name_or_id!r} does not match "
                f"a registry entry — drop or fix the location id."
            )
            continue
        for ws_id, override_def in ws_map.items():
            encoded = {}
            for label, src in [
                ("can_reach", override_def.can_reach),
                ("can_place", override_def.can_place),
                ("always_allow", override_def.always_allow),
            ]:
                try:
                    ast = parse_predicate(src)
                    ast = resolve_calls(ast, ops, all_macros)
                    ast = expand_macros(ast, all_macros, parsed_macro_bodies, ops)
                    errs = well_formedness(ast, ops, items, logic_regions, locations, label)
                    if errs:
                        for e in errs:
                            all_errors.append(
                                f"override(ws={ws_id}) {loc_name_or_id}.{label}: {e}"
                            )
                    encoded[label] = encode_predicate(ast, ops, items, logic_regions, locations)
                except ParseError as e:
                    all_errors.append(
                        f"override(ws={ws_id}) {loc_name_or_id}.{label}: parse error: {e}"
                    )
                    encoded[label] = b"\x0d\x00"  # safe default: FALSE
            # Capture the override's region if it differs from the empty
            # default. Codegen at emit time resolves the region name to
            # an index into sorted_region_ids; if the name doesn't match
            # any region OR equals the base region, region_override is set
            # to 0xFFFF.
            region_str = override_def.region if override_def.region else None
            compiled_overrides.setdefault(ws_id, []).append((loc_name_or_id, region_str, encoded))

    # Compile per-world-state edge predicates (Inverted overworld + cross-
    # region traversal). Encode and bucket by ws_id; emitted alongside base
    # edges in logic_data.c.
    compiled_world_state_edges: dict[int, list[tuple[EdgeDef, bytes]]] = {}
    for ws_id, edges_list in world_state_edges.items():
        for e in edges_list:
            try:
                ast = parse_predicate(e.predicate)
                ast = resolve_calls(ast, ops, all_macros)
                ast = expand_macros(ast, all_macros, parsed_macro_bodies, ops)
                errs = well_formedness(ast, ops, items, logic_regions, locations,
                                       f"ws-edge[{ws_id}]")
                if errs:
                    for err in errs:
                        all_errors.append(
                            f"ws-edge(ws={ws_id}) {e.from_}->{e.to}: {err}"
                        )
                compiled_world_state_edges.setdefault(ws_id, []).append(
                    (e, encode_predicate(ast, ops, items, logic_regions, locations))
                )
            except ParseError as ex:
                all_errors.append(
                    f"ws-edge(ws={ws_id}): parse error: {ex}"
                )
                compiled_world_state_edges.setdefault(ws_id, []).append(
                    (e, b"\x0d\x00")
                )

    # Boss-shuffle runtime — compile the per-boss kill predicates for
    # OP_CAN_KILL_BOSS dispatch. Same parse→resolve→expand→encode pipeline as
    # locations; each body reuses a canonical CanKill<Boss> macro so the
    # off-shuffle resolution is byte-identical to the inline predicate. The list
    # order is the boss-pool-index contract (see BOSS_KILL_BODIES).
    boss_kill_predicates = []
    for bi, body in enumerate(BOSS_KILL_BODIES):
        try:
            ast = parse_predicate(body)
            ast = resolve_calls(ast, ops, all_macros)
            ast = expand_macros(ast, all_macros, parsed_macro_bodies, ops)
            errs = well_formedness(ast, ops, items, logic_regions, locations,
                                   f"boss_kill[{bi}]")
            if errs:
                for e in errs:
                    all_errors.append(f"boss_kill[{bi}] ({body}): {e}")
            boss_kill_predicates.append(encode_predicate(ast, ops, items, logic_regions, locations))
        except ParseError as e:
            all_errors.append(f"boss_kill[{bi}] ({body}): parse error: {e}")
            boss_kill_predicates.append(b"\x0d\x00")  # safe default: FALSE

    # Cave entrance-shuffle source gates from entrance_registry.yaml. Omitted or
    # TRUE() rows encode as len 0, meaning the source region alone gates the slot.
    cave_source_predicates: list[bytes] = []
    for idx, (interior_id, src) in enumerate(
            load_cave_source_predicates(RANDO_ASSETS / "entrance_registry.yaml")):
        if src.strip() == "TRUE()":
            cave_source_predicates.append(b"")
            continue
        try:
            ast = parse_predicate(src)
            ast = resolve_calls(ast, ops, all_macros)
            ast = expand_macros(ast, all_macros, parsed_macro_bodies, ops)
            errs = well_formedness(ast, ops, items, logic_regions, locations,
                                   f"cave_source[{idx}]")
            if errs:
                for e in errs:
                    all_errors.append(f"cave_source[{idx}] {interior_id}: {e}")
            cave_source_predicates.append(encode_predicate(ast, ops, items, logic_regions, locations))
        except ParseError as e:
            all_errors.append(f"cave_source[{idx}] {interior_id}: parse error: {e}")
            cave_source_predicates.append(b"\x0d\x00")  # safe default: FALSE

    if all_errors:
        for err in all_errors:
            print(f"WARN: {err}", file=sys.stderr)
        if args.strict:
            sys.exit(1)

    # --- Door shuffle (add-rando-door-shuffle) -----------------------------
    # Compile the door-table Vm-leaf predicates + portal gates, and wrap each
    # door-controlled location's can_reach as
    #   (NOT DOORS_ACTIVE(d) AND vanilla) OR (DOORS_ACTIVE(d) AND DOORS_LOC_REACHABLE(loc))
    # — evaluates to exactly the vanilla bytes' result while inactive.
    # This block runs AFTER the strict gate above, so its errors get their own
    # flush below (an unknown macro otherwise compiled a door predicate to
    # constant FALSE silently, even under --strict).
    n_errors_pre_door = len(all_errors)
    door_vm_preds: list[bytes] = []
    door_portal_rows: list[tuple] = []
    door_pot_rows: list[dict] = []
    door_pot_bridge_digest = 0
    door_manifest_path = Path("assets/rando/door_predicates.gen.json")
    door_portals_path = Path("assets/rando/door_portals.yaml")
    if door_manifest_path.exists():
        import json as _json
        door_man = _json.loads(door_manifest_path.read_text())

        def compile_door_src(src: str, label: str) -> bytes:
            try:
                d_ast = parse_predicate(src)
                d_ast = resolve_calls(d_ast, ops, all_macros)
                d_ast = expand_macros(d_ast, all_macros, parsed_macro_bodies, ops)
                d_errs = well_formedness(d_ast, ops, items, logic_regions, locations, "can_reach")
                if d_errs:
                    for e in d_errs:
                        all_errors.append(f"door {label}: {e}")
                return encode_predicate(d_ast, ops, items, logic_regions, locations)
            except ParseError as e:
                all_errors.append(f"door {label}: parse error: {e}")
                return b"\x0d\x00"  # FALSE — conservative (unparsed => impassable)

        for di, src in enumerate(door_man.get("predicates", [])):
            door_vm_preds.append(compile_door_src(src, f"vmpred[{di}] {src[:60]!r}"))

        if door_portals_path.exists():
            door_portals_doc = yaml.safe_load(door_portals_path.read_text())
            gates_by_name = {p["name"]: p for p in door_portals_doc.get("portals", [])}
            _sorted_rids = sorted(logic_regions.keys()) if logic_regions else []
            _rid_index = {r: i for i, r in enumerate(_sorted_rids)}
            for p in door_man.get("portals", []):
                g = gates_by_name.get(p["name"])
                if g is None:
                    all_errors.append(f"door portal {p['name']!r}: no door_portals.yaml row")
                    continue
                fr = g.get("fork_region")
                if fr is None:
                    fr_idx = 0xFFFF
                else:
                    fr_idx = _rid_index.get(fr)
                    if fr_idx is None:
                        all_errors.append(f"door portal {p['name']!r}: unknown fork region {fr!r}")
                        continue
                enc = compile_door_src(g["predicate"], f"portal {p['name']}") if g.get("predicate") else b""
                door_portal_rows.append((p["dungeon"], p["is_drop"], p["door_region"],
                                         fr_idx, enc, p["name"]))
        else:
            all_errors.append("door_portals.yaml missing (door tables present)")

        if door_pot_sources:
            door_pot_rows, door_pot_bridge_digest = build_door_pot_bridge(
                door_pot_sources, compile_door_src)

        _OP_AND, _OP_OR, _OP_NOT, _OP_DA, _OP_DLR = 12, 13, 14, 20, 21
        _loc_by_id = {l.id: (name, l) for name, l in locations.items()}
        _door_wrapped_ids = set()

        def wrap_door_location(fid: int, dgn: int, label: str) -> None:
            pair = _loc_by_id.get(fid)
            if pair is None:
                all_errors.append(f"door location id {fid}: not in location registry")
                return
            loc_key, _loc = pair
            enc = location_predicates.get(loc_key)
            if enc is None:
                all_errors.append(f"door location {loc_key!r}: no compiled predicates")
                return
            if fid in _door_wrapped_ids:
                return
            _door_wrapped_ids.add(fid)
            v = enc["can_reach"]
            enc["can_reach"] = (bytes([_OP_OR, 2, _OP_AND, 2, _OP_NOT, _OP_DA, dgn]) + v +
                                bytes([_OP_AND, 2, _OP_DA, dgn,
                                       _OP_DLR, fid & 0xFF, (fid >> 8) & 0xFF]))

        for entry in door_man.get("locations", []):
            wrap_door_location(entry["fork_id"], entry["dungeon"], "door")
        for row in door_pot_rows:
            wrap_door_location(row["loc_id"], row["dungeon"], "door pot")

    # Flush door-compile errors collected after the main strict gate (earlier
    # errors were already printed there; only the door block's are new).
    if len(all_errors) > n_errors_pre_door:
        for err in all_errors[n_errors_pre_door:]:
            print(f"ERROR: {err}", file=sys.stderr)
        # A door predicate that fails to compile becomes a constant-FALSE edge,
        # which silently walls off door-shuffle reachability (the silver-arrows
        # bug class) and can certify placements against a wrong graph. That must
        # never ship, so fail the build UNCONDITIONALLY — not only under CI
        # --strict — because the local Make / MSBuild codegen invokes this script
        # WITHOUT --strict. The committed door_predicates.gen.json
        # compiles clean, so this never fires on an unmodified tree.
        sys.exit(1)

    # Emit artifacts.
    out_headers = Path(args.out_headers)
    out_data = Path(args.out_data)
    out_headers.mkdir(parents=True, exist_ok=True)
    out_data.mkdir(parents=True, exist_ok=True)
    emit_location_ids(locations, out_headers / "location_ids.h")
    emit_item_ids(items, out_headers / "item_ids.h")
    emit_logic_data(locations, logic_regions, logic_edges, location_predicates, edge_predicates,
                    out_data / "logic_data.c", items=items, logic_loc_preds=logic_loc_preds,
                    compiled_overrides=compiled_overrides,
                    compiled_world_state_edges=compiled_world_state_edges,
                    trick_status_rows=trick_status_rows,
                    glitch_status_rows=glitch_status_rows,
                    boss_kill_predicates=boss_kill_predicates,
                    dungeon_vanilla_boss=DUNGEON_VANILLA_BOSS,
                    cave_source_predicates=cave_source_predicates,
                    door_vm_preds=door_vm_preds,
                    door_portal_rows=door_portal_rows,
                    door_pot_rows=door_pot_rows,
                    door_pot_bridge_digest=door_pot_bridge_digest)
    chest_lookup_count = emit_chest_lookup(locations, out_headers / "chest_lookup.h")
    pot_lookup_count = emit_pot_lookup(pot_lookup_rows, out_headers / "pot_lookup.h")
    pot_nonpot_drop_count = emit_pot_nonpot_drop_counts(
        load_pot_nonpot_drop_counts(RANDO_ASSETS / "pot_key_depth.gen.yaml"),
        items,
        out_headers / "pot_nonpot_drop_counts.h",
    )
    icon_atlas_count = emit_icon_atlas(
        Path("assets/rando/icon_atlas.yaml"),
        out_headers / "icon_atlas.h",
    )
    direct_grant_icon_count = emit_direct_grant_icons(
        Path("assets/rando/direct_grant_icons.yaml"),
        items,
        out_headers / "direct_grant_icons.h",
    )

    print(f"generated location_ids.h ({len(locations)} locations)")
    print(f"generated item_ids.h ({len(items)} items)")
    print(f"generated logic_data.c ({len(logic_regions)} regions, {len(logic_edges)} edges, {len(locations)} locations)")
    print(f"generated chest_lookup.h ({chest_lookup_count} chest entries)")
    print(f"generated pot_lookup.h ({pot_lookup_count} pot entries)")
    print(f"generated pot_nonpot_drop_counts.h ({pot_nonpot_drop_count} free-grant entries)")
    print(f"generated icon_atlas.h ({icon_atlas_count} icon entries)")
    print(f"generated direct_grant_icons.h ({direct_grant_icon_count} mapped icons)")
    print(f"warnings: {len(all_errors)}, macro errors: {len(macro_errors)}")


if __name__ == "__main__":
    main()
