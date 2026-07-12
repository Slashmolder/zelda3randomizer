#!/usr/bin/env python3
"""Generate the local forced enemy-drop registry.

Reads the packed dungeon sprite assets from zelda3_assets.dat, finds forced-key
marker entries (type 0xe4, y=0xfe small key / y=0xfd big key), and writes
assets/rando/enemy_drops.gen.yaml for rando_logic_gen.py.
"""
from __future__ import annotations

import argparse
import difflib
import hashlib
import re
import struct
import sys
from pathlib import Path

import yaml

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))

from gen_soul_tables import enemy_soul_by_species  # noqa: E402

DEFAULT_ASSETS = REPO / "zelda3_assets.dat"
DEFAULT_OUT = REPO / "assets" / "rando" / "enemy_drops.gen.yaml"
DEFAULT_KEY_DEPTH = REPO / "key_depth.txt"
ENEMY_DROP_BASE_ID = 1300

SMALL_KEY_ITEMS = {
    0: "SmallKey_HyruleCastleEscape",
    1: "SmallKey_EasternPalace",
    2: "SmallKey_DesertPalace",
    3: "SmallKey_TowerOfHera",
    4: "SmallKey_HyruleCastleTower",
    5: "SmallKey_PalaceOfDarkness",
    6: "SmallKey_SwampPalace",
    7: "SmallKey_SkullWoods",
    8: "SmallKey_ThievesTown",
    9: "SmallKey_IcePalace",
    10: "SmallKey_MiseryMire",
    11: "SmallKey_TurtleRock",
    12: "SmallKey_GanonsTower",
}
ENEMY_DROP_ASSET_KEYS = ("kDungeonSprites", "kDungeonSpriteOffs")


