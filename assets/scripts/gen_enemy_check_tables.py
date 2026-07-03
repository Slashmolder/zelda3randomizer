#!/usr/bin/env python3
"""Generate the local ordinary enemy-check registry.

Reads the packed dungeon and overworld sprite assets from zelda3_assets.dat,
reuses the enemy-shuffle constraint table as the first-pass "safe enemy" oracle,
and writes assets/rando/enemy_checks.gen.yaml for rando_logic_gen.py.

The dungeon tier remains conservative: rooms without a key-depth ROOM row are
not emitted, even if they live in the dungeon sprite table, because those are
typically cave/interior sprite lists that do not have dungeon door-region logic.
The all tier appends static authored overworld enemies whose runtime identity is
stable under the vanilla sprite-list stage.
"""
from __future__ import annotations

import argparse
import difflib
import hashlib
import itertools
import math
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path

import yaml

SCRIPT_DIR = Path(__file__).resolve().parent
REPO = SCRIPT_DIR.parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

from audit_enemy_check_candidates import (  # noqa: E402
    SHUFFLE_ENEMIES_C,
    collect_dungeon_candidates,
    collect_overworld_candidates,
    forced_drop_source_set,
    load_entry_room_predicates,
    load_pot_room_predicates,
    load_region_only_pot_rooms,
    parse_enemy_constraints,
)
from gen_enemy_drop_tables import (  # noqa: E402
    BIG_KEY_BINDINGS,
    DEFAULT_ASSETS,
    DEFAULT_KEY_DEPTH,
    SMALL_KEY_BINDINGS,
    SMALL_KEY_ITEMS,
    parse_key_depth,
    read_assets,
)

DEFAULT_OUT = REPO / "assets" / "rando" / "enemy_checks.gen.yaml"
POT_REGISTRY = REPO / "assets" / "rando" / "pots.gen.yaml"
ENTRANCE_REGISTRY = REPO / "assets" / "rando" / "entrance_registry.yaml"
SPRITE_C = REPO / "src" / "sprite.c"
SPRITE_MAIN_C = REPO / "src" / "sprite_main.c"
ENEMY_CHECK_BASE_ID = 1400
THROWN_POT_DAMAGE_TYPE = 3
SPECIAL_DAMAGE_MIN = 249

DUNGEON_REGIONS = {
    0: "HyruleCastleEscape",
    1: "EasternPalace_Lobby",
    2: "DesertPalace_Lobby",
    3: "TowerOfHera_Lobby",
    4: "HyruleCastleTower",
    5: "PalaceOfDarkness",
    6: "SwampPalace",
    7: "SkullWoods",
    8: "ThievesTown",
    9: "IcePalace_Lobby",
    10: "MiseryMire_Lobby",
    11: "TurtleRock_Lobby",
    12: "GanonsTower_Lobby",
}

