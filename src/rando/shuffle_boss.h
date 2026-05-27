// shuffle_boss.h — Phase B Slice 7 (add-rando-shuffles-and-minigames) scaffold.
//
// Boss-room reassignment. Randomizes which boss sprite appears in each
// dungeon-boss room while preserving the dungeon's prize/reward binding
// (EP's pendant stays at EP regardless of which boss is there). Goal-
// required bosses (Agahnim 1, Agahnim 2, Ganon) are pinned to their
// canonical slots and never appear elsewhere in the pool.
//
// Status: STUB. Wiring landing place; full assignment algorithm + per-
// site sprite-handler instrumentation is Phase B follow-up work.
//
// ALTTPR upstream: app/Boss.php (boss-pool definition + assignment rules).

#ifndef ZELDA3_RANDO_SHUFFLE_BOSS_H_
#define ZELDA3_RANDO_SHUFFLE_BOSS_H_

#include "../types.h"
#include "rando_placement.h"

// One-shot generation entry. Builds the per-dungeon boss assignment
// based on the active settings (boss_shuffle setting — currently
// reserved, not exposed as a CSV key). Deterministic from
// (settings, seed_u64).
//
// `out_assignment` is sized to one byte per dungeon-id (currently 13);
// each entry is the sprite_id (or boss-pool index) assigned to that
// dungeon's boss room. The mapping table is consumed by the per-site
// sprite handlers (TBD) that read the active boss assignment when
// spawning a boss.
//
// STUB: returns identity (each dungeon keeps its vanilla boss).
//
// Returns true on success.
bool BossShuffle_Generate(const RandoSettings *settings,
                          uint64 seed_u64,
                          uint8 out_assignment[16]);

// Returns the boss-pool index for `dungeon_id` from the currently
// installed assignment. Returns 0xFF when no assignment is active
// (boss shuffle off, or no slot loaded).
uint8 BossShuffle_GetForDungeon(uint8 dungeon_id);

// Per-site instrumentation entry — Phase B §65.
//
// Given the vanilla boss sprite type the room data wants to spawn
// (e.g. 0x53 ArmosKnight for EP's boss room), returns the sprite
// type that should actually spawn under the active shuffle.
//
// Returns `vanilla_sprite_type` unchanged when:
//   - the sprite type is not a recognized boss
//   - boss-shuffle is off (assignment active but identity)
//
// Vanilla boss sprite IDs: 0x09 Moldorm, 0x53 ArmosKnights,
// 0x54 Lanmolas, 0x7A Agahnim (pinned), 0x88 Mothula,
// 0x8C Arrghus, 0x92 HelmasaurKing, 0xA3 Kholdstare,
// 0xB6 Agahnim2 (pinned), 0xBD Vitreous, 0xCB Trinexx, 0xCE Blind.
uint8 BossShuffle_RemapSpriteType(uint8 vanilla_sprite_type);

#endif  // ZELDA3_RANDO_SHUFFLE_BOSS_H_