SMALL_KEY_BINDINGS = {
    0x00E: {
        "drop_name": "Ice Palace - Jelly Key Drop",
        "door_dungeon": 9,
        "door_region": 345,
        "region": "IcePalace_Lobby",
        "vanilla_item": "SmallKey_IcePalace",
        "can_reach": "HAS_ITEM(Hammer) AND CanLiftRocks() AND (HAS_ITEM(Hookshot) OR HAS_ITEM(SmallKey_IcePalace)) AND CanKillMostThings(world, 5)",
    },
    0x013: {
        "drop_name": "Turtle Rock - Pokey 2 Key Drop",
        "door_dungeon": 11,
        "door_region": 466,
        "region": "TurtleRock_Lobby",
        "vanilla_item": "SmallKey_TurtleRock",
        "can_reach": "HAS_AMOUNT(SmallKey_TurtleRock, 2) AND CanKillMostThings(world, 5)",
    },
    0x021: {
        "drop_name": "Hyrule Castle - Key Rat Key Drop",
        "door_dungeon": 0,
        "door_region": 28,
        "region": "HyruleCastleEscape",
        "vanilla_item": "SmallKey_HyruleCastleEscape",
        "can_reach": "CanKillEscapeThings(world) AND HAS_ITEM(Lamp)",
    },
    0x039: {
        "drop_name": "Skull Woods - Spike Corner Key Drop",
        "door_dungeon": 7,
        "door_region": 273,
        "region": "SkullWoods",
        "vanilla_item": "SmallKey_SkullWoods",
        "can_reach": "HAS_ITEM(SmallKey_SkullWoods) AND CanKillMostThings(world, 5)",
    },
    0x03D: {
        "drop_name": "Ganons Tower - Mini Helmasaur Key Drop",
        "door_dungeon": 12,
        "door_region": 546,
        "region": "GanonsTower_Lobby",
        "vanilla_item": "SmallKey_GanonsTower",
        "can_reach": "CanShootArrowsL1() AND CanLightTorches() AND HAS_ITEM(BigKey_GanonsTower) AND HAS_AMOUNT(SmallKey_GanonsTower, 3) AND CanKillLanmolas(world)",
    },
    0x03E: {
        "drop_name": "Ice Palace - Conveyor Key Drop",
        "door_dungeon": 9,
        "door_region": 326,
        "region": "IcePalace_Lobby",
        "vanilla_item": "SmallKey_IcePalace",
        "can_reach": "CanKillMostThings(world, 5)",
    },
    0x071: {
        "drop_name": "Hyrule Castle - Boomerang Guard Key Drop",
        "door_dungeon": 0,
        "door_region": 11,
        "region": "HyruleCastleEscape",
        "vanilla_item": "SmallKey_HyruleCastleEscape",
        "can_reach": "CanKillEscapeThings(world)",
    },
    0x072: {
        "drop_name": "Hyrule Castle - Map Guard Key Drop",
        "door_dungeon": 0,
        "door_region": 17,
        "region": "HyruleCastleEscape",
        "vanilla_item": "SmallKey_HyruleCastleEscape",
        "can_reach": "CanKillEscapeThings(world)",
    },
    0x099: {
        "drop_name": "Eastern Palace - Dark Eyegore Key Drop",
        "door_dungeon": 1,
        "door_region": 47,
        "region": "EasternPalace_Lobby",
        "vanilla_item": "SmallKey_EasternPalace",
        # Room 0x099 is entered from room 0x0A9 through Eastern Courtyard N,
        # whose vanilla door kind is BigKey (0x220).  The key-depth dump opens
        # big-key doors by design and therefore cannot supply this term; keep
        # it explicit here so neither the forced key drop nor ordinary enemy
        # checks in this room can hold the Eastern big key behind itself.
        "can_reach": "(HAS_ITEM(Lamp) OR CanDarkRoomNav()) AND "
                     "HAS_ITEM(BigKey_EasternPalace) AND CanKillMostThings(world, 5)",
    },
    0x0B0: {
        "drop_name": "Castle Tower - Circle of Pots Key Drop",
        "door_dungeon": 4,
        "door_region": 130,
        "region": "HyruleCastleTower",
        "vanilla_item": "SmallKey_HyruleCastleTower",
        "can_reach": "(HAS_ITEM(Lamp) OR CanDarkRoomNav()) AND HAS_ITEM(SmallKey_HyruleCastleTower) AND CanKillMostThings(world, 8)",
    },
    0x0B6: {
        "drop_name": "Turtle Rock - Pokey 1 Key Drop",
        "door_dungeon": 11,
        "door_region": 465,
        "region": "TurtleRock_Lobby",
        "vanilla_item": "SmallKey_TurtleRock",
        "can_reach": "HAS_ITEM(SmallKey_TurtleRock) AND CanKillMostThings(world, 5)",
    },
    0x0C0: {
        "drop_name": "Castle Tower - Dark Archer Key Drop",
        "door_dungeon": 4,
        "door_region": 131,
        "region": "HyruleCastleTower",
        "vanilla_item": "SmallKey_HyruleCastleTower",
        "can_reach": "(HAS_ITEM(Lamp) OR CanDarkRoomNav()) AND HAS_ITEM(SmallKey_HyruleCastleTower) AND CanKillMostThings(world, 8)",
    },
    0x0C1: {
        "drop_name": "Misery Mire - Conveyor Crystal Key Drop",
        "door_dungeon": 10,
        "door_region": 377,
        "region": "MiseryMire_Lobby",
        "vanilla_item": "SmallKey_MiseryMire",
        "can_reach": "CanLightTorches() AND HAS_AMOUNT(SmallKey_MiseryMire, 3) AND CanKillMostThings(world, 5)",
    },
}

BIG_KEY_BINDINGS = {
    0x080: {
        "drop_name": "Hyrule Castle - Ball-n-chain Big Key Drop",
        "door_dungeon": 0,
        # The source enemy is in the cellblock room. gen_enemy_drop_tables.py
        # refreshes this from key_depth.txt so a door-table rebind cannot drift.
        "door_region": 15,
        "region": "HyruleCastleEscape",
        # Pure one-shot model: the enemy is a check, while the vanilla castle
        # big key remains a free runtime grant because no modeled HCE/HCT big-key
        # item exists in item_registry.yaml.
        "vanilla_item": "Nothing",
        "can_reach": "CanKillEscapeThings(world)",
        "big_key_policy": "one_shot_preserve_vanilla_big_key",
    },
}


def die(msg: str) -> None:
    print(f"gen_enemy_drop_tables: ERROR: {msg}", file=sys.stderr)
    raise SystemExit(2)


