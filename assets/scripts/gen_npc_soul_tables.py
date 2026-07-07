#!/usr/bin/env python3
"""gen_npc_soul_tables.py — emit src/rando/npc_soul_tables.h for the NPC souls
feature (openspec/changes/archive/2026-07-07-add-npc-souls).

The 23-NPC roster is curated HERE (this script is the spec for the data), but
every SITE row is DERIVED from the packed sprite assets + the sprite handler
table — never hand-transcribed (the plan-review pass falsified 3 transcribed
ids; see add-npc-souls design.md "Transcribed sprite table was WRONG").

Sources parsed:
  - zelda3_assets.dat: kDungeonSprites/kDungeonSpriteOffs (room lists),
    kOverworldSprites/kOverworldSpriteOffs (3 stages x 144 areas)
  - src/sprite_main.c: kSpriteActiveRoutines[243] (id -> handler symbol)

Modes:
  --scan   print every occurrence of each roster sprite id (authoring aid)
  default  validate assets/rando/npc_souls.yaml against the scan + emit the
           committed header src/rando/npc_soul_tables.h
  --check  verify the committed header matches (CI) without writing

Invariants enforced here (fail-closed):
  - every roster sprite id resolves to the EXPECTED handler symbol
  - 0xE9 never has site rows (potion cauldrons + the powder-bag grant
    sprite); 0xC0 sites are area-kind only (the catfish's own static entry —
    the dynamic 0xC0 reward deliveries never consult NPC sites by design)
  - no 0xBB site collides with a Retro take-any host room (0x10F/0x112/0x11F)
  - room-keyed (interior) sites are world=any (Inverted swaps entering-worlds)
  - every site row matches an occurrence found in the packed assets
  - site rows are disjoint from kSoulForSprite species (enemy souls own those)
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(REPO / "assets" / "scripts"))

from gen_enemy_drop_tables import (  # noqa: E402
    die,
    parse_u16le_array,
    read_assets,
    sprite_entries,
)

SPRITE_MAIN_C = REPO / "src" / "sprite_main.c"
ASSETS_DAT = REPO / "zelda3_assets.dat"
SOUL_TABLES_H = REPO / "src" / "rando" / "soul_tables.h"
OUT_HEADER = REPO / "src" / "rando" / "npc_soul_tables.h"
NPC_YAML = REPO / "assets" / "rando" / "npc_souls.yaml"

DUNGEON_ROOM_COUNT = 0x128
TAKE_ANY_HOST_ROOMS = (0x10F, 0x112, 0x11F)
NEVER_SITE_TYPES = {0xE9}  # 0xC0 is area-site-only (see validate())

# ---------------------------------------------------------------------------
# Roster: (token, sprite id, expected handler symbol). Order defines the NPC
# soul index (appended after kSoulCount in the combined ownership bitfield)
# and the registry item id (kSoulItemBase + kSoulCount + index). Append-only.
# ---------------------------------------------------------------------------
ROSTER = [
    ("Sahasrahla",     0x16, "Sprite_16_Elder_bounce"),
    ("KingZora",       0x52, "Sprite_52_KingZora"),
    ("Witch",          0x36, "Sprite_Witch"),
    ("MagicBat",       0x3A, "Sprite_3A_MagicBat"),
    ("SickKid",        0x1F, "Sprite_1F_SickKid"),
    ("BottleMerchant", 0x75, "Sprite_BottleVendor"),
    ("Hobo",           0x2B, "Sprite_2B_Hobo"),
    ("OldMan",         0xAD, "Sprite_AD_OldMan"),
    ("Stumpy",         0x2E, "Sprite_2E_FluteKid"),
    ("Catfish",        0xC0, "Sprite_C0_Catfish"),
    ("WaterfallFairy", None, None),                  # wish-pond grant branch, NO sprite
    ("PyramidFairy",   None, None),                  # wish-pond grant branch, NO sprite
    ("HomeSmith",      0x1A, "Sprite_1A_Smithy"),
    ("FrogSmith",      0x1A, "Sprite_1A_Smithy"),
    ("MiddleAgedMan",  0x39, "Sprite_39_Locksmith"),
    ("DiggingGame",    0xD5, "Sprite_D5_DigGameGuy"),
    ("ChestGame",      0xBB, "Sprite_BB_Shopkeeper"),
    ("MazeGameLady",   0x2F, "Sprite_MazeGameLady"),
    ("MazeGameGuy",    0x30, "Sprite_MazeGameGuy"),
    ("MMCNpc",         0xBB, "Sprite_BB_Shopkeeper"),
    ("HypeCaveNpc",    0xBB, "Sprite_BB_Shopkeeper"),
    ("Kiki",           0xB6, "Sprite_B6_Kiki"),
    ("BombShopDealer", 0xB5, "Sprite_B5_BombShop"),
    ("Uncle",          0x73, "Sprite_73_UncleAndPriest"),
]

NPC_SOUL_COUNT = len(ROSTER)
assert NPC_SOUL_COUNT == 24, "roster size is a user-locked decision (23 + Uncle, added 2026-07-06)"

# ---------------------------------------------------------------------------
# Site bindings — CURATED disambiguation over the scanned occurrences. Every
# row is validated against the packed assets (must exist) and against the
# exclusion rules (shared-id actors that must NOT be suppressed). Scan-found
# occurrences deliberately NOT bound:
#   0x16 room 0x10A            = Aginah (his own check stays vanilla)
#   0x2E areas 0x2A (LW)       = the light-world flute boy (flute quest)
#   0xBB rooms 0x0FF/0x100/0x10F/0x110/0x112/0x118/0x11F/0x124/0x125
#                              = shops / take-any hosts / other minigames /
#                                under-rock thieves (not checks)
# kind: "room" (interior static load; world MUST be any — Inverted swaps
# entering-worlds) or "area" (overworld proxima; world scoping allowed).
# Overworld special areas (>= 0x80: under-bridge 0x80, Zora's domain 0x81)
# are world=any (they are unique overlay areas, not LW/DW pairs).
# ---------------------------------------------------------------------------
W_ANY, W_LW, W_DW = "any", "lw", "dw"
SITES = [
    # (soul token, kind, id, world)
    ("Sahasrahla",     "room", 0x105, W_ANY),
    ("KingZora",       "area", 0x81,  W_ANY),   # special overlay area
    ("Witch",          "area", 0x16,  W_LW),
    ("MagicBat",       "room", 0x0E3, W_ANY),
    ("SickKid",        "room", 0x102, W_ANY),
    ("BottleMerchant", "area", 0x18,  W_LW),
    ("Hobo",           "area", 0x80,  W_ANY),   # special overlay area
    ("OldMan",         "room", 0x0E4, W_ANY),
    ("OldMan",         "room", 0x0F0, W_ANY),
    ("Stumpy",         "area", 0x6A,  W_DW),    # 0x2A LW = flute boy, unbound
    ("Catfish",        "area", 0x4F,  W_DW),    # static sprite; dynamic 0xC0
                                                # deliveries never funnel-gated
    ("HomeSmith",      "room", 0x121, W_ANY),
    ("FrogSmith",      "area", 0x69,  W_DW),
    ("MiddleAgedMan",  "area", 0x3A,  W_LW),
    ("DiggingGame",    "area", 0x68,  W_DW),
    ("ChestGame",      "room", 0x106, W_ANY),
    ("MazeGameLady",   "area", 0x28,  W_LW),
    ("MazeGameGuy",    "area", 0x28,  W_LW),
    ("MMCNpc",         "room", 0x123, W_ANY),
    ("HypeCaveNpc",    "room", 0x11E, W_ANY),
    ("Kiki",           "area", 0x5E,  W_DW),
    ("BombShopDealer", "room", 0x11C, W_ANY),
    # Uncle: bind ONLY the secret-passage grant site (room 0x055). Room 0x104
    # (Link's house) is the Standard opening choreography actor — suppressing
    # him risks wedging the intro trigger chain without gating anything; room
    # 0x012 is the PRIEST (Zelda escort delivery) sharing the 0x73 handler.
    ("Uncle",          "room", 0x055, W_ANY),
]

# Location gates: fork location-registry NAME -> required souls (ALL of them).
# Injected into logic by rando_logic_gen.py reading the emitted yaml. The
# Kiki PoD entry-EDGE gate is a separate section (edges, not locations).
GATES = {
    "Sahasrahla":               ["Sahasrahla"],
    "King Zora":                ["KingZora"],
    "Potion Shop":              ["Witch"],
    "Magic Bat":                ["MagicBat"],
    "Sick Kid":                 ["SickKid"],
    "Bottle Merchant":          ["BottleMerchant"],
    "Hobo":                     ["Hobo"],
    "Old Man":                  ["OldMan"],
    "Stumpy":                   ["Stumpy"],
    "Catfish":                  ["Catfish"],
    "Waterfall Fairy - Left":   ["WaterfallFairy"],
    "Waterfall Fairy - Right":  ["WaterfallFairy"],
    "Pyramid Fairy - Left":     ["PyramidFairy", "BombShopDealer"],
    "Pyramid Fairy - Right":    ["PyramidFairy", "BombShopDealer"],
    "Blacksmith":               ["HomeSmith", "FrogSmith"],
    "Purple Chest":             ["HomeSmith", "FrogSmith", "MiddleAgedMan"],
    "Digging Game":             ["DiggingGame"],
    "Chest Game":               ["ChestGame"],
    "Maze Race":                ["MazeGameLady", "MazeGameGuy"],
    "Mini Moldorm Cave - NPC":  ["MMCNpc"],
    "Link's Uncle":             ["Uncle"],
    "Hype Cave - NPC":          ["HypeCaveNpc"],
}

# PoD entry edge gate (Kiki's payoff opens the dungeon). The base edge lives in
# logic_parts/20_palace_of_darkness.yaml; Inverted's additive TRUE() edge
# bypasses it there by design.
EDGE_GATES = [
    # Verified: logic_parts/20_palace_of_darkness.yaml edge
    # DarkWorld_NorthEast -> PalaceOfDarkness (RescuedZelda AND pearl-gate).
    {"from": "DarkWorld_NorthEast", "to": "PalaceOfDarkness",
     "souls": ["Kiki"]},
]


def parse_handler_table() -> list[str | None]:
    text = SPRITE_MAIN_C.read_text(encoding="utf-8", errors="replace")
    m = re.search(
        r"kSpriteActiveRoutines\[(\d+)\]\s*=\s*\{(.*?)\};", text, re.S)
    if not m:
        die("kSpriteActiveRoutines table not found in sprite_main.c")
    count = int(m.group(1))
    body = re.sub(r"/\*.*?\*/", "", m.group(2), flags=re.S)  # block comments
    body = re.sub(r"//[^\n]*", "", body)                     # line comments
    entries: list[str | None] = []
    for tok in body.split(","):
        tok = tok.strip()
        if not tok:
            continue
        if tok == "NULL":
            entries.append(None)
        else:
            mm = re.match(r"&(\w+)", tok)
            if not mm:
                die(f"unparseable handler entry: {tok!r}")
            entries.append(mm.group(1))
    if len(entries) != count:
        die(f"handler table: parsed {len(entries)} entries, declared {count}")
    return entries


def parse_soul_for_sprite() -> dict[int, int]:
    """Enemy-souls species map from soul_tables.h — NPC sites must be disjoint."""
    text = SOUL_TABLES_H.read_text(encoding="utf-8")
    m = re.search(r"kSoulForSprite\[256\]\s*=\s*\{(.*?)\};", text, re.S)
    if not m:
        die("kSoulForSprite not found in soul_tables.h")
    body = re.sub(r"/\*.*?\*/", "", m.group(1), flags=re.S)
    body = re.sub(r"//[^\n]*", "", body)
    vals = [v.strip() for v in body.replace("\n", " ").split(",") if v.strip()]
    if len(vals) != 256:
        die(f"kSoulForSprite: {len(vals)} entries, want 256")
    out = {}
    for i, v in enumerate(vals):
        n = int(v, 0)
        if n != 0xFF:
            out[i] = n
    return out


def iter_overworld_entries(sprites: bytes, offsets: list[int]):
    for offset_index, off in enumerate(offsets):
        if off >= len(sprites):
            die(f"overworld offset index {offset_index} outside kOverworldSprites")
        stage = offset_index // 144
        area = offset_index % 144
        pos = off
        slot = 0
        while pos < len(sprites):
            y = sprites[pos]
            if y == 0xFF:
                break
            if pos + 2 >= len(sprites):
                die(f"overworld list {offset_index} truncated")
            x = sprites[pos + 1]
            typ = sprites[pos + 2]
            yield stage, area, slot, y, x, typ
            slot += 1
            pos += 3
        else:
            die(f"overworld list {offset_index} missing 0xff terminator")


def scan_occurrences(assets: dict[str, bytes]) -> dict[int, dict]:
    """type -> {"rooms": {room: [slots]}, "areas": {(stage, area): [slots]}}"""
    want = {sid for _, sid, _ in ROSTER if sid is not None}
    out: dict[int, dict] = {t: {"rooms": {}, "areas": {}} for t in want}
    sprites = assets["kDungeonSprites"]
    offs = parse_u16le_array(assets["kDungeonSpriteOffs"])
    for room in range(min(DUNGEON_ROOM_COUNT, len(offs))):
        for slot, y, x, typ in sprite_entries(sprites, offs, room):
            if x >= 0xE0:
                continue  # OVERLORD entry — its type byte is an overlord id,
                          # NOT a sprite id (x>=0xE0 is the marker; CLAUDE.md).
            if typ in want:
                out[typ]["rooms"].setdefault(room, []).append(slot)
    ow = assets["kOverworldSprites"]
    ow_offs = parse_u16le_array(assets["kOverworldSpriteOffs"])
    for stage, area, slot, y, x, typ in iter_overworld_entries(ow, ow_offs):
        if typ in want:
            out[typ]["areas"].setdefault((stage, area), []).append(slot)
    return out


def cmd_scan() -> int:
    handlers = parse_handler_table()
    assets = read_assets(ASSETS_DAT)
    occ = scan_occurrences(assets)
    print("# Roster occurrence scan (authoring aid for npc_souls.yaml)")
    for token, sid, expect in ROSTER:
        if sid is None:
            print(f"\n{token}: (no sprite — trigger/grant-branch gated)")
            continue
        h = handlers[sid] if sid < len(handlers) else None
        ok = "OK" if h == expect else f"MISMATCH (table has {h})"
        print(f"\n{token}: type=0x{sid:02X} handler={expect} [{ok}]")
        rooms = occ[sid]["rooms"]
        for room in sorted(rooms):
            print(f"  room 0x{room:03X} slots {rooms[room]}")
        areas = occ[sid]["areas"]
        for (stage, area) in sorted(areas):
            world = "DW" if area >= 0x40 and area < 0x80 else "LW"
            print(f"  overworld stage {stage} area 0x{area:02X} ({world}) slots {areas[(stage, area)]}")
        if not rooms and not areas:
            print("  (no static occurrences — dynamic-only sprite)")
    return 0


def cmd_dump_rooms(rooms: list[int]) -> int:
    assets = read_assets(ASSETS_DAT)
    sprites = assets["kDungeonSprites"]
    offs = parse_u16le_array(assets["kDungeonSpriteOffs"])
    for room in rooms:
        print(f"room 0x{room:03X}:")
        for slot, y, x, typ in sprite_entries(sprites, offs, room):
            marker = " (overlord)" if x >= 0xE0 else ""
            print(f"  slot {slot}: type=0x{typ:02X} y=0x{y:02X} x=0x{x:02X}{marker}")
    return 0


def validate(handlers: list[str | None], occ: dict[int, dict]) -> None:
    tokens = [t for t, _, _ in ROSTER]
    if len(set(tokens)) != len(tokens):
        die("duplicate roster token")
    by_token = {t: (sid, h) for t, sid, h in ROSTER}
    # id -> expected handler
    for token, sid, expect in ROSTER:
        if sid is None:
            continue
        h = handlers[sid] if sid < len(handlers) else None
        if h != expect:
            die(f"{token}: sprite 0x{sid:02X} handler is {h}, expected {expect}")
    # enemy-souls disjointness
    enemy_map = parse_soul_for_sprite()
    for token, sid, _ in ROSTER:
        if sid is not None and sid in enemy_map:
            die(f"{token}: sprite 0x{sid:02X} already owned by enemy souls")
    # site rows
    for token, kind, sid_key, world in SITES:
        if token not in by_token:
            die(f"site row references unknown soul {token!r}")
        stype = by_token[token][0]
        if stype is None:
            die(f"{token}: has site rows but no sprite id")
        if stype == 0xE9:
            die(f"{token}: 0xE9 must NEVER have site rows (potion cauldrons "
                f"+ the powder-bag grant sprite)")
        if stype == 0xC0 and kind != "area":
            # The static catfish (area 0x4F) is suppressible; the DYNAMIC
            # 0xC0 reward deliveries (King Zora flippers / Catfish medallion)
            # are safe because the dynamic funnel NEVER consults NPC sites.
            die(f"{token}: 0xC0 sites must be area-kind only")
        if kind == "room":
            if world != W_ANY:
                die(f"{token}: room-keyed site must be world=any "
                    f"(Inverted swaps interior entering-worlds)")
            if stype == 0xBB and sid_key in TAKE_ANY_HOST_ROOMS:
                die(f"{token}: 0xBB site collides with take-any host room "
                    f"0x{sid_key:03X}")
            if sid_key not in occ[stype]["rooms"]:
                die(f"{token}: no 0x{stype:02X} in room 0x{sid_key:03X} "
                    f"per the packed assets")
        elif kind == "area":
            if sid_key >= 0x80 and world != W_ANY:
                die(f"{token}: special overlay area 0x{sid_key:02X} must be "
                    f"world=any")
            found = {a for (_, a) in occ[stype]["areas"]}
            if sid_key not in found:
                die(f"{token}: no 0x{stype:02X} in overworld area "
                    f"0x{sid_key:02X} per the packed assets")
        else:
            die(f"{token}: bad site kind {kind!r}")
    # gates reference known souls; catfish 0xC0 never in trigger position etc.
    sited = {t for t, _, _, _ in SITES}
    for loc, souls in GATES.items():
        for s in souls:
            if s not in by_token:
                die(f"gate {loc!r} references unknown soul {s!r}")
    for e in EDGE_GATES:
        for s in e["souls"]:
            if s not in by_token:
                die(f"edge gate references unknown soul {s!r}")
    # every soul is enforced somewhere: a site row, or a documented
    # grant-branch gate (fairies), and appears in at least one gate.
    gated = {s for souls in GATES.values() for s in souls}
    gated |= {s for e in EDGE_GATES for s in e["souls"]}
    for token, sid, _ in ROSTER:
        if token not in gated:
            die(f"{token}: appears in no location/edge gate")
        if sid is not None and token not in sited:
            die(f"{token}: sprite-backed soul with no site row")


def render_header() -> str:
    lines = []
    a = lines.append
    a("// npc_soul_tables.h — GENERATED by assets/scripts/gen_npc_soul_tables.py")
    a("// (add-npc-souls). Curated roster + asset-derived site rows; do not")
    a("// hand-edit — regenerate (script asserts ids, sites, and exclusions).")
    a("#pragma once")
    a("#include \"../types.h\"")
    a("")
    a("enum {")
    for i, (token, _, _) in enumerate(ROSTER):
        a(f"  kNpcSoul_{token} = {i},")
    a(f"  kNpcSoulCount = {NPC_SOUL_COUNT},")
    a("};")
    a("")
    a("// Suppression site: sprite `type` at `key` (room id when `is_room`,")
    a("// else overworld area id), `world`: 0=any 1=LW-only 2=DW-only.")
    a("// Sorted by (type, key) — linear scan is fine (22 rows, spawn-time).")
    a("// INVARIANT: consulted ONLY by the static-room and overworld-proxima")
    a("// hooks — NEVER by Sprite_SpawnDynamicallyEx. The dynamic 0xC0 reward")
    a("// deliveries (King Zora flippers / Catfish medallion) must always")
    a("// spawn; only the catfish's own STATIC area entry is suppressed.")
    a("typedef struct NpcSoulSite {")
    a("  uint8 type;")
    a("  uint8 is_room;")
    a("  uint16 key;")
    a("  uint8 world;")
    a("  uint8 npc_soul;  // kNpcSoul_*")
    a("} NpcSoulSite;")
    a("")
    a("// Exactly one TU (souls.c) defines NPC_SOUL_TABLES_IMPL (the")
    a("// SOUL_TABLES_IMPL pattern).")
    tok_idx = {t: i for i, (t, _, _) in enumerate(ROSTER)}
    by_type = {t: sid for t, sid, _ in ROSTER if sid is not None}
    rows = []
    for token, kind, key, world in SITES:
        wv = {W_ANY: 0, W_LW: 1, W_DW: 2}[world]
        rows.append((by_type[token], 1 if kind == "room" else 0, key, wv,
                     tok_idx[token], token))
    rows.sort(key=lambda r: (r[0], r[2]))
    a(f"enum {{ kNpcSoulSiteCount = {len(rows)} }};")
    a("extern const NpcSoulSite kNpcSoulSites[kNpcSoulSiteCount];")
    a("extern const char *const kNpcSoulNames[kNpcSoulCount];")
    a("")
    a("#ifdef NPC_SOUL_TABLES_IMPL")
    a("const NpcSoulSite kNpcSoulSites[kNpcSoulSiteCount] = {")
    for stype, is_room, key, wv, soul, token in rows:
        a(f"  {{ 0x{stype:02X}, {is_room}, 0x{key:04X}, {wv}, "
          f"kNpcSoul_{token} }},")
    a("};")
    a("const char *const kNpcSoulNames[kNpcSoulCount] = {")
    for token, _, _ in ROSTER:
        a(f"  \"Soul_Npc_{token}\",")
    a("};")
    a("#endif  // NPC_SOUL_TABLES_IMPL")
    a("")
    a("// Souls with no sprite site (gated at their grant branch / trigger):")
    for token, sid, _ in ROSTER:
        if token not in {t for t, _, _, _ in SITES}:
            a(f"//   kNpcSoul_{token}")
    a("")
    return "\n".join(lines) + "\n"


def render_yaml() -> str:
    lines = []
    a = lines.append
    a("# npc_souls.yaml — GENERATED by assets/scripts/gen_npc_soul_tables.py")
    a("# (add-npc-souls). Read by rando_logic_gen.py for the location/edge soul")
    a("# gates. Regenerate, never hand-edit.")
    a("souls:")
    for i, (token, sid, _) in enumerate(ROSTER):
        sid_s = f"0x{sid:02X}" if sid is not None else "null"
        a(f"  - {{ token: {token}, index: {i}, sprite: {sid_s} }}")
    a("gates:")
    for loc, souls in GATES.items():
        a(f"  - location: \"{loc}\"")
        a(f"    souls: [{', '.join(souls)}]")
    a("edge_gates:")
    for e in EDGE_GATES:
        a(f"  - {{ from: {e['from']}, to: {e['to']}, "
          f"souls: [{', '.join(e['souls'])}] }}")
    return "\n".join(lines) + "\n"


def write_lf(path: Path, text: str) -> None:
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)


def cmd_emit(check: bool) -> int:
    handlers = parse_handler_table()
    assets = read_assets(ASSETS_DAT)
    occ = scan_occurrences(assets)
    validate(handlers, occ)
    header = render_header()
    yaml_text = render_yaml()
    if check:
        ok = True
        for path, want in ((OUT_HEADER, header), (NPC_YAML, yaml_text)):
            have = path.read_text(encoding="utf-8") if path.exists() else ""
            if have != want:
                print(f"STALE: {path}", file=sys.stderr)
                ok = False
        print("gen_npc_soul_tables: check " + ("OK" if ok else "FAILED"))
        return 0 if ok else 1
    write_lf(OUT_HEADER, header)
    write_lf(NPC_YAML, yaml_text)
    print(f"wrote {OUT_HEADER}")
    print(f"wrote {NPC_YAML}")
    return 0


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--scan", action="store_true")
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--dump-room", action="append", default=[],
                    help="hex room id; repeatable (authoring aid)")
    args = ap.parse_args(argv)
    if args.dump_room:
        return cmd_dump_rooms([int(r, 16) for r in args.dump_room])
    if args.scan:
        return cmd_scan()
    return cmd_emit(args.check)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
