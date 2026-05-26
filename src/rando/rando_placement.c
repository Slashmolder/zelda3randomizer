// rando_placement.c — item pool, placement table, digest (tasks.md §4.1, §4.4, §6.1).
//
// Phase A0 scope:
//   - BuildItemPool returns a minimal "identity" pool: every location's
//     vanilla_item, in registry order. Phase A1 replaces with real per-settings
//     construction (progressive vs absolute, dungeon-item modes, etc.).
//   - Place_AssumedFill returns an identity placement (every location ←
//     its vanilla_item_id). Phase A1 replaces with the assumed-fill algorithm.
//   - PlacementTable_ComputeDigest emits SHA-256 over the canonical
//     serialization — this works at A0 and is what the regression corpus diffs.
//   - Rando_OnLocationCheck (in rando.c) consults the active placement table
//     once one is installed via Placement_Install.

#include "rando_placement.h"
#include "rando.h"
#include "rando_logic.h"
#include "rando_rng.h"
#include "rando_shuffles.h"
#include "../types.h"
#include "third_party/sha256/sha256.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Generated tables — RandoLocationDef typedef + kRandoLocations[] /
// kRandoLocationsCount extern come from rando_logic.h.

// ---------------------------------------------------------------------------
// Active placement table — set by Placement_Install. Phase A0: starts NULL,
// meaning Rando_OnLocationCheck returns vanilla_item_id for every call
// (effectively rando-inactive).
// ---------------------------------------------------------------------------
static const RandoPlacementTable *g_active_placement = NULL;

void Placement_Install(const RandoPlacementTable *t) {
  g_active_placement = t;
}

const RandoPlacementTable *Placement_GetActive(void) {
  return g_active_placement;
}

// ---------------------------------------------------------------------------
// Linear-scan dispatch lookup. Phase A0 placement tables are small (~237
// entries); O(N) scan per location-check is fine. Phase A1 may switch to a
// sorted-by-location-id table with binary search.
//
// Returns vanilla_item_id when:
//   - No placement table installed (g_active_placement == NULL).
//   - location_id is not in the table (e.g., a slot from a future binary).
// ---------------------------------------------------------------------------
uint16 Placement_Lookup(uint16 location_id, uint16 vanilla_item_id) {
  if (g_active_placement == NULL) return vanilla_item_id;
  for (uint16 i = 0; i < g_active_placement->count; i++) {
    if (g_active_placement->entries[i].location_id == location_id) {
      return g_active_placement->entries[i].item_id;
    }
  }
  return vanilla_item_id;
}

// ---------------------------------------------------------------------------
// BuildItemPool — per-settings construction (tasks.md §4.1).
//
// Phase A1 implementation. The pool is built from:
//
//   1. Progression items per mode.weapons (progressive vs. absolute weapon set)
//   2. Common non-weapon equipment (boots, flippers, moon pearl, ...)
//   3. Dungeon items per dungeon_items.{small_keys,big_keys,maps,compasses} mode
//      - Vanilla : NOT added to pool (placed at vanilla locations)
//      - Dungeon : added to pool with per-location placement restrictions
//      - Wild    : added to pool, no per-location restrictions
//   4. Bottles (default 4; capped at link_bottle_info[4] slots total)
//   5. Magic upgrades (HalfMagic, QuarterMagic)
//   6. Heart items (PieceOfHeart, BossHeartContainer)
//   7. Multi-tier rupees
//   8. Junk pool (SmallMagic, Arrows, Bombs; Rupoor at hard/expert)
//   9. TriforcePiece × pieces_placed for Triforce Hunt / Ganon Hunt
//  10. Junk-pad to |locations|
//
// The exact junk counts mirror ALTTPR's config/alttp.php `item.junk` table
// (Phase A1 approximation — see ALTTPR `World.php::getItemPool` for the
// authoritative computation, line 838).
// ---------------------------------------------------------------------------

// Item registry id constants (from assets/rando/item_registry.yaml).
// Kept as named constants so the implementation reads cleanly.
enum {
  ID_ProgressiveSword = 0,
  ID_ProgressiveShield = 1,
  ID_ProgressiveArmor = 2,
  ID_ProgressiveGlove = 3,
  ID_ProgressiveBow = 4,
  ID_L1Sword = 5, ID_L2Sword = 6, ID_L3Sword = 7, ID_L4Sword = 8,
  ID_FighterShield = 9, ID_RedShield = 10, ID_MirrorShield = 11,
  ID_BlueMail = 12, ID_RedMail = 13,
  ID_PowerGlove = 14, ID_TitanMitt = 15,
  ID_FireRod = 16, ID_IceRod = 17, ID_Hammer = 18, ID_Hookshot = 19,
  ID_Bow = 20, ID_BlueBoomerang = 21, ID_RedBoomerang = 22,
  ID_MagicPowder = 23, ID_Mushroom = 24,
  ID_Bombos = 25, ID_Ether = 26, ID_Quake = 27, ID_Lamp = 28,
  ID_Shovel = 29, ID_OcarinaInactive = 30,
  ID_BugCatchingNet = 31, ID_BookOfMudora = 32,
  ID_CaneOfSomaria = 33, ID_CaneOfByrna = 34, ID_Cape = 35,
  ID_MagicMirror = 36, ID_Boots = 37, ID_Flippers = 38, ID_MoonPearl = 39,
  ID_SilverArrowUpgrade = 40,
  ID_HalfMagic = 41, ID_QuarterMagic = 42,
  ID_BottleEmpty = 43, ID_BottleWithFairy = 44, ID_BottleWithBee = 45,
  ID_BottleWithGoodBee = 46, ID_BottleWithRedPotion = 47,
  ID_BottleWithGreenPotion = 48, ID_BottleWithBluePotion = 49,
  ID_PieceOfHeart = 50, ID_BossHeartContainer = 51,
  ID_TriforcePiece = 52,
  ID_SmallKey_HCE = 53, ID_SmallKey_EP = 54, ID_SmallKey_DP = 55,
  ID_SmallKey_TH = 56, ID_SmallKey_HCT = 57, ID_SmallKey_PoD = 58,
  ID_SmallKey_SP = 59, ID_SmallKey_SW = 60, ID_SmallKey_TT = 61,
  ID_SmallKey_IP = 62, ID_SmallKey_MM = 63, ID_SmallKey_TR = 64,
  ID_SmallKey_GT = 65,
  ID_BigKey_EP = 66, ID_BigKey_DP = 67, ID_BigKey_TH = 68,
  ID_BigKey_PoD = 69, ID_BigKey_SP = 70, ID_BigKey_SW = 71,
  ID_BigKey_TT = 72, ID_BigKey_IP = 73, ID_BigKey_MM = 74,
  ID_BigKey_TR = 75, ID_BigKey_GT = 76,
  ID_Map_EP = 77, ID_Map_DP = 78, ID_Map_TH = 79, ID_Map_PoD = 80,
  ID_Map_SP = 81, ID_Map_SW = 82, ID_Map_TT = 83, ID_Map_IP = 84,
  ID_Map_MM = 85, ID_Map_TR = 86, ID_Map_GT = 87,
  ID_Compass_EP = 88, ID_Compass_DP = 89, ID_Compass_TH = 90,
  ID_Compass_PoD = 91, ID_Compass_SP = 92, ID_Compass_SW = 93,
  ID_Compass_TT = 94, ID_Compass_IP = 95, ID_Compass_MM = 96,
  ID_Compass_TR = 97, ID_Compass_GT = 98,
  ID_Rupee1 = 99, ID_Rupee5 = 100, ID_Rupee20 = 101,
  ID_Rupee100 = 102, ID_Rupee300 = 103,
  ID_SmallMagic = 104, ID_Arrow1 = 105, ID_Arrow10 = 106,
  ID_Bombs1 = 107, ID_Bombs3 = 108, ID_Bombs10 = 109, ID_Rupoor = 110,
  ID_Map_HCE = 124,
};

