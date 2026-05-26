// rando_logic.h — predicate VM and reachability (tasks.md §3.7-§3.9). Stub.
//
// The predicate VM evaluates the bytecode emitted by assets/rando_logic_gen.py
// from logic.yaml. Two evaluation entry points:
//   - Predicate_Evaluate(p, counts, settings) for can_reach
//   - Predicate_EvaluatePlacement(p, counts, settings, candidate_item)
//     for can_place (only context where OP_ITEM_IS may appear)
//
// Logic_ComputeReachability runs the fixed-point expansion over the static
// EdgeDef[] graph (consulting the RegionRemap overlay for Phase C entrance
// shuffle; identity in Phase A).

#ifndef ZELDA3_RANDO_LOGIC_H_
#define ZELDA3_RANDO_LOGIC_H_

#include "../types.h"

// Opaque types — concrete definitions live alongside the generated
// logic_data.c.
typedef struct RandoPredicate RandoPredicate;
typedef struct RandoSettings RandoSettings;
typedef struct RandoReachability RandoReachability;

// Inventory counts indexed by item id. Phase A array size is bounded by the
// item_registry.yaml entry count (currently ~70 items including the virtual
// StartingHeart entry).
typedef struct RandoCounts {
  uint16 by_item_id[128];
} RandoCounts;

// Phase A op IDs (tasks.md §3.4). Stable numeric assignments live in
// assets/rando/op_registry.yaml; this enum mirrors them for code consumers.
typedef enum {
  OP_HAS_ITEM = 1,
  OP_HAS_AMOUNT = 2,
  OP_HAS_ANY_OF = 3,
  OP_HAS_ANY_COUNT = 4,
  OP_WORLDSTATE_EQ = 5,
  OP_GOAL_EQ = 6,
  OP_GOAL_REQUIRES_DUNGEON = 7,
  OP_DUNGEON_CLEARED = 8,
  OP_REGION_REACHABLE = 9,
  OP_HAS_PRIZE = 10,
  OP_MEDALLION_OPENS = 11,
  OP_ITEM_IS = 12,
  OP_AND = 13,
  OP_OR = 14,
  OP_NOT = 15,
  // Phase B placeholders:
  OP_TRICK = 16,
  OP_DIFFICULTY_AT_LEAST = 17,
  OP_GLITCH_LEVEL_AT_LEAST = 18,
} RandoOp;

bool Predicate_Evaluate(const RandoPredicate *p,
                        const RandoCounts *counts,
                        const RandoSettings *settings);

bool Predicate_EvaluatePlacement(const RandoPredicate *p,
                                 const RandoCounts *counts,
                                 const RandoSettings *settings,
                                 uint16 candidate_item);

// Compute reachability for the current inventory + settings. Result is an
// opaque bitset queried via Reachability_HasLocation / Reachability_HasRegion.
// Memoized per-iteration internally; the caller invalidates with
// Rando_BumpReachabilityCounter (see rando.h).
const RandoReachability *Logic_ComputeReachability(const RandoCounts *counts,
                                                   const RandoSettings *settings);

bool Reachability_HasLocation(const RandoReachability *r, uint16 location_id);
bool Reachability_HasRegion(const RandoReachability *r, uint16 region_id);

#endif  // ZELDA3_RANDO_LOGIC_H_