def read_assets(path: Path) -> dict[str, bytes]:
    data = path.read_bytes()
    if len(data) < 88:
        die(f"{path} is too small to be zelda3_assets.dat")
    if not (data.startswith(b"Zelda3_v0") or data.startswith(b"Zelda3_v1")):
        die(f"{path} does not start with a known Zelda3 asset signature")
    count, key_len = struct.unpack_from("<II", data, 80)
    sizes_off = 88
    keys_off = sizes_off + 4 * count
    keys_end = keys_off + key_len
    if keys_end > len(data):
        die(f"{path} has a truncated asset key table")
    sizes = list(struct.unpack_from("<" + "I" * count, data, sizes_off))
    keys = [k.decode("utf-8") for k in data[keys_off:keys_end].split(b"\0") if k]
    if len(keys) != count:
        die(f"{path} key count mismatch ({len(keys)} names for {count} assets)")
    off = keys_end
    out: dict[str, bytes] = {}
    for name, size in zip(keys, sizes):
        off = (off + 3) & ~3
        end = off + size
        if end > len(data):
            die(f"{path} asset {name} is truncated")
        out[name] = data[off:end]
        off = end
    return out


def asset_payload_sha256(assets: dict[str, bytes], names=None) -> str:
    """Hash selected parsed asset records, independent of container version."""
    h = hashlib.sha256()
    for name in sorted(assets if names is None else names):
        key = name.encode("utf-8")
        raw = assets[name]
        h.update(len(key).to_bytes(4, "little"))
        h.update(key)
        h.update(len(raw).to_bytes(8, "little"))
        h.update(raw)
    return h.hexdigest()


