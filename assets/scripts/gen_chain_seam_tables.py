#!/usr/bin/env python3
"""Generate dungeon-chain boss seam tables from committed registries and assets."""

import argparse
import re
import struct
import sys
from dataclasses import dataclass
from pathlib import Path


REPO = Path(__file__).resolve().parents[2]

ASSET_DUNGEON_ROOM_HEADERS = 6
ASSET_DUNGEON_ROOM_HEADER_OFFS = 7
ASSET_ENTRANCE_ROOMS = 11

DOOR_FLAG_PORTAL = 0x100

DOOR_TYPE_NORMAL = 0
DOOR_TYPE_SPIRAL_STAIRS = 1
DOOR_TYPE_STRAIGHT_STAIRS = 2
DOOR_TYPE_HOLE = 5

DIR_NAMES = {
    0: "kDoorTblDir_North",
    1: "kDoorTblDir_South",
    2: "kDoorTblDir_West",
    3: "kDoorTblDir_East",
    4: "kDoorTblDir_Up",
    5: "kDoorTblDir_Down",
}
DIR_DELTAS = {0: -0x10, 1: 0x10, 2: -1, 3: 1}


@dataclass(frozen=True)
class PoolDungeon:
    name: str
    short_name: str
    enum_name: str
    rando_id: int
    boss_room: int


POOL = [
    PoolDungeon("Eastern Palace", "EP", "kRandoDungeon_EasternPalace", 1, 0xC8),
    PoolDungeon("Desert Palace", "DP", "kRandoDungeon_DesertPalace", 2, 0x33),
    PoolDungeon("Tower of Hera", "ToH", "kRandoDungeon_TowerOfHera", 3, 0x07),
    PoolDungeon("Palace of Darkness", "PoD", "kRandoDungeon_PalaceOfDarkness", 5, 0x5A),
    PoolDungeon("Swamp Palace", "SP", "kRandoDungeon_SwampPalace", 6, 0x06),
    PoolDungeon("Thieves Town", "TT", "kRandoDungeon_ThievesTown", 8, 0xAC),
    PoolDungeon("Ice Palace", "IP", "kRandoDungeon_IcePalace", 9, 0xDE),
    PoolDungeon("Misery Mire", "MM", "kRandoDungeon_MiseryMire", 10, 0x90),
    PoolDungeon("Turtle Rock", "TR", "kRandoDungeon_TurtleRock", 11, 0xA4),
]
POOL_BY_BOSS_ROOM = {d.boss_room: d for d in POOL}


@dataclass(frozen=True)
class DoorRow:
    door_id: int
    name: str
    region: int
    partner: int
    flags: int
    room: int
    door_type: int
    direction: int
    door_index: int
    rando_dungeon: int


@dataclass(frozen=True)
class SeamRow:
    kind: str
    dungeon: PoolDungeon
    source_room: int
    dest_room: int
    door_id: int
    direction: int
    slot: int
    name: str


def read_assets(path):
    data = path.read_bytes()
    if len(data) < 88:
        raise ValueError(f"{path} is too small to be a zelda3_assets.dat file")
    count = struct.unpack_from("<I", data, 80)[0]
    key_sig_len = struct.unpack_from("<I", data, 84)[0]
    offset = 88 + count * 4 + key_sig_len
    assets = []
    for i in range(count):
        size = struct.unpack_from("<I", data, 88 + i * 4)[0]
        offset = (offset + 3) & ~3
        if offset + size > len(data):
            raise ValueError(f"{path} asset {i} extends past EOF")
        assets.append(data[offset:offset + size])
        offset += size
    return assets