// Per-dungeon small-key counts (vanilla per ALTTPR config; small_keys.X).
static const struct { uint16 item_id; uint8 count; } kVanillaSmallKeyCounts[] = {
  { ID_SmallKey_HCE, 1 },
  { ID_SmallKey_EP,  0 }, // EP has no small keys in vanilla
  { ID_SmallKey_DP,  1 },
  { ID_SmallKey_TH,  1 },
  { ID_SmallKey_HCT, 2 },
  { ID_SmallKey_PoD, 6 },
  { ID_SmallKey_SP,  1 },
  { ID_SmallKey_SW,  3 },
  { ID_SmallKey_TT,  1 },
  { ID_SmallKey_IP,  2 },
  { ID_SmallKey_MM,  3 },
  { ID_SmallKey_TR,  4 },
  { ID_SmallKey_GT,  4 },
};

static const uint16 kBigKeys[] = {
  ID_BigKey_EP, ID_BigKey_DP, ID_BigKey_TH, ID_BigKey_PoD, ID_BigKey_SP,
  ID_BigKey_SW, ID_BigKey_TT, ID_BigKey_IP, ID_BigKey_MM, ID_BigKey_TR, ID_BigKey_GT,
};
static const uint16 kMaps[] = {
  ID_Map_HCE, ID_Map_EP, ID_Map_DP, ID_Map_TH, ID_Map_PoD, ID_Map_SP,
  ID_Map_SW, ID_Map_TT, ID_Map_IP, ID_Map_MM, ID_Map_TR, ID_Map_GT,
};
static const uint16 kCompasses[] = {
  ID_Compass_EP, ID_Compass_DP, ID_Compass_TH, ID_Compass_PoD, ID_Compass_SP,
  ID_Compass_SW, ID_Compass_TT, ID_Compass_IP, ID_Compass_MM, ID_Compass_TR, ID_Compass_GT,
};

// Add `n` copies of `item_id` to the pool, respecting capacity.
static uint16 pool_add(uint16 *pool, uint16 used, uint16 capacity, uint16 item_id, uint16 n) {
  for (uint16 i = 0; i < n && used < capacity; i++) pool[used++] = item_id;
  return used;
}