def parse_u16le_array(raw: bytes) -> list[int]:
    if len(raw) % 2:
        die("kDungeonSpriteOffs has odd byte length")
    return list(struct.unpack_from("<" + "H" * (len(raw) // 2), raw, 0))


def sprite_entries(sprites: bytes, offsets: list[int], room: int):
    off = offsets[room]
    if off >= len(sprites):
        die(f"room 0x{room:03x} sprite offset 0x{off:x} outside kDungeonSprites")
    pos = off + 1  # sort byte
    list_index = 0
    while pos < len(sprites):
        y = sprites[pos]
        if y == 0xFF:
            return
        if pos + 2 >= len(sprites):
            die(f"room 0x{room:03x} sprite list is truncated")
        x = sprites[pos + 1]
        typ = sprites[pos + 2]
        yield list_index, y, x, typ
        list_index += 1
        pos += 3
    die(f"room 0x{room:03x} sprite list missing 0xff terminator")


def parse_key_depth(path: Path) -> dict[str, dict]:
    if not path.exists():
        die(f"missing {path}; run `zelda3 --dump-key-depth {path}` first")
    drops: dict[tuple[int, int], dict] = {}
    locations: dict[str, dict] = {}
    rooms: dict[tuple[int, int], list[dict]] = {}
    dungeons: dict[int, dict] = {}
    for ln in path.read_text(encoding="utf-8").splitlines():
        m = re.match(r'DUNGEON (\d+) chest=(\d+) drop=(\d+) .*name="([^"]*)"', ln)
        if m:
            dungeons[int(m[1])] = {
                "chest": int(m[2]),
                "drop": int(m[3]),
                "name": m[4],
            }
            continue
        m = re.match(
            r'LOC (\d+) loc=(\d+) region=(\d+) depth=(-?\d+) mindepth=(-?\d+) name="([^"]*)"',
            ln,
        )
        if m:
            dungeon, loc_id, region = int(m[1]), int(m[2]), int(m[3])
            depth, mindepth = int(m[4]), int(m[5])
            locations[m[6]] = {
                "dungeon": dungeon,
                "loc_id": loc_id,
                "region": region,
                "key_depth": depth,
                "key_mindepth": mindepth,
            }
            continue
        m = re.match(
            r'DROP (\d+) index=(\d+) region=(\d+) depth=(-?\d+) mindepth=(-?\d+) name="([^"]*)"',
            ln,
        )
        if not m:
            m = re.match(
                r'ROOM room=0x([0-9a-fA-F]+) region=(\d+) depth=(-?\d+) mindepth=(-?\d+) dungeon=(\d+)',
                ln,
            )
            if m:
                room, region = int(m[1], 16), int(m[2])
                depth, mindepth, dungeon = int(m[3]), int(m[4]), int(m[5])
                rooms.setdefault((dungeon, room), []).append({
                    "region": region,
                    "key_depth": depth,
                    "key_mindepth": mindepth,
                })
            continue
        dungeon, index, region = int(m[1]), int(m[2]), int(m[3])
        depth, mindepth = int(m[4]), int(m[5])
        if depth < 0 or mindepth < 0:
            die(f"{path}: DROP dungeon={dungeon} region={region} has negative depth")
        key = (dungeon, region)
        if key in drops:
            die(f"{path}: duplicate DROP dungeon={dungeon} region={region}")
        drops[key] = {
            "drop_index": index,
            "key_depth": depth,
            "key_mindepth": mindepth,
            "door_drop_name": m[6],
        }
    if not drops:
        die(f"{path}: no DROP rows found")
    if not locations:
        die(f"{path}: no LOC rows found")
    if not rooms:
        die(f"{path}: no ROOM rows found")
    if not dungeons:
        die(f"{path}: no DUNGEON rows found")
    return {
        "drops": drops,
        "locations": locations,
        "rooms": rooms,
        "dungeons": dungeons,
    }


def collect_forced_drops(assets: dict[str, bytes]) -> tuple[list[dict], list[dict]]:
    try:
        sprites = assets["kDungeonSprites"]
        offsets = parse_u16le_array(assets["kDungeonSpriteOffs"])
    except KeyError as e:
        die(f"missing asset {e.args[0]} in zelda3_assets.dat")

    small_rows: list[dict] = []
    big_rows: list[dict] = []
    seen = set()
    for room in range(len(offsets)):
        runtime_slot = 0
        prev_real: dict | None = None
        last_was_marker = False
        for list_index, y, x, typ in sprite_entries(sprites, offsets, room):
            if typ == 0xE4 and y in (0xFE, 0xFD):
                if prev_real is None:
                    die(f"room 0x{room:03x} forced-key marker appears before any real sprite")
                if last_was_marker:
                    die(f"room 0x{room:03x} forced-key marker follows another marker")
                source_slot = int(prev_real["source_slot"])
                key = (room, source_slot, y)
                if key in seen:
                    die(f"duplicate forced-key marker for room 0x{room:03x} slot {source_slot}")
                seen.add(key)
                base = {
                    "room": room,
                    "source_slot": source_slot,
                    "source_type": int(prev_real["type"]),
                    "source_y": int(prev_real["y"]),
                    "source_x": int(prev_real["x"]),
                    "marker_list_index": list_index,
                }
                if y == 0xFE:
                    if room == 0x087:
                        die("room 0x087 is the ToH basement cage standing key, not an enemy drop")
                    if room not in SMALL_KEY_BINDINGS:
                        die(f"small-key marker room 0x{room:03x} lacks reviewed binding")
                    binding = SMALL_KEY_BINDINGS[room]
                    base.update({
                        "drop": "small_key",
                        "drop_name": binding["drop_name"],
                        "door_dungeon": binding["door_dungeon"],
                        "door_region": binding["door_region"],
                        "region": binding["region"],
                        "vanilla_item": binding["vanilla_item"],
                        "can_reach": binding["can_reach"],
                    })
                    small_rows.append(base)
                else:
                    if room not in BIG_KEY_BINDINGS:
                        die(f"big-key marker room 0x{room:03x} lacks reviewed binding")
                    binding = BIG_KEY_BINDINGS[room]
                    base.update({
                        "drop": "big_key",
                        "drop_name": binding["drop_name"],
                        "door_dungeon": binding["door_dungeon"],
                        "door_region": binding["door_region"],
                        "region": binding["region"],
                        "vanilla_item": binding["vanilla_item"],
                        "can_reach": binding["can_reach"],
                        "big_key_policy": binding["big_key_policy"],
                    })
                    big_rows.append(base)
                last_was_marker = True
                continue
            if x >= 0xE0:
                last_was_marker = False
                continue
            if runtime_slot >= 16:
                die(f"room 0x{room:03x} has more than 16 real sprite slots before marker scan finished")
            prev_real = {
                "source_slot": runtime_slot,
                "type": typ,
                "y": y,
                "x": x,
                "list_index": list_index,
            }
            runtime_slot += 1
            last_was_marker = False

    return small_rows, big_rows


def annotate_key_depth(small_rows: list[dict], key_depth: dict[tuple[int, int], dict]) -> None:
    seen_drop_indices = set()
    for row in small_rows:
        key = (int(row["door_dungeon"]), int(row["door_region"]))
        kd = key_depth.get(key)
        if kd is None:
            die(f"{row['drop_name']}: no key-depth DROP row for dungeon={key[0]} region={key[1]}")
        drop_index = int(kd["drop_index"])
        if drop_index in seen_drop_indices:
            die(f"duplicate key-depth DROP index {drop_index} across active enemy drops")
        seen_drop_indices.add(drop_index)
        row.update(kd)


def annotate_big_key_depth(big_rows: list[dict], key_depth: dict[str, dict]) -> None:
    rooms = key_depth["rooms"]
    for row in big_rows:
        key = (int(row["door_dungeon"]), int(row["room"]))
        hits = [r for r in rooms.get(key, []) if int(r["key_depth"]) >= 0]
        if not hits:
            die(f"{row['drop_name']}: no key-depth ROOM row for dungeon={key[0]} room=0x{key[1]:03x}")
        # A room can contain multiple door regions. Use the deepest one for this
        # one-shot check; HCE room 0x080 currently resolves to the cellblock.
        best = max(hits, key=lambda r: (int(r["key_depth"]), int(r["key_mindepth"])))
        row["door_region"] = int(best["region"])
        row["key_depth"] = int(best["key_depth"])
        row["key_mindepth"] = int(best["key_mindepth"])
        row["small_key_item"] = SMALL_KEY_ITEMS[int(row["door_dungeon"])]


def make_key_depth_locations(small_rows: list[dict], key_depth: dict[str, dict]) -> list[dict]:
    active_dungeons = {int(r["door_dungeon"]) for r in small_rows}
    rows = []
    for name, kd in sorted(key_depth["locations"].items()):
        d = int(kd["dungeon"])
        if d not in active_dungeons:
            continue
        depth = int(kd["key_depth"])
        mindepth = int(kd["key_mindepth"])
        if depth <= 0 and mindepth <= 0:
            continue
        rows.append({
            "name": name,
            "item": SMALL_KEY_ITEMS[d],
            "door_dungeon": d,
            "door_region": int(kd["region"]),
            "key_depth": depth,
            "key_mindepth": mindepth,
        })

    # The Zelda rescue event is not a door-table LOC, but once enemy drops are
    # itemized the escape completion path must account for the three HCE keys:
    # Map Guard -> Armory, Boomerang Guard -> Cellblock, Key Rat -> Sanctuary.
    if 0 in active_dungeons:
        rows.append({
            "name": "Zelda",
            "item": SMALL_KEY_ITEMS[0],
            "door_dungeon": 0,
            "door_region": 23,
            "key_depth": 4,
            "key_mindepth": 3,
            "notes": "HCE escort completion: Map Guard + Boomerang Guard + Key Rat chain",
        })
    return rows


def make_small_key_source_counts(small_rows: list[dict], key_depth: dict[str, dict]) -> list[dict]:
    enemy_by_dungeon: dict[int, int] = {}
    for row in small_rows:
        d = int(row["door_dungeon"])
        enemy_by_dungeon[d] = enemy_by_dungeon.get(d, 0) + 1
    rows = []
    for d in sorted(enemy_by_dungeon):
        rows.append({
            "door_dungeon": d,
            "item": SMALL_KEY_ITEMS[d],
            "chest_count": int(key_depth["dungeons"].get(d, {}).get("chest", 0)),
            "drop_count": int(key_depth["dungeons"].get(d, {}).get("drop", 0)),
            "enemy_drop_count": enemy_by_dungeon[d],
        })
    return rows


def make_doc(assets: dict[str, bytes], assets_path: Path,
             small_rows: list[dict], big_rows: list[dict],
             key_depth: dict[str, dict]) -> dict:
    small_rows = sorted(small_rows, key=lambda r: (r["room"], r["source_slot"]))
    big_rows = sorted(big_rows, key=lambda r: (r["room"], r["source_slot"]))
    out_rows = []
    seen_sources = set()
    seen_rooms = set()
    soul_by_species = enemy_soul_by_species()
    for row in small_rows + big_rows:
        room = int(row["room"])
        slot = int(row["source_slot"])
        source_key = (room, slot)
        if source_key in seen_sources:
            die(f"duplicate active enemy-drop source room 0x{room:03x} slot {slot}")
        seen_sources.add(source_key)
        if room in seen_rooms:
            die(f"multiple active enemy key drops in room 0x{room:03x}; room-level key bits would collide")
        seen_rooms.add(room)
        # add-enemy-souls (task 1.6): killing the holder requires the holder's
        # soul at souls_shuffle=all — the term is inert below that tier and the
        # holder's species is enemy-shuffle-pinned (kRandoSoulPinRooms), so the
        # vanilla source_type stays authoritative. Species with no soul family
        # (e.g. the TR Pokeys) always spawn and carry no term.
        soul = soul_by_species.get(int(row["source_type"]))
        row = dict(row)
        row["source_soul"] = soul
        if soul is not None:
            row["can_reach"] = f"({row['can_reach']}) AND NeedsEnemySoul(Soul_{soul})"
        loc_id = ENEMY_DROP_BASE_ID + len(out_rows)
        out = {
            "id": loc_id,
            "name": row["drop_name"],
            "domain": "dungeon",
            "type": "EnemyDrop",
            "drop": row["drop"],
            "door_dungeon": int(row["door_dungeon"]),
            "door_region": int(row["door_region"]),
            "region": row["region"],
            "vanilla_item": row["vanilla_item"],
            "can_reach": row["can_reach"],
            "room": room,
            "source_slot": slot,
            "source_type": int(row["source_type"]),
            "source_soul": row["source_soul"],
            "source_y": int(row["source_y"]),
            "source_x": int(row["source_x"]),
            "marker_list_index": int(row["marker_list_index"]),
        }
        if row["drop"] == "small_key":
            out["door_drop_index"] = int(row["drop_index"])
            out["key_depth"] = int(row["key_depth"])
            out["key_mindepth"] = int(row["key_mindepth"])
            out["door_drop_name"] = row["door_drop_name"]
        elif row["drop"] == "big_key":
            out["key_depth"] = int(row["key_depth"])
            out["key_mindepth"] = int(row["key_mindepth"])
            out["small_key_item"] = row["small_key_item"]
            out["big_key_policy"] = row["big_key_policy"]
        out_rows.append(out)
    return {
        "format_version": 3,
        "_generated_by": "assets/scripts/gen_enemy_drop_tables.py (do not hand-edit)",
        "source": {
            "assets": str(assets_path.name),
            "sha256": asset_payload_sha256(assets, ENEMY_DROP_ASSET_KEYS),
            "sprite_tables": list(ENEMY_DROP_ASSET_KEYS),
        },
        "enemy_drops": out_rows,
        "small_key_source_counts": make_small_key_source_counts(small_rows, key_depth),
        "key_depth_locations": make_key_depth_locations(small_rows, key_depth),
        "hce_big_key_locations": [
            "Hyrule Castle - Zelda's Cell",
            "Zelda",
        ],
        "big_key_candidates": [
            {
                **row,
                "active_location_id": ENEMY_DROP_BASE_ID + len(small_rows) + i,
            }
            for i, row in enumerate(big_rows)
        ],
    }


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
        print(f"gen_enemy_drop_tables: OK {path} is fresh", file=sys.stderr)
        return 0
    print(f"gen_enemy_drop_tables: ERROR: {path} is stale; run "
          f"`python assets/scripts/gen_enemy_drop_tables.py` to refresh it.",
          file=sys.stderr)
    if have.replace(b"\r\n", b"\n") == want:
        print("gen_enemy_drop_tables: drift is line-ending-only (expected LF).",
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


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--assets", type=Path, default=DEFAULT_ASSETS,
                    help="Path to zelda3_assets.dat")
    ap.add_argument("--out", type=Path, default=DEFAULT_OUT,
                    help="Output YAML path")
    ap.add_argument("--key-depth", type=Path, default=DEFAULT_KEY_DEPTH,
                    help="key_depth.txt from `zelda3 --dump-key-depth`")
    ap.add_argument("--check", action="store_true",
                    help="Fail if the output is stale instead of writing it")
    args = ap.parse_args(argv)

    assets_path = args.assets if args.assets.is_absolute() else (REPO / args.assets)
    out_path = args.out if args.out.is_absolute() else (REPO / args.out)
    key_depth_path = args.key_depth if args.key_depth.is_absolute() else (REPO / args.key_depth)
    if not assets_path.exists():
        die(f"missing {assets_path}; build/extract assets first")

    assets = read_assets(assets_path)
    small_rows, big_rows = collect_forced_drops(assets)
    if len(small_rows) != len(SMALL_KEY_BINDINGS):
        die(f"expected {len(SMALL_KEY_BINDINGS)} small-key drops, found {len(small_rows)}")
    if len(big_rows) != 1:
        die(f"expected exactly one one-shot big-key row, found {len(big_rows)}")
    key_depth = parse_key_depth(key_depth_path)
    annotate_key_depth(small_rows, key_depth["drops"])
    annotate_big_key_depth(big_rows, key_depth)
    doc = make_doc(assets, assets_path, small_rows, big_rows, key_depth)
    text = render_yaml(doc)

    if args.check:
        return check_fresh(out_path, text)
    write_lf(out_path, text)
    print(f"gen_enemy_drop_tables: wrote {len(small_rows)} small-key rows and "
          f"{len(big_rows)} one-shot big-key row to {out_path}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
