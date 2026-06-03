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
// Returns true on success. Also INSTALLS the assignment into module-global
// runtime state (consumed by BossShuffle_RemapSpriteType at sprite-load) and
// marks the assignment active.
bool BossShuffle_Generate(const RandoSettings *settings,
                          uint64 seed_u64,
                          uint8 out_assignment[16]);

// Pure assignment computation — NO global side effects. Deterministic from
// (settings, seed_u64). Writes the dungeon-id → boss-pool-index table into
// `out_assignment[16]` (0xFF for HCE/unused slots; Agahnim 1/2 pinned at
// slots 4/12). Used by the spoiler writer (which must not perturb the runtime
// install) and internally by BossShuffle_Generate. When boss_shuffle is off,
// writes the identity (vanilla) assignment.
void BossShuffle_ComputeAssignment(const RandoSettings *settings,
                                   uint64 seed_u64,
                                   uint8 out_assignment[16]);

// Clear the installed assignment (slot-teardown). After this,
// BossShuffle_RemapSpriteType/_ShouldSuppressSecondary are pure passthroughs
// (vanilla bosses) until the next BossShuffle_Generate. Pairs with
// Rando_DeactivateSlot so a shuffle slot's assignment can't leak into the
// next (non-shuffle) slot or into vanilla play.
void BossShuffle_Deactivate(void);

// Human-readable names for the spoiler (boss assignments). `pool_index` is a
// kBoss_* value (0..11); `dungeon_id` is 0..12. Returns "?" out of range.
const char *BossShuffle_BossName(uint8 pool_index);
const char *BossShuffle_DungeonName(uint8 dungeon_id);

// Self-check (invoked from --rando-selftest): asserts determinism, the
// pinned-Agahnim invariant, the off→identity contract, and that the shuffled
// assignment is a permutation of the 10-boss pool over the 10 shuffleable
// dungeons. exit(2) on failure.
void BossShuffle_SelfCheck(void);

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
// 0x54 Lanmolas, 0x7A Agahnim 1/2 (both pinned), 0x88 Mothula,
// 0x8C Arrghus, 0x92 HelmasaurKing, 0xA2 Kholdstare, 0xBD Vitreous,
// 0xCB Trinexx, 0xCE Blind.
uint8 BossShuffle_RemapSpriteType(uint8 vanilla_sprite_type);

// Per-site instrumentation — audit §65 M1 follow-up.
//
// Returns true if `vanilla_sprite_type` is a room-data secondary segment
// of a boss whose primary has been remapped to something else (i.e., the
// segment would spawn as an orphan with no kill-logic context). The
// caller is expected to skip the sprite-spawn.
//
// Returns false when:
//   - boss-shuffle is inactive (g_boss_assignment_active = false)
//   - the sprite isn't a known secondary segment
//   - the parent dungeon still has its vanilla boss (identity remap —
//     segment is wanted)
//
// Known room-data secondaries (sprites that appear alongside their
// primary in dungeon room data, NOT spawned by runtime boss logic):
//   0xCC Trinexx left arm  → TR  (dungeon 11), parent kBoss_Trinexx
//   0xCD Trinexx right arm → TR  (dungeon 11), parent kBoss_Trinexx
//   0xA3 KholdstareShell   → IP  (dungeon 9),  parent kBoss_Kholdstare
//
// Other bosses' segments (Mothula beams 0x89, Arrghi puffs 0x8D,
// VitreousEye 0xBE, BlindHead) are spawned at runtime by the primary
// boss's logic and so naturally come along with the remapped boss
// instead of the orphaned vanilla one — no suppression needed.
bool BossShuffle_ShouldSuppressSecondary(uint8 vanilla_sprite_type);

#endif  // ZELDA3_RANDO_SHUFFLE_BOSS_H_