# Coarse overworld screen -> logic-region binding for static all-tier enemies.
# This map intentionally covers only screens that currently emit eligible rows;
# if a future asset/table change produces a new eligible screen, generation
# fails closed until its source-region binding is reviewed.
OVERWORLD_REGIONS = {
    # Light World north west: Lost Woods, Kakariko, graveyard, smithy side.
    0: "LightWorld_NorthWest",
    2: "LightWorld_NorthWest",
    16: "LightWorld_NorthWest",
    17: "LightWorld_NorthWest",
    18: "LightWorld_NorthWest",
    19: "LightWorld_NorthWest",
    20: "LightWorld_NorthWest",
    24: "LightWorld_NorthWest",
    26: "LightWorld_NorthWest",
    34: "LightWorld_NorthWest",
    40: "LightWorld_NorthWest",
    41: "LightWorld_NorthWest",

    # Light World north east: Hyrule Castle/Eastern/Zora-side overworld.
    15: "LightWorld_NorthEast",
    22: "LightWorld_NorthEast",
    23: "LightWorld_NorthEast",
    30: "LightWorld_NorthEast",
    37: "LightWorld_NorthEast",
    45: "LightWorld_NorthEast",
    46: "LightWorld_NorthEast",
    47: "LightWorld_NorthEast",
    129: "LightWorld_NorthEast",

    # Hyrule Castle exterior guards use the escape region so Standard-mode
    # pre-rescue guards are not gated behind the post-rescue overworld edge.
    27: "HyruleCastleEscape",
    29: "HyruleCastleEscape",

    # Light World south: Link's House, desert, swamp, and Lake Hylia side.
    21: "LightWorld_South",
    43: "LightWorld_South",
    44: "LightWorld_South",
    48: "LightWorld_South",
    50: "LightWorld_South",
    51: "LightWorld_South",
    52: "LightWorld_South",
    53: "LightWorld_South",
    55: "LightWorld_South",
    58: "LightWorld_South",
    59: "LightWorld_South",
    60: "LightWorld_South",
    63: "LightWorld_South",

    # Light World death mountain.
    3: "LightWorld_DeathMountain_West",
    5: "LightWorld_DeathMountain_West",
    10: "LightWorld_DeathMountain_West",

    # Dark World north west: Skull Woods / Village of Outcasts side.
    64: "DarkWorld_NorthWest",
    66: "DarkWorld_NorthWest",
    80: "DarkWorld_NorthWest",
    81: "DarkWorld_NorthWest",
    82: "DarkWorld_NorthWest",
    83: "DarkWorld_NorthWest",
    84: "DarkWorld_NorthWest",
    88: "DarkWorld_NorthWest",
    90: "DarkWorld_NorthWest",

    # Dark World north east: pyramid, dark palace maze, and Zora-side dark world.
    79: "DarkWorld_NorthEast",
    86: "DarkWorld_NorthEast",
    87: "DarkWorld_NorthEast",
    91: "DarkWorld_NorthEast",
    93: "DarkWorld_NorthEast",
    94: "DarkWorld_NorthEast",
    101: "DarkWorld_NorthEast",
    109: "DarkWorld_NorthEast",
    110: "DarkWorld_NorthEast",
    111: "DarkWorld_NorthEast",

    # Dark World south and Mire.
    85: "DarkWorld_South",
    107: "DarkWorld_South",
    108: "DarkWorld_South",
    114: "DarkWorld_South",
    115: "DarkWorld_South",
    116: "DarkWorld_South",
    117: "DarkWorld_South",
    119: "DarkWorld_South",
    123: "DarkWorld_South",
    124: "DarkWorld_South",
    127: "DarkWorld_South",
    122: "DarkWorld_Mire",

    # Dark World death mountain west.
    74: "DarkWorld_DeathMountain_West",
}

# The enemy-shuffle table establishes which source types are safe ordinary enemy
# checks. This table tightens only source types whose real kill requirement is
# proven narrower than the generic ALTTPR firepower predicate.
SPECIAL_INVENTORY_KILL_PREDICATES = {}

# Some sprite prep routines replace kSpriteInit_Health at runtime. Use the
# maximum vanilla prep value so thrown-pot counts never understate HP.
PREP_HEALTH_ARRAYS = {
    0x6D: (SPRITE_MAIN_C, "kSpriteRat_Health"),
    0x6E: (SPRITE_MAIN_C, "kSpriteRope_Health"),
}


def die(msg: str) -> None:
    print(f"gen_enemy_check_tables: ERROR: {msg}", file=sys.stderr)
    raise SystemExit(2)


def room_key_depth_rows(key_depth: dict[str, dict]) -> dict[int, list[dict]]:
    out: dict[int, list[dict]] = defaultdict(list)
    for (dungeon, room), hits in key_depth["rooms"].items():
        for raw in hits:
            depth = int(raw["key_depth"])
            mindepth = int(raw["key_mindepth"])
            if depth < 0 or mindepth < 0:
                continue
            out[int(room)].append({
                "dungeon": int(dungeon),
                "room": int(room),
                "region": int(raw["region"]),
                "key_depth": depth,
                "key_mindepth": mindepth,
            })
    return dict(out)


def best_room_key_depth(room_rows: list[dict]) -> dict:
    if not room_rows:
        raise ValueError("best_room_key_depth called without rows")
    return max(room_rows, key=lambda r: (int(r["key_depth"]), int(r["key_mindepth"]), int(r["region"])))


def kill_predicate_for_dungeon(dungeon: int) -> str:
    if dungeon == 0:
        return "CanKillEscapeThings(world)"
    if dungeon == 4:
        return "CanKillMostThings(world, 8)"
    return "CanKillMostThings(world, 5)"


def kill_predicate_for_overworld(region: str) -> str:
    if region == "HyruleCastleEscape":
        return "CanKillEscapeThings(world)"
    return "CanKillMostThings(world, 5)"


