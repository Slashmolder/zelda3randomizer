// rando_hints.h — Phase B Slice 5 (add-rando-hints) scaffold.
//
// Status: STUB. Full hint generation pipeline (ALTTPR upstream
// HintService.php + Text.php, ~1287 lines) is multi-week work and is NOT
// implemented yet. This header pins the public API surface so dependent
// slices (Triforce Hunt UX, Murahdahla integration) can compile against
// it; all entry points return empty/no-op until the implementation lands.
//
// Per the proposal (openspec/changes/add-rando-hints/proposal.md):
//   - Hints come from 4 NPC sources: Sahasrahla telepathic tiles, the
//     Storyteller sprite, bookshelves, and Murahdahla (the Triforce Hunt
//     dark-world hint NPC).
//   - Hint set is generated deterministically at slot-creation time from
//     (settings, seed, placement, spheres) — no runtime RNG.
//   - Hints are stored in the slot's spoiler JSON under a `hints` section
//     and exposed at runtime via `Rando_GetHintDialogueId(npc_id)`.
//   - kGeneratorVersion bumps when the implementation actually changes
//     placement-affecting behavior — the stub does not, so the bump can
//     wait for the real implementation.

#ifndef ZELDA3_RANDO_HINTS_H_
#define ZELDA3_RANDO_HINTS_H_

#include "../types.h"
#include "rando_placement.h"

// Hint-NPC id space. Each value identifies one in-game hint site; the
// generator allocates a per-slot dialogue id for each. The order is
// stable for the spoiler-JSON `hints[]` array.
typedef enum RandoHintNpc {
  kRandoHintNpc_None = 0,
  // Light-world telepathic tiles. Per ALTTPR upstream: ~7 sites.
  kRandoHintNpc_SahasrahlaTile_North = 1,
  kRandoHintNpc_SahasrahlaTile_South = 2,
  // Storyteller (in-shrine NPC).
  kRandoHintNpc_Storyteller = 10,
  // Bookshelves (book-of-mudora interactions).
  kRandoHintNpc_Bookshelf = 11,
  // Triforce Hunt hint NPC.
  kRandoHintNpc_Murahdahla = 20,
  kRandoHintNpc__Count = 21,
} RandoHintNpc;

// Settings axis: hint sources active for this slot.
typedef enum RandoHintsMode {
  kHintsMode_Off = 0,       // no hints generated
  kHintsMode_Sahasrahla = 1,// only Sahasrahla-style tiles
  kHintsMode_Full = 2,      // all sources (Triforce-Hunt default)
} RandoHintsMode;

// Generate the per-slot hint set. Stub: clears state; full implementation
// is Phase B follow-up work. `settings` and `placements` are inputs (not
// modified). On success the hints are cached in module-internal state and
// readable via Rando_GetHintDialogueId / Rando_GetHintString.
//
// Returns true on success (always true today since the stub is a no-op).
bool Rando_GenerateHints(const RandoSettings *settings,
                         const RandoPlacementTable *placements,
                         const RandoSpheres *spheres);

// Look up the dialogue id assigned to `npc` in the active slot. Returns
// 0xFFFF when no hint is allocated for this NPC (caller falls back to the
// vanilla dialogue). STUB: always returns 0xFFFF until the generator
// lands.
uint16 Rando_GetHintDialogueId(RandoHintNpc npc);

// Retrieve the hint text string for `npc` for spoiler emission. STUB:
// returns NULL until the generator lands.
const char *Rando_GetHintString(RandoHintNpc npc);

// Clear the active hint set (called from Rando_DeactivateSlot). No-op in
// the stub.
void Rando_ClearHints(void);

#endif  // ZELDA3_RANDO_HINTS_H_
