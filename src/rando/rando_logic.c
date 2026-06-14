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
#include "item_ids.h"  // ITEM_GenericKey / ITEM_SmallKey_* (genericKeys collapse)
#include "shuffle_doors.h"  // door-shuffle oracle (OP_DOORS_LOC_REACHABLE)

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

// genericKeys (Retro) small-key collapse — a direct port of ALTTPR
// ItemCollection::has() (app/Support/ItemCollection.php:271-273): when generic
// keys are in effect, ANY per-dungeon small-key requirement (regardless of the
// requested amount) is satisfied by holding >=1 GenericKey. Big keys, maps and
// compasses are unaffected (they keep per-dungeon identity under Retro). The
// per-dungeon SmallKey ids are 53..65 (item_registry.yaml, contiguous
// ITEM_SmallKey_HyruleCastleEscape..ITEM_SmallKey_GanonsTower). When generic
// keys are NOT active the caller falls through to the normal per-dungeon count,
// so non-Retro evaluation is byte-identical.
static bool logic_generic_keys_active(const PredicateContext *ctx) {
  return ctx->settings != NULL &&
         ctx->settings->world_state == (uint8)kWorldState_Retro;
}

static bool item_is_small_key(uint16 item_id) {
  return item_id >= ITEM_SmallKey_HyruleCastleEscape &&
         item_id <= ITEM_SmallKey_GanonsTower;
}

static bool eval_has_item(Cursor *c, const PredicateContext *ctx) {
  uint16 item_id = cursor_u16le(c);
  if (c->error || item_id >= 256) return false;
  if (item_is_small_key(item_id) && logic_generic_keys_active(ctx))
    return ctx->counts->by_item_id[ITEM_GenericKey] >= 1;
  return ctx->counts->by_item_id[item_id] >= 1;
}

static bool eval_has_amount(Cursor *c, const PredicateContext *ctx) {
  uint16 item_id = cursor_u16le(c);
  uint8 n = cursor_u8(c);
  if (c->error || item_id >= 256) return false;
  if (item_is_small_key(item_id) && logic_generic_keys_active(ctx))
    return ctx->counts->by_item_id[ITEM_GenericKey] >= 1;  // any key opens any door
  return ctx->counts->by_item_id[item_id] >= n;
}

static bool eval_has_any_of(Cursor *c, const PredicateContext *ctx) {
  uint8 count = cursor_u8(c);
  bool result = false;
  bool generic = logic_generic_keys_active(ctx);
  for (uint8 i = 0; i < count; i++) {
    uint16 item_id = cursor_u16le(c);
    if (c->error || item_id >= 256) { result = false; continue; }
    // genericKeys (Retro) small-key collapse — mirror eval_has_item /
    // eval_has_amount: any per-dungeon SmallKey requirement is satisfied by
    // holding >=1 GenericKey. Inert under default settings (no author uses
    // HAS_ANY for a SmallKey today) but kept consistent so a future logic_parts
    // file that lists a SmallKey in HAS_ANY can't desync from the runtime pool.
    uint16 eff_id = (generic && item_is_small_key(item_id)) ? ITEM_GenericKey : item_id;
    if (ctx->counts->by_item_id[eff_id] >= 1) result = true;
  }
  return result;
}

