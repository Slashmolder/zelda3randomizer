// shuffle_enemies.c — add-rando-enemy-shuffle (sprite-type substitution).
//
// See shuffle_enemies.h for the contract. Determinism: a dedicated xoshiro256**
// stream forked off the seed (salt "ENEMYSHF") so the fill RNG + the boss/drop
// streams are unperturbed. The substitution is computed per-room/per-area at
// LOAD time (not a stored permutation) because the candidate pool depends on
// which GFX sheets are LIVE-loaded for that room/area — load-history dependent
// and unknown at generation time. A per-room/per-area RNG keyed by
// (seed XOR salt, room-or-area-key, slot) makes every pick deterministic and
// reproducible without storing anything in the slot sidecar (regenerated from
// (seed, settings) at activation, exactly like boss/drop shuffle).
//
// ANTI-CRASH (design.md D3, non-negotiable): every candidate's required GFX
// sheet(s) must be a subset of the room/area's actually-loaded sheets — read
// LIVE from sprite_gfx_subset_0..3 (g_ram 0xC2FC..0xC2FF), which already
// reflect the kSpriteTilesets row AND the 0-entry inheritance (a 0 subgroup
// entry leaves the previously-loaded sheet in place — Gfx_LoadSpritesInner,
// load_gfx.c:627-640). Reading the live subset values sidesteps the
// 0-inheritance hazard entirely (we never consult the static kSpriteTilesets
// row). The required-sheet sets come from Enemizer's per-sprite AddSubgroupN
// lists (SpriteRequirement.cs).
//
// BEATABILITY (design.md D7): the constraint table is the SOLE enforcer (logic
// models no per-room kill-clear). Conservative first pass: unknown-safety
// sprites are do_not_randomize; bosses + mini-bosses (incl. GT mini-bosses
// 0x09/0x53/0x54) are excluded; key-safety is over-approximated — EVERY
// dungeon replacement must be killable && !cannot_have_key, and dungeon rooms
// in the shutter/immovable/flying exclusion lists are further restricted. This
// over-restricts variety in favor of never shipping an unbeatable seed; widen
// once playtested.

#include "shuffle_enemies.h"
#include "rando_settings.h"
#include "rando_rng.h"
#include "../features.h"   // kRam_EnemyShuffleVanPos2 (reserved-block allocation)
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// Live game-state RAM (defined in zelda_rtl.c). We read sprite_gfx_subset_0..3
// (0xC2FC..0xC2FF) directly to learn the room/area's actually-loaded sheets,
// avoiding variables.h's macro wall in this TU (same pattern as
// rando_placement.c). The fork's g_ram is 131072 bytes; 0xC2FF is well inside.
extern uint8 g_ram[131072];

// ---------------------------------------------------------------------------
// Constraint table — per sprite-type byte (0x00..0xF2). Authored conservatively
// by cross-checking Enemizer SpriteRequirement.cs against the fork's
// Sprite_HEX_* ids (the Enemizer SpriteId IS the SNES sprite type byte, so the
// mapping is the identity). Flags:
//   ESF_RANDOMIZABLE — a safe enemy we may substitute IN (and replace OUT).
//                      Absent ⇒ do_not_randomize (NPC/object/overlord/boss/
//                      marker/unknown). NOTHING outside this set is ever a
//                      candidate or a replaced source.
//   ESF_KILLABLE     — can be killed by the player's normal kit (sword/bombs/
//                      etc.). Required for shutter/kill-clear/key rooms.
//   ESF_CANNOT_KEY   — must NOT carry a small key (Enemizer CannotHaveKey).
//                      INDEPENDENT of killable (Keese/Buzzblob/Geldman are
//                      killable but key-banned). A key-room replacement needs
//                      killable && !cannot_key.
//   ESF_WATER        — water-capable (Enemizer IsWaterSprite). Water-only rooms
//                      draw from these.
//   ESF_NEVER_DUNGEON / ESF_NEVER_OVERWORLD — directional bans (Enemizer
//                      NeverUseDungeon / NeverUseOverworld).
//   ESF_FLYING       — flying sprite; excluded from the per-room flying-exclude
//                      list (DontUseFlyingSprites).
// `sheets[]` lists the GFX sheet ids the sprite needs (union of Enemizer's
// AddSubgroupN constraints); ESF_SHEETS_COUNT terminator implied by trailing 0
// entries (sheet id 0 is never a real requirement here). A candidate is
// in-sheet only when EVERY listed sheet appears in the live 4-slot set.

enum {
  ESF_RANDOMIZABLE   = 1u << 0,
  ESF_KILLABLE       = 1u << 1,
  ESF_CANNOT_KEY     = 1u << 2,
  ESF_WATER          = 1u << 3,
  ESF_NEVER_DUNGEON  = 1u << 4,
  ESF_NEVER_OVERWORLD= 1u << 5,
  ESF_FLYING         = 1u << 6,
};

#define ES_MAX_SHEETS 3

typedef struct EnemyConstraint {
  uint8 flags;
  uint8 sheets[ES_MAX_SHEETS];  // required sheet ids; 0 = unused slot
} EnemyConstraint;

// Highest sprite-type byte we model (Ganon's bank ends at 0xF2 control markers;
// real enemies top out well below). 256 keeps indexing trivial + bounds-safe.
#define ES_TABLE_LEN 256

// Helper macros for terse, auditable initializers. Each enemy lists its
// Enemizer subgroup sheet requirements as the union of AddSubgroupN ids. When
// Enemizer lists MULTIPLE acceptable ids for one subgroup position (e.g.
// AddSubgroup2(28, 36)), we record only the FIRST/most-common id — a stricter
// (sound) requirement: the candidate is admitted only when that exact sheet is
// loaded. Over-restriction is safe (fewer picks); under-restriction crashes.
#define E_RAND(killflags, ...) { (ESF_RANDOMIZABLE | killflags), { __VA_ARGS__ } }

