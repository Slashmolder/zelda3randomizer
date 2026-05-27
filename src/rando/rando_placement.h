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

// Per-run placer stats from the most recent Place_AssumedFill call.
// Cleared at the start of every Place_AssumedFill call, populated before
// it returns. Read by the spoiler writer to populate fallback_warnings.
typedef struct PlacementStats {
  uint16 forward_fill_fallback_count;  // # of placements that fell to forward-fill
  uint8 attempts_used;                 // # of assumed-fill attempts taken
  uint16 best_unreachable_count;       // unreachable placements in the accepted attempt
} PlacementStats;
const PlacementStats *Placement_GetLastStats(void);

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
// Goal completability (tasks.md §3.9, randomizer-logic / Goal-completion).
//
// After Place_AssumedFill returns, the spoiler writer calls this to set the
// `goal_completable` field. The conservative semantics: with the full
// placement-table inventory in hand, the locations the active goal demands
// are reachable.
//
// Phase A1 implementation:
//   - Fast Ganon / Ganon: every dungeon required for the configured
//     crystals.ganon count is reachable AND the Ganon prize-event location
//     (and Agahnim 2 for goal=ganon) is reachable.
//   - All Dungeons: every dungeon-boss location is reachable.
//   - Pedestal: Master Sword Pedestal location is reachable AND inventory
//     contains the 3 pendants.
//   - Triforce Hunt: at least `pieces_required` TriforcePiece items are
//     placed at reachable locations, and the Pedestal location is reachable.
//   - Ganon Hunt: Triforce Hunt's piece requirement + the Ganon location.
//   - Completionist: every location in the placement table is reachable.
//
// Returns true when the goal is reachable, false otherwise. Logs a one-line
// reason to stderr on false.
// ---------------------------------------------------------------------------
bool Goal_IsCompletable(const RandoSettings *settings,
                        const RandoPlacementTable *placements);

// Should the generator REFUSE this seed because the goal isn't reachable?
// Returns false (i.e., don't refuse) when settings.accessibility==None — the
// player explicitly opted in to a possibly-unwinnable seed. Otherwise
// returns `!Goal_IsCompletable`. The spoiler's `goal_completable` field
// is always the pure reachability predicate (Goal_IsCompletable); only
// generation refusal gates honor the accessibility=none opt-out.
bool Goal_ShouldRefuse(const RandoSettings *settings,
                       const RandoPlacementTable *placements);

// ---------------------------------------------------------------------------
// Sphere computation (tasks.md §5 / randomizer-core / Sphere semantics).
//
// Sphere 0 = locations reachable with the starting inventory alone.
// Sphere N+1 = locations newly reachable after collecting every item in
// spheres 0..N. The fixed-point terminates when no new sphere can be added.
//
// Locations whose placed items never become reachable get sphere_index =
// kSphereIndexUnreachable. The presence of any such location is a strong
// signal that the placement is broken (or the logic graph is incomplete).
// ---------------------------------------------------------------------------
#define kSphereIndexUnreachable 0xFF
#define kSphereMaxCount         32   // hard cap; logical depth is typically < 20

typedef struct RandoSpheres {
  // For each entry index in the placement table: which sphere it belongs to.
  // Indexed by placement-table index (0..placements->count-1).
  uint8 sphere_index_by_placement[512];
  uint8 max_sphere;                  // highest sphere reached (inclusive); 0 if no progress
  uint16 unreachable_count;          // count of placements with sphere_index_by_placement == 0xFF
} RandoSpheres;

// Compute sphere assignment for every placement. Returns true if every
// placement is assigned a sphere (placement is fully reachable); false if
// any placement is unreachable.
bool Logic_ComputeSpheres(const RandoSettings *settings,
                          const RandoPlacementTable *placements,
                          RandoSpheres *out);

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