def overworld_stage_gate(stage: int) -> str:
    # Runtime selects stage 0 while sram_progress_indicator < 2, stage 1 at
    # post-escape progress 2, and stage 2 at post-Agahnim progress 3.
    # Open/Retro start post-escape and pregrant RescuedZelda, matching stage 1.
    if stage == 0:
        return "TRUE()"
    if stage == 1:
        return "HAS_ITEM(RescuedZelda)"
    return "HAS_ITEM(DefeatAgahnim)"


def parse_c_uint8_array(path: Path, name: str) -> list[int]:
    text = path.read_text(encoding="utf-8", errors="replace")
    m = re.search(
        rf"(?:static\s+const|const)\s+uint8\s+{re.escape(name)}\[[^\]]+\]\s*=\s*\{{(.*?)\}};",
        text,
        re.S,
    )
    if not m:
        die(f"{path}: could not parse uint8 array {name}")
    body = re.sub(r"//.*", "", m.group(1))
    return [int(x, 0) for x in re.findall(r"0x[0-9A-Fa-f]+|\d+", body)]


def enemy_damage_subclasses(assets: dict[str, bytes]) -> list[int]:
    raw = assets.get("kEnemyDamageData")
    if raw is None:
        die("zelda3_assets.dat is missing kEnemyDamageData")
    out = []
    for b in raw:
        out.append(b >> 4)
        out.append(b & 0x0F)
    if len(out) < 16:
        die("kEnemyDamageData is too small to contain sprite damage rows")
    return out


def thrown_pot_requirements(assets: dict[str, bytes], source_types: set[int]) -> dict[int, dict]:
    """Return per-source-type thrown-pot damage metadata.

    Indoor thrown pots reach ThrownSprite_CheckDamageToSingleSprite(), which
    applies damage preset 3. The engine maps sprite type + damage type -> enemy
    damage subclass, then subclass -> actual HP damage or a special non-lethal
    status code. We count only normal positive HP damage; stun/transform/freeze-
    style special codes are not treated as kills.
    """
    health = parse_c_uint8_array(SPRITE_C, "kSpriteInit_Health")
    enemy_damages = parse_c_uint8_array(SPRITE_C, "kEnemyDamages")
    subclasses = enemy_damage_subclasses(assets)
    out = {}
    for typ in sorted(source_types):
        if typ >= len(health):
            die(f"{SPRITE_C}: kSpriteInit_Health lacks source type 0x{typ:02x}")
        if typ * 16 + THROWN_POT_DAMAGE_TYPE >= len(subclasses):
            die(f"kEnemyDamageData lacks thrown-pot damage row for source type 0x{typ:02x}")
        hp = int(health[typ])
        prep_health = PREP_HEALTH_ARRAYS.get(typ)
        if prep_health is not None:
            prep_path, prep_array = prep_health
            hp = max(hp, max(parse_c_uint8_array(prep_path, prep_array)))
        subclass = int(subclasses[typ * 16 + THROWN_POT_DAMAGE_TYPE])
        idx = THROWN_POT_DAMAGE_TYPE * 8 + subclass
        if idx >= len(enemy_damages):
            die(f"{SPRITE_C}: kEnemyDamages lacks damage type {THROWN_POT_DAMAGE_TYPE} subclass {subclass}")
        damage = int(enemy_damages[idx])
        pots_needed = None
        if hp not in (0, 255) and 0 < damage < SPECIAL_DAMAGE_MIN:
            pots_needed = int(math.ceil(hp / damage))
        out[typ] = {
            "damage_type": THROWN_POT_DAMAGE_TYPE,
            "damage_subclass": subclass,
            "damage": damage,
            "health": hp,
            "pots_needed": pots_needed,
        }
    return out


def unique_preserve_order(values) -> list:
    seen = set()
    out = []
    for value in values:
        if value in seen:
            continue
        seen.add(value)
        out.append(value)
    return out