uint16 BuildItemPool(const RandoSettings *settings, uint16 *out_items, uint16 capacity) {
  if (settings == NULL || out_items == NULL || capacity == 0) return 0;
  uint16 n = 0;

  // ----- Progression items: sword / shield / armor / glove / bow -----
  if (settings->mode_weapons == kModeWeapons_Randomized ||
      settings->mode_weapons == kModeWeapons_Assured) {
    // ALTTPR convention for Randomized/Assured: 4 progressive swords, 3
    // progressive shields, 2 progressive armor, 2 progressive gloves, 2
    // progressive bows. (Per `Randomizer.php:183-198`; counts match the max
    // tier counts in item_registry.yaml.)
    n = pool_add(out_items, n, capacity, ID_ProgressiveSword, 4);
    n = pool_add(out_items, n, capacity, ID_ProgressiveShield, 3);
    n = pool_add(out_items, n, capacity, ID_ProgressiveArmor, 2);
    n = pool_add(out_items, n, capacity, ID_ProgressiveGlove, 2);
    n = pool_add(out_items, n, capacity, ID_ProgressiveBow, 2);
  } else {
    // Absolute weapon mode (Phase B 'vanilla' / Phase B 'swordless' reserved):
    // emit one of each tier as a distinct item.
    n = pool_add(out_items, n, capacity, ID_L1Sword, 1);
    n = pool_add(out_items, n, capacity, ID_L2Sword, 1);
    n = pool_add(out_items, n, capacity, ID_L3Sword, 1);
    n = pool_add(out_items, n, capacity, ID_L4Sword, 1);
    n = pool_add(out_items, n, capacity, ID_FighterShield, 1);
    n = pool_add(out_items, n, capacity, ID_RedShield, 1);
    n = pool_add(out_items, n, capacity, ID_MirrorShield, 1);
    n = pool_add(out_items, n, capacity, ID_BlueMail, 1);
    n = pool_add(out_items, n, capacity, ID_RedMail, 1);
    n = pool_add(out_items, n, capacity, ID_PowerGlove, 1);
    n = pool_add(out_items, n, capacity, ID_TitanMitt, 1);
    n = pool_add(out_items, n, capacity, ID_Bow, 1);
    n = pool_add(out_items, n, capacity, ID_SilverArrowUpgrade, 1);
  }

  // ----- Common non-weapon equipment (always one copy each) -----
  n = pool_add(out_items, n, capacity, ID_FireRod, 1);
  n = pool_add(out_items, n, capacity, ID_IceRod, 1);
  n = pool_add(out_items, n, capacity, ID_Hammer, 1);
  n = pool_add(out_items, n, capacity, ID_Hookshot, 1);
  n = pool_add(out_items, n, capacity, ID_BlueBoomerang, 1);
  n = pool_add(out_items, n, capacity, ID_RedBoomerang, 1);
  n = pool_add(out_items, n, capacity, ID_MagicPowder, 1);
  n = pool_add(out_items, n, capacity, ID_Mushroom, 1);
  n = pool_add(out_items, n, capacity, ID_Bombos, 1);
  n = pool_add(out_items, n, capacity, ID_Ether, 1);
  n = pool_add(out_items, n, capacity, ID_Quake, 1);
  n = pool_add(out_items, n, capacity, ID_Lamp, 1);
  n = pool_add(out_items, n, capacity, ID_Shovel, 1);
  n = pool_add(out_items, n, capacity, ID_OcarinaInactive, 1);
  n = pool_add(out_items, n, capacity, ID_BugCatchingNet, 1);
  n = pool_add(out_items, n, capacity, ID_BookOfMudora, 1);
  n = pool_add(out_items, n, capacity, ID_CaneOfSomaria, 1);
  n = pool_add(out_items, n, capacity, ID_CaneOfByrna, 1);
  n = pool_add(out_items, n, capacity, ID_Cape, 1);
  n = pool_add(out_items, n, capacity, ID_MagicMirror, 1);
  n = pool_add(out_items, n, capacity, ID_Boots, 1);
  n = pool_add(out_items, n, capacity, ID_Flippers, 1);
  n = pool_add(out_items, n, capacity, ID_MoonPearl, 1);

  // ----- Magic upgrades -----
  n = pool_add(out_items, n, capacity, ID_HalfMagic, 1);
  // QuarterMagic is a Phase A item but ALTTPR places it only at specific
  // higher-pool-difficulty configs. Phase A includes it; if the player
  // collects both Half then Quarter, the dispatcher applies them in order
  // (per audit §0.4.1 notes).
  n = pool_add(out_items, n, capacity, ID_QuarterMagic, 1);

  // ----- Bottles: default 4 (the cap; link_bottle_info has 4 slots) -----
  // Per `randomizer-core / Item pool construction`: "Bottle count is capped
  // at 4 total". For now emit 4 BottleEmpty; future settings (Triforce Hunt
  // assured-bottle) may pre-place a starting bottle in which case the pool
  // contains 3.
  n = pool_add(out_items, n, capacity, ID_BottleEmpty, 4);

  // ----- Heart items: PoH and BossHeartContainer counts per ALTTPR vanilla -----
  // Vanilla ALTTPR: 24 Piece-of-Heart + 10 BossHeartContainer (one per dungeon
  // boss). When region.bossHeartsInPool is false (Phase A default), the 10
  // boss-heart slots are identity-placed at the boss locations, so the pool
  // includes BossHeartContainer ×10 anyway — they end up at their _BossHeart slots.
  n = pool_add(out_items, n, capacity, ID_PieceOfHeart, 24);
  n = pool_add(out_items, n, capacity, ID_BossHeartContainer, 10);

  // ----- Dungeon items (per dungeon_items.* mode) -----
  // Vanilla: NOT in pool (placed at vanilla locations by the placement
  // algorithm's identity rule). Dungeon/Wild: add to pool.
  if (settings->dungeon_small_keys_mode != kDungeonItemMode_Vanilla) {
    for (uint8 i = 0; i < (uint8)(sizeof(kVanillaSmallKeyCounts) / sizeof(kVanillaSmallKeyCounts[0])); i++) {
      n = pool_add(out_items, n, capacity, kVanillaSmallKeyCounts[i].item_id, kVanillaSmallKeyCounts[i].count);
    }
  }
  if (settings->dungeon_big_keys_mode != kDungeonItemMode_Vanilla) {
    for (uint8 i = 0; i < (uint8)(sizeof(kBigKeys) / sizeof(kBigKeys[0])); i++) {
      n = pool_add(out_items, n, capacity, kBigKeys[i], 1);
    }
  }
  if (settings->dungeon_maps_mode != kDungeonItemMode_Vanilla) {
    for (uint8 i = 0; i < (uint8)(sizeof(kMaps) / sizeof(kMaps[0])); i++) {
      n = pool_add(out_items, n, capacity, kMaps[i], 1);
    }
  }
  if (settings->dungeon_compasses_mode != kDungeonItemMode_Vanilla) {
    for (uint8 i = 0; i < (uint8)(sizeof(kCompasses) / sizeof(kCompasses[0])); i++) {
      n = pool_add(out_items, n, capacity, kCompasses[i], 1);
    }
  }

  // ----- Rupees -----
  // ALTTPR vanilla pool: 4× Rupee300, 5× Rupee100, 28× Rupee20, 7× Rupee5,
  // 2× Rupee1. Numbers approximate per `config/alttp.php item.junk`.
  n = pool_add(out_items, n, capacity, ID_Rupee300, 4);
  n = pool_add(out_items, n, capacity, ID_Rupee100, 5);
  n = pool_add(out_items, n, capacity, ID_Rupee20,  28);
  n = pool_add(out_items, n, capacity, ID_Rupee5,   7);
  n = pool_add(out_items, n, capacity, ID_Rupee1,   2);

  // ----- Arrows, Bombs, Magic refills -----
  n = pool_add(out_items, n, capacity, ID_Arrow10, 5);
  n = pool_add(out_items, n, capacity, ID_Arrow1,  1);
  n = pool_add(out_items, n, capacity, ID_Bombs10, 1);
  n = pool_add(out_items, n, capacity, ID_Bombs3,  10);
  n = pool_add(out_items, n, capacity, ID_Bombs1,  0);
  n = pool_add(out_items, n, capacity, ID_SmallMagic, 1);

  // ----- Rupoor (hard/expert pools only) -----
  if (settings->item_pool_difficulty == kItemPoolDifficulty_Hard ||
      settings->item_pool_difficulty == kItemPoolDifficulty_Expert) {
    n = pool_add(out_items, n, capacity, ID_Rupoor,
                 settings->item_pool_difficulty == kItemPoolDifficulty_Expert ? 4 : 2);
  }

  // ----- Triforce pieces (Hunt goals) -----
  if (settings->goal == kGoal_TriforceHunt || settings->goal == kGoal_GanonHunt) {
    n = pool_add(out_items, n, capacity, ID_TriforcePiece, settings->pieces_placed);
  }

  // ----- Junk-pad to match location count -----
  // ALTTPR pads with small rupees / single-bomb / single-arrow / small heart.
  // For Phase A we pad with Rupee20 (the most-common ALTTPR junk).
  uint16 target = (uint16)kRandoLocationsCount;
  while (n < target && n < capacity) {
    out_items[n++] = ID_Rupee20;
  }

  return n;
}

// ---------------------------------------------------------------------------
// Item-progression classifier (tasks.md §4.3).
//
// "Progression" items are those whose placement affects reachability —
// weapons, utility items, dungeon keys, bottles, prizes, triforce pieces,
// and the magic upgrades. Non-progression items (rupees, arrows, bombs,
// hearts, maps, compasses, junk) are placed last via simple shuffle.
//
// Item IDs match `assets/rando/item_registry.yaml`. Centralizing the
// classification here means the spec stays the source of truth.
// ---------------------------------------------------------------------------
static bool is_progression_item(uint16 item_id) {
  // Progressive items (0..4)
  if (item_id <= 4) return true;
  // Absolute weapons / utility (5..40 covers sword tiers, shields, armor,
  // gloves, rods, hammer, hookshot, bow, boomerangs, powder, mushroom,
  // medallions, lamp, shovel, ocarina, bug net, book, somaria, byrna, cape,
  // mirror, boots, flippers, moon pearl, silver arrows).
  if (item_id >= 5 && item_id <= 40) return true;
  // Magic upgrades (41, 42)
  if (item_id == 41 || item_id == 42) return true;
  // Bottles (43..49)
  if (item_id >= 43 && item_id <= 49) return true;
  // PieceOfHeart (50) / BossHeartContainer (51) — NOT progression for Phase A.
  // (Phase B's hero mode or low-health logic may revisit.)
  // TriforcePiece (52) — progression for Triforce Hunt / Ganon Hunt goals.
  if (item_id == 52) return true;
  // Small keys (53..65) and big keys (66..76) — progression for dungeon traversal.
  if (item_id >= 53 && item_id <= 76) return true;
  // Maps (77..87) and compasses (88..98) — NOT progression.
  // Multi-tier rupees (99..103) — NOT progression.
  // Junk (104..110) — NOT progression.
  // Prize pendants (111..113) and crystals (114..120) — progression
  // (gate Sahasrahla / Pedestal / Ganon's Tower / Ganon).
  if (item_id >= 111 && item_id <= 120) return true;
  // Virtual items (121+) — NOT in pool; not progression.
  return false;
}

