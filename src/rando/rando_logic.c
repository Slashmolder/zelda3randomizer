// rando_logic.c — predicate VM (tasks.md §3.7).
//
// Decodes the bytecode stream produced by assets/rando_logic_gen.py.
// Format reference lives in rando_logic.h and the codegen docstring.
//
// Determinism: no rand, no time, no float. Iteration over operand lists is
// linear scan. The recursive evaluator's depth is bounded by the codegen's
// inline-complexity check (~6 ops per macro after expansion).

#include "rando_logic.h"
#include "rando.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Read helpers — every multi-byte read is explicit LE per byte-order pin.
// ---------------------------------------------------------------------------
static inline uint16 read_u16le(const uint8 *p) {
  return (uint16)p[0] | ((uint16)p[1] << 8);
}

// ---------------------------------------------------------------------------
// Internal evaluator. Walks the bytecode tree recursively.
//
// Caller passes a cursor that the evaluator advances. Each top-level call
// reads exactly one predicate (i.e., one opcode + its operands + any nested
// children).
//
// Returns the boolean result. On malformed bytecode (unknown op, ran off the
// end), returns false in release and asserts in debug.
// ---------------------------------------------------------------------------

typedef struct Cursor {
  const uint8 *p;
  const uint8 *end;
  bool error;
} Cursor;

static bool cursor_ok(const Cursor *c, size_t need) {
  return !c->error && (size_t)(c->end - c->p) >= need;
}

static uint8 cursor_u8(Cursor *c) {
  if (!cursor_ok(c, 1)) { c->error = true; return 0; }
  return *c->p++;
}

static uint16 cursor_u16le(Cursor *c) {
  if (!cursor_ok(c, 2)) { c->error = true; return 0; }
  uint16 v = read_u16le(c->p);
  c->p += 2;
  return v;
}

static bool eval(Cursor *c, const PredicateContext *ctx);

static bool eval_has_item(Cursor *c, const PredicateContext *ctx) {
  uint16 item_id = cursor_u16le(c);
  if (c->error || item_id >= 256) return false;
  return ctx->counts->by_item_id[item_id] >= 1;
}

static bool eval_has_amount(Cursor *c, const PredicateContext *ctx) {
  uint16 item_id = cursor_u16le(c);
  uint8 n = cursor_u8(c);
  if (c->error || item_id >= 256) return false;
  return ctx->counts->by_item_id[item_id] >= n;
}

static bool eval_has_any_of(Cursor *c, const PredicateContext *ctx) {
  uint8 count = cursor_u8(c);
  bool result = false;
  for (uint8 i = 0; i < count; i++) {
    uint16 item_id = cursor_u16le(c);
    if (c->error || item_id >= 256) { result = false; continue; }
    if (ctx->counts->by_item_id[item_id] >= 1) result = true;
  }
  return result;
}

static bool eval_has_any_count(Cursor *c, const PredicateContext *ctx) {
  uint8 count = cursor_u8(c);
  // Sum the counts across all ids, then compare to threshold.
  uint32 sum = 0;
  for (uint8 i = 0; i < count; i++) {
    uint16 item_id = cursor_u16le(c);
    if (c->error || item_id >= 256) continue;
    sum += ctx->counts->by_item_id[item_id];
  }
  uint8 n = cursor_u8(c);
  if (c->error) return false;
  return sum >= n;
}

static bool eval_worldstate_eq(Cursor *c, const PredicateContext *ctx) {
  uint8 ws = cursor_u8(c);
  if (c->error || ctx->settings == NULL) return false;
  return ctx->settings->world_state == ws;
}

static bool eval_goal_eq(Cursor *c, const PredicateContext *ctx) {
  uint8 g = cursor_u8(c);
  if (c->error || ctx->settings == NULL) return false;
  return ctx->settings->goal == g;
}

static bool eval_goal_requires_dungeon(Cursor *c, const PredicateContext *ctx) {
  uint8 d = cursor_u8(c);
  if (c->error || ctx->settings == NULL) return false;
  if (d >= kRandoDungeonCount) return false;
  // Phase A goal semantics: All Dungeons requires every dungeon. Completionist
  // requires every dungeon whose locations are non-empty. Fast Ganon / Ganon
  // do NOT require dungeon clears (only crystals.ganon). Triforce-Hunt /
  // Ganon-Hunt require no specific dungeons.
  switch (ctx->settings->goal) {
    case kGoal_Dungeons:
    case kGoal_Completionist:
      return true;
    case kGoal_Ganon:
    case kGoal_FastGanon:
    case kGoal_GanonHunt:
    case kGoal_Pedestal:
    case kGoal_TriforceHunt:
    default:
      return false;
  }
}

static bool eval_dungeon_cleared(Cursor *c, const PredicateContext *ctx) {
  uint8 d = cursor_u8(c);
  if (c->error) return false;
  if (d >= 64) return false;  // bitmask is 64 bits
  return (ctx->cleared_dungeons_bitmask >> d) & 1;
}

static bool eval_region_reachable(Cursor *c, const PredicateContext *ctx) {
  uint16 region_id = cursor_u16le(c);
  if (c->error) return false;
  // OP_REGION_REACHABLE's operand IS a region id (the Phase A `RegionRemap`
  // indirection was retired in Phase C — it was identity dead code that would
  // have corrupted this hot predicate if ever populated; see design.md §1).
  // Phase A0: if no reachability bitset has been supplied (e.g., a standalone
  // Predicate_Evaluate call outside of Logic_ComputeReachability), conservatively
  // return false. The placer / tracker pass a populated bitset.
  if (ctx->reachable_regions_bitset == NULL) return false;
  if (region_id >= ctx->reachable_regions_count) return false;
  uint16 byte_idx = region_id >> 3;
  uint8 bit_idx = region_id & 7;
  return (ctx->reachable_regions_bitset[byte_idx] >> bit_idx) & 1;
}

