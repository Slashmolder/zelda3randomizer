// shuffle_boss.c — Phase B Slice 7 (add-rando-shuffles-and-minigames).
//
// Phase B §63 lands the deterministic permutation algorithm; per-site
// sprite-handler instrumentation (§65) still pending. Until that lands
// the generated assignment is written but not consumed by the game
// (BossShuffle_GetForDungeon is read-only API surface).
//
// ALTTPR upstream: app/Boss.php — 13 bosses. Agahnim and Agahnim2 are
// pinned (HCT and GT top). The 11 remaining bosses shuffle across the
// 11 non-Agahnim dungeon-boss rooms (EP, DP, ToH, PoD, SP, SW, TT, IP,
// MM, TR, GT).
//
// Determinism: Fisher-Yates shuffle keyed by (seed_u64 XOR salt) via
// the project's xoshiro256** RNG (rando_rng.h).

#include "shuffle_boss.h"
#include "rando_settings.h"
#include "rando_rng.h"
#include <string.h>

// Boss-pool indices — match `Boss::all()` order in `app/Boss.php:68-129`.
// Agahnim (3) and Agahnim2 (11) are NOT in the shuffle pool; they're
// pinned per slot.
enum {
  kBoss_ArmosKnights  = 0,
  kBoss_Lanmolas      = 1,
  kBoss_Moldorm       = 2,
  kBoss_Agahnim       = 3,   // pinned at HCT (dungeon 4)
  kBoss_HelmasaurKing = 4,
  kBoss_Arrghus       = 5,
  kBoss_Mothula       = 6,
  kBoss_Blind         = 7,
  kBoss_Kholdstare    = 8,
  kBoss_Vitreous      = 9,
  kBoss_Trinexx       = 10,
  kBoss_Agahnim2      = 11,  // pinned at GT-top (dungeon 12)
};

// Vanilla per-dungeon boss assignment (dungeon-id → boss-pool index).
// Indexing matches the dungeon-id table in `op_registry.yaml`:
//   HCE=0, EP=1, DP=2, ToH=3, HCT=4, PoD=5, SP=6, SW=7, TT=8, IP=9,
//   MM=10, TR=11, GT=12.
// HCE (0) has no boss (escape sequence); we still occupy slot 0 with
// 0xFF so the loop's index = dungeon_id.
static const uint8 kBossVanilla[16] = {
  0xFF,                // 0  HCE  (no boss)
  kBoss_ArmosKnights,  // 1  EP
  kBoss_Lanmolas,      // 2  DP
  kBoss_Moldorm,       // 3  ToH
  kBoss_Agahnim,       // 4  HCT  (pinned)
  kBoss_HelmasaurKing, // 5  PoD
  kBoss_Arrghus,       // 6  SP
  kBoss_Mothula,       // 7  SW
  kBoss_Blind,         // 8  TT
  kBoss_Kholdstare,    // 9  IP
  kBoss_Vitreous,      // 10 MM
  kBoss_Trinexx,       // 11 TR
  kBoss_Agahnim2,      // 12 GT   (pinned)
  0xFF, 0xFF, 0xFF,    // 13-15 unused
};

// Dungeon-ids whose boss is shuffleable (excludes HCE=0, HCT=4, GT=12).
// Per ALTTPR's app/Boss.php, both Agahnim 1 (HCT) and Agahnim 2 (GT top)
// are pinned — they share sprite_type 0x7A (the runtime discriminates by
// `is_in_dark_world` inside `Sprite_7A_Agahnim`), so pinning both is the
// only safe option without per-call-site world disambiguation. The
// shuffleable pool is 10 bosses across 10 dungeon-boss rooms.
static const uint8 kBossShuffleableDungeons[10] = {
  1, 2, 3, 5, 6, 7, 8, 9, 10, 11,
};

// Phase A pool of shuffleable bosses (10 entries, excluding both
// Agahnims). The vanilla mapping (kBossVanilla) places these at the
// dungeons listed above.
static const uint8 kBossShufflePool[10] = {
  kBoss_ArmosKnights, kBoss_Lanmolas, kBoss_Moldorm,
  kBoss_HelmasaurKing, kBoss_Arrghus, kBoss_Mothula,
  kBoss_Blind, kBoss_Kholdstare, kBoss_Vitreous,
  kBoss_Trinexx,
};

static uint8 g_boss_assignment[16];
static bool g_boss_assignment_active = false;

// Fisher-Yates shuffle of `arr[0..n-1]` using `rng` for randomness.
static void fy_shuffle_u8(RandoRng *rng, uint8 *arr, uint32 n) {
  for (uint32 i = n; i > 1; i--) {
    uint32 j = Rng_NextRange(rng, i);
    uint8 t = arr[i - 1]; arr[i - 1] = arr[j]; arr[j] = t;
  }
}

