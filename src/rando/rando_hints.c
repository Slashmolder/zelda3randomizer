// rando_hints.c — Phase B Slice 5 (add-rando-hints) generator body.
//
// Implements `Rando_GenerateHints` per the simplified design.md §57
// algorithm. Scope:
//
//   - 15 telepathic-tile hints, each pinned to a random non-junk
//     placement from the active seed.
//   - 1 Murahdahla hint, populated only when goal ∈ {TriforceHunt,
//     GanonHunt}, listing the regions that hold Triforce pieces.
//   - Confirmed vanilla dialogue item-location claims, redirected at dialogue
//     load time to the active placement without consuming a generated slot.
//
// What this generator does NOT do (deferred to follow-up commits):
//
//   - Match ALTTPR's `HintService.applyHints` 6-step pool algorithm
//     line-by-line. The text we emit is `"<item> is at <location>."`
//     in a stable format; ALTTPR uses Location::getHint() with
//     per-location flavor text from `Text.php`. Translation is mechanical
//     once we own the text-engine intercept (#85).
//   - `Rando_RemapTeleMsg` is a vestigial stub from an earlier design and
//     is NOT on the live path. The runtime intercept is wired directly:
//     `Text_LoadCharacterBuffer` (messaging.c) calls
//     `Rando_RenderHintMessage` before the vanilla dialogue decode, so a
//     read telepathic tile shows the generated hint in-game (not just in
//     the spoiler) whenever the slot was generated with HINTS on.
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
#include "item_ids.h"     // ITEM_* symbols (codegen from item_registry.yaml)
#include "location_ids.h" // LOC_* symbols (codegen from location_registry.yaml)
#include "rando.h"        // Rando_GetActiveSettings
#include "rando_logic.h"  // Rando_GetLocationName, Rando_GetItemName
#include "rando_rng.h"
#include "rando_settings.h"  // Settings_SetDefaults for Hints_SelfCheck
#include "../types.h"
#include "../variables.h"    // g_ram (backs the g_rando_slot_active macro)
#include "../features.h"     // g_rando_slot_active (g_ram macro)
#include "../zelda_rtl.h"    // g_zenv.dialogue_flags
#include "third_party/sha256/sha256.h"

#include <stdio.h>
#include <stdlib.h>  // abort
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
#define kHintMaxCharsPerLine 13  // VWF is variable-width; 13 is a safe cap that
                                 // avoids overrun on the 256-px message box for
                                 // worst-case wide glyphs.

typedef struct HintEntry {
  uint8 active;
  uint16 placement_loc_id;
  uint16 placement_item_id;
  char text[kRandoHintTextMax];
} HintEntry;

static HintEntry g_hint_table[kRandoHintNpc__Count];

// Vanilla dialogue whose item/location claim becomes false under randomized
// placement. These are runtime dialogue_message_index values, NOT the one-based
// row numbers printed in assets/dialogue.txt (see the OpenSpec audit).
typedef enum RandoDialogueHintRedirectKind {
  // A named fixed item moved away from the vanilla location. Resolve item->loc.
  kDialogueHintRedirect_ItemLocation = 0,
  // A physical surface describes the reward at one fixed location. Resolve loc->item.
  kDialogueHintRedirect_LocationItem,
  // Item->loc, but the replacement must retain a live Yes/No command.
  kDialogueHintRedirect_ItemLocationChoice,
} RandoDialogueHintRedirectKind;

typedef struct RandoDialogueHintRedirect {
  uint16 message_id;
  uint16 item_id;
  uint16 location_id;
  const char *source_name;
  uint8 flags;
  uint8 kind;
} RandoDialogueHintRedirect;

enum {
  kDialogueHintRedirectFlag_None = 0,
  kDialogueHintRedirectFlag_BumperCaveSign = 1,
};

static const RandoDialogueHintRedirect kDialogueHintRedirects[] = {
  {0x033, ITEM_Prize_GreenPendant, 0xFFFFu, "Sahasrahla",
   kDialogueHintRedirectFlag_None, kDialogueHintRedirect_ItemLocation},
  {0x036, ITEM_MoonPearl, 0xFFFFu, "Post-Agahnim Sahasrahla",
   kDialogueHintRedirectFlag_None, kDialogueHintRedirect_ItemLocation},
  {0x09E, ITEM_MoonPearl, 0xFFFFu, "Old mountain man",
   kDialogueHintRedirectFlag_None, kDialogueHintRedirect_ItemLocation},
  {0x0A8, ITEM_PieceOfHeart, LOC_Bumper_Cave, "Bumper Cave sign",
   kDialogueHintRedirectFlag_BumperCaveSign, kDialogueHintRedirect_LocationItem},
  {0x0E5, ITEM_OcarinaInactive, 0xFFFFu, "Stumpy / Flute Boy",
   kDialogueHintRedirectFlag_None, kDialogueHintRedirect_ItemLocationChoice},
  {0x125, ITEM_BookOfMudora, 0xFFFFu, "Aginah",
   kDialogueHintRedirectFlag_None, kDialogueHintRedirect_ItemLocation},
  {0x15D, ITEM_MoonPearl, 0xFFFFu, "Dark-World bully",
   kDialogueHintRedirectFlag_None, kDialogueHintRedirect_ItemLocation},
};

typedef enum RandoDialogueHintRedirectStatus {
  kDialogueHintRedirect_NotRecognized = 0,
  kDialogueHintRedirect_SlotInactive,
  kDialogueHintRedirect_UnsupportedLocale,
  kDialogueHintRedirect_SettingsUnavailable,
  kDialogueHintRedirect_HintsOff,
  kDialogueHintRedirect_DiscriminatorMismatch,
  kDialogueHintRedirect_PlacementUnavailable,
  kDialogueHintRedirect_ItemAbsent,
  kDialogueHintRedirect_LocationAbsent,
  kDialogueHintRedirect_Active,
} RandoDialogueHintRedirectStatus;

typedef struct RandoDialogueHintResolution {
  const RandoDialogueHintRedirect *redirect;
  RandoDialogueHintRedirectStatus status;
  uint16 item_id;
  uint16 location_id;
} RandoDialogueHintResolution;

static const RandoDialogueHintRedirect *dialogue_hint_redirect_for_msg(uint16 msg_id) {
  for (size_t i = 0; i < sizeof(kDialogueHintRedirects) / sizeof(kDialogueHintRedirects[0]); i++) {
    if (kDialogueHintRedirects[i].message_id == msg_id) return &kDialogueHintRedirects[i];
  }
  return NULL;
}

static bool dialogue_hint_redirect_discriminator_matches(
    const RandoDialogueHintRedirect *redirect, bool is_indoors,
    uint16 overworld_screen) {
  if (redirect == NULL) return false;
  switch (redirect->flags) {
  case kDialogueHintRedirectFlag_None:
    return true;
  case kDialogueHintRedirectFlag_BumperCaveSign:
    return !is_indoors && (uint8)overworld_screen == 0x4A;
  default:
    // Fail closed: adding a shared ID requires an explicit context predicate.
    return false;
  }
}

static RandoDialogueHintResolution resolve_dialogue_hint_for_context(
    uint16 msg_id, bool slot_active, uint8 dialogue_flags,
    const RandoSettings *settings, const RandoPlacementTable *placements,
    bool is_indoors, uint16 overworld_screen) {
  RandoDialogueHintResolution out = {
    dialogue_hint_redirect_for_msg(msg_id), kDialogueHintRedirect_NotRecognized,
    0xFFFFu, 0xFFFFu,
  };
  if (out.redirect == NULL) return out;
  if (out.redirect->kind != kDialogueHintRedirect_LocationItem)
    out.item_id = out.redirect->item_id;
  if (!slot_active) {
    out.status = kDialogueHintRedirect_SlotInactive;
    return out;
  }
  if (dialogue_flags != 0) {
    out.status = kDialogueHintRedirect_UnsupportedLocale;
    return out;
  }
  if (settings == NULL) {
    out.status = kDialogueHintRedirect_SettingsUnavailable;
    return out;
  }
  if (settings->hints != kHintsMode_On) {
    out.status = kDialogueHintRedirect_HintsOff;
    return out;
  }
  if (!dialogue_hint_redirect_discriminator_matches(out.redirect, is_indoors,
                                                     overworld_screen)) {
    out.status = kDialogueHintRedirect_DiscriminatorMismatch;
    return out;
  }
  if (placements == NULL || placements->entries == NULL) {
    out.status = kDialogueHintRedirect_PlacementUnavailable;
    return out;
  }

  if (out.redirect->kind == kDialogueHintRedirect_LocationItem) {
    for (uint16 i = 0; i < placements->count; i++) {
      if (placements->entries[i].location_id == out.redirect->location_id) {
        out.location_id = placements->entries[i].location_id;
        out.item_id = placements->entries[i].item_id;
        break;
      }
    }
    out.status = out.location_id == 0xFFFFu ? kDialogueHintRedirect_LocationAbsent
                                            : kDialogueHintRedirect_Active;
    return out;
  }

  // Customizers may deliberately produce duplicate target items. Pick the
  // lowest location ID independently of placement-table iteration order.
  for (uint16 i = 0; i < placements->count; i++) {
    if (placements->entries[i].item_id == out.item_id &&
        (out.location_id == 0xFFFFu ||
         placements->entries[i].location_id < out.location_id))
      out.location_id = placements->entries[i].location_id;
  }
  out.status = out.location_id == 0xFFFFu ? kDialogueHintRedirect_ItemAbsent
                                          : kDialogueHintRedirect_Active;
  return out;
}

