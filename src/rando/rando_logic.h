// rando_logic.h — predicate VM and reachability (tasks.md §3.7-§3.9).
//
// The predicate VM evaluates the bytecode emitted by assets/rando_logic_gen.py
// from logic.yaml. Two evaluation entry points:
//   - Predicate_Evaluate(bc, len, counts, settings) for can_reach
//   - Predicate_EvaluatePlacement(bc, len, counts, settings, candidate_item)
//     for can_place (only context where OP_ITEM_IS may appear)
//
// Logic_ComputeReachability runs the fixed-point expansion over the static
// EdgeDef[] graph (consulting the RegionRemap overlay for Phase C entrance
// shuffle; identity in Phase A).
//
// Bytecode format (per assets/rando_logic_gen.py docstring):
//   HAS_ITEM       <op:u8> <item_id:u16_le>
//   HAS_AMOUNT     <op:u8> <item_id:u16_le> <n:u8>
//   HAS_ANY_OF     <op:u8> <count:u8> <id:u16_le>×count
//   HAS_ANY_COUNT  <op:u8> <count:u8> <id:u16_le>×count <n:u8>
//   WORLDSTATE_EQ  <op:u8> <state:u8>
//   GOAL_EQ        <op:u8> <goal:u8>
//   GOAL_REQUIRES_DUNGEON <op:u8> <dungeon:u8>
//   DUNGEON_CLEARED <op:u8> <dungeon:u8>
//   REGION_REACHABLE <op:u8> <region_id:u16_le>
//   HAS_PRIZE      <op:u8> <prize:u8>
//   MEDALLION_OPENS <op:u8> <entrance:u8>
//   ITEM_IS        <op:u8> <item_id:u16_le>
//   NOT            <op:u8> <child>
//   AND            <op:u8> <count:u8> <child>×count
//   OR             <op:u8> <count:u8> <child>×count
// Vacuous AND (count=0) is TRUE; vacuous OR (count=0) is FALSE.

#ifndef ZELDA3_RANDO_LOGIC_H_
#define ZELDA3_RANDO_LOGIC_H_

#include "../types.h"
#include "rando_settings.h"

// ---------------------------------------------------------------------------
// Op IDs — mirror assets/rando/op_registry.yaml (0-based, append-only).
// ---------------------------------------------------------------------------
typedef enum {
  OP_HAS_ITEM = 0,
  OP_HAS_AMOUNT = 1,
  OP_HAS_ANY_OF = 2,
  OP_HAS_ANY_COUNT = 3,
  OP_WORLDSTATE_EQ = 4,
  OP_GOAL_EQ = 5,
  OP_GOAL_REQUIRES_DUNGEON = 6,
  OP_DUNGEON_CLEARED = 7,
  OP_REGION_REACHABLE = 8,
  OP_HAS_PRIZE = 9,
  OP_MEDALLION_OPENS = 10,
  OP_ITEM_IS = 11,
  OP_AND = 12,
  OP_OR = 13,
  OP_NOT = 14,
  // Phase B placeholders — predicates that reference these always evaluate
  // to their zero-glitch / zero-trick branch in Phase A.
  OP_TRICK = 15,
  OP_DIFFICULTY_AT_LEAST = 16,
  OP_GLITCH_LEVEL_AT_LEAST = 17,
  OP__COUNT = 18,
} RandoOp;

// ---------------------------------------------------------------------------
// Inventory snapshot. by_item_id[id] holds the placer's count for the given
// item registry id. Absolute items are {0,1}; progressive items count up to
// their max_count; bottle-with-contents IDs are distinct counters.
// ---------------------------------------------------------------------------
typedef struct RandoCounts {
  uint16 by_item_id[256];
} RandoCounts;

// ---------------------------------------------------------------------------
// Phase A: per-seed shuffle tables exposed to the VM.
//
// - dungeon_prize_assignment[d] = prize_id placed at dungeon d's _Prize slot.
//   Indexed by dungeon id (per the dungeon-id table in op_registry.yaml).
//   Phase A defaults: identity (each dungeon holds its vanilla prize).
// - medallion_entrance_assignment[e] = item_id of the medallion that opens
//   entrance e. e ∈ {MiseryMire_Entrance, TurtleRock_Entrance}.
// ---------------------------------------------------------------------------
#define kRandoDungeonCount  13
#define kRandoPrizeCount    10
#define kRandoMedallionEntranceCount 2

