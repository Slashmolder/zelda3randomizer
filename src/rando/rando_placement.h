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

// Install the active placement table — subsequent Rando_OnLocationCheck calls
// consult this table. Pass NULL to clear (revert to pure pass-through, i.e.,
// rando mode inactive).
void Placement_Install(const RandoPlacementTable *t);
const RandoPlacementTable *Placement_GetActive(void);

// Looks up location_id in the active table. Returns vanilla_item_id when no
// table is installed or the location_id is missing. Called from
// Rando_OnLocationCheck (rando.c).
uint16 Placement_Lookup(uint16 location_id, uint16 vanilla_item_id);

// Self-check: digest stability under sort-order, BuildItemPool sanity. Exits 2
// on failure (per Settings_SelfCheck pattern).
void Placement_SelfCheck(void);

// ---------------------------------------------------------------------------
// Starting-inventory injection (tasks.md §4.2).
//
// Grants the per-world-state starting items exactly once per slot. The
// `kRam_RandoStartingInventoryGranted` gate ensures save-reload does NOT
// re-inject (per randomizer-placement / "Starting inventory injection").
//
// Per `randomizer-placement / Starting-inventory injection atomicity`: all
// per-item grants complete in the same ZeldaRunFrame call. Caller MUST NOT
// invoke ZeldaWriteSram between Try... and the flag being set; both happen
// in this function.
//
// Returns true if injection ran this call, false if it was already granted
// (no-op) or rando is inactive.
// ---------------------------------------------------------------------------
bool Rando_TryGrantStartingInventory(const RandoSettings *settings);

#endif  // ZELDA3_RANDO_PLACEMENT_H_
