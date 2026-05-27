// rando_hints.c — Phase B Slice 5 (add-rando-hints) generator body.
//
// Implements `Rando_GenerateHints` per the simplified design.md §57
// algorithm. Scope:
//
//   - 15 telepathic-tile hints, each pinned to a random non-junk
//     placement from the active seed.
//   - 1 Murahdahla hint, populated only when goal ∈ {TriforceHunt,
//     GanonHunt}, listing the regions that hold Triforce pieces.
//
// What this generator does NOT do (deferred to follow-up commits):
//
//   - Match ALTTPR's `HintService.applyHints` 6-step pool algorithm
//     line-by-line. The text we emit is `"<item> is at <location>."`
//     in a stable format; ALTTPR uses Location::getHint() with
//     per-location flavor text from `Text.php`. Translation is mechanical
//     once we own the text-engine intercept (#85).
//   - Wire `Rando_RemapTeleMsg` into the runtime message-engine read
//     path. Without that, the hints we generate are visible in the
//     spoiler only — telepathic tiles in-game still play vanilla
//     dialogue. The dispatch wiring is the playtest-gated piece per
//     the `logic_vs_runtime_gap` memo.
//   - Joke-hint fallback from ALTTPR's `strings/hint.txt` (60+
//     entries). Currently fills the 15 slots regardless; a future
//     pass can swap in joke text for slots where no interesting
//     placement is available.
//   - Murahdahla per-piece sphere annotation. Today emits a region
//     summary; ALTTPR groups by sphere.
//
// Determinism: hint text is byte-identical for a given (settings,
// seed_u64) pair — the sub-RNG seed comes from the placement-table
// digest, which is itself deterministic from those inputs.

#include "rando_hints.h"
#include "rando_logic.h"  // Rando_GetLocationName, Rando_GetItemName
#include "rando_rng.h"
#include "../types.h"
#include "third_party/sha256/sha256.h"

#include <stdio.h>
#include <string.h>

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

// Per-NPC hint storage. Sized exactly for the enum; index 0 (_None)
// is unused but kept so RandoHintNpc literals map directly.
#define kRandoHintTextMax 160

typedef struct HintEntry {
  uint8 active;
  uint16 placement_loc_id;
  uint16 placement_item_id;
  char text[kRandoHintTextMax];
} HintEntry;

static HintEntry g_hint_table[kRandoHintNpc__Count];

// Junk-item ids the generator filters out of the hintable pool. IDs
// per `assets/rando/item_registry.yaml`. Hinting "Rupee5 is at Mire
// Shed - Left" is noise; hinting "Hookshot is at Aginah's Cave" is
// useful. The PieceOfHeart and BossHeartContainer items are also
// filtered because in Phase A pools they tend to dominate the hint
// output. The list is conservative — when in doubt, the item stays
// hintable.
static bool item_is_junk(uint16 item_id) {
  switch (item_id) {
    case  50:  // PieceOfHeart
    case  51:  // BossHeartContainer
    case  99:  // Rupee1
    case 100:  // Rupee5
    case 101:  // Rupee20
    case 102:  // Rupee100
    case 103:  // Rupee300
    case 104:  // SmallMagic
    case 105:  // Arrow1
    case 106:  // Arrow10
    case 107:  // Bombs1
    case 108:  // Bombs3
    case 109:  // Bombs10
    case 110:  // Rupoor
      return true;
    default:
      return false;
  }
}

// TriforcePiece item id per item_registry.yaml. Used for the
// Murahdahla per-region count.
#define kItemId_TriforcePiece 52u

// Seed the sub-RNG deterministically from the placement table. Same
// (settings, seed_u64) → same placement table → same digest → same
// hint sub-RNG → same hints.
static void seed_hint_rng(const RandoPlacementTable *placements,
                          uint64 *out_seed) {
  uint8 digest[32];
  PlacementTable_ComputeDigest(placements, digest);
  uint64 seed = 0;
  for (int i = 0; i < 8; i++) {
    seed = (seed << 8) | (uint64)digest[i];
  }
  // XOR a magic constant so the hint RNG is decoupled from any
  // future direct-use of the placement digest as a seed elsewhere.
  // Magic = "HINT" ASCII repeated (48 49 4e 54 48 49 4e 54).
  *out_seed = seed ^ 0x48494E5448494E54ULL;
}

// Fisher-Yates shuffle of `arr[0..n)` using the sub-RNG.
static void shuffle_u16(RandoRng *rng, uint16 *arr, uint16 n) {
  for (uint16 i = n - 1; i > 0; i--) {
    uint32 j = Rng_NextRange(rng, i + 1);
    uint16 tmp = arr[i];
    arr[i] = arr[(uint16)j];
    arr[(uint16)j] = tmp;
  }
}

