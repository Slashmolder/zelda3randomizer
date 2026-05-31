// shuffle_entrance.{c,h} — Phase C entrance shuffle, Stage 1 (coupled cave
// shuffle). See openspec/changes/add-rando-entrance-shuffle/{design,apply_plan}.md.
//
// ONE permutation π over the cave interiors drives TWO halves:
//   LOGIC   — each shuffled cave-location's effective region becomes the vanilla
//             region of whichever cave-door slot now leads to it (closed form;
//             reuses the per-location region-override seam in rando_logic.c).
//   RUNTIME — a shadow of the door→entrance-id table kOverworld_Entrance_Id
//             (g_asset_ptrs[126]) repointed so a door loads the permuted interior.
//             Coupling (enter A ⇒ exit A) is automatic for caves because
//             Dungeon_LoadEntrance caches the SOURCE overworld position at entry.
//
// Stage 1 scope: caves only, coupled, Open/Standard world states only
// (Inverted carries a static region override that this would clobber; Retro's
// TakeAny re-uses several cave host-rooms — both deferred). Default-off ⇒
// byte-identical to non-entrance-shuffle seeds.
#ifndef ZELDA3_RANDO_SHUFFLE_ENTRANCE_H_
#define ZELDA3_RANDO_SHUFFLE_ENTRANCE_H_

#include "../types.h"
#include "rando_settings.h"