static RandoDialogueHintResolution resolve_active_dialogue_hint(uint16 msg_id) {
  if (!g_rando_slot_active) {
    return resolve_dialogue_hint_for_context(
        msg_id, false, g_zenv.dialogue_flags, NULL, NULL,
        player_is_indoors != 0, overworld_screen_index);
  }
  return resolve_dialogue_hint_for_context(
      msg_id, true, g_zenv.dialogue_flags, Rando_GetActiveSettings(),
      Placement_GetActive(), player_is_indoors != 0, overworld_screen_index);
}

// Junk-item ids the generator filters out of the hintable pool.
// Item ids are sourced from `src/rando/item_ids.h` (codegen from
// `assets/rando/item_registry.yaml`) so registry re-ids don't silently
// misclassify live progression items as junk (or vice versa).
//
// Hinting "Rupee5 is at Mire Shed - Left" is noise; hinting "Hookshot
// is at Aginah's Cave" is useful. The PieceOfHeart and BossHeartContainer
// items are also filtered because in Phase A pools they tend to dominate
// the hint output. The list is conservative — when in doubt, the item
// stays hintable.
static bool item_is_junk(uint16 item_id) {
  // add-rando-trap-catalog — every trap effect id in the contiguous block is junk
  // for hinting (traps are never hinted/pointed-to).
  if (item_id >= ITEM_TrapDamage && item_id <= ITEM_TrapTeleport) return true;
  // add-rando-pot-sanity — ITEM_Nothing fills empty pots; an empty pot must never
  // be a hint source ("Nothing is in <pot>" is noise). Junk-holding pots (rupees/
  // bombs/...) are already excluded by the cases below, and progression-holding
  // pots (a key/medallion under a pot) stay hint-eligible — exactly D12's rule.
  if (item_id == ITEM_Nothing) return true;
  switch (item_id) {
    case ITEM_PieceOfHeart:
    case ITEM_BossHeartContainer:
    case ITEM_Rupee1:
    case ITEM_Rupee5:
    case ITEM_Rupee20:
    case ITEM_Rupee100:
    case ITEM_Rupee300:
    case ITEM_SmallMagic:
    case ITEM_Arrow1:
    case ITEM_Arrow10:
    case ITEM_Bombs1:
    case ITEM_Bombs3:
    case ITEM_Bombs10:
    case ITEM_Rupoor:
      return true;
    default:
      return false;
  }
}

static bool loc_is_medallion_config(uint16 loc_id) {
  for (uint32 i = 0; i < kRandoLocationsCount; i++) {
    if (kRandoLocations[i].id == loc_id)
      return kRandoLocations[i].type == LOCTYPE_Medallion;
  }
  return false;
}

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

// Produce a short, player-friendly item name from the internal registry name so
// a hint fits one 3-row message box. Strips placeholder prefixes (Prize_/
// Progressive), collapses dungeon items to their kind, and splits CamelCase /
// underscores into words.
static void hint_friendly_item(const char *in, char *out, int outsz) {
  if (in == NULL) { out[0] = '\0'; return; }
  if (strncmp(in, "Prize_Crystal", 13) == 0) { snprintf(out, outsz, "Crystal %s", in + 13); return; }
  if (strncmp(in, "SmallKey_", 9) == 0) { snprintf(out, outsz, "Small Key"); return; }
  if (strncmp(in, "BigKey_", 7) == 0)   { snprintf(out, outsz, "Big Key");   return; }
  if (strncmp(in, "Compass_", 8) == 0)  { snprintf(out, outsz, "Compass");   return; }
  if (strncmp(in, "Map_", 4) == 0)      { snprintf(out, outsz, "Map");       return; }
  if (strcmp(in, "BugCatchingNet") == 0) { snprintf(out, outsz, "Net");     return; }
  if (strcmp(in, "DefeatAgahnim") == 0)  { snprintf(out, outsz, "Agahnim"); return; }
  if (strncmp(in, "Prize_", 6) == 0) in += 6;         // Prize_GreenPendant -> GreenPendant
  if (strncmp(in, "Progressive", 11) == 0) in += 11;  // ProgressiveSword -> Sword
  // add-npc-souls: Soul_Npc_KingZora -> "King Zora Soul" (the raw "Npc" token
  // is an internal namespace, not player text). Enemy souls keep their
  // existing "Soul <Name>" rendering via the generic pass below.
  bool npc_soul = false;
  if (strncmp(in, "Soul_Npc_", 9) == 0) { in += 9; npc_soul = true; }
  int o = 0;
  for (int i = 0; in[i] && o < outsz - 2; i++) {
    char c = in[i];
    if (c == '_') { out[o++] = ' '; continue; }
    if (i > 0 && c >= 'A' && c <= 'Z' && in[i - 1] >= 'a' && in[i - 1] <= 'z')
      out[o++] = ' ';  // split CamelCase: space before a capital following a lowercase
    out[o++] = c;
  }
  out[o] = '\0';
  if (npc_soul && o < outsz - 6) {
    snprintf(out + o, (size_t)(outsz - o), " Soul");
  }
}

// Friendly location: keep just the area/room, dropping the " - <sub-spot>" tail.
static void hint_friendly_loc(const char *in, char *out, int outsz) {
  if (in == NULL) { out[0] = '\0'; return; }
  const char *dash = strstr(in, " - ");
  int n = dash ? (int)(dash - in) : (int)strlen(in);
  if (n > outsz - 1) n = outsz - 1;
  memcpy(out, in, (size_t)n);
  out[n] = '\0';
}

typedef struct HintLocationAlias {
  const char *long_name;
  const char *short_name;
} HintLocationAlias;

// Player-facing short forms for common long base-location names. Generated
// pot/enemy/terrain rows are handled by the deterministic acronym fallback
// below; these overrides keep prominent vanilla locations recognizable.
static const HintLocationAlias kHintLocationAliases[] = {
  {"Sahasrahla's Hut", "Sahasrahla"},
  {"Master Sword Pedestal", "MS Pedestal"},
  {"Checkerboard Cave", "Checker Cave"},
  {"Dark World Village of Outcasts", "DW Outcasts"},
  {"Dark World Lumberjack Hut", "DW Lumber Hut"},
  {"Dark World Lake Hylia Shop", "DW Hylia Shop"},
  {"Light World Lake Hylia Shop", "LW Hylia Shop"},
  {"Dark World Death Mountain Shop", "DW DM Shop"},
  {"Light World Death Mountain Shop", "LW DM Shop"},
  {"Dark Lake Hylia Ledge Spike Cave", "DW Spike Cave"},
  {"Dark Lake Hylia Ledge Fairy", "DW Ledge Fairy"},
  {"Lake Hylia Fortune Teller", "Hylia Fortune"},
  {"Fortune Teller (Light)", "LW Fortune"},
  {"Archery Game (Take-Any)", "Archery Game"},
  {"Good Bee Cave (Take-Any)", "Good Bee Cave"},
  {"Hookshot Fairy (Take-Any)", "Hookshot Fairy"},
};

static bool hint_is_separator(char c) {
  return c == ' ' || c == '_' || c == '(' || c == ')' || c == '/' || c == '-';
}

