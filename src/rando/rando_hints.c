// rando_hints.c — Phase B Slice 5 (add-rando-hints) scaffold.
//
// STUB. See rando_hints.h for the contract. Real implementation is a
// translation of ALTTPR's app/Services/HintService.php (177 lines) +
// app/Text.php (1110 lines) into deterministic C, plus dialogue-ID
// allocation against the vanilla text-engine ID range, plus per-NPC
// dispatch in src/sprite_main.c. Multi-week task; deferred.

#include "rando_hints.h"

bool Rando_GenerateHints(const RandoSettings *settings,
                         const RandoPlacementTable *placements,
                         const RandoSpheres *spheres) {
  (void)settings;
  (void)placements;
  (void)spheres;
  // Stub: no-op. Real impl: walk placements + spheres, allocate per-NPC
  // dialogue IDs from the carved-out range, populate the hint set.
  return true;
}

uint16 Rando_GetHintDialogueId(RandoHintNpc npc) {
  (void)npc;
  return 0xFFFFu;  // sentinel for "no hint allocated" — caller uses vanilla
}

const char *Rando_GetHintString(RandoHintNpc npc) {
  (void)npc;
  return 0;  // NULL — no hint yet
}

void Rando_ClearHints(void) {
  // Stub: no state to clear yet.
}
