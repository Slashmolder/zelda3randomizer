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
//   INSTANT_FLUTE  <op:u8>
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
  // Phase B swordless — settings.mode_weapons == operand. Used by the Ganon /
  // Agahnim / CanMeltThings predicates to add a swordless branch.
  OP_MODEWEAPONS_EQ = 18,
  // Boss-shuffle runtime — "can kill the boss assigned to dungeon_id". Resolves
  // the per-seed boss assignment (boss_assignment, NULL→vanilla) then evaluates
  // that boss's kill predicate from kRandoBossKillPred[]. boss_shuffle off ⇒
  // vanilla identity ⇒ byte-identical to the inline CanKill<Boss> it replaces.
  OP_CAN_KILL_BOSS = 19,
  // Door shuffle (add-rando-door-shuffle). OP_DOORS_ACTIVE(door_dungeon_idx):
  // a layout is installed AND that dungeon's shuffled_mask bit is set (pinned
  // dungeons stay clear -> vanilla branch of the codegen wrap).
  // OP_DOORS_LOC_REACHABLE(loc_u16): the door-shuffle oracle — the same
  // crystal-aware explorer the stitcher/prover use, seeded from the portal
  // lobbies whose kDoorPortalGates row holds under the current region bitset,
  // gated by inventory + per-key-door worst-case thresholds. False when no
  // layout is installed.
  OP_DOORS_ACTIVE = 20,
  OP_DOORS_LOC_REACHABLE = 21,
  // Randomizer QoL — true when the seed setting promotes OcarinaInactive
  // pickups to the active bird-woken flute immediately.
  OP_INSTANT_FLUTE = 22,
  // Pot sanity (add-rando-pot-sanity task #25) — true iff dungeon pot keys are
  // first-class shuffled checks (pot_shuffle >= Keys AND door shuffle off). A
  // pot-bearing dungeon's deep locations wrap their small-key term so pots-off
  // keeps the vanilla worst-case (byte-identical) and pots-on requires the
  // prover worst-case that counts the now-itemized pot keys.
  OP_POT_KEYS_ON = 23,
  // Pot sanity (task #25) — POT_KEYS_ON AND small keys are WILD (keysanity,
  // incl. Retro). The pot-bearing dungeons' deep locations gate their wild
  // worst-case key requirement on this: under wild keys you must HOLD the keys
  // before reaching (they live anywhere in the world), so the static worst-case
  // is both correct and achievable. Dungeon-keys (in-context collection) is a
  // separate follow-on and stays on the vanilla branch here.
  OP_POT_KEYS_WILD = 24,
  OP__COUNT = 25,
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
//   Indexed by kRandoDungeon_* order from dungeon_ids.h.
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
  // Boss-shuffle assignment: [16]; entry = boss-pool index
  // (kBoss_* in shuffle_boss.c) currently in that dungeon's boss room. NULL when
  // the placer/slot hasn't installed it — OP_CAN_KILL_BOSS then falls back to
  // kRandoDungeonVanillaBoss (vanilla boss), so reachability is unchanged.
  const uint8 *boss_assignment;                // [16]; entries are boss-pool indices

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

// Copy the last Logic_ComputeReachability result into a private snapshot buffer
// (refresh=true) and return it, or return the existing snapshot unchanged
// (refresh=false). The snapshot survives later Logic_ComputeReachability calls,
// which only touch the shared result buffer. Used by the runtime tracker bridge
// to hold a stable reachability across frames / multiple windows.
const RandoReachability *Reachability_Snapshot(bool refresh);

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
  uint8 type;                  // location-type enum (Chest/BigChest/.../Medallion);
                               // shop-class ordinals shared below (LOCTYPE_*)
  uint8 world_state_filter;    // bitmask: 0=all worlds, else bit per world-state
} RandoLocationDef;

// logic.schema.yaml location-type ordinals that are referenced across modules.
// Shared here so ordinals can't drift between the placer, spoiler emitters,
// trackers, and hint generator. Other type ordinals stay local to
// rando_placement.c while they are single-site.
enum {
  LOCTYPE_Prize_Event = 12,  // virtual event trigger, not an item check
  LOCTYPE_Medallion   = 13,  // MM/TR medallion config slot, not an item check
  LOCTYPE_Shop        = 14,  // Retro regular shop slot (identity-pinned inventory)
  LOCTYPE_ShopUpgrade = 15,  // Retro capacity-upgrade slot (identity-placed)
  LOCTYPE_TakeAny     = 16,  // Retro take-any cave slot
  LOCTYPE_Pot         = 17,  // add-rando-pot-sanity dungeon pot (per-tier active subset)
};