static bool eval_has_prize(Cursor *c, const PredicateContext *ctx) {
  uint8 prize_id = cursor_u8(c);
  if (c->error || ctx->dungeon_prize_assignment == NULL) return false;
  // OP_HAS_PRIZE p evaluates true iff the dungeon currently holding prize p
  // (per the shuffle assignment) is in cleared_dungeons.
  for (uint8 d = 0; d < kRandoDungeonCount; d++) {
    if (ctx->dungeon_prize_assignment[d] == prize_id) {
      return (ctx->cleared_dungeons_bitmask >> d) & 1;
    }
  }
  return false;
}

static bool eval_medallion_opens(Cursor *c, const PredicateContext *ctx) {
  uint8 entrance_id = cursor_u8(c);
  if (c->error) return false;
  if (entrance_id >= kRandoMedallionEntranceCount) return false;
  if (ctx->medallion_entrance_assignment == NULL) return false;
  uint16 medallion_item_id = ctx->medallion_entrance_assignment[entrance_id];
  if (medallion_item_id >= 256) return false;
  return ctx->counts->by_item_id[medallion_item_id] >= 1;
}

static bool eval_item_is(Cursor *c, const PredicateContext *ctx) {
  uint16 item_id = cursor_u16le(c);
  if (c->error) return false;
  // Well-formedness pass in codegen rejects OP_ITEM_IS outside can_place; in
  // release we still guard against bad bytecode reaching here.
  assert(ctx->placement_context && "OP_ITEM_IS only valid in placement context");
  if (!ctx->placement_context) return false;
  return ctx->candidate_item == item_id;
}

static bool eval_not(Cursor *c, const PredicateContext *ctx) {
  return !eval(c, ctx);
}

static bool eval_and(Cursor *c, const PredicateContext *ctx) {
  uint8 count = cursor_u8(c);
  bool result = true;
  for (uint8 i = 0; i < count; i++) {
    bool child = eval(c, ctx);
    if (!child) {
      // Short-circuit: skip remaining children (but we still need to walk
      // them to advance the cursor). For correctness of the cursor we DO
      // need to read past them — so iterate without checking.
      result = false;
    }
  }
  return result;
}

static bool eval_or(Cursor *c, const PredicateContext *ctx) {
  uint8 count = cursor_u8(c);
  bool result = false;
  for (uint8 i = 0; i < count; i++) {
    bool child = eval(c, ctx);
    if (child) result = true;
  }
  return result;
}

// Phase B trick gate. Phase A pinned settings.tricks=0, so the bit test
// always failed. Slice 4 wires the bit test against the 8-bit
// settings.tricks bitfield (op_registry.yaml `tricks:` section enumerates
// the bit positions). Predicates referencing trick_id >= 8 are rejected
// by the codegen well-formedness pass; the runtime guard returns false
// defensively.
static bool eval_trick(Cursor *c, const PredicateContext *ctx) {
  uint8 trick_id = cursor_u8(c);
  if (c->error || ctx->settings == NULL) return false;
  if (trick_id >= 8) return false;
  return (ctx->settings->tricks & (uint8)(1u << trick_id)) != 0;
}
static bool eval_difficulty(Cursor *c, const PredicateContext *ctx) {
  uint8 level = cursor_u8(c);
  if (c->error || ctx->settings == NULL) return false;
  return ctx->settings->item_pool_difficulty >= level;
}
static bool eval_glitch(Cursor *c, const PredicateContext *ctx) {
  uint8 level = cursor_u8(c);
  if (c->error || ctx->settings == NULL) return false;
  return ctx->settings->logic >= level;
}

static bool eval(Cursor *c, const PredicateContext *ctx) {
  uint8 op = cursor_u8(c);
  if (c->error) return false;
  switch (op) {
    case OP_HAS_ITEM:               return eval_has_item(c, ctx);
    case OP_HAS_AMOUNT:             return eval_has_amount(c, ctx);
    case OP_HAS_ANY_OF:             return eval_has_any_of(c, ctx);
    case OP_HAS_ANY_COUNT:          return eval_has_any_count(c, ctx);
    case OP_WORLDSTATE_EQ:          return eval_worldstate_eq(c, ctx);
    case OP_GOAL_EQ:                return eval_goal_eq(c, ctx);
    case OP_GOAL_REQUIRES_DUNGEON:  return eval_goal_requires_dungeon(c, ctx);
    case OP_DUNGEON_CLEARED:        return eval_dungeon_cleared(c, ctx);
    case OP_REGION_REACHABLE:       return eval_region_reachable(c, ctx);
    case OP_HAS_PRIZE:              return eval_has_prize(c, ctx);
    case OP_MEDALLION_OPENS:        return eval_medallion_opens(c, ctx);
    case OP_ITEM_IS:                return eval_item_is(c, ctx);
    case OP_NOT:                    return eval_not(c, ctx);
    case OP_AND:                    return eval_and(c, ctx);
    case OP_OR:                     return eval_or(c, ctx);
    case OP_TRICK:                  return eval_trick(c, ctx);
    case OP_DIFFICULTY_AT_LEAST:    return eval_difficulty(c, ctx);
    case OP_GLITCH_LEVEL_AT_LEAST:  return eval_glitch(c, ctx);
    default:
      assert(0 && "unknown predicate op");
      c->error = true;
      return false;
  }
}

// ---------------------------------------------------------------------------
// Public entry points
// ---------------------------------------------------------------------------

bool Predicate_EvalCtx(const uint8 *bytecode, uint16 length,
                       const PredicateContext *ctx) {
  if (bytecode == NULL || length == 0 || ctx == NULL || ctx->counts == NULL) {
    return false;
  }
  Cursor c = { bytecode, bytecode + length, false };
  bool result = eval(&c, ctx);
  // Either we consumed the entire stream, or hit an error. Malformed bytecode
  // asserts in debug builds; in release we just return the partial result.
  assert(!c.error && "predicate bytecode error");
  // Trailing bytes is also an error — the codegen always emits exactly the
  // declared length.
  assert(c.p == c.end && "predicate length mismatch");
  return result;
}

