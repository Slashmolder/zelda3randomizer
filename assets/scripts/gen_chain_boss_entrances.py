#!/usr/bin/env python3
"""Generate synthetic entrance records for dungeon-chain boss terminals."""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from functools import lru_cache
from pathlib import Path

import yaml


REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "assets"))
import tables  # noqa: E402


ENTRANCE_BASE = 133
LAYOUT_QUADRANT_FLAGS = [
    0xF, 0xF, 0xF, 0xF, 0xB, 0xB, 0x7, 0x7,
    0xF, 0xB, 0xF, 0x7, 0xB, 0xF, 0x7, 0xF,
    0xE, 0xD, 0xE, 0xD, 0xF, 0xF, 0xE, 0xD,
    0xE, 0xD, 0xF, 0xF, 0xA, 0x9, 0x6, 0x5,
]


@dataclass(frozen=True)
class BossSpec:
    name: str
    enum_name: str
    rando_id: int
    boss_room: int
    home_entrance: int
    entry_kind: str


BOSSES = [
    BossSpec("Eastern Palace", "kRandoDungeon_EasternPalace", 1, 0xC8, 8, "door"),
    BossSpec("Desert Palace", "kRandoDungeon_DesertPalace", 2, 0x33, 9, "door"),
    BossSpec("Tower of Hera", "kRandoDungeon_TowerOfHera", 3, 0x07, 51, "stair"),
    BossSpec("Palace of Darkness", "kRandoDungeon_PalaceOfDarkness", 5, 0x5A, 38, "door"),
    BossSpec("Swamp Palace", "kRandoDungeon_SwampPalace", 6, 0x06, 37, "door"),
    BossSpec("Thieves Town", "kRandoDungeon_ThievesTown", 8, 0xAC, 52, "door"),
    BossSpec("Ice Palace", "kRandoDungeon_IcePalace", 9, 0xDE, 45, "hole"),
    BossSpec("Misery Mire", "kRandoDungeon_MiseryMire", 10, 0x90, 39, "door"),
    BossSpec("Turtle Rock", "kRandoDungeon_TurtleRock", 11, 0xA4, 53, "door"),
]


def load_room(room: int) -> dict:
    path = REPO / "assets" / "dungeon" / f"dungeon-{room}.yaml"
    with path.open("r", encoding="utf-8") as fp:
        return yaml.safe_load(fp)


@lru_cache(maxsize=1)
def load_vanilla_entrances() -> dict[int, dict]:
    out = {}
    for path in (REPO / "assets" / "dungeon").glob("dungeon-*.yaml"):
        source_room = int(path.stem.split("-")[1])
        with path.open("r", encoding="utf-8") as fp:
            data = yaml.safe_load(fp)
        for entrance in data.get("Entrances", []):
            idx = entrance["entrance_index"]
            if idx in out:
                raise ValueError(f"duplicate vanilla entrance {idx}")
            row = dict(entrance)
            row["_source_room"] = source_room
            out[idx] = row
    return out


def load_entrance(index: int) -> dict:
    try:
        return load_vanilla_entrances()[index]
    except KeyError:
        raise ValueError(f"vanilla entrance {index} not found") from None


def parse_outbound_slots() -> dict[int, int]:
    path = REPO / "src" / "rando" / "chain_seams.gen.c"
    text = path.read_text(encoding="utf-8")
    marker = "const ChainSeamRow kChainBossOutboundSeams"
    start = text.find(marker)
    if start < 0:
        raise ValueError(f"{path} is missing kChainBossOutboundSeams")
    open_brace = text.find("{", start)
    end = text.find("};", open_brace)
    if open_brace < 0 or end < 0:
        raise ValueError(f"{path} has malformed kChainBossOutboundSeams")

    slots = {}
    for line in text[open_brace + 1:end].splitlines():
        match = re.search(
            r"\{kChainSeamKind_Door,\s*(kRandoDungeon_\w+),\s*"
            r"0x([0-9A-Fa-f]+),\s*0x([0-9A-Fa-f]+),\s*\d+,\s*"
            r"kDoorTblDir_South,\s*(\d+)\}",
            line,
        )
        if not match:
            continue
        enum_name = match.group(1)
        source_room = int(match.group(2), 16)
        slot = int(match.group(4))
        for spec in BOSSES:
            if spec.enum_name == enum_name and spec.boss_room == source_room:
                slots[spec.rando_id] = slot
                break
    return slots


