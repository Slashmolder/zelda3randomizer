#!/usr/bin/env python3
"""gen_soul_room_tables.py — kill-gated-room soul requirements (add-enemy-souls
task 1.4). Emits assets/rando/soul_rooms.gen.yaml for rando_logic_gen.py.

Under souls_shuffle=all (bosses+enemies), a kill-gated room whose resident's
soul is un-owned holds its doors/chest shut (the runtime kill-gate hold in
Sprite_CheckIfScreenIsClear / RoomIsClear counts suppressed spawns as
not-killed). Placement logic must therefore require the residents' souls for
everything a kill room gates. This generator derives that mapping from the
game data, no memory:

  1. Room-header tag bytes (kDungeonRoomHeaders[5]/[6]) classify each room
     against the kDungTagroutines dispatch (src/dungeon.c): tags 0x01..0x0A
     open the room's trap/shutter doors on kill-clear; tags 0x29..0x32 reveal
     the room's chest(s) on kill-clear. Boss-class tags (prize/heart/Agahnim/
     GanonDoor/RekillableBoss) are handled by the boss-soul terms already in
     the CanKill<Boss> macros and are excluded here (asserted below).
  2. Room sprite lists (kDungeonSprites) give each room's residents; the soul
     family map (gen_soul_tables.py) gives the soul set. A room with no
     enemy-soul residents can never be held (soul-less species always spawn,
     and enemy shuffle only substitutes ESF_RANDOMIZABLE species, all of which
     have souls), so it is waived.
  3. For kill->doors rooms, the committed door graph (src/rando/
     door_tables.gen.c, harvested from ALttPDoorRandomizer's vanilla world)
     yields the gated set: regions reachable from the dungeon's portals only
     THROUGH the room's trap-kind doors. Fork locations homed in those regions
     (kDoorTblLocations) + the region ids themselves (for generated pot/enemy
     locations, matched by door_region) get the room's souls.
  4. For kill->chest rooms, the fork chest locations homed in the room's own
     regions get the souls.
  5. Cave rooms outside the door graph (Mimic Cave, Mini Moldorm Cave,
     Paradox Cave) carry reviewed bindings; their trap doors are verified
     against the room-data door lists (kDungeonRoomDoorOffs) so a binding
     can't silently bind a room with no shutter.

Output is LOCAL/gitignored (derives from zelda3_assets.dat). When absent,
rando_logic_gen.py emits kRandoSoulRoomsBaked=0 and souls_shuffle=all seeds
fail closed in BuildItemPool (same pattern as the pot/enemy-drop registries).

Usage:
  python assets/scripts/gen_soul_room_tables.py [--assets PATH] [--out PATH] [--check]
"""
from __future__ import annotations

import argparse
import difflib
import hashlib
import re
import struct
import sys
from collections import defaultdict
from pathlib import Path

import yaml

SCRIPT_DIR = Path(__file__).resolve().parent
REPO = SCRIPT_DIR.parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

from gen_enemy_drop_tables import (  # noqa: E402
    DEFAULT_ASSETS,
    parse_u16le_array,
    read_assets,
    sprite_entries,
)
from gen_soul_tables import BOSS_PARTS, BOSS_SOULS, ENEMY_FAMILIES  # noqa: E402

DEFAULT_OUT = REPO / "assets" / "rando" / "soul_rooms.gen.yaml"
DOOR_TABLES_C = REPO / "src" / "rando" / "door_tables.gen.c"