def strip_throwable_combat_terms(expr: str) -> str:
    """Relax generic room-combat gates when pots themselves are the kill route.

    Pot rows often inherit room-level CanKill* gates from the location logic.
    Those gates are correct when collecting a pot item after clearing a room, but
    they are circular when the pot is being used as the weapon. Keep non-generic
    gates such as keys, Lamp/dark-room navigation, BigKey, Hammer, Bow puzzles,
    etc.; remove only the broad generic combat macros that this generator replaces
    with per-enemy kill logic.
    """
    out = str(expr or "TRUE()")
    terms = [
        r"CanKillMostThings\(\s*world\s*,\s*\d+\s*\)",
        r"CanKillEscapeThings\(\s*world\s*\)",
    ]
    for term in terms:
        out = re.sub(rf"\s+AND\s+{term}", "", out)
        out = re.sub(rf"{term}\s+AND\s+", "", out)
        out = re.sub(rf"^\s*{term}\s*$", "TRUE()", out)
        out = re.sub(rf"\(\s*{term}\s*\)", "TRUE()", out)
    out = re.sub(r"\(\s*TRUE\(\)\s*\)", "TRUE()", out)
    out = re.sub(r"TRUE\(\)\s+AND\s+", "", out)
    out = re.sub(r"\s+AND\s+TRUE\(\)", "", out)
    return out.strip() or "TRUE()"


def and_predicate(parts: list[str]) -> str:
    clean = [p for p in parts if p and p != "TRUE()"]
    if not clean:
        return "TRUE()"
    if len(clean) == 1:
        return clean[0]
    return " AND ".join(f"({p})" for p in clean)


def or_predicate(parts: list[str]) -> str:
    clean = unique_preserve_order([p for p in parts if p and p != "FALSE()"])
    if not clean:
        return "FALSE()"
    if any(p == "TRUE()" for p in clean):
        return "TRUE()"
    if len(clean) == 1:
        return clean[0]
    return " OR ".join(f"({p})" for p in clean)


def room_pot_predicate(room: int, pot_predicates: dict[int, list[dict]]) -> str | None:
    preds = unique_preserve_order(
        p.get("can_reach") for p in pot_predicates.get(room, []) if p.get("can_reach")
    )
    if not preds:
        return None
    if len(preds) == 1:
        return str(preds[0])
    return " OR ".join(f"({p})" for p in preds)


def room_pot_rows(path: Path) -> dict[int, list[dict]]:
    if not path.exists():
        return {}
    doc = yaml.safe_load(path.read_text(encoding="utf-8")) or {}
    out: dict[int, list[dict]] = defaultdict(list)
    for raw in doc.get("pots", []) or []:
        if "room" not in raw:
            continue
        out[int(raw["room"])].append(dict(raw))
    return dict(out)


def throwable_pot_predicate(room: int, count: int, pot_rows: dict[int, list[dict]]) -> tuple[str | None, int]:
    rows = pot_rows.get(room, [])
    if count <= 0:
        return "TRUE()", len(rows)
    if len(rows) < count:
        return None, len(rows)

    grouped: Counter[str] = Counter()
    for row in rows:
        grouped[strip_throwable_combat_terms(row.get("can_reach") or "TRUE()")] += 1
    groups = sorted(grouped.items(), key=lambda kv: kv[0])
    if len(groups) > 10:
        die(f"room 0x{room:03x} has too many distinct throwable-pot predicates ({len(groups)})")

    branches = []
    group_indices = range(len(groups))
    for size in range(1, len(groups) + 1):
        for combo in itertools.combinations(group_indices, size):
            if sum(groups[i][1] for i in combo) < count:
                continue
            branch = and_predicate([groups[i][0] for i in combo])
            branches.append(branch)
    return or_predicate(branches), len(rows)


def reviewed_room_binding(room: int) -> dict | None:
    if room in SMALL_KEY_BINDINGS:
        return SMALL_KEY_BINDINGS[room]
    if room in BIG_KEY_BINDINGS:
        return BIG_KEY_BINDINGS[room]
    return None


def enemy_inventory_kill_predicate(source_type: int, dungeon: int) -> tuple[str, str]:
    if source_type in SPECIAL_INVENTORY_KILL_PREDICATES:
        return SPECIAL_INVENTORY_KILL_PREDICATES[source_type], "source_type_special"
    return kill_predicate_for_dungeon(dungeon), "source_type_generic"


def display_path(path: Path) -> str:
    try:
        return path.relative_to(REPO).as_posix()
    except ValueError:
        return str(path)