// Fisher-Yates over a uint16 array, using Rng_NextRange for unbiased picks.
static void shuffle_u16(uint16 *arr, uint16 n, RandoRng *rng) {
  for (int i = (int)n - 1; i > 0; i--) {
    uint32 j = Rng_NextRange(rng, (uint32)(i + 1));
    uint16 tmp = arr[i];
    arr[i] = arr[(uint16)j];
    arr[(uint16)j] = tmp;
  }
}

// Evaluate can_place OR always_allow for a given (location, candidate_item) pair.
static bool location_accepts_item(const RandoLocationDef *loc,
                                  uint16 candidate_item,
                                  const RandoCounts *counts,
                                  const RandoSettings *settings) {
  // can_place defaults to TRUE() when not overridden — the bytecode for
  // the default is "AND with 0 children" (2 bytes 0x0c 0x00), which
  // Predicate_EvaluatePlacement evaluates as true.
  const uint8 *cp_bc = kRandoPredicateStream + loc->can_place_offset;
  if (Predicate_EvaluatePlacement(cp_bc, loc->can_place_length, counts, settings, candidate_item)) {
    return true;
  }
  // always_allow defaults to FALSE() (OR with 0 children, 0x0d 0x00) — also
  // evaluatable safely. If it returns true, the placement is permitted even
  // when can_place rejected.
  const uint8 *aa_bc = kRandoPredicateStream + loc->always_allow_offset;
  return Predicate_EvaluatePlacement(aa_bc, loc->always_allow_length, counts, settings, candidate_item);
}

// Single-attempt inner implementation of assumed-fill. Returns true on
// "every progression item placed at a reachable location" (success), false
// otherwise (forward-fill fallback fired at least once). The outer
// Place_AssumedFill wraps this with a bounded-retry loop.
static bool place_assumed_fill_attempt(const RandoSettings *settings,
                                       uint64 seed_u64,
                                       RandoPlacementTable *out,
                                       uint16 *out_fallback_count);

// ---------------------------------------------------------------------------
// Place_AssumedFill — Phase A1 implementation.
//
// Algorithm (per `randomizer-placement / Assumed-fill placement`):
//   1. Build the per-settings item pool via BuildItemPool.
//   2. Partition into progression (~70 items) vs junk (~165).
//   3. Shuffle progression order; "assumed inventory" = counts of all
//      progression items still unplaced.
//   4. For each progression item (in shuffled order):
//        a. Remove from assumed inventory.
//        b. Run Logic_ComputeReachability with current counts.
//        c. Filter open locations to reachable + can_place(item).
//        d. If candidates: pick a random one (Rng_NextRange).
//        e. If no candidates: forward-fill fallback.
//   5. Shuffle junk; fill remaining open locations.
//   6. Run sphere computation against the produced placement; if any
//      progression item is unreachable, retry with a perturbed seed.
//   7. After kAssumedFillMaxAttempts retries, accept the last attempt and
//      surface a fallback warning. (Aligns with the spec's "forward-fill
//      fallback after timeout" — bounded-rewind is the in-attempt retry
//      strategy; this is the cross-attempt retry strategy.)
// ---------------------------------------------------------------------------
#define kAssumedFillMaxAttempts 8

bool Place_AssumedFill(const RandoSettings *settings,
                       uint64 seed_u64,
                       int budget_seconds,
                       RandoPlacementTable *out) {
  (void)budget_seconds;
  if (settings == NULL || out == NULL || out->entries == NULL) return false;

  uint16 best_unreachable = 0xFFFF;
  uint16 best_fallback = 0xFFFF;
  static RandoPlacement best_entries[512];
  uint16 best_count = 0;
  bool best_complete = false;

  for (int attempt = 0; attempt < kAssumedFillMaxAttempts; attempt++) {
    // Perturb the seed per attempt so each retry produces a different
    // progression order. The first attempt uses the unmodified seed so
    // single-seed determinism holds when the first attempt succeeds.
    uint64 attempt_seed = seed_u64 ^ ((uint64)attempt * 0x9E3779B97F4A7C15ull);
    uint16 fallback_count = 0;
    if (!place_assumed_fill_attempt(settings, attempt_seed, out, &fallback_count)) {
      // Inner attempt itself failed catastrophically (couldn't place an
      // item even with forward-fill). Continue retrying with a new seed.
      continue;
    }
    // Check completability via sphere computation.
    RandoSpheres spheres;
    bool full_reach = Logic_ComputeSpheres(settings, out, &spheres);
    if (full_reach && fallback_count == 0) {
      // Best possible outcome — accept this placement.
      return true;
    }
    // Track best-so-far (fewest unreachable + fewest fallbacks).
    uint16 score = spheres.unreachable_count * 100 + fallback_count;
    uint16 best_score = (best_unreachable == 0xFFFF) ? 0xFFFF : (best_unreachable * 100 + best_fallback);
    if (score < best_score) {
      best_unreachable = spheres.unreachable_count;
      best_fallback = fallback_count;
      best_count = out->count;
      for (uint16 i = 0; i < out->count; i++) best_entries[i] = out->entries[i];
      best_complete = full_reach;
    }
  }
  // All attempts exhausted; restore the best-scored placement.
  if (best_unreachable == 0xFFFF) {
    fprintf(stderr, "Place_AssumedFill: no attempt produced a placement\n");
    return false;
  }
  for (uint16 i = 0; i < best_count; i++) out->entries[i] = best_entries[i];
  out->count = best_count;
  fprintf(stderr,
          "Place_AssumedFill: best of %d attempts: %u unreachable placement(s), %u forward-fill fallback(s).\n",
          kAssumedFillMaxAttempts, (unsigned)best_unreachable, (unsigned)best_fallback);
  // Return true to signal "a placement was produced" — the caller checks
  // goal_completable / sphere unreachable_count to decide whether the seed
  // is actually playable. (best_complete may be false; we still keep the
  // best-scored output so the spoiler surfaces diagnostic info.)
  (void)best_complete;
  return true;
}