def clamp(value: int, lo: int, hi: int) -> int:
    return max(lo, min(hi, value))


def quadrant_names(room_data: dict, player_x: int, player_y: int) -> list[str]:
    header = room_data["Header"]
    qx = 1 if player_x >= 0x100 else 0
    qy = 2 if player_y >= 0x100 else 0
    composite = header["layout"] * 4 + header["start_quadrant"] | qx | qy
    if composite >= len(LAYOUT_QUADRANT_FLAGS):
        raise ValueError(f"bad room layout composite {composite}")
    flags = LAYOUT_QUADRANT_FLAGS[composite]
    full_x = "double_x" if (flags & (2 if qx else 1)) == 0 else "single_x"
    full_y = "double_y" if (flags & (8 if qy else 4)) == 0 else "single_y"
    quadrant = (
        ("lower_" if qy else "upper_") +
        ("right" if qx else "left")
    )
    return [full_x, full_y, quadrant]


def camera_record(room_data: dict, player_x: int, player_y: int) -> tuple[list[int], list[int]]:
    scroll_x = clamp(player_x - 120, 0, 256)
    scroll_y = clamp(player_y - 109, 0, 272)
    return [scroll_x, scroll_y], [scroll_x + 127, scroll_y + 119]


def door_spawn(slot: int) -> tuple[int, int]:
    if slot not in (0, 1, 2):
        raise ValueError(f"bad boss door slot {slot}")
    return 120 + slot * 128, 472


def hera_stair_spawn(room_data: dict) -> tuple[int, int]:
    for obj in room_data.get("Layer1", []):
        if obj.get("n") == "139-Door Staircase Down L":
            return obj["x"] * 8 + 16, obj["y"] * 8 + 32
    raise ValueError("Tower of Hera boss staircase object not found")


def ice_hole_spawn(room_data: dict) -> tuple[int, int]:
    for obj in room_data.get("Layer2", []):
        if str(obj.get("n", "")).startswith("F95-Kholdstare Shell"):
            return obj["x"] * 8 + 32, obj["y"] * 8 + 86
    raise ValueError("Ice Palace boss shell object not found")


def palace_raw(palace: str) -> int:
    idx = tables.kPalaceNames.index(palace)
    return -1 if idx == 0 else (idx - 1) * 2


def music_id(music: str) -> int:
    return tables.kMusicNamesRev[music]


def build_entries() -> list[dict]:
    slots = parse_outbound_slots()
    entries = []
    for offset, spec in enumerate(BOSSES):
        room_data = load_room(spec.boss_room)
        home = load_entrance(spec.home_entrance)
        if spec.entry_kind == "door":
            player_x, player_y = door_spawn(slots[spec.rando_id])
            doorway_orientation = 1
        elif spec.entry_kind == "stair":
            player_x, player_y = hera_stair_spawn(room_data)
            doorway_orientation = 0
        elif spec.entry_kind == "hole":
            player_x, player_y = ice_hole_spawn(room_data)
            doorway_orientation = 0
        else:
            raise ValueError(f"{spec.name}: unknown entry kind {spec.entry_kind!r}")

        scroll_xy, camera_xy = camera_record(room_data, player_x, player_y)
        entries.append({
            "entrance_index": ENTRANCE_BASE + offset,
            "name": f"Chain Boss Entrance - {spec.name}",
            "rando_dungeon": spec.rando_id,
            "rando_dungeon_enum": spec.enum_name,
            "home_entrance": spec.home_entrance,
            "main_exit_room": home["_source_room"],
            "room": spec.boss_room,
            "scroll_xy": scroll_xy,
            "player_xy": [player_x, player_y],
            "camera_xy": camera_xy,
            "blockset": home["blockset"],
            "music": home["music"],
            "palace": home["palace"],
            "doorway_orientation": doorway_orientation,
            "plane": 0,
            "ladder_level": 0,
            "quadrants": quadrant_names(room_data, player_x, player_y),
            "floor": home["floor"],
            "house_exit_door": ["none"],
        })
    return entries