def base_can_reach(room: int, dungeon: int, pot_predicates: dict[int, list[dict]],
                   region_only_pot_rooms: dict[int, str],
                   entry_room_predicates: dict[int, dict]) -> tuple[str | None, str]:
    binding = reviewed_room_binding(room)
    if binding is not None and binding.get("can_reach"):
        return str(binding["can_reach"]), "forced_key_room_binding"

    pot_pred = room_pot_predicate(room, pot_predicates)
    if pot_pred is not None:
        return pot_pred, "pot_room"

    entry = entry_room_predicates.get(room)
    if entry is not None and entry.get("entry_region") == DUNGEON_REGIONS.get(dungeon):
        return "TRUE()", "dungeon_entry_room"

    if region_only_pot_rooms.get(room) == DUNGEON_REGIONS.get(dungeon):
        return "TRUE()", "pot_room_region_only"

    # Key-depth ROOM rows prove only small-key door depth. They do not carry the
    # non-key room route predicate (Big Key, Hookshot, Hammer, dark navigation,
    # switch state, etc.) and the current logic graph has coarse dungeon lobby
    # regions rather than one region per door-table room. Keep those candidates
    # audit-only until a first-class door-region reach op can model them.
    return None, "key_depth_room_audit_only"


def enemy_can_reach(candidate: dict, dungeon: int, pot_predicates: dict[int, list[dict]],
                    region_only_pot_rooms: dict[int, str], entry_room_predicates: dict[int, dict],
                    pot_rows: dict[int, list[dict]],
                    pot_requirements: dict[int, dict]) -> dict | None:
    room = int(candidate["room"])
    base_pred, source = base_can_reach(
        room, dungeon, pot_predicates, region_only_pot_rooms, entry_room_predicates)
    if not base_pred:
        return None
    base_access = strip_throwable_combat_terms(base_pred)
    source_type = int(candidate["source_type"])
    inventory_pred, inventory_source = enemy_inventory_kill_predicate(source_type, dungeon)
    kill_branches = [inventory_pred]

    pot_meta = dict(pot_requirements.get(source_type) or {})
    pot_need = pot_meta.get("pots_needed")
    pot_pred = None
    pot_count = 0
    if pot_need is not None:
        pot_pred, pot_count = throwable_pot_predicate(room, int(pot_need), pot_rows)
        if pot_pred:
            kill_branches.append(pot_pred)
    else:
        pot_count = len(pot_rows.get(room, []))

    kill_pred = or_predicate(kill_branches)
    return {
        "can_reach": and_predicate([base_access, kill_pred]),
        "base_can_reach": base_access,
        "base_predicate_source": source,
        "kill_predicate": kill_pred,
        "inventory_kill_predicate": inventory_pred,
        "inventory_kill_source": inventory_source,
        "throwable_pots_required": pot_need,
        "throwable_pots_in_room": pot_count,
        "throwable_pots_can_reach": pot_pred,
        "throwable_pot_damage": pot_meta.get("damage"),
        "throwable_pot_damage_subclass": pot_meta.get("damage_subclass"),
        "enemy_health": pot_meta.get("health"),
    }


def enemy_check_name(row: dict, ordinal: int) -> str:
    return (
        f"Enemy Check - Room 0x{int(row['room']):03X} "
        f"Slot {int(row['source_slot']):02d} - {row['source_name']}"
    )


def overworld_enemy_check_name(row: dict) -> str:
    return (
        f"Enemy Check - Overworld 0x{int(row['area']):02X} "
        f"Stage {int(row['stage'])} Slot {int(row['source_slot']):02d} - "
        f"{row['source_name']}"
    )