// The table. Everything not explicitly RANDOMIZABLE is do_not_randomize (the
// zero-initialized default), which covers NPCs, objects, overlords, bosses,
// mini-bosses, markers, absorbables, and every unknown/unsafe id — the
// conservative default the spec demands.
//
// Sheet ids are the Enemizer AddSubgroupN constants (= the fork's Decomp_spr
// sheet ids). Cross-checked entry-by-entry against SpriteRequirement.cs.
static const EnemyConstraint kEnemyTable[ES_TABLE_LEN] = {
  // --- killable melee/common enemies (safe in key & shutter rooms) ---
  [0x08] = E_RAND(ESF_KILLABLE, 12),                     // Octorok (one-way) — Enemizer sub2=[12,24] is OR-within-slot-2; record one (sheets_loaded is AND, so listing both wrongly required BOTH)
  [0x0A] = E_RAND(ESF_KILLABLE, 12),                     // Octorok (four-way)
  [0x0E] = E_RAND(ESF_KILLABLE, 22, 23),                 // Snapdragon
  [0x11] = E_RAND(ESF_KILLABLE, 22),                     // Hinox
  [0x12] = E_RAND(ESF_KILLABLE, 23),                     // Moblin
  [0x13] = E_RAND(ESF_KILLABLE, 30),                     // Mini-Helmasaur
  [0x18] = E_RAND(ESF_KILLABLE, 30),                     // Mini-Moldorm
  [0x22] = E_RAND(ESF_KILLABLE, 22),                     // Ropa
  [0x41] = E_RAND(ESF_KILLABLE, 73),                     // Blue sword soldier
  [0x42] = E_RAND(ESF_KILLABLE, 73),                     // Green sword soldier
  [0x43] = E_RAND(ESF_KILLABLE, 73),                     // Red spear soldier
  [0x45] = E_RAND(ESF_KILLABLE, 73),                     // Green spear soldier
  [0x4E] = E_RAND(ESF_KILLABLE, 44),                     // Popo
  [0x4F] = E_RAND(ESF_KILLABLE, 44),                     // Popo2
  [0x51] = E_RAND(ESF_KILLABLE, 16),                     // Armos (statue)
  [0x58] = E_RAND(ESF_KILLABLE, 12),                     // Crab
  [0x6D] = E_RAND(ESF_KILLABLE, 28),                     // Rat
  [0x6E] = E_RAND(ESF_KILLABLE, 28),                     // Rope
  [0x71] = E_RAND(ESF_KILLABLE, 47),                     // Leever
  [0x99] = E_RAND(ESF_KILLABLE, 38),                     // Pengator
  [0xA5] = E_RAND(ESF_KILLABLE, 40),                     // Blue Zazak
  [0xA6] = E_RAND(ESF_KILLABLE, 40),                     // Red Zazak
  [0xA7] = E_RAND(ESF_KILLABLE, 31),                     // Stalfos
  [0xC3] = E_RAND(ESF_KILLABLE, 40),                     // Gibo (floating blob)
  [0xC9] = E_RAND(ESF_KILLABLE, 16),                     // Tektite
  [0x8B] = E_RAND(ESF_KILLABLE, 35),                     // Gibdo
  [0x83] = E_RAND(ESF_KILLABLE, 46),                     // Green Eyegore

  // --- killable but KEY-BANNED (CannotHaveKey, independent of killable) ---
  [0x0D] = E_RAND(ESF_KILLABLE | ESF_CANNOT_KEY, 17),    // Buzzblob
  [0x4C] = E_RAND(ESF_KILLABLE | ESF_CANNOT_KEY, 18),    // Geldman
  [0x6F] = E_RAND(ESF_KILLABLE | ESF_CANNOT_KEY, 28),    // Keese
  [0xAA] = E_RAND(ESF_KILLABLE | ESF_CANNOT_KEY, 27),    // Pikit (shield-eater)

  // --- water-capable (water-only rooms draw from these; CannotHaveKey) ---
  // Walking Zora needs sheets in TWO slots: sub2=12 (head) AND sub3=68 (body).
  // Listing only 12 let the picker spawn a Zora wherever 12 was loaded but 68 was
  // not (e.g. a Buzzblob area with slot 3 == 17), drawing the body from the wrong
  // sheet → "hybrid zora+buzzblob" (owner F12, area 0x3C). Both sheets required.
  [0x56] = E_RAND(ESF_KILLABLE | ESF_WATER | ESF_CANNOT_KEY, 12, 68),  // Walking Zora

  // --- flying (excluded from DontUseFlyingSprites rooms; CannotHaveKey) ---
  [0x00] = E_RAND(ESF_CANNOT_KEY | ESF_FLYING, 17),      // Raven
  [0x01] = E_RAND(ESF_CANNOT_KEY | ESF_FLYING, 18),      // Vulture
  [0x19] = E_RAND(ESF_CANNOT_KEY | ESF_FLYING, 14),      // Poe

  // --- non-killable-by-default / hazard-class enemies. RANDOMIZABLE (safe to
  //     swap IN where their sheet is loaded) but NOT killable ⇒ never chosen
  //     for a key/shutter room. Conservative: omit anything Enemizer leaves
  //     ambiguous. ---
  [0x26] = E_RAND(0, 30),                                // Hardhat Beetle (knockback only)
  [0x9B] = E_RAND(0, 37),                                // Wizzrobe (bomb-immune)
};

// ---------------------------------------------------------------------------
// Per-room exclusion lists (ported from Enemizer; NOT derivable from room
// bytes). Room ids are standard ALTTP dungeon_room_index values.
//
// kFlyingExcludeRooms — rooms where a flying replacement strands/laggs
//   (DontUseFlyingSprites). kImmovableExcludeRooms — rooms where an immovable /
//   slow replacement can block a door/switch (DontUseImmovableSpritesRooms,
//   ~60 rooms). In BOTH lists we conservatively restrict the candidate pool to
//   killable ground enemies (no flying, and — since the MVP has no reliable
//   "immovable" flag — we simply require killable, which all our ground
//   candidates are). The lists are also used to reject obviously-unsafe rooms
//   for the corresponding classes.

static const uint16 kFlyingExcludeRooms[] = {
  210,  // Misery Mire 02 Wizzrobes
  268,  // Mimic Cave
};
#define kFlyingExcludeRoomsCount (sizeof(kFlyingExcludeRooms)/sizeof(kFlyingExcludeRooms[0]))

// Hard-exclude rooms: never substitute ANY sprite here (Mimic Cave + Agahnim's
// Tower final bridge — Enemizer's most-restricted rooms). Cheap and safe.
static const uint16 kHardExcludeRooms[] = {
  268,  // Mimic Cave (R268_MimicCave)
  64,   // Agahnim's Tower final bridge (R64_AgahnimsTower_FinalBridgeRoom)
};
#define kHardExcludeRoomsCount (sizeof(kHardExcludeRooms)/sizeof(kHardExcludeRooms[0]))

