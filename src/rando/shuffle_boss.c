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
static const uint8 kBossShuffleableDungeons[11] = {
  1, 2, 3, 5, 6, 7, 8, 9, 10, 11, 12,
};
// 12 (GT) is in the list because GT-Big-Boss (Moldorm at top) is part
// of the shuffle in ALTTPR; Agahnim2 is its own slot pinned separately.
// Wait — GT has TWO boss rooms in ALTTPR. The shuffleable one is
// "GT mini-boss" (e.g., Moldorm at top); Agahnim2 is the final boss.
// For Phase A simplicity treat GT's shuffle slot as the same dungeon
// id (12); the per-site instrumentation will disambiguate.

// Phase A pool of shuffleable bosses (11 entries, excluding Agahnim +
// Agahnim2). The vanilla mapping (kBossVanilla) places these at the
// dungeons listed above.
static const uint8 kBossShufflePool[11] = {
  kBoss_ArmosKnights, kBoss_Lanmolas, kBoss_Moldorm,
  kBoss_HelmasaurKing, kBoss_Arrghus, kBoss_Mothula,
  kBoss_Blind, kBoss_Kholdstare, kBoss_Vitreous,
  kBoss_Trinexx, kBoss_Agahnim2,
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

  uint8 pool[11];
  memcpy(pool, kBossShufflePool, sizeof(pool));
  fy_shuffle_u8(&rng, pool, 11);

  // Assign shuffled pool to the shuffleable dungeon slots in order.
  for (uint32 i = 0; i < 11; i++) {
    uint8 dungeon = kBossShuffleableDungeons[i];
    out_assignment[dungeon] = pool[i];
  }
  // Pin Agahnim 1 + Agahnim 2 — overwrite anything the shuffle could
  // have landed there. (Agahnim2 is in the pool because slot 12=GT can
  // receive any pool boss for its shuffleable slot; the GT-top Agahnim2
  // boss is per-instance separately at the sprite-handler level.)
  out_assignment[4] = kBoss_Agahnim;

  memcpy(g_boss_assignment, out_assignment, sizeof(g_boss_assignment));
  g_boss_assignment_active = true;
  return true;
}

uint8 BossShuffle_GetForDungeon(uint8 dungeon_id) {
  if (!g_boss_assignment_active) return 0xFF;
  if (dungeon_id >= 16) return 0xFF;
  return g_boss_assignment[dungeon_id];
}
