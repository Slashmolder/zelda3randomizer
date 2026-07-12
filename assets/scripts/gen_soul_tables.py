#!/usr/bin/env python3
"""gen_soul_tables.py — emit src/rando/soul_tables.h for the enemy/boss souls
feature (openspec/changes/archive/2026-07-07-add-enemy-souls).

The soul catalog is curated HERE (this script is the spec for the data, per the
claim-grounding discipline): boss souls, enemy-family grouping over the
randomizable species, and boss part->parent mappings. The script parses
src/rando/shuffle_enemies.c's kEnemyTable block (the authoritative randomizable
species list, itself generated from Enemizer) and asserts the family map covers
it exactly — a species added to kEnemyTable without a family assignment fails
this script, not silently at runtime.

Output src/rando/soul_tables.h is CHECKED IN (structural sprite-id facts, no
ROM data). Run:  python assets/scripts/gen_soul_tables.py [--check]
--check verifies the committed header matches (CI-friendly) without writing.

It also prints the item_registry.yaml fragment for the soul items to stdout on
--print-registry, for manual append (registry is append-only, hand-owned).
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent.parent
ENEMY_TABLE_SRC = REPO / "src" / "rando" / "shuffle_enemies.c"
OUT_HEADER = REPO / "src" / "rando" / "soul_tables.h"

# --------------------------------------------------------------------------
# Boss souls. Order of the first 11 mirrors the kBoss_* pool enum in
# shuffle_boss.c (ArmosKnights..Trinexx, Agahnim2 collapsed onto Agahnim);
# Ganon is appended last (not part of the boss-shuffle pool).
# Registry item ids are kSoulItemBase + index (contiguous, asserted in C).
BOSS_SOULS = [
    # (soul token,        primary sprite ids)
    ("ArmosKnights",      [0x53]),
    ("Lanmolas",          [0x54]),
    ("Moldorm",           [0x09]),
    ("Agahnim",           [0x7A]),        # covers Castle Tower AND GT Agahnim 2
    ("HelmasaurKing",     [0x92]),
    ("Arrghus",           [0x8C]),
    ("Mothula",           [0x88]),
    ("Blind",             [0xCE]),
    ("Kholdstare",        [0xA2]),
    ("Vitreous",          [0xBD]),
    ("Trinexx",           [0xCB]),
    ("Ganon",             [0xD6, 0xD7]),  # 0xD7 dispatches to Sprite_D6_Ganon
]

# kBoss_* pool index -> soul index. Slots 0..10 are identity; slot 11
# (kBoss_Agahnim2) maps to the single Agahnim soul (index 3).
BOSS_POOL_TO_SOUL = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 3]

# Boss "parts": sprite ids that belong to a boss and must suppress/allow with
# it. ROOM-DATA secondaries (Trinexx arms 0xCC/0xCD, Kholdstare shell 0xA3 —
# see kBossSecondarySpriteInfo in shuffle_boss.c) REQUIRE these entries;
# AI-spawned children (Mothula beam, Arrghus puffs, Vitreous eyes, cutscene
# Agahnim) are parent-gated at spawn time but mapped anyway as
# belt-and-suspenders. Deliberately EXCLUDED shared-use hazards that other
# rooms/cutscenes spawn independently of any boss: 0xA4 FallingIce (Ice Palace
# room hazard), 0xBF Lightning.
BOSS_PARTS = {
    0x89: "Mothula",       # Mothula beam
    0x8D: "Arrghus",       # Arrghus puff
    0xA3: "Kholdstare",    # Kholdstare shell (room-data secondary)
    0xBE: "Vitreous",      # Vitreous eye
    0xCC: "Trinexx",       # Trinexx arm (room-data secondary)
    0xCD: "Trinexx",       # Trinexx arm (room-data secondary)
    0xC1: "Agahnim",       # cutscene Agahnim
}

# --------------------------------------------------------------------------
# Enemy families: every ESF_RANDOMIZABLE species in kEnemyTable must appear in
# exactly one family. Family token becomes ITEM_Soul_<token>. Grouping merges
# behavior/palette variants only (soldier ranks, Bari colors, ...).
ENEMY_FAMILIES = [
    # (family token,      display-ish name,        species ids)
    ("Raven",             [0x00]),
    ("Vulture",           [0x01]),
    ("Octorok",           [0x08, 0x0A]),
    ("Buzzblob",          [0x0D]),
    ("Snapdragon",        [0x0E]),
    ("Hinox",             [0x11]),
    ("Moblin",            [0x12]),
    ("MiniHelmasaur",     [0x13]),
    ("BushHoarder",       [0x17]),
    ("MiniMoldorm",       [0x18]),
    ("Poe",               [0x19]),
    ("Ropa",              [0x22]),
    ("Bari",              [0x23, 0x24]),
    ("HardhatBeetle",     [0x26]),
    ("Soldier",           [0x41, 0x42, 0x43, 0x44, 0x45, 0x46,
                           0x47, 0x48, 0x49, 0x4A, 0x4B]),
    ("Geldman",           [0x4C]),
    ("Popo",              [0x4E, 0x4F]),
    ("Armos",             [0x51]),
    ("WalkingZora",       [0x56]),
    ("Crab",              [0x58]),
    ("BallChainTrooper",  [0x6A]),
    ("Rat",               [0x6D]),
    ("Rope",              [0x6E]),
    ("Keese",             [0x6F]),
    ("Leever",            [0x71]),
    ("Eyegore",           [0x83, 0x84]),
    ("Gibdo",             [0x8B]),
    ("Pengator",          [0x99]),
    ("Wizzrobe",          [0x9B]),
    ("Zazak",             [0xA5, 0xA6]),
    ("Stalfos",           [0xA7]),
    ("Pikit",             [0xAA]),
    ("Gibo",              [0xC3]),
    ("Tektite",           [0xC9]),
]


def enemy_soul_by_species() -> dict[int, str]:
    """species id -> enemy-family soul token (enemy families only; boss souls
    are handled by the CanKill<Boss> macros, not per-species terms). Shared by
    gen_enemy_drop_tables.py / gen_enemy_check_tables.py (task 1.6 soul terms)
    and gen_soul_room_tables.py so the predicate emitters and the runtime
    suppression can't disagree about which species carries which soul."""
    return {sid: tok for tok, ids in ENEMY_FAMILIES for sid in ids}