// Shutter / kill-clear rooms (Enemizer NeedKillable_doors via Room.IsShutterRoom)
// + the immovable-sprite rooms: in either, every replacement MUST be killable.
// The MVP enforces killable for ALL dungeon rooms (see EnemyShuffle_PickDungeon),
// so this list is currently advisory; kept for when the dungeon pool widens past
// "killable-only". Ported subset of DontUseImmovableSpritesRooms (the rooms most
// likely to softlock if blocked); a conservative restriction, not exhaustive.
static const uint16 kKillableRequiredRooms[] = {
  11, 25, 30, 38, 39, 54, 63, 64, 66, 70, 73, 75, 78, 85, 87, 95,
  101, 106, 116, 118, 125, 127, 131, 132, 133, 140, 141, 146, 149,
  152, 155, 156, 157, 158, 160, 170, 175, 179, 186, 187, 188, 198,
  203, 206, 208, 210, 213, 216, 220,
};
#define kKillableRequiredRoomsCount (sizeof(kKillableRequiredRooms)/sizeof(kKillableRequiredRooms[0]))

// ---------------------------------------------------------------------------
// Module state.
static bool g_enemy_shuffle_active = false;  // installed AND enabled
static uint64 g_enemy_shuffle_seed = 0;      // salted per-seed base

// Enemy-shuffle RNG salt: a recognizable constant XORed into the seed so this
// stream is independent of boss (0xB055A11F...) / drop (0xD0DDA1FF...) / prize
// streams. "ENEMYSHF" leetspeak-ish.
#define kEnemyShuffleSalt 0xE1E10F5Full

// ---------------------------------------------------------------------------
// Helpers.

static bool table_is_randomizable(uint8 type) {
  return (kEnemyTable[type].flags & ESF_RANDOMIZABLE) != 0;
}

// True iff every required sheet of `type` is present in the live 4-slot set.
static bool sheets_loaded(uint8 type, const uint8 live[4]) {
  const EnemyConstraint *c = &kEnemyTable[type];
  for (int i = 0; i < ES_MAX_SHEETS; i++) {
    uint8 need = c->sheets[i];
    if (need == 0) continue;  // unused slot
    bool found = false;
    for (int j = 0; j < 4; j++) {
      if (live[j] == need) { found = true; break; }
    }
    if (!found) return false;
  }
  return true;
}

static bool room_in_list(uint16 room, const uint16 *list, uint32 n) {
  for (uint32 i = 0; i < n; i++) if (list[i] == room) return true;
  return false;
}

// Read the LIVE loaded sprite sheet set (g_ram 0xC2FC..0xC2FF). These already
// reflect the 0-inheritance (a 0 subgroup entry retained the prior sheet), so
// the constraint check is sound without consulting the static kSpriteTilesets
// row. (Direct g_ram read keeps this TU free of variables.h's macro wall.)
static void read_live_sheets(uint8 out[4]) {
  out[0] = g_ram[0xC2FC];
  out[1] = g_ram[0xC2FD];
  out[2] = g_ram[0xC2FE];
  out[3] = g_ram[0xC2FF];
}

// Core pick. `key` keys the per-entry RNG (room/area id mixed with slot).
// `require_killable` / `require_key_capable` / `require_water` / `forbid_flying`
// constrain the candidate pool per the room/area context. Returns the chosen
// replacement type, or `vanilla_type` unchanged when no valid candidate exists
// (always a safe no-op).
static uint8 pick_replacement(uint64 key, uint8 vanilla_type,
                              const uint8 live[4],
                              bool require_killable,
                              bool require_key_capable,
                              bool require_water,
                              bool forbid_flying,
                              bool never_dungeon_ok,
                              bool never_overworld_ok) {
  // Build the candidate list deterministically (table order is fixed), filtered
  // by the context constraints + in-sheet test. Cap is generous.
  uint8 cands[ES_TABLE_LEN];
  uint32 ncand = 0;
  for (uint32 t = 0; t < ES_TABLE_LEN; t++) {
    const EnemyConstraint *c = &kEnemyTable[t];
    if (!(c->flags & ESF_RANDOMIZABLE)) continue;
    if (require_killable && !(c->flags & ESF_KILLABLE)) continue;
    if (require_key_capable && (c->flags & ESF_CANNOT_KEY)) continue;
    if (require_water && !(c->flags & ESF_WATER)) continue;
    if (forbid_flying && (c->flags & ESF_FLYING)) continue;
    // Directional bans: never_*_ok==false means we're in that context and must
    // skip sprites banned there.
    if (!never_dungeon_ok && (c->flags & ESF_NEVER_DUNGEON)) continue;
    if (!never_overworld_ok && (c->flags & ESF_NEVER_OVERWORLD)) continue;
    if (!sheets_loaded((uint8)t, live)) continue;
    cands[ncand++] = (uint8)t;
  }
  if (ncand == 0) return vanilla_type;  // no safe swap → leave vanilla

  RandoRng rng;
  Rng_SeedFromU64(&rng, g_enemy_shuffle_seed ^ key);
  uint32 idx = Rng_NextRange(&rng, ncand);
  return cands[idx];
}

