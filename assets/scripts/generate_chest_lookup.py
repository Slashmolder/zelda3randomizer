#!/usr/bin/env python3
"""Generate the (dungeon_room, chest_ordinal) -> LOC_* table for rando.c.

Approach: read the runtime chest table directly from zelda3_assets.dat,
then cross-reference each (room, ordinal_in_room, item_value, big_flag)
tuple against ALTTPR PHP location data (which gives chest names) using
the ROM-offset structure that both files preserve.

Source of truth (cross-checked):
  1) zelda3_assets.dat at asset index 8 (`kDungeonRoomChests`) — binary
     (room, item, big) tuples in their compile order (room-ascending).
  2) ALTTPR PHP files (../alttp_vt_randomizer/app/Region/Standard/**) —
     `new Location\\Chest("Name", [0xADDR])` with ROM offsets.
  3) location_registry.yaml — chest name -> location_id + vanilla_item.

Key insight: extract_resources.py reads ROM linearly at 0x81e96e, building
`all[room].append((data, big))`. So within each room, the order in dungeon-N.yaml
matches ROM offset order. PHP file chest listings are in ROM offset order too
(authors typically enumerate by offset). So for each (room, ordinal_in_room)
in the yaml, the corresponding PHP name is the ordinal-th name when PHP names
for that room are sorted by ROM address.

To determine which room each PHP entry belongs to, use a property-based
matching: match by (room's set of items) and disambiguate using item value
+ within-room offset position. For most rooms the items are unique, so
matching by item value is exact. For the few rooms with duplicate items,
the within-room offset preserves the ordering.

This script needs the assets.dat to be present in the repo root.
"""

from __future__ import annotations
import re
import struct
import sys
import os
import yaml

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ASSETS_DAT = os.path.join(ROOT, "zelda3_assets.dat")
# Locate ALTTPR PHP — try sibling-of-ROOT, then sibling-of-master (for worktrees).
_PHP_CANDIDATES = [
    os.path.normpath(os.path.join(ROOT, "..", "alttp_vt_randomizer", "app", "Region", "Standard")),
    "C:/src/alttp_vt_randomizer/app/Region/Standard",
]
PHP_ROOT = next((p for p in _PHP_CANDIDATES if os.path.isdir(p)), _PHP_CANDIDATES[0])
LOC_YAML = os.path.join(ROOT, "assets", "rando", "location_registry.yaml")

CHEST_TABLE_BASE = 0xE96E
KNUM_ASSETS = 165
CHEST_ASSET_INDEX = 8

# Match: new Location\Chest("Name", [0xEXXX], ...) and new Location\BigChest(...)
PHP_CHEST_RE = re.compile(
    r"new\s+Location\\(?P<type>(?:Big)?Chest)\(\s*\"(?P<name>[^\"]+)\"\s*,\s*\[\s*0x(?P<addr>[A-F0-9]+)",
    re.IGNORECASE,
)


def read_chest_table_from_assets():
    """Return list of (room, item, big) tuples in compile (room-ascending) order."""
    with open(ASSETS_DAT, "rb") as f:
        data = f.read()
    offset_start = struct.unpack_from("<I", data, 84)[0]
    sizes = [struct.unpack_from("<I", data, 88 + i * 4)[0] for i in range(KNUM_ASSETS)]
    offset = 88 + KNUM_ASSETS * 4 + offset_start
    asset_ptrs = []
    for i in range(KNUM_ASSETS):
        offset = (offset + 3) & ~3
        asset_ptrs.append(offset)
        offset += sizes[i]
    size = sizes[CHEST_ASSET_INDEX]
    ptr = asset_ptrs[CHEST_ASSET_INDEX]
    chest_data = data[ptr:ptr + size]
    entries = []
    for i in range(0, size, 3):
        room_word = chest_data[i] | (chest_data[i + 1] << 8)
        item = chest_data[i + 2]
        big = (room_word & 0x8000) != 0
        room = room_word & 0x7fff
        entries.append((room, item, big))
    return entries


