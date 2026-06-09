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
  [0x08] = E_RAND(ESF_KILLABLE, 12, 24),                 // Octorok (one-way)
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
  [0x56] = E_RAND(ESF_KILLABLE | ESF_WATER | ESF_CANNOT_KEY, 12),  // Walking Zora

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
  return true;
}

void EnemyShuffle_Deactivate(void) {
  g_enemy_shuffle_active = false;
  g_enemy_shuffle_seed = 0;
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

  fprintf(stderr, "[EnemyShuffle_SelfCheck] OK\n");
}