// ===========================================================================
// Sprite-group SHEET RESHUFFLE (design.md D4 — the variety unlock).
//
// The MVP picker (EnemyShuffle_Pick*) only chooses among enemies whose GFX sheet
// is ALREADY loaded for the room/area, so swaps are same-family. The reshuffle
// widens that pool by RE-ASSIGNING which sprite GFX sheet loads into subgroup
// SLOT 2 (the "themed enemy" slot) at sheet-load time — so an Octorok room can
// load the Gibdo/Zazak/Pengator/Eyegore sheet and the existing picker gets the
// wider pool for free (it reads the LIVE sprite_gfx_subset_* we rewrite here).
//
// SCOPE (phase 1, conservative — playtest-gated widening per CLAUDE.md): SLOT 2
// ONLY. A room/area is reshuffle-eligible only when slot 2 is provably FREE:
// every present sprite is either a randomizable enemy (the picker substitutes it)
// or a KNOWN type that does NOT need slot 2, and NO overlord is present
// (overlord-spawned sprites bypass the picker, so a slot-2-spawning overlord
// would render garbage). Enemizer's four position whitelists are DISJOINT (a
// sheet id belongs to exactly one slot), so a slot-2 sheet only ever holds slot-2
// enemies — each enemy's tiles land in their canonical VRAM region and the
// picker's "sheet loaded" test stays sound without a position-aware change.
//
// ANTI-GARBAGE: the slot-2 pool is restricted to sheets that EACH self-contain a
// killable + key-capable enemy, so after a swap the picker ALWAYS finds a valid
// substitution for every slot-2 enemy (no vanilla passthrough with a now-missing
// sheet) and dungeon key/shutter rooms stay fillable. The room's vanilla slot-2
// sheet is ALWAYS a candidate (owner constraint: true-random, vanilla-inclusive).
//
// INHERITANCE: kSpriteTilesets rows with a 0 in slot 2 INHERIT the prior room's
// sheet, so a reshuffle could leak into a later room that needs a specific slot-2
// sheet. We track the true VANILLA-resolved slot-2 sheet in a snapshot-safe g_ram
// shadow (kRam_EnemyShuffleVanPos2): eligible rooms reshuffle FROM it; INELIGIBLE
// rooms RESTORE it — so a leaked sheet can never reach a room that pins slot 2.
//
// Determinism: per-(seed, room/area), reproducible for races, regenerated from
// (seed, settings) like the picker. Rides the existing enemy_shuffle activation
// (no separate settings axis — owner decision); off ⇒ this is never called.
// ===========================================================================

// The vanilla-slot-2 inheritance shadow lives at kRam_EnemyShuffleVanPos2
// (features.h reserved block). Snapshot-safe so a Ctrl+F1 restore keeps the
// inheritance chain intact.

// Distinct RNG salt for the sheet choice (independent of the pick salts above).
#define kEnemyShuffleSheetSalt 0x5348454554ull  // "SHEET"

// Set to 1 to emit playtest diagnostics into the reserved g_ram block (read via
// an F12 dump). 0x663 hook-calls, 0x664 slot-2 changed, 0x665 ineligible/restored,
// 0x666 last vanilla slot-2, 0x667 last chosen slot-2. Flip to 0 before merge.
#define ES_RESHUFFLE_DIAG 1

// Slot-2 reshuffle pool — Enemizer PotentialSubset2 restricted to sheets that
// each self-contain a killable, key-capable randomizable enemy in kEnemyTable
// (so a dungeon swap is always fillable + garbage-free):
//   12→Crab/Octorok, 23→Moblin, 28→Rat/Rope, 35→Gibdo, 38→Pengator,
//   40→Zazak/Gibo, 46→Green Eyegore.
static const uint8 kEsSafePos2Pool[] = { 12, 23, 28, 35, 38, 40, 46 };
#define kEsSafePos2PoolCount (sizeof(kEsSafePos2Pool)/sizeof(kEsSafePos2Pool[0]))

// Sprite-type ids in 0x00..0xF2 with NO Enemizer SpriteRequirement entry
// (commented-out / unused / glitch sprites). A room containing one is treated as
// UNKNOWN ⇒ slot 2 ineligible (we can't prove the sprite's sheet need → restore).
static const uint8 kEsUnknownLowTypes[] = {
  0x02, 0x05, 0x07, 0x0C, 0x70, 0x77, 0x79, 0x85, 0x87
};
#define kEsUnknownLowCount (sizeof(kEsUnknownLowTypes)/sizeof(kEsUnknownLowTypes[0]))

// Sprite-type ids whose tiles live in subgroup SLOT 2 (Enemizer sub2 non-empty),
// verified-complete from SpriteRequirement.cs. A NON-randomizable present sprite
// from this set pins slot 2 ⇒ ineligible. (Randomizable members are checked
// first — the picker substitutes them — so listing them here is harmless.)
static const uint8 kEsPos2NeedTypes[] = {
  0x01, 0x08, 0x09, 0x0A, 0x0E, 0x0F, 0x10, 0x12, 0x16, 0x20, 0x2C, 0x36, 0x4B,
  0x4C, 0x50, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x5D, 0x5E, 0x5F, 0x60, 0x62,
  0x6D, 0x6E, 0x6F, 0x78, 0x7A, 0x81, 0x83, 0x84, 0x86, 0x88, 0x89, 0x8B, 0x8C,
  0x8D, 0x8E, 0x90, 0x92, 0x99, 0x9A, 0x9B, 0x9E, 0xA0, 0xA1, 0xA2, 0xA4, 0xA5,
  0xA6, 0xAD, 0xBB, 0xBC, 0xC0, 0xC1, 0xC3, 0xC7, 0xC8, 0xCA, 0xCE, 0xD6, 0xED,
  0xF2
  // 0x16 (Sahasrahla/Aginah, sub2=76) + 0xBC (Drunk-in-the-Inn, sub2=74) were
  // missing from the extraction agent's summary list (fresh-eyes audit HIGH);
  // both pin slot 2 and would otherwise render corrupt. Full set re-derived from
  // SpriteRequirement.cs rows (every sub2-non-empty id ≤0xF2).
};
#define kEsPos2NeedCount (sizeof(kEsPos2NeedTypes)/sizeof(kEsPos2NeedTypes[0]))

// Implemented in sprite.c / overworld.c — the asset-blob room/area sprite-data
// pointer, walked here for the room's TYPE list at sheet-load time (before
// Dungeon_LoadSprites / Overworld_LoadSprites parse it). Forward-declared to keep
// this TU free of the heavy sprite/overworld headers (same spirit as the g_ram
// direct reads). Signatures must match the definitions.
extern const uint8 *Dungeon_GetRoomSpritePtr(uint16 room);
extern const uint8 *GetOverworldSpritePtr(int area);

static bool type_is_known(uint8 t) {
  if (t > 0xF2) return false;
  for (uint32 i = 0; i < kEsUnknownLowCount; i++)
    if (kEsUnknownLowTypes[i] == t) return false;
  return true;
}

static bool type_needs_pos2(uint8 t) {
  for (uint32 i = 0; i < kEsPos2NeedCount; i++)
    if (kEsPos2NeedTypes[i] == t) return true;
  return false;
}