bool Predicate_Evaluate(const uint8 *bytecode, uint16 length,
                        const RandoCounts *counts,
                        const RandoSettings *settings) {
  PredicateContext ctx;
  memset(&ctx, 0, sizeof(ctx));
  ctx.counts = counts;
  ctx.settings = settings;
  ctx.placement_context = 0;
  return Predicate_EvalCtx(bytecode, length, &ctx);
}

bool Predicate_EvaluatePlacement(const uint8 *bytecode, uint16 length,
                                 const RandoCounts *counts,
                                 const RandoSettings *settings,
                                 uint16 candidate_item) {
  PredicateContext ctx;
  memset(&ctx, 0, sizeof(ctx));
  ctx.counts = counts;
  ctx.settings = settings;
  ctx.candidate_item = candidate_item;
  ctx.placement_context = 1;
  return Predicate_EvalCtx(bytecode, length, &ctx);
}

// ---------------------------------------------------------------------------
// Phase C entrance shuffle — per-seed cave location-region overrides.
//
// When a cave entrance is shuffled, the cave-location's effective region becomes
// the vanilla region of whichever overworld door now leads to it (the closed
// form in shuffle_entrance.c). This is the SAME `effective_region` seam the
// static Inverted/Retro override uses (below), but driven per-seed by the
// entrance permutation instead of by world_state. It is consulted AFTER the
// static override; Stage 1 only supports Open/Standard, which carry no static
// location override, so there is no clobber. Inactive by default ⇒ the location
// loop is byte-identical to non-entrance-shuffle reachability.
#define kEntranceRegionOverrideMax 512
static uint16 g_entrance_region_override[kEntranceRegionOverrideMax];
static bool g_entrance_override_active = false;
// Stage 3 (cross-category): optional extra predicate AND-ed into an overridden
// cave-location's reachability — used when a cave lands behind a (gated) dungeon
// door so it inherits the door's requirement. pred_len 0 = no extra predicate.
static uint32 g_entrance_override_pred_off[kEntranceRegionOverrideMax];
static uint16 g_entrance_override_pred_len[kEntranceRegionOverrideMax];

void Rando_BeginEntranceRegionOverrides(void) {
  for (int i = 0; i < kEntranceRegionOverrideMax; i++) {
    g_entrance_region_override[i] = 0xFFFF;
    g_entrance_override_pred_len[i] = 0;
  }
  g_entrance_override_active = true;
}

void Rando_SetEntranceRegionOverride(uint16 loc_id, uint16 region_id) {
  if (loc_id < kEntranceRegionOverrideMax)
    g_entrance_region_override[loc_id] = region_id;
}

void Rando_ClearEntranceRegionOverrides(void) {
  g_entrance_override_active = false;
}

uint16 Rando_GetEntranceRegionOverride(uint16 loc_id) {
  if (!g_entrance_override_active || loc_id >= kEntranceRegionOverrideMax)
    return 0xFFFF;
  return g_entrance_region_override[loc_id];
}

// ---------------------------------------------------------------------------
// Phase C entrance shuffle (Stage 2) — per-seed DUNGEON edge overlay.
//
// Dungeons ARE first-class regions with inbound overworld door-edges, so the
// caves' location-region-override is the wrong tool. Instead we remap the
// *destination* of dungeon door-edges per π, keyed by the dungeon ENTRY region
// (each single-entrance dungeon's entry region is the `to_region` of exactly one
// door-edge). The edge's PREDICATE (the door-access requirement) stays with the
// door; only where the door leads changes — correct entrance-shuffle semantics.
// Internal dungeon edges + event gates are untouched (they have a dungeon entry
// region as `from`, not `to`). Inactive by default ⇒ byte-identical reachability.
#define kEntranceEdgeOverrideMax 64
static uint16 g_entrance_edge_override[kEntranceEdgeOverrideMax];
static bool g_entrance_edge_active = false;
// Stage 3 (cross-category): per-seed ADDED edges (overworld region → dungeon
// entry) for dungeons that land behind cave doors. Walked alongside kRandoEdges
// when g_entrance_edge_active. pred_len 0 = unconditional (cave-door access).
#define kEntranceAddedEdgeMax 64
static struct {
  uint16 from_region, to_region;
  uint32 pred_off;
  uint16 pred_len;
} g_entrance_added_edges[kEntranceAddedEdgeMax];
static int g_entrance_added_edge_count = 0;

void Rando_BeginEntranceEdgeOverrides(void) {
  for (int i = 0; i < kEntranceEdgeOverrideMax; i++)
    g_entrance_edge_override[i] = 0xFFFF;
  g_entrance_added_edge_count = 0;
  g_entrance_edge_active = true;
}

void Rando_SetEntranceEdgeOverride(uint16 old_to_region, uint16 new_to_region) {
  if (old_to_region < kEntranceEdgeOverrideMax)
    g_entrance_edge_override[old_to_region] = new_to_region;
}

void Rando_ClearEntranceEdgeOverrides(void) {
  g_entrance_edge_active = false;
}

bool Rando_EntranceEdgeOverridesActive(void) {
  return g_entrance_edge_active;
}

uint16 Rando_GetEntranceEdgeOverride(uint16 to_region) {
  if (!g_entrance_edge_active || to_region >= kEntranceEdgeOverrideMax)
    return to_region;
  uint16 ov = g_entrance_edge_override[to_region];
  return (ov == 0xFFFF) ? to_region : ov;
}