# Room-header tag classification. Values are indices into kDungTagroutines
# (src/dungeon.c) — verified against the dispatch table + handler bodies:
#   0x01..0x08  quadrant-gated RoomTag_QuadrantTrigger wrappers; tag < 0x0B
#               branch: Sprite_CheckIfScreenIsClear -> Dung_TagRoutine_TrapdoorsUp
#   0x09        RoomTag_QuadrantTrigger direct (same kill->doors branch)
#   0x0A        RoomTag_RoomTrigger: Sprite_CheckIfRoomIsClear -> TrapdoorsUp
#   0x29..0x31  quadrant wrappers, tag >= 0x29 branch: kill -> chest reveal
#   0x32        RoomTag_RoomTrigger: room-clear -> chest reveal
#   0x26        RoomTag_KillRoomBlock (SE-quadrant kill -> tag clear)
KILL_DOOR_TAGS = set(range(0x01, 0x0B))
KILL_CHEST_TAGS = set(range(0x29, 0x33))
KILLROOMBLOCK_TAG = 0x26
# Boss-class kill tags — covered by NeedsBossSoul terms inside the
# CanKill<Boss> macro bodies (boss/prize locations, GT refights, Aga/Ganon).
BOSS_TAGS = {0x15, 0x25, 0x38, 0x3D, 0x3F}

# RoomData DoorKind codes that are trap/shutter doors (open via TrapdoorsUp).
# Source: ALttPDoorRandomizer RoomData.py DoorKind — Trap=0x18 (both sides),
# TrapTriggerable=0x36, Trap2=0x38, TrapTriggerableLow=0x44, TrapLowE3=0x4A.
TRAP_DOOR_KINDS = {0x18, 0x36, 0x38, 0x44, 0x4A}

# ---------------------------------------------------------------------------
# Reviewed cave bindings — kill-gated rooms OUTSIDE the 13-dungeon door graph.
# fork location ids from assets/rando/location_registry.yaml; the trap-door
# presence for each room is asserted from the room-data door list, so a stale
# binding (or a missed new one) fails generation instead of shipping silently.
CAVE_BINDINGS = {
    0x0EF: {
        "name": "Paradox Cave (lower)",
        # Trap2 shutter (pos 0x80) held by the Mini Moldorms. Conservatively
        # gates all five lower chests (over-strict if the shutter only gates a
        # sub-section; never unsound). Upper chests (203/204) enter from the
        # top entrance and are not bound here.
        "loc_ids": [198, 199, 200, 201, 202],
        "loc_names": [
            "Paradox Cave Lower - Far Left", "Paradox Cave Lower - Left",
            "Paradox Cave Lower - Right", "Paradox Cave Lower - Far Right",
            "Paradox Cave Lower - Middle",
        ],
    },
    0x10C: {
        "name": "Mimic Cave",
        "loc_ids": [197],
        "loc_names": ["Mimic Cave"],
    },
    0x123: {
        "name": "Mini Moldorm Cave",
        "loc_ids": [176, 177, 178, 179, 185],
        "loc_names": [
            "Mini Moldorm Cave - Far Left", "Mini Moldorm Cave - Left",
            "Mini Moldorm Cave - Right", "Mini Moldorm Cave - Far Right",
            "Mini Moldorm Cave - NPC",
        ],
    },
}

# Curated event sites: logic locations that live in a door-graph region but are
# NOT in kDoorTblLocations (events, not chests). Region name -> logic location
# names to wrap when that region is gated. Souls are computed from the flood,
# never hand-listed.
EVENT_SITE_REGIONS = {
    "Tower Agahnim 1": ["Agahnim"],  # the Aga1 prize event (12_hyrule_castle_tower.yaml)
}


def die(msg: str) -> None:
    print(f"gen_soul_room_tables: ERROR: {msg}", file=sys.stderr)
    raise SystemExit(2)


# ---------------------------------------------------------------------------
# Soul family map (enemy families only — boss souls are macro-handled).
def build_soul_maps():
    enemy_soul_for = {}
    for tok, ids in ENEMY_FAMILIES:
        for sid in ids:
            enemy_soul_for[sid] = tok
    boss_soul_species = set()
    for _tok, ids in BOSS_SOULS:
        boss_soul_species.update(ids)
    boss_soul_species.update(BOSS_PARTS.keys())
    return enemy_soul_for, boss_soul_species