// Boss / boss-secondary sprite ids (Enemizer class=boss). A boss room is NEVER
// slot-2-eligible: boss shuffle can REDIRECT a vanilla boss room to host a
// different boss (Dungeon_LoadSprites src_room redirect, shuffle_boss.c), and a
// boss's sheets are loaded by the room header + spawned outside the picker — so a
// slot-2 reshuffle there would garbage the boss. Most bosses also need slot 2 (so
// already pin), but Armos Knights / Lanmolas / Vitreous / Trinexx need slot 3 and
// would otherwise slip through eligible; listing the full set is unambiguous.
static const uint8 kEsBossTypes[] = {
  0x09, 0x53, 0x54, 0x7A, 0x88, 0x8C, 0x8D, 0x92, 0xA2, 0xA3, 0xA4,
  0xBD, 0xBE, 0xBF, 0xC1, 0xCB, 0xCC, 0xCD, 0xCE, 0xD6
};
#define kEsBossCount (sizeof(kEsBossTypes)/sizeof(kEsBossTypes[0]))

static bool type_is_boss(uint8 t) {
  for (uint32 i = 0; i < kEsBossCount; i++)
    if (kEsBossTypes[i] == t) return true;
  return false;
}

// A present sprite that PINS slot 2 to its current sheet (so we must not
// reshuffle the room). Randomizable enemies never pin (the picker substitutes
// them). Bosses (boss-shuffle redirect hazard), unknown ids, and slot-2-needing
// non-randomizable sprites all pin. Pure — selfchecked.
static bool type_blocks_pos2(uint8 t) {
  if (table_is_randomizable(t)) return false;
  if (type_is_boss(t)) return true;
  return !type_is_known(t) || type_needs_pos2(t);
}

// Deterministic slot-2 choice: uniform over the safe pool ∪ {van2} (vanilla is
// always a possible outcome). `van2` is guaranteed non-zero by the caller. Pure.
static uint8 choose_pos2(uint8 van2, uint64 key) {
  uint8 cands[kEsSafePos2PoolCount + 1];
  uint32 n = 0;
  bool van_in_pool = false;
  for (uint32 i = 0; i < kEsSafePos2PoolCount; i++) {
    cands[n++] = kEsSafePos2Pool[i];
    if (kEsSafePos2Pool[i] == van2) van_in_pool = true;
  }
  if (!van_in_pool) cands[n++] = van2;
  RandoRng rng;
  Rng_SeedFromU64(&rng, key);
  return cands[Rng_NextRange(&rng, n)];
}

// Walk a dungeon room's sprite TYPE list; true iff slot 2 is free to reshuffle.
// Skips the leading sort_sprites_setting byte; entries are {y,x,type} until 0xff;
// type 0xe4 = control entry; x >= 0xe0 = overlord (conservatively pins slot 2).
#define kEsMaxSpriteScan 96  // defensive cap on the room/area list walk

static bool dungeon_pos2_eligible(uint16 room) {
  const uint8 *src = Dungeon_GetRoomSpritePtr(room);
  src++;  // sort_sprites_setting
  for (int i = 0; src[0] != 0xff; src += 3, i++) {
    if (i >= kEsMaxSpriteScan) return false;  // malformed / unterminated → conservative
    uint8 x = src[1], type = src[2];
    if (type == 0xe4) continue;     // control entry (die-action marker)
    if (x >= 0xe0) return false;    // overlord present (conservative)
    if (type_blocks_pos2(type)) return false;
  }
  return true;
}

// Walk an overworld area's sprite TYPE list; true iff slot 2 is free. Entries are
// 3-byte; type 0xf4 = sprite-count marker; type >= 0xf3 = overlord (conservative).
static bool ow_pos2_eligible(uint16 area) {
  const uint8 *src = GetOverworldSpritePtr((int)area);
  for (int i = 0; src[0] != 0xff; src += 3, i++) {
    if (i >= kEsMaxSpriteScan) return false;  // malformed / unterminated → conservative
    uint8 type = src[2];
    if (type == 0xf4) continue;     // sprite-count marker
    if (type >= 0xf3) return false; // overlord present (conservative)
    if (type_blocks_pos2(type)) return false;
  }
  return true;
}

void EnemyShuffle_ReshuffleCurrentRoomSheets(const uint8 *tileset_row) {
  if (!g_enemy_shuffle_active || tileset_row == NULL) return;

  // Only act inside a dungeon (0x07) or overworld (0x08 load / 0x09 transition)
  // room load. Other InitializeTilesets callers (attract, select-file, ending,
  // dungeon-map preview) must not be touched.
  uint8 module = g_ram[0x10];                 // main_module_index
  bool is_dungeon = (module == 0x07);
  bool is_ow = (module == 0x08 || module == 0x09);
  if (!is_dungeon && !is_ow) return;
  if (g_ram[0xAA3] >= 0x80) return;           // sprite_graphics_index: 0x80| = map preview

  // True vanilla-resolved slot 2: this row's own sheet if it loads one (a fixed
  // per-room value), else the inherited value tracked in the shadow. A 0 baseline
  // = no trustworthy vanilla yet (the shadow is 0 = "not established" until the
  // first room that loads its own slot 2); leave the loaded sheet untouched.
  bool owns_pos2 = (tileset_row[2] != 0);
  uint8 van2 = owns_pos2 ? tileset_row[2] : g_ram[kRam_EnemyShuffleVanPos2];
  if (van2 == 0) return;
  g_ram[kRam_EnemyShuffleVanPos2] = van2;

  bool eligible;
  uint64 key;
  if (is_dungeon) {
    uint16 room = (uint16)(g_ram[0xA0] | (g_ram[0xA1] << 8));  // dungeon_room_index
    eligible = dungeon_pos2_eligible(room);
    key = g_enemy_shuffle_seed ^ (uint64)room ^ kEnemyShuffleSheetSalt;
  } else {
    uint16 area = (uint16)(g_ram[0x40A] | (g_ram[0x40B] << 8)); // overworld_area_index
    eligible = ow_pos2_eligible(area);
    key = g_enemy_shuffle_seed ^ ((uint64)area << 20) ^ kEnemyShuffleSheetSalt;
  }

  // Only reshuffle rooms that OWN their slot 2 (tileset_row[2] != 0). A room that
  // INHERITS slot 2 is restored to the vanilla shadow instead — this both kills
  // the inheritance leak AND keeps the choice DETERMINISTIC per (seed, room):
  // choose_pos2 is then only ever fed van2 == tileset_row[2] (a fixed per-room
  // constant), never the visit-order-dependent shadow.
  bool reshuffle = owns_pos2 && eligible;
  uint8 chosen = reshuffle ? choose_pos2(van2, key) : van2;
  g_ram[0xC2FE] = chosen;  // sprite_gfx_subset_2 — what the picker + decompress see

#if ES_RESHUFFLE_DIAG
  if (g_ram[0x663] < 0xff) g_ram[0x663]++;                       // hook calls
  if (chosen != van2 && g_ram[0x664] < 0xff) g_ram[0x664]++;     // slot 2 actually changed
  if (!reshuffle && g_ram[0x665] < 0xff) g_ram[0x665]++;         // restored (inherit/ineligible)
  g_ram[0x666] = van2;
  g_ram[0x667] = chosen;
#endif
}