// ---------------------------------------------------------------------------
// Phase C entrance shuffle (Stage 3 / cross-category) — two extra primitives for
// caves↔dungeons mixing. ALL dungeon doors are item-gated (Moon Pearl, Flippers,
// Book, crystals, …), so a clean cross-shuffle needs both halves below.
//
// 3a. PREDICATE-CARRYING cave override: when a CAVE lands behind a DUNGEON door,
//     the cave's locations must inherit that door's predicate (else the placer
//     could strand the gating item inside — a softlock the reachability gate
//     can't see). Parallels the plain region override with an optional extra
//     predicate AND-ed into the cave's can_reach.
// 3b. ADDED edges: when a DUNGEON lands behind a CAVE door, there is no existing
//     edge to remap (caves aren't regions), so we ADD an edge
//     overworld-region → dungeon-entry. Cave doors have no access gate beyond
//     being in the region, so added edges are UNCONDITIONAL (pred_len 0 = true).
//
// Both share the g_entrance_override_active flag (set by Begin) and are inert by
// default ⇒ byte-identical reachability. (State declared above with the region /
// edge override blocks so the Begin functions can reset it.)
void Rando_SetEntranceRegionOverridePred(uint16 loc_id, uint16 region_id,
                                         uint32 pred_off, uint16 pred_len) {
  if (loc_id >= kEntranceRegionOverrideMax) return;
  g_entrance_region_override[loc_id] = region_id;
  g_entrance_override_pred_off[loc_id] = pred_off;
  g_entrance_override_pred_len[loc_id] = pred_len;
}

void Rando_AddEntranceEdge(uint16 from_region, uint16 to_region,
                           uint32 pred_off, uint16 pred_len) {
  if (g_entrance_added_edge_count >= kEntranceAddedEdgeMax) return;
  int i = g_entrance_added_edge_count++;
  g_entrance_added_edges[i].from_region = from_region;
  g_entrance_added_edges[i].to_region = to_region;
  g_entrance_added_edges[i].pred_off = pred_off;
  g_entrance_added_edges[i].pred_len = pred_len;
}

// ---------------------------------------------------------------------------
// Logic_ComputeReachability (task 3.8) — fixed-point expansion.
//
// Algorithm:
//   1. Initialize reachable_regions = {start_region}, reachable_locations = {}.
//      The start region depends on world_state (Open/Standard/Retro start in
//      LinksHouse; Inverted starts in DarkWorld_South). Phase A maps these
//      via region-id-by-name lookups when logic.yaml is populated; until
//      then, region 0 is treated as the start.
//   2. Iterate: for each edge from a reachable region, evaluate its predicate;
//      if true, mark target region reachable. For each location in a reachable
//      region, evaluate its can_reach; if true, mark reachable.
//   3. Stop when no new region / location was added in an iteration.
//   4. Memoize the result keyed by (counts, settings) — Phase A0 keeps the
//      result in a single static buffer (single-context per process).
//
// Performance budget (task 3.11): under 5 ms on reference desktop, under 20 ms
// on Switch, both for the full ~216-location graph. Phase A0's empty graph
// completes in microseconds.
// ---------------------------------------------------------------------------

#define kReachabilityMaxRegions 256
#define kReachabilityMaxLocations 512

struct RandoReachability {
  uint8 region_bitset[(kReachabilityMaxRegions + 7) >> 3];
  uint8 location_bitset[(kReachabilityMaxLocations + 7) >> 3];
  uint64 cleared_dungeons_bitmask;
  uint16 reachable_regions_count;
};

static struct RandoReachability g_reachability;

static inline void bitset_set(uint8 *bs, uint16 idx) {
  bs[idx >> 3] |= (uint8)(1u << (idx & 7));
}

static inline bool bitset_has(const uint8 *bs, uint16 idx) {
  return (bs[idx >> 3] >> (idx & 7)) & 1;
}

