// rando_generate.c — see rando_generate.h.
//
// Rando_GenerateSlot is a PURE RELOCATION of the body of
// SelectFile_Settings_HandleGenerate (src/select_file.c), lifted from the
// Settings_ComputeHash step through the SelectFile_ResetSidecarCache +
// selectfile_arr1[slot]=1 step. Parameterized:
//   g_settings_working        -> *settings
//   g_settings_target_slot    -> slot_index
//   g_rec_working_features0    -> recommended_features0
//   budget literal             -> (budget < 0 ? (settings->race_mode ? 0 : 10) : budget)
//   SelectFile_ResetSidecarCache()/selectfile_arr1[slot]=1 -> SelectFile_NotifySlotWritten(slot)
// On any failure path it returns false with a message in `err` and frees the
// working `entries`. It does NOT call Placement_Install (install is slot-load-
// only). If out!=NULL, before freeing it malloc+memcpy's an owned placement
// copy into out->placement and fills the scalar result fields.
#include "rando_generate.h"

#include "../zelda_rtl.h"     // g_zenv, ZeldaWriteSram
#include "../config.h"        // g_config (.features0)
#include "../load_gfx.h"      // kSrmOffs_Name, kSrmOffs_DiedCounter
#include "../select_file.h"   // Intro_FixCksum
#include "rando.h"            // kGeneratorVersion
#include "rando_save.h"       // RandoSidecarSlot, kSlotKind_Randomizer, Rando_WriteSidecarSlot, ...
#include "rando_spoiler.h"    // RandoSpoiler, Spoiler_ResolvePath, Spoiler_Write

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