// ---------------------------------------------------------------------------
// Public API.

bool EnemyShuffle_Generate(const struct RandoSettings *settings,
                           uint64 seed_u64) {
  if (settings == NULL || settings->enemy_shuffle == 0) {
    // Off → inactive (load paths take the vanilla branch → byte-identical).
    EnemyShuffle_Deactivate();
    return true;
  }
  g_enemy_shuffle_seed = seed_u64 ^ kEnemyShuffleSalt;
  g_enemy_shuffle_active = true;
  // Reset the sheet-reshuffle inheritance shadow to 0 = "not established". The
  // first room that loads its OWN slot 2 (tileset_row[2] != 0) establishes it;
  // until then, a room that INHERITS slot 2 (and so resolves van2 == 0) is left
  // untouched by the van2 == 0 guard — avoids reshuffling/restoring from the
  // stale file-select/menu sheet that happens to be loaded at slot activation.
  g_ram[kRam_EnemyShuffleVanPos2] = 0;
  return true;
}

void EnemyShuffle_Deactivate(void) {
  g_enemy_shuffle_active = false;
  g_enemy_shuffle_seed = 0;
  g_ram[kRam_EnemyShuffleVanPos2] = 0;  // drop the inheritance shadow
}

bool EnemyShuffle_IsActive(void) {
  return g_enemy_shuffle_active;
}

uint8 EnemyShuffle_PickDungeon(uint16 room, uint8 slot, uint8 vanilla_type) {
  if (!g_enemy_shuffle_active) return vanilla_type;
  // Only substitute a recognized randomizable enemy (markers/overlords/NPCs/
  // objects/bosses pass through). The caller already skips control (0xe4) /
  // overlord (x>=0xe0) entries before the type read, but guard anyway.
  if (!table_is_randomizable(vanilla_type)) return vanilla_type;
  // Hard-exclude rooms: never touch.
  if (room_in_list(room, kHardExcludeRooms, kHardExcludeRoomsCount))
    return vanilla_type;

  uint8 live[4];
  read_live_sheets(live);

  // Conservative dungeon policy (MVP): EVERY dungeon replacement must be
  // killable && key-capable (so any key a shuffled item placement drops here is
  // obtainable, and any shutter/kill-clear door opens — beatability is the SOLE
  // job of this table). Flying is forbidden in the flying-exclude rooms.
  bool forbid_flying = room_in_list(room, kFlyingExcludeRooms, kFlyingExcludeRoomsCount);
  uint64 key = ((uint64)room << 8) ^ (uint64)slot ^ 0x000000000000D000ull;
  return pick_replacement(key, vanilla_type, live,
                          /*require_killable=*/true,
                          /*require_key_capable=*/true,
                          /*require_water=*/false,
                          /*forbid_flying=*/forbid_flying,
                          /*never_dungeon_ok=*/false,  // we ARE a dungeon
                          /*never_overworld_ok=*/true);
}

uint8 EnemyShuffle_PickOverworld(uint8 area, uint8 slot, uint8 vanilla_type) {
  if (!g_enemy_shuffle_active) return vanilla_type;
  if (!table_is_randomizable(vanilla_type)) return vanilla_type;

  uint8 live[4];
  read_live_sheets(live);

  // Overworld has no key/shutter rooms, so no killable/key constraint is needed
  // for beatability. We still keep the pool to randomizable enemies whose sheet
  // is loaded (anti-crash) and honor the never_overworld directional ban.
  uint64 key = ((uint64)area << 8) ^ (uint64)slot ^ 0x0000000000000700ull;
  return pick_replacement(key, vanilla_type, live,
                          /*require_killable=*/false,
                          /*require_key_capable=*/false,
                          /*require_water=*/false,
                          /*forbid_flying=*/false,
                          /*never_dungeon_ok=*/true,
                          /*never_overworld_ok=*/false);  // we ARE overworld
}

// ---------------------------------------------------------------------------
// Self-check (--rando-selftest). Enemy shuffle is orthogonal to item placement
// (the corpus is blind to it), so determinism + the structural invariants can
// ONLY be pinned here. exit(2) on any failure.
// ---------------------------------------------------------------------------
static void enemy_selfcheck_die(const char *msg) {
  fprintf(stderr, "[EnemyShuffle_SelfCheck] FAIL: %s\n", msg);
  exit(2);
}

// GT mini-boss + boss sprite ids that MUST be excluded (design.md D7 / Task 1.5).
// If any of these ever became RANDOMIZABLE the GT mini-boss gauntlet's
// CanKill<Boss> gates (or a boss fight) would break.
static const uint8 kMustExcludeBossIds[] = {
  0x09, 0x53, 0x54,  // GT mini-bosses: Moldorm / Armos Knights / Lanmolas
  0x88, 0x8C, 0x92, 0xA2, 0xBD, 0xCB, 0xCE,  // Mothula/Arrghus/Helmasaur/Kholdstare/Vitreous/Trinexx/Blind
  0x7A,              // Agahnim 1/2
  0xA3, 0xCC, 0xCD,  // boss secondaries: Kholdstare shell / Trinexx arms
  0xD6,              // Ganon
};