static inline bool Rando_LocationTypeCountsAsCheck(uint8 type) {
  return type != LOCTYPE_Prize_Event && type != LOCTYPE_Medallion;
}

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

// ---------------------------------------------------------------------------
// Single location-id ceiling for the whole randomizer module.
//
// EVERY array, bitmap, loop bound, or guard keyed by a location_id MUST size /
// bound by this constant — never a bare literal (512/1024). The static registry
// is append-only and grows over time (328 baseline + 835 pot-sanity pots = 1163
// today); 2048 leaves headroom. Because every consumer routes through this one
// name, a registry that ever exceeds capacity is a SINGLE build break — the
// codegen `_Static_assert(LOC__COUNT <= 2048)` in location_ids.h plus the
// `_Static_assert(LOC__COUNT <= kRandoLocationCapacity)` name-ties in rando.c /
// rando_placement.c — not a silent overflow / truncation / drop (fail-open).
// Keep the 2048 in location_ids.h's codegen assert (rando_logic_gen.py) in
// lockstep with this value.
// ---------------------------------------------------------------------------
#define kRandoLocationCapacity 2048
extern const RandoRegionDef kRandoRegions[];
extern const uint32 kRandoRegionsCount;
extern const RandoEdgeDef kRandoEdges[];
extern const uint32 kRandoEdgesCount;
extern const uint8 kRandoPredicateStream[];
extern const uint32 kRandoPredicateStreamSize;

// Boss-shuffle runtime — OP_CAN_KILL_BOSS dispatch tables (emitted by
// assets/rando_logic_gen.py). Indexed by boss-pool index (kBoss_* in
// src/rando/shuffle_boss.c): 0=Armos, 1=Lanmolas, 2=Moldorm, 3=Agahnim,
// 4=Helmasaur, 5=Arrghus, 6=Mothula, 7=Blind, 8=Kholdstare, 9=Vitreous,
// 10=Trinexx, 11=Agahnim2. Each entry points at that boss's kill predicate in
// kRandoPredicateStream (the same compiled bytecode the inline CanKill<Boss>
// macro produces, so the off-shuffle resolution is byte-identical).
typedef struct RandoBossKillPred {
  uint32 offset;   // into kRandoPredicateStream
  uint16 length;   // bytes; 0 = no predicate (always false)
} RandoBossKillPred;
extern const RandoBossKillPred kRandoBossKillPred[];
extern const uint32 kRandoBossKillPredCount;
// dungeon-id (HCE=0..GT=12) → vanilla boss-pool index, or 0xFF for HCE/unused.
// Mirrors shuffle_boss.c kBossVanilla; used by OP_CAN_KILL_BOSS when no per-seed
// boss assignment is installed. BossShuffle_SelfCheck (shuffle_boss.c) element-
// wise cross-checks this against kBossVanilla — both run via
// Rando_RunAllSelfChecks (the check lives there, not in Logic_SelfCheck).
extern const uint8 kRandoDungeonVanillaBoss[kRandoDungeonCount];

// ---------------------------------------------------------------------------
// §12.6 — per-trick / per-glitch-level ROM-version verification status.
// ALTTPR targets the Japanese 1.0 ROM; this fork targets US 1.0. A trick that
// exists in ALTTPR's logic graph may have JP/US timing/mechanic differences or
// be absent on US 1.0. The generator consults these tables to warn (in the
// spoiler `fallback_warnings`) when a seed enables an unverified trick / level.
// The string→int encoding is produced by ROM_VER_STATUS in rando_logic_gen.py.
// ---------------------------------------------------------------------------
typedef enum {
  kRomVerStatus_UntestedUs10  = 0,  // in the upstream graph; not confirmed on US 1.0 (default)
  kRomVerStatus_VerifiedUs10  = 1,  // performed end-to-end on a real US 1.0 build
  kRomVerStatus_CrossVersion  = 2,  // pure player skill / verified identical JP↔US
  kRomVerStatus_Jp10Only      = 3,  // confirmed NOT to work on US 1.0 (codegen forbids in gates)
  kRomVerStatus_Us10Different = 4,  // exists on both but with different timing/mechanics
} RandoRomVerStatus;

typedef struct RandoTrickStatus {
  uint8 bit;                  // settings.tricks bit position
  uint8 rom_version_status;   // RandoRomVerStatus
  const char *id;             // kebab-case trick id (e.g. "pearl-bypass")
} RandoTrickStatus;
extern const RandoTrickStatus kRandoTrickStatus[];
extern const uint32 kRandoTrickStatusCount;