def emit_yaml(entries: list[dict]) -> str:
    data = {
        "_generated_by": "assets/scripts/gen_chain_boss_entrances.py",
        "base_index": ENTRANCE_BASE,
        "count": len(entries),
        "entries": entries,
    }
    return yaml.safe_dump(data, sort_keys=False, width=100)


def emit_header(entries: list[dict]) -> str:
    rows = []
    for entry in entries:
        name = entry["name"]
        prefix = "Chain Boss Entrance - "
        if name.startswith(prefix):
            name = name[len(prefix):]
        rows.append(
            "  {%d, %s, %d, 0x%03X, 0x%03X, %d, %d}, // %s" % (
                entry["entrance_index"],
                entry["rando_dungeon_enum"],
                entry["home_entrance"],
                entry["main_exit_room"],
                entry["room"],
                palace_raw(entry["palace"]),
                music_id(entry["music"]),
                name,
            )
        )
    return "\n".join([
        "// chain_boss_entrances.gen.h - GENERATED by assets/scripts/gen_chain_boss_entrances.py. Do not edit.",
        "#pragma once",
        "",
        '#include "dungeon_ids.h"',
        '#include "../types.h"',
        "",
        f"#define kChainBossEntranceBase {ENTRANCE_BASE}",
        f"#define kChainBossEntranceCount {len(entries)}",
        "#define kChainBossEntranceLimit (kChainBossEntranceBase + kChainBossEntranceCount)",
        "",
        "typedef struct ChainBossEntranceCheck {",
        "  uint8 entrance_id;",
        "  uint8 rando_dungeon;",
        "  uint8 main_entrance_id;",
        "  uint16 main_exit_room;",
        "  uint16 room;",
        "  int8 palace;",
        "  uint8 music;",
        "} ChainBossEntranceCheck;",
        "",
        "static const ChainBossEntranceCheck kChainBossEntranceChecks[kChainBossEntranceCount] = {",
        *rows,
        "};",
        "",
    ])


def write_or_check(path: Path, content: str, check: bool) -> None:
    if check:
        if not path.exists():
            raise ValueError(f"{path} is missing")
        if path.read_text(encoding="utf-8") != content:
            raise ValueError(f"{path} is stale; rerun gen_chain_boss_entrances.py")
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="\n") as fp:
        fp.write(content)


def validate(entries: list[dict]) -> None:
    if len(entries) != 9:
        raise ValueError(f"expected 9 chain boss entrances, got {len(entries)}")
    for expected, entry in enumerate(entries, ENTRANCE_BASE):
        if entry["entrance_index"] != expected:
            raise ValueError(f"non-contiguous entrance id {entry['entrance_index']} at {expected}")
        if not (0 <= entry["player_xy"][0] <= 511 and 0 <= entry["player_xy"][1] <= 511):
            raise ValueError(f"{entry['name']}: player_xy out of room bounds")
        if entry["house_exit_door"] != ["none"]:
            raise ValueError(f"{entry['name']}: synthetic entrances must not cache an exit door")
        if not (0 <= entry["main_exit_room"] < 0x100):
            raise ValueError(f"{entry['name']}: main_exit_room must be a dungeon exit-search room")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()

    entries = build_entries()
    validate(entries)
    write_or_check(REPO / "assets" / "rando" / "chain_boss_entrances.gen.yaml",
                   emit_yaml(entries), args.check)
    write_or_check(REPO / "src" / "rando" / "chain_boss_entrances.gen.h",
                   emit_header(entries), args.check)
    action = "checked" if args.check else "wrote"
    print(f"gen_chain_boss_entrances: {action} {len(entries)} synthetic boss entrances")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except ValueError as exc:
        print(f"gen_chain_boss_entrances: FAIL: {exc}", file=sys.stderr)
        raise SystemExit(1)