bool Rando_GenerateHints(const RandoSettings *settings,
                         const RandoPlacementTable *placements,
                         const RandoSpheres *spheres) {
  (void)spheres;  // Sphere data unused in this pass; reserved.
  Rando_ClearHints();

  if (settings == NULL || placements == NULL) return false;
  if (settings->hints == kHintsMode_Off) return true;  // Hints disabled.

  // Build the hintable-placement pool. Walk `placements->entries`,
  // skip junk items. The resulting indices are into `placements->entries`.
  uint16 hintable_indices[512];
  uint16 hintable_count = 0;
  uint16 entry_count = placements->count;
  if (entry_count > 512) entry_count = 512;
  for (uint16 i = 0; i < entry_count; i++) {
    if (item_is_junk(placements->entries[i].item_id)) continue;
    hintable_indices[hintable_count++] = i;
  }
  if (hintable_count == 0) return true;  // Nothing to hint about.

  // Deterministic sub-RNG.
  uint64 sub_seed;
  seed_hint_rng(placements, &sub_seed);
  RandoRng rng;
  Rng_SeedFromU64(&rng, sub_seed);

  // Shuffle the hintable pool so the 15 picks below sample without
  // replacement deterministically.
  shuffle_u16(&rng, hintable_indices, hintable_count);

  // Populate the 15 telepathic tiles (IDs 1..15). Stop early if we
  // run out of hintable placements (shouldn't happen at Phase A
  // pool sizes, but defensive).
  uint16 pool_cursor = 0;
  for (RandoHintNpc npc = kRandoHintNpc_TeleEasternPalace;
       npc <= kRandoHintNpc_TeleSouthEastDarkworldCave;
       npc++) {
    if (pool_cursor >= hintable_count) break;
    uint16 pick = hintable_indices[pool_cursor++];
    uint16 loc = placements->entries[pick].location_id;
    uint16 item = placements->entries[pick].item_id;
    HintEntry *e = &g_hint_table[npc];
    e->active = 1;
    e->placement_loc_id = loc;
    e->placement_item_id = item;
    snprintf(e->text, sizeof(e->text), "The %s lies at %s.",
             Rando_GetItemName(item), Rando_GetLocationName(loc));
  }

  // Murahdahla — populate when the goal is Triforce-related.
  if (settings->goal == kGoal_TriforceHunt || settings->goal == kGoal_GanonHunt) {
    // Count regions that hold at least one TriforcePiece placement.
    // Stash up to 8 region names for the summary text. We use a
    // small fixed buffer for simplicity; the summary truncates if
    // more than 8 distinct regions appear.
    uint8 seen_regions[16] = {0};
    uint8 seen_count = 0;
    uint8 piece_count = 0;
    for (uint16 i = 0; i < entry_count; i++) {
      if (placements->entries[i].item_id != kItemId_TriforcePiece) continue;
      piece_count++;
      // Look up region for this location. Skip if not found.
      uint16 loc_id = placements->entries[i].location_id;
      for (uint32 j = 0; j < kRandoLocationsCount; j++) {
        if (kRandoLocations[j].id != loc_id) continue;
        uint16 region_id = kRandoLocations[j].region_id;
        if (region_id == 0xFFFFu) break;
        // Already recorded?
        bool found = false;
        for (uint8 k = 0; k < seen_count; k++) {
          if (seen_regions[k] == (uint8)region_id) { found = true; break; }
        }
        if (!found && seen_count < (uint8)(sizeof(seen_regions))) {
          seen_regions[seen_count++] = (uint8)region_id;
        }
        break;
      }
    }
    HintEntry *e = &g_hint_table[kRandoHintNpc_Murahdahla];
    e->active = 1;
    e->placement_loc_id = 0xFFFFu;
    e->placement_item_id = kItemId_TriforcePiece;
    snprintf(e->text, sizeof(e->text),
             "Murahdahla: %u Triforce piece%s placed across %u region%s.",
             (unsigned)piece_count, piece_count == 1 ? "" : "s",
             (unsigned)seen_count, seen_count == 1 ? "" : "s");
  }

  return true;
}

uint16 Rando_GetHintDialogueId(RandoHintNpc npc) {
  if (npc <= kRandoHintNpc_None || npc >= kRandoHintNpc__Count) return 0xFFFFu;
  if (!g_hint_table[npc].active) return 0xFFFFu;
  return (uint16)(kRandoHintDialogueBase + ((uint16)npc - 1));
}

const char *Rando_GetHintString(RandoHintNpc npc) {
  if (npc <= kRandoHintNpc_None || npc >= kRandoHintNpc__Count) return 0;
  if (!g_hint_table[npc].active) return 0;
  return g_hint_table[npc].text;
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
  // Wiring this is #85 in the Phase B planning doc — playtest-gated.
  return vanilla_id;
}

void Rando_ClearHints(void) {
  memset(g_hint_table, 0, sizeof(g_hint_table));
}