static bool place_assumed_fill_attempt(const RandoSettings *settings,
                                       uint64 seed_u64,
                                       RandoPlacementTable *out,
                                       uint16 *out_fallback_count) {
  if (settings == NULL || out == NULL || out->entries == NULL) return false;
  if (out_fallback_count) *out_fallback_count = 0;

  // ----- 1. Build pool + shuffle-assignment tables -----
  uint16 pool[512];
  uint16 pool_n = BuildItemPool(settings, pool, 512);
  if (pool_n == 0) return false;

  // Run prize + medallion shuffles. Their outputs are stored as static state
  // that Logic_ComputeReachability picks up via Rando_GetDungeonPrizeAssignment
  // / Rando_GetMedallionAssignment. The placer needs these BEFORE running
  // reachability, otherwise OP_HAS_PRIZE / OP_MEDALLION_OPENS evaluate to
  // false everywhere and most of the graph is unreachable.
  //
  // Note: this state is process-global, which is fine here because the CLI
  // generates one seed at a time. Multi-seed batch generation (task 1.6a
  // batch form) would need to re-install per iteration.
  static uint8 prize_assignment[kRandoDungeonCount];
  static uint8 medallion_assignment[kRandoMedallionEntranceCount];
  {
    // Use a dedicated RNG seeded from seed_u64 so the shuffles are
    // deterministic per (seed, settings) — independent of the placer's RNG
    // consumption order.
    RandoRng shuffle_rng;
    Rng_SeedFromU64(&shuffle_rng, seed_u64);
    PrizeShuffle_Run(settings, &shuffle_rng, prize_assignment);
    MedallionShuffle_Run(settings, &shuffle_rng, medallion_assignment);
  }
  Rando_SetDungeonPrizeAssignment(prize_assignment);
  Rando_SetMedallionAssignment(medallion_assignment);

  // Dungeon → Prize location id, for prize-shuffle placement. Indexed by
  // dungeon id (HCE=0..GT=12). 0xFFFF = no Prize location for that dungeon.
  static const uint16 kDungeonPrizeLocations[13] = {
    0xFFFF,  // HCE
    17,      // EP Prize
    24,      // DP Prize
    31,      // TH Prize
    0xFFFF,  // HCT (Aga 1 is at 34, Prize_Event pinned separately)
    49,      // PoD Prize
    60,      // SP Prize
    69,      // SW Prize
    78,      // TT Prize
    87,      // IP Prize
    96,      // MM Prize
    109,     // TR Prize
    0xFFFF,  // GT (Aga 2 is at 137, Prize_Event pinned separately)
  };

  // ----- 2. Partition into progression / junk -----
  static uint16 progression[256];
  static uint16 junk[512];
  uint16 prog_n = 0, junk_n = 0;
  for (uint16 i = 0; i < pool_n; i++) {
    if (is_progression_item(pool[i])) {
      if (prog_n < sizeof(progression) / sizeof(progression[0])) progression[prog_n++] = pool[i];
    } else {
      if (junk_n < sizeof(junk) / sizeof(junk[0])) junk[junk_n++] = pool[i];
    }
  }

  // ----- 3. Collect open locations (world-state-filtered, sorted by id) -----
  // open_loc_idx[k] = index into kRandoLocations of the k-th open location.
  static uint16 open_loc_idx[512];
  uint16 open_n = 0;
  for (uint32 i = 0; i < kRandoLocationsCount; i++) {
    const RandoLocationDef *loc = &kRandoLocations[i];
    if (loc->world_state_filter != 0 &&
        !(loc->world_state_filter & (1u << settings->world_state))) continue;
    open_loc_idx[open_n++] = (uint16)i;
  }

  // placement_at[k] = item placed at open_loc_idx[k], or 0xFFFF if empty.
  static uint16 placement_at[512];
  for (uint16 k = 0; k < open_n; k++) placement_at[k] = 0xFFFF;

  // ----- 3b. Pre-place vanilla-mode dungeon items + event-prize pins -----
  //
  // Several location types are NOT subject to assumed-fill randomization:
  //
  //   1. Prize_Event (Zelda, Agahnim, Agahnim 2, Ganon, Bomb Merchant
  //      in Inverted): these are virtual event triggers. Their vanilla
  //      item (RescuedZelda / DefeatAgahnim / etc.) MUST be pinned so
  //      sphere computation sees the event item enter inventory when the
  //      location is reached — otherwise downstream regions that gate
  //      on the event are forever unreachable.
  //
  //   2. Medallion config locations (Misery Mire Medallion / Turtle Rock
  //      Medallion): these are read by the medallion-shuffle module, not
  //      placed. For now pin to vanilla (matches medallion_shuffle=0 case).
  //
  //   3. In Vanilla dungeon-item mode (the default), small keys / big keys /
  //      maps / compasses belong at their vanilla locations. Pin them here
  //      before assumed fill runs, otherwise junk fills those slots and
  //      dungeon locks become unopenable.
  //
  // Per-class mode lookup: settings.dungeon_{small_keys,big_keys,maps,compasses}_mode.
  // Item id ranges (per item_registry.yaml):
  //   53..65 = small keys, 66..76 = big keys, 77..87 = maps (plus id 124 = Map_HCE),
  //   88..98 = compasses.
  // Location type ids: Prize_Event = 12, Prize_Pendant = 11, Prize_Crystal = 10,
  // Medallion = 13 (per logic.schema.yaml).
  const uint8 LOCTYPE_Prize_Crystal = 10;
  const uint8 LOCTYPE_Prize_Pendant = 11;
  const uint8 LOCTYPE_Prize_Event   = 12;
  const uint8 LOCTYPE_Medallion     = 13;
  for (uint16 k = 0; k < open_n; k++) {
    const RandoLocationDef *loc = &kRandoLocations[open_loc_idx[k]];
    uint16 vi = loc->vanilla_item_id;
    bool vanilla_pin = false;
    if (loc->type == LOCTYPE_Prize_Event || loc->type == LOCTYPE_Medallion) {
      // Always pin event / medallion locations to vanilla item.
      vanilla_pin = true;
    } else if (loc->type == LOCTYPE_Prize_Pendant || loc->type == LOCTYPE_Prize_Crystal) {
      // Pin per the prize-shuffle assignment. Find the dungeon whose Prize
      // location matches this slot, then look up the assigned prize id and
      // map it to the item-registry id (+111 offset).
      for (uint8 d = 0; d < kRandoDungeonCount; d++) {
        if (kDungeonPrizeLocations[d] == 0xFFFF) continue;
        if (kDungeonPrizeLocations[d] != loc->id) continue;
        uint8 prize_id = prize_assignment[d];
        if (prize_id < kRandoPrizeCount) {
          // prize_id 0..9 (Green/Red/Blue pendants + Crystal1..7) → item id 111..120
          placement_at[k] = 111 + prize_id;
          break;
        }
      }
      // If prize_shuffle is disabled, prize_assignment[d] is the vanilla
      // identity; the lookup above still resolves correctly. Locations not
      // in kDungeonPrizeLocations (shouldn't happen for Prize_* types) fall
      // through to the assumed-fill placer.
      continue;
    } else if (vi >= 53 && vi <= 65) {
      vanilla_pin = (settings->dungeon_small_keys_mode == kDungeonItemMode_Vanilla);
    } else if (vi >= 66 && vi <= 76) {
      vanilla_pin = (settings->dungeon_big_keys_mode == kDungeonItemMode_Vanilla);
    } else if ((vi >= 77 && vi <= 87) || vi == 124) {
      vanilla_pin = (settings->dungeon_maps_mode == kDungeonItemMode_Vanilla);
    } else if (vi >= 88 && vi <= 98) {
      vanilla_pin = (settings->dungeon_compasses_mode == kDungeonItemMode_Vanilla);
    }
    if (vanilla_pin) {
      placement_at[k] = vi;
    }
  }

  // ----- 4. Seed the assumed inventory with all progression items -----
  RandoRng rng;
  Rng_SeedFromU64(&rng, seed_u64);

  RandoCounts counts;
  memset(&counts, 0, sizeof(counts));
  counts.by_item_id[121] = 3;  // StartingHeart virtual item (id 121, count 3)
  // Per-world-state pre-collected virtual items — mirrors ALTTPR's
  // `World::pre_collected_items` mechanism. In Open / Inverted / Retro the
  // player begins with RescuedZelda already granted (the sanctuary escort
  // is skipped); in Standard the player earns it via HCE. (id 122 per
  // item_registry.yaml.)
  if (settings->world_state != kWorldState_Standard) {
    counts.by_item_id[122] = 1;
  }
  for (uint16 i = 0; i < prog_n; i++) {
    counts.by_item_id[progression[i]]++;
  }
  // Add pre-placed vanilla dungeon items to the assumed inventory so
  // reachability evaluates as if the player will collect them in-place.
  for (uint16 k = 0; k < open_n; k++) {
    if (placement_at[k] == 0xFFFF) continue;
    uint16 vi = placement_at[k];
    if (vi < 256) counts.by_item_id[vi]++;
  }

  // Shuffle the progression placement order.
  shuffle_u16(progression, prog_n, &rng);

  // ----- 5. Place each progression item -----
  uint16 fallback_count = 0;
  for (uint16 i = 0; i < prog_n; i++) {
    uint16 item = progression[i];
    // Remove from assumed inventory before computing reachability.
    if (counts.by_item_id[item] > 0) counts.by_item_id[item]--;

    const RandoReachability *r = Logic_ComputeReachability(&counts, settings);

    // Find candidate locations: open + reachable + accepts item.
    static uint16 candidates[512];
    uint16 cand_n = 0;
    for (uint16 k = 0; k < open_n; k++) {
      if (placement_at[k] != 0xFFFF) continue;
      const RandoLocationDef *loc = &kRandoLocations[open_loc_idx[k]];
      if (r != NULL && !Reachability_HasLocation(r, loc->id)) continue;
      if (!location_accepts_item(loc, item, &counts, settings)) continue;
      candidates[cand_n++] = k;
    }

    if (cand_n > 0) {
      uint32 pick = Rng_NextRange(&rng, cand_n);
      placement_at[candidates[pick]] = item;
    } else {
      // Forward-fill fallback: place at any open + can_place location
      // (ignore reachability). If still nothing, take the first open slot
      // regardless of can_place — last-resort recovery.
      cand_n = 0;
      for (uint16 k = 0; k < open_n; k++) {
        if (placement_at[k] != 0xFFFF) continue;
        const RandoLocationDef *loc = &kRandoLocations[open_loc_idx[k]];
        if (!location_accepts_item(loc, item, &counts, settings)) continue;
        candidates[cand_n++] = k;
      }
      if (cand_n == 0) {
        for (uint16 k = 0; k < open_n; k++) {
          if (placement_at[k] != 0xFFFF) continue;
          candidates[cand_n++] = k;
        }
      }
      if (cand_n == 0) {
        fprintf(stderr, "Place_AssumedFill: no open location for progression item %u\n",
                (unsigned)item);
        return false;
      }
      uint32 pick = Rng_NextRange(&rng, cand_n);
      placement_at[candidates[pick]] = item;
      fallback_count++;
    }
  }

  // ----- 6. Fill remaining open locations with junk -----
  shuffle_u16(junk, junk_n, &rng);
  uint16 j_idx = 0;
  for (uint16 k = 0; k < open_n; k++) {
    if (placement_at[k] != 0xFFFF) continue;
    if (j_idx < junk_n) {
      placement_at[k] = junk[j_idx++];
    } else {
      // Pool/locations cardinality mismatch — fall back to vanilla item.
      placement_at[k] = kRandoLocations[open_loc_idx[k]].vanilla_item_id;
    }
  }

  // ----- 7. Emit placement table -----
  for (uint16 k = 0; k < open_n; k++) {
    out->entries[k].location_id = kRandoLocations[open_loc_idx[k]].id;
    out->entries[k].item_id = placement_at[k];
  }
  out->count = open_n;

  if (out_fallback_count) *out_fallback_count = fallback_count;
  return true;
}