def parse_randomizable_species() -> dict[int, str]:
    """Parse kEnemyTable's ESF_RANDOMIZABLE entries out of shuffle_enemies.c."""
    text = ENEMY_TABLE_SRC.read_text(encoding="utf-8")
    m = re.search(r"kEnemyTable\[ES_TABLE_LEN\] = \{(.*?)\n\};", text, re.S)
    if not m:
        sys.exit("gen_soul_tables: kEnemyTable block not found in shuffle_enemies.c")
    species = {}
    for mm in re.finditer(
            r"\[0x([0-9A-Fa-f]{2})\]\s*=\s*\{\s*\(ESF_RANDOMIZABLE[^\n]*//\s*(.+)$",
            m.group(1), re.M):
        species[int(mm.group(1), 16)] = mm.group(2).strip()
    if not species:
        sys.exit("gen_soul_tables: parsed zero randomizable species (regex drift?)")
    return species


def build_and_check():
    species = parse_randomizable_species()
    # These species are deliberately not enemy-shuffle replacements, but they
    # are finite authored all-enemy checks and therefore still need their
    # family's soul gate and runtime suppression behavior.
    check_only_species = {0x84}  # Red Eyegore

    fam_by_species: dict[int, str] = {}
    for token, ids in ENEMY_FAMILIES:
        for sid in ids:
            if sid in fam_by_species:
                sys.exit(f"gen_soul_tables: species 0x{sid:02X} in two families")
            fam_by_species[sid] = token

    # Completeness both directions vs kEnemyTable.
    missing = sorted(set(species) - set(fam_by_species))
    extra = sorted(set(fam_by_species) - set(species) - check_only_species)
    missing_check_only = sorted(check_only_species - set(fam_by_species))
    if missing:
        sys.exit("gen_soul_tables: kEnemyTable species with no family: " +
                 ", ".join(f"0x{s:02X} ({species[s]})" for s in missing))
    if extra:
        sys.exit("gen_soul_tables: family species not in kEnemyTable: " +
                 ", ".join(f"0x{s:02X}" for s in extra))
    if missing_check_only:
        sys.exit("gen_soul_tables: check-only species with no family: " +
                 ", ".join(f"0x{s:02X}" for s in missing_check_only))

    boss_species = {sid: tok for tok, ids in BOSS_SOULS for sid in ids}
    boss_tokens = {tok for tok, _ in BOSS_SOULS}
    for sid, tok in BOSS_PARTS.items():
        if tok not in boss_tokens:
            sys.exit(f"gen_soul_tables: part 0x{sid:02X} maps to unknown boss {tok}")
        if sid in boss_species or sid in fam_by_species:
            sys.exit(f"gen_soul_tables: part 0x{sid:02X} collides with a primary")
    overlap = set(boss_species) & set(fam_by_species)
    if overlap:
        sys.exit("gen_soul_tables: boss/family species overlap: " +
                 ", ".join(f"0x{s:02X}" for s in sorted(overlap)))

    souls = [tok for tok, _ in BOSS_SOULS] + [tok for tok, _ in ENEMY_FAMILIES]
    if len(souls) != len(set(souls)):
        sys.exit("gen_soul_tables: duplicate soul token")
    n_boss = len(BOSS_SOULS)

    soul_index = {tok: i for i, tok in enumerate(souls)}
    sprite_to_soul = [0xFF] * 256
    for tok, ids in BOSS_SOULS:
        for sid in ids:
            sprite_to_soul[sid] = soul_index[tok]
    for sid, tok in BOSS_PARTS.items():
        sprite_to_soul[sid] = soul_index[tok]
    for tok, ids in ENEMY_FAMILIES:
        for sid in ids:
            sprite_to_soul[sid] = soul_index[tok]

    return souls, n_boss, sprite_to_soul


