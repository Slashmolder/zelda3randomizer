// rando.h — randomizer master header. Phase A0 stub.
//
// Per add-randomizer-support proposal: this module is the seed-generation
// pipeline, runtime override layer for vanilla item-grant sites, and host of
// the per-slot randomizer state. All public types and entry points the rest
// of the codebase consumes live here.
//
// Phase A0 status: this header declares the API surface. The .c files that
// implement it land incrementally through Phase A.

#ifndef ZELDA3_RANDO_H_
#define ZELDA3_RANDO_H_

#include "../types.h"

// ---------------------------------------------------------------------------
// kGeneratorVersion — bumped per tasks.md §13.6 whenever placement output
// could change. The bump triggers regression-corpus regeneration.
//
// Phase A0 starts at 0; first real placement-affecting change bumps to 1.
// ---------------------------------------------------------------------------
#define kGeneratorVersion 6u

// ---------------------------------------------------------------------------
// g_assets_hash — SHA-256 of the loaded asset blob (computed once after
// LoadAssets returns; see tasks.md §1.1a). Compared against kVanillaAssetsHash
// from the generated header src/rando/vanilla_assets_hash.h for the
// non-vanilla-asset-warning flow.
// ---------------------------------------------------------------------------
extern uint8 g_assets_hash[32];

// ---------------------------------------------------------------------------
// Rando_OnLocationCheck — the universal grant-site dispatcher
// (tasks.md §6.1; randomizer-placement spec).
//
// Called from every grant site listed in audit.md §0.3 with:
//   - location_id : ALTTPR canonical numeric id (from location_registry.yaml)
//   - vanilla_item_id : the item the vanilla game would have granted
//
// Returns the item_id to actually grant. On an unknown location_id (e.g., a
// rando slot from a future binary that has more locations than this one
// knows), the dispatcher returns vanilla_item_id unchanged so the vanilla
// grant path runs.
//
// Wrapped at each call site by:
//   if (enhanced_features1 & kFeatures1_RandomizerActive) {
//     item = Rando_OnLocationCheck(<location_id_const>, item);
//   }
// ---------------------------------------------------------------------------
uint16 Rando_OnLocationCheck(uint16 location_id, uint16 vanilla_item_id);

// ---------------------------------------------------------------------------
// Rando_BumpReachabilityCounter — invalidates the tracker's memoized
// reachability cache when a story-progress event flag is written
// (tasks.md §0.4a). Called from every reachability-affecting write site
// enumerated in audit.md §0.4a.
// ---------------------------------------------------------------------------
void Rando_BumpReachabilityCounter(void);

// ---------------------------------------------------------------------------
// Active per-seed shuffle assignments. The predicate VM's OP_HAS_PRIZE and
// OP_MEDALLION_OPENS consult these via PredicateContext; the placer + sphere
// computation + tracker call Logic_ComputeReachability which reads them.
//
// Callers MUST set these before generating reachability — otherwise both ops
// degrade to "false", which makes prize-gated locations (Sahasrahla, GT
// entry, Master Sword Pedestal) and medallion-gated dungeons (MM, TR)
// unreachable.
//
// Pass NULL to clear (resets to "no assignment installed" — falls back to
// false). Pointers are borrowed; caller retains ownership.
// ---------------------------------------------------------------------------
void Rando_SetDungeonPrizeAssignment(const uint8 *assignment);    // [kRandoDungeonCount]
void Rando_SetMedallionAssignment(const uint8 *assignment);       // [kRandoMedallionEntranceCount]
const uint8 *Rando_GetDungeonPrizeAssignment(void);
const uint8 *Rando_GetMedallionAssignment(void);

// ---------------------------------------------------------------------------
// Self-tests (tasks.md §2.2, §13.x). Always linked; CI invokes via
// `--rando-selftest`. Exits with code 2 on any failure.
// ---------------------------------------------------------------------------
void Rando_SelfCheck(void);            // SHA-256 NIST vectors
void Rando_RunAllSelfChecks(void);     // SHA-256 + RNG (+ future subsystems)

#endif  // ZELDA3_RANDO_H_