def make_doc(assets_path: Path, key_depth_path: Path,
             dungeon_rows: list[dict], excluded_counts: Counter,
             overworld_rows: list[dict], overworld_excluded_counts: Counter,
             key_depth: dict[str, dict], pot_predicates: dict[int, list[dict]],
             region_only_pot_rooms: dict[int, str], entry_room_predicates: dict[int, dict],
             pot_rows: dict[int, list[dict]],
             pot_requirements: dict[int, dict]) -> dict:
    by_room = room_key_depth_rows(key_depth)
    rows = []
    doc_excluded_counts = Counter(excluded_counts)
    out_of_scope_no_key_depth = []
    audit_only_no_room_predicate = []
    emitted_by_room: Counter[int] = Counter()
    no_key_depth_by_room: Counter[int] = Counter()
    audit_only_by_room: Counter[int] = Counter()

    for candidate in sorted(dungeon_rows, key=lambda r: (int(r["room"]), int(r["source_slot"]))):
        room = int(candidate["room"])
        room_rows = by_room.get(room, [])
        if not room_rows:
            no_key_depth_by_room[room] += 1
            doc_excluded_counts["no_key_depth_room"] += 1
            out_of_scope_no_key_depth.append(candidate)
            continue
        best = best_room_key_depth(room_rows)
        dungeon = int(best["dungeon"])
        if dungeon not in SMALL_KEY_ITEMS or dungeon not in DUNGEON_REGIONS:
            no_key_depth_by_room[room] += 1
            doc_excluded_counts["unsupported_dungeon"] += 1
            out_of_scope_no_key_depth.append(candidate)
            continue
        reach = enemy_can_reach(
            candidate, dungeon, pot_predicates, region_only_pot_rooms,
            entry_room_predicates, pot_rows, pot_requirements)
        if reach is None:
            audit_only_by_room[room] += 1
            doc_excluded_counts["no_conservative_room_predicate"] += 1
            audit_only_no_room_predicate.append(candidate)
            continue

        loc_id = ENEMY_CHECK_BASE_ID + len(rows)
        emitted_by_room[room] += 1
        rows.append({
            "id": loc_id,
            "name": enemy_check_name(candidate, emitted_by_room[room]),
            "domain": "dungeon",
            "type": "Enemy",
            "room": room,
            "source_slot": int(candidate["source_slot"]),
            "source_type": int(candidate["source_type"]),
            "source_name": candidate["source_name"],
            "source_y": int(candidate["source_y"]),
            "source_x": int(candidate["source_x"]),
            "door_dungeon": dungeon,
            "door_region": int(best["region"]),
            "door_regions": [int(r["region"]) for r in sorted(room_rows, key=lambda r: int(r["region"]))],
            "region": DUNGEON_REGIONS[dungeon],
            "small_key_item": SMALL_KEY_ITEMS[dungeon],
            "vanilla_item": "Nothing",
            "can_reach": reach["can_reach"],
            "base_can_reach": reach["base_can_reach"],
            "predicate_source": reach["base_predicate_source"],
            "kill_predicate": reach["kill_predicate"],
            "inventory_kill_predicate": reach["inventory_kill_predicate"],
            "inventory_kill_source": reach["inventory_kill_source"],
            "throwable_pots_required": reach["throwable_pots_required"],
            "throwable_pots_in_room": reach["throwable_pots_in_room"],
            "throwable_pots_can_reach": reach["throwable_pots_can_reach"],
            "throwable_pot_damage": reach["throwable_pot_damage"],
            "throwable_pot_damage_subclass": reach["throwable_pot_damage_subclass"],
            "enemy_health": reach["enemy_health"],
            "key_depth": int(best["key_depth"]),
            "key_mindepth": int(best["key_mindepth"]),
        })

    overworld_emitted = 0
    for candidate in sorted(
            overworld_rows,
            key=lambda r: (int(r["stage"]), int(r["area"]), int(r["source_slot"]))):
        area = int(candidate["area"])
        region = OVERWORLD_REGIONS.get(area)
        if region is None:
            die(f"overworld enemy candidate area 0x{area:02x} has no reviewed region binding")
        stage = int(candidate["stage"])
        kill_pred = kill_predicate_for_overworld(region)
        stage_gate = overworld_stage_gate(stage)
        region_gate = f"OP_REGION_REACHABLE({region})"
        base_access = and_predicate([region_gate, stage_gate])
        can_reach = and_predicate([base_access, kill_pred])
        rows.append({
            "id": ENEMY_CHECK_BASE_ID + len(rows),
            "name": overworld_enemy_check_name(candidate),
            "domain": "overworld",
            "type": "Enemy",
            "area": area,
            "stage": stage,
            "source_slot": int(candidate["source_slot"]),
            "block": int(candidate["block"]),
            "source_type": int(candidate["source_type"]),
            "source_name": candidate["source_name"],
            "source_y": int(candidate["source_y"]),
            "source_x": int(candidate["source_x"]),
            "region": region,
            "vanilla_item": "Nothing",
            "can_reach": can_reach,
            "base_can_reach": base_access,
            "predicate_source": "overworld_region_stage",
            "kill_predicate": kill_pred,
            "inventory_kill_predicate": kill_pred,
            "inventory_kill_source": "overworld_generic",
            "throwable_pots_required": None,
            "throwable_pots_in_room": 0,
            "throwable_pots_can_reach": None,
            "throwable_pot_damage": None,
            "throwable_pot_damage_subclass": None,
            "enemy_health": None,
            "all_tier_only": True,
        })
        overworld_emitted += 1

    dungeon_emitted = len(rows) - overworld_emitted
    source_types = {int(r["source_type"]) for r in rows}
    return {
        "format_version": 1,
        "_generated_by": "assets/scripts/gen_enemy_check_tables.py (do not hand-edit)",
        "source": {
            "assets": str(assets_path.name),
            "sha256": hashlib.sha256(assets_path.read_bytes()).hexdigest(),
            # Local checks dump this under tmp/, while direct developer runs
            # commonly use key_depth.txt in the repo root. Keep the registry
            # stable across equivalent dump locations.
            "key_depth": key_depth_path.name,
            "constraint_table": SHUFFLE_ENEMIES_C.relative_to(REPO).as_posix(),
            "pot_predicates": POT_REGISTRY.relative_to(REPO).as_posix(),
            "entry_room_predicates": ENTRANCE_REGISTRY.relative_to(REPO).as_posix(),
            "enemy_health": SPRITE_C.relative_to(REPO).as_posix() + ":kSpriteInit_Health",
            "enemy_prep_health": SPRITE_MAIN_C.relative_to(REPO).as_posix() + ":source-specific prep health max",
            "enemy_damage": assets_path.name + ":kEnemyDamageData",
            "thrown_pot_damage": SPRITE_C.relative_to(REPO).as_posix() + ":ThrownSprite_CheckDamageToSingleSprite damage preset 3",
        },
        "policy": {
            "scope": "dungeon_plus_all-tier_static_overworld",
            "eligible_source_type": ["ESF_RANDOMIZABLE", "ESF_KILLABLE"],
            "excluded_source_type_flags": {
                "dungeon": ["ESF_CANNOT_KEY", "ESF_FLYING"],
                "overworld_all_tier": [],
            },
            "excluded_sources": [
                "existing forced-key enemy-drop checks",
                "overlords/control markers",
                "underworld sprite-table rooms with no key-depth ROOM row (outside dungeon-only scope)",
                "overworld sprite-table rows with no stable active-list runtime identity",
                "boss/miniboss and finite scripted-spawn groups until separate source identity and reward coexistence are implemented",
            ],
            "runtime_identity": {
                "dungeon": "(dungeon_room_index, sprite_N source slot)",
                "overworld": "(active overworld sprite-list stage, overworld_area_index, source_slot, sprite_N_word block)",
            },
            "kill_logic": [
                "dungeon: per-source inventory predicate",
                "dungeon: OR thrown-pot kill route when engine damage tables show liftable pots deal normal HP damage",
                "dungeon: thrown-pot route requires at least the generated pots_needed count in the room",
                "overworld: region-reachable plus generic overworld combat predicate",
            ],
            "base_room_predicates": [
                "reviewed forced-key room binding",
                "reviewed pot-room predicate",
                "dungeon entry room with matching entrance_registry entry_region",
                "single-region pot room with no local can_reach gate",
            ],
        },
        "summary": {
            "scanned_underworld_source_count": len(dungeon_rows),
            "scanned_overworld_source_count": len(overworld_rows),
            "candidate_count": len(dungeon_rows) + len(overworld_rows),
            "emitted_count": len(rows),
            "emitted_dungeon_count": dungeon_emitted,
            "emitted_overworld_count": overworld_emitted,
            "excluded_count": sum(doc_excluded_counts.values()) + sum(overworld_excluded_counts.values()),
            "out_of_scope_no_key_depth_count": len(out_of_scope_no_key_depth),
            "audit_only_no_room_predicate_count": len(audit_only_no_room_predicate),
            "emitted_rooms": len(emitted_by_room),
            "out_of_scope_no_key_depth_rooms": len(no_key_depth_by_room),
            "audit_only_no_room_predicate_rooms": len(audit_only_by_room),
            "source_types": len(source_types),
            "rows_with_throwable_pot_route": sum(1 for r in rows if r.get("throwable_pots_can_reach")),
            "rows_pot_killable_by_damage_table": sum(1 for r in rows if r.get("throwable_pots_required") is not None),
            "rows_with_special_inventory_kill": sum(1 for r in rows if r.get("inventory_kill_source") == "source_type_special"),
        },
        "excluded_counts": {
            "dungeon": dict(sorted(doc_excluded_counts.items())),
            "overworld": dict(sorted(overworld_excluded_counts.items())),
        },
        "enemy_checks": rows,
        "out_of_scope_no_key_depth": [
            {
                "room": int(r["room"]),
                "source_slot": int(r["source_slot"]),
                "source_type": int(r["source_type"]),
                "source_name": r["source_name"],
                "source_y": int(r["source_y"]),
                "source_x": int(r["source_x"]),
            }
            for r in out_of_scope_no_key_depth
        ],
        "audit_only_no_room_predicate": [
            {
                "room": int(r["room"]),
                "source_slot": int(r["source_slot"]),
                "source_type": int(r["source_type"]),
                "source_name": r["source_name"],
                "source_y": int(r["source_y"]),
                "source_x": int(r["source_x"]),
                "reason": "no_conservative_room_predicate",
            }
            for r in audit_only_no_room_predicate
        ],
    }


