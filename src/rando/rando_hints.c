// rando_hints.c — Phase B Slice 5 (add-rando-hints) scaffold.
//
// See rando_hints.h for the contract. This translation unit implements
// the public-API stubs + the ALTTPR-string-ID mapping. The actual
// hint-text generation pipeline (5-step algorithm in §57.2) lands in a
// follow-up commit.

#include "rando_hints.h"

// String IDs.
//   Indices 0-14 mirror `app/Services/HintService.php:59-75` exactly
//   (15 telepathic tiles).
//   Index 15 = ALTTPR Murahdahla.
//   Indices 16-19 = zelda3-fork extensions (NOT in ALTTPR), prefixed
//   `fork_` so spoiler-JSON diffing against ALTTPR seeds cleanly
//   distinguishes the core 16 from the fork extras.
// Indexed by `RandoHintNpc - 1` (skipping `_None`).
static const char *kRandoHintStringIds[kRandoHintNpc__Count - 1] = {
  "telepathic_tile_eastern_palace",
  "telepathic_tile_tower_of_hera_floor_4",
  "telepathic_tile_spectacle_rock",
  "telepathic_tile_swamp_entrance",
  "telepathic_tile_thieves_town_upstairs",
  "telepathic_tile_misery_mire",
  "telepathic_tile_palace_of_darkness",
  "telepathic_tile_desert_bonk_torch_room",
  "telepathic_tile_castle_tower",
  "telepathic_tile_ice_large_room",
  "telepathic_tile_turtle_rock",
  "telepathic_tile_ice_entrace",  // [sic] ALTTPR typo
  "telepathic_tile_ice_stalfos_knights_room",
  "telepathic_tile_tower_of_hera_entrance",
  "telepathic_tile_south_east_darkworld_cave",
  "murahdahla",
  // Fork extensions.
  "fork_storyteller",
  "fork_fortune_teller_kakariko",
  "fork_fortune_teller_dark_world",
  "fork_fortune_teller_lake_hylia",
};

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
  return 0xFFFFu;  // sentinel for "no hint allocated"
}

const char *Rando_GetHintString(RandoHintNpc npc) {
  (void)npc;
  return 0;  // NULL — no hint yet
}

const char *Rando_GetHintNpcStringId(RandoHintNpc npc) {
  if (npc <= kRandoHintNpc_None || npc >= kRandoHintNpc__Count) return 0;
  return kRandoHintStringIds[npc - 1];
}

uint16 Rando_RemapTeleMsg(uint16 vanilla_id, uint16 room_or_area, bool is_overworld) {
  (void)room_or_area;
  (void)is_overworld;
  // Stub: no remapping until the per-tile (area, vanilla_id) → RandoHintNpc
  // mapping table lands. Returns the vanilla dialogue id unchanged.
  return vanilla_id;
}

void Rando_ClearHints(void) {
  // Stub: no state to clear yet.
}