bool BossShuffle_Generate(const RandoSettings *settings,
                          uint64 seed_u64,
                          uint8 out_assignment[16]) {
  // Identity assignment first.
  memcpy(out_assignment, kBossVanilla, sizeof(kBossVanilla));

  if (settings == NULL || settings->boss_shuffle == 0) {
    // Off — identity. Mark active for the API contract; lookup returns
    // vanilla.
    memcpy(g_boss_assignment, out_assignment, sizeof(g_boss_assignment));
    g_boss_assignment_active = true;
    return true;
  }

  // Deterministic permutation. Salt distinguishes the boss-shuffle RNG
  // stream from other slice's RNG so changing one shuffle setting
  // doesn't perturb the others.
  RandoRng rng;
  // Salt = "BOSS5HFF" cooked into a 64-bit constant ("BOSSSHFF" in
  // ASCII = 0x424F5353_53484646; pick a recognizable cousin).
  Rng_SeedFromU64(&rng, seed_u64 ^ 0xB055A11FB055A11Full);

  uint8 pool[10];
  memcpy(pool, kBossShufflePool, sizeof(pool));
  fy_shuffle_u8(&rng, pool, 10);

  // Assign shuffled pool to the shuffleable dungeon slots in order.
  for (uint32 i = 0; i < 10; i++) {
    uint8 dungeon = kBossShuffleableDungeons[i];
    out_assignment[dungeon] = pool[i];
  }
  // Pin Agahnim 1 (HCT) and Agahnim 2 (GT-top). kBossVanilla already
  // assigns these, but write explicitly to defend against future pool
  // edits that could touch slots 4/12.
  out_assignment[4]  = kBoss_Agahnim;
  out_assignment[12] = kBoss_Agahnim2;

  memcpy(g_boss_assignment, out_assignment, sizeof(g_boss_assignment));
  g_boss_assignment_active = true;
  return true;
}

uint8 BossShuffle_GetForDungeon(uint8 dungeon_id) {
  if (!g_boss_assignment_active) return 0xFF;
  if (dungeon_id >= 16) return 0xFF;
  return g_boss_assignment[dungeon_id];
}

// Vanilla boss sprite IDs (cross-referenced with the `Sprite_*` symbol
// table in `src/sprite_main.h` and `other/names.txt`). Agahnim 1 and Agahnim 2
// share sprite_type 0x7A; the runtime discriminates by `is_in_dark_world`
// inside `Sprite_7A_Agahnim`. Both are PINNED at their vanilla dungeons (HCT=4
// and GT-top=12) so the remap never has to disambiguate.
//   0x09 Moldorm        (ToH, dungeon 3)
//   0x53 ArmosKnights   (EP,  dungeon 1)
//   0x54 Lanmolas       (DP,  dungeon 2)
//   0x7A Agahnim 1/2    (HCT/GT, dungeons 4/12 — pinned, no remap entry)
//   0x88 Mothula        (SW,  dungeon 7)
//   0x8C Arrghus        (SP,  dungeon 6)
//   0x92 HelmasaurKing  (PoD, dungeon 5)
//   0xA2 Kholdstare     (IP,  dungeon 9)  -- 0xA3 is KholdstareShell, NOT the boss
//   0xBD Vitreous       (MM,  dungeon 10)
//   0xCB Trinexx        (TR,  dungeon 11)
//   0xCE Blind          (TT,  dungeon 8)
//
// 0xB6 is `Sprite_B6_Kiki`, NOT Agahnim 2 — DO NOT
// add it to the table; remapping Kiki at GT-door would soft-lock GT entry.
//
// Translation: each boss sprite type in the table uniquely identifies
// which dungeon's boss is being spawned, because vanilla LttP places one
// boss per dungeon. We use the incoming sprite_type as the dungeon-key,
// look up the shuffle assignment, and translate the assigned pool index
// back to a sprite type via kBossPoolIdxToSprite.
//
// Limitation (M1 follow-up): multi-segment bosses (Trinexx CB/CC/CD,
// Lanmolas, Armos Knights x6, Mothula spikes) only remap the primary
// sprite; secondary segments pass through unchanged. The shuffled room
// will have orphan segments of the original boss alongside the new
// boss until a group-aware remap lands.
typedef struct BossSpriteMap {
  uint8 sprite_type;
  uint8 dungeon_id;
  uint8 pool_idx;
} BossSpriteMap;