// ---------------------------------------------------------------------------
// PredicateContext — full evaluation environment. The simple entry points
// (Predicate_Evaluate / Predicate_EvaluatePlacement) build one of these and
// call Predicate_EvalCtx.
// ---------------------------------------------------------------------------
typedef struct PredicateContext {
  const RandoCounts *counts;
  const RandoSettings *settings;

  // can_place context:
  uint16 candidate_item;
  uint8 placement_context;  // 0 = can_reach, 1 = can_place (OP_ITEM_IS allowed)

  // Per-seed shuffle tables:
  const uint8 *dungeon_prize_assignment;       // [kRandoDungeonCount]; entries are prize ids 0..9
  const uint8 *medallion_entrance_assignment;  // [kRandoMedallionEntranceCount]; entries are item ids

  // Per-iteration reachability state (filled by Logic_ComputeReachability;
  // unused by standalone Predicate_Evaluate calls — pass 0 / NULL).
  uint64 cleared_dungeons_bitmask;             // bit d = dungeon d cleared
  const uint8 *reachable_regions_bitset;       // bit array of region ids; NULL = none reachable
  uint16 reachable_regions_count;              // total region count (for bound check)
} PredicateContext;

// ---------------------------------------------------------------------------
// Core evaluator.
//
// Returns true iff the predicate evaluates true under ctx. Caller passes the
// raw bytecode stream and its length in bytes; the VM consumes exactly len
// bytes (a debug-build assertion verifies). Returns false on malformed
// bytecode (and asserts in debug builds).
// ---------------------------------------------------------------------------
bool Predicate_EvalCtx(const uint8 *bytecode, uint16 length,
                       const PredicateContext *ctx);

// Convenience for can_reach predicates (no candidate_item).
bool Predicate_Evaluate(const uint8 *bytecode, uint16 length,
                        const RandoCounts *counts,
                        const RandoSettings *settings);

// Convenience for can_place predicates (candidate_item register populated).
bool Predicate_EvaluatePlacement(const uint8 *bytecode, uint16 length,
                                 const RandoCounts *counts,
                                 const RandoSettings *settings,
                                 uint16 candidate_item);

// Self-check (per Rng_SelfCheck / Share_SelfCheck / Settings_SelfCheck
// pattern). Builds a minimal in-memory bytecode stream exercising each op,
// asserts known results; exits with code 2 on failure.
void Logic_SelfCheck(void);

// ---------------------------------------------------------------------------
// Reachability (task 3.8). Phase A0 stub: returns NULL. Phase A1 implements
// the fixed-point expansion. Memoized per call; caller invalidates by calling
// Rando_BumpReachabilityCounter.
// ---------------------------------------------------------------------------
typedef struct RandoReachability RandoReachability;

const RandoReachability *Logic_ComputeReachability(const RandoCounts *counts,
                                                   const RandoSettings *settings);

bool Reachability_HasLocation(const RandoReachability *r, uint16 location_id);
bool Reachability_HasRegion(const RandoReachability *r, uint16 region_id);

// ---------------------------------------------------------------------------
// Generated-data record types (emitted by assets/rando_logic_gen.py into
// src/rando/logic_data.c). Declared here so other compilation units can
// reference the table without redeclaring the type.
// ---------------------------------------------------------------------------

typedef struct RandoLocationDef {
  uint16 id;
  uint16 vanilla_item_id;
  uint16 region_id;            // index into kRandoRegions, or 0xFFFF when location lacks logic.yaml region binding
  uint16 _pad0;
  uint32 can_reach_offset;     // offset into kRandoPredicateStream
  uint16 can_reach_length;
  uint32 can_place_offset;
  uint16 can_place_length;
  uint32 always_allow_offset;
  uint16 always_allow_length;
  uint8 type;                  // location-type enum (Chest/BigChest/.../Medallion)
  uint8 world_state_filter;    // bitmask: 0=all worlds, else bit per world-state
} RandoLocationDef;

typedef struct RandoRegionDef {
  uint16 id;
  uint16 parent_id;            // 0xFFFF = no parent
  uint8 dungeon_id;            // 0xFF = not a dungeon
  uint8 world_state_filter;
} RandoRegionDef;

typedef struct RandoEdgeDef {
  uint16 from_region;
  uint16 to_region;
  uint32 predicate_offset;
  uint16 predicate_length;
  uint8 one_way;
  uint8 _pad;
} RandoEdgeDef;

extern const RandoLocationDef kRandoLocations[];
extern const uint32 kRandoLocationsCount;
extern const RandoRegionDef kRandoRegions[];
extern const uint32 kRandoRegionsCount;
extern const RandoEdgeDef kRandoEdges[];
extern const uint32 kRandoEdgesCount;
extern const uint8 kRandoPredicateStream[];
extern const uint32 kRandoPredicateStreamSize;