// ---------------------------------------------------------------------------
// PlacementTable_ComputeDigest — SHA-256 over a canonical-LE serialization.
//
// Canonical serialization: for each (location_id, item_id) pair in
// location_id-sorted order, emit 4 bytes: <location_id:u16_le> <item_id:u16_le>.
// The result is fed to SHA-256.
//
// Determinism contract: the input order is sorted by location_id, so the
// digest is invariant under any internal reordering during placement. This is
// the source of truth for the regression corpus (tasks.md §12.1).
// ---------------------------------------------------------------------------
static int placement_cmp(const void *a, const void *b) {
  const RandoPlacement *pa = a;
  const RandoPlacement *pb = b;
  if (pa->location_id < pb->location_id) return -1;
  if (pa->location_id > pb->location_id) return 1;
  return 0;
}

void PlacementTable_ComputeDigest(const RandoPlacementTable *t, uint8 out_digest[32]) {
  if (t == NULL || t->entries == NULL || t->count == 0) {
    // Hash of empty input is well-defined (SHA-256 of zero bytes).
    sha256_buffer((const uint8 *)"", 0, out_digest);
    return;
  }
  // Copy entries into a local buffer and sort by location_id.
  // Phase A worst case is ~250 entries; sorting in place is fine.
  RandoPlacement local[256];
  uint16 n = t->count;
  if (n > 256) n = 256;
  memcpy(local, t->entries, n * sizeof(RandoPlacement));
  qsort(local, n, sizeof(RandoPlacement), placement_cmp);

  // Serialize: 4 bytes per entry, little-endian.
  uint8 buf[256 * 4];
  for (uint16 i = 0; i < n; i++) {
    buf[i * 4 + 0] = local[i].location_id & 0xff;
    buf[i * 4 + 1] = local[i].location_id >> 8;
    buf[i * 4 + 2] = local[i].item_id & 0xff;
    buf[i * 4 + 3] = local[i].item_id >> 8;
  }
  sha256_buffer(buf, n * 4, out_digest);
}

// ---------------------------------------------------------------------------
// Goal completability (tasks.md §3.9). Run after Place_AssumedFill to verify
// the placement is winnable for the active goal.
// ---------------------------------------------------------------------------

// Build a final-state inventory from a placement table: count[item_id] is the
// number of times that item appears across all placements. Pre-populated with
// counts[StartingHeart] = 3.
static void build_final_inventory(const RandoPlacementTable *t, RandoCounts *out) {
  memset(out, 0, sizeof(*out));
  out->by_item_id[121] = 3;  // StartingHeart
  if (t == NULL) return;
  for (uint16 i = 0; i < t->count; i++) {
    uint16 item_id = t->entries[i].item_id;
    if (item_id < 256 && out->by_item_id[item_id] < 0xFFFF) {
      out->by_item_id[item_id]++;
    }
  }
}

// Lookup a location_id in the placement table; returns the placed item or
// 0xFFFF if not found.
static uint16 placement_at_location(const RandoPlacementTable *t, uint16 loc_id) {
  if (t == NULL) return 0xFFFF;
  for (uint16 i = 0; i < t->count; i++) {
    if (t->entries[i].location_id == loc_id) return t->entries[i].item_id;
  }
  return 0xFFFF;
}

// Count reachable placements that hold a specific item id.
static uint16 count_reachable_placements_of(const RandoPlacementTable *t,
                                            const RandoReachability *r,
                                            uint16 item_id) {
  if (t == NULL || r == NULL) return 0;
  uint16 n = 0;
  for (uint16 i = 0; i < t->count; i++) {
    if (t->entries[i].item_id != item_id) continue;
    if (Reachability_HasLocation(r, t->entries[i].location_id)) n++;
  }
  return n;
}

