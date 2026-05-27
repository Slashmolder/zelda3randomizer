// shuffle_drops.h — Phase B Slice 8 (add-rando-shuffles-and-minigames) scaffold.
//
// Sprite-death drop-pool shuffle. Randomizes enemy-drop tables while
// preserving the heart-drop-survives-early-game guarantee (low-HP zones
// still drop some hearts). Runs AFTER item placement so sphere data is
// known; aggressive shuffles can be gated by sphere depth.
//
// Status: STUB. Per-table reassignment lands in the Phase B follow-up;
// this header pins the API.
//
// ALTTPR upstream: app/EnemyDrop.php.

#ifndef ZELDA3_RANDO_SHUFFLE_DROPS_H_
#define ZELDA3_RANDO_SHUFFLE_DROPS_H_

#include "../types.h"
#include "rando_placement.h"

// One-shot generation entry. Deterministic from (settings, seed_u64,
// placement). The placement table is needed so we can consult sphere data
// (early-sphere zones get conservative drop-table reshuffling; deep
// spheres can tolerate aggressive shuffles).
//
// Returns true on success.
bool DropShuffle_Generate(const RandoSettings *settings,
                          uint64 seed_u64,
                          const RandoPlacementTable *placements,
                          const RandoSpheres *spheres);

// Look up the (possibly shuffled) drop entry at index `drop_table_index`.
// Returns the vanilla drop when no shuffle is active.
uint8 DropShuffle_Lookup(uint8 drop_table_index);

#endif  // ZELDA3_RANDO_SHUFFLE_DROPS_H_