typedef struct RandoGlitchLevelStatus {
  uint8 level;                // settings.logic value (1=OverworldGlitches, 2=MajorGlitches, ...)
  uint8 rom_version_status;   // RandoRomVerStatus
  const char *id;             // e.g. "overworld_glitches"
} RandoGlitchLevelStatus;
extern const RandoGlitchLevelStatus kRandoGlitchLevelStatus[];
extern const uint32 kRandoGlitchLevelStatusCount;

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

// Phase C entrance shuffle — per-seed cave location-region overrides. Begin
// resets to identity + activates; Set assigns one location's effective region;
// Clear deactivates (restoring byte-identical reachability). Driven by
// shuffle_entrance.c from the entrance permutation.
void Rando_BeginEntranceRegionOverrides(void);
void Rando_SetEntranceRegionOverride(uint16 loc_id, uint16 region_id);
void Rando_ClearEntranceRegionOverrides(void);
// Returns the current per-seed override region for a location, or 0xFFFF if
// none/inactive. For the self-check + tracker.
uint16 Rando_GetEntranceRegionOverride(uint16 loc_id);

// Phase C entrance shuffle (Stage 2) — per-seed DUNGEON edge overlay. Begin
// resets to identity + activates; Set remaps a door-edge whose destination is
// `old_to_region` to land at `new_to_region` instead (keyed by the dungeon entry
// region); Clear deactivates; Get resolves (returns the input when inactive/none).
void Rando_BeginEntranceEdgeOverrides(void);
void Rando_SetEntranceEdgeOverride(uint16 old_to_region, uint16 new_to_region);
void Rando_ClearEntranceEdgeOverrides(void);
uint16 Rando_GetEntranceEdgeOverride(uint16 to_region);
// True while edge overrides / added edges are active (between Begin and Clear).
// Lets the decoupled exit-edge pass add edges on top of an already-begun dungeon/
// cross edge set without re-Begin (which would wipe them).
bool Rando_EntranceEdgeOverridesActive(void);

// Phase C entrance shuffle (Stage 3 / cross-category) primitives.
// SetEntranceRegionOverridePred: like the region override, but ALSO AND a
// predicate (pred_off/pred_len into kRandoPredicateStream; len 0 = none) into the
// cave-location's reachability — for a cave behind a gated dungeon door so it
// inherits the door's requirement. Reset by Rando_BeginEntranceRegionOverrides.
void Rando_SetEntranceRegionOverridePred(uint16 loc_id, uint16 region_id,
                                         uint32 pred_off, uint16 pred_len);
// AddEntranceEdge: add a per-seed edge from_region → to_region (pred_len 0 =
// unconditional) — for a dungeon behind a cave door. Reset by
// Rando_BeginEntranceEdgeOverrides; walked when edge overrides are active.
void Rando_AddEntranceEdge(uint16 from_region, uint16 to_region,
                           uint32 pred_off, uint16 pred_len);
int Rando_GetEntranceAddedEdgeCount(void);

// ---------------------------------------------------------------------------
// Door shuffle (add-rando-door-shuffle) — logic-side install + generated data.
// ---------------------------------------------------------------------------

// Compiled fork-DSL predicates referenced by door-table rule-blob Vm leaves
// (emitted by rando_logic_gen.py from assets/rando/door_predicates.gen.json,
// index-matched to that manifest).
typedef struct RandoDoorVmPred {
  uint32 off;
  uint16 len;
} RandoDoorVmPred;
extern const RandoDoorVmPred kDoorVmPreds[];
extern const uint32 kDoorVmPredsCount;

// Portal seeding gates (door_portals.yaml): the oracle floods a dungeon from
// exactly the portal lobbies whose fork_region bit is set in the current
// reachability bitset AND whose optional predicate holds. fork_region 0xFFFF
// = never independently enterable (TR ledge doors).
typedef struct RandoDoorPortalGate {
  uint8 dungeon;       // kDoorTblDungeons index
  uint8 is_drop;
  uint16 door_region;  // door-table region id of the lobby
  uint16 fork_region;  // fork logic region id, 0xFFFF = never
  uint32 pred_off;     // extra gate predicate, len 0 = none
  uint16 pred_len;
} RandoDoorPortalGate;
extern const RandoDoorPortalGate kDoorPortalGates[];
extern const uint32 kDoorPortalGatesCount;

// Install/clear the per-seed door layout for logic evaluation (generation
// installs before Place_AssumedFill; slot activation installs the regenerated
// layout; teardown clears). `layout` must outlive the install. active_mask
// bit = kDoorTblDungeons index actually shuffled (pins excluded by caller).
struct DoorShuffleLayout;
void Rando_SetDoorLogicLayout(const struct DoorShuffleLayout *layout, uint16 active_mask);
const struct DoorShuffleLayout *Rando_GetDoorLogicLayout(uint16 *active_mask_out);

#endif  // ZELDA3_RANDO_LOGIC_H_