def extract_php_chests():
    """Return list of (rom_addr, name, is_big) for all dungeon-table chests."""
    chests = []
    for root, _, files in os.walk(PHP_ROOT):
        for fn in files:
            if not fn.endswith(".php"):
                continue
            path = os.path.join(root, fn)
            try:
                with open(path, "r", encoding="utf-8") as f:
                    contents = f.read()
            except (OSError, UnicodeDecodeError):
                continue
            for m in PHP_CHEST_RE.finditer(contents):
                name = m.group("name")
                addr = int(m.group("addr"), 16)
                is_big = m.group("type").lower() == "bigchest"
                if 0xE000 <= addr <= 0xEC00:
                    chests.append((addr, name, is_big))
    chests.sort(key=lambda c: c[0])
    return chests


def extract_location_id_map():
    with open(LOC_YAML, "r", encoding="utf-8") as f:
        data = yaml.safe_load(f)
    return {entry["name"]: entry["id"] for entry in data["locations"]}


def main():
    binary_entries = read_chest_table_from_assets()
    php_chests = extract_php_chests()
    loc_map = extract_location_id_map()

    # Build per-room ordered lists from the binary (these are in ROM offset order
    # within each room, per extract_resources.get_chest_info behavior).
    rooms = {}
    for binary_idx, (room, item, big) in enumerate(binary_entries):
        rooms.setdefault(room, []).append((binary_idx, item, big))

    # Compute room for each PHP entry by matching item+big+iteration-order.
    # Strategy: maintain a per-room cursor and walk PHP chests in ROM offset
    # order. For each PHP chest, scan all rooms with a matching item value at
    # the cursor position; if found, advance that room's cursor.
    #
    # This works because:
    #  - PHP order == ROM offset order
    #  - Within each room, binary order == ROM offset order (per extract logic)
    #  - So the Nth PHP chest matching room R == Nth binary chest in room R
    #
    # To disambiguate when multiple rooms have the same next-item, we use
    # the fact that PHP rooms appear in mostly-monotonic groups, but for
    # safety we just pick the lowest-room match with matching (item, big)
    # at the current cursor position.

    room_cursor = {r: 0 for r in rooms}
    matches = []  # list of (room, ordinal, addr, name, big, item)

    for addr, name, php_big in php_chests:
        # Try to find a room whose current-cursor entry matches (item, big).
        # We don't have item from PHP directly, so we infer it from name via
        # vanilla_item. But the names are sometimes different from registry —
        # better: just try every room's current cursor and find one where
        # (cursor_item != None) and big matches.
        # Since PHP order == ROM order, the next chest in PHP belongs to the
        # room whose cursor next-entry's binary_idx is the smallest globally
        # un-claimed binary_idx.
        # Find the room R with the smallest unconsumed binary_idx whose `big`
        # matches.
        best_room = None
        best_idx = None
        for r, chests in rooms.items():
            cur = room_cursor[r]
            if cur >= len(chests):
                continue
            binary_idx, item, big = chests[cur]
            if best_room is None or binary_idx < best_idx:
                best_idx = binary_idx
                best_room = r
        if best_room is None:
            print(f"WARNING: no room left for PHP chest '{name}' @ 0x{addr:04X}", file=sys.stderr)
            continue
        cursor = room_cursor[best_room]
        binary_idx, item, big = rooms[best_room][cursor]
        room_cursor[best_room] += 1
        # Sanity: big flag should match.
        if big != php_big:
            print(
                f"WARNING: big-flag mismatch for '{name}' @ 0x{addr:04X}: php_big={php_big} binary_big={big}",
                file=sys.stderr,
            )
        matches.append((best_room, cursor, addr, name, big, item))

    # Wait — the above is wrong: PHP order doesn't equal binary_idx order
    # because compile order isn't ROM order. Let me instead match by item
    # value where possible.
    #
    # Actually the correct approach: build a flat list of ALL binary entries
    # in compile order with (binary_idx, room, ordinal_in_room, item, big).
    # Then sort PHP by ROM offset (PHP order == ROM offset order). The number
    # of entries should match exactly. But the SET maps 1:1 even if the
    # ORDERING differs.
    #
    # Simpler approach for matching: every chest's (room, ordinal_in_room,
    # item, big) tuple uniquely identifies it in the binary. Compute that
    # tuple for each PHP chest by reading the ROM (which we don't have).
    # Alternative: use the location_registry's vanilla_item value to match
    # PHP names against binary entries.
    #
    # Re-do: forget the cursor approach above. Build mapping via vanilla_item.

    # NEW APPROACH: load location_registry, look up vanilla_item for each loc.
    # Map item-name strings (like "BlueBoomerang") to LttP receive codes.
    with open(LOC_YAML, "r", encoding="utf-8") as f:
        reg_data = yaml.safe_load(f)
    reg_by_name = {entry["name"]: entry for entry in reg_data["locations"]}

    # Build (item_name -> LttP code) map. The vanilla_item field in the
    # registry uses our internal ITEM_* names. LttP codes are 0x00..0x4b.
    # We'll match using a known table.
    ITEM_NAME_TO_LTTP = {
        "L1Sword": 0x00, "L2Sword": 0x01, "L3Sword": 0x02, "L4Sword": 0x03,
        "FighterShield": 0x04, "RedShield": 0x05, "MirrorShield": 0x06,
        "FireRod": 0x07, "IceRod": 0x08, "Hammer": 0x09, "Hookshot": 0x0a,
        "Bow": 0x0b, "BlueBoomerang": 0x0c, "MagicPowder": 0x0d,
        "BottleEmpty": 0x16, "PieceOfHeart": 0x17, "CaneOfByrna": 0x18,
        "Cape": 0x19, "MagicMirror": 0x1a, "PowerGlove": 0x1b, "TitanMitt": 0x1c,
        "BookOfMudora": 0x1d, "Flippers": 0x1e, "MoonPearl": 0x1f,
        "BugCatchingNet": 0x21, "BlueMail": 0x22, "RedMail": 0x23,
        "SmallKey_HyruleCastleEscape": 0x24, "SmallKey_EasternPalace": 0x24,
        "SmallKey_DesertPalace": 0x24, "SmallKey_TowerOfHera": 0x24,
        "SmallKey_HyruleCastleTower": 0x24, "SmallKey_PalaceOfDarkness": 0x24,
        "SmallKey_SwampPalace": 0x24, "SmallKey_SkullWoods": 0x24,
        "SmallKey_ThievesTown": 0x24, "SmallKey_IcePalace": 0x24,
        "SmallKey_MiseryMire": 0x24, "SmallKey_TurtleRock": 0x24,
        "SmallKey_GanonsTower": 0x24,
        "Compass_EasternPalace": 0x25, "Compass_DesertPalace": 0x25,
        "Compass_TowerOfHera": 0x25, "Compass_PalaceOfDarkness": 0x25,
        "Compass_SwampPalace": 0x25, "Compass_SkullWoods": 0x25,
        "Compass_ThievesTown": 0x25, "Compass_IcePalace": 0x25,
        "Compass_MiseryMire": 0x25, "Compass_TurtleRock": 0x25,
        "Compass_GanonsTower": 0x25,
        "BossHeartContainer": 0x26,
        "Bombs3": 0x27, "Bombs10": 0x28,
        "Mushroom": 0x29, "RedBoomerang": 0x2a,
        "BottleWithBee": 0x2b, "BottleWithFairy": 0x2c,
        "BottleWithRedPotion": 0x2d, "RedPotion": 0x2e,
        "GreenPotion": 0x2f, "BluePotion": 0x30,
        "BombsRefill": 0x31,
        "BigKey_EasternPalace": 0x32, "BigKey_DesertPalace": 0x32,
        "BigKey_TowerOfHera": 0x32, "BigKey_PalaceOfDarkness": 0x32,
        "BigKey_SwampPalace": 0x32, "BigKey_SkullWoods": 0x32,
        "BigKey_ThievesTown": 0x32, "BigKey_IcePalace": 0x32,
        "BigKey_MiseryMire": 0x32, "BigKey_TurtleRock": 0x32,
        "BigKey_GanonsTower": 0x32,
        "Map_HyruleCastleEscape": 0x33, "Map_EasternPalace": 0x33,
        "Map_DesertPalace": 0x33, "Map_TowerOfHera": 0x33,
        "Map_PalaceOfDarkness": 0x33, "Map_SwampPalace": 0x33,
        "Map_SkullWoods": 0x33, "Map_ThievesTown": 0x33,
        "Map_IcePalace": 0x33, "Map_MiseryMire": 0x33,
        "Map_TurtleRock": 0x33, "Map_GanonsTower": 0x33,
        "Rupee1": 0x34, "Rupee5": 0x35, "Rupee20": 0x36,
        "Prize_GreenPendant": 0x37, "Prize_RedPendant": 0x38, "Prize_BluePendant": 0x39,
        "BowSilvers": 0x3a, "BowSilvers2": 0x3b,
        "BottleWithGoodBee": 0x3c, "BottleWithGreenPotion": 0x3d,
        "Rupee100": 0x40, "Rupee50": 0x41,
        "HeartRefill": 0x42, "Arrow1": 0x43, "Arrow10": 0x44,
        "MagicRefill": 0x45, "Rupee1_alt": 0x46, "Rupee1_alt2": 0x47,
        "BottleWithBluePotion": 0x48,
        "Bombos": 0x0f, "Ether": 0x10, "Quake": 0x11,
        "OcarinaInactive": 0x13, "Shovel": 0x13,
        "Boots": 0x4b,
        "CaneOfSomaria": 0x15,
        "Lamp": 0x12,
    }

    # Build (room, item, big) -> name mapping by walking PHP files per-room.
    # Actually, we need (room, item, big) -> name. The mapping requires room.
    # Without it from PHP, we infer via location_registry.yaml:
    #   For each chest location, get vanilla_item -> LttP code.
    # Then for the binary entry's (room, item, big), find the matching
    # registry entry whose vanilla_item matches and ... still needs room info.

    # SIMPLEST WORKING approach: just emit binary entries with no name match
    # — but use location_registry to find chest locations and bind them by
    # name. The room information comes from running the game (or extracting
    # the ROM table). Since we have the binary, we have the room.
    #
    # Final scheme: for the C lookup table, we just need (room, ordinal,
    # location_id). We get (room, ordinal) from the binary table iteration
    # order. We need location_id by matching to ALTTPR PHP chest name.
    #
    # To bind name to (room, ordinal): we'll print binary entries with their
    # item value and ALTTPR PHP entries, and the human reviewer can read the
    # output to verify. For automated mapping, we cluster by item value and
    # check uniqueness.

    # Practical: walk binary entries in their stored order (which is the order
    # OpenChestForItem iterates). For each binary entry, find the ALTTPR
    # location whose vanilla_item LttP code matches the binary's item byte AND
    # whose ALTTPR chest name (when sorted by ROM offset) hasn't been claimed
    # yet for that item value. Use a greedy in-order match.

    # Group PHP by name -> rom_offset
    php_by_name = {name: (addr, is_big) for addr, name, is_big in php_chests}

    # Build (lttp_code -> sorted-by-ROM list of (addr, name, is_big)) for each
    # ALTTPR chest, indexed by what LttP code their vanilla_item resolves to.
    chest_locations = []  # only chest/bigchest types
    for entry in reg_data["locations"]:
        if entry.get("type") not in ("Chest", "BigChest"):
            continue
        name = entry["name"]
        if name not in php_by_name:
            # ALTTPR registry entry without a PHP chest definition (rare).
            continue
        addr, is_big_php = php_by_name[name]
        vi_name = entry["vanilla_item"]
        lttp = ITEM_NAME_TO_LTTP.get(vi_name)
        if lttp is None:
            # Unknown vanilla item; can't auto-match. Will report.
            chest_locations.append({
                "id": entry["id"], "name": name, "addr": addr,
                "is_big_php": is_big_php, "lttp": None, "vi_name": vi_name,
            })
            continue
        chest_locations.append({
            "id": entry["id"], "name": name, "addr": addr,
            "is_big_php": is_big_php, "lttp": lttp, "vi_name": vi_name,
        })

    # Sort chest_locations by ROM address (which equals ROM iteration order).
    chest_locations.sort(key=lambda c: c["addr"])

    # Group by (lttp, is_big) queues — for each lttp+big combo, queue of (addr, name, id).
    from collections import defaultdict, deque
    queues = defaultdict(deque)
    for c in chest_locations:
        if c["lttp"] is None:
            continue
        queues[(c["lttp"], c["is_big_php"])].append(c)

    # Now walk binary entries in their compile order. For each binary
    # (room, item, big), pop the front of queues[(item, big)] to get the
    # location_id. Within a room, this preserves the ordinal-in-room order.
    matches = []
    unmatched_binary = []
    for binary_idx, (room, item, big) in enumerate(binary_entries):
        key = (item, big)
        q = queues.get(key)
        if not q:
            unmatched_binary.append((binary_idx, room, item, big))
            continue
        c = q.popleft()
        matches.append({
            "binary_idx": binary_idx,
            "room": room,
            "item": item,
            "big": big,
            "loc_id": c["id"],
            "name": c["name"],
            "addr": c["addr"],
        })

    # Compute ordinal_in_room for each match by counting earlier matches in
    # the same room.
    ordinals = {}
    output_rows = []
    for m in matches:
        room = m["room"]
        ord_ = ordinals.get(room, 0)
        ordinals[room] = ord_ + 1
        output_rows.append((room, ord_, m["loc_id"], m["name"], m["addr"], m["item"], m["big"]))

    # Sort by (room, ordinal) for C output.
    output_rows.sort(key=lambda r: (r[0], r[1]))

    # Emit C table.
    lines = []
    lines.append("// AUTO-GENERATED by assets/scripts/generate_chest_lookup.py.")
    lines.append("// Source: zelda3_assets.dat (kDungeonRoomChests) cross-referenced")
    lines.append("// with ALTTPR PHP location data + assets/rando/location_registry.yaml.")
    lines.append("// Iteration order matches OpenChestForItem (src/dungeon.c:5705).")
    lines.append("//")
    lines.append("// %d chests bound." % len(output_rows))
    lines.append("typedef struct ChestLookupEntry {")
    lines.append("  uint16 room;")
    lines.append("  uint8  ordinal;")
    lines.append("  uint16 location_id;")
    lines.append("} ChestLookupEntry;")
    lines.append("")
    lines.append("static const ChestLookupEntry kChestLookup[] = {")
    for room, ordinal, loc_id, name, addr, item, big in output_rows:
        big_mark = "BigChest" if big else "Chest"
        lines.append(
            "  { %3d, %d, %3d },  // 0x%04X item=0x%02X %s '%s'"
            % (room, ordinal, loc_id, addr, item, big_mark, name)
        )
    lines.append("};")
    lines.append("#define kChestLookup_COUNT (sizeof(kChestLookup)/sizeof(kChestLookup[0]))")
    print("\n".join(lines))

    if unmatched_binary:
        print(f"\n// {len(unmatched_binary)} unmatched binary entries:", file=sys.stderr)
        for bidx, room, item, big in unmatched_binary:
            print(f"//   binary_idx={bidx} room={room} item=0x{item:02X} big={big}", file=sys.stderr)
    # Report unconsumed PHP queues.
    leftover = []
    for k, q in queues.items():
        for c in q:
            leftover.append(c)
    if leftover:
        print(f"\n// {len(leftover)} unmatched PHP chests:", file=sys.stderr)
        for c in leftover:
            print(f"//   0x{c['addr']:04X} '{c['name']}' (id {c['id']}, vi {c['vi_name']})", file=sys.stderr)


if __name__ == "__main__":
    main()