static char hint_ascii_upper(char c) {
  return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

// Compact an arbitrary registry location without silently chopping off its
// distinguishing tail. The result keeps uppercase/CamelCase initials from all
// prefix tokens plus the final token in full. Examples:
//   HyruleCastleEscape Pot R002 P2650 -> HCEPR P2650
//   LightWorld_NorthWest Bush S00 P0338 -> LWNWBS P0338
static void hint_compact_loc(const char *in, char *out, int outsz) {
  char base[128];
  hint_friendly_loc(in, base, sizeof(base));
  for (size_t i = 0; i < sizeof(kHintLocationAliases) / sizeof(kHintLocationAliases[0]); i++) {
    if (strcmp(base, kHintLocationAliases[i].long_name) == 0) {
      snprintf(out, (size_t)outsz, "%s", kHintLocationAliases[i].short_name);
      return;
    }
  }

  const char *token_start[24];
  int token_len[24];
  int tokens = 0;
  for (int i = 0; base[i] != '\0' && tokens < 24;) {
    while (base[i] != '\0' && hint_is_separator(base[i])) i++;
    if (base[i] == '\0') break;
    int start = i;
    while (base[i] != '\0' && !hint_is_separator(base[i])) i++;
    token_start[tokens] = &base[start];
    token_len[tokens] = i - start;
    tokens++;
  }
  if (tokens == 0) {
    out[0] = '\0';
    return;
  }

  int w = 0;
  for (int i = 0; i < tokens - 1 && w < outsz - 2; i++) {
    bool emitted = false;
    for (int j = 0; j < token_len[i] && w < outsz - 2; j++) {
      char c = token_start[i][j];
      if (c >= 'A' && c <= 'Z') {
        out[w++] = c;
        emitted = true;
      }
    }
    if (!emitted) out[w++] = hint_ascii_upper(token_start[i][0]);
  }
  if (tokens > 1 && w < outsz - 1) out[w++] = ' ';

  const char *last = token_start[tokens - 1];
  int last_len = token_len[tokens - 1];
  if (last_len <= kHintMaxCharsPerLine) {
    for (int j = 0; j < last_len && w < outsz - 1; j++) out[w++] = last[j];
  } else {
    bool emitted = false;
    for (int j = 0; j < last_len && w < outsz - 1; j++) {
      if (last[j] >= 'A' && last[j] <= 'Z') {
        out[w++] = last[j];
        emitted = true;
      }
    }
    if (!emitted) {
      for (int j = 0; j < last_len && j < kHintMaxCharsPerLine && w < outsz - 1; j++)
        out[w++] = last[j];
    }
  }
  out[w] = '\0';
}

static bool npc_hint_item_name(uint16 item_id, char *out, int outsz) {
  if (out == NULL || outsz <= 0) return false;
  // Player-facing short forms for long or internal registry tokens. The
  // location->item Bumper sign can surface any item, so keep this helper shared
  // by both redirect directions.
  switch (item_id) {
  case ITEM_BookOfMudora:   snprintf(out, (size_t)outsz, "Book"); return true;
  case ITEM_OcarinaInactive: snprintf(out, (size_t)outsz, "Flute"); return true;
  case ITEM_PieceOfHeart:   snprintf(out, (size_t)outsz, "Heart Piece"); return true;
  case ITEM_BottleEmpty:    snprintf(out, (size_t)outsz, "Bottle"); return true;
  default:
    hint_friendly_item(Rando_GetItemName(item_id), out, outsz);
    return out[0] != '\0';
  }
}

typedef struct HintDungeonAlias {
  const char *registry_suffix;
  const char *short_name;
} HintDungeonAlias;

// Dungeon items normally use a generic kind in generated hints, where the
// hinted location supplies the useful context. A location->item surface has no
// such context: if Bumper Cave holds a wild dungeon item, its sign must retain
// which dungeon that key/map/compass belongs to.
static const HintDungeonAlias kHintDungeonAliases[] = {
  {"HyruleCastleEscape", "Escape"},
  {"EasternPalace", "Eastern"},
  {"DesertPalace", "Desert"},
  {"TowerOfHera", "Hera"},
  {"HyruleCastleTower", "Castle Tower"},
  {"PalaceOfDarkness", "PoD"},
  {"SwampPalace", "Swamp"},
  {"SkullWoods", "Skull"},
  {"ThievesTown", "Thieves"},
  {"IcePalace", "Ice"},
  {"MiseryMire", "Mire"},
  {"TurtleRock", "Turtle Rock"},
  {"GanonsTower", "GT"},
};

static bool location_hint_dungeon_item_name(const char *raw_name, char *out,
                                            int outsz) {
  static const struct {
    const char *prefix;
    const char *kind;
  } kinds[] = {
    {"SmallKey_", "Small Key"},
    {"BigKey_", "Big Key"},
    {"Map_", "Map"},
    {"Compass_", "Compass"},
  };

  if (raw_name == NULL || out == NULL || outsz <= 0) return false;
  for (size_t k = 0; k < sizeof(kinds) / sizeof(kinds[0]); k++) {
    size_t prefix_len = strlen(kinds[k].prefix);
    if (strncmp(raw_name, kinds[k].prefix, prefix_len) != 0) continue;
    const char *suffix = raw_name + prefix_len;
    for (size_t d = 0; d < sizeof(kHintDungeonAliases) /
                               sizeof(kHintDungeonAliases[0]); d++) {
      if (strcmp(suffix, kHintDungeonAliases[d].registry_suffix) == 0) {
        snprintf(out, (size_t)outsz, "%s %s",
                 kHintDungeonAliases[d].short_name, kinds[k].kind);
        return true;
      }
    }
    return false;
  }
  return false;
}

static bool location_hint_item_name(uint16 item_id, char *out, int outsz) {
  const char *raw_name = Rando_GetItemName(item_id);
  if (location_hint_dungeon_item_name(raw_name, out, outsz)) return true;
  // A future dungeon suffix must add an explicit qualified alias. Falling back
  // to the shared helper here would silently collapse it to an ambiguous kind.
  if (raw_name != NULL &&
      (strncmp(raw_name, "SmallKey_", 9) == 0 ||
       strncmp(raw_name, "BigKey_", 7) == 0 ||
       strncmp(raw_name, "Map_", 4) == 0 ||
       strncmp(raw_name, "Compass_", 8) == 0))
    return false;
  return npc_hint_item_name(item_id, out, outsz);
}

// Populate one item-location hint slot from placement index `pick`. Shared by
// the telepathic-tile loop and the fork-extension NPC loop so their text format
// can never drift.
static void hint_set_item_placement(RandoHintNpc npc,
                                    const RandoPlacementTable *placements,
                                    uint16 pick) {
  uint16 loc = placements->entries[pick].location_id;
  uint16 item = placements->entries[pick].item_id;
  HintEntry *e = &g_hint_table[npc];
  e->active = 1;
  e->placement_loc_id = loc;
  e->placement_item_id = item;
  char fitem[48], floc[48];
  hint_friendly_item(Rando_GetItemName(item), fitem, sizeof fitem);
  hint_friendly_loc(Rando_GetLocationName(loc), floc, sizeof floc);
  snprintf(e->text, sizeof(e->text), "%s is in %s", fitem, floc);
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
  //
  // Sized by the module-wide location ceiling (kRandoLocationCapacity,
  // rando_logic.h): the hintable pool must hold every placement, and the
  // placement table is capacity-bounded. A smaller cap would silently drop
  // high-location_id placements from hint sourcing. The registry-vs-capacity
  // tie is the LOC__COUNT <= kRandoLocationCapacity assert in rando.c.
  uint16 hintable_indices[kRandoLocationCapacity];
  uint16 hintable_count = 0;
  uint16 entry_count = placements->count;
  if (entry_count > kRandoLocationCapacity) entry_count = kRandoLocationCapacity;
  for (uint16 i = 0; i < entry_count; i++) {
    if (loc_is_medallion_config(placements->entries[i].location_id)) continue;
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
    hint_set_item_placement(npc, placements, hintable_indices[pool_cursor++]);
  }

  // Fork-extension NPCs (ids 17-19): the Storyteller + Kakariko/Dark-World
  // Fortune Tellers route existing vanilla NPC dialogue through the hint
  // generator for extra in-game hint locations. They draw the NEXT picks from
  // the same shuffled pool (pool_cursor continues), so the 15 tele-tile picks
  // above are unchanged and a fork hint never duplicates a tile hint. The
  // Lake-Hylia Fortune Teller (id 20) shares its room with the Kakariko one and
  // has no runtime discriminator, so it is intentionally NOT populated — at
  // runtime it surfaces the Kakariko hint (see Rando_RenderHintMessage).
  for (RandoHintNpc npc = kRandoHintNpc_ForkStoryteller;
       npc <= kRandoHintNpc_ForkFortuneTellerDark;  // 17..19; skip 20 Lake Hylia
       npc++) {
    if (pool_cursor >= hintable_count) break;
    hint_set_item_placement(npc, placements, hintable_indices[pool_cursor++]);
  }

  // Murahdahla — populate when the goal is Triforce-related.
  if (settings->goal == kGoal_TriforceHunt || settings->goal == kGoal_GanonHunt) {
    // Count regions that hold at least one TriforcePiece placement.
    // Stash the distinct region ids in a fixed buffer for the summary text.
    // The buffer MUST cover every region or the "across N regions" tally
    // silently caps and under-reports: there are 26 distinct location regions
    // today (location_registry.yaml) and pieces_placed can reach ~50, so a
    // [16] cap mis-counted wide Triforce-Hunt seeds. Sized to 64 for append-only
    // headroom. region_id is uint16 in `RandoLocationDef`; the dedupe array
    // matches that width so two distinct regions ≥ 256 don't collide.
    uint16 seen_regions[64] = {0};
    uint8 seen_count = 0;
    uint16 piece_count = 0;  // uint16 — `settings.pieces_placed` is uint16
                              // (range up to 65535); a uint8 here would wrap
                              // silently past 255. Practical TH max is ~50
                              // but the type pin guards against future axis
                              // widening.
    for (uint16 i = 0; i < entry_count; i++) {
      if (loc_is_medallion_config(placements->entries[i].location_id)) continue;
      if (placements->entries[i].item_id != ITEM_TriforcePiece) continue;
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
          if (seen_regions[k] == region_id) { found = true; break; }
        }
        if (!found && seen_count < (uint8)(sizeof(seen_regions) / sizeof(seen_regions[0]))) {
          seen_regions[seen_count++] = region_id;
        }
        break;
      }
    }
    HintEntry *e = &g_hint_table[kRandoHintNpc_Murahdahla];
    e->active = 1;
    e->placement_loc_id = 0xFFFFu;
    e->placement_item_id = ITEM_TriforcePiece;
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

// ---------------------------------------------------------------------------
// Telepathic-tile runtime dispatch (Phase B Slice 5 §57.3).
//
// g_hint_table stores plain ASCII text (see HintEntry.text). The messaging
// engine consumes FONT CHARACTER CODES (the post-dictionary decode stream),
// so the dispatch path below encodes ASCII -> US font codes mirroring
// assets/text_compression.py::kTextAlphabet_US and emits the message-box line
// commands the renderer's Text_DecodeCmd understands.
// ---------------------------------------------------------------------------

// Count of telepathic-tile hint slots = the 15 contiguous Tele* enumerators
// (kRandoHintNpc_TeleEasternPalace .. kRandoHintNpc_TeleSouthEastDarkworldCave).
#define kHintTileCount \
  (kRandoHintNpc_TeleSouthEastDarkworldCave - kRandoHintNpc_TeleEasternPalace + 1)

// The 15 hint-bearing vanilla US telepathic-tile message ids, in ascending
// order. Index i maps to RandoHintNpc (kRandoHintNpc_TeleEasternPalace + i).
// NOTE: 0xB4 IS the Eastern Palace tile ("...the treasure hidden in this palace
// to defeat armored foes"), the FIRST ALTTPR tile — not generic filler. An
// earlier off-by-one (reading a 1-indexed dialogue dump as 0-indexed) dropped
// it and instead included 0xC7, which is actually the Chris Houlihan secret-room
// text and not a hint tile. 0xB4 is set as dialogue only by the tele table
// (Dungeon_GetTeleMsg), so intercepting it is safe; among the 15 tiles only
// Eastern Palace happens to use the value that doubles as the room default.
static const uint16 kHintTileMsgIds[kHintTileCount] = {
  0xB4, 0xB5, 0xB8, 0xB9, 0xBA, 0xBB, 0xBE, 0xBF,
  0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6,
};

bool Rando_IsHintTileMessage(uint16 msg_id) {
  for (int i = 0; i < kHintTileCount; i++) {
    if (kHintTileMsgIds[i] == msg_id) return true;
  }
  return false;
}

// ASCII -> US font character code. Mirrors kTextAlphabet_US (text_compression.py).
// Returns 0xFF for characters with no glyph in the US alphabet (caller maps
// those to a space). Note the US alphabet has NO ':' glyph.
static uint8 ascii_to_font(char ch) {
  if (ch >= 'A' && ch <= 'Z') return (uint8)(ch - 'A');            // 0..25
  if (ch >= 'a' && ch <= 'z') return (uint8)(26 + (ch - 'a'));     // 26..51
  if (ch >= '0' && ch <= '9') return (uint8)(52 + (ch - '0'));     // 52..61
  switch (ch) {
    case '!': return 62;
    case '?': return 63;
    case '-': return 64;
    case '.': return 65;
    case ',': return 66;
    case '>': return 68;
    case '\'': return 81;
    case ' ': return 89;  // 0x59
    default:  return 0xFF;
  }
}

// US message-engine control bytes (Text_DecodeCmd: code = 0x67 + cmd_index).
//   kTextCmd_1 = 13 -> 0x74 (jump to visual row 0 / top)
//   kTextCmd_2 = 14 -> 0x75 (jump to visual row 1 / middle)
//   kTextCmd_3 = 15 -> 0x76 (jump to visual row 2 / bottom)
//   kTextCmd_Waitkey = 23 -> 0x7e (pause for input between pages)
//   kTextCmd_EndMessage = 24 -> 0x7f (terminator; matches Text_LoadCharacterBuffer)
// NOTE: 0x74/0x75/0x76 map directly to visual rows 0/1/2 (the message engine's
// kVWF_RowPositions = {0,2,4}; see messaging.c). Row 0 is implicit (no command),
// so the encoder's 2nd row must emit 0x75 (row 1) and its 3rd row 0x76 (row 2).
// The previous values 0x74/0x75 sent the 2nd row to row 0 (overwriting the 1st)
// and the 3rd row to row 1 — the reported "overlapping/dropped words" glitch.
#define kHintFontCmdLine0   0x74u
#define kHintFontCmdLine1   0x75u
#define kHintFontCmdLine2   0x76u
#define kHintFontCmdChoose  0x68u
#define kHintFontCmdScroll  0x73u
#define kHintFontCmdWaitkey 0x7eu
#define kHintFontCmdEnd     0x7fu

// Render `text` (NUL-terminated ASCII) into `out` as US font codes, wrapping on
// word boundaries to fit the THREE-row dialogue box. The box holds rows 0/1/2;
// anything that would overflow row 2 is dropped rather than paged. The normal
// pre-decode hint path deliberately remains one box; the Stumpy choice surface
// uses a separate post-decode composer with one explicit page transition.
// Friendly short hint text (see hint_friendly_item/loc) is sized to fit.
// Returns bytes written (excluding the 0x7f terminator the caller appends).
static int encode_hint_text(const char *text, uint8 *out, bool *out_complete) {
  int w = 0;            // write cursor into out
  int col = 0;          // glyph count on the current row
  int row = 0;          // 0..2 (the 3 box rows)
  const char *p = text;
  while (*p && w < 240) {
    // Measure the next word (run of non-space chars).
    const char *word_end = p;
    while (*word_end && *word_end != ' ') word_end++;
    int word_len = (int)(word_end - p);
    if (word_len == 0) { p++; continue; }  // skip the space separator itself

    // Wrap if the word won't fit on the current row (and the row isn't empty).
    if (col != 0 && col + 1 + word_len > kHintMaxCharsPerLine) {
      if (row >= 2) {
        if (out_complete != NULL) *out_complete = false;
        return w;  // box full — stop (no paging)
      }
      row++;
      col = 0;
      out[w++] = (row == 1) ? kHintFontCmdLine1 : kHintFontCmdLine2;
    } else if (col != 0) {
      out[w++] = ascii_to_font(' ');  // inter-word space
      col++;
    }

    // Emit the word's glyphs (hard-wrap if a single word exceeds the row).
    // Guard at 239 (not 240): one iteration can write TWO bytes — a row command
    // on a hard-wrap AND the glyph — so reserving a byte keeps the write cursor
    // <= 240, leaving room for the caller's terminator at out[w] within the soft
    // cap. (Real hints are short and never approach this; it only bit a
    // pathological >240-byte string, which could push w to 241.)
    for (const char *q = p; q < word_end && w < 239; q++) {
      if (col >= kHintMaxCharsPerLine) {
        if (row >= 2) {
          if (out_complete != NULL) *out_complete = false;
          return w;  // box full mid-word — stop
        }
        row++;
        col = 0;
        out[w++] = (row == 1) ? kHintFontCmdLine1 : kHintFontCmdLine2;
      }
      uint8 fc = ascii_to_font(*q);
      if (fc == 0xFF) fc = ascii_to_font(' ');  // glyphless char -> space
      out[w++] = fc;
      col++;
    }
    p = word_end;
  }
  if (out_complete != NULL) *out_complete = (*p == '\0');
  return w;
}

static bool encode_item_location_hint(
    const RandoDialogueHintResolution *resolution, uint8 *out_buffer,
    bool *out_used_id_fallback) {
  char item[64];
  const char *raw_loc = Rando_GetLocationName(resolution->location_id);
  if (!npc_hint_item_name(resolution->item_id, item, sizeof(item)) ||
      raw_loc == NULL) return false;

  char loc[96], compact_loc[64], text[192];
  uint8 encoded[240];
  bool complete = false;
  hint_friendly_loc(raw_loc, loc, sizeof(loc));

  snprintf(text, sizeof(text), "%s is in %s", item, loc);
  int w = encode_hint_text(text, encoded, &complete);
  if (!complete) {
    snprintf(text, sizeof(text), "%s at %s", item, loc);
    w = encode_hint_text(text, encoded, &complete);
  }
  if (!complete) {
    hint_compact_loc(raw_loc, compact_loc, sizeof(compact_loc));
    snprintf(text, sizeof(text), "%s at %s", item, compact_loc);
    w = encode_hint_text(text, encoded, &complete);
  }

  bool used_id_fallback = false;
  if (!complete) {
    // This is deliberately explicit rather than returning a partially encoded
    // location. The all-location self-check rejects any current registry row
    // that reaches it, so it is only a forward-compatible fail-safe.
    snprintf(text, sizeof(text), "%s at Loc %u", item,
             (unsigned)resolution->location_id);
    w = encode_hint_text(text, encoded, &complete);
    used_id_fallback = true;
  }
  if (!complete) return false;
  memcpy(out_buffer, encoded, (size_t)w);
  out_buffer[w] = kHintFontCmdEnd;
  if (out_used_id_fallback != NULL) *out_used_id_fallback = used_id_fallback;
  return true;
}

static bool encode_location_item_hint(
    const RandoDialogueHintResolution *resolution, uint8 *out_buffer) {
  char item[64], text[128];
  uint8 encoded[240];
  bool complete = false;
  int w = 0;
  if (!location_hint_item_name(resolution->item_id, item, sizeof(item)))
    return false;

  // Preserve the sign's Cape clue when the placed item name allows it. The
  // shorter forms keep the item intact rather than silently truncating it.
  snprintf(text, sizeof(text), "Cape prize is %s", item);
  w = encode_hint_text(text, encoded, &complete);
  if (!complete) {
    snprintf(text, sizeof(text), "Prize is %s", item);
    w = encode_hint_text(text, encoded, &complete);
  }
  if (!complete) {
    snprintf(text, sizeof(text), "%s", item);
    w = encode_hint_text(text, encoded, &complete);
  }
  if (!complete) return false;
  memcpy(out_buffer, encoded, (size_t)w);
  out_buffer[w] = kHintFontCmdEnd;
  return true;
}

static bool encode_dialogue_hint(const RandoDialogueHintResolution *resolution,
                                 uint8 *out_buffer,
                                 bool *out_used_id_fallback) {
  if (resolution == NULL || resolution->redirect == NULL ||
      resolution->status != kDialogueHintRedirect_Active)
    return false;
  if (resolution->redirect->kind == kDialogueHintRedirect_LocationItem) {
    if (out_used_id_fallback != NULL) *out_used_id_fallback = false;
    return encode_location_item_hint(resolution, out_buffer);
  }
  return encode_item_location_hint(resolution, out_buffer,
                                   out_used_id_fallback);
}

static int append_hint_ascii(uint8 *out, int pos, const char *text) {
  while (*text && pos < 239) {
    uint8 fc = ascii_to_font(*text++);
    out[pos++] = fc == 0xFF ? ascii_to_font(' ') : fc;
  }
  return pos;
}

static bool encode_interactive_dialogue_hint(
    const RandoDialogueHintResolution *resolution, uint8 *out_buffer) {
  if (resolution == NULL || resolution->redirect == NULL ||
      resolution->redirect->kind != kDialogueHintRedirect_ItemLocationChoice ||
      resolution->status != kDialogueHintRedirect_Active)
    return false;

  // Encode into a local buffer and copy out only on FULL success. The header
  // contract is "returns false without modifying the buffer": out_buffer is
  // the live messaging text buffer holding the decoded vanilla prompt, and a
  // partial write followed by a false return would clobber it — losing the
  // Choose command, so the Yes/No branch would run on a stale selection.
  // (Today the overflow guard below is unreachable — the 3-row envelope caps
  // hint output well under it — but the contract must not depend on that.)
  uint8 tmp[256];
  bool used_id_fallback = false;
  if (!encode_item_location_hint(resolution, tmp, &used_id_fallback))
    return false;

  int w = 0;
  while (w < 239 && tmp[w] != kHintFontCmdEnd) w++;
  if (w >= 200) return false;  // ample room for the fixed choice page below.
  tmp[w++] = kHintFontCmdWaitkey;
  tmp[w++] = kHintFontCmdScroll;
  // Scroll leaves the VWF cursor on the bottom row. Explicitly jump back to
  // row 0 or the prompt would be drawn on row 2 and then overwritten by No way.
  tmp[w++] = kHintFontCmdLine0;
  w = append_hint_ascii(tmp, w, "Can you help?");
  tmp[w++] = kHintFontCmdLine1;
  w = append_hint_ascii(tmp, w, "> Yes");
  tmp[w++] = kHintFontCmdLine2;
  w = append_hint_ascii(tmp, w, "  No way");
  tmp[w++] = kHintFontCmdChoose;
  tmp[w] = kHintFontCmdEnd;
  memcpy(out_buffer, tmp, (size_t)(w + 1));
  return true;
}

// Fortune Teller reading-message id range (kFortuneTeller_Readings in
// sprite_main.c): 0xEA..0xF1 and 0xF6..0xFD. The 0xF2..0xF5 gap is the payment /
// solicit / decline flow, deliberately left to vanilla. These ids are
// FortuneTeller-exclusive (verified), so no room gate is needed.
static bool is_fortune_reading_msg(uint16 msg_id) {
  return (msg_id >= 0xEAu && msg_id <= 0xF1u) || (msg_id >= 0xF6u && msg_id <= 0xFDu);
}

// Map a dialogue message id to the hint NPC whose text should replace it: the 15
// telepathic tiles first, then the fork-extension NPCs. Returns
// kRandoHintNpc_None for non-hint ids (caller falls through to vanilla decode).
static RandoHintNpc hint_npc_for_msg(uint16 msg_id) {
  for (int i = 0; i < kHintTileCount; i++)
    if (kHintTileMsgIds[i] == msg_id)
      return (RandoHintNpc)(kRandoHintNpc_TeleEasternPalace + i);
  // Storyteller (Sprite_28_DarkWorldHintNPC): the paid-tip messages 0xFF/0x101/
  // 0x102/0x103 are storyteller-exclusive (verified — subtype2 cases 0/1/2/3 each
  // call Sprite_ShowMessageUnconditional with one of these ids after
  // DarkWorldHintNPC_HandlePayment succeeds), so no location gate is needed.
  if (msg_id == 0xFFu || msg_id == 0x101u || msg_id == 0x102u || msg_id == 0x103u)
    return kRandoHintNpc_ForkStoryteller;
  // Fortune Teller reading -> Kakariko (light world) or Dark-World instance by
  // the current world. The Lake-Hylia FT shares the Kakariko room with no
  // runtime discriminator, so it also surfaces the Kakariko hint (id 18).
  // Sprite_FortuneTeller selects its light/dark instance via
  // (savegame_is_darkworld >> 6) & 1, not the raw byte — match it so the hint
  // picks the same world the sprite renders for.
  if (is_fortune_reading_msg(msg_id))
    return ((savegame_is_darkworld >> 6) & 1) ? kRandoHintNpc_ForkFortuneTellerDark
                                              : kRandoHintNpc_ForkFortuneTellerKak;
  return kRandoHintNpc_None;
}

bool Rando_RenderHintMessage(uint16 msg_id, uint8 *out_buffer) {
  if (out_buffer == NULL) return false;
  // This pre-decoded renderer implements only the US/original command grammar
  // and font alphabet. Preserve every non-US locale buffer for vanilla decode.
  if (g_zenv.dialogue_flags != 0) return false;
  // Slot-active gate: g_rando_slot_active is a g_ram macro (features.h).
  if (!g_rando_slot_active) return false;

  RandoHintNpc npc = hint_npc_for_msg(msg_id);
  if (npc != kRandoHintNpc_None) {
    const char *text = Rando_GetHintString(npc);
    if (text == NULL) return false;  // hints disabled / no hint -> vanilla decode.

    int w = encode_hint_text(text, out_buffer, NULL);
    out_buffer[w] = kHintFontCmdEnd;  // 0x7f terminator (matches vanilla path).
    return true;
  }

  RandoDialogueHintResolution redirect = resolve_active_dialogue_hint(msg_id);
  if (redirect.status != kDialogueHintRedirect_Active ||
      redirect.redirect->kind == kDialogueHintRedirect_ItemLocationChoice)
    return false;
  return encode_dialogue_hint(&redirect, out_buffer, NULL);
}

bool Rando_RewriteInteractiveHintMessage(uint16 msg_id, uint8 *out_buffer) {
  if (out_buffer == NULL) return false;
  RandoDialogueHintResolution redirect = resolve_active_dialogue_hint(msg_id);
  return encode_interactive_dialogue_hint(&redirect, out_buffer);
}

bool Rando_IsDynamicHintMessage(uint16 msg_id) {
  return resolve_active_dialogue_hint(msg_id).status ==
         kDialogueHintRedirect_Active;
}

static const char *dialogue_hint_redirect_status_name(
    RandoDialogueHintRedirectStatus status) {
  switch (status) {
  case kDialogueHintRedirect_NotRecognized: return "not recognized";
  case kDialogueHintRedirect_SlotInactive: return "slot inactive";
  case kDialogueHintRedirect_UnsupportedLocale: return "unsupported locale";
  case kDialogueHintRedirect_SettingsUnavailable: return "settings unavailable";
  case kDialogueHintRedirect_HintsOff: return "hints off";
  case kDialogueHintRedirect_DiscriminatorMismatch: return "discriminator mismatch";
  case kDialogueHintRedirect_PlacementUnavailable: return "placement unavailable";
  case kDialogueHintRedirect_ItemAbsent: return "item absent";
  case kDialogueHintRedirect_LocationAbsent: return "location absent";
  case kDialogueHintRedirect_Active: return "active";
  default: return "unknown";
  }
}

static const char *dialogue_hint_redirect_kind_name(
    const RandoDialogueHintRedirect *redirect) {
  if (redirect == NULL) return "none";
  switch (redirect->kind) {
  case kDialogueHintRedirect_ItemLocation: return "item-location";
  case kDialogueHintRedirect_LocationItem: return "location-item sign";
  case kDialogueHintRedirect_ItemLocationChoice: return "interactive item-location";
  default: return "unknown";
  }
}

static void dump_dialogue_hint_resolution(
    FILE *f, const RandoDialogueHintResolution *redirect) {
  if (f == NULL || redirect == NULL || redirect->redirect == NULL) return;
  const char *item = redirect->item_id == 0xFFFFu
                         ? NULL : Rando_GetItemName(redirect->item_id);
  const char *loc = redirect->status == kDialogueHintRedirect_Active
                        ? Rando_GetLocationName(redirect->location_id)
                        : NULL;
  if (loc != NULL) {
    fprintf(f, "redirect source=%s  surface=%s  target_item=%s  status=%s  "
               "resolved_location=%s  location_id=%u\n",
            redirect->redirect->source_name,
            dialogue_hint_redirect_kind_name(redirect->redirect),
            item != NULL ? item : "(unresolved)",
            dialogue_hint_redirect_status_name(redirect->status), loc,
            (unsigned)redirect->location_id);
  } else {
    fprintf(f, "redirect source=%s  surface=%s  target_item=%s  status=%s  "
               "resolved_location=(none)  location_id=(none)\n",
            redirect->redirect->source_name,
            dialogue_hint_redirect_kind_name(redirect->redirect),
            item != NULL ? item : "(unresolved)",
            dialogue_hint_redirect_status_name(redirect->status));
  }
}

// Dev diagnostic (F12 / ZeldaDumpDebugState): write the live hint-table state to
// dump_hints.txt so a telepathic-tile "no hint" report can be diagnosed without
// a debugger. `cur_msg_id` is the current dialogue_message_index — pass the id
// of the tile just read to see whether it is a hint tile and which NPC/text it
// maps to.
void Rando_DumpHintDebug(uint16 cur_msg_id) {
  FILE *f = fopen("dump_hints.txt", "w");
  if (f == NULL) return;
  int active = 0;
  for (int i = 1; i < kRandoHintNpc__Count; i++)
    if (g_hint_table[i].active) active++;
  RandoHintNpc generated_npc = hint_npc_for_msg(cur_msg_id);
  bool generated_active = g_rando_slot_active && g_zenv.dialogue_flags == 0 &&
                          generated_npc != kRandoHintNpc_None &&
                          Rando_GetHintString(generated_npc) != NULL;
  RandoDialogueHintResolution redirect = resolve_active_dialogue_hint(cur_msg_id);
  const char *kind = generated_npc != kRandoHintNpc_None ? "generated hint" :
                     redirect.redirect != NULL ? "vanilla-dialogue redirect" : "none";
  fprintf(f, "slot_active=%d  active_hints=%d/%d  cur_dialogue_msg=0x%02X  "
             "is_hint_tile=%d  hint_kind=%s  generated_active=%d\n",
          (int)g_rando_slot_active, active, (int)kRandoHintNpc__Count - 1,
          (unsigned)cur_msg_id, (int)Rando_IsHintTileMessage(cur_msg_id), kind,
          (int)generated_active);
  dump_dialogue_hint_resolution(f, &redirect);
  for (int i = 1; i < kRandoHintNpc__Count; i++) {
    fprintf(f, "  npc %2d active=%d text=%s\n", i, (int)g_hint_table[i].active,
            g_hint_table[i].text[0] ? g_hint_table[i].text : "(empty)");
  }
  fclose(f);
}

// -----------------------------------------------------------------------------
// Hints_SelfCheck — determinism assertion.
//
// Asserts that two consecutive invocations of Rando_GenerateHints on the
// same (settings, placement) yield byte-identical g_hint_table contents.
// Without this CI assertion the determinism contract documented at the
// top of this file ("hint text is byte-identical for a given (settings,
// seed_u64)") was only verified by manual eyeballing.
//
// Also exercises the kHintsMode_Off short-circuit and the NULL-args
// defensive rejects so the corpus runner doesn't have to.
// -----------------------------------------------------------------------------
static void hints_selfcheck_die(const char *what) {
  fprintf(stderr, "Hints_SelfCheck: %s\n", what);
  abort();
}

static char hint_font_to_ascii(uint8 c) {
  if (c < 26) return (char)('A' + c);
  if (c < 52) return (char)('a' + c - 26);
  if (c < 62) return (char)('0' + c - 52);
  if (c == 64) return '-';
  if (c == 81) return '\'';
  return ' ';
}

static bool encoded_hint_contains(const uint8 *encoded, const char *needle) {
  char ascii[256];
  int w = 0;
  for (int i = 0; i < 240 && encoded[i] != kHintFontCmdEnd && w < (int)sizeof(ascii) - 1; i++) {
    uint8 c = encoded[i];
    if (c == kHintFontCmdLine1 || c == kHintFontCmdLine2) {
      if (w != 0 && ascii[w - 1] != ' ') ascii[w++] = ' ';
    } else {
      ascii[w++] = hint_font_to_ascii(c);
    }
  }
  ascii[w] = '\0';
  return strstr(ascii, needle) != NULL;
}

void Hints_SelfCheck(void) {
  // Synthetic placement table: 4 entries — 2 progression items (Hookshot,
  // Boots) + 2 junk (Rupee5, Bombs1). Item IDs sourced from item_ids.h so
  // future registry re-ids don't break the test. Prior version used raw
  // integers 6 and 8 with Hookshot / Boots comments, but those IDs are
  // L2Sword / L4Sword in the actual registry — the test still exercised
  // determinism but on the wrong items, which is exactly the drift this
  // file's item_is_junk symbol-reference fix was added to prevent.
  static RandoPlacement synth_entries[4];
  synth_entries[0].location_id = 1;  synth_entries[0].item_id = ITEM_Hookshot;
  synth_entries[1].location_id = 2;  synth_entries[1].item_id = ITEM_Boots;
  synth_entries[2].location_id = 3;  synth_entries[2].item_id = ITEM_Rupee5;
  synth_entries[3].location_id = 4;  synth_entries[3].item_id = ITEM_Bombs1;
  RandoPlacementTable table;
  table.entries = synth_entries;
  table.count = 4;

  RandoSettings settings;
  Settings_SetDefaults(&settings);
  settings.hints = kHintsMode_On;
  settings.goal = kGoal_FastGanon;  // Murahdahla path inactive on Fast Ganon.

  // First run: capture the hint table.
  bool ok = Rando_GenerateHints(&settings, &table, NULL);
  if (!ok) {
    fprintf(stderr, "Hints_SelfCheck: first GenerateHints failed.\n");
    abort();
  }
  HintEntry snapshot[kRandoHintNpc__Count];
  memcpy(snapshot, g_hint_table, sizeof(snapshot));

  // Second run: must produce byte-identical output.
  Rando_ClearHints();
  ok = Rando_GenerateHints(&settings, &table, NULL);
  if (!ok) {
    fprintf(stderr, "Hints_SelfCheck: second GenerateHints failed.\n");
    abort();
  }
  if (memcmp(snapshot, g_hint_table, sizeof(snapshot)) != 0) {
    fprintf(stderr, "Hints_SelfCheck: non-deterministic hint output.\n");
    abort();
  }

  // Murahdahla determinism: the Fast-Ganon goal above never enters the
  // Triforce/Ganon-hunt branch — the only path with nested region-dedup
  // iteration, and thus the one most prone to nondeterministic drift — so its
  // determinism was previously unasserted. Round-trip a second time under
  // Triforce Hunt with a TriforcePiece in the pool so the branch actually runs.
  {
    static RandoPlacement th_entries[4];
    th_entries[0].location_id = 1;  th_entries[0].item_id = ITEM_TriforcePiece;
    th_entries[1].location_id = 2;  th_entries[1].item_id = ITEM_Hookshot;
    th_entries[2].location_id = 3;  th_entries[2].item_id = ITEM_Boots;
    th_entries[3].location_id = 4;  th_entries[3].item_id = ITEM_Rupee5;
    RandoPlacementTable th_table;
    th_table.entries = th_entries;
    th_table.count = 4;
    RandoSettings th = settings;       // still hints=On from above
    th.hints = kHintsMode_On;
    th.goal = kGoal_TriforceHunt;

    Rando_ClearHints();
    if (!Rando_GenerateHints(&th, &th_table, NULL)) {
      fprintf(stderr, "Hints_SelfCheck: Triforce-Hunt GenerateHints failed.\n");
      abort();
    }
    HintEntry th_snapshot[kRandoHintNpc__Count];
    memcpy(th_snapshot, g_hint_table, sizeof(th_snapshot));
    Rando_ClearHints();
    if (!Rando_GenerateHints(&th, &th_table, NULL)) {
      fprintf(stderr, "Hints_SelfCheck: second Triforce-Hunt GenerateHints failed.\n");
      abort();
    }
    if (memcmp(th_snapshot, g_hint_table, sizeof(th_snapshot)) != 0) {
      fprintf(stderr, "Hints_SelfCheck: non-deterministic Murahdahla hint output.\n");
      abort();
    }
  }

  // Medallion config slots are generation-time requirements, not item
  // locations. They should not be emitted as item hints even if their
  // pre-pinned item id is otherwise hintable.
  {
    static RandoPlacement med_entries[2];
    med_entries[0].location_id = LOC_Misery_Mire_Medallion;
    med_entries[0].item_id = ITEM_Hookshot;
    med_entries[1].location_id = LOC_Turtle_Rock_Medallion;
    med_entries[1].item_id = ITEM_Boots;
    RandoPlacementTable med_table;
    med_table.entries = med_entries;
    med_table.count = 2;
    Rando_ClearHints();
    settings.hints = kHintsMode_On;
    if (!Rando_GenerateHints(&settings, &med_table, NULL)) {
      fprintf(stderr, "Hints_SelfCheck: medallion-config GenerateHints failed.\n");
      abort();
    }
    for (int i = 0; i < kRandoHintNpc__Count; i++) {
      if (g_hint_table[i].active) {
        fprintf(stderr, "Hints_SelfCheck: medallion config slot was hinted.\n");
        abort();
      }
    }
  }

  // Vanilla-dialogue redirects use a pure context resolver so every fallback
  // gate, direction, discriminator, and placement policy can be covered
  // without fabricating a live sidecar slot. Library's Bottle is deliberately
  // adjacent to a Book at Sick Kid: the lookup must reverse-search for Book,
  // never inspect Library.
  {
    static RandoPlacement redirect_entries[] = {
      {LOC_Library, ITEM_BottleEmpty},
      {LOC_Sick_Kid, ITEM_BookOfMudora},
      {LOC_Ice_Palace_Prize, ITEM_MoonPearl},
      {LOC_Magic_Bat, ITEM_Prize_GreenPendant},
      {LOC_Bumper_Cave, ITEM_Hookshot},
      {LOC_Desert_Ledge, ITEM_OcarinaInactive},
      {LOC_Maze_Race, ITEM_PieceOfHeart},
    };
    RandoPlacementTable redirect_table = {
      redirect_entries,
      (uint16)(sizeof(redirect_entries) / sizeof(redirect_entries[0])),
    };
    RandoSettings on;
    Settings_SetDefaults(&on);
    on.hints = kHintsMode_On;

    RandoDialogueHintResolution book = resolve_dialogue_hint_for_context(
        0x125, true, 0, &on, &redirect_table, false, 0);
    if (book.status != kDialogueHintRedirect_Active ||
        book.item_id != ITEM_BookOfMudora || book.location_id != LOC_Sick_Kid)
      hints_selfcheck_die("Aginah must resolve Book at Sick Kid, not Library contents");
    uint8 encoded[241];
    bool used_fallback = false;
    if (!encode_dialogue_hint(&book, encoded, &used_fallback) || used_fallback ||
        !encoded_hint_contains(encoded, "Book") ||
        !encoded_hint_contains(encoded, "Sick Kid"))
      hints_selfcheck_die("Aginah Book redirect text");

    const uint16 pearl_msgs[] = {0x15D, 0x09E, 0x036};
    for (size_t i = 0; i < sizeof(pearl_msgs) / sizeof(pearl_msgs[0]); i++) {
      RandoDialogueHintResolution pearl = resolve_dialogue_hint_for_context(
          pearl_msgs[i], true, 0, &on, &redirect_table, false, 0);
      if (pearl.status != kDialogueHintRedirect_Active ||
          pearl.location_id != LOC_Ice_Palace_Prize)
        hints_selfcheck_die("Moon Pearl redirect mapping");
      if (!encode_dialogue_hint(&pearl, encoded, &used_fallback) || used_fallback ||
          !encoded_hint_contains(encoded, "Moon Pearl") ||
          !encoded_hint_contains(encoded, "Ice Palace"))
        hints_selfcheck_die("Moon Pearl redirect text");
    }

    RandoDialogueHintResolution pendant = resolve_dialogue_hint_for_context(
        0x033, true, 0, &on, &redirect_table, false, 0);
    if (pendant.status != kDialogueHintRedirect_Active ||
        pendant.location_id != LOC_Magic_Bat)
      hints_selfcheck_die("Sahasrahla Green Pendant redirect mapping");

    // Bumper Cave is the inverse direction: the physical sign identifies the
    // location, so resolve that row's item. The unrelated Heart Piece at Maze
    // Race must not affect the answer.
    RandoDialogueHintResolution bumper = resolve_dialogue_hint_for_context(
        0x0A8, true, 0, &on, &redirect_table, false, 0x4A);
    if (bumper.status != kDialogueHintRedirect_Active ||
        bumper.location_id != LOC_Bumper_Cave || bumper.item_id != ITEM_Hookshot)
      hints_selfcheck_die("Bumper sign must resolve the item at Bumper Cave");
    if (!encode_dialogue_hint(&bumper, encoded, &used_fallback) || used_fallback ||
        !encoded_hint_contains(encoded, "Hookshot"))
      hints_selfcheck_die("Bumper sign redirect text");
    if (resolve_dialogue_hint_for_context(
            0x0A8, true, 0, &on, &redirect_table, false, 0x49).status !=
        kDialogueHintRedirect_DiscriminatorMismatch)
      hints_selfcheck_die("Bumper sign wrong-screen discriminator");
    if (resolve_dialogue_hint_for_context(
            0x0A8, true, 0, &on, &redirect_table, true, 0x4A).status !=
        kDialogueHintRedirect_DiscriminatorMismatch)
      hints_selfcheck_die("Bumper sign indoor discriminator");

    // Stumpy retains a live choice page after naming the Flute's actual
    // placement. The separate LOC_Stumpy reward remains owned by 0xE6.
    RandoDialogueHintResolution stumpy = resolve_dialogue_hint_for_context(
        0x0E5, true, 0, &on, &redirect_table, false, 0);
    if (stumpy.status != kDialogueHintRedirect_Active ||
        stumpy.item_id != ITEM_OcarinaInactive ||
        stumpy.location_id != LOC_Desert_Ledge)
      hints_selfcheck_die("Stumpy Flute redirect mapping");
    if (!encode_interactive_dialogue_hint(&stumpy, encoded) ||
        !encoded_hint_contains(encoded, "Flute") ||
        !encoded_hint_contains(encoded, "Desert Ledge"))
      hints_selfcheck_die("Stumpy interactive hint text");
    bool has_choose = false, has_page_reset = false;
    for (int i = 0; i < 240 && encoded[i] != kHintFontCmdEnd; i++) {
      if (encoded[i] == kHintFontCmdChoose) has_choose = true;
      if (i < 237 && encoded[i] == kHintFontCmdWaitkey &&
          encoded[i + 1] == kHintFontCmdScroll &&
          encoded[i + 2] == kHintFontCmdLine0)
        has_page_reset = true;
    }
    if (!has_choose || !has_page_reset)
      hints_selfcheck_die("Stumpy interactive hint lost choice/page commands");

    RandoSettings off = on;
    off.hints = kHintsMode_Off;
    if (resolve_dialogue_hint_for_context(
            0x125, true, 0, &off, &redirect_table, false, 0).status !=
        kDialogueHintRedirect_HintsOff)
      hints_selfcheck_die("redirect hints-off fallback");
    if (resolve_dialogue_hint_for_context(
            0x125, false, 0, &on, &redirect_table, false, 0).status !=
        kDialogueHintRedirect_SlotInactive)
      hints_selfcheck_die("redirect inactive-slot fallback");
    if (resolve_dialogue_hint_for_context(
            0x125, true, 0, NULL, &redirect_table, false, 0).status !=
        kDialogueHintRedirect_SettingsUnavailable)
      hints_selfcheck_die("redirect missing-settings fallback");
    if (resolve_dialogue_hint_for_context(
            0x125, true, 1, &on, &redirect_table, false, 0).status !=
        kDialogueHintRedirect_UnsupportedLocale)
      hints_selfcheck_die("redirect non-US fallback");
    if (resolve_dialogue_hint_for_context(
            0x125, true, 0, &on, NULL, false, 0).status !=
        kDialogueHintRedirect_PlacementUnavailable)
      hints_selfcheck_die("redirect missing-placement fallback");

    RandoPlacement no_target_entry = {LOC_Library, ITEM_BottleEmpty};
    RandoPlacementTable no_target = {&no_target_entry, 1};
    if (resolve_dialogue_hint_for_context(
            0x125, true, 0, &on, &no_target, false, 0).status !=
        kDialogueHintRedirect_ItemAbsent)
      hints_selfcheck_die("redirect missing-item fallback");
    if (resolve_dialogue_hint_for_context(
            0x0A8, true, 0, &on, &no_target, false, 0x4A).status !=
        kDialogueHintRedirect_LocationAbsent)
      hints_selfcheck_die("redirect missing-location fallback");

    const uint16 adjacent[] = {
      0x032, 0x034, 0x035, 0x037, 0x09D, 0x09F, 0x0A7, 0x0A9,
      0x0E4, 0x0E6, 0x124, 0x126, 0x15C, 0x15E,
    };
    for (size_t i = 0; i < sizeof(adjacent) / sizeof(adjacent[0]); i++) {
      if (resolve_dialogue_hint_for_context(
              adjacent[i], true, 0, &on, &redirect_table, false, 0).status !=
          kDialogueHintRedirect_NotRecognized)
        hints_selfcheck_die("adjacent dialogue ID was intercepted");
    }

    RandoDialogueHintRedirect flagged = {
      0x777, ITEM_MoonPearl, 0xFFFFu, "test", 0x80,
      kDialogueHintRedirect_ItemLocation,
    };
    if (dialogue_hint_redirect_discriminator_matches(&flagged, false, 0))
      hints_selfcheck_die("unknown redirect discriminator flags must fail closed");

    // Duplicate target policy is lowest location ID even when the rows arrive in
    // descending order.
    RandoPlacement duplicate_entries[] = {
      {LOC_Library, ITEM_MoonPearl},
      {LOC_Sick_Kid, ITEM_MoonPearl},
    };
    RandoPlacementTable duplicates = {duplicate_entries, 2};
    if (resolve_dialogue_hint_for_context(
            0x15D, true, 0, &on, &duplicates, false, 0).location_id !=
        LOC_Sick_Kid)
      hints_selfcheck_die("duplicate item policy must choose lowest location ID");

    // F12 uses this exact formatter. Pin the identifying source, target, status,
    // and resolved location rather than merely testing the resolver fields.
    FILE *diag = tmpfile();
    if (diag == NULL) hints_selfcheck_die("tmpfile for redirect diagnostic");
    dump_dialogue_hint_resolution(diag, &book);
    rewind(diag);
    char diag_text[512];
    size_t diag_len = fread(diag_text, 1, sizeof(diag_text) - 1, diag);
    fclose(diag);
    diag_text[diag_len] = '\0';
    if (strstr(diag_text, "source=Aginah") == NULL ||
        strstr(diag_text, "target_item=BookOfMudora") == NULL ||
        strstr(diag_text, "status=active") == NULL ||
        strstr(diag_text, "resolved_location=Sick Kid") == NULL)
      hints_selfcheck_die("redirect diagnostic content");

    diag = tmpfile();
    if (diag == NULL) hints_selfcheck_die("tmpfile for Bumper diagnostic");
    dump_dialogue_hint_resolution(diag, &bumper);
    rewind(diag);
    diag_len = fread(diag_text, 1, sizeof(diag_text) - 1, diag);
    fclose(diag);
    diag_text[diag_len] = '\0';
    if (strstr(diag_text, "source=Bumper Cave sign") == NULL ||
        strstr(diag_text, "surface=location-item sign") == NULL ||
        strstr(diag_text, "target_item=Hookshot") == NULL ||
        strstr(diag_text, "resolved_location=Bumper Cave") == NULL)
      hints_selfcheck_die("Bumper redirect diagnostic content");

    // The 15 tile IDs and existing fork mappings are independent of the new
    // redirect table. This catches an accidental overlap or range broadening.
    static const uint16 expected_tiles[kHintTileCount] = {
      0xB4, 0xB5, 0xB8, 0xB9, 0xBA, 0xBB, 0xBE, 0xBF,
      0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6,
    };
    if (memcmp(expected_tiles, kHintTileMsgIds, sizeof(expected_tiles)) != 0)
      hints_selfcheck_die("telepathic tile ID mapping drift");
    for (int i = 0; i < kHintTileCount; i++) {
      if (hint_npc_for_msg(expected_tiles[i]) !=
          (RandoHintNpc)(kRandoHintNpc_TeleEasternPalace + i))
        hints_selfcheck_die("telepathic tile NPC mapping drift");
    }
    if (hint_npc_for_msg(0xFF) != kRandoHintNpc_ForkStoryteller ||
        hint_npc_for_msg(0xEF) == kRandoHintNpc_None)
      hints_selfcheck_die("fork NPC mapping drift");

    // Deliberate locale improvement: generated hints now also preserve non-US
    // buffers instead of injecting US glyph codes.
    uint8 saved_slot_active = g_rando_slot_active;
    uint8 saved_dialogue_flags = g_zenv.dialogue_flags;
    g_rando_slot_active = 1;
    if (!Rando_GenerateHints(&on, &table, NULL))
      hints_selfcheck_die("generate hints for locale fallback");
    for (uint8 locale_flags = 1; locale_flags <= 2; locale_flags++) {
      g_zenv.dialogue_flags = locale_flags;
      memset(encoded, 0x55, sizeof(encoded));
      if (Rando_RenderHintMessage(0xB4, encoded) || encoded[0] != 0x55)
        hints_selfcheck_die("non-US generated hint must preserve vanilla buffer");
    }
    g_rando_slot_active = saved_slot_active;
    g_zenv.dialogue_flags = saved_dialogue_flags;

    // Every current registry location must retain a readable name without the
    // numeric forward-compatibility fallback for each item->location surface.
    for (size_t r = 0; r < sizeof(kDialogueHintRedirects) /
                               sizeof(kDialogueHintRedirects[0]); r++) {
      if (kDialogueHintRedirects[r].kind == kDialogueHintRedirect_LocationItem)
        continue;
      RandoDialogueHintResolution format = {
        &kDialogueHintRedirects[r], kDialogueHintRedirect_Active,
        kDialogueHintRedirects[r].item_id, 0xFFFFu,
      };
      for (uint32 i = 0; i < kRandoLocationsCount; i++) {
        format.location_id = kRandoLocations[i].id;
        used_fallback = false;
        if (!encode_dialogue_hint(&format, encoded, &used_fallback) ||
            used_fallback) {
          fprintf(stderr,
                  "Hints_SelfCheck: dynamic hint format overflow item=%s loc=%u (%s)\n",
                  Rando_GetItemName(format.item_id),
                  (unsigned)format.location_id,
                  Rando_GetLocationName(format.location_id));
          abort();
        }
      }
    }

    // Bumper Cave's location->item direction can surface any registry item.
    // Assert that the useful item name always survives the three-row envelope.
    RandoDialogueHintResolution bumper_format = {
      dialogue_hint_redirect_for_msg(0x0A8), kDialogueHintRedirect_Active,
      0xFFFFu, LOC_Bumper_Cave,
    };
    for (uint16 item = 0; item < ITEM__COUNT; item++) {
      bumper_format.item_id = item;
      if (!encode_dialogue_hint(&bumper_format, encoded, NULL)) {
        fprintf(stderr,
                "Hints_SelfCheck: Bumper item format overflow item=%u (%s)\n",
                (unsigned)item, Rando_GetItemName(item));
        abort();
      }
    }

    // Wild dungeon items must remain semantically distinguishable on a
    // location->item sign; the generated-hint helper intentionally collapses
    // these to a generic kind and is therefore insufficient here.
    char eastern_key[64], pod_key[64];
    if (!location_hint_item_name(ITEM_SmallKey_EasternPalace, eastern_key,
                                 sizeof(eastern_key)) ||
        !location_hint_item_name(ITEM_SmallKey_PalaceOfDarkness, pod_key,
                                 sizeof(pod_key)) ||
        strcmp(eastern_key, "Eastern Small Key") != 0 ||
        strcmp(pod_key, "PoD Small Key") != 0 ||
        strcmp(eastern_key, pod_key) == 0)
      hints_selfcheck_die("Bumper dungeon-item names must retain dungeon identity");

    bumper_format.item_id = ITEM_SmallKey_PalaceOfDarkness;
    if (!encode_dialogue_hint(&bumper_format, encoded, NULL) ||
        !encoded_hint_contains(encoded, "PoD Small Key"))
      hints_selfcheck_die("Bumper qualified dungeon-item text");
  }

  // kHintsMode_Off: must populate nothing.
  Rando_ClearHints();
  settings.hints = kHintsMode_Off;
  ok = Rando_GenerateHints(&settings, &table, NULL);
  if (!ok) {
    fprintf(stderr, "Hints_SelfCheck: hints=Off branch failed.\n");
    abort();
  }
  for (int i = 0; i < kRandoHintNpc__Count; i++) {
    if (g_hint_table[i].active) {
      fprintf(stderr, "Hints_SelfCheck: hints=Off populated entry %d.\n", i);
      abort();
    }
  }

  // NULL args: defensive reject.
  if (Rando_GenerateHints(NULL, &table, NULL)) {
    fprintf(stderr, "Hints_SelfCheck: NULL settings should fail.\n");
    abort();
  }
  if (Rando_GenerateHints(&settings, NULL, NULL)) {
    fprintf(stderr, "Hints_SelfCheck: NULL placements should fail.\n");
    abort();
  }

  Rando_ClearHints();
  fprintf(stderr, "[Hints_SelfCheck] OK\n");
}

// Cross-TU capacity ABI probe -- see rando_logic.h / Rando_SelfCheckCapacityABI.
RANDO_DEFINE_CAPACITY_PROBE(rando_hints)