// Special location IDs frequently checked by goal predicates. These match
// location_registry.yaml ordering (kept in sync — codegen will eventually
// emit named LOC_* macros for this, but for Phase A1 the IDs are stable).
#define LOC_ID_MasterSwordPedestal 151
#define LOC_ID_Ganon               212
#define LOC_ID_Agahnim2            137

// Pendant item ids (matches item_registry.yaml).
#define ITEM_ID_GreenPendant 111
#define ITEM_ID_RedPendant   112
#define ITEM_ID_BluePendant  113
#define ITEM_ID_TriforcePiece 52

bool Goal_IsCompletable(const RandoSettings *settings,
                        const RandoPlacementTable *placements) {
  if (settings == NULL || placements == NULL) return false;

  RandoCounts final_inv;
  build_final_inventory(placements, &final_inv);
  const RandoReachability *r = Logic_ComputeReachability(&final_inv, settings);
  if (r == NULL) {
    fprintf(stderr, "Goal_IsCompletable: reachability returned NULL (empty graph?)\n");
    return false;
  }

  switch (settings->goal) {
    case kGoal_Ganon: {
      // Need: Ganon vulnerable (≥ crystals.ganon crystals collected from
      // REACHABLE locations) AND Agahnim 2 cleared AND Ganon reachable.
      uint16 reachable_crystals = 0;
      for (uint16 cid = 114; cid <= 120; cid++) {
        reachable_crystals += count_reachable_placements_of(placements, r, cid);
      }
      if (reachable_crystals < settings->crystals_ganon) {
        fprintf(stderr, "Goal_IsCompletable(ganon): %u reachable crystals, need %u\n",
                (unsigned)reachable_crystals, (unsigned)settings->crystals_ganon);
        return false;
      }
      if (!Reachability_HasLocation(r, LOC_ID_Agahnim2)) {
        fprintf(stderr, "Goal_IsCompletable(ganon): Agahnim 2 unreachable\n");
        return false;
      }
      if (!Reachability_HasLocation(r, LOC_ID_Ganon)) {
        fprintf(stderr, "Goal_IsCompletable(ganon): Ganon unreachable\n");
        return false;
      }
      return true;
    }
    case kGoal_FastGanon: {
      // Per spec scenario "Fast Ganon gates on crystals and tower access":
      // require crystals.ganon crystals for vulnerability AND Ganon reachable
      // (GT entry edge implicitly requires crystals.tower).
      uint16 reachable_crystals = 0;
      for (uint16 cid = 114; cid <= 120; cid++) {
        reachable_crystals += count_reachable_placements_of(placements, r, cid);
      }
      if (reachable_crystals < settings->crystals_ganon) {
        fprintf(stderr, "Goal_IsCompletable(fast_ganon): %u reachable crystals, need %u\n",
                (unsigned)reachable_crystals, (unsigned)settings->crystals_ganon);
        return false;
      }
      if (!Reachability_HasLocation(r, LOC_ID_Ganon)) {
        fprintf(stderr, "Goal_IsCompletable(fast_ganon): Ganon unreachable\n");
        return false;
      }
      return true;
    }
    case kGoal_Dungeons:
      // Every dungeon's _Boss location reachable. Dungeon-boss IDs match
      // location_registry: EP=16, DP=23, TH=30, PoD=48, SP=59, SW=68, TT=77,
      // IP=86, MM=95, TR=108, plus HCT=34 (Agahnim) and GT=137 (Agahnim 2).
      {
        const uint16 boss_locs[] = {16, 23, 30, 34, 48, 59, 68, 77, 86, 95, 108, 137};
        for (size_t i = 0; i < sizeof(boss_locs) / sizeof(boss_locs[0]); i++) {
          if (!Reachability_HasLocation(r, boss_locs[i])) {
            fprintf(stderr, "Goal_IsCompletable(dungeons): boss location %u unreachable\n",
                    (unsigned)boss_locs[i]);
            return false;
          }
        }
      }
      return true;
    case kGoal_Pedestal:
      if (!Reachability_HasLocation(r, LOC_ID_MasterSwordPedestal)) {
        fprintf(stderr, "Goal_IsCompletable(pedestal): Master Sword Pedestal unreachable\n");
        return false;
      }
      if (final_inv.by_item_id[ITEM_ID_GreenPendant] == 0 ||
          final_inv.by_item_id[ITEM_ID_RedPendant] == 0 ||
          final_inv.by_item_id[ITEM_ID_BluePendant] == 0) {
        fprintf(stderr, "Goal_IsCompletable(pedestal): missing pendant in placement\n");
        return false;
      }
      return true;
    case kGoal_TriforceHunt: {
      uint16 reachable_pieces = count_reachable_placements_of(placements, r, ITEM_ID_TriforcePiece);
      if (reachable_pieces < settings->pieces_required) {
        fprintf(stderr, "Goal_IsCompletable(triforce-hunt): %u reachable pieces, need %u\n",
                (unsigned)reachable_pieces, (unsigned)settings->pieces_required);
        return false;
      }
      if (!Reachability_HasLocation(r, LOC_ID_MasterSwordPedestal)) {
        fprintf(stderr, "Goal_IsCompletable(triforce-hunt): Pedestal unreachable\n");
        return false;
      }
      return true;
    }
    case kGoal_GanonHunt: {
      uint16 reachable_pieces = count_reachable_placements_of(placements, r, ITEM_ID_TriforcePiece);
      if (reachable_pieces < settings->pieces_required) {
        fprintf(stderr, "Goal_IsCompletable(ganonhunt): %u reachable pieces, need %u\n",
                (unsigned)reachable_pieces, (unsigned)settings->pieces_required);
        return false;
      }
      if (!Reachability_HasLocation(r, LOC_ID_Ganon)) {
        fprintf(stderr, "Goal_IsCompletable(ganonhunt): Ganon unreachable\n");
        return false;
      }
      return true;
    }
    case kGoal_Completionist:
      for (uint16 i = 0; i < placements->count; i++) {
        if (!Reachability_HasLocation(r, placements->entries[i].location_id)) {
          fprintf(stderr,
                  "Goal_IsCompletable(completionist): location %u unreachable\n",
                  (unsigned)placements->entries[i].location_id);
          return false;
        }
      }
      return true;
    default:
      fprintf(stderr, "Goal_IsCompletable: unknown goal %u\n", (unsigned)settings->goal);
      return false;
  }
}

