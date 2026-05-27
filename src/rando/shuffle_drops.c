// shuffle_drops.c — Phase B Slice 8 (add-rando-shuffles-and-minigames).
//
// Phase B §64 lands the deterministic permutation algorithm for
// sprite-death drop tables. Per-site sprite-handler instrumentation
// (§65) is pending — until that lands, DropShuffle_Lookup is read-only
// API surface.
//
// ALTTPR upstream: app/EnemyDrop.php — drop pools for enemy classes,
// permuted per seed with a "heart-drop-survives-early-game" guarantee
// (low-HP zones keep at least one heart drop). For Phase A we
// implement the basic Fisher-Yates permutation; the heart-floor
// guarantee + sphere-aware ordering is a Phase B+ refinement.

#include "shuffle_drops.h"
#include "rando_settings.h"
#include "rando_rng.h"
#include <string.h>

// Drop-table indices — Phase A models 64 entries. Mapping to specific
// sprite-death drops is per-site (sprite_main.c instrumentation). The
// algorithm operates on indices; lookup translates index → drop entry.
#define kDropTableEntryCount 64

static uint8 g_drop_assignment[kDropTableEntryCount];
static bool g_drop_assignment_active = false;

// Fisher-Yates shuffle of `arr[0..n-1]` using `rng`.
static void fy_shuffle_u8(RandoRng *rng, uint8 *arr, uint32 n) {
  for (uint32 i = n; i > 1; i--) {
    uint32 j = Rng_NextRange(rng, i);
    uint8 t = arr[i - 1]; arr[i - 1] = arr[j]; arr[j] = t;
  }
}

bool DropShuffle_Generate(const RandoSettings *settings,
                          uint64 seed_u64,
                          const RandoPlacementTable *placements,
                          const RandoSpheres *spheres) {
  (void)placements;
  (void)spheres;

  // Identity mapping first.
  for (uint8 i = 0; i < kDropTableEntryCount; i++) {
    g_drop_assignment[i] = i;
  }

  if (settings == NULL || settings->drop_shuffle == 0) {
    // Off — identity. Marked active so lookup returns vanilla.
    g_drop_assignment_active = true;
    return true;
  }

  // Deterministic permutation seeded with a per-shuffle salt.
  RandoRng rng;
  // Salt distinguishes the drop-shuffle RNG stream from boss/etc.
  Rng_SeedFromU64(&rng, seed_u64 ^ 0xD0DDA1FFD0DDA1FFull);
  fy_shuffle_u8(&rng, g_drop_assignment, kDropTableEntryCount);

  // Phase B refinement (deferred): re-pin a heart drop somewhere in
  // the early-game pool so low-HP zones don't soft-lock. Today the
  // shuffle is unconstrained; per-site instrumentation can enforce
  // floor invariants when it lands.

  g_drop_assignment_active = true;
  return true;
}

uint8 DropShuffle_Lookup(uint8 drop_table_index) {
  if (!g_drop_assignment_active) return drop_table_index;
  if (drop_table_index >= kDropTableEntryCount) return drop_table_index;
  return g_drop_assignment[drop_table_index];
}