// Phase B Slice 2 — per-world-state location-predicate override.
// When `settings.world_state == kWorldState_Inverted` (or Retro, future),
// the runtime VM looks up (location_id) in the per-world-state override
// table and uses its (cr/cp/aa) offsets instead of the base ones in
// `kRandoLocations`. Tables are sorted by location_id for binary search.
//
// `region_override`: 0xFFFF = "no region change" (use base region from
// `kRandoLocations[loc].region_id`). Otherwise, the runtime treats this
// location as belonging to `region_override` for reachability gating —
// e.g., Ether Tablet (id 194) moves from LW-DM-West (Standard) to
// LW-DM-East (Inverted) per PHP `Inverted/LightWorld/DeathMountain/East.php:25-26`.
typedef struct RandoLocationPredOverride {
  uint16 location_id;
  uint16 region_override;        // 0xFFFF = use base region_id
  uint32 can_reach_offset;
  uint16 can_reach_length;
  uint32 can_place_offset;
  uint16 can_place_length;
  uint32 always_allow_offset;
  uint16 always_allow_length;
} RandoLocationPredOverride;

extern const RandoLocationPredOverride kRandoLocationPredOverrides_Inverted[];
extern const uint32 kRandoLocationPredOverrides_InvertedCount;
extern const RandoLocationPredOverride kRandoLocationPredOverrides_Retro[];
extern const uint32 kRandoLocationPredOverrides_RetroCount;

// Per-world-state edges. Each non-Standard world-state with extra cross-
// region edges (Inverted's mirror-back, DarkWorld entry edges) emits its
// own table. The base `kRandoEdges` is the Standard graph.
extern const RandoEdgeDef kRandoEdges_Inverted[];
extern const uint32 kRandoEdges_InvertedCount;

// Lookup the predicate override for `loc_id` under `world_state`. Returns
// NULL when no override is installed (caller uses the base predicate from
// kRandoLocations[loc_id]). Binary search over the sorted override table.
const RandoLocationPredOverride *Rando_FindPredicateOverride(uint16 loc_id,
                                                             uint8 world_state);

// Human-readable name lookups (used by spoiler / debug output).
// Both return a pointer to a static string in the generated logic_data.c.
// Never returns NULL — falls back to "(unknown)" / "(unbound)" on miss.
const char *Rando_GetRegionName(uint16 region_id);
const char *Rando_GetLocationName(uint16 location_id);
const char *Rando_GetItemName(uint16 item_id);

// Translate a rando registry item_id to the LttP Link_ReceiveItem dispatch
// code. Used by §6 grant-site dispatch wrappers (rando dispatches via
// Rando_OnLocationCheck which returns a registry id; the existing
// Link_ReceiveItem path expects the LttP receive-item code byte).
//
// Returns 0xFF when no vanilla dispatch exists (progressive items, dungeon
// items, prize items, virtual items). Callers SHOULD fall back to the
// vanilla item that this grant site would have emitted; the rando subsystem
// will eventually grow per-item-class handlers (§6.2) for these cases.
uint8 Rando_VanillaItemForRegistryId(uint16 registry_item_id);

// Start region per world_state. Indexed by WorldState enum (Open=0,
// Standard=1, Inverted=2, Retro=3). Value is a region id (index into
// kRandoRegions) — the reachability fixed-point seeds from this region.
// A value of 0xFFFF means "no start region declared for this world_state";
// the graph is treated as empty (no locations reachable).
extern const uint16 kRandoStartRegionByWorldState[4];

// Look up the numeric region id by string name. Returns 0xFFFF when not
// found. O(N) linear scan; fine for the few authoring-time uses.
uint16 Rando_FindRegionByName(const char *name);

// NB: the Phase A `RegionRemap` scaffold (RegionRemap_Lookup /
// Rando_SetRegionRemap / Rando_ResetRegionRemap) was RETIRED in Phase C
// entrance shuffle. It was dead code (0 install callers, identity in every
// shipped seed) AND the wrong abstraction — it remapped an OP_REGION_REACHABLE
// *region operand*, whereas entrance shuffle rewires which interior a
// door-*edge* terminates at. Caves now use a per-seed location-region override
// (see rando_logic.c `Rando_SetEntranceRegionOverrides`); dungeons (Stage 2)
// will use a per-seed edge overlay mirroring kRandoEdges_Inverted. See
// openspec/changes/add-rando-entrance-shuffle/design.md §1.

#endif  // ZELDA3_RANDO_LOGIC_H_