const RandoReachability *Logic_ComputeReachability(const RandoCounts *counts,
                                                   const RandoSettings *settings) {
  if (counts == NULL || settings == NULL) return NULL;
  memset(&g_reachability, 0, sizeof(g_reachability));
  g_reachability.reachable_regions_count = kReachabilityMaxRegions;

  // Seed the fixed-point with the world-state-appropriate start region. The
  // codegen-emitted `kRandoStartRegionByWorldState[]` maps WorldState → region
  // id; 0xFFFF means "no start region for this world-state" (e.g., Inverted
  // before LinksHouse_Inverted is declared). In that case the graph has no
  // reachable region and Logic_ComputeReachability returns an all-zero bitset
  // — the placer falls back to "every location stays at vanilla" for that
  // world-state.
  uint16 start_region = 0xFFFF;
  if (settings->world_state < 4) {
    start_region = kRandoStartRegionByWorldState[settings->world_state];
  }
  if (start_region != 0xFFFF && start_region < kReachabilityMaxRegions) {
    bitset_set(g_reachability.region_bitset, start_region);
  }

  PredicateContext ctx;
  memset(&ctx, 0, sizeof(ctx));
  ctx.counts = counts;
  ctx.settings = settings;
  ctx.reachable_regions_bitset = g_reachability.region_bitset;
  ctx.reachable_regions_count = kReachabilityMaxRegions;
  // Per-seed shuffle assignments (NULL when the placer hasn't installed
  // them yet — OP_HAS_PRIZE and OP_MEDALLION_OPENS evaluate to false in
  // that case, which makes prize-gated / medallion-gated areas unreachable).
  ctx.dungeon_prize_assignment = Rando_GetDungeonPrizeAssignment();
  ctx.medallion_entrance_assignment = Rando_GetMedallionAssignment();
  // cleared_dungeons_bitmask gets recomputed at the end of each fixed-point
  // iteration based on which boss locations have been reached. See below.

  // Fixed-point iteration. Cap at 64 iterations to bound runtime; the graph
  // depth is well under 32 in practice (per ALTTPR's region nesting).
  for (int iter = 0; iter < 64; iter++) {
    bool changed = false;

    // Expand reachable regions via edges.
    // Phase B Slice 2 — also walk per-world-state edges. The base
    // kRandoEdges array carries the Standard/Open/Retro graph; Inverted
    // adds its own edges (DarkWorld_South → IcePalace, LightWorld mirror
    // back to DarkWorld, etc.). When the active world state is Inverted,
    // both tables are walked.
    for (uint32 e = 0; e < kRandoEdgesCount; e++) {
      const RandoEdgeDef *edge = &kRandoEdges[e];
      if (edge->from_region == 0xFFFF || edge->to_region == 0xFFFF) continue;
      // Phase C Stage 2 — dungeon entrance shuffle remaps a door-edge's
      // destination per π (keeping its door-access predicate). Identity when
      // inactive ⇒ byte-identical.
      uint16 to_region = Rando_GetEntranceEdgeOverride(edge->to_region);
      if (!bitset_has(g_reachability.region_bitset, edge->from_region)) continue;
      if (bitset_has(g_reachability.region_bitset, to_region)) continue;
      const uint8 *bc = kRandoPredicateStream + edge->predicate_offset;
      if (Predicate_EvalCtx(bc, edge->predicate_length, &ctx)) {
        bitset_set(g_reachability.region_bitset, to_region);
        changed = true;
      }
    }
    if (settings->world_state == 2 /* kWorldState_Inverted */) {
      for (uint32 e = 0; e < kRandoEdges_InvertedCount; e++) {
        const RandoEdgeDef *edge = &kRandoEdges_Inverted[e];
        if (edge->from_region == 0xFFFF || edge->to_region == 0xFFFF) continue;
        uint16 to_region = Rando_GetEntranceEdgeOverride(edge->to_region);
        if (!bitset_has(g_reachability.region_bitset, edge->from_region)) continue;
        if (bitset_has(g_reachability.region_bitset, to_region)) continue;
        const uint8 *bc = kRandoPredicateStream + edge->predicate_offset;
        if (Predicate_EvalCtx(bc, edge->predicate_length, &ctx)) {
          bitset_set(g_reachability.region_bitset, to_region);
          changed = true;
        }
      }
    }
    // Phase C Stage 3 (cross-category) — per-seed ADDED edges (overworld region →
    // dungeon entry, for dungeons behind cave doors). Unconditional when
    // pred_len == 0 (a cave door has no access gate beyond being in the region).
    if (g_entrance_edge_active) {
      for (int e = 0; e < g_entrance_added_edge_count; e++) {
        uint16 fr = g_entrance_added_edges[e].from_region;
        uint16 tr = g_entrance_added_edges[e].to_region;
        if (fr == 0xFFFF || tr == 0xFFFF) continue;
        if (!bitset_has(g_reachability.region_bitset, fr)) continue;
        if (bitset_has(g_reachability.region_bitset, tr)) continue;
        uint16 pl = g_entrance_added_edges[e].pred_len;
        if (pl == 0 ||
            Predicate_EvalCtx(kRandoPredicateStream + g_entrance_added_edges[e].pred_off,
                              pl, &ctx)) {
          bitset_set(g_reachability.region_bitset, tr);
          changed = true;
        }
      }
    }

    // Expand reachable locations: a location is reachable iff
    //   (a) its world_state_filter permits the active world_state,
    //   (b) its region is in the reachable_region set (when the location
    //       has a region_id binding; locations with region_id == 0xFFFF
    //       are treated as "always-reachable region" — used for fountains
    //       and a handful of standalone locations), AND
    //   (c) its can_reach predicate evaluates true under the current
    //       inventory snapshot.
    for (uint32 i = 0; i < kRandoLocationsCount; i++) {
      const RandoLocationDef *loc = &kRandoLocations[i];
      if (bitset_has(g_reachability.location_bitset, loc->id)) continue;
      if (loc->world_state_filter != 0) {
        if (!(loc->world_state_filter & (1u << settings->world_state))) continue;
      }
      // Phase B Slice 2 — consult the per-world-state override table.
      // Inverted seeds get a different can_reach predicate per location;
      // when no override exists, fall back to the base predicate. The
      // override may ALSO change the region the location belongs to —
      // e.g., Ether Tablet (id 194) moves East↔West in Inverted per
      // `Inverted/LightWorld/DeathMountain/East.php:25-26`.
      uint32 cr_offset = loc->can_reach_offset;
      uint16 cr_length = loc->can_reach_length;
      uint16 effective_region = loc->region_id;
      const RandoLocationPredOverride *ov =
          Rando_FindPredicateOverride(loc->id, settings->world_state);
      if (ov != NULL) {
        cr_offset = ov->can_reach_offset;
        cr_length = ov->can_reach_length;
        if (ov->region_override != 0xFFFF) {
          effective_region = ov->region_override;
        }
      }
      // Phase C — per-seed entrance-shuffle cave region override (takes
      // precedence; Open/Standard carry no static `ov`, so no conflict).
      uint32 ov_pred_off = 0; uint16 ov_pred_len = 0;  // Stage 3 cross-cat gate
      if (g_entrance_override_active && loc->id < kEntranceRegionOverrideMax &&
          g_entrance_region_override[loc->id] != 0xFFFF) {
        effective_region = g_entrance_region_override[loc->id];
        // Stage 3 — a cave behind a (gated) dungeon door inherits that door's
        // predicate so the placer can't strand the gating item inside.
        ov_pred_off = g_entrance_override_pred_off[loc->id];
        ov_pred_len = g_entrance_override_pred_len[loc->id];
      }
      if (effective_region != 0xFFFF) {
        if (effective_region >= kReachabilityMaxRegions) continue;
        if (!bitset_has(g_reachability.region_bitset, effective_region)) continue;
      }
      // Cross-category override predicate (the destination dungeon door's gate).
      if (ov_pred_len != 0 &&
          !Predicate_EvalCtx(kRandoPredicateStream + ov_pred_off, ov_pred_len, &ctx)) {
        continue;
      }
      const uint8 *bc = kRandoPredicateStream + cr_offset;
      if (Predicate_EvalCtx(bc, cr_length, &ctx)) {
        bitset_set(g_reachability.location_bitset, loc->id);
        changed = true;
      }
    }

    // Update cleared_dungeons_bitmask based on which boss locations are
    // now reachable. Mapping per the dungeon-id table in op_registry.yaml
    // (HCE=0, EP=1, DP=2, TH=3, HCT=4, PoD=5, SP=6, SW=7, TT=8, IP=9, MM=10,
    // TR=11, GT=12). The boss location is the canonical "dungeon cleared"
    // signal — when its Prize location is reachable the boss is necessarily
    // defeated to get there.
    static const uint16 kDungeonBossLocations[13] = {
      0xFFFF,  // HCE: no boss (escape sequence; Zelda event handles it)
      16,      // EP Boss
      23,      // DP Boss
      30,      // TH Boss
      34,      // HCT Agahnim (Prize_Event)
      48,      // PoD Boss
      59,      // SP Boss
      68,      // SW Boss
      77,      // TT Boss
      86,      // IP Boss
      95,      // MM Boss
      108,     // TR Boss
      137,     // GT Agahnim 2 (Prize_Event)
    };
    uint64 new_cleared = 0;
    for (uint8 d = 0; d < 13; d++) {
      uint16 loc_id = kDungeonBossLocations[d];
      if (loc_id == 0xFFFF) continue;
      if (loc_id < kReachabilityMaxLocations &&
          bitset_has(g_reachability.location_bitset, loc_id)) {
        new_cleared |= (uint64)1 << d;
      }
    }
    if (new_cleared != g_reachability.cleared_dungeons_bitmask) {
      g_reachability.cleared_dungeons_bitmask = new_cleared;
      ctx.cleared_dungeons_bitmask = new_cleared;
      changed = true;
    }

    if (!changed) break;
  }

  return &g_reachability;
}

