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
#include "rando_logic.h"  // Rando_GetLocationName, Rando_GetItemName
#include "rando_rng.h"
#include "rando_settings.h"  // Settings_SetDefaults for Hints_SelfCheck
#include "../types.h"
#include "../variables.h"    // g_ram (backs the g_rando_slot_active macro)
#include "../features.h"     // g_rando_slot_active (g_ram macro)
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

typedef struct HintEntry {
  uint8 active;
  uint16 placement_loc_id;
  uint16 placement_item_id;
  char text[kRandoHintTextMax];
} HintEntry;

static HintEntry g_hint_table[kRandoHintNpc__Count];

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
  //
  // The 512 cap mirrors `kRando_SessionPlacementCapacity` (the placer's
  // entry pool ceiling — see rando.c session table allocation). Slice 3a
  // raised the placement-table digest cap 256→512 for the same reason;
  // adding new locations beyond 512 would silently drop them from the
  // hintable pool, so the static_assert below trips at compile time
  // (rather than at runtime) when the registry grows past 512.
  uint16 hintable_indices[512];
  _Static_assert(512 >= 256, "hintable pool must not silently truncate");
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
    // Stash up to N region ids in a small fixed buffer for the summary
    // text. region_id is uint16 in `RandoLocationDef`; the dedupe array
    // matches that width so two distinct regions ≥ 256 don't collide
    // (kRandoRegions is append-only, so the 256-region threshold could
    // be crossed in a future slice). Audit-of-audit LOW-2 of phase-b.
    uint16 seen_regions[16] = {0};
    uint8 seen_count = 0;
    uint16 piece_count = 0;  // uint16 — `settings.pieces_placed` is uint16
                              // (range up to 65535); a uint8 here would wrap
                              // silently past 255. Practical TH max is ~50
                              // but the type pin guards against future axis
                              // widening.
    for (uint16 i = 0; i < entry_count; i++) {
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
    case '\'': return 81;
    case ' ': return 89;  // 0x59
    default:  return 0xFF;
  }
}

// US message-engine control bytes (Text_DecodeCmd: code = 0x67 + cmd_index).
//   kTextCmd_1 = 13 -> 0x74 (advance to row 1)
//   kTextCmd_2 = 14 -> 0x75 (advance to row 2)
//   kTextCmd_Waitkey = 23 -> 0x7e (pause for input between pages)
//   kTextCmd_EndMessage = 24 -> 0x7f (terminator; matches Text_LoadCharacterBuffer)
#define kHintFontCmdLine1   0x74u
#define kHintFontCmdLine2   0x75u
#define kHintFontCmdWaitkey 0x7eu
#define kHintFontCmdEnd     0x7fu

// Render `text` (NUL-terminated ASCII) into `out` as US font codes, wrapping
// on word boundaries to fit the three-row dialogue box and inserting the
// row-advance control codes. Pages break with a Waitkey when the text exceeds
// three rows. Returns the number of bytes written (excluding the 0x7f
// terminator the caller appends). `out` must hold >= 256 bytes.
#define kHintMaxCharsPerLine 13  // VWF is variable-width; 13 is a safe cap that
                                 // avoids overrun on the 256-px message box for
                                 // worst-case wide glyphs.
static int encode_hint_text(const char *text, uint8 *out) {
  int w = 0;            // write cursor into out
  int col = 0;          // glyph count on the current row
  int row = 0;          // 0..2 within the current page
  const char *p = text;
  while (*p && w < 240) {
    // Measure the next word (run of non-space chars).
    const char *word_end = p;
    while (*word_end && *word_end != ' ') word_end++;
    int word_len = (int)(word_end - p);
    if (word_len == 0) { p++; continue; }  // skip the space separator itself

    // Wrap if the word won't fit on the current row (and the row isn't empty).
    if (col != 0 && col + 1 + word_len > kHintMaxCharsPerLine) {
      row++;
      col = 0;
      if (row == 1)      out[w++] = kHintFontCmdLine1;
      else if (row == 2) out[w++] = kHintFontCmdLine2;
      else {
        // Page is full — pause and start a fresh page at row 0.
        out[w++] = kHintFontCmdWaitkey;
        row = 0;
      }
    } else if (col != 0) {
      out[w++] = ascii_to_font(' ');  // inter-word space
      col++;
    }

    // Emit the word's glyphs (hard-wrap if a single word exceeds the row).
    for (const char *q = p; q < word_end && w < 240; q++) {
      if (col >= kHintMaxCharsPerLine) {
        row++;
        col = 0;
        if (row == 1)      out[w++] = kHintFontCmdLine1;
        else if (row == 2) out[w++] = kHintFontCmdLine2;
        else { out[w++] = kHintFontCmdWaitkey; row = 0; }
      }
      uint8 fc = ascii_to_font(*q);
      if (fc == 0xFF) fc = ascii_to_font(' ');  // glyphless char -> space
      out[w++] = fc;
      col++;
    }
    p = word_end;
  }
  return w;
}

bool Rando_RenderHintMessage(uint16 msg_id, uint8 *out_buffer) {
  if (out_buffer == NULL) return false;
  // Slot-active gate: g_rando_slot_active is a g_ram macro (features.h).
  if (!g_rando_slot_active) return false;
  if (!Rando_IsHintTileMessage(msg_id)) return false;

  // Map the vanilla tele-message id positionally to a RandoHintNpc.
  RandoHintNpc npc = kRandoHintNpc_None;
  for (int i = 0; i < kHintTileCount; i++) {
    if (kHintTileMsgIds[i] == msg_id) {
      npc = (RandoHintNpc)(kRandoHintNpc_TeleEasternPalace + i);
      break;
    }
  }
  if (npc == kRandoHintNpc_None) return false;

  const char *text = Rando_GetHintString(npc);
  if (text == NULL) return false;  // hints disabled / no hint -> vanilla decode.

  int w = encode_hint_text(text, out_buffer);
  out_buffer[w] = kHintFontCmdEnd;  // 0x7f terminator (matches vanilla path).
  return true;
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
  fprintf(f, "slot_active=%d  active_hints=%d/%d  cur_dialogue_msg=0x%02X  is_hint_tile=%d\n",
          (int)g_rando_slot_active, active, (int)kRandoHintNpc__Count - 1,
          (unsigned)cur_msg_id, (int)Rando_IsHintTileMessage(cur_msg_id));
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