# ---------------------------------------------------------------------------
# door_tables.gen.c parsing (committed artifact; format is the emit of
# gen_door_tables.py — initializer rows with a trailing `// Name` comment).
def parse_door_table(text: str, name: str):
    m = re.search(rf"const \w+ {re.escape(name)}\[[^\]]*\] = \{{(.*?)\n\}};", text, re.S)
    if not m:
        die(f"table {name} not found in {DOOR_TABLES_C}")
    rows = []
    for line in m.group(1).splitlines():
        mm = re.match(r"\s*\{([^}]*)\},?(?:\s*//\s*(.*))?$", line)
        if not mm:
            continue
        vals = [int(v.strip(), 0) for v in mm.group(1).split(",") if v.strip()]
        rows.append((vals, (mm.group(2) or "").strip()))
    if not rows:
        die(f"table {name} parsed empty (format drift in door_tables.gen.c?)")
    return rows


class DoorGraph:
    # kDoorTblDoors field order (door_tables.gen.h DoorTblDoor)
    D_REGION, D_ROOM, D_KIND, D_DUNGEON = 1, 4, 11, 16

    def __init__(self, path: Path):
        text = path.read_text(encoding="utf-8")
        self.doors = parse_door_table(text, "kDoorTblDoors")
        self.regions = parse_door_table(text, "kDoorTblRegions")
        self.edges = parse_door_table(text, "kDoorTblEdges")
        self.locations = parse_door_table(text, "kDoorTblLocations")
        self.portals = parse_door_table(text, "kDoorTblPortals")
        self.dungeons = parse_door_table(text, "kDoorTblDungeons")

        self.region_name = {i: c for i, (v, c) in enumerate(self.regions)}
        self.region_by_name = {c: i for i, (v, c) in enumerate(self.regions)}
        self.dungeon_names = [c for _v, c in self.dungeons]
        self.adj = defaultdict(list)  # from_region -> [(to_region, edge_door_id)]
        for v, _c in self.edges:
            self.adj[v[0]].append((v[1], v[3]))
        self.portal_regions = defaultdict(set)
        for v, _c in self.portals:
            self.portal_regions[v[0]].add(v[2])
        # room -> door ids; door id -> row
        self.doors_in_room = defaultdict(list)
        for di, (v, _c) in enumerate(self.doors):
            self.doors_in_room[v[self.D_ROOM]].append(di)
        # region -> [(fork_loc_id, name)]
        self.locs_in_region = defaultdict(list)
        for v, c in self.locations:
            self.locs_in_region[v[1]].append((v[0], c))

    def rooms_in_graph(self):
        return set(self.doors_in_room.keys())

    def trap_doors_of_room(self, room: int):
        out = set()
        for di in self.doors_in_room.get(room, []):
            if self.doors[di][0][self.D_KIND] in TRAP_DOOR_KINDS:
                out.add(di)
        return out

    def regions_of_room(self, room: int):
        return {self.doors[di][0][self.D_REGION] for di in self.doors_in_room.get(room, [])}

    def dungeon_of_room(self, room: int):
        ds = {self.doors[di][0][self.D_DUNGEON] for di in self.doors_in_room.get(room, [])}
        if len(ds) != 1:
            die(f"room 0x{room:03X} spans door-table dungeons {sorted(ds)}")
        return next(iter(ds))

    def flood(self, dungeon: int, banned_doors: set):
        seen = set()
        stack = list(self.portal_regions[dungeon])
        while stack:
            r = stack.pop()
            if r in seen:
                continue
            seen.add(r)
            for to, door in self.adj[r]:
                if door != 0xFFFF and door in banned_doors:
                    continue
                if to not in seen:
                    stack.append(to)
        return seen

    def behind_regions(self, room: int):
        """Regions reachable only through `room`'s trap doors (vanilla graph)."""
        trap = self.trap_doors_of_room(room)
        if not trap:
            return set(), trap
        d = self.dungeon_of_room(room)
        return self.flood(d, set()) - self.flood(d, trap), trap