bool Rando_GenerateSlot(const RandoSettings *settings, uint64 seed_u64, int budget,
                        int slot_index, uint32 recommended_features0,
                        RandoGenerateResult *out, char *err, size_t err_cap) {
  if (err != NULL && err_cap > 0) err[0] = '\0';

  // Refuse an out-of-range slot BEFORE any SRAM/sidecar write. The SRAM init
  // below indexes g_zenv.sram + slot_index*0x500, so a negative slot_index
  // (e.g. the window closed mid-request, clearing the kind-toggle target to
  // -1) would memset/write BEFORE the buffer. Audit BLOCKER fix.
  if (slot_index < 0 || slot_index >= kRandoSidecar_SlotCount) {
    if (err != NULL) snprintf(err, err_cap, "invalid slot index %d", slot_index);
    return false;
  }

  // Compute settings_hash (already cached as short).
  uint8 settings_hash_full[32];
  Settings_ComputeHash(settings, settings_hash_full);

  // Run placement.
  extern const uint32 kRandoLocationsCount;
  RandoPlacement *entries = (RandoPlacement *)calloc(kRandoLocationsCount,
                                                     sizeof(RandoPlacement));
  if (entries == NULL) {
    if (err != NULL) snprintf(err, err_cap, "OOM allocating placement table");
    return false;
  }
  RandoPlacementTable table = { entries, 0 };
  // Use a generous budget so even Triforce-Hunt configurations succeed.
  // Phase B Slice 6 audit H1 — race-mode generation must pass
  // budget_seconds=0 (no wall-clock cutoff) so the placer runs to its
  // deterministic kAssumedFillMaxAttempts cap. Reveal also passes 0; this
  // matches both sides so the stamp is reproducible across machines.
  int effective_budget = (budget < 0) ? ((settings->race_mode != 0) ? 0 : 10) : budget;
  bool placed = Place_AssumedFill(settings, seed_u64, effective_budget, &table);
  if (!placed) {
    if (err != NULL) snprintf(err, err_cap, "placement failed");
    free(entries);
    return false;
  }

  // Build share string.
  ShareString ss;
  memset(&ss, 0, sizeof(ss));
  ss.version = (uint8)kGeneratorVersion;
  memcpy(ss.settings_hash, settings_hash_full, 16);
  ss.seed_u64 = seed_u64;
  char share_string[kShareStringBase32MaxLen];
  int share_len = Share_Encode(&ss, share_string, sizeof(share_string));
  if (share_len <= 0) {
    if (err != NULL) snprintf(err, err_cap, "share string encode failed");
    free(entries);
    return false;
  }

  // Pack the 31-byte raw binary blob via the public Share_PackBinary
  // helper so the trailing CRC is correct. Previously the code rebuilt
  // the blob inline and left CRC = 0, which meant the slot's stored
  // share_string base32-re-encoded to a string
  // DIFFERENT from the one Share_Encode emitted to the user — friends
  // who tried Share_Decode on the banner-displayed string would get
  // BadChecksum. The slot header reserves 32 bytes for share_string; we
  // zero-pad bytes 31..32 here for cleanliness.
  uint8 raw_binary[32];
  memset(raw_binary, 0, sizeof(raw_binary));
  Share_PackBinary(&ss, raw_binary);

  // Compute spoiler path + write spoiler files.
  char spoiler_json_path[512];
  char spoiler_txt_path[512];
  int n = Spoiler_ResolvePath(share_string, ".json", spoiler_json_path,
                              sizeof(spoiler_json_path));
  int m = Spoiler_ResolvePath(share_string, ".txt", spoiler_txt_path,
                              sizeof(spoiler_txt_path));
  bool goal_completable = false;
  if (n > 0 && m > 0) {
    RandoSpheres spheres;
    bool spheres_ok = Logic_ComputeSpheres(settings, &table, &spheres);
    (void)spheres_ok;
    RandoSpoiler spoiler;
    memset(&spoiler, 0, sizeof(spoiler));
    spoiler.share_string = share_string;
    spoiler.seed_u64 = seed_u64;
    spoiler.generator_version = kGeneratorVersion;
    spoiler.settings = settings;
    spoiler.placements = &table;
    spoiler.spheres = &spheres;
    spoiler.goal_completable = Goal_IsCompletable(settings, &table);
    goal_completable = spoiler.goal_completable;
    {
      const PlacementStats *st = Placement_GetLastStats();
      spoiler.forward_fill_fallback_count = st->forward_fill_fallback_count;
      spoiler.retry_attempts = st->attempts_used;
    }
    // Phase B Slice 6 — Spoiler_Write branches on race_mode (full vs suppressed).
    if (!Spoiler_Write(&spoiler, spoiler_json_path, spoiler_txt_path)) {
      fprintf(stderr, "[settings] spoiler write failed: %s\n", spoiler_json_path);
    }
  }

  // Build & write the sidecar slot. Slot kind = Randomizer.
  RandoSidecarSlot slot;
  memset(&slot, 0, sizeof(slot));
  slot.header.slot_kind = kSlotKind_Randomizer;
  slot.header.generator_version = (uint16)kGeneratorVersion;
  memcpy(slot.header.settings_hash, settings_hash_full, 16);
  memcpy(slot.header.share_string, raw_binary, kRandoSidecar_ShareStringLength);
  // Phase B hints: carry the `hints` and `goal` axes in the slot's reserved
  // tail (rando_save.h settings extension) so telepathic-tile hints can be
  // regenerated at slot load. The generator reads only these two axes.
  slot.header.settings_ext_present = 1;
  slot.header.hints_setting = settings->hints;
  slot.header.goal = settings->goal;
  // Flags: set the forward-fill bit if the placer used the fallback.
  bool used_forward_fill = false;
  {
    const PlacementStats *st = Placement_GetLastStats();
    if (st->forward_fill_fallback_count > 0) {
      slot.header.flags |= kRandoSlotFlag_ForwardFillUsed;
      used_forward_fill = true;
    }
  }
  // Copy placements + compute placement_table_size (BYTES = 2 * max_loc_id + 2).
  if (table.count > (uint16)(sizeof(slot.placements) / sizeof(slot.placements[0]))) {
    if (err != NULL)
      snprintf(err, err_cap, "placement count %u exceeds sidecar slot capacity",
               (unsigned)table.count);
    free(entries);
    return false;
  }
  memcpy(slot.placements, entries, sizeof(RandoPlacement) * table.count);
  slot.placement_count = table.count;
  uint16 max_loc = 0;
  for (uint16 i = 0; i < table.count; ++i) {
    if (entries[i].location_id > max_loc) max_loc = entries[i].location_id;
  }
  slot.header.placement_table_size = (uint16)((max_loc + 1) * 2);

  // Initialize the target sram.dat slot with the same "new file" defaults
  // that NameFile_DoTheNaming applies for vanilla saves so the slot is
  // valid when the player picks it. The actual rando-specific runtime
  // bookkeeping (starting-inventory injection, etc.) happens at game-start
  // time via Rando_TryGrantStartingInventory etc.
  uint8 *target_sram = g_zenv.sram + slot_index * 0x500;
  memset(target_sram, 0, 0x500);
  // Pre-name the file "RANDO " to match the rando-banner convention.
  uint16 *name = (uint16 *)(target_sram + kSrmOffs_Name);
  name[0] = 0x21;  // R
  name[1] = 0x00;  // A
  name[2] = 0x0d;  // N
  name[3] = 0x03;  // D
  name[4] = 0x0e;  // O
  name[5] = 0xa9;  // blank
  WORD(target_sram[0x3e5]) = 0x55aa;
  WORD(target_sram[0x20c]) = 0xf000;
  WORD(target_sram[0x20e]) = 0xf000;
  // 0x3e3 is name[5] (already blank above); DiedCounter lives at
  // kSrmOffs_DiedCounter = 0x405.
  WORD(target_sram[kSrmOffs_DiedCounter]) = 0xffff;
  // Replicate the new-file init from `NameFile_DoTheNaming` so the slot
  // has the canonical starting state — without this, health bytes
  // stayed zero, so the rando slot loaded as instant-death (0 hearts
  // / 0 max). The 60-byte block initializes health, magic, gloves, etc.
  // The bytes
  // 0x18,0x18 at offsets +44/+45 are the starting health + max health
  // (0x18 = 3 hearts in quarter-heart units); 0xf8 at offset +57 is the
  // boomerang/item slot baseline.
  static const uint8 kSramInit_Normal[60] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0,    0, 0, 0,    0,    0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0,    0, 0, 0,    0,    0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0,    0, 0, 0, 0x18, 0x18, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0xf8, 0, 0,
  };
  memcpy(target_sram + 0x340, kSramInit_Normal, 60);
  Intro_FixCksum(target_sram);

  if (!Rando_WriteSidecarSlot(slot_index, &slot, target_sram, 0x500)) {
    if (err != NULL)
      snprintf(err, err_cap, "sidecar write failed for slot %u", (unsigned)slot_index);
    free(entries);
    return false;
  }
  // Commit the vanilla SRAM image too (sidecar first by spec; then sram.dat).
  ZeldaWriteSram();

  // Apply recommended-features panel choices (if user toggled). Per spec
  // the user must opt in explicitly; we honor whatever state the panel
  // reflects (recommended_features0 vs g_config.features0). The user
  // changed bits — that's the explicit opt-in.
  if (recommended_features0 != g_config.features0) {
    g_config.features0 = recommended_features0;
  }

  // If the caller wants the placement, hand it an independently malloc'd copy
  // (POD RandoPlacement, trivially copyable) that the caller owns. Do this
  // BEFORE freeing the working `entries`.
  if (out != NULL) {
    memset(out, 0, sizeof(*out));
    out->ok = true;
    out->used_forward_fill = used_forward_fill;
    out->goal_completable = goal_completable;
    out->race_mode = (settings->race_mode != 0);
    memcpy(out->share_string, share_string, sizeof(out->share_string));
    memcpy(out->settings_hash, settings_hash_full, sizeof(out->settings_hash));
    if (table.count > 0) {
      RandoPlacement *copy =
          (RandoPlacement *)malloc(sizeof(RandoPlacement) * table.count);
      if (copy != NULL) {
        memcpy(copy, entries, sizeof(RandoPlacement) * table.count);
        out->placement.entries = copy;
        out->placement.count = table.count;
      }
    }
  }

  free(entries);

  // Reset sidecar cache + flag the slot active so the next file-select
  // render picks up the rando banner.
  SelectFile_NotifySlotWritten(slot_index);

  return true;
}