def emit_header(souls, n_boss, sprite_to_soul) -> str:
    L = []
    L.append("// soul_tables.h - enemy/boss souls catalog (add-enemy-souls).")
    L.append("// GENERATED by assets/scripts/gen_soul_tables.py - edit the SCRIPT, not")
    L.append("// this file, then re-run it (checked in: structural sprite-id facts only).")
    L.append("// Soul item registry ids are kSoulItemBase + kSoul_* (contiguous block in")
    L.append("// item_registry.yaml; _Static_asserts in souls.c pin both ends).")
    L.append("#pragma once")
    L.append("#include \"../types.h\"")
    L.append("")
    L.append("enum {")
    for i, tok in enumerate(souls):
        L.append(f"  kSoul_{tok} = {i},")
    L.append(f"  kSoulCount = {len(souls)},")
    L.append(f"  kSoulBossCount = {n_boss},  // souls [0..{n_boss-1}] are boss souls")
    L.append("};")
    L.append("")
    L.append("// kBoss_* pool index (shuffle_boss.c) -> soul index. Agahnim2 -> Agahnim.")
    L.append("extern const uint8 kBossPoolSoul[12];")
    L.append("// Final sprite type -> soul index (0xFF = no soul; species always spawns).")
    L.append("// Includes boss parts mapped to their parent's soul.")
    L.append("extern const uint8 kSoulForSprite[256];")
    L.append("// Soul index -> registry item name (display names, spoiler/tracker).")
    L.append("extern const char *const kSoulNames[kSoulCount];")
    L.append("")
    L.append("// Table definitions — exactly one TU (souls.c) defines SOUL_TABLES_IMPL.")
    L.append("#ifdef SOUL_TABLES_IMPL")
    L.append("const uint8 kBossPoolSoul[12] = {")
    L.append("  " + ", ".join(str(i) for i in BOSS_POOL_TO_SOUL) + ",")
    L.append("};")
    L.append("const uint8 kSoulForSprite[256] = {")
    for i in range(0, 256, 16):
        chunk = sprite_to_soul[i:i + 16]
        L.append("  " + ", ".join(f"0x{v:02x}" for v in chunk) + f",  // 0x{i:02X}")
    L.append("};")
    L.append("const char *const kSoulNames[kSoulCount] = {")
    for tok in souls:
        L.append(f"  \"Soul_{tok}\",")
    L.append("};")
    L.append("#endif  // SOUL_TABLES_IMPL")
    L.append("")
    return "\n".join(L)


def emit_registry_fragment(souls, base_id: int) -> str:
    L = []
    L.append("  # ===========================================================================")
    L.append(f"  # Enemy/boss souls (IDs {base_id}..{base_id + len(souls) - 1}) - add-enemy-souls.")
    L.append("  # CONTIGUOUS block, order mirrors kSoul_* in soul_tables.h (generated by")
    L.append("  # assets/scripts/gen_soul_tables.py). Item id = kSoulItemBase + kSoul_<X>.")
    L.append("  # ===========================================================================")
    for i, tok in enumerate(souls):
        L.append(f"  - id: {base_id + i}")
        L.append(f"    name: Soul_{tok}")
        L.append("    category: soul")
        L.append("    dispatch: direct_soul")
    return "\n".join(L)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true",
                    help="verify committed header matches; write nothing")
    ap.add_argument("--print-registry", action="store_true",
                    help="print item_registry.yaml fragment (base id 150)")
    ap.add_argument("--registry-base", type=int, default=150)
    args = ap.parse_args()

    souls, n_boss, sprite_to_soul = build_and_check()
    header = emit_header(souls, n_boss, sprite_to_soul)

    if args.print_registry:
        print(emit_registry_fragment(souls, args.registry_base))
        return

    if args.check:
        current = OUT_HEADER.read_text(encoding="utf-8") if OUT_HEADER.exists() else ""
        if current != header:
            sys.exit("gen_soul_tables: src/rando/soul_tables.h is stale - re-run "
                     "python assets/scripts/gen_soul_tables.py")
        print(f"gen_soul_tables: OK ({len(souls)} souls, {n_boss} boss)")
        return

    with open(OUT_HEADER, "w", encoding="utf-8", newline="\n") as f:
        f.write(header)
    print(f"gen_soul_tables: wrote {OUT_HEADER} ({len(souls)} souls: "
          f"{n_boss} boss + {len(souls) - n_boss} enemy families)")


if __name__ == "__main__":
    main()