# ---------------------------------------------------------------------------
def room_data_trap_doors(assets, room: int):
    """Trap-kind doors from the room-data door list (kDungeonRoomDoorOffs)."""
    room_data = assets["kDungeonRoom"]
    door_offs = parse_u16le_array(assets["kDungeonRoomDoorOffs"])
    if room >= len(door_offs):
        return []
    off = door_offs[room]
    out = []
    for _ in range(32):  # defensive cap; vanilla rooms have < 16 doors
        if off + 2 > len(room_data):
            break
        v = struct.unpack_from("<H", room_data, off)[0]
        if v == 0xFFFF:
            break
        if (v >> 8) in TRAP_DOOR_KINDS:
            out.append(v)
        off += 2
    return out


def scan_rooms(assets, enemy_soul_for, boss_soul_species):
    """Classify every room with a kill tag; return (rows, waived)."""
    hdr = assets["kDungeonRoomHeaders"]
    hdr_offs = parse_u16le_array(assets["kDungeonRoomHeadersOffs"])
    sprites = assets["kDungeonSprites"]
    spr_offs = parse_u16le_array(assets["kDungeonSpriteOffs"])

    rows, waived = [], []
    n_rooms = min(len(hdr_offs), len(spr_offs))
    for room in range(n_rooms):
        off = hdr_offs[room]
        if off + 14 > len(hdr):
            continue
        tags = (hdr[off + 5], hdr[off + 6])
        kinds = set()
        for t in tags:
            if t in KILL_DOOR_TAGS:
                kinds.add("doors")
            elif t in KILL_CHEST_TAGS:
                kinds.add("chest")
            elif t == KILLROOMBLOCK_TAG:
                kinds.add("killroomblock")
        if not kinds:
            continue

        souls = set()
        boss_souled = False
        drop_source_slots = []
        runtime_slot = 0
        for _li, y, x, typ in sprite_entries(sprites, spr_offs, room):
            if typ == 0xE4 and y in (0xFE, 0xFD):
                drop_source_slots.append(runtime_slot - 1)
                continue
            if x >= 0xE0:
                continue
            if typ in enemy_soul_for:
                souls.add(enemy_soul_for[typ])
            elif typ in boss_soul_species:
                boss_souled = True
            runtime_slot += 1

        if boss_souled:
            # Refight/boss rooms: gated by the NeedsBossSoul terms inside the
            # CanKill<Boss> macros on every location the flood would find
            # (verified against 33_ganons_tower.yaml during implementation).
            # They must not ALSO carry enemy souls, or this exclusion would
            # drop a real requirement.
            if souls:
                die(f"room 0x{room:03X} mixes boss souls with enemy souls "
                    f"{sorted(souls)} — needs explicit modeling")
            waived.append({"room": room, "reason": "boss_soul_room",
                           "tags": [f"0x{t:02X}" for t in tags]})
            continue
        if "killroomblock" in kinds and souls:
            # Tag 0x26's gating effect is unmodeled; today its only room (TR
            # 0x004) has no soul-mapped residents. If that ever changes, model
            # it before shipping.
            die(f"room 0x{room:03X} has KillRoomBlock tag 0x26 WITH enemy souls "
                f"{sorted(souls)} — unmodeled kill gate")
        if not souls:
            # No enemy-soul residents -> the hold can never bind here (soul-less
            # species always spawn; enemy shuffle only substitutes randomizable
            # species, all of which carry souls per gen_soul_tables' assertion).
            waived.append({"room": room, "reason": "no_enemy_soul_residents",
                           "tags": [f"0x{t:02X}" for t in tags]})
            continue
        rows.append({
            "room": room,
            "kinds": sorted(kinds),
            "souls": sorted(souls),
            "drop_source_slots": sorted(s for s in drop_source_slots if s >= 0),
        })
    return rows, waived