def build_doc(args) -> dict:
    assets_path = args.assets if args.assets.is_absolute() else REPO / args.assets
    if not assets_path.exists():
        die(f"missing {assets_path}; build/extract assets first")
    key_depth_path = args.key_depth if args.key_depth.is_absolute() else REPO / args.key_depth
    assets = read_assets(assets_path)
    constraints = parse_enemy_constraints(SHUFFLE_ENEMIES_C)
    forced_sources = forced_drop_source_set(assets)
    dungeon_rows, excluded_counts = collect_dungeon_candidates(
        assets,
        constraints,
        forced_sources,
        allow_cannot_key=False,
        allow_flying=False,
    )
    overworld_rows, _overworld_collisions, overworld_excluded_counts = collect_overworld_candidates(
        assets,
        constraints,
        allow_cannot_key=True,
        allow_flying=True,
    )
    key_depth = parse_key_depth(key_depth_path)
    pot_predicates = load_pot_room_predicates(POT_REGISTRY)
    region_only_pot_rooms = load_region_only_pot_rooms(POT_REGISTRY)
    entry_room_predicates = load_entry_room_predicates(ENTRANCE_REGISTRY)
    pot_rows = room_pot_rows(POT_REGISTRY)
    pot_requirements = thrown_pot_requirements(
        assets, {int(row["source_type"]) for row in dungeon_rows})
    return make_doc(
        assets_path,
        key_depth_path,
        dungeon_rows,
        excluded_counts,
        overworld_rows,
        overworld_excluded_counts,
        key_depth,
        pot_predicates,
        region_only_pot_rooms,
        entry_room_predicates,
        pot_rows,
        pot_requirements,
    )


