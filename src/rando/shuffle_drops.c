// shuffle_drops.c — Phase B Slice 8 (add-rando-shuffles-and-minigames) scaffold.
//
// See shuffle_drops.h. STUB: identity mapping.

#include "shuffle_drops.h"

bool DropShuffle_Generate(const RandoSettings *settings,
                          uint64 seed_u64,
                          const RandoPlacementTable *placements,
                          const RandoSpheres *spheres) {
  (void)settings;
  (void)seed_u64;
  (void)placements;
  (void)spheres;
  // Stub: no shuffle. Real impl: deterministic permutation of the drop
  // pool with heart-drop-floor preservation.
  return true;
}

uint8 DropShuffle_Lookup(uint8 drop_table_index) {
  // Identity: every lookup returns its input (vanilla behavior).
  return drop_table_index;
}