def u16_list(blob):
    if len(blob) % 2:
        raise ValueError("odd-length uint16 asset")
    return list(struct.unpack("<" + "H" * (len(blob) // 2), blob))


def load_header_destinations(assets):
    header_blob = assets[ASSET_DUNGEON_ROOM_HEADERS]
    offsets = u16_list(assets[ASSET_DUNGEON_ROOM_HEADER_OFFS])
    out = {}
    for room, off in enumerate(offsets):
        if off + 14 > len(header_blob):
            raise ValueError(f"room {room:#x} header extends past header asset")
        hdr = header_blob[off:off + 14]
        out[room] = tuple(hdr[9:14])
    return out


def load_entrance_rooms(assets):
    return u16_list(assets[ASSET_ENTRANCE_ROOMS])


def load_synthetic_entrance_range(path):
    text = path.read_text(encoding="utf-8")
    base = re.search(r"^base_index:\s*(\d+)\s*$", text, re.MULTILINE)
    count = re.search(r"^count:\s*(\d+)\s*$", text, re.MULTILINE)
    if base is None or count is None:
        raise ValueError(f"{path}: missing base_index/count")
    start = int(base.group(1))
    return range(start, start + int(count.group(1)))


def parse_door_registry(path):
    text = path.read_text(encoding="utf-8")
    rows = []
    for m in re.finditer(r'- \{ id: (\d+), name: "([^"]+)" \}', text):
        rows.append((int(m.group(1)), m.group(2)))
    if not rows:
        raise ValueError(f"no door rows parsed from {path}")
    for expected, (door_id, _) in enumerate(rows):
        if door_id != expected:
            raise ValueError(f"door_registry id {door_id} is not append-only at {expected}")
    return [name for _, name in rows]


def parse_door_table(path, registry_names):
    text = path.read_text(encoding="utf-8")
    start = text.find("const DoorTblDoor kDoorTblDoors")
    if start < 0:
        raise ValueError(f"missing kDoorTblDoors in {path}")
    open_brace = text.find("{", start)
    end = text.find("};", open_brace)
    if open_brace < 0 or end < 0:
        raise ValueError(f"malformed kDoorTblDoors section in {path}")

    rows = []
    for line in text[open_brace + 1:end].splitlines():
        m = re.search(r"\{([^}]*)\}, // (.*)", line)
        if not m:
            continue
        vals = [int(x, 0) for x in m.group(1).split(",")]
        if len(vals) != 17:
            raise ValueError(f"unexpected DoorTblDoor row width at id {len(rows)}")
        rows.append(DoorRow(
            door_id=len(rows),
            name=m.group(2),
            region=vals[1],
            partner=vals[2],
            flags=vals[3],
            room=vals[4],
            door_type=vals[5],
            direction=vals[6],
            door_index=vals[7],
            rando_dungeon=vals[16],
        ))

    if len(rows) != len(registry_names):
        raise ValueError(f"door table rows {len(rows)} != registry rows {len(registry_names)}")
    for row, name in zip(rows, registry_names):
        if row.name != name:
            raise ValueError(f"door id {row.door_id} name drift: table={row.name!r} registry={name!r}")
    return rows


def source_rooms_by_region(rows):
    out = {}
    for row in rows:
        if row.room == 0xFF:
            continue
        out.setdefault(row.region, set()).add(row.room)
    return out


def source_room(row, rooms_by_region):
    if row.room != 0xFF:
        return row.room
    rooms = rooms_by_region.get(row.region, set())
    if len(rooms) == 1:
        return next(iter(rooms))
    return None


def transition_dest(row, rows, source, header_destinations):
    if row.door_type == DOOR_TYPE_NORMAL:
        if row.partner != 0xFFFF:
            if row.partner >= len(rows):
                raise ValueError(f"{row.name}: partner id {row.partner} out of range")
            partner_room = rows[row.partner].room
            if partner_room != 0xFF:
                return partner_room
        if row.direction not in DIR_DELTAS:
            return None
        return source + DIR_DELTAS[row.direction]
    if row.door_type in (DOOR_TYPE_SPIRAL_STAIRS, DOOR_TYPE_STRAIGHT_STAIRS):
        if not 0 <= row.door_index <= 3:
            return None
        return header_destinations[source][row.door_index + 1]
    if row.door_type == DOOR_TYPE_HOLE:
        return header_destinations[source][0]
    return None


def seam_kind(row):
    if row.door_type == DOOR_TYPE_NORMAL:
        return "Door"
    if row.door_type in (DOOR_TYPE_SPIRAL_STAIRS, DOOR_TYPE_STRAIGHT_STAIRS):
        return "Stair"
    if row.door_type == DOOR_TYPE_HOLE:
        return "Hole"
    raise ValueError(f"{row.name}: unsupported seam type {row.door_type}")


def seam_slot(row):
    if row.door_type == DOOR_TYPE_HOLE:
        return 0
    if row.door_type in (DOOR_TYPE_SPIRAL_STAIRS, DOOR_TYPE_STRAIGHT_STAIRS):
        return row.door_index + 1
    return row.door_index


def collect_seams(rows, header_destinations):
    rooms_by_region = source_rooms_by_region(rows)
    inbound = []
    outbound = []
    relevant_types = {
        DOOR_TYPE_NORMAL,
        DOOR_TYPE_SPIRAL_STAIRS,
        DOOR_TYPE_STRAIGHT_STAIRS,
        DOOR_TYPE_HOLE,
    }
    for row in rows:
        if row.door_type not in relevant_types:
            continue
        source = source_room(row, rooms_by_region)
        if source is None:
            continue
        dest = transition_dest(row, rows, source, header_destinations)
        if dest is None:
            continue
        source_dungeon = POOL_BY_BOSS_ROOM.get(source)
        dest_dungeon = POOL_BY_BOSS_ROOM.get(dest)

        if source_dungeon is None and dest_dungeon is not None:
            if row.rando_dungeon == dest_dungeon.rando_id and not (row.flags & DOOR_FLAG_PORTAL):
                inbound.append(SeamRow(seam_kind(row), dest_dungeon, source, dest,
                                       row.door_id, row.direction, seam_slot(row), row.name))

        if source_dungeon is not None:
            outbound.append(SeamRow(seam_kind(row), source_dungeon, source, dest,
                                    row.door_id, row.direction, seam_slot(row), row.name))

    inbound.sort(key=lambda r: r.dungeon.rando_id)
    outbound.sort(key=lambda r: (r.dungeon.rando_id, r.kind, r.door_id))
    return inbound, outbound


def validate(inbound, outbound, entrance_rooms, synthetic_entrance_range):
    got = [row.dungeon.rando_id for row in inbound]
    want = [d.rando_id for d in POOL]
    if got != want:
        raise ValueError(f"inbound boss seams by dungeon {got} != expected {want}")
    if not any(row.dungeon.short_name == "IP" and row.kind == "Hole" for row in inbound):
        raise ValueError("Ice Palace boss seam was not resolved as a hole")
    vanilla_entrance_rooms = entrance_rooms[:synthetic_entrance_range.start]
    for boss_room in POOL_BY_BOSS_ROOM:
        if boss_room in vanilla_entrance_rooms:
            raise ValueError(f"boss room {boss_room:#x} already has a vanilla entrance record")
    if len(entrance_rooms) >= synthetic_entrance_range.stop:
        expected_synthetic_rooms = set(POOL_BY_BOSS_ROOM)
        synthetic_rooms = set(entrance_rooms[synthetic_entrance_range.start:
                                            synthetic_entrance_range.stop])
        if synthetic_rooms != expected_synthetic_rooms:
            raise ValueError("synthetic boss entrance rooms drifted from boss pool")
    outbound_dungeons = {row.dungeon.rando_id for row in outbound}
    missing = [d.enum_name for d in POOL if d.rando_id not in outbound_dungeons and d.short_name != "IP"]
    if missing:
        raise ValueError(f"non-IP boss room(s) have no outbound seams: {', '.join(missing)}")


def direction_expr(direction):
    return DIR_NAMES.get(direction, "kChainSeamDir_None")


def emit_header(inbound, outbound):
    return "".join([
        "// chain_seams.gen.h - GENERATED by assets/scripts/gen_chain_seam_tables.py. Do not edit.\n",
        "#pragma once\n\n",
        '#include "door_tables.gen.h"\n',
        '#include "dungeon_ids.h"\n\n',
        "enum {\n",
        "  kChainSeamKind_Door,\n",
        "  kChainSeamKind_Stair,\n",
        "  kChainSeamKind_Hole,\n",
        "};\n\n",
        "enum { kChainSeamDir_None = 0xFF };\n\n",
        "typedef struct ChainSeamRow {\n",
        "  uint8 kind;\n",
        "  uint8 rando_dungeon;\n",
        "  uint16 source_room;\n",
        "  uint16 dest_room;\n",
        "  uint16 door_id;\n",
        "  uint8 direction;\n",
        "  uint8 slot;\n",
        "} ChainSeamRow;\n\n",
        f"#define kChainBossSeamCount {len(inbound)}\n",
        f"#define kChainBossOutboundSeamCount {len(outbound)}\n\n",
        "extern const ChainSeamRow kChainBossSeams[kChainBossSeamCount];\n",
        "extern const ChainSeamRow kChainBossOutboundSeams[kChainBossOutboundSeamCount];\n",
    ])


def row_expr(row):
    return ("  {kChainSeamKind_%s, %s, 0x%03X, 0x%03X, %d, %s, %d}, // %s -> %s" %
            (row.kind, row.dungeon.enum_name, row.source_room, row.dest_room,
             row.door_id, direction_expr(row.direction), row.slot, row.name,
             row.dungeon.name if row.dest_room == row.dungeon.boss_room else f"room 0x{row.dest_room:03X}"))


def emit_c(inbound, outbound):
    rows_in = "\n".join(row_expr(row) for row in inbound)
    rows_out = "\n".join(row_expr(row) for row in outbound)
    return "".join([
        "// chain_seams.gen.c - GENERATED by assets/scripts/gen_chain_seam_tables.py. Do not edit.\n",
        "#include \"chain_seams.gen.h\"\n\n",
        "const ChainSeamRow kChainBossSeams[kChainBossSeamCount] = {\n",
        rows_in,
        "\n};\n\n",
        "const ChainSeamRow kChainBossOutboundSeams[kChainBossOutboundSeamCount] = {\n",
        rows_out,
        "\n};\n",
    ])


def write_or_check(path, content, check):
    if check:
        if not path.exists():
            raise ValueError(f"{path} is missing")
        current = path.read_text(encoding="utf-8")
        if current != content:
            raise ValueError(f"{path} is stale; rerun gen_chain_seam_tables.py")
    else:
        with path.open("w", encoding="utf-8", newline="\n") as f:
            f.write(content)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--asset-file", default=str(REPO / "zelda3_assets.dat"))
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()

    asset_path = Path(args.asset_file)
    registry_path = REPO / "assets" / "rando" / "door_registry.yaml"
    chain_boss_entrances_path = REPO / "assets" / "rando" / "chain_boss_entrances.gen.yaml"
    door_table_path = REPO / "src" / "rando" / "door_tables.gen.c"
    out_h = REPO / "src" / "rando" / "chain_seams.gen.h"
    out_c = REPO / "src" / "rando" / "chain_seams.gen.c"

    assets = read_assets(asset_path)
    header_destinations = load_header_destinations(assets)
    entrance_rooms = load_entrance_rooms(assets)
    synthetic_entrance_range = load_synthetic_entrance_range(chain_boss_entrances_path)
    registry_names = parse_door_registry(registry_path)
    door_rows = parse_door_table(door_table_path, registry_names)
    inbound, outbound = collect_seams(door_rows, header_destinations)
    validate(inbound, outbound, entrance_rooms, synthetic_entrance_range)

    write_or_check(out_h, emit_header(inbound, outbound), args.check)
    write_or_check(out_c, emit_c(inbound, outbound), args.check)
    action = "checked" if args.check else "wrote"
    print(f"gen_chain_seam_tables: {action} {len(inbound)} inbound and "
          f"{len(outbound)} outbound boss seams")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except ValueError as e:
        print(f"gen_chain_seam_tables: FAIL: {e}", file=sys.stderr)
        raise SystemExit(1)