def parse_boss_home_rooms() -> set:
    """Boss home rooms from src/dungeon.c's kBossRooms (authoritative,
    committed). Boss rooms carry no kill-gate tag, so the kill-tag scan never
    sees them — but the runtime prize spawns are gated on
    Sprite_CheckIfScreenIsClear, which the suppressed-spawn hold also blocks.
    A boss home room containing an ENEMY-family-soul resident would let a
    suppressed bystander swallow a legitimately-killed boss's prize; assert
    the combination out of existence (fresh-eyes review)."""
    text = (REPO / "src" / "dungeon.c").read_text(encoding="utf-8")
    m = re.search(r"kBossRooms\[[^\]]*\]\s*=\s*\{([^}]*)\}", text)
    if not m:
        die("kBossRooms not found in src/dungeon.c")
    return {int(v.strip(), 0) for v in m.group(1).split(",") if v.strip()}


def forced_drop_rooms(assets) -> set:
    """Rooms containing a forced key-drop marker (0xE4 + y in 0xFE/0xFD)."""
    sprites = assets["kDungeonSprites"]
    spr_offs = parse_u16le_array(assets["kDungeonSpriteOffs"])
    out = set()
    for room in range(len(spr_offs)):
        for _li, y, x, typ in sprite_entries(sprites, spr_offs, room):
            if typ == 0xE4 and y in (0xFE, 0xFD):
                out.add(room)
                break
    return out


def assert_boss_home_rooms_unmixed(assets, enemy_soul_for) -> None:
    """See parse_boss_home_rooms — fail generation if any boss home room gains
    an enemy-family-soul resident (a suppressed bystander would hold the
    boss-death prize spawn shut)."""
    sprites = assets["kDungeonSprites"]
    spr_offs = parse_u16le_array(assets["kDungeonSpriteOffs"])
    for room in sorted(parse_boss_home_rooms()):
        mixed = set()
        for _li, y, x, typ in sprite_entries(sprites, spr_offs, room):
            if typ == 0xE4 and y in (0xFE, 0xFD):
                continue
            if x >= 0xE0:
                continue
            if typ in enemy_soul_for:
                mixed.add(enemy_soul_for[typ])
        if mixed:
            die(f"boss home room 0x{room:03X} contains enemy-soul residents "
                f"{sorted(mixed)} — a suppressed bystander would swallow the "
                f"boss prize spawn; model this before shipping")