void EnemyShuffle_SelfCheck(void) {
  RandoSettings s;
  Settings_SetDefaults(&s);

  // 1) Off → inactive passthrough.
  {
    RandoSettings off = s; off.enemy_shuffle = 0;
    EnemyShuffle_Deactivate();
    if (!EnemyShuffle_Generate(&off, 0x0123456789ABCDEFull))
      enemy_selfcheck_die("Generate(off) must succeed");
    if (EnemyShuffle_IsActive())
      enemy_selfcheck_die("enemy_shuffle off must be inactive");
    if (EnemyShuffle_PickDungeon(0x52, 0, 0x08) != 0x08)
      enemy_selfcheck_die("inactive PickDungeon must be a passthrough");
    if (EnemyShuffle_PickOverworld(0x00, 0, 0x08) != 0x08)
      enemy_selfcheck_die("inactive PickOverworld must be a passthrough");
  }

  // 2) Bosses + GT mini-bosses + secondaries are NEVER randomizable.
  for (uint32 i = 0; i < sizeof(kMustExcludeBossIds); i++) {
    if (table_is_randomizable(kMustExcludeBossIds[i]))
      enemy_selfcheck_die("a boss / mini-boss / secondary id is randomizable (D7 violation)");
  }
  // Markers/NPCs/objects we must never touch (spot check).
  if (table_is_randomizable(0xe4) || table_is_randomizable(0x76) /*Zelda*/ ||
      table_is_randomizable(0x1c) /*Statue*/ || table_is_randomizable(0xd8) /*Heart*/)
    enemy_selfcheck_die("a marker/NPC/object/absorbable id is randomizable");

  // 3) Table integrity: every RANDOMIZABLE entry has >=1 required sheet (a
  // sheet-less entry would match every room and could load garbage), and every
  // CANNOT_KEY-free killable entry is a genuine enemy id (<0xF3).
  for (uint32 t = 0; t < ES_TABLE_LEN; t++) {
    const EnemyConstraint *c = &kEnemyTable[t];
    if (!(c->flags & ESF_RANDOMIZABLE)) continue;
    if (t >= 0xF3) enemy_selfcheck_die("randomizable id in the overlord/marker range (>=0xF3)");
    bool any_sheet = false;
    for (int i = 0; i < ES_MAX_SHEETS; i++) if (c->sheets[i]) any_sheet = true;
    if (!any_sheet) enemy_selfcheck_die("randomizable enemy has no required sheet (would match any room)");
  }
  // There MUST be at least one killable && key-capable candidate (else dungeon
  // key rooms could never be filled and beatability would silently fail).
  {
    bool any_keyable = false;
    for (uint32 t = 0; t < ES_TABLE_LEN; t++) {
      const EnemyConstraint *c = &kEnemyTable[t];
      if ((c->flags & ESF_RANDOMIZABLE) && (c->flags & ESF_KILLABLE) &&
          !(c->flags & ESF_CANNOT_KEY)) { any_keyable = true; break; }
    }
    if (!any_keyable) enemy_selfcheck_die("no killable+key-capable candidate exists");
  }

  // 4) On → determinism + in-sheet + killable/key invariants over a synthetic
  // loaded-sheet set. We install a shuffle and drive the pick with a fabricated
  // live-sheet set by temporarily writing g_ram[0xC2FC..0xC2FF].
  {
    RandoSettings on = s; on.enemy_shuffle = 1;
    if (!EnemyShuffle_Generate(&on, 0xDEADBEEFCAFEBABEull))
      enemy_selfcheck_die("Generate(on) must succeed");
    if (!EnemyShuffle_IsActive())
      enemy_selfcheck_die("enemy_shuffle on must be active");

    // Save + set a synthetic loaded set that loads a broad common pool:
    // sheets 12,22,23,73 cover Octorok/Snapdragon/Moblin/soldiers.
    uint8 sav[4] = { g_ram[0xC2FC], g_ram[0xC2FD], g_ram[0xC2FE], g_ram[0xC2FF] };
    g_ram[0xC2FC] = 12; g_ram[0xC2FD] = 22; g_ram[0xC2FE] = 23; g_ram[0xC2FF] = 73;
    uint8 live[4] = { 12, 22, 23, 73 };

    // Determinism: same (room, slot, type) → same pick.
    uint8 a = EnemyShuffle_PickDungeon(0x52, 3, 0x12);
    uint8 b = EnemyShuffle_PickDungeon(0x52, 3, 0x12);
    if (a != b) enemy_selfcheck_die("PickDungeon is not deterministic for a fixed (room,slot,type)");

    // Every dungeon pick over a sampled room/slot/type space is randomizable,
    // killable, key-capable, in-sheet (the MVP dungeon invariant) — or the
    // vanilla passthrough (when the room is hard-excluded / no candidate).
    for (uint16 room = 0; room < 300; room += 7) {
      bool hard = room_in_list(room, kHardExcludeRooms, kHardExcludeRoomsCount);
      for (uint8 slot = 0; slot < 4; slot++) {
        uint8 r = EnemyShuffle_PickDungeon(room, slot, 0x12 /*Moblin, randomizable*/);
        if (hard) {
          if (r != 0x12) enemy_selfcheck_die("hard-excluded room was substituted");
          continue;
        }
        if (r == 0x12) continue;  // passthrough (no candidate) is allowed
        const EnemyConstraint *c = &kEnemyTable[r];
        if (!(c->flags & ESF_RANDOMIZABLE))
          enemy_selfcheck_die("dungeon pick is not randomizable");
        if (!(c->flags & ESF_KILLABLE))
          enemy_selfcheck_die("dungeon pick is not killable (key/shutter softlock risk)");
        if (c->flags & ESF_CANNOT_KEY)
          enemy_selfcheck_die("dungeon pick is key-banned (key-room softlock risk)");
        if (!sheets_loaded(r, live))
          enemy_selfcheck_die("dungeon pick references an unloaded sheet (CRASH risk)");
      }
    }

    // Overworld picks must be randomizable + in-sheet (no killable constraint).
    for (uint8 area = 0; area < 64; area += 5) {
      for (uint8 slot = 0; slot < 3; slot++) {
        uint8 r = EnemyShuffle_PickOverworld(area, slot, 0x08 /*Octorok*/);
        if (r == 0x08) continue;
        const EnemyConstraint *c = &kEnemyTable[r];
        if (!(c->flags & ESF_RANDOMIZABLE))
          enemy_selfcheck_die("overworld pick is not randomizable");
        if (!sheets_loaded(r, live))
          enemy_selfcheck_die("overworld pick references an unloaded sheet (CRASH risk)");
      }
    }

    // An excluded source type is NEVER substituted even when active.
    if (EnemyShuffle_PickDungeon(0x52, 0, 0x53 /*Armos Knights boss*/) != 0x53)
      enemy_selfcheck_die("excluded source type (boss) was substituted");
    if (EnemyShuffle_PickDungeon(0x52, 0, 0xe4 /*control marker*/) != 0xe4)
      enemy_selfcheck_die("control marker was substituted");

    // Restore g_ram + deactivate.
    g_ram[0xC2FC] = sav[0]; g_ram[0xC2FD] = sav[1];
    g_ram[0xC2FE] = sav[2]; g_ram[0xC2FF] = sav[3];
    EnemyShuffle_Deactivate();
  }

  // 5) After deactivate, picks are passthrough again.
  if (EnemyShuffle_PickDungeon(0x52, 0, 0x12) != 0x12)
    enemy_selfcheck_die("post-deactivate PickDungeon must be a passthrough");

  // 6) Sheet-reshuffle (design.md D4) pure-logic invariants.
  {
    // (a) Pool integrity: every safe slot-2 sheet self-contains a randomizable,
    // killable, key-capable enemy (so a dungeon swap is always fillable). This is
    // the anti-garbage + anti-softlock guarantee — if the table ever drifts so a
    // pool sheet loses its candidate, FAIL here rather than ship a bad seed.
    for (uint32 i = 0; i < kEsSafePos2PoolCount; i++) {
      uint8 sheet = kEsSafePos2Pool[i];
      uint8 live1[4] = { 0, 0, sheet, 0 };  // ONLY this sheet loaded (in slot 2)
      bool ok = false;
      for (uint32 t = 0; t < ES_TABLE_LEN; t++) {
        const EnemyConstraint *c = &kEnemyTable[t];
        if ((c->flags & ESF_RANDOMIZABLE) && (c->flags & ESF_KILLABLE) &&
            !(c->flags & ESF_CANNOT_KEY) && sheets_loaded((uint8)t, live1)) {
          ok = true; break;
        }
      }
      if (!ok) enemy_selfcheck_die("a safe slot-2 pool sheet has no killable+key candidate");
    }

    // (a2) Multi-slot enemies must require ALL their sheets — a missing slot lets
    // the picker spawn them where part of their gfx isn't loaded (garbage render,
    // e.g. Walking Zora head on sheet 12 + body on a wrong slot-3 sheet). Guard
    // the two known two-slot randomizable enemies: Walking Zora (12+68) and
    // Snapdragon (22+23) must NOT be in-sheet when only one of their sheets loads.
    {
      uint8 only12[4] = { 12, 0, 0, 0 };   // Zora head sheet, no body sheet 68
      if (sheets_loaded(0x56, only12))
        enemy_selfcheck_die("Walking Zora admissible with only sheet 12 (missing sub3=68 → hybrid render)");
      uint8 only23[4] = { 23, 0, 0, 0 };   // Snapdragon slot-2 sheet, no sub0=22
      if (sheets_loaded(0x0E, only23))
        enemy_selfcheck_die("Snapdragon admissible with only sheet 23 (missing sub0=22)");
      uint8 both[4] = { 22, 0, 23, 0 };
      if (!sheets_loaded(0x0E, both))
        enemy_selfcheck_die("Snapdragon NOT admissible with both its sheets loaded");
    }

    // (b) type_blocks_pos2 classification spot checks.
    if (type_blocks_pos2(0x12))   // Moblin: randomizable ⇒ never pins (substituted)
      enemy_selfcheck_die("randomizable enemy wrongly pins slot 2");
    if (!type_blocks_pos2(0x92))  // Helmasaur King: boss, needs slot 2 ⇒ pins
      enemy_selfcheck_die("a slot-2 boss failed to pin slot 2");
    if (!type_blocks_pos2(0x90))  // Wallmaster: do-not-randomize, needs slot 2 ⇒ pins
      enemy_selfcheck_die("Wallmaster failed to pin slot 2");
    if (!type_blocks_pos2(0x53))  // Armos Knights: a SLOT-3 boss — must still pin (boss-shuffle redirect)
      enemy_selfcheck_die("a slot-3 boss (Armos Knights) failed to pin slot 2");
    if (!type_blocks_pos2(0x54))  // Lanmolas: another slot-3 boss ⇒ must pin
      enemy_selfcheck_die("a slot-3 boss (Lanmolas) failed to pin slot 2");
    if (!type_blocks_pos2(0x16))  // Sahasrahla/Aginah NPC (sub2=76) — audit HIGH regression guard
      enemy_selfcheck_die("Sahasrahla/Aginah (slot-2 NPC) failed to pin slot 2");
    if (!type_blocks_pos2(0xBC))  // Drunk-in-the-Inn NPC (sub2=74) — audit HIGH regression guard
      enemy_selfcheck_die("Drunk-in-the-Inn (slot-2 NPC) failed to pin slot 2");
    if (!type_blocks_pos2(0x05))  // unclassified id ⇒ pins (conservative)
      enemy_selfcheck_die("an unknown type failed to pin slot 2");
    if (type_blocks_pos2(0x1C))   // Statue: known, slot-3 object ⇒ does NOT pin slot 2
      enemy_selfcheck_die("a slot-3 object wrongly pinned slot 2");
    if (type_blocks_pos2(0x21))   // Push switch: known, slot-3 object ⇒ does NOT pin
      enemy_selfcheck_die("a slot-3 switch wrongly pinned slot 2");

    // (c) choose_pos2: deterministic, always in pool ∪ {van2}, vanilla-inclusive.
    for (uint8 van2 = 1; van2 != 0; van2++) {           // every non-zero baseline
      uint64 k = 0x1234567800000000ull ^ van2;
      uint8 a = choose_pos2(van2, k);
      uint8 b = choose_pos2(van2, k);
      if (a != b) enemy_selfcheck_die("choose_pos2 is not deterministic");
      bool in_set = (a == van2);
      for (uint32 i = 0; i < kEsSafePos2PoolCount; i++)
        if (kEsSafePos2Pool[i] == a) in_set = true;
      if (!in_set) enemy_selfcheck_die("choose_pos2 returned a sheet outside pool ∪ {van2}");
      if (a == 0) enemy_selfcheck_die("choose_pos2 returned sheet 0 (would mis-load)");
    }
    // Vanilla-inclusive: for a van2 OUTSIDE the pool, some key yields van2.
    {
      uint8 outside = 18;  // Geldman sheet — not in kEsSafePos2Pool
      bool reachable = false;
      for (uint64 k = 0; k < 256 && !reachable; k++)
        if (choose_pos2(outside, k) == outside) reachable = true;
      if (!reachable) enemy_selfcheck_die("vanilla slot-2 sheet is never a choose_pos2 outcome");
    }
  }

  fprintf(stderr, "[EnemyShuffle_SelfCheck] OK\n");
}