bool Reachability_HasLocation(const RandoReachability *r, uint16 location_id) {
  if (r == NULL) return false;
  if (location_id >= kReachabilityMaxLocations) return false;
  return bitset_has(r->location_bitset, location_id);
}

bool Reachability_HasRegion(const RandoReachability *r, uint16 region_id) {
  if (r == NULL) return false;
  if (region_id >= kReachabilityMaxRegions) return false;
  return bitset_has(r->region_bitset, region_id);
}

// ---------------------------------------------------------------------------
// Phase B Slice 2 — per-world-state predicate override lookup.
//
// Override tables are sorted by location_id at codegen time; binary search
// is the standard lookup. Returns NULL when no override is installed for
// (location_id, world_state).
// ---------------------------------------------------------------------------
static const RandoLocationPredOverride *
binary_search_overrides(const RandoLocationPredOverride *arr, uint32 count, uint16 loc_id) {
  if (count == 0) return NULL;
  int lo = 0, hi = (int)count - 1;
  while (lo <= hi) {
    int mid = lo + ((hi - lo) >> 1);
    uint16 mid_id = arr[mid].location_id;
    if (mid_id == loc_id) return &arr[mid];
    if (mid_id < loc_id) lo = mid + 1;
    else hi = mid - 1;
  }
  return NULL;
}

const RandoLocationPredOverride *
Rando_FindPredicateOverride(uint16 loc_id, uint8 world_state) {
  switch (world_state) {
    case 2:  // kWorldState_Inverted
      return binary_search_overrides(kRandoLocationPredOverrides_Inverted,
                                     kRandoLocationPredOverrides_InvertedCount,
                                     loc_id);
    case 3:  // kWorldState_Retro
      return binary_search_overrides(kRandoLocationPredOverrides_Retro,
                                     kRandoLocationPredOverrides_RetroCount,
                                     loc_id);
    default:
      return NULL;
  }
}

// ---------------------------------------------------------------------------
// Self-check — synthetic bytecode exercising each op.
//
// Pattern mirrors Rng_SelfCheck / Share_SelfCheck / Settings_SelfCheck:
// builds known inputs, asserts known outputs, exits 2 on failure.
// ---------------------------------------------------------------------------

static void selfcheck_die(const char *msg) {
  fprintf(stderr, "[Logic_SelfCheck] FAIL: %s\n", msg);
  exit(2);
}

#define LSC_ASSERT(cond, msg) do { if (!(cond)) selfcheck_die(msg); } while (0)

