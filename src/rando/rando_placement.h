// rando_placement.h — assumed fill + dispatcher (tasks.md §4, §6.1). Stub.
//
// BuildItemPool constructs the per-settings item pool (progressive vs absolute
// per mode.weapons, dungeon-item modes, Triforce-Hunt padding, Rupoor
// injection in hard/expert, ...).
//
// Place_AssumedFill runs the assumed-fill algorithm with bounded-retry
// rewind + forward-fill fallback (per randomizer-placement spec).
//
// Rando_OnLocationCheck — see rando.h. The dispatcher reads the
// placement_table to resolve location_id → placed_item_id.

#ifndef ZELDA3_RANDO_PLACEMENT_H_
#define ZELDA3_RANDO_PLACEMENT_H_

#include "../types.h"
#include "rando_logic.h"

typedef struct RandoPlacement {
  uint16 location_id;
  uint16 item_id;
} RandoPlacement;

typedef struct RandoPlacementTable {
  RandoPlacement *entries;
  uint16 count;
} RandoPlacementTable;

// Build the per-settings item pool. Returns the number of items written.
// |out_items| must be sized to hold the maximum possible pool (~216 entries
// at Phase A worst case).
uint16 BuildItemPool(const RandoSettings *settings, uint16 *out_items, uint16 capacity);

// Assumed fill. Returns true on success; false if the budget was exhausted
// without producing a complete placement and the caller fell back to
// forward-fill. Either way, |out| is populated.
bool Place_AssumedFill(const RandoSettings *settings,
                       uint64 seed_u64,
                       int budget_seconds,
                       RandoPlacementTable *out);

// SHA-256 of the canonical-serialized placement table, for regression-corpus
// diffing (tasks.md §4.4).
void PlacementTable_ComputeDigest(const RandoPlacementTable *t, uint8 out_digest[32]);

#endif  // ZELDA3_RANDO_PLACEMENT_H_