def make_doc(assets_path: Path, assets, graph: DoorGraph,
             enemy_soul_for, boss_soul_species) -> dict:
    rows, waived = scan_rooms(assets, enemy_soul_for, boss_soul_species)
    assert_boss_home_rooms_unmixed(assets, enemy_soul_for)
    graph_rooms = graph.rooms_in_graph()

    kill_rooms = []
    loc_wraps = defaultdict(set)      # fork loc id -> souls
    loc_names = {}                    # fork loc id -> audit name
    region_wraps = defaultdict(set)   # door-graph region id -> souls
    name_wraps = defaultdict(set)     # curated logic-location NAME -> souls
    pin_rooms = set()

    for row in rows:
        room = row["room"]
        souls = row["souls"]
        pin_rooms.add(room)
        entry = dict(row)
        if room in graph_rooms:
            entry["dungeon"] = graph.dungeon_of_room(room)
            entry["dungeon_name"] = graph.dungeon_names[entry["dungeon"]]
            behind, trap = set(), set()
            if "doors" in row["kinds"]:
                behind, trap = graph.behind_regions(room)
                # Cross-check the door-graph trap kinds against the room-data
                # door list — two independent decoders of the same source.
                if bool(trap) != bool(room_data_trap_doors(assets, room)):
                    die(f"room 0x{room:03X}: door-graph trap doors "
                        f"({len(trap)}) disagree with room-data door list")
            own_regions = graph.regions_of_room(room)
            gated_regions = set(behind)
            if "chest" in row["kinds"]:
                gated_regions |= own_regions  # chest reveal gates the room's own chests
            gated_loc_ids = []
            for reg in sorted(gated_regions):
                for loc_id, loc_name in graph.locs_in_region.get(reg, []):
                    # chest-kind own-region wraps + doors-kind behind wraps
                    # both funnel here; union below dedups.
                    gated_loc_ids.append(loc_id)
                    loc_names[loc_id] = loc_name
                    loc_wraps[loc_id].update(souls)
                rn = graph.region_name.get(reg, "")
                for site in EVENT_SITE_REGIONS.get(rn, []):
                    name_wraps[site].update(souls)
            # Only DOOR-gated regions gate generated (pot/enemy) locations by
            # region: a chest-kind tag reveals chests, it does not close the
            # room, so floor drops/pots there stay collectable.
            for reg in sorted(behind):
                region_wraps[reg].update(souls)
            entry["behind_regions"] = sorted(behind)
            entry["behind_region_names"] = [graph.region_name.get(r, "?") for r in sorted(behind)]
            entry["gated_loc_ids"] = sorted(set(gated_loc_ids))
            entry["trap_doors"] = sorted(graph.doors[di][1] for di in trap) if trap else []
            if "doors" in row["kinds"] and not trap:
                die(f"room 0x{room:03X} kill->doors tag but no trap doors in the door graph")
        else:
            binding = CAVE_BINDINGS.get(room)
            if binding is None:
                die(f"kill-gated room 0x{room:03X} with souls {souls} is outside "
                    f"the door graph and has no reviewed cave binding")
            if not room_data_trap_doors(assets, room):
                die(f"cave room 0x{room:03X} ({binding['name']}) binding exists "
                    f"but the room-data door list has no trap doors")
            entry["cave"] = binding["name"]
            entry["gated_loc_ids"] = list(binding["loc_ids"])
            for loc_id, loc_name in zip(binding["loc_ids"], binding["loc_names"]):
                loc_wraps[loc_id].update(souls)
                loc_names[loc_id] = loc_name
        kill_rooms.append(entry)

    # Reviewed cave bindings must all have been consumed (a binding for a room
    # that lost its kill tag or its souls is stale).
    bound_rooms = {r["room"] for r in kill_rooms}
    for room in CAVE_BINDINGS:
        if room not in bound_rooms:
            die(f"CAVE_BINDINGS entry 0x{room:03X} matched no kill-gated souled room (stale binding)")

    # Pot locations are matched by ROOM (pots.gen.yaml has no door-region
    # column): a room whose regions intersect ANY kill room's behind-set gets
    # that kill room's souls. Over-strict when only part of a room is gated
    # (the pot may sit in the open sub-region) — sound, never permissive.
    # Cave kill rooms gate their own pots the same way (Mimic Cave has one).
    room_wraps = defaultdict(set)
    behind_by_room = {r["room"]: set(r.get("behind_regions", [])) for r in kill_rooms}
    # 0xFF is the door table's "no room" sentinel (hole/warp doors), not a real
    # room — regions_of_room(0xFF) unions unrelated dungeons and would wrap any
    # future room-255 location with every soul (fresh-eyes review).
    all_graph_rooms = graph.rooms_in_graph() - {0xFF}
    for r2 in all_graph_rooms:
        regs = graph.regions_of_room(r2)
        for kr in kill_rooms:
            if regs & behind_by_room.get(kr["room"], set()):
                room_wraps[r2].update(kr["souls"])
    for kr in kill_rooms:
        if "cave" in kr:
            room_wraps[kr["room"]].update(kr["souls"])

    # NOTE (fresh-eyes review): when a kill room's ONLY soul-mapped residents
    # are its forced-drop sources, the runtime hold is inert at
    # enemy_drop_checks=off (the exemption spawns them), so that room's wraps
    # are over-strict there — kept anyway (sound; the wraps must hold at
    # edc>=keys where suppression applies to the sources too).

    # Forced key-drop rooms are ALSO pinned to vanilla species under the
    # enemies tier: the drop locations' logic predicates (gen_enemy_drop_
    # tables.py) require the VANILLA holder's soul, and the runtime edc=off
    # exemption identifies the holder by its vanilla drop-source slot — both
    # break if enemy shuffle substitutes the species. Kill rooms above are
    # pinned for the same reason (their soul terms name vanilla residents).
    drop_rooms = forced_drop_rooms(assets)
    pin_rooms |= drop_rooms

    return {
        "format_version": 1,
        "_generated_by": "assets/scripts/gen_soul_room_tables.py (do not hand-edit)",
        "source": {
            "assets": str(assets_path.name),
            "sha256": hashlib.sha256(assets_path.read_bytes()).hexdigest(),
            "door_tables": "src/rando/door_tables.gen.c",
        },
        "kill_rooms": kill_rooms,
        "location_wraps": [
            {"loc_id": lid, "name": loc_names.get(lid, ""), "souls": sorted(s)}
            for lid, s in sorted(loc_wraps.items())
        ],
        "region_wraps": [
            {"region": reg, "name": graph.region_name.get(reg, ""), "souls": sorted(s)}
            for reg, s in sorted(region_wraps.items())
        ],
        "name_wraps": [
            {"name": nm, "souls": sorted(s)} for nm, s in sorted(name_wraps.items())
        ],
        "room_wraps": [
            {"room": rm, "souls": sorted(s)} for rm, s in sorted(room_wraps.items())
        ],
        "pin_rooms": sorted(pin_rooms),
        "forced_drop_rooms": sorted(drop_rooms),
        "waived_rooms": waived,
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
        print(f"gen_soul_room_tables: OK {path} is fresh", file=sys.stderr)
        return 0
    print(f"gen_soul_room_tables: ERROR: {path} is stale; run "
          f"`python assets/scripts/gen_soul_room_tables.py` to refresh it.",
          file=sys.stderr)
    have_text = have.decode("utf-8", "replace")
    for line in difflib.unified_diff(
            have_text.splitlines(), expected.splitlines(),
            fromfile=str(path), tofile=f"{path} (regenerated)", lineterm=""):
        print(line, file=sys.stderr)
    return 1


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--assets", type=Path, default=DEFAULT_ASSETS)
    ap.add_argument("--out", type=Path, default=DEFAULT_OUT)
    ap.add_argument("--check", action="store_true",
                    help="Fail if the output is stale instead of writing it")
    args = ap.parse_args(argv)

    assets_path = args.assets if args.assets.is_absolute() else (REPO / args.assets)
    out_path = args.out if args.out.is_absolute() else (REPO / args.out)
    if not assets_path.exists():
        die(f"missing {assets_path}; build/extract assets first")
    if not DOOR_TABLES_C.exists():
        die(f"missing {DOOR_TABLES_C} (committed artifact — bad checkout?)")

    assets = read_assets(assets_path)
    graph = DoorGraph(DOOR_TABLES_C)
    enemy_soul_for, boss_soul_species = build_soul_maps()
    doc = make_doc(assets_path, assets, graph, enemy_soul_for, boss_soul_species)
    text = render_yaml(doc)

    if args.check:
        return check_fresh(out_path, text)
    write_lf(out_path, text)
    print(f"gen_soul_room_tables: wrote {len(doc['kill_rooms'])} kill rooms, "
          f"{len(doc['location_wraps'])} location wraps, "
          f"{len(doc['region_wraps'])} region wraps, "
          f"{len(doc['pin_rooms'])} pinned rooms to {out_path}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