static bool eval_has_any_count(Cursor *c, const PredicateContext *ctx) {
  uint8 count = cursor_u8(c);
  // Sum the counts across all ids, then compare to threshold.
  uint32 sum = 0;
  bool generic = logic_generic_keys_active(ctx);
  bool added_generic = false;
  for (uint8 i = 0; i < count; i++) {
    uint16 item_id = cursor_u16le(c);
    if (c->error || item_id >= 256) continue;
    // genericKeys (Retro) small-key collapse — fold every per-dungeon SmallKey
    // id in the list into the single shared GenericKey count, added at most
    // once so multiple SmallKey ids don't multiply-count the shared pool.
    // Inert under default settings (no author uses HAS_ANY for a SmallKey).
    if (generic && item_is_small_key(item_id)) {
      if (!added_generic) {
        sum += ctx->counts->by_item_id[ITEM_GenericKey];
        added_generic = true;
      }
      continue;
    }
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
    // NOTE: despite the name, AND does NOT short-circuit — every child MUST be
    // evaluated so the cursor advances past its bytecode. Only record falsity.
    if (!child) result = false;
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
// Phase B swordless — settings.mode_weapons == operand. The Ganon/Agahnim/
// CanMeltThings predicates branch on OP_MODEWEAPONS_EQ(swordless); under the
// default (randomized) it is false so those swordless disjuncts are inert.
static bool eval_modeweapons_eq(Cursor *c, const PredicateContext *ctx) {
  uint8 mw = cursor_u8(c);
  if (c->error || ctx->settings == NULL) return false;
  return ctx->settings->mode_weapons == mw;
}

// Boss-shuffle runtime — "can kill the boss assigned to dungeon_id". Resolves
// the per-seed boss assignment (ctx->boss_assignment; NULL ⇒ the vanilla boss
// via kRandoDungeonVanillaBoss), then RE-ENTERS the evaluator on that boss's
// kill predicate (kRandoBossKillPred[boss]). The outer cursor `c` advances past
// exactly the 1-byte dungeon operand; the sub-predicate runs on its own cursor
// so the length-consumed invariant of the caller is preserved.
//
// With boss_shuffle off the installed assignment is the vanilla identity (and a
// NULL assignment falls back to the same vanilla map), so this resolves to the
// dungeon's vanilla boss-kill predicate — byte-identical reachability to the
// inline CanKill<Boss> macro it replaced. No boss-kill predicate references
// OP_CAN_KILL_BOSS, so the re-entry can't recurse unboundedly.
// Door-shuffle ops — bodies live after the per-seed install section below
// (they consult the installed layout + the explorer-backed oracle).
static bool eval_doors_active(Cursor *c, const PredicateContext *ctx);
static bool eval_doors_loc_reachable(Cursor *c, const PredicateContext *ctx);

static bool eval_can_kill_boss(Cursor *c, const PredicateContext *ctx) {
  uint8 dungeon = cursor_u8(c);
  if (c->error || dungeon >= kRandoDungeonCount) return false;
  uint8 boss = (ctx->boss_assignment != NULL)
                   ? ctx->boss_assignment[dungeon]
                   : kRandoDungeonVanillaBoss[dungeon];
  if (boss >= kRandoBossKillPredCount) return false;  // 0xFF (HCE/unused) → false
  const RandoBossKillPred *p = &kRandoBossKillPred[boss];
  if (p->length == 0) return false;
  Cursor sub = { kRandoPredicateStream + p->offset,
                 kRandoPredicateStream + p->offset + p->length, false };
  bool r = eval(&sub, ctx);
  if (sub.error) c->error = true;  // propagate malformed-bytecode signal
  return r;
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
    case OP_MODEWEAPONS_EQ:         return eval_modeweapons_eq(c, ctx);
    case OP_CAN_KILL_BOSS:          return eval_can_kill_boss(c, ctx);
    case OP_DOORS_ACTIVE:           return eval_doors_active(c, ctx);
    case OP_DOORS_LOC_REACHABLE:    return eval_doors_loc_reachable(c, ctx);
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
  // Phase D (add-rando-major-glitch) NoLogic short-circuit. At logic==NoLogic
  // EVERY predicate — reachability (placement_context==0) AND the LOGIC-level
  // placement can_place (placement_context==1) — returns true.
  //
  // Reachability-true (D1) → goal-completability and all accessibility tiers pass
  // vacuously, so the --generate-seed strict refusal (main.c) does not fire and
  // the seed has no reachability guarantee (mirrors ALTTPR World.php:93 — regions
  // not initialized under NoLogic).
  //
  // can_place-true (F5; was D1-deferred, now built per owner request) bypasses the
  // LOGIC-level can_place PREDICATES — the NotADungeonItem fill ban and item-locked
  // slots (e.g. Swamp Palace Entrance's `OP_ITEM_IS(SmallKey_SwampPalace)`) — so
  // placement is no longer LOGIC-constrained. SCOPE NOTE: the structural dungeon-
  // item MODE containment ("Dungeon-mode small key stays in its dungeon") is the
  // C-level `dungeon_mode_accepts_item` check in rando_placement.c, OUTSIDE this
  // predicate VM, and is deliberately STILL respected — it is an explicit MODE
  // choice (not a logic rule) and keeps Dungeon-mode keys runtime-grantable. So
  // F5 = "NoLogic ignores placement LOGIC", NOT "dungeon keys anywhere even in
  // Dungeon mode" (that would override a deliberate setting + risk ungrantable
  // keys — see design.md D1). Verified: a NoLogic + wild-keys seed's placement
  // DIFFERS from the pre-F5 confined placement (c0f5e87c -> 8be3e395 at seed
  // 0xF5F5), while the default Vanilla-keys NoLogic seed is BYTE-IDENTICAL (its
  // dungeon items are pre-pinned, never subject to can_place).
  //
  // Fires only at logic==NoLogic(4): no logic<4 digest moves; the default-keys
  // NoLogic corpus entry is unchanged and a NoLogic+wild-keys entry pins F5's
  // effect. See design.md D1/F5.
  if (ctx->settings != NULL && ctx->settings->logic >= 4 /* NoLogic */) {
    return true;
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

// Count of currently-active added edges (0 when the edge set is inactive). Used
// by the self-check to guard against cross-attempt accumulation (decoupled HIGH).
int Rando_GetEntranceAddedEdgeCount(void) {
  return g_entrance_edge_active ? g_entrance_added_edge_count : 0;
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
// Door shuffle — per-seed layout install + the OP_DOORS_* oracle.
//
// The oracle runs the SAME crystal-aware explorer the stitcher and key
// prover use (DoorExplore_Run), seeded from the portal lobbies whose
// kDoorPortalGates row holds under the current region bitset, with Vm rule
// leaves bound to the compiled fork predicate stream (kDoorVmPreds) and
// key-door edges gated by the prover's worst-case thresholds. Results are
// memoized per (dungeon, fixed-point pass): within one pass counts are fixed
// and the region bitset only grows between passes, so a stale-by-one-pass
// cache can only underestimate — the loop exits only after a zero-change
// pass, whose cache inputs are exact. Monotone, converges.
// ---------------------------------------------------------------------------

static const DoorShuffleLayout *g_door_logic_layout;
static uint16 g_door_logic_mask;
static uint32 g_door_oracle_gen = 1;
static uint32 g_door_oracle_cache_gen[kDoorTbl_DungeonCount];
static DoorExploreResult g_door_oracle_cache[kDoorTbl_DungeonCount];
// Input fingerprint per cached flood: the flood's outcome is a pure function
// of (inventory counts, portal set, held keys, big key) for a fixed layout.
// Assumed fill calls Logic_ComputeReachability hundreds of times with mostly
// IDENTICAL inventory; re-flooding per fixed-point pass made a basic seed
// take minutes. When the fingerprint matches, the pass-stale cache entry is
// still exact — reuse it across generations.
static uint64 g_door_oracle_cache_fp[kDoorTbl_DungeonCount];
static uint64 g_door_counts_fp;       // FNV over ctx->counts, per oracle gen
static uint32 g_door_counts_fp_gen;
static bool g_in_door_oracle;
// Per-counts memo for the vm-pred leaves (see door_vm_pred_cb).
#define kDoorVmMemoMax 128
// The flood-skip fingerprint masks (CollectVmAt in shuffle_doors.c) and this
// memo both cap at 128 vm indices; the memo fails safe past the cap but the
// MASK would fail UNSOUND (a dropped index = a missed reflood trigger) —
// refuse to compile instead.
_Static_assert(kDoorTbl_VmPredCount <= kDoorVmMemoMax,
               "door vm preds exceed the fingerprint/memo width");
static uint8 g_door_vm_memo[kDoorVmMemoMax];  // 0 unknown / 1 false / 2 true
static uint64 g_door_vm_memo_fp;

static uint64 door_fnv64(uint64 h, const void *data, size_t n) {
  const uint8 *p = (const uint8 *)data;
  for (size_t i = 0; i < n; i++) {
    h ^= p[i];
    h *= 0x100000001B3ull;
  }
  return h;
}

void Rando_SetDoorLogicLayout(const struct DoorShuffleLayout *layout, uint16 active_mask) {
  g_door_logic_layout = layout;
  g_door_logic_mask = layout ? active_mask : 0;
  g_door_oracle_gen++;
  // Hard-invalidate the flood cache + vm-pred memo: the input fingerprints
  // deliberately exclude the layout pointer and the settings/boss-assignment
  // context (constant within a run), so a NEW install must drop everything.
  memset(g_door_oracle_cache_gen, 0, sizeof(g_door_oracle_cache_gen));
  memset(g_door_oracle_cache_fp, 0, sizeof(g_door_oracle_cache_fp));
  memset(g_door_vm_memo, 0, sizeof(g_door_vm_memo));
  g_door_vm_memo_fp = ~0ull;
  g_door_counts_fp_gen = 0;
}

const struct DoorShuffleLayout *Rando_GetDoorLogicLayout(uint16 *active_mask_out) {
  if (active_mask_out)
    *active_mask_out = g_door_logic_mask;
  return g_door_logic_layout;
}

// Per-counts memo for the vm-pred leaves: they are pure functions of the
// inventory counts (verified: no door vm pred expands to OP_REGION_REACHABLE
// or OP_DOORS_*), and some expand large (CanKillMostThings). A flood
// evaluates them per edge; without the memo a basic seed's fill spent most
// of its time re-running the same 69 predicates thousands of times.
static bool door_vm_pred_cb(void *ud, uint16 vm_index) {
  const PredicateContext *ctx = (const PredicateContext *)ud;
  if (vm_index >= kDoorVmPredsCount)
    return false;
  if (g_door_vm_memo_fp != g_door_counts_fp) {
    memset(g_door_vm_memo, 0, sizeof(g_door_vm_memo));
    g_door_vm_memo_fp = g_door_counts_fp;
  }
  if (vm_index < kDoorVmMemoMax && g_door_vm_memo[vm_index])
    return g_door_vm_memo[vm_index] == 2;
  // Door vm-pred leaves are pure item/macro terms (the door-table translator
  // never emits OP_DOORS_* into them), so this cannot recurse into the
  // oracle; g_in_door_oracle guards the invariant.
  bool v = Predicate_EvalCtx(kRandoPredicateStream + kDoorVmPreds[vm_index].off,
                             kDoorVmPreds[vm_index].len, ctx);
  if (vm_index < kDoorVmMemoMax)
    g_door_vm_memo[vm_index] = v ? 2 : 1;
  return v;
}

static const DoorExploreResult *door_oracle_get(uint8 dungeon, const PredicateContext *ctx,
                                                DoorExploreGates *gates_out) {
  // The gates are rebuilt every call (cheap); the flood itself is cached.
  static uint8 held_keys[kDoorTbl_DungeonCount];
  static uint8 big_key_held[kDoorTbl_DungeonCount];
  for (int i = 0; i < kDoorTbl_DungeonCount; i++) {
    uint16 sk = kDoorTblDungeons[i].small_key_item;
    uint16 bk = kDoorTblDungeons[i].big_key_item;
    held_keys[i] = (sk != 0xFFFF && ctx->counts) ? ctx->counts->by_item_id[sk] : 0;
    big_key_held[i] = (bk == 0xFFFF) ? 1
                      : ((ctx->counts && ctx->counts->by_item_id[bk]) ? 1 : 0);
  }
  gates_out->vm_pred = door_vm_pred_cb;
  gates_out->ud = (void *)ctx;
  gates_out->held_keys = held_keys;
  gates_out->big_key_held = big_key_held;
  gates_out->key_thresholds = g_door_logic_layout;

  if (g_door_oracle_cache_gen[dungeon] == g_door_oracle_gen)
    return &g_door_oracle_cache[dungeon];

  uint16 portals[12];
  int n = 0;
  for (uint32 i = 0; i < kDoorPortalGatesCount; i++) {
    const RandoDoorPortalGate *g = &kDoorPortalGates[i];
    if (g->dungeon != dungeon || g->fork_region == 0xFFFF)
      continue;
    if (ctx->reachable_regions_bitset == NULL)
      continue;
    if (!((ctx->reachable_regions_bitset[g->fork_region >> 3] >> (g->fork_region & 7)) & 1))
      continue;
    if (g->pred_len &&
        !Predicate_EvalCtx(kRandoPredicateStream + g->pred_off, g->pred_len, ctx))
      continue;
    if (n < 12)
      portals[n++] = g->door_region;
  }

  // Flood-skip: a flood's outcome is a pure function of (the vm-pred results
  // the dungeon's rules reference, held keys, big key, portal set) for a
  // fixed layout. Fingerprint exactly those — assumed fill changes one item
  // at a time, and most changes flip no predicate relevant to most dungeons.
  if (g_door_counts_fp_gen != g_door_oracle_gen) {
    uint64 fp = ctx->counts
        ? door_fnv64(0xcbf29ce484222325ull, ctx->counts->by_item_id,
                     sizeof(ctx->counts->by_item_id))
        : 0;
    // A door VM predicate can read settings->mode_weapons (swordless) via
    // OP_MODEWEAPONS_EQ, so the per-counts memo/flood fingerprint is NOT a pure
    // function of the inventory counts. Fold mode_weapons in too so a
    // future caller that evaluates the same door layout under two settings
    // differing only in swordless can't get a stale memoized result. Memo-key
    // only — never affects the computed value, so placement is byte-identical.
    if (ctx->settings)
      fp = door_fnv64(fp, &ctx->settings->mode_weapons,
                      sizeof(ctx->settings->mode_weapons));
    g_door_counts_fp = fp;
    g_door_counts_fp_gen = g_door_oracle_gen;
  }
  uint64 vm_mask[2];
  DoorExplore_RelevantVmMask(dungeon, vm_mask);
  uint64 vm_bits[2] = { 0, 0 };
  for (int w = 0; w < 2; w++) {
    for (int b = 0; b < 64; b++) {
      if (!((vm_mask[w] >> b) & 1))
        continue;
      if (door_vm_pred_cb((void *)ctx, (uint16)(w * 64 + b)))
        vm_bits[w] |= 1ull << b;
    }
  }
  uint64 fp = door_fnv64(0xD00Eull + dungeon, vm_bits, sizeof(vm_bits));
  fp = door_fnv64(fp, portals, (size_t)n * sizeof(portals[0]));
  fp = door_fnv64(fp, &held_keys[dungeon], 1);
  fp = door_fnv64(fp, &big_key_held[dungeon], 1);
  if (fp == g_door_oracle_cache_fp[dungeon] && g_door_oracle_cache_gen[dungeon] != 0) {
    g_door_oracle_cache_gen[dungeon] = g_door_oracle_gen;
    return &g_door_oracle_cache[dungeon];
  }

  g_in_door_oracle = true;
  DoorExplore_Run(g_door_logic_layout, dungeon, portals, n, gates_out,
                  &g_door_oracle_cache[dungeon]);
  g_in_door_oracle = false;
  g_door_oracle_cache_gen[dungeon] = g_door_oracle_gen;
  g_door_oracle_cache_fp[dungeon] = fp;
  return &g_door_oracle_cache[dungeon];
}

static bool eval_doors_active(Cursor *c, const PredicateContext *ctx) {
  uint8 d = cursor_u8(c);
  (void)ctx;
  return g_door_logic_layout != NULL && d < kDoorTbl_DungeonCount &&
         ((g_door_logic_mask >> d) & 1) != 0;
}

static bool eval_doors_loc_reachable(Cursor *c, const PredicateContext *ctx) {
  uint16 loc_id = cursor_u16le(c);
  if (g_door_logic_layout == NULL || g_in_door_oracle)
    return false;
  // kDoorTblLocations is sorted by fork_loc_id — binary search.
  int lo = 0, hi = kDoorTbl_LocationCount - 1, found = -1;
  while (lo <= hi) {
    int mid = (lo + hi) >> 1;
    if (kDoorTblLocations[mid].fork_loc_id == loc_id) { found = mid; break; }
    if (kDoorTblLocations[mid].fork_loc_id < loc_id) lo = mid + 1;
    else hi = mid - 1;
  }
  if (found < 0)
    return false;
  const DoorTblLocation *dl = &kDoorTblLocations[found];
  uint8 dungeon = kDoorTblRegions[dl->region].dungeon;
  DoorExploreGates gates;
  const DoorExploreResult *r = door_oracle_get(dungeon, ctx, &gates);
  if (!DoorExplore_Reached(r, dl->region))
    return false;
  return DoorExplore_EvalRule(dl->rule, r, &gates);
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

// under Inverted the placer should evaluate the INVERTED graph, not
// base ∪ inverted. A base edge whose (from,to) pair ALSO has an Inverted edge is
// SHADOWED: the Inverted edge (walked separately below) is authoritative for that
// pair, and walking the looser base edge too leaks base reachability — e.g. the
// base Desert/EP entrance edges gate on RescuedZelda-only, while their Inverted
// counterparts require MoonPearl, so the unshadowed base edge let the Desert
// interior be reached in the Inverted model without the pearl. Precompute the
// shadowed-base-edge set once from the constant edge tables; the base loop skips
// these when world_state==Inverted. (Beatability is unchanged — the runtime
// pre-grants MoonPearl in Inverted — but this restores ALTTPR-faithful Inverted
// placement, so Inverted placement_digests move; the rest stay byte-identical.)
// Bitmap of (from,to) region pairs that an Inverted edge covers (compile-time
// sized on kReachabilityMaxRegions so it isn't a file-scope VLA — kRandoEdgesCount
// is a runtime const). A base edge whose pair is set is shadowed under Inverted.
static uint8 g_inverted_pair_set[kReachabilityMaxRegions][(kReachabilityMaxRegions + 7) >> 3];
static bool g_inverted_pair_ready = false;
static void compute_inverted_shadow(void) {
  if (g_inverted_pair_ready) return;
  memset(g_inverted_pair_set, 0, sizeof(g_inverted_pair_set));
  for (uint32 j = 0; j < kRandoEdges_InvertedCount; j++) {
    uint16 f = kRandoEdges_Inverted[j].from_region, t = kRandoEdges_Inverted[j].to_region;
    if (f < kReachabilityMaxRegions && t < kReachabilityMaxRegions)
      g_inverted_pair_set[f][t >> 3] |= (uint8)(1u << (t & 7));
  }
  g_inverted_pair_ready = true;
}
static inline bool base_edge_inverted_shadowed(uint16 from, uint16 to) {
  if (from >= kReachabilityMaxRegions || to >= kReachabilityMaxRegions) return false;
  return (g_inverted_pair_set[from][to >> 3] >> (to & 7)) & 1;
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
  // Boss-shuffle assignment for OP_CAN_KILL_BOSS. NULL ⇒ vanilla boss fallback,
  // so a graph with no boss assignment installed gates each `- Boss` on its
  // vanilla boss-kill predicate (byte-identical to the pre-op logic).
  ctx.boss_assignment = Rando_GetBossAssignment();
  // cleared_dungeons_bitmask gets recomputed at the end of each fixed-point
  // iteration based on which boss locations have been reached. See below.

  // L7 — precompute (once) which base edges an inverted edge shadows, so the
  // base-edge walk can skip them under Inverted (the inverted edge is then the
  // sole authority for that (from,to) pair).
  const bool inverted = (settings->world_state == 2 /* kWorldState_Inverted */);
  if (inverted) compute_inverted_shadow();

  // Fixed-point iteration. Cap at 64 iterations to bound runtime; the graph
  // depth is well under 32 in practice (per ALTTPR's region nesting).
  for (int iter = 0; iter < 64; iter++) {
    bool changed = false;
    // Door shuffle: invalidate the oracle memo each pass — counts are fixed
    // for this whole call, but the region bitset (portal availability) grows
    // between passes.
    g_door_oracle_gen++;

    // Expand reachable regions via edges.
    // Phase B Slice 2 — also walk per-world-state edges. The base kRandoEdges
    // array carries the Standard/Open/Retro graph; Inverted adds its own edges
    // (DarkWorld_South → IcePalace, LightWorld mirror back to DarkWorld, etc.).
    // Under Inverted both tables are walked, BUT a base edge shadowed by an
    // inverted edge for the same (from,to) is skipped (L7) so the Inverted graph
    // REPLACES — not unions with — the base graph for those pairs.
    for (uint32 e = 0; e < kRandoEdgesCount; e++) {
      const RandoEdgeDef *edge = &kRandoEdges[e];
      if (edge->from_region == 0xFFFF || edge->to_region == 0xFFFF) continue;
      if (inverted && base_edge_inverted_shadowed(edge->from_region, edge->to_region))
        continue;  // L7: the inverted edge for this (from,to) pair is authoritative
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
    //       has a region_id binding; a location with region_id == 0xFFFF
    //       would be treated as "always-reachable region" — a defensive
    //       fallback; every currently generated location has a binding,
    //       since the unbound Fountain slots 140/141 were retired), AND
    //   (c) its can_reach predicate evaluates true under the current
    //       inventory snapshot.
    for (uint32 i = 0; i < kRandoLocationsCount; i++) {
      const RandoLocationDef *loc = &kRandoLocations[i];
      // Bound loc->id before indexing the [kReachabilityMaxLocations]-sized
      // bitset. Every shipping id is < 512 (a codegen _Static_assert
      // enforces it), so this never fires today; it fails CLOSED (the location
      // is simply treated unreachable) if a future registry append overflows,
      // mirroring the bounded cleared-dungeons boss loop instead of corrupting
      // adjacent memory.
      if (loc->id >= kReachabilityMaxLocations) continue;
      if (bitset_has(g_reachability.location_bitset, loc->id)) continue;
      if (loc->world_state_filter != 0) {
        // Guard the shift: world_state is validated at every byte entry point
        // (Settings_Validate), but a shift by >=32 would be UB if a new caller
        // bypasses validation. Unknown world_state = filter never matches.
        if (settings->world_state >= 32 ||
            !(loc->world_state_filter & (1u << settings->world_state))) continue;
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

// Private snapshot buffer. Logic_ComputeReachability returns a pointer into the
// single shared g_reachability, which any later call overwrites. A caller that
// must hold a result across other reachability computations (the runtime
// tracker bridge) copies it here once and reads the stable snapshot.
static struct RandoReachability g_reachability_snapshot;

const RandoReachability *Reachability_Snapshot(bool refresh) {
  if (refresh) g_reachability_snapshot = g_reachability;
  return &g_reachability_snapshot;
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

  // CAN_KILL_BOSS — resolves the dungeon's ASSIGNED boss, then evaluates THAT
  // boss's kill predicate. The same op + dungeon yields different results under
  // different assignments — the core of boss-shuffle beatability. Self-contained
  // (local counts + default settings) so it doesn't disturb the shared fixtures.
  {
    uint8 bc[] = { OP_CAN_KILL_BOSS, 1 };  // dungeon 1 = EasternPalace
    RandoCounts c2;
    RandoSettings s2;
    Settings_SetDefaults(&s2);  // randomized weapons (not swordless)
    PredicateContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.counts = &c2;
    ctx.settings = &s2;

    // (a) NULL assignment -> vanilla boss: EP = Armos Knights (a single sword
    // suffices). No weapon -> false; a sword -> true.
    memset(&c2, 0, sizeof(c2));
    ctx.boss_assignment = NULL;
    LSC_ASSERT(Predicate_EvalCtx(bc, sizeof(bc), &ctx) == false,
               "CAN_KILL_BOSS(EP) vanilla(Armos) with no weapon should be false");
    c2.by_item_id[ITEM_L1Sword] = 1;
    LSC_ASSERT(Predicate_EvalCtx(bc, sizeof(bc), &ctx) == true,
               "CAN_KILL_BOSS(EP) vanilla(Armos) with a sword should be true");

    // (b) Shuffle Trinexx (boss-pool index 10) into EP. Trinexx needs FireRod
    // AND IceRod AND a real weapon tier — so the single L1 sword that killed
    // Armos is NOT enough. Proves the op tracks the SHUFFLED boss.
    uint8 boss_assignment[kRandoDungeonCount];
    memset(boss_assignment, 0xff, sizeof(boss_assignment));
    boss_assignment[1] = 10;  // EasternPalace <- Trinexx
    ctx.boss_assignment = boss_assignment;
    LSC_ASSERT(Predicate_EvalCtx(bc, sizeof(bc), &ctx) == false,
               "CAN_KILL_BOSS(EP) shuffled(Trinexx) with only an L1 sword should be false");
    memset(&c2, 0, sizeof(c2));
    c2.by_item_id[ITEM_FireRod] = 1;
    c2.by_item_id[ITEM_IceRod] = 1;
    c2.by_item_id[ITEM_ProgressiveSword] = 3;  // HasSword3 satisfies both sword conjuncts
    LSC_ASSERT(Predicate_EvalCtx(bc, sizeof(bc), &ctx) == true,
               "CAN_KILL_BOSS(EP) shuffled(Trinexx) with fire+ice+sword3 should be true");

    // (c) No-boss dungeon (HCE=0 -> vanilla map 0xFF) resolves to false, not OOB.
    uint8 bc0[] = { OP_CAN_KILL_BOSS, 0 };
    ctx.boss_assignment = NULL;
    LSC_ASSERT(Predicate_EvalCtx(bc0, sizeof(bc0), &ctx) == false,
               "CAN_KILL_BOSS(HCE=no boss) should be false");
  }

  // OP_TRICK / OP_GLITCH_LEVEL_AT_LEAST are wired (Slice 4) but resolve to their
  // OFF branch under default settings (tricks=0, logic=NoGlitches).
  {
    uint8 bc[] = { OP_TRICK, 5 };
    LSC_ASSERT(Predicate_Evaluate(bc, sizeof(bc), &counts, &settings) == false,
               "OP_TRICK(5) should be false under default settings (tricks=0)");
  }
  {
    uint8 bc[] = { OP_GLITCH_LEVEL_AT_LEAST, 1 };
    LSC_ASSERT(Predicate_Evaluate(bc, sizeof(bc), &counts, &settings) == false,
               "OP_GLITCH_LEVEL_AT_LEAST(1) should be false in Phase A (logic=NoGlitches)");
  }

  // ON-state coverage: assert the bit-shift / threshold math actually fires when
  // the axis is enabled. Without this, a regression in `1u << trick_id` or the
  // `>= level` compare passes both selftest and the (default-settings) corpus
  // silently and only surfaces in playtest — the exact gap this project keeps
  // hitting. Uses a scratch copy so the shared `settings` stays at defaults.
  {
    RandoSettings on = settings;
    uint8 bc[] = { OP_TRICK, 5 };
    on.tricks = (uint8)(1u << 5);
    LSC_ASSERT(Predicate_Evaluate(bc, sizeof(bc), &counts, &on) == true,
               "OP_TRICK(5) should be true when settings.tricks bit 5 is set");
    on.tricks = (uint8)(1u << 4);  // adjacent bit must NOT satisfy trick 5
    LSC_ASSERT(Predicate_Evaluate(bc, sizeof(bc), &counts, &on) == false,
               "OP_TRICK(5) must not be satisfied by an adjacent trick bit");
  }
  {
    RandoSettings on = settings;
    on.logic = 1;  // one glitch tier above NoGlitches
    uint8 bc1[] = { OP_GLITCH_LEVEL_AT_LEAST, 1 };
    LSC_ASSERT(Predicate_Evaluate(bc1, sizeof(bc1), &counts, &on) == true,
               "OP_GLITCH_LEVEL_AT_LEAST(1) should be true when settings.logic>=1");
    uint8 bc2[] = { OP_GLITCH_LEVEL_AT_LEAST, 2 };
    LSC_ASSERT(Predicate_Evaluate(bc2, sizeof(bc2), &counts, &on) == false,
               "OP_GLITCH_LEVEL_AT_LEAST(2) should be false when settings.logic==1");
  }
  // OP_MODEWEAPONS_EQ (swordless) — off under default (randomized=0), on at 3.
  {
    uint8 bc[] = { OP_MODEWEAPONS_EQ, 3 };  // 3 = swordless
    LSC_ASSERT(Predicate_Evaluate(bc, sizeof(bc), &counts, &settings) == false,
               "OP_MODEWEAPONS_EQ(swordless) should be false under default (randomized)");
    RandoSettings on = settings;
    on.mode_weapons = 3;
    LSC_ASSERT(Predicate_Evaluate(bc, sizeof(bc), &counts, &on) == true,
               "OP_MODEWEAPONS_EQ(swordless) should be true when mode_weapons==3");
    uint8 bc0[] = { OP_MODEWEAPONS_EQ, 0 };  // randomized
    LSC_ASSERT(Predicate_Evaluate(bc0, sizeof(bc0), &counts, &on) == false,
               "OP_MODEWEAPONS_EQ(randomized) should be false when mode_weapons==3");
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
  {
    // ON-state: difficulty axis above default must satisfy the higher threshold.
    RandoSettings on = settings;
    on.item_pool_difficulty = 2;
    uint8 bc[] = { OP_DIFFICULTY_AT_LEAST, 2 };
    LSC_ASSERT(Predicate_Evaluate(bc, sizeof(bc), &counts, &on) == true,
               "OP_DIFFICULTY_AT_LEAST(2) should be true when item_pool_difficulty>=2");
  }

  // Inverted reachability self-check. Verify the world-state
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
    // The Standard start region (LinksHouse) should NOT be in the reachable set
    // under Inverted. NOTE: this exclusion is by GRAPH TOPOLOGY — it is
    // not the Inverted start region and no edge points TO it — NOT by its
    // world_state_filter. Region world_state_filter is metadata that
    // reachability never consults (only LOCATIONS are ws-filtered); per-world
    // region scoping must therefore be done via edges, not the filter.
    if (lhs != 0xFFFF && lhs != lhi && inv_reach != NULL) {
      LSC_ASSERT(!Reachability_HasRegion(inv_reach, lhs),
                 "LinksHouse should NOT be reachable under world_state=Inverted "
                 "(gated by world_state_filter)");
    }
  }

  // genericKeys (Retro) small-key collapse — the cross-dungeon key dependency.
  // Under Retro, any per-dungeon small-key predicate is satisfied by holding
  // >=1 GenericKey, so a single generic key opens BOTH a PoD 5-key door AND a
  // TR 4-key door (the shared-pool semantics). Big keys are NOT collapsed. When
  // genericKeys is off the per-dungeon count rules unchanged.
  {
    RandoCounts gk;
    memset(&gk, 0, sizeof(gk));
    RandoSettings retro;
    Settings_SetDefaults(&retro);
    retro.world_state = kWorldState_Retro;

    // Operand bytes (item ids are u16 little-endian in the bytecode).
    #define LSC_U16(id) (uint8)((id) & 0xff), (uint8)((id) >> 8)
    uint8 pod5[] = { OP_HAS_AMOUNT, LSC_U16(ITEM_SmallKey_PalaceOfDarkness), 5 };
    uint8 tr4[]  = { OP_HAS_AMOUNT, LSC_U16(ITEM_SmallKey_TurtleRock), 4 };
    uint8 sp1[]  = { OP_HAS_ITEM,   LSC_U16(ITEM_SmallKey_SwampPalace) };
    uint8 bkpod[]= { OP_HAS_ITEM,   LSC_U16(ITEM_BigKey_PalaceOfDarkness) };

    // 0 generic keys → every small-key door closed (no per-dungeon keys exist).
    LSC_ASSERT(Predicate_Evaluate(pod5, sizeof(pod5), &gk, &retro) == false,
               "Retro: PoD 5-key door must be closed with 0 generic keys");
    LSC_ASSERT(Predicate_Evaluate(tr4, sizeof(tr4), &gk, &retro) == false,
               "Retro: TR 4-key door must be closed with 0 generic keys");

    // 1 generic key → BOTH the PoD-5 and TR-4 doors open (cross-dungeon share),
    // and the Swamp Palace single-key door opens — all from one shared key.
    gk.by_item_id[ITEM_GenericKey] = 1;
    LSC_ASSERT(Predicate_Evaluate(pod5, sizeof(pod5), &gk, &retro) == true,
               "Retro: 1 generic key must open the PoD 5-key door (collapse)");
    LSC_ASSERT(Predicate_Evaluate(tr4, sizeof(tr4), &gk, &retro) == true,
               "Retro: the SAME generic key must open the TR 4-key door (cross-dungeon)");
    LSC_ASSERT(Predicate_Evaluate(sp1, sizeof(sp1), &gk, &retro) == true,
               "Retro: 1 generic key must open the Swamp Palace small-key door");
    // Big keys keep per-dungeon identity — NOT satisfied by a generic key.
    LSC_ASSERT(Predicate_Evaluate(bkpod, sizeof(bkpod), &gk, &retro) == false,
               "Retro: a generic key must NOT satisfy a BigKey requirement");

    // Non-Retro: the collapse is OFF — a generic key does not open a small-key
    // door (the per-dungeon count rules, and no per-dungeon key is held).
    RandoSettings open;
    Settings_SetDefaults(&open);  // world_state defaults to Open
    LSC_ASSERT(Predicate_Evaluate(pod5, sizeof(pod5), &gk, &open) == false,
               "non-Retro: a generic key must NOT collapse small-key doors");
    #undef LSC_U16
  }

  fprintf(stderr, "[Logic_SelfCheck] OK\n");
}