// ---------------------------------------------------------------------------
// Sphere computation (randomizer-core / Sphere semantics).
//
// Walks the placement table by sphere:
//   Sphere 0: starting inventory only.
//   Sphere N+1: locations newly reachable under inventory accumulated from
//               spheres 0..N (items from those placements).
//
// On success: every placement gets an assigned sphere. On failure: any
// unreachable placement keeps sphere_index = kSphereIndexUnreachable.
// ---------------------------------------------------------------------------
bool Logic_ComputeSpheres(const RandoSettings *settings,
                          const RandoPlacementTable *placements,
                          RandoSpheres *out) {
  if (settings == NULL || placements == NULL || out == NULL) return false;
  memset(out, 0, sizeof(*out));
  for (uint16 i = 0; i < (uint16)(sizeof(out->sphere_index_by_placement) / sizeof(out->sphere_index_by_placement[0])); i++) {
    out->sphere_index_by_placement[i] = kSphereIndexUnreachable;
  }

  // Build the running inventory: starts as defaults (StartingHeart + world-
  // state pre-grants) and accumulates items from each completed sphere.
  RandoCounts counts;
  memset(&counts, 0, sizeof(counts));
  counts.by_item_id[121] = 3;  // StartingHeart
  if (settings->world_state != kWorldState_Standard) {
    counts.by_item_id[122] = 1;  // RescuedZelda pre-grant
  }

  uint16 remaining = placements->count;
  uint8 sphere = 0;
  while (remaining > 0 && sphere < kSphereMaxCount) {
    const RandoReachability *r = Logic_ComputeReachability(&counts, settings);
    uint16 added_this_sphere = 0;
    for (uint16 i = 0; i < placements->count; i++) {
      if (out->sphere_index_by_placement[i] != kSphereIndexUnreachable) continue;
      uint16 loc_id = placements->entries[i].location_id;
      if (r != NULL && Reachability_HasLocation(r, loc_id)) {
        out->sphere_index_by_placement[i] = sphere;
        added_this_sphere++;
      }
    }
    if (added_this_sphere == 0) {
      // Fixed point — remaining placements are unreachable.
      break;
    }
    // Accumulate items from this sphere into the running inventory.
    for (uint16 i = 0; i < placements->count; i++) {
      if (out->sphere_index_by_placement[i] != sphere) continue;
      uint16 item = placements->entries[i].item_id;
      if (item < 256 && counts.by_item_id[item] < 0xFFFF) {
        counts.by_item_id[item]++;
      }
    }
    out->max_sphere = sphere;
    remaining -= added_this_sphere;
    sphere++;
  }

  // Count unreachable placements.
  out->unreachable_count = 0;
  for (uint16 i = 0; i < placements->count; i++) {
    if (out->sphere_index_by_placement[i] == kSphereIndexUnreachable) {
      out->unreachable_count++;
    }
  }
  return out->unreachable_count == 0;
}

// ---------------------------------------------------------------------------
// Starting-inventory injection (tasks.md §4.2).
//
// Phase A1 implementation: world-state-aware starting inventory.
//
// Per world-state:
//   - Open / Retro: Link starts with no extra items (uncle's slot still places
//     the chosen item; collected on encounter — distinct from Standard mode).
//   - Standard: Link starts swordless (link_sword_type = 0); the uncle's slot
//     determines what becomes the starting item once collected. No injection
//     here — the dispatcher at the uncle's grant site grants the placed item.
//   - Inverted: Link starts with the Moon Pearl as a virtual starting item
//     (since the inverted dark-world traversal does not require it; ALTTPR
//     pre-collects to keep the placement pool's MoonPearl meaningful at the
//     pendant logic gate). MagicMirror is similarly pre-collected per
//     `app/Region/Inverted/`. (Phase B note: confirm exact pre-collected
//     set against ALTTPR's `pre_collected_items` config when finalizing.)
//
// Atomicity: the dispatch calls below all run in the same call frame; no
// I/O happens between them.
// ---------------------------------------------------------------------------

// link_item_* RAM cells (offsets per features.h / variables.h).
// We can't include variables.h cleanly from src/rando/ without pulling in
// game-state headers; use the same address-by-offset pattern as features.h
// and just call the dispatcher helpers exposed by Phase A1 receive paths.
//
// For Phase A1 starting inventory, we set the simplest items directly via
// the existing dispatcher (Link_ReceiveItem). That dispatcher is in
// src/misc.c and is the same one §6 grant sites use.

extern void Link_ReceiveItem(uint8 item, int chest_position);

// Returns the RAM address of kRam_RandoStartingInventoryGranted to allow the
// injection to read/write the gate cell without pulling in the macro defs.
#include "../features.h"
extern uint8 g_ram[];

bool Rando_TryGrantStartingInventory(const RandoSettings *settings) {
  if (settings == NULL) return false;
  if (g_rando_slot_active == 0) return false;
  if (g_rando_starting_inventory_granted != 0) return false;

  // Inverted: pre-grant Moon Pearl + Magic Mirror equivalents.
  if (settings->world_state == kWorldState_Inverted) {
    Link_ReceiveItem(0x1f, 0);  // Moon Pearl (registry id 39, vanilla dispatch 0x1f)
    Link_ReceiveItem(0x1a, 0);  // Magic Mirror (registry id 36, vanilla dispatch 0x1a)
  }

  // Standard: no pre-grant (uncle's slot handles it via §6 dispatch).
  // Open / Retro: no pre-grant by default; future hero-mode (Phase B) may
  // pre-grant a starting bottle, hearts, etc.

  // Mark gate so save-reload does not re-inject. Per §4.2 atomicity, this
  // SHALL happen in the same frame as the grants above.
  g_rando_starting_inventory_granted = 1;
  return true;
}

// ---------------------------------------------------------------------------
// Self-check
// ---------------------------------------------------------------------------
static void selfcheck_die(const char *msg) {
  fprintf(stderr, "[Placement_SelfCheck] FAIL: %s\n", msg);
  exit(2);
}

void Placement_SelfCheck(void) {
  // Build identity placement, compute digest, assert digest stable across
  // sort-order perturbations of input.
  RandoPlacement entries[3] = {
    { 100, 5 },
    { 50,  10 },
    { 200, 15 },
  };
  RandoPlacementTable t = { entries, 3 };
  uint8 digest1[32];
  PlacementTable_ComputeDigest(&t, digest1);

  // Same entries in different order → same digest (sort invariance).
  RandoPlacement entries2[3] = {
    { 200, 15 },
    { 50,  10 },
    { 100, 5 },
  };
  RandoPlacementTable t2 = { entries2, 3 };
  uint8 digest2[32];
  PlacementTable_ComputeDigest(&t2, digest2);

  if (memcmp(digest1, digest2, 32) != 0) {
    selfcheck_die("placement digest not sort-invariant");
  }

  // Changing one item changes the digest.
  entries[0].item_id = 99;
  uint8 digest3[32];
  PlacementTable_ComputeDigest(&t, digest3);
  if (memcmp(digest1, digest3, 32) == 0) {
    selfcheck_die("placement digest collision on different items");
  }

  // BuildItemPool: with default settings, pool size equals kRandoLocationsCount
  // (junk-padded). With NULL settings, the function safely returns 0.
  {
    RandoSettings defaults;
    Settings_SetDefaults(&defaults);
    uint16 pool[512];
    uint16 n = BuildItemPool(&defaults, pool, 512);
    if (n != kRandoLocationsCount) {
      fprintf(stderr, "[Placement_SelfCheck] BuildItemPool returned %u, expected %u\n",
              (unsigned)n, (unsigned)kRandoLocationsCount);
      selfcheck_die("BuildItemPool count mismatch");
    }
    // NULL settings → 0 (safe rejection, not crash).
    uint16 n_null = BuildItemPool(NULL, pool, 512);
    if (n_null != 0) selfcheck_die("BuildItemPool(NULL) should return 0");
  }

  fprintf(stderr, "[Placement_SelfCheck] OK\n");
}