def render_yaml(doc: dict) -> str:
    return yaml.safe_dump(doc, sort_keys=False, allow_unicode=False, width=1000) + "\n"


def write_lf(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="\n") as f:
        f.write(text)


def check_fresh(path: Path, expected: str) -> int:
    want = expected.encode("utf-8")
    have = path.read_bytes() if path.exists() else b""
    if have == want:
        print(f"gen_enemy_check_tables: OK {path} is fresh", file=sys.stderr)
        return 0
    print(f"gen_enemy_check_tables: ERROR: {path} is stale; run "
          f"`python assets/scripts/gen_enemy_check_tables.py` to refresh it.",
          file=sys.stderr)
    for line in difflib.unified_diff(
            have.decode("utf-8", errors="replace").splitlines(),
            expected.splitlines(),
            fromfile=str(path),
            tofile=f"{path} (regenerated)",
            lineterm=""):
        print(line, file=sys.stderr)
    return 1


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--assets", type=Path, default=DEFAULT_ASSETS,
                    help="Path to zelda3_assets.dat")
    ap.add_argument("--key-depth", type=Path, default=DEFAULT_KEY_DEPTH,
                    help="key_depth.txt from `zelda3 --dump-key-depth`")
    ap.add_argument("--out", type=Path, default=DEFAULT_OUT,
                    help="Output YAML path")
    ap.add_argument("--check", action="store_true",
                    help="Fail if the output is stale instead of writing it")
    args = ap.parse_args(argv)

    out_path = args.out if args.out.is_absolute() else REPO / args.out
    doc = build_doc(args)
    text = render_yaml(doc)
    summary = doc["summary"]
    print(
        "gen_enemy_check_tables: "
        f"{summary['emitted_dungeon_count']} dungeon + "
        f"{summary['emitted_overworld_count']} overworld ordinary enemy checks "
        f"emitted from {summary['candidate_count']} eligible sources; "
        f"{summary['audit_only_no_room_predicate_count']} audit-only without "
        "conservative room predicates; "
        f"{summary['out_of_scope_no_key_depth_count']} underworld sources outside "
        "dungeon-only key-depth scope",
        file=sys.stderr,
    )
    if args.check:
        return check_fresh(out_path, text)
    write_lf(out_path, text)
    print(f"gen_enemy_check_tables: wrote {out_path}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