#ifdef __cplusplus
extern "C" {
#endif

// Upper bound on the number of cave interiors permuted (the static registry has
// 38). Used to size caller assignment buffers.
#define kEntranceMaxInteriors 64

// True iff the active settings request an entrance shuffle that Stage 1
// supports: shuffle_cave_entrances on, world_state ∈ {Open, Standard}. Inverted
// and Retro are explicitly out of Stage 1 scope (return false).
bool Entrance_IsActive(const RandoSettings *settings);

// Compute the cave permutation for (seed, attempt) into `assign` (a permutation
// of [0, N) where assign[i] = the interior index now reached through interior
// i's overworld door). Returns N (the interior count), or 0 when not active.
// Pure + deterministic (xoshiro seeded by (seed, attempt)); no live assets.
int Entrance_ComputePermutation(const RandoSettings *settings, uint64 seed,
                                uint8 attempt,
                                uint8 assign[kEntranceMaxInteriors]);

// Install the per-seed cave region overrides into the logic graph from `assign`
// (so Logic_ComputeReachability / Place_AssumedFill / Goal_IsCompletable see the
// shuffled reachability). Clears any prior overrides first. `n` is the return of
// Entrance_ComputePermutation.
void Entrance_ApplyRegionOverrides(const uint8 *assign, int n);

// Clear the logic-graph cave region overrides (restore identity reachability).
// MUST be called after a generation attempt and on slot teardown so unrelated
// (non-entrance-shuffle) generations stay byte-identical.
void Entrance_ClearRegionOverrides(void);

// Build the runtime door overlay: copy the vanilla door→entrance-id table
// (`vanilla`, length `len` = g_asset_sizes[126]) into `overlay`, then for every
// door slot whose entrance-id belongs to a cave interior, rewrite it to the
// representative entrance-id of that interior's image under `assign`. Door slots
// for non-cave entrances are copied unchanged. Safe no-op if buffers are NULL.
void Entrance_BuildDoorOverlay(const uint8 *assign, int n,
                               const uint8 *vanilla, uint32 len,
                               uint8 *overlay);

// --- Dungeon pool (Stage 2) ---------------------------------------------------

// True iff dungeon entrance shuffle is active (shuffle_dungeon_entrances +
// Open/Standard). The 6 cleanly single-entrance dungeons shuffle among themselves.
bool Entrance_IsDungeonActive(const RandoSettings *settings);

// Compute the dungeon permutation for (seed, attempt) into `assign` (length =
// dungeon count). Independent of the cave permutation (distinct RNG salt).
int Entrance_ComputeDungeonPermutation(const RandoSettings *settings, uint64 seed,
                                       uint8 attempt,
                                       uint8 assign[kEntranceMaxInteriors]);

// Install / clear the per-seed dungeon EDGE overrides (door-edge destination
// remap by entry region) into the logic graph.
void Entrance_ApplyEdgeOverrides(const uint8 *assign, int n);
void Entrance_ClearEdgeOverrides(void);

// Remap dungeon door slots in `overlay` (built by Entrance_BuildDoorOverlay,
// which leaves dungeon slots = vanilla). Cave + dungeon entrance-id sets are
// disjoint, so the passes compose.
void Entrance_RemapDungeonDoors(const uint8 *assign, int n,
                                uint8 *overlay, uint32 len);

// Coupling: the exit room the room-keyed search should target so a player who
// entered the SOURCE dungeon door `vanilla_entrance_id` returns there. 0 if not
// a v1 dungeon door.
uint16 Entrance_DungeonSourceExitRoom(uint8 vanilla_entrance_id);

// --- Cross-category (Crossed, Stage 3) ---------------------------------------
// Active iff cross_category + cave + dungeon shuffle on + Open/Standard. When
// active, caves + cross-eligible dungeons shuffle in ONE combined pool (the
// separate cave/dungeon paths are NOT used).
bool Entrance_IsCrossActive(const RandoSettings *settings);
// Combined-pool permutation (caves then cross-eligible dungeons). Returns the
// pool size, or 0 when inactive.
int Entrance_ComputeCrossPermutation(const RandoSettings *settings, uint64 seed,
                                     uint8 attempt, uint8 assign[kEntranceMaxInteriors]);
// Install the 4-case cross-class logic overrides (region / region+predicate /
// edge-remap / edge-add) from the combined permutation.
void Entrance_ApplyCrossOverrides(const uint8 *assign, int n);
// Build the unified cross door overlay (each door → target endpoint's id).
void Entrance_BuildCrossOverlay(const uint8 *assign, int n, const uint8 *vanilla,
                                uint32 len, uint8 *overlay);
// True iff `vanilla_entrance_id` is a cave interior — the runtime forces the
// cached exit for a cave-source door (so a cave→dungeon cross stays coupled).
bool Entrance_IsCaveEntranceId(uint8 vanilla_entrance_id);

// --- Decoupled / per-endpoint ("Insanity", Stage 4) --------------------------
// Decoupled splits entry from exit: entering cave-hole i loads an interior (the
// existing π_in cave shuffle) but EXITING drops you at a DIFFERENT hole's
// overworld spot. The exit side is an INDEPENDENT uniform permutation π_out
// (distinct RNG salt from π_in); it is added to the graph as one-way warps that
// compose ADDITIVELY on top of the entry shuffle (warps only add reachability,
// so the model is conservative — it never strands what coupled reach allowed).
// D.1/D.2 = this logic+generation; D.3/D.4 runtime (baked cave-arrival table) is
// now wired, so decoupled CAVES are playable. Caves only (dungeon decoupled
// deferred — its arrival composition π_out∘π_in⁻¹ is future work).
//
// Active iff decoupled + shuffle_cave_entrances on + Open/Standard. (cross is a
// separate axis; decoupled composes on top of the cave entry shuffle.)
bool Entrance_IsDecoupledActive(const RandoSettings *settings);
// Net hole→exit-hole permutation for (seed, attempt) into `exit_assign` (length =
// cave interior count). Distinct RNG salt from the entry/dungeon/cross pools.
int Entrance_ComputeDecoupledExit(const RandoSettings *settings, uint64 seed,
                                  uint8 attempt, uint8 exit_assign[kEntranceMaxInteriors]);
// Add the per-seed one-way exit edges region(hole i) → region(exit_assign[i]) to
// the logic graph (on top of any entry overrides). Unconditional (a cave has no
// access gate beyond reaching its hole). Cleared by Entrance_ClearEdgeOverrides.
void Entrance_ApplyDecoupledExitEdges(const uint8 *exit_assign, int n);
// Spoiler: one-way "decoupled_exit" map (hole → exit hole).
void Entrance_WriteDecoupledSpoilerJson(void *file, const uint8 *exit_assign, int n);
void Entrance_WriteDecoupledSpoilerText(void *file, const uint8 *exit_assign, int n);
// D.4 runtime helpers: map an entrance-id → cave interior, and a cave interior's
// representative entrance-id (for the decoupled arrival-table keying + replay).
int Entrance_InteriorOfEntranceId(uint8 ent_id);
uint8 Entrance_CaveRepresentativeId(int interior);
// D.3 capture helpers: total cave-interior count (decoupled pool size) + name.
int Entrance_CaveInteriorCount(void);
const char *Entrance_CaveInteriorName(int interior);

// Emit the spoiler "entrance_mapping" section (door interior → loaded interior)
// for permutation `assign` (length `n`). The JSON form writes the whole
// `  "entrance_mapping": [ ... ],` block (2-space indent, trailing comma) to
// match the surrounding spoiler; the text form writes a labelled list. No-op
// when assign is NULL or n <= 0. `file` is a FILE* (void* to keep <stdio.h> out
// of the header).
void Entrance_WriteSpoilerJson(void *file, const uint8 *assign, int n);
void Entrance_WriteSpoilerText(void *file, const uint8 *assign, int n);
// Dungeon entrance_mapping section (Stage 2).
void Entrance_WriteDungeonSpoilerJson(void *file, const uint8 *assign, int n);
void Entrance_WriteDungeonSpoilerText(void *file, const uint8 *assign, int n);
// Cross-category combined-pool mapping section (Stage 3).
void Entrance_WriteCrossSpoilerJson(void *file, const uint8 *assign, int n);
void Entrance_WriteCrossSpoilerText(void *file, const uint8 *assign, int n);

// Self-test (pure; no live assets): permutation is a bijection, region-override
// closed form is self-consistent (a fixed point is a no-op; a swap moves regions
// symmetrically), and the door-overlay rewrite is coupled-consistent.
void Entrance_SelfCheck(void);

#ifdef __cplusplus
}
#endif

#endif  // ZELDA3_RANDO_SHUFFLE_ENTRANCE_H_