void Logic_SelfCheck(void) {
  RandoCounts counts;
  memset(&counts, 0, sizeof(counts));
  RandoSettings settings;
  Settings_SetDefaults(&settings);

  // HAS_ITEM(5) when count[5] == 0 -> false
  {
    uint8 bc[] = { OP_HAS_ITEM, 5, 0 };
    LSC_ASSERT(Predicate_Evaluate(bc, sizeof(bc), &counts, &settings) == false,
               "HAS_ITEM(5) without item should be false");
  }
  // HAS_ITEM(5) when count[5] == 1 -> true
  counts.by_item_id[5] = 1;
  {
    uint8 bc[] = { OP_HAS_ITEM, 5, 0 };
    LSC_ASSERT(Predicate_Evaluate(bc, sizeof(bc), &counts, &settings) == true,
               "HAS_ITEM(5) with item should be true");
  }

  // HAS_AMOUNT(5, 2) when count[5] == 1 -> false
  {
    uint8 bc[] = { OP_HAS_AMOUNT, 5, 0, 2 };
    LSC_ASSERT(Predicate_Evaluate(bc, sizeof(bc), &counts, &settings) == false,
               "HAS_AMOUNT(5,2) with count=1 should be false");
  }
  // HAS_AMOUNT(5, 2) when count[5] == 2 -> true
  counts.by_item_id[5] = 2;
  {
    uint8 bc[] = { OP_HAS_AMOUNT, 5, 0, 2 };
    LSC_ASSERT(Predicate_Evaluate(bc, sizeof(bc), &counts, &settings) == true,
               "HAS_AMOUNT(5,2) with count=2 should be true");
  }
  counts.by_item_id[5] = 0;

  // HAS_ANY_OF([7, 8]) when neither set -> false
  {
    uint8 bc[] = { OP_HAS_ANY_OF, 2, 7, 0, 8, 0 };
    LSC_ASSERT(Predicate_Evaluate(bc, sizeof(bc), &counts, &settings) == false,
               "HAS_ANY_OF empty inventory should be false");
  }
  counts.by_item_id[8] = 1;
  {
    uint8 bc[] = { OP_HAS_ANY_OF, 2, 7, 0, 8, 0 };
    LSC_ASSERT(Predicate_Evaluate(bc, sizeof(bc), &counts, &settings) == true,
               "HAS_ANY_OF with one item set should be true");
  }
  counts.by_item_id[8] = 0;

  // HAS_ANY_COUNT([7,8,9], 2): 1+1+0 = 2 -> true; 1+0+0 = 1 -> false
  counts.by_item_id[7] = 1;
  counts.by_item_id[8] = 1;
  {
    uint8 bc[] = { OP_HAS_ANY_COUNT, 3, 7, 0, 8, 0, 9, 0, 2 };
    LSC_ASSERT(Predicate_Evaluate(bc, sizeof(bc), &counts, &settings) == true,
               "HAS_ANY_COUNT sum=2 threshold 2 should be true");
  }
  counts.by_item_id[8] = 0;
  {
    uint8 bc[] = { OP_HAS_ANY_COUNT, 3, 7, 0, 8, 0, 9, 0, 2 };
    LSC_ASSERT(Predicate_Evaluate(bc, sizeof(bc), &counts, &settings) == false,
               "HAS_ANY_COUNT sum=1 threshold 2 should be false");
  }
  // Vacuous HAS_ANY_COUNT edge cases:
  //   - count=0, n=0: sum 0 >= 0 → true (vacuous)
  //   - count=0, n=1: sum 0 >= 1 → false
  {
    uint8 bc[] = { OP_HAS_ANY_COUNT, 0, 0 };
    LSC_ASSERT(Predicate_Evaluate(bc, sizeof(bc), &counts, &settings) == true,
               "HAS_ANY_COUNT vacuous (count=0, n=0) should be true");
  }
  {
    uint8 bc[] = { OP_HAS_ANY_COUNT, 0, 1 };
    LSC_ASSERT(Predicate_Evaluate(bc, sizeof(bc), &counts, &settings) == false,
               "HAS_ANY_COUNT vacuous (count=0, n=1) should be false");
  }
  counts.by_item_id[7] = 0;

  // WORLDSTATE_EQ(open) — defaults set world_state = Open (0)
  {
    uint8 bc[] = { OP_WORLDSTATE_EQ, kWorldState_Open };
    LSC_ASSERT(Predicate_Evaluate(bc, sizeof(bc), &counts, &settings) == true,
               "WORLDSTATE_EQ(open) against defaults should be true");
  }
  {
    uint8 bc[] = { OP_WORLDSTATE_EQ, kWorldState_Inverted };
    LSC_ASSERT(Predicate_Evaluate(bc, sizeof(bc), &counts, &settings) == false,
               "WORLDSTATE_EQ(inverted) against defaults should be false");
  }

  // GOAL_EQ(fast_ganon) — defaults set goal = FastGanon (1)
  {
    uint8 bc[] = { OP_GOAL_EQ, kGoal_FastGanon };
    LSC_ASSERT(Predicate_Evaluate(bc, sizeof(bc), &counts, &settings) == true,
               "GOAL_EQ(fast_ganon) against defaults should be true");
  }

  // AND with all true children
  {
    uint8 bc[] = {
      OP_AND, 2,
        OP_WORLDSTATE_EQ, kWorldState_Open,
        OP_GOAL_EQ, kGoal_FastGanon,
    };
    LSC_ASSERT(Predicate_Evaluate(bc, sizeof(bc), &counts, &settings) == true,
               "AND of two truths should be true");
  }
  // AND with one false child
  {
    uint8 bc[] = {
      OP_AND, 2,
        OP_WORLDSTATE_EQ, kWorldState_Inverted,
        OP_GOAL_EQ, kGoal_FastGanon,
    };
    LSC_ASSERT(Predicate_Evaluate(bc, sizeof(bc), &counts, &settings) == false,
               "AND with one false should be false");
  }
  // Vacuous AND -> TRUE
  {
    uint8 bc[] = { OP_AND, 0 };
    LSC_ASSERT(Predicate_Evaluate(bc, sizeof(bc), &counts, &settings) == true,
               "vacuous AND should be true");
  }
  // Vacuous OR -> FALSE
  {
    uint8 bc[] = { OP_OR, 0 };
    LSC_ASSERT(Predicate_Evaluate(bc, sizeof(bc), &counts, &settings) == false,
               "vacuous OR should be false");
  }

  // OR with one truth
  {
    uint8 bc[] = {
      OP_OR, 2,
        OP_WORLDSTATE_EQ, kWorldState_Inverted,
        OP_GOAL_EQ, kGoal_FastGanon,
    };
    LSC_ASSERT(Predicate_Evaluate(bc, sizeof(bc), &counts, &settings) == true,
               "OR with one truth should be true");
  }

  // NOT
  {
    uint8 bc[] = { OP_NOT, OP_WORLDSTATE_EQ, kWorldState_Inverted };
    LSC_ASSERT(Predicate_Evaluate(bc, sizeof(bc), &counts, &settings) == true,
               "NOT (false) should be true");
  }

  // ITEM_IS in placement context — true iff candidate matches
  {
    uint8 bc[] = { OP_ITEM_IS, 42, 0 };
    LSC_ASSERT(Predicate_EvaluatePlacement(bc, sizeof(bc), &counts, &settings, 42) == true,
               "ITEM_IS(42) with candidate 42 should be true");
    LSC_ASSERT(Predicate_EvaluatePlacement(bc, sizeof(bc), &counts, &settings, 41) == false,
               "ITEM_IS(42) with candidate 41 should be false");
  }

  // DUNGEON_CLEARED — driven by cleared_dungeons_bitmask in ctx
  {
    uint8 bc[] = { OP_DUNGEON_CLEARED, 5 };
    PredicateContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.counts = &counts;
    ctx.settings = &settings;
    ctx.cleared_dungeons_bitmask = (uint64)1 << 5;
    LSC_ASSERT(Predicate_EvalCtx(bc, sizeof(bc), &ctx) == true,
               "DUNGEON_CLEARED with bit set should be true");
    ctx.cleared_dungeons_bitmask = 0;
    LSC_ASSERT(Predicate_EvalCtx(bc, sizeof(bc), &ctx) == false,
               "DUNGEON_CLEARED with bit clear should be false");
  }

  // HAS_PRIZE — true iff the dungeon currently holding the prize is cleared
  {
    uint8 bc[] = { OP_HAS_PRIZE, 0 };  // prize id 0 = Prize_GreenPendant
    uint8 prize_assignment[kRandoDungeonCount];
    memset(prize_assignment, 0xff, sizeof(prize_assignment));
    prize_assignment[1] = 0;  // EasternPalace (id 1) holds the green pendant
    PredicateContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.counts = &counts;
    ctx.settings = &settings;
    ctx.dungeon_prize_assignment = prize_assignment;
    ctx.cleared_dungeons_bitmask = (uint64)1 << 1;  // EP cleared
    LSC_ASSERT(Predicate_EvalCtx(bc, sizeof(bc), &ctx) == true,
               "HAS_PRIZE when holding-dungeon cleared should be true");
    ctx.cleared_dungeons_bitmask = 0;
    LSC_ASSERT(Predicate_EvalCtx(bc, sizeof(bc), &ctx) == false,
               "HAS_PRIZE when holding-dungeon not cleared should be false");
  }

  // MEDALLION_OPENS — true iff inventory has the medallion that opens the entrance
  {
    uint8 bc[] = { OP_MEDALLION_OPENS, 0 };  // entrance 0 = MiseryMire
    uint8 medallion_assignment[kRandoMedallionEntranceCount];
    // Misery Mire opens with item id 26 (Ether per item_registry)
    medallion_assignment[0] = 26;
    medallion_assignment[1] = 25;
    PredicateContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.counts = &counts;
    ctx.settings = &settings;
    ctx.medallion_entrance_assignment = medallion_assignment;
    counts.by_item_id[26] = 1;
    LSC_ASSERT(Predicate_EvalCtx(bc, sizeof(bc), &ctx) == true,
               "MEDALLION_OPENS with required medallion should be true");
    counts.by_item_id[26] = 0;
    LSC_ASSERT(Predicate_EvalCtx(bc, sizeof(bc), &ctx) == false,
               "MEDALLION_OPENS without required medallion should be false");
  }

  // Phase B placeholder ops always evaluate to their zero branch in Phase A
  {
    uint8 bc[] = { OP_TRICK, 5 };
    LSC_ASSERT(Predicate_Evaluate(bc, sizeof(bc), &counts, &settings) == false,
               "OP_TRICK should always be false in Phase A");
  }
  {
    uint8 bc[] = { OP_GLITCH_LEVEL_AT_LEAST, 1 };
    LSC_ASSERT(Predicate_Evaluate(bc, sizeof(bc), &counts, &settings) == false,
               "OP_GLITCH_LEVEL_AT_LEAST(1) should be false in Phase A (logic=NoGlitches)");
  }

  // OP_DIFFICULTY_AT_LEAST against defaults (normal=1)
  {
    uint8 bc[] = { OP_DIFFICULTY_AT_LEAST, 1 };
    LSC_ASSERT(Predicate_Evaluate(bc, sizeof(bc), &counts, &settings) == true,
               "OP_DIFFICULTY_AT_LEAST(1) against defaults (normal=1) should be true");
  }
  {
    uint8 bc[] = { OP_DIFFICULTY_AT_LEAST, 2 };
    LSC_ASSERT(Predicate_Evaluate(bc, sizeof(bc), &counts, &settings) == false,
               "OP_DIFFICULTY_AT_LEAST(2) against defaults (normal=1) should be false");
  }

  // Audit L8 — Inverted reachability self-check. Verify the world-state
  // override path actually fires by computing reachability under Inverted
  // and asserting that `LinksHouse_Inverted` (the Inverted start region)
  // is reachable while `LinksHouse` (the Standard start) is NOT.
  // Exercises:
  //   - `Logic_ComputeReachability` running under `world_state = 2`.
  //   - `kRandoStartRegionByWorldState[Inverted]` resolving to the right id.
  //   - `kRandoEdges_Inverted` being walked (the trivial
  //     LinksHouse_Inverted → DarkWorld_South edge means DW_South should
  //     also be reachable).
  {
    RandoSettings inv_settings;
    Settings_SetDefaults(&inv_settings);
    inv_settings.world_state = 2;  // kWorldState_Inverted
    RandoCounts inv_counts;
    memset(&inv_counts, 0, sizeof(inv_counts));
    // Grant the full pool (every item id has count >= 1) so we're
    // checking GRAPH reachability, not item gating.
    for (int i = 0; i < 256; i++) inv_counts.by_item_id[i] = 99;
    const RandoReachability *inv_reach =
        Logic_ComputeReachability(&inv_counts, &inv_settings);
    LSC_ASSERT(inv_reach != NULL,
               "Logic_ComputeReachability returned NULL under Inverted");

    uint16 lhi = Rando_FindRegionByName("LinksHouse_Inverted");
    uint16 lhs = Rando_FindRegionByName("LinksHouse");
    LSC_ASSERT(lhi != 0xFFFF,
               "LinksHouse_Inverted region must exist for Inverted seeds");
    if (lhi != 0xFFFF && inv_reach != NULL) {
      LSC_ASSERT(Reachability_HasRegion(inv_reach, lhi),
                 "LinksHouse_Inverted should be reachable under world_state=Inverted");
    }
    // The Standard start region has world_state_filter excluding Inverted;
    // it should NOT be in the reachable set under Inverted.
    if (lhs != 0xFFFF && lhs != lhi && inv_reach != NULL) {
      LSC_ASSERT(!Reachability_HasRegion(inv_reach, lhs),
                 "LinksHouse should NOT be reachable under world_state=Inverted "
                 "(gated by world_state_filter)");
    }
  }

  fprintf(stderr, "[Logic_SelfCheck] OK\n");
}