static const BossSpriteMap kBossSpriteMap[] = {
  { 0x09, 3,  kBoss_Moldorm },
  { 0x53, 1,  kBoss_ArmosKnights },
  { 0x54, 2,  kBoss_Lanmolas },
  { 0x88, 7,  kBoss_Mothula },
  { 0x8C, 6,  kBoss_Arrghus },
  { 0x92, 5,  kBoss_HelmasaurKing },
  { 0xA2, 9,  kBoss_Kholdstare },
  { 0xBD, 10, kBoss_Vitreous },
  { 0xCB, 11, kBoss_Trinexx },
  { 0xCE, 8,  kBoss_Blind },
};
#define kBossSpriteMapCount (sizeof(kBossSpriteMap) / sizeof(kBossSpriteMap[0]))

// Reverse: pool index → sprite type. 12 entries. Both Agahnim slots
// map to 0xFF (poisoned sentinel — cluster audit LOW-3) rather than
// the shared 0x7A. The remap function falls back to vanilla on 0xFF,
// preserving correctness if a future edit puts Agahnim2 back in the
// pool: instead of silently spawning the wrong Agahnim variant in a
// random dungeon (the `is_in_dark_world` rendering discriminator inside
// `Sprite_7A_Agahnim` can't tell which Agahnim was intended), the
// remap returns the input vanilla type unchanged. The safer default
// future-proofs against the exact bug §65 audit caught and removed.
static const uint8 kBossPoolIdxToSprite[12] = {
  [kBoss_ArmosKnights]  = 0x53,
  [kBoss_Lanmolas]      = 0x54,
  [kBoss_Moldorm]       = 0x09,
  [kBoss_Agahnim]       = 0xFF,  // pinned; never picked → poisoned
  [kBoss_HelmasaurKing] = 0x92,
  [kBoss_Arrghus]       = 0x8C,
  [kBoss_Mothula]       = 0x88,
  [kBoss_Blind]         = 0xCE,
  [kBoss_Kholdstare]    = 0xA2,
  [kBoss_Vitreous]      = 0xBD,
  [kBoss_Trinexx]       = 0xCB,
  [kBoss_Agahnim2]      = 0xFF,  // pinned; never picked → poisoned
};

uint8 BossShuffle_RemapSpriteType(uint8 vanilla_sprite_type) {
  if (!g_boss_assignment_active) return vanilla_sprite_type;

  for (uint32 i = 0; i < kBossSpriteMapCount; i++) {
    if (kBossSpriteMap[i].sprite_type != vanilla_sprite_type) continue;
    uint8 dungeon = kBossSpriteMap[i].dungeon_id;
    uint8 pool_idx = g_boss_assignment[dungeon];
    if (pool_idx == 0xFF || pool_idx >= 12) return vanilla_sprite_type;
    uint8 mapped = kBossPoolIdxToSprite[pool_idx];
    // 0 = uninitialized entry; 0xFF = poisoned (pinned Agahnim variants
    // that must not be substituted because the sprite id cannot
    // disambiguate A1 vs A2 without world-state context — cluster
    // audit LOW-3). Fall back to vanilla in both cases.
    if (mapped == 0 || mapped == 0xFF) return vanilla_sprite_type;
    return mapped;
  }
  return vanilla_sprite_type;
}

// Room-data secondary segments. Each entry pairs a secondary sprite_type
// with the dungeon + pool-index of its primary boss. Suppression fires
// when boss-shuffle is active AND the dungeon's assignment differs from
// the vanilla primary.
typedef struct BossSecondarySegment {
  uint8 sprite_type;
  uint8 parent_dungeon_id;
  uint8 parent_pool_idx;
} BossSecondarySegment;

static const BossSecondarySegment kBossSecondaries[] = {
  { 0xCC, 11, kBoss_Trinexx },    // Trinexx left arm  (TR)
  { 0xCD, 11, kBoss_Trinexx },    // Trinexx right arm (TR)
  { 0xA3, 9,  kBoss_Kholdstare }, // KholdstareShell   (IP)
};
#define kBossSecondariesCount (sizeof(kBossSecondaries) / sizeof(kBossSecondaries[0]))

bool BossShuffle_ShouldSuppressSecondary(uint8 vanilla_sprite_type) {
  if (!g_boss_assignment_active) return false;
  for (uint32 i = 0; i < kBossSecondariesCount; i++) {
    if (kBossSecondaries[i].sprite_type != vanilla_sprite_type) continue;
    uint8 dungeon = kBossSecondaries[i].parent_dungeon_id;
    uint8 assigned = g_boss_assignment[dungeon];
    return assigned != kBossSecondaries[i].parent_pool_idx;
  }
  return false;
}
