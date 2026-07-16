#!/usr/bin/env python3
"""Generate the local ordinary enemy-check registry.

Reads the packed dungeon and overworld sprite assets from zelda3_assets.dat,
reuses the enemy-shuffle constraint table as the first-pass "safe enemy" oracle,
and writes assets/rando/enemy_checks.gen.yaml for rando_logic_gen.py.

The dungeon tier remains conservative: rooms without a key-depth ROOM row are
not emitted, even if they live in the dungeon sprite table, because those are
typically cave/interior sprite lists that do not have dungeon door-region logic.
The all tier appends static authored overworld enemies whose runtime identity is
stable under the vanilla sprite-list stage, plus reviewed underworld exceptions
whose cave/interior access can be modeled directly.
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
    HCE_ENEMY_KILL_PREDICATE,
    SMALL_KEY_BINDINGS,
    SMALL_KEY_ITEMS,
    asset_payload_sha256,
    parse_key_depth,
    parse_u16le_array,
    read_assets,
    sprite_entries,
)
from gen_soul_tables import enemy_soul_by_species  # noqa: E402

# add-enemy-souls (task 1.6): killing a check's source enemy requires that
# species' soul at souls_shuffle=all. Terms are inert below the tier; GT
# miniboss checks are excluded (their CanKill<Boss> macros carry the boss-soul
# terms). Ordinary enemy checks never coexist with enemy shuffle (the tier
# degrades to Keys there), so the vanilla source_type is authoritative.
SOUL_BY_SPECIES = enemy_soul_by_species()

# Static, finite, killable sources that are safe as one-shot all-tier checks but
# deliberately absent from the enemy-shuffle replacement pool. Red Eyegores
# have stable room/source-slot identity and a precise Bow kill predicate; not
# being a safe generic shuffle replacement must not silently make them vanilla
# in a setting named `all`.
ALL_TIER_CHECK_ONLY_SOURCE_CONSTRAINTS = {
    0x84: {
        "name": "Red Eyegore",
        "flags": ["ESF_KILLABLE"],
        "sheets": [46],
    },
}


def soul_term_for(source_type: int) -> str | None:
    tok = SOUL_BY_SPECIES.get(int(source_type))
    return f"NeedsEnemySoul(Soul_{tok})" if tok is not None else None

DEFAULT_OUT = REPO / "assets" / "rando" / "enemy_checks.gen.yaml"
POT_REGISTRY = REPO / "assets" / "rando" / "pots.gen.yaml"
ENTRANCE_REGISTRY = REPO / "assets" / "rando" / "entrance_registry.yaml"
SPRITE_C = REPO / "src" / "sprite.c"
SPRITE_MAIN_C = REPO / "src" / "sprite_main.c"
ENEMY_CHECK_BASE_ID = 1400
THROWN_POT_DAMAGE_TYPE = 3
SPECIAL_DAMAGE_MIN = 249
ENEMY_CHECK_ASSET_KEYS = (
    "kDungeonSprites",
    "kDungeonSpriteOffs",
    "kEnemyDamageData",
    "kOverworldSprites",
    "kOverworldSpriteOffs",
)

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

UNDERWORLD_GENERIC_KILL_PREDICATE = "CanKillMostThings(world, 5)"

ALL_TIER_BOMB_ROOM_ACCESS = (
    "OP_REGION_REACHABLE(LightWorld_NorthWest) AND CanBombThings()"
)

ALL_TIER_HOOKSHOT_CAVE_ACCESS = (
    "OP_REGION_REACHABLE(DarkWorld_DeathMountain_East) AND HAS_ITEM(Hookshot) AND "
    "(((NOT OP_WORLDSTATE_EQ(inverted)) AND (HAS_ITEM(MoonPearl) OR CanPearlBypass()) "
    "AND (CanLiftRocks() OR CanBootsClip() OR CanOneFrameClipOW())) OR "
    "(OP_WORLDSTATE_EQ(inverted) AND (CanLiftRocks() OR (HAS_ITEM(MagicMirror) "
    "AND CanBombThings() AND OP_REGION_REACHABLE(LightWorld_DeathMountain_East)) "
    "OR CanBootsClip() OR CanOneFrameClipOW())))"
)

ALL_TIER_MIMIC_CAVE_ACCESS = (
    "OP_REGION_REACHABLE(LightWorld_DeathMountain_East) AND "
    "(((NOT OP_WORLDSTATE_EQ(inverted)) AND HAS_ITEM(Hammer) AND HAS_ITEM(MagicMirror) "
    "AND ((HAS_AMOUNT(SmallKey_TurtleRock, 2) AND OP_REGION_REACHABLE(TurtleRock_Lobby)) "
    "OR CanOneFrameClipOW())) OR "
    "(OP_WORLDSTATE_EQ(inverted) AND HAS_ITEM(Hammer) AND HAS_ITEM(MoonPearl)))"
)

ALL_TIER_MINI_MOLDORM_CAVE_ACCESS = (
    "OP_REGION_REACHABLE(LightWorld_South) AND CanBombThings() AND "
    "((NOT OP_WORLDSTATE_EQ(inverted)) OR HAS_ITEM(MoonPearl))"
)

ALL_TIER_HCE_SECRET_PASSAGE_ACCESS = "OP_REGION_REACHABLE(HyruleCastleEscape)"

ALL_TIER_LW_DM_EAST_CAVE_ACCESS = (
    "OP_REGION_REACHABLE(LightWorld_DeathMountain_East) AND "
    "((NOT OP_WORLDSTATE_EQ(inverted)) OR HAS_ITEM(MoonPearl))"
)

ALL_TIER_PARADOX_CAVE_UPPER_ACCESS = (
    "OP_REGION_REACHABLE(LightWorld_DeathMountain_East) AND "
    "((NOT OP_WORLDSTATE_EQ(inverted)) OR (HAS_ITEM(MoonPearl) AND CanBombThings()))"
)

ALL_TIER_SPECTACLE_ROCK_CAVE_ACCESS = (
    "OP_REGION_REACHABLE(LightWorld_DeathMountain_West)"
)


def reviewed_underworld_inventory_binding(region: str, base_can_reach: str,
                                          predicate_source: str) -> dict:
    return {
        "region": region,
        "base_can_reach": base_can_reach,
        "predicate_source": predicate_source,
        "inventory_kill_predicate": UNDERWORLD_GENERIC_KILL_PREDICATE,
        "inventory_kill_source": "underworld_generic",
        "allow_throwable_pots": False,
    }


def reviewed_key_depth_room_binding(region: str, base_can_reach: str,
                                    predicate_source: str,
                                    inventory_kill_predicate: str = UNDERWORLD_GENERIC_KILL_PREDICATE,
                                    inventory_kill_source: str = "reviewed_key_depth_room",
                                    allow_throwable_pots: bool = True) -> dict:
    return {
        "region": region,
        "base_can_reach": base_can_reach,
        "predicate_source": predicate_source,
        "inventory_kill_predicate": inventory_kill_predicate,
        "inventory_kill_source": inventory_kill_source,
        "allow_throwable_pots": allow_throwable_pots,
        "use_key_depth": True,
    }


def reviewed_key_depth_room_bindings(room: int, slots, region: str,
                                     base_can_reach: str, predicate_source: str,
                                     inventory_kill_predicate: str = UNDERWORLD_GENERIC_KILL_PREDICATE,
                                     inventory_kill_source: str = "reviewed_key_depth_room",
                                     allow_throwable_pots: bool = True) -> dict:
    return {
        (room, int(slot)): reviewed_key_depth_room_binding(
            region, base_can_reach, predicate_source,
            inventory_kill_predicate=inventory_kill_predicate,
            inventory_kill_source=inventory_kill_source,
            allow_throwable_pots=allow_throwable_pots)
        for slot in slots
    }


def reviewed_hce_castle_guard_binding(predicate_source: str) -> dict:
    return {
        "region": "HyruleCastleEscape",
        "base_can_reach": ALL_TIER_HCE_SECRET_PASSAGE_ACCESS,
        "predicate_source": predicate_source,
        "inventory_kill_predicate": HCE_ENEMY_KILL_PREDICATE,
        "inventory_kill_source": "hce_castle_guard",
        "allow_throwable_pots": False,
        "use_key_depth": True,
    }


ALL_TIER_UNDERWORLD_BINDINGS = {
    # Reviewed key-depth rooms. These rooms have door-table ROOM rows, but no
    # pot/entry conservative predicate. Bind only the non-key room gate here;
    # the generator copies key-depth + door-region metadata so Wild/Dungeon key
    # terms and door shuffle still use the normal bridge.
    **reviewed_key_depth_room_bindings(
        0x022, range(7), "HyruleCastleEscape",
        f"({HCE_ENEMY_KILL_PREDICATE}) AND (HAS_ITEM(Lamp) OR CanDarkRoomNav())",
        "all_tier_underworld_reviewed_hce_sewers_water",
        inventory_kill_predicate=HCE_ENEMY_KILL_PREDICATE,
        inventory_kill_source="hce_sewers"),
    **reviewed_key_depth_room_bindings(
        0x042, range(6), "HyruleCastleEscape",
        HCE_ENEMY_KILL_PREDICATE,
        "all_tier_underworld_reviewed_hce_sewers_rope_room",
        inventory_kill_predicate=HCE_ENEMY_KILL_PREDICATE,
        inventory_kill_source="hce_sewers"),
    **reviewed_key_depth_room_bindings(
        0x081, range(2), "HyruleCastleEscape",
        HCE_ENEMY_KILL_PREDICATE,
        "all_tier_underworld_reviewed_hce_guardroom",
        inventory_kill_predicate=HCE_ENEMY_KILL_PREDICATE,
        inventory_kill_source="hce_guardroom"),

    **reviewed_key_depth_room_bindings(
        0x040, [0, 1, 3, 4, 5], "HyruleCastleTower",
        "HAS_ITEM(Lamp) OR CanDarkRoomNav()",
        "all_tier_underworld_reviewed_hct_upper_rooms",
        inventory_kill_predicate="CanKillMostThings(world, 8)",
        inventory_kill_source="hct_upper_rooms"),

    **reviewed_key_depth_room_bindings(
        0x02E, range(6), "IcePalace_Lobby", "TRUE()",
        "all_tier_underworld_reviewed_ice_compass_room"),
    **reviewed_key_depth_room_bindings(
        0x05F, range(3), "IcePalace_Lobby", "HAS_ITEM(Hookshot)",
        "all_tier_underworld_reviewed_ice_spike_room"),
    **reviewed_key_depth_room_bindings(
        0x06E, range(5), "IcePalace_Lobby",
        "HAS_ITEM(Hammer) AND CanLiftRocks() AND HAS_ITEM(BigKey_IcePalace)",
        "all_tier_underworld_reviewed_ice_pengator_trap"),
    **reviewed_key_depth_room_bindings(
        0x0AE, range(2), "IcePalace_Lobby", "TRUE()",
        "all_tier_underworld_reviewed_ice_iced_t"),
    **reviewed_key_depth_room_bindings(
        0x0BE, [2, 3, 5, 6], "IcePalace_Lobby",
        "HAS_ITEM(Hammer) AND CanLiftRocks() AND HAS_ITEM(BigKey_IcePalace)",
        "all_tier_underworld_reviewed_ice_anti_fairy_switch"),

    **reviewed_key_depth_room_bindings(
        0x03A, [3, 4], "PalaceOfDarkness",
        "(HAS_ITEM(Lamp) OR CanDarkRoomNav()) AND HAS_ITEM(BigKey_PalaceOfDarkness)",
        "all_tier_underworld_reviewed_pod_big_key_landing"),
    **reviewed_key_depth_room_bindings(
        0x03B, [3, 5], "PalaceOfDarkness", "TRUE()",
        "all_tier_underworld_reviewed_pod_conveyor"),

    **reviewed_key_depth_room_bindings(
        0x044, [2, 3, 5, 7], "ThievesTown",
        "HAS_ITEM(Hammer) AND HAS_ITEM(BigKey_ThievesTown)",
        "all_tier_underworld_reviewed_tt_big_chest_conveyor"),
    **reviewed_key_depth_room_bindings(
        0x0BB, [0, 1, 2, 3, 5, 7, 8, 10], "ThievesTown",
        "HAS_ITEM(BigKey_ThievesTown)",
        "all_tier_underworld_reviewed_tt_hellway"),

    **reviewed_key_depth_room_bindings(
        0x04C, [2, 3, 4, 5, 6], "GanonsTower_Lobby",
        "HAS_ITEM(Hookshot) AND CanShootArrowsL1() AND CanLightTorches() "
        "AND HAS_ITEM(BigKey_GanonsTower) AND CanKillLanmolas(world) "
        "AND CanKillMoldorm()",
        "all_tier_underworld_reviewed_gt_late_tower"),
    **reviewed_key_depth_room_bindings(
        0x095, range(4), "GanonsTower_Lobby",
        "HAS_ITEM(Hookshot) AND CanShootArrowsL1() AND CanLightTorches() "
        "AND HAS_ITEM(BigKey_GanonsTower) AND CanKillLanmolas(world) "
        "AND CanKillMoldorm()",
        "all_tier_underworld_reviewed_gt_late_tower"),
    **reviewed_key_depth_room_bindings(
        0x0A5, [10, 11], "GanonsTower_Lobby",
        "HAS_ITEM(Hookshot) AND CanShootArrowsL1() AND CanLightTorches() "
        "AND HAS_ITEM(BigKey_GanonsTower) AND CanKillLanmolas(world) "
        "AND CanKillMoldorm()",
        "all_tier_underworld_reviewed_gt_late_tower"),

    **reviewed_key_depth_room_bindings(
        0x0C5, [6], "TurtleRock_Lobby", "TRUE()",
        "all_tier_underworld_reviewed_tr_dash_bridge"),
    **reviewed_key_depth_room_bindings(
        0x0D2, [1, 5, 6, 8, 9], "MiseryMire_Lobby", "TRUE()",
        "all_tier_underworld_reviewed_mire_2"),

    # Death Mountain maze cave rooms. These normal cave-interior enemies are
    # stored in the dungeon sprite table but have no vanilla door-graph ROOM
    # rows. Bind them to the matching cave/check access instead of forcing them
    # into dungeon key-depth logic. Throwable-pot combat stays disabled here
    # because the current pot-region model does not prove same-side cave access
    # for these no-key-depth DM maze rooms.
    (0x0DF, 0): reviewed_underworld_inventory_binding(
        "LightWorld_DeathMountain_East",
        ALL_TIER_PARADOX_CAVE_UPPER_ACCESS,
        "all_tier_underworld_reviewed_paradox_cave_upper"),
    (0x0DF, 1): reviewed_underworld_inventory_binding(
        "LightWorld_DeathMountain_East",
        ALL_TIER_PARADOX_CAVE_UPPER_ACCESS,
        "all_tier_underworld_reviewed_paradox_cave_upper"),
    (0x0EE, 0): reviewed_underworld_inventory_binding(
        "LightWorld_DeathMountain_East",
        ALL_TIER_LW_DM_EAST_CAVE_ACCESS,
        "all_tier_underworld_reviewed_paradox_cave_lower"),
    (0x0EE, 1): reviewed_underworld_inventory_binding(
        "LightWorld_DeathMountain_East",
        ALL_TIER_LW_DM_EAST_CAVE_ACCESS,
        "all_tier_underworld_reviewed_paradox_cave_lower"),
    (0x0EE, 2): reviewed_underworld_inventory_binding(
        "LightWorld_DeathMountain_East",
        ALL_TIER_LW_DM_EAST_CAVE_ACCESS,
        "all_tier_underworld_reviewed_paradox_cave_lower"),
    (0x0EE, 3): reviewed_underworld_inventory_binding(
        "LightWorld_DeathMountain_East",
        ALL_TIER_LW_DM_EAST_CAVE_ACCESS,
        "all_tier_underworld_reviewed_paradox_cave_lower"),
    (0x0EE, 4): reviewed_underworld_inventory_binding(
        "LightWorld_DeathMountain_East",
        ALL_TIER_LW_DM_EAST_CAVE_ACCESS,
        "all_tier_underworld_reviewed_paradox_cave_lower"),
    (0x0EF, 0): reviewed_underworld_inventory_binding(
        "LightWorld_DeathMountain_East",
        ALL_TIER_LW_DM_EAST_CAVE_ACCESS,
        "all_tier_underworld_reviewed_paradox_cave_lower"),
    (0x0EF, 1): reviewed_underworld_inventory_binding(
        "LightWorld_DeathMountain_East",
        ALL_TIER_LW_DM_EAST_CAVE_ACCESS,
        "all_tier_underworld_reviewed_paradox_cave_lower"),
    (0x0EF, 2): reviewed_underworld_inventory_binding(
        "LightWorld_DeathMountain_East",
        ALL_TIER_LW_DM_EAST_CAVE_ACCESS,
        "all_tier_underworld_reviewed_paradox_cave_lower"),
    (0x0F9, 0): reviewed_underworld_inventory_binding(
        "LightWorld_DeathMountain_West",
        ALL_TIER_SPECTACLE_ROCK_CAVE_ACCESS,
        "all_tier_underworld_reviewed_spectacle_rock_cave"),
    (0x0F9, 1): reviewed_underworld_inventory_binding(
        "LightWorld_DeathMountain_West",
        ALL_TIER_SPECTACLE_ROCK_CAVE_ACCESS,
        "all_tier_underworld_reviewed_spectacle_rock_cave"),
    (0x0F9, 2): reviewed_underworld_inventory_binding(
        "LightWorld_DeathMountain_West",
        ALL_TIER_SPECTACLE_ROCK_CAVE_ACCESS,
        "all_tier_underworld_reviewed_spectacle_rock_cave"),
    (0x0F9, 3): reviewed_underworld_inventory_binding(
        "LightWorld_DeathMountain_West",
        ALL_TIER_SPECTACLE_ROCK_CAVE_ACCESS,
        "all_tier_underworld_reviewed_spectacle_rock_cave"),
    (0x0FD, 0): reviewed_underworld_inventory_binding(
        "LightWorld_DeathMountain_East",
        ALL_TIER_LW_DM_EAST_CAVE_ACCESS,
        "all_tier_underworld_reviewed_spiral_cave"),
    (0x0FD, 1): reviewed_underworld_inventory_binding(
        "LightWorld_DeathMountain_East",
        ALL_TIER_LW_DM_EAST_CAVE_ACCESS,
        "all_tier_underworld_reviewed_spiral_cave"),
    (0x0FD, 4): reviewed_underworld_inventory_binding(
        "LightWorld_DeathMountain_East",
        ALL_TIER_LW_DM_EAST_CAVE_ACCESS,
        "all_tier_underworld_reviewed_spiral_cave"),
    (0x0FE, 0): reviewed_underworld_inventory_binding(
        "LightWorld_DeathMountain_East",
        ALL_TIER_LW_DM_EAST_CAVE_ACCESS,
        "all_tier_underworld_reviewed_spiral_cave"),
    (0x0FE, 1): reviewed_underworld_inventory_binding(
        "LightWorld_DeathMountain_East",
        ALL_TIER_LW_DM_EAST_CAVE_ACCESS,
        "all_tier_underworld_reviewed_spiral_cave"),
    (0x0FE, 2): reviewed_underworld_inventory_binding(
        "LightWorld_DeathMountain_East",
        ALL_TIER_LW_DM_EAST_CAVE_ACCESS,
        "all_tier_underworld_reviewed_spiral_cave"),
    (0x0FE, 3): reviewed_underworld_inventory_binding(
        "LightWorld_DeathMountain_East",
        ALL_TIER_LW_DM_EAST_CAVE_ACCESS,
        "all_tier_underworld_reviewed_spiral_cave"),
    (0x0FE, 4): reviewed_underworld_inventory_binding(
        "LightWorld_DeathMountain_East",
        ALL_TIER_LW_DM_EAST_CAVE_ACCESS,
        "all_tier_underworld_reviewed_spiral_cave"),

    # Hyrule Castle Throne Room and central lobby guards. These rooms are
    # reachable through the normal HCE region, but they have no pot/key-depth
    # room predicate row. Killing the guards still needs the normal HCE combat
    # predicate; there are no reviewed thrown-pot kill routes in these rooms.
    (0x051, 1): reviewed_hce_castle_guard_binding(
        "all_tier_underworld_reviewed_hce_throne_room"),
    (0x051, 2): reviewed_hce_castle_guard_binding(
        "all_tier_underworld_reviewed_hce_throne_room"),
    (0x061, 0): reviewed_hce_castle_guard_binding(
        "all_tier_underworld_reviewed_hce_central_lobby"),
    (0x061, 1): reviewed_hce_castle_guard_binding(
        "all_tier_underworld_reviewed_hce_central_lobby"),
    (0x061, 2): reviewed_hce_castle_guard_binding(
        "all_tier_underworld_reviewed_hce_central_lobby"),

    # HCE Secret Passage. This engine room is covered by high-level HCE
    # location logic, but it has no vanilla door-graph ROOM row, so model it as
    # a reviewed all-tier underworld exception instead of forcing it into the
    # dungeon key-depth set.
    (0x055, 1): {
        "region": "HyruleCastleEscape",
        "base_can_reach": ALL_TIER_HCE_SECRET_PASSAGE_ACCESS,
        "predicate_source": "all_tier_underworld_reviewed_hce_secret_passage",
        "inventory_kill_predicate": HCE_ENEMY_KILL_PREDICATE,
        "inventory_kill_source": "hce_secret_passage",
        "throwable_pots_can_reach": "HAS_ITEM(Lamp) OR CanDarkRoomNav()",
    },
    (0x055, 2): {
        "region": "HyruleCastleEscape",
        "base_can_reach": ALL_TIER_HCE_SECRET_PASSAGE_ACCESS,
        "predicate_source": "all_tier_underworld_reviewed_hce_secret_passage",
        "inventory_kill_predicate": HCE_ENEMY_KILL_PREDICATE,
        "inventory_kill_source": "hce_secret_passage",
        "throwable_pots_can_reach": "HAS_ITEM(Lamp) OR CanDarkRoomNav()",
    },
    # Kakariko Storage Shed / Library room. These two rats are behind a bombable
    # wall and have eight liftable pots in-room, so the kill route is modeled by
    # the same thrown-pot damage table used for dungeon enemy checks.
    (0x107, 1): {
        "region": "LightWorld_NorthWest",
        "base_can_reach": ALL_TIER_BOMB_ROOM_ACCESS,
        "predicate_source": "all_tier_underworld_reviewed_bomb_room",
        "inventory_kill_predicate": UNDERWORLD_GENERIC_KILL_PREDICATE,
        "inventory_kill_source": "underworld_generic",
        "throwable_pots_can_reach": "TRUE()",
    },
    (0x107, 2): {
        "region": "LightWorld_NorthWest",
        "base_can_reach": ALL_TIER_BOMB_ROOM_ACCESS,
        "predicate_source": "all_tier_underworld_reviewed_bomb_room",
        "inventory_kill_predicate": UNDERWORLD_GENERIC_KILL_PREDICATE,
        "inventory_kill_source": "underworld_generic",
        "throwable_pots_can_reach": "TRUE()",
    },
    # Hookshot Cave-side room. These Blue Bari are in a cave room with no
    # dungeon key-depth row; reuse the matching Hookshot Cave chest access for
    # Standard/Open/Retro and Inverted, then apply ordinary cave combat logic.
    (0x03C, 1): {
        "region": "DarkWorld_DeathMountain_East",
        "base_can_reach": ALL_TIER_HOOKSHOT_CAVE_ACCESS,
        "predicate_source": "all_tier_underworld_reviewed_hookshot_cave",
        "inventory_kill_predicate": UNDERWORLD_GENERIC_KILL_PREDICATE,
        "inventory_kill_source": "underworld_generic",
        "throwable_pots_can_reach": "TRUE()",
    },
    (0x03C, 2): {
        "region": "DarkWorld_DeathMountain_East",
        "base_can_reach": ALL_TIER_HOOKSHOT_CAVE_ACCESS,
        "predicate_source": "all_tier_underworld_reviewed_hookshot_cave",
        "inventory_kill_predicate": UNDERWORLD_GENERIC_KILL_PREDICATE,
        "inventory_kill_source": "underworld_generic",
        "throwable_pots_can_reach": "TRUE()",
    },
    # Mimic Cave. The static source type names these as Green Eyegores, while
    # SpritePrep_Eyegore switches room 0x10C into Goriya/Mimic behavior.
    (0x10C, 4): {
        "region": "LightWorld_DeathMountain_East",
        "base_can_reach": ALL_TIER_MIMIC_CAVE_ACCESS,
        "predicate_source": "all_tier_underworld_reviewed_mimic_cave",
        "inventory_kill_predicate": UNDERWORLD_GENERIC_KILL_PREDICATE,
        "inventory_kill_source": "underworld_generic",
        "allow_throwable_pots": False,
    },
    (0x10C, 5): {
        "region": "LightWorld_DeathMountain_East",
        "base_can_reach": ALL_TIER_MIMIC_CAVE_ACCESS,
        "predicate_source": "all_tier_underworld_reviewed_mimic_cave",
        "inventory_kill_predicate": UNDERWORLD_GENERIC_KILL_PREDICATE,
        "inventory_kill_source": "underworld_generic",
        "allow_throwable_pots": False,
    },
    (0x10C, 6): {
        "region": "LightWorld_DeathMountain_East",
        "base_can_reach": ALL_TIER_MIMIC_CAVE_ACCESS,
        "predicate_source": "all_tier_underworld_reviewed_mimic_cave",
        "inventory_kill_predicate": UNDERWORLD_GENERIC_KILL_PREDICATE,
        "inventory_kill_source": "underworld_generic",
        "allow_throwable_pots": False,
    },
    (0x10C, 7): {
        "region": "LightWorld_DeathMountain_East",
        "base_can_reach": ALL_TIER_MIMIC_CAVE_ACCESS,
        "predicate_source": "all_tier_underworld_reviewed_mimic_cave",
        "inventory_kill_predicate": UNDERWORLD_GENERIC_KILL_PREDICATE,
        "inventory_kill_source": "underworld_generic",
        "allow_throwable_pots": False,
    },
    # Mini Moldorm Cave. The four Mini-Moldorms share the same cave access as
    # the four chests/NPC in that room, minus the combat term which is added
    # per enemy check below.
    (0x123, 0): {
        "region": "LightWorld_South",
        "base_can_reach": ALL_TIER_MINI_MOLDORM_CAVE_ACCESS,
        "predicate_source": "all_tier_underworld_reviewed_mini_moldorm_cave",
        "inventory_kill_predicate": UNDERWORLD_GENERIC_KILL_PREDICATE,
        "inventory_kill_source": "underworld_generic",
    },
    (0x123, 1): {
        "region": "LightWorld_South",
        "base_can_reach": ALL_TIER_MINI_MOLDORM_CAVE_ACCESS,
        "predicate_source": "all_tier_underworld_reviewed_mini_moldorm_cave",
        "inventory_kill_predicate": UNDERWORLD_GENERIC_KILL_PREDICATE,
        "inventory_kill_source": "underworld_generic",
    },
    (0x123, 2): {
        "region": "LightWorld_South",
        "base_can_reach": ALL_TIER_MINI_MOLDORM_CAVE_ACCESS,
        "predicate_source": "all_tier_underworld_reviewed_mini_moldorm_cave",
        "inventory_kill_predicate": UNDERWORLD_GENERIC_KILL_PREDICATE,
        "inventory_kill_source": "underworld_generic",
    },
    (0x123, 3): {
        "region": "LightWorld_South",
        "base_can_reach": ALL_TIER_MINI_MOLDORM_CAVE_ACCESS,
        "predicate_source": "all_tier_underworld_reviewed_mini_moldorm_cave",
        "inventory_kill_predicate": UNDERWORLD_GENERIC_KILL_PREDICATE,
        "inventory_kill_source": "underworld_generic",
    },
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
    112: "DarkWorld_Mire",
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
SPECIAL_INVENTORY_KILL_PREDICATES = {
    0x84: "CanShootArrowsL1()",  # Red Eyegore deflects every non-arrow hit.
}

# Some sprite prep routines replace kSpriteInit_Health at runtime. Use the
# maximum vanilla prep value so thrown-pot counts never understate HP.
PREP_HEALTH_ARRAYS = {
    0x6D: (SPRITE_MAIN_C, "kSpriteRat_Health"),
    0x6E: (SPRITE_MAIN_C, "kSpriteRope_Health"),
}


def world_state_predicate(standard: str, inverted: str | None = None) -> str:
    if inverted is None or inverted == standard:
        return standard
    return (
        f"((OP_WORLDSTATE_EQ(inverted) AND ({inverted})) OR "
        f"((NOT OP_WORLDSTATE_EQ(inverted)) AND ({standard})))"
    )


GT_MINIBOSS_EVENT_CHECKS = [
    {
        "name": "Enemy Check - Ganon's Tower Ice Armos Enemy",
        "boss_kind": "gt_miniboss",
        "game_dungeon": 13,
        "rando_dungeon": 12,
        # Runtime event identity must use dungeon_room_index, not the logical
        # door-region room. GT Ice Armos is region room 0x064 but executes in
        # the lower half of physical room 0x01C.
        "room": 0x01C,
        "logical_room": 0x064,
        "door_dungeon": 12,
        "door_region": 537,
        "region": "GanonsTower_Lobby",
        "source_name": "GT Ice Armos",
        "can_reach": world_state_predicate(
            "((HAS_ITEM(Hammer) AND HAS_ITEM(Hookshot)) OR "
            "(HAS_ITEM(FireRod) AND HAS_ITEM(CaneOfSomaria))) AND "
            "HAS_AMOUNT(SmallKey_GanonsTower, 3) AND CanKillArmosKnights()",
            "((HAS_ITEM(Hammer) AND HAS_ITEM(Hookshot)) OR "
            "(HAS_ITEM(FireRod) AND HAS_ITEM(CaneOfSomaria))) AND "
            "HAS_AMOUNT(SmallKey_GanonsTower, 3) AND CanKillArmosKnights() "
            "AND (HAS_ITEM(MoonPearl) OR CanBunnyRevival(world) OR CanPearlBypass())",
        ),
    },
    {
        "name": "Enemy Check - Ganon's Tower Lanmolas 2 Enemy",
        "boss_kind": "gt_miniboss",
        "game_dungeon": 13,
        "rando_dungeon": 12,
        # GT Lanmolas 2 is logical room 0x067 but its live event runs in
        # physical room 0x06C (confirmed by owner F12 after the kill).
        "room": 0x06C,
        "logical_room": 0x067,
        "door_dungeon": 12,
        "door_region": 540,
        "region": "GanonsTower_Lobby",
        "source_name": "GT Lanmolas 2",
        "can_reach": world_state_predicate(
            "CanShootArrowsL1() AND CanLightTorches() AND HAS_ITEM(BigKey_GanonsTower) "
            "AND HAS_AMOUNT(SmallKey_GanonsTower, 3) AND CanKillLanmolas(world)",
            "CanShootArrowsL1() AND CanLightTorches() AND HAS_ITEM(BigKey_GanonsTower) "
            "AND HAS_AMOUNT(SmallKey_GanonsTower, 3) AND CanKillLanmolas(world) "
            "AND (HAS_ITEM(MoonPearl) OR CanBunnyRevival(world) OR CanPearlBypass())",
        ),
    },
    {
        "name": "Enemy Check - Ganon's Tower Moldorm Enemy",
        "boss_kind": "gt_miniboss",
        "game_dungeon": 13,
        "rando_dungeon": 12,
        # GT Moldorm's logical room 0x06A is rendered in physical room 0x04D.
        # The death explosion hook therefore observes 0x04D.
        "room": 0x04D,
        "logical_room": 0x06A,
        "door_dungeon": 12,
        "door_region": 547,
        "region": "GanonsTower_Lobby",
        "source_name": "GT Moldorm",
        "can_reach": world_state_predicate(
            "HAS_ITEM(Hookshot) AND CanShootArrowsL1() AND CanLightTorches() "
            "AND HAS_ITEM(BigKey_GanonsTower) AND HAS_AMOUNT(SmallKey_GanonsTower, 4) "
            "AND CanKillLanmolas(world) AND CanKillMoldorm()",
            "HAS_ITEM(Hookshot) AND CanShootArrowsL1() AND CanLightTorches() "
            "AND HAS_ITEM(BigKey_GanonsTower) AND HAS_AMOUNT(SmallKey_GanonsTower, 4) "
            "AND CanKillLanmolas(world) AND CanKillMoldorm() "
            "AND (HAS_ITEM(MoonPearl) OR CanBunnyRevival(world) OR CanPearlBypass())",
        ),
    },
]

# These are deliberately physical runtime rooms, not the logical room ids used
# by door regions and spoiler grouping. Keep the distinction load-bearing: using
# logical 0x064/0x067 made Ice Armos and Lanmolas 2 silently fail to dispatch.
GT_MINIBOSS_RUNTIME_ROOM_CONTRACT = {
    "GT Ice Armos": (0x01C, 0x064),
    "GT Lanmolas 2": (0x06C, 0x067),
    "GT Moldorm": (0x04D, 0x06A),
}

SCRIPTED_SPAWN_SPECS = {
    0x18: {
        "source_name": "Invisible Stalfos",
        "child_type": 0xA7,
        "child_name": "Red Stalfos",
        "child_count": 4,
        "trigger": "player proximity",
    },
    0x05: {
        "source_name": "Falling Stalfos Trap",
        "child_type": 0x85,
        "child_name": "Falling Stalfos",
        "child_count": 1,
        "trigger": "room trap activation",
    },
    0x06: {
        "source_name": "Bad Switch Rope Trap",
        "child_type": 0x6E,
        "child_name": "Rope",
        "child_count": 1,
        "trigger": "bad switch activation",
    },
}

# These authored spawners have stable runtime identity, but their trigger cannot
# be repeated after leaving the room. Eastern 0x0A9's four falling-Stalfos
# parents are armed only by opening the big chest: the room-transition reset
# clears byte_7E0B9E, while the already-open chest can never arm it again. A
# missed child would therefore make its placed item permanently unavailable.
# Keep the repeatable Falling Stalfos in room 0x00A and the proximity-triggered
# Red Stalfos in 0x0A8; only this reviewed parent class is unsafe.
SCRIPTED_SPAWN_PARENT_EXCLUSIONS = {
    (0x0A9, slot, 0x05): "one_shot_big_chest_trigger"
    for slot in range(2, 6)
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
        return HCE_ENEMY_KILL_PREDICATE
    if dungeon == 4:
        return "CanKillMostThings(world, 8)"
    return "CanKillMostThings(world, 5)"


def kill_predicate_for_overworld(region: str) -> str:
    if region == "HyruleCastleEscape":
        return HCE_ENEMY_KILL_PREDICATE
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
        r"CanKillHceThings\(\s*world\s*\)",
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


def enemy_can_reach_from_base(candidate: dict, dungeon: int, base_pred: str,
                              source: str, pot_rows: dict[int, list[dict]],
                              pot_requirements: dict[int, dict],
                              inventory_predicate: str | None = None,
                              inventory_source: str | None = None,
                              allow_throwable_pots: bool = True,
                              throwable_pots_predicate: str | None = None) -> dict:
    room = int(candidate["room"])
    base_access = strip_throwable_combat_terms(base_pred)
    source_type = int(candidate["source_type"])
    if inventory_predicate is None:
        inventory_pred, inventory_source = enemy_inventory_kill_predicate(source_type, dungeon)
    else:
        inventory_pred = inventory_predicate
        inventory_source = inventory_source or "reviewed_underworld"
    kill_branches = [inventory_pred]

    pot_meta = dict(pot_requirements.get(source_type) or {})
    pot_need = pot_meta.get("pots_needed")
    pot_pred = None
    pot_count = 0
    if pot_need is not None and allow_throwable_pots:
        if throwable_pots_predicate is None:
            pot_pred, pot_count = throwable_pot_predicate(room, int(pot_need), pot_rows)
        else:
            pot_count = len(pot_rows.get(room, []))
            if pot_count >= int(pot_need):
                pot_pred = throwable_pots_predicate
        if pot_pred:
            # A shuffled pot is itself a one-shot location. Until the logic
            # model carries per-pot consumption/disjointness, do not let the
            # same pot both grant a prerequisite item and later act as the
            # throwable that proves this enemy kill. POT_KEYS_ON() is the
            # shared effective-pot-sanity predicate (including the cave-
            # entrance forced-off rule), so the counted route remains usable
            # only when pots are vanilla and inventory combat is the sole
            # fallback while any pot tier is active.
            pot_pred = and_predicate(["NOT POT_KEYS_ON()", pot_pred])
            kill_branches.append(pot_pred)
    else:
        pot_count = len(pot_rows.get(room, []))

    kill_pred = or_predicate(kill_branches)
    soul_term = soul_term_for(source_type)
    reach_parts = [base_access, kill_pred] if soul_term is None else \
                  [base_access, soul_term, kill_pred]
    return {
        "can_reach": and_predicate(reach_parts),
        "base_can_reach": base_access,
        "base_predicate_source": source,
        "source_soul": SOUL_BY_SPECIES.get(int(source_type)),
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


def enemy_can_reach(candidate: dict, dungeon: int, pot_predicates: dict[int, list[dict]],
                    region_only_pot_rooms: dict[int, str], entry_room_predicates: dict[int, dict],
                    pot_rows: dict[int, list[dict]],
                    pot_requirements: dict[int, dict]) -> dict | None:
    room = int(candidate["room"])
    base_pred, source = base_can_reach(
        room, dungeon, pot_predicates, region_only_pot_rooms, entry_room_predicates)
    if not base_pred:
        return None
    return enemy_can_reach_from_base(
        candidate, dungeon, base_pred, source, pot_rows, pot_requirements)


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


def scripted_enemy_check_name(row: dict) -> str:
    return (
        f"Enemy Check - Room 0x{int(row['room']):03X} "
        f"Script {int(row['parent_source_slot']):02d} Child {int(row['child_index'])} - "
        f"{row['child_name']}"
    )


def dungeon_candidate_key(row: dict) -> tuple[int, int]:
    return (int(row["room"]), int(row["source_slot"]))


def collect_scripted_spawn_candidates(
        assets: dict[str, bytes]) -> tuple[list[dict], Counter, list[dict]]:
    try:
        sprites = assets["kDungeonSprites"]
        offsets = parse_u16le_array(assets["kDungeonSpriteOffs"])
    except KeyError as e:
        die(f"missing asset {e.args[0]} in zelda3_assets.dat")

    rows: list[dict] = []
    excluded: Counter = Counter()
    excluded_rows: list[dict] = []
    observed_one_shot_big_chest_parents: set[tuple[int, int, int]] = set()
    for room in range(len(offsets)):
        for list_index, y, x, typ in sprite_entries(sprites, offsets, room):
            if x < 0xE0:
                continue
            parent_key = (room, int(list_index), typ)
            if room == 0x0A9 and typ == 0x05:
                observed_one_shot_big_chest_parents.add(parent_key)
            reason = SCRIPTED_SPAWN_PARENT_EXCLUSIONS.get(parent_key)
            if reason is not None:
                excluded[reason] += 1
                excluded_rows.append({
                    "room": room,
                    "parent_source_slot": int(list_index),
                    "overlord_type": typ,
                    "parent_y": y,
                    "parent_x": x,
                    "runtime_identity": (
                        f"scripted-parent:0x{room:03X}:{list_index}:0x{typ:02X}"
                    ),
                    "reason": reason,
                })
                continue
            spec = SCRIPTED_SPAWN_SPECS.get(typ)
            if spec is None:
                reason = ("bomb_trap_not_enemy" if typ == 0x1A else
                          "unsupported_scripted_spawner")
                excluded[reason] += 1
                excluded_rows.append({
                    "room": room,
                    "parent_source_slot": int(list_index),
                    "overlord_type": typ,
                    "parent_y": y,
                    "parent_x": x,
                    "runtime_identity": (
                        f"scripted-parent:0x{room:03X}:{list_index}:0x{typ:02X}"
                    ),
                    "reason": reason,
                })
                continue
            for child_index in range(int(spec["child_count"])):
                rows.append({
                    "domain": "scripted_spawn",
                    "room": room,
                    "parent_source_slot": int(list_index),
                    "overlord_type": typ,
                    "parent_source_name": spec["source_name"],
                    "parent_y": y,
                    "parent_x": x,
                    "child_index": child_index,
                    "child_type": int(spec["child_type"]),
                    "child_name": spec["child_name"],
                    "trigger": spec["trigger"],
                    "runtime_identity": (
                        f"scripted:0x{room:03X}:{list_index}:0x{typ:02X}:{child_index}"
                    ),
                })
    if observed_one_shot_big_chest_parents != set(SCRIPTED_SPAWN_PARENT_EXCLUSIONS):
        die(
            "Eastern big-chest Falling Stalfos parent identities drifted: "
            f"observed {sorted(observed_one_shot_big_chest_parents)}, expected "
            f"{sorted(SCRIPTED_SPAWN_PARENT_EXCLUSIONS)}"
        )
    excluded_rows.sort(
        key=lambda r: (int(r["room"]), int(r["parent_source_slot"]),
                       int(r["overlord_type"])))
    excluded_identities = [str(r["runtime_identity"]) for r in excluded_rows]
    if len(excluded_identities) != len(set(excluded_identities)):
        die("scripted-spawn exclusion identities are not unique")
    if len(excluded_rows) != sum(excluded.values()):
        die(
            "scripted-spawn exclusion rows/counts drifted: "
            f"{len(excluded_rows)} rows != {sum(excluded.values())} counted")
    return rows, excluded, excluded_rows


def make_doc(assets: dict[str, bytes], assets_path: Path, key_depth_path: Path,
             dungeon_rows: list[dict], dungeon_all_rows: list[dict],
             excluded_counts: Counter, all_scope_excluded_counts: Counter,
             overworld_rows: list[dict], overworld_excluded_counts: Counter,
             key_depth: dict[str, dict], pot_predicates: dict[int, list[dict]],
             region_only_pot_rooms: dict[int, str], entry_room_predicates: dict[int, dict],
             pot_rows: dict[int, list[dict]],
             pot_requirements: dict[int, dict]) -> dict:
    by_room = room_key_depth_rows(key_depth)
    rows = []
    doc_excluded_counts = Counter(all_scope_excluded_counts)
    out_of_scope_no_key_depth = []
    audit_only_no_room_predicate = []
    all_tier_underworld_candidates: dict[tuple[int, int], dict] = {}
    all_tier_underworld_rescue_reasons: dict[tuple[int, int], str] = {}
    all_tier_underworld_room_rows: dict[tuple[int, int], list[dict]] = {}
    all_scope_by_key = {dungeon_candidate_key(r): r for r in dungeon_all_rows}
    base_dungeon_keys = {dungeon_candidate_key(r) for r in dungeon_rows}
    curated_check_only_keys = {
        key for key, row in all_scope_by_key.items()
        if int(row["source_type"]) in ALL_TIER_CHECK_ONLY_SOURCE_CONSTRAINTS
    }
    # Preserve every existing generated location id by appending newly curated
    # check-only classes after all established domains rather than interleaving
    # them into the room-sorted automatic all-tier block.
    automatic_all_tier_keys = (
        set(all_scope_by_key) - base_dungeon_keys - curated_check_only_keys)
    automatic_all_tier_no_key_depth = []
    automatic_all_tier_no_room_predicate = []
    emitted_by_room: Counter[int] = Counter()
    no_key_depth_by_room: Counter[int] = Counter()
    audit_only_by_room: Counter[int] = Counter()
    automatic_all_tier_emitted = 0

    def append_key_depth_dungeon_row(candidate: dict, room_rows: list[dict],
                                     best: dict, dungeon: int, reach: dict,
                                     *, all_tier_only: bool = False,
                                     reviewed_binding_reason: str | None = None) -> None:
        nonlocal automatic_all_tier_emitted
        room = int(candidate["room"])
        loc_id = ENEMY_CHECK_BASE_ID + len(rows)
        emitted_by_room[room] += 1
        row = {
            "id": loc_id,
            "name": enemy_check_name(candidate, emitted_by_room[room]),
            "domain": "dungeon",
            "type": "Enemy",
            "room": room,
            "source_slot": int(candidate["source_slot"]),
            "source_type": int(candidate["source_type"]),
            "source_soul": SOUL_BY_SPECIES.get(int(candidate["source_type"])),
            "source_name": candidate["source_name"],
            "source_y": int(candidate["source_y"]),
            "source_x": int(candidate["source_x"]),
            "door_dungeon": dungeon,
            "door_region": int(best["region"]),
            "door_regions": [
                int(r["region"]) for r in sorted(room_rows, key=lambda r: int(r["region"]))
            ],
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
        }
        if all_tier_only:
            row["all_tier_only"] = True
            if reviewed_binding_reason:
                row["reviewed_binding_reason"] = reviewed_binding_reason
            automatic_all_tier_emitted += 1
        rows.append(row)

    for candidate in sorted(dungeon_rows, key=lambda r: (int(r["room"]), int(r["source_slot"]))):
        room = int(candidate["room"])
        source_slot = int(candidate["source_slot"])
        all_tier_binding = ALL_TIER_UNDERWORLD_BINDINGS.get((room, source_slot))
        room_rows = by_room.get(room, [])
        if not room_rows:
            if all_tier_binding is not None:
                key = (room, source_slot)
                all_tier_underworld_candidates[key] = candidate
                all_tier_underworld_rescue_reasons[key] = "reviewed_no_key_depth_room"
                all_tier_underworld_room_rows[key] = []
                continue
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
            if all_tier_binding is not None:
                key = (room, source_slot)
                all_tier_underworld_candidates[key] = candidate
                all_tier_underworld_rescue_reasons[key] = "reviewed_no_conservative_room_predicate"
                all_tier_underworld_room_rows[key] = room_rows
                continue
            audit_only_by_room[room] += 1
            doc_excluded_counts["no_conservative_room_predicate"] += 1
            audit_only_no_room_predicate.append(candidate)
            continue

        append_key_depth_dungeon_row(candidate, room_rows, best, dungeon, reach)

    for key in sorted(automatic_all_tier_keys):
        candidate = all_scope_by_key[key]
        room, source_slot = key
        all_tier_binding = ALL_TIER_UNDERWORLD_BINDINGS.get((room, source_slot))
        room_rows = by_room.get(room, [])
        if not room_rows:
            if all_tier_binding is not None:
                all_tier_underworld_candidates[key] = candidate
                all_tier_underworld_rescue_reasons[key] = "reviewed_no_key_depth_room"
                all_tier_underworld_room_rows[key] = []
                continue
            doc_excluded_counts["all_tier_no_key_depth_room"] += 1
            automatic_all_tier_no_key_depth.append(candidate)
            continue
        best = best_room_key_depth(room_rows)
        dungeon = int(best["dungeon"])
        if dungeon not in SMALL_KEY_ITEMS or dungeon not in DUNGEON_REGIONS:
            doc_excluded_counts["all_tier_unsupported_dungeon"] += 1
            automatic_all_tier_no_key_depth.append(candidate)
            continue
        reach = enemy_can_reach(
            candidate, dungeon, pot_predicates, region_only_pot_rooms,
            entry_room_predicates, pot_rows, pot_requirements)
        if reach is None:
            if all_tier_binding is not None:
                all_tier_underworld_candidates[key] = candidate
                all_tier_underworld_rescue_reasons[key] = "reviewed_no_conservative_room_predicate"
                all_tier_underworld_room_rows[key] = room_rows
                continue
            doc_excluded_counts["all_tier_no_conservative_room_predicate"] += 1
            automatic_all_tier_no_room_predicate.append(candidate)
            continue

        append_key_depth_dungeon_row(
            candidate, room_rows, best, dungeon, reach,
            all_tier_only=True,
            reviewed_binding_reason="automatic_all_tier_source_type")

    gt_miniboss_emitted = 0
    for boss in GT_MINIBOSS_EVENT_CHECKS:
        if boss.get("boss_kind") != "gt_miniboss":
            die(f"non-GT boss event must not be emitted as an enemy check: {boss.get('name')}")
        expected_rooms = GT_MINIBOSS_RUNTIME_ROOM_CONTRACT.get(boss["source_name"])
        actual_rooms = (int(boss["room"]), int(boss["logical_room"]))
        if expected_rooms != actual_rooms:
            die(
                f"GT miniboss runtime/logical room drift for {boss['source_name']}: "
                f"got {actual_rooms}, expected {expected_rooms}"
            )
        rows.append({
            "id": ENEMY_CHECK_BASE_ID + len(rows),
            "name": boss["name"],
            "domain": "boss",
            "type": "Enemy",
            "boss_kind": boss["boss_kind"],
            "game_dungeon": int(boss["game_dungeon"]),
            "rando_dungeon": int(boss["rando_dungeon"]),
            "room": int(boss["room"]),
            "logical_room": int(boss["logical_room"]),
            "door_dungeon": int(boss["door_dungeon"]),
            "door_region": int(boss["door_region"]),
            "region": boss["region"],
            "source_name": boss["source_name"],
            "vanilla_item": "Nothing",
            "can_reach": boss["can_reach"],
            "base_can_reach": boss["can_reach"],
            "predicate_source": "boss_event",
            "kill_predicate": "boss_event_predicate",
            "all_tier_only": True,
            "marker_policy": "suppressed",
        })
        gt_miniboss_emitted += 1

    scripted_rows, scripted_excluded_counts, scripted_excluded_rows = (
        collect_scripted_spawn_candidates(assets))
    scripted_emitted = 0
    for candidate in sorted(
            scripted_rows,
            key=lambda r: (int(r["room"]), int(r["parent_source_slot"]), int(r["child_index"]))):
        room = int(candidate["room"])
        room_rows = by_room.get(room, [])
        if not room_rows:
            doc_excluded_counts["scripted_no_key_depth_room"] += 1
            continue
        best = best_room_key_depth(room_rows)
        dungeon = int(best["dungeon"])
        if dungeon not in SMALL_KEY_ITEMS or dungeon not in DUNGEON_REGIONS:
            doc_excluded_counts["scripted_unsupported_dungeon"] += 1
            continue
        pseudo = {
            "room": room,
            "source_type": int(candidate["child_type"]),
            "source_soul": SOUL_BY_SPECIES.get(int(candidate["child_type"])),
        }
        reach = enemy_can_reach(
            pseudo, dungeon, pot_predicates, region_only_pot_rooms,
            entry_room_predicates, pot_rows, pot_requirements)
        if reach is None:
            doc_excluded_counts["scripted_no_conservative_room_predicate"] += 1
            continue
        rows.append({
            "id": ENEMY_CHECK_BASE_ID + len(rows),
            "name": scripted_enemy_check_name(candidate),
            "domain": "scripted_spawn",
            "type": "Enemy",
            "room": room,
            "parent_source_slot": int(candidate["parent_source_slot"]),
            "overlord_type": int(candidate["overlord_type"]),
            "parent_source_name": candidate["parent_source_name"],
            "parent_y": int(candidate["parent_y"]),
            "parent_x": int(candidate["parent_x"]),
            "child_index": int(candidate["child_index"]),
            "child_type": int(candidate["child_type"]),
            "child_name": candidate["child_name"],
            "trigger": candidate["trigger"],
            "door_dungeon": dungeon,
            "door_region": int(best["region"]),
            "door_regions": [int(r["region"]) for r in sorted(room_rows, key=lambda r: int(r["region"]))],
            "region": DUNGEON_REGIONS[dungeon],
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
            "all_tier_only": True,
            "marker_policy": "child_carrier",
        })
        scripted_emitted += 1

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
        ow_soul_term = soul_term_for(int(candidate["source_type"]))
        ow_parts = [base_access, kill_pred] if ow_soul_term is None else \
                   [base_access, ow_soul_term, kill_pred]
        can_reach = and_predicate(ow_parts)
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
            "source_soul": SOUL_BY_SPECIES.get(int(candidate["source_type"])),
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

    underworld_all_tier_emitted = 0
    for key in sorted(ALL_TIER_UNDERWORLD_BINDINGS):
        candidate = all_tier_underworld_candidates.get(key)
        if candidate is None:
            room, source_slot = key
            die(
                "reviewed all-tier underworld enemy candidate "
                f"room=0x{room:03x} slot={source_slot} was not found")
        binding = ALL_TIER_UNDERWORLD_BINDINGS[key]
        reach = enemy_can_reach_from_base(
            candidate, 0, str(binding["base_can_reach"]),
            str(binding["predicate_source"]), pot_rows, pot_requirements,
            inventory_predicate=binding.get("inventory_kill_predicate"),
            inventory_source=binding.get("inventory_kill_source"),
            allow_throwable_pots=bool(binding.get("allow_throwable_pots", True)),
            throwable_pots_predicate=binding.get("throwable_pots_can_reach"))
        room = int(candidate["room"])
        emitted_by_room[room] += 1
        row = {
            "id": ENEMY_CHECK_BASE_ID + len(rows),
            "name": enemy_check_name(candidate, emitted_by_room[room]),
            "domain": "dungeon",
            "type": "Enemy",
            "room": room,
            "source_slot": int(candidate["source_slot"]),
            "source_type": int(candidate["source_type"]),
            "source_soul": SOUL_BY_SPECIES.get(int(candidate["source_type"])),
            "source_name": candidate["source_name"],
            "source_y": int(candidate["source_y"]),
            "source_x": int(candidate["source_x"]),
            "region": str(binding["region"]),
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
            "reviewed_binding_reason": all_tier_underworld_rescue_reasons.get(
                key, "reviewed_all_tier_binding"),
            "all_tier_only": True,
        }
        if binding.get("use_key_depth"):
            room_rows = all_tier_underworld_room_rows.get(key, [])
            if not room_rows:
                die(
                    "reviewed key-depth enemy binding has no key-depth ROOM row "
                    f"room=0x{room:03x} slot={key[1]}")
            best = best_room_key_depth(room_rows)
            dungeon = int(best["dungeon"])
            if dungeon not in SMALL_KEY_ITEMS:
                die(
                    "reviewed key-depth enemy binding has unsupported dungeon "
                    f"room=0x{room:03x} slot={key[1]} dungeon={dungeon}")
            row.update({
                "door_dungeon": dungeon,
                "door_region": int(best["region"]),
                "door_regions": [
                    int(r["region"]) for r in sorted(
                        room_rows, key=lambda r: int(r["region"]))
                ],
                "small_key_item": SMALL_KEY_ITEMS[dungeon],
                "key_depth": int(best["key_depth"]),
                "key_mindepth": int(best["key_mindepth"]),
            })
        rows.append(row)
        underworld_all_tier_emitted += 1

    # Curated check-only source types are append-only. They are required to use
    # an already modeled key-depth room/access predicate; otherwise generation
    # fails instead of silently restoring the original "all but these" gap.
    for key in sorted(curated_check_only_keys):
        candidate = all_scope_by_key[key]
        room_rows = by_room.get(key[0], [])
        if not room_rows:
            die(
                "curated all-tier check-only source has no key-depth ROOM row: "
                f"room=0x{key[0]:03x} slot={key[1]}")
        best = best_room_key_depth(room_rows)
        dungeon = int(best["dungeon"])
        if dungeon not in SMALL_KEY_ITEMS or dungeon not in DUNGEON_REGIONS:
            die(
                "curated all-tier check-only source has unsupported dungeon: "
                f"room=0x{key[0]:03x} slot={key[1]} dungeon={dungeon}")
        reach = enemy_can_reach(
            candidate, dungeon, pot_predicates, region_only_pot_rooms,
            entry_room_predicates, pot_rows, pot_requirements)
        if reach is None:
            die(
                "curated all-tier check-only source has no conservative room predicate: "
                f"room=0x{key[0]:03x} slot={key[1]}")
        append_key_depth_dungeon_row(
            candidate, room_rows, best, dungeon, reach,
            all_tier_only=True,
            reviewed_binding_reason="curated_check_only_source_type")

    # Conservative pot-consumption guard. Every emitted throwable-pot branch
    # must carry the effective pots-off predicate both in its audit metadata
    # and in the compiled kill expression. This catches a future generator
    # refactor that accidentally restores the pot-sanity double count.
    for row in rows:
        pot_branch = row.get("throwable_pots_can_reach")
        if not pot_branch:
            continue
        if "NOT POT_KEYS_ON()" not in str(pot_branch):
            die(
                "throwable-pot route is missing the effective pots-off guard: "
                f"location {row.get('id')} {row.get('name')}")
        if str(pot_branch) not in str(row.get("kill_predicate") or ""):
            die(
                "throwable-pot route metadata drifted from the kill predicate: "
                f"location {row.get('id')} {row.get('name')}")

    underworld_all_tier_total = automatic_all_tier_emitted + underworld_all_tier_emitted
    dungeon_emitted = sum(
        1 for r in rows if r.get("domain") == "dungeon" and not r.get("all_tier_only"))
    source_types = {
        int(r.get("source_type", r.get("child_type", 0)))
        for r in rows
        if "source_type" in r or "child_type" in r
    }
    expected_check_only = {
        dungeon_candidate_key(r)
        for r in dungeon_all_rows
        if int(r["source_type"]) in ALL_TIER_CHECK_ONLY_SOURCE_CONSTRAINTS
    }
    emitted_check_only = {
        (int(r["room"]), int(r["source_slot"]))
        for r in rows
        if r.get("domain") == "dungeon" and
           int(r.get("source_type", -1)) in ALL_TIER_CHECK_ONLY_SOURCE_CONSTRAINTS
    }
    if expected_check_only != emitted_check_only:
        missing = sorted(expected_check_only - emitted_check_only)
        extra = sorted(emitted_check_only - expected_check_only)
        die(
            "all-tier check-only source coverage drifted: "
            f"missing={missing} extra={extra}")
    return {
        "format_version": 1,
        "_generated_by": "assets/scripts/gen_enemy_check_tables.py (do not hand-edit)",
        "source": {
            "assets": str(assets_path.name),
            "sha256": asset_payload_sha256(assets, ENEMY_CHECK_ASSET_KEYS),
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
            "scope": "dungeon_plus_all-tier_static_overworld_reviewed_underworld_boss_and_finite_scripted",
            "eligible_source_type": [
                "ESF_RANDOMIZABLE + ESF_KILLABLE",
                "curated static all-tier check-only source types",
            ],
            "all_tier_check_only_source_types": {
                f"0x{typ:02X}": meta["name"]
                for typ, meta in sorted(ALL_TIER_CHECK_ONLY_SOURCE_CONSTRAINTS.items())
            },
            "excluded_source_type_flags": {
                "dungeon": ["ESF_CANNOT_KEY", "ESF_FLYING"],
                "dungeon_all_tier": [],
                "overworld_all_tier": [],
            },
            "excluded_sources": [
                "existing forced-key enemy-drop checks",
                "overlords/control markers",
                "one-shot scripted spawns whose trigger cannot be repeated after a missed kill",
                "underworld sprite-table rooms with no key-depth ROOM row and no reviewed all-tier binding",
                "overworld sprite-table rows with no stable active-list runtime identity",
                "farmable, unbounded, projectile, bomb-trap, and non-killable dynamic spawns",
            ],
            "runtime_identity": {
                "dungeon": "(dungeon_room_index, sprite_N source slot)",
                "underworld_all_tier": "(dungeon_room_index, sprite_N source slot)",
                "overworld": "(active overworld sprite-list stage, overworld_area_index, source_slot, sprite_N_word block)",
                "boss": "(current destination game dungeon or GT miniboss room event)",
                "scripted_spawn": "(dungeon_room_index, parent overlord source-list slot, overlord type, child index)",
            },
            "kill_logic": [
                "dungeon: per-source inventory predicate",
                "dungeon: OR thrown-pot kill route when engine damage tables show liftable pots deal normal HP damage and effective pot shuffle is off",
                "dungeon: thrown-pot route requires at least the generated pots_needed count in the room",
                "dungeon all-tier: key-banned/flying killable sources reuse the same modeled room access as dungeon checks and emit only when enemy_drop_checks=all",
                "underworld all-tier: reviewed room access plus per-source or binding-specific inventory/thrown-pot kill route",
                "overworld: region-reachable plus generic overworld combat predicate",
                "boss: existing boss and GT miniboss kill predicates",
                "scripted_spawn: parent room reachability plus child enemy kill route",
            ],
            "base_room_predicates": [
                "reviewed forced-key room binding",
                "reviewed pot-room predicate",
                "dungeon entry room with matching entrance_registry entry_region",
                "single-region pot room with no local can_reach gate",
            ],
        },
        "summary": {
            "scanned_underworld_source_count": len(dungeon_all_rows),
            "scanned_base_dungeon_source_count": len(dungeon_rows),
            "scanned_overworld_source_count": len(overworld_rows),
            "candidate_count": len(dungeon_all_rows) + len(overworld_rows) + len(scripted_rows) + len(GT_MINIBOSS_EVENT_CHECKS),
            "emitted_count": len(rows),
            "emitted_dungeon_count": dungeon_emitted,
            "emitted_underworld_all_tier_count": underworld_all_tier_total,
            "emitted_underworld_all_tier_automatic_count": automatic_all_tier_emitted,
            "emitted_underworld_all_tier_reviewed_count": underworld_all_tier_emitted,
            "emitted_curated_check_only_count": len(curated_check_only_keys),
            "emitted_overworld_count": overworld_emitted,
            # Compatibility key consumed by audit_enemy_check_candidates.py;
            # the emitted boss-domain rows are GT minibosses only.
            "emitted_boss_count": gt_miniboss_emitted,
            "emitted_scripted_spawn_count": scripted_emitted,
            "excluded_scripted_spawn_source_count": len(scripted_excluded_rows),
            "excluded_count": (
                sum(doc_excluded_counts.values()) + sum(overworld_excluded_counts.values()) +
                sum(scripted_excluded_counts.values())
            ),
            "out_of_scope_no_key_depth_count": len(out_of_scope_no_key_depth),
            "audit_only_no_room_predicate_count": len(audit_only_no_room_predicate),
            "emitted_rooms": len(emitted_by_room),
            "out_of_scope_no_key_depth_rooms": len(no_key_depth_by_room),
            "audit_only_no_room_predicate_rooms": len(audit_only_by_room),
            "source_types": len(source_types),
            "rows_with_throwable_pot_route": sum(1 for r in rows if r.get("throwable_pots_can_reach")),
            "rows_with_pot_shuffle_guarded_throwable_route": sum(
                1 for r in rows
                if "NOT POT_KEYS_ON()" in str(r.get("throwable_pots_can_reach") or "")),
            "rows_pot_killable_by_damage_table": sum(1 for r in rows if r.get("throwable_pots_required") is not None),
            "rows_with_special_inventory_kill": sum(1 for r in rows if r.get("inventory_kill_source") == "source_type_special"),
            "reviewed_all_tier_no_key_depth_count": sum(
                1 for reason in all_tier_underworld_rescue_reasons.values()
                if reason == "reviewed_no_key_depth_room"),
            "reviewed_all_tier_no_conservative_room_predicate_count": sum(
                1 for reason in all_tier_underworld_rescue_reasons.values()
                if reason == "reviewed_no_conservative_room_predicate"),
        },
        "excluded_counts": {
            "dungeon": dict(sorted(doc_excluded_counts.items())),
            "overworld": dict(sorted(overworld_excluded_counts.items())),
            "scripted_spawn": dict(sorted(scripted_excluded_counts.items())),
        },
        "enemy_checks": rows,
        "scripted_spawn_exclusions": scripted_excluded_rows,
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
        "reviewed_all_tier_rescued": [
            {
                "room": int(all_tier_underworld_candidates[key]["room"]),
                "source_slot": int(all_tier_underworld_candidates[key]["source_slot"]),
                "source_type": int(all_tier_underworld_candidates[key]["source_type"]),
                "source_name": all_tier_underworld_candidates[key]["source_name"],
                "source_y": int(all_tier_underworld_candidates[key]["source_y"]),
                "source_x": int(all_tier_underworld_candidates[key]["source_x"]),
                "reason": all_tier_underworld_rescue_reasons.get(
                    key, "reviewed_all_tier_binding"),
                "predicate_source": str(ALL_TIER_UNDERWORLD_BINDINGS[key]["predicate_source"]),
                "use_key_depth": bool(ALL_TIER_UNDERWORLD_BINDINGS[key].get("use_key_depth")),
            }
            for key in sorted(all_tier_underworld_candidates)
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
    dungeon_all_rows, all_scope_excluded_counts = collect_dungeon_candidates(
        assets,
        constraints,
        forced_sources,
        allow_cannot_key=True,
        allow_flying=True,
        extra_source_constraints=ALL_TIER_CHECK_ONLY_SOURCE_CONSTRAINTS,
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
        assets,
        {int(row["source_type"]) for row in itertools.chain(dungeon_rows, dungeon_all_rows)})
    return make_doc(
        assets,
        assets_path,
        key_depth_path,
        dungeon_rows,
        dungeon_all_rows,
        excluded_counts,
        all_scope_excluded_counts,
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
        f"{summary['emitted_underworld_all_tier_count']} underworld all-tier + "
        f"{summary['emitted_overworld_count']} overworld + "
        f"{summary['emitted_boss_count']} GT miniboss + "
        f"{summary['emitted_scripted_spawn_count']} scripted ordinary enemy checks "
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
