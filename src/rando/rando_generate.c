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
#include "rando_hints.h"      // Rando_GenerateHints (populate hints[] before spoiler write)
#include "shuffle_entrance.h" // Phase C entrance shuffle (cave permutation + region overrides)

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// Initializes a freshly-generated rando playable slot's 0x500-byte SRAM image:
// the vanilla "new file" defaults (RANDO name + health/magic baseline) plus the
// world-state-specific post-escape start state. Writes ONLY into
// target_sram[0..0x4ff]; no file/config/global side effects. Factored out of
// Rando_GenerateSlot so the start-state init is unit-testable
// (RandoGenerate_SelfCheck) against a scratch buffer — this exact init was
// silently dropped by a merge once, booting non-Standard slots into the vanilla
// intro and softlocking. Mirrors ALTTPR's initsramtable.asm.
void Rando_InitNewSlotSram(uint8 *target_sram, uint8 world_state) {
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
  WORD(target_sram[kSrmOffs_DiedCounter]) = 0xffff;
  // Replicate the new-file init from NameFile_DoTheNaming so the slot has the
  // canonical starting state — without this, health bytes stayed zero and the
  // rando slot loaded as instant-death (0/0 hearts). Offsets +44/+45 = starting
  // + max health (0x18 = 3 hearts in quarter-heart units); +57 = item baseline.
  static const uint8 kSramInit_Normal[60] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0,    0, 0, 0,    0,    0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0,    0, 0, 0,    0,    0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0,    0, 0, 0, 0x18, 0x18, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0xf8, 0, 0,
  };
  memcpy(target_sram + 0x340, kSramInit_Normal, 60);

  // Non-Standard world-state starting SRAM. The placer pre-grants RescuedZelda
  // and skips the sphere-0 weapon/lamp guarantee for every non-Standard
  // world_state (the logic graph assumes the HC escape is already done), so the
  // runtime MUST start post-escape or a fresh save boots into the vanilla
  // rain/uncle/escape intro and hard-softlocks. target_sram[X] maps to
  // g_ram[0xF000 + X] once the slot loads. Standard is intentionally untouched:
  // the vanilla intro IS the Standard start.
  switch (world_state) {
    case kWorldState_Open:
    case kWorldState_Retro:
      // Light-World post-escape free-roam. World flag stays 0 (Light World),
      // no Moon Pearl / Mirror grant.
      target_sram[0x3C5] = 0x02;  // sram_progress_indicator (skip intro)
      target_sram[0x3C6] = 0x14;  // sram_progress_flags
      break;
    case kWorldState_Inverted:
      target_sram[0x3CA] = 0x40;  // savegame_is_darkworld (DW)
      target_sram[0x3C5] = 0x02;  // sram_progress_indicator (skip intro)
      target_sram[0x3C6] = 0x14;  // sram_progress_flags
      target_sram[0x357] = 0x01;  // link_item_moon_pearl (held; no bunny in DW)
      target_sram[0x353] = 0x02;  // link_item_mirror (Magic Mirror)
      target_sram[0x3C8] = 0x01;  // which_starting_point (Sanctuary)
      break;
    case kWorldState_Standard:
    default:
      // Standard: vanilla rain/uncle/escape intro — leave SRAM at fresh defaults.
      break;
  }
}

// Self-test for the world-state start-state SRAM init above (the merge-dropped
// softlock class). Runs against a scratch buffer — no side effects. Called by
// Rando_RunAllSelfChecks; exits(2) on mismatch.
void RandoGenerate_SelfCheck(void) {
  uint8 sram[0x500];

  // Common new-file defaults must always be present (a zeroed health block
  // booted the slot as instant-death; the RANDO name marks the banner).
  Rando_InitNewSlotSram(sram, kWorldState_Standard);
  if (sram[0x340 + 44] != 0x18 || sram[0x340 + 45] != 0x18 ||
      sram[0x340 + 57] != 0xf8 || sram[kSrmOffs_Name] != 0x21) {
    fprintf(stderr, "RandoGenerate_SelfCheck: new-file SRAM defaults missing\n");
    exit(2);
  }
  // Standard keeps the vanilla rain/uncle/escape intro: NO post-escape bytes.
  // If the whole switch is ever dropped again, non-Standard would match this
  // (all-zero) instead of its post-escape state — caught by the cases below.
  if (sram[0x3C5] != 0 || sram[0x3C6] != 0 || sram[0x3CA] != 0 ||
      sram[0x357] != 0 || sram[0x353] != 0 || sram[0x3C8] != 0) {
    fprintf(stderr, "RandoGenerate_SelfCheck: Standard slot must keep vanilla intro SRAM\n");
    exit(2);
  }
  // Open + Retro: Light-World post-escape free-roam (skip intro); no DW/MoonPearl.
  for (int i = 0; i < 2; ++i) {
    uint8 ws = (i == 0) ? (uint8)kWorldState_Open : (uint8)kWorldState_Retro;
    Rando_InitNewSlotSram(sram, ws);
    if (sram[0x3C5] != 0x02 || sram[0x3C6] != 0x14 ||
        sram[0x3CA] != 0 || sram[0x357] != 0 || sram[0x353] != 0) {
      fprintf(stderr, "RandoGenerate_SelfCheck: Open/Retro post-escape SRAM wrong (ws=%u)\n",
              (unsigned)ws);
      exit(2);
    }
  }
  // Inverted: Dark-World start with Moon Pearl + Magic Mirror + Sanctuary spawn.
  Rando_InitNewSlotSram(sram, kWorldState_Inverted);
  if (sram[0x3CA] != 0x40 || sram[0x3C5] != 0x02 || sram[0x3C6] != 0x14 ||
      sram[0x357] != 0x01 || sram[0x353] != 0x02 || sram[0x3C8] != 0x01) {
    fprintf(stderr, "RandoGenerate_SelfCheck: Inverted DW start-state SRAM wrong\n");
    exit(2);
  }
  fprintf(stderr, "[RandoGenerate_SelfCheck] OK\n");
}

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

  // Phase C — entrance shuffle: draw a cave permutation π, install its per-seed
  // region overrides so the placer/goal-check see the shuffled reachability, run
  // placement, and accept the first π under which the goal is completable
  // (reject-and-retry; coupled caves are ≈always solvable so attempt 0 normally
  // wins). The accepted attempt index is stored in the slot header (with the
  // packed axis byte) so slot-load regenerates the same π for the door overlay.
  // Default-off ⇒ this whole block is skipped and placement is byte-identical.
  uint8 entrance_axes = 0;
  uint8 entrance_attempt = 0;
  uint8 cave_assign[kEntranceMaxInteriors];
  int cave_count = 0;
  uint8 dun_assign[kEntranceMaxInteriors];
  int dun_count = 0;
  uint8 cross_assign[kEntranceMaxInteriors];
  int cross_count = 0;
  uint8 decoupled_assign[kEntranceMaxInteriors];
  int decoupled_count = 0;
  bool cross_on = Entrance_IsCrossActive(settings);   // supersedes the separate paths
  bool cave_on = !cross_on && Entrance_IsActive(settings);
  bool dun_on = !cross_on && Entrance_IsDungeonActive(settings);
  // Decoupled ("Insanity", D.1/D.2 — LOGIC + GENERATION only; runtime exit redirect
  // D.3/D.4 not yet wired, so this is exercised only by --generate-seed/corpus, not a
  // playable slot). Adds one-way exit warps on top of the cave entry shuffle.
  bool decoupled_on = Entrance_IsDecoupledActive(settings);
  bool placed = false;
  Entrance_ClearRegionOverrides();  // ensure a clean logic graph
  Entrance_ClearEdgeOverrides();
  if (cross_on || cave_on || dun_on || decoupled_on) {
    uint8 canon[kSettingsCanonicalLen];
    Settings_CanonicalSerialize(settings, canon);
    entrance_axes = canon[25];  // == the packed entrance-axis byte
    const int kEntranceMaxRetry = 64;
    for (int att = 0; att < kEntranceMaxRetry; att++) {
      if (cross_on) {
        cross_count = Entrance_ComputeCrossPermutation(settings, seed_u64, (uint8)att, cross_assign);
        Entrance_ApplyCrossOverrides(cross_assign, cross_count);
      } else {
        if (cave_on) {
          cave_count = Entrance_ComputePermutation(settings, seed_u64, (uint8)att, cave_assign);
          Entrance_ApplyRegionOverrides(cave_assign, cave_count);
        }
        if (dun_on) {
          dun_count = Entrance_ComputeDungeonPermutation(settings, seed_u64, (uint8)att, dun_assign);
          Entrance_ApplyEdgeOverrides(dun_assign, dun_count);
        }
      }
      // Decoupled exit warps compose on top of whichever entry pass ran (D.1).
      // ApplyDecoupledExitEdges begins edge overrides itself only if no edge pass
      // ran this attempt, so it never wipes the dungeon/cross edge set.
      if (decoupled_on) {
        decoupled_count = Entrance_ComputeDecoupledExit(settings, seed_u64, (uint8)att, decoupled_assign);
        Entrance_ApplyDecoupledExitEdges(decoupled_assign, decoupled_count);
      }
      table.count = 0;
      if (Place_AssumedFill(settings, seed_u64, effective_budget, &table)) {
        // Require FULL reachability, not just goal-completability: an entrance
        // permutation can make some interiors circularly unreachable (e.g. a
        // medallion/crystal-gated door leading to the very dungeon that grants
        // the gating item). Place_AssumedFill accepts a best-effort with stranded
        // placements; we must REJECT that π and try another rather than ship a
        // seed with unreachable items. Logic_ComputeSpheres returns true only when
        // every placement is reachable.
        RandoSpheres reach_spheres;
        if (Logic_ComputeSpheres(settings, &table, &reach_spheres) &&
            Goal_IsCompletable(settings, &table)) {
          placed = true;
          entrance_attempt = (uint8)att;
          break;
        }
      }
    }
    // NB: leave the accepted π's overrides ACTIVE — the spoiler's sphere + goal
    // computation below must see the shuffled reachability. Cleared right after
    // the spoiler block (and at the next generation's start).
  } else {
    placed = Place_AssumedFill(settings, seed_u64, effective_budget, &table);
  }
  if (!placed) {
    // Audit H1 — clear the entrance overrides on the failure path too (the
    // success path clears after the spoiler block; a failed generation would
    // otherwise leak the last attempt's shuffled reachability to the tracker).
    Entrance_ClearRegionOverrides();
    Entrance_ClearEdgeOverrides();
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
    // Phase B Slice 5 §3 (ported from main's in-game generate fix) — populate
    // per-NPC hint texts into g_hint_table so the spoiler's hints[] array is
    // non-empty when hints=on. The CLI --generate-seed path does this before its
    // Spoiler_Write; this shared playable-slot path (in-game screen + native
    // window) must too, or the spoiler emits an empty hints[] despite "hints": 1.
    // No-op when hints == Off.
    Rando_GenerateHints(settings, &table, &spheres);
    RandoSpoiler spoiler;
    memset(&spoiler, 0, sizeof(spoiler));
    spoiler.share_string = share_string;
    spoiler.seed_u64 = seed_u64;
    spoiler.generator_version = kGeneratorVersion;
    spoiler.settings = settings;
    spoiler.placements = &table;
    spoiler.spheres = &spheres;
    // Phase C — entrance_mapping sections (omitted when the respective count is 0).
    spoiler.entrance_assign = (cave_count > 0) ? cave_assign : NULL;
    spoiler.entrance_count = cave_count;
    spoiler.dungeon_assign = (dun_count > 0) ? dun_assign : NULL;
    spoiler.dungeon_count = dun_count;
    spoiler.cross_assign = (cross_count > 0) ? cross_assign : NULL;
    spoiler.cross_count = cross_count;
    spoiler.decoupled_assign = (decoupled_count > 0) ? decoupled_assign : NULL;
    spoiler.decoupled_count = decoupled_count;
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
  // Phase C — the accepted π's overrides have now fed placement, the spheres, and
  // the goal check. Clear them so any later reachability (e.g. the tracker, or the
  // next generation) starts from the identity graph.
  Entrance_ClearRegionOverrides();
  Entrance_ClearEdgeOverrides();

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
  // Carry world_state too (@68). Without this the slot header keeps the memset
  // 0 = kWorldState_Open, so a Standard/Inverted/Retro seed loads as Open at
  // runtime: for Standard that makes Rando_SuppressHyruleCastleEscape() true and
  // despawns the opening (Link's-house) Uncle — room 0x104, whose low byte 0x04
  // collides with the sewers-passage branch in SpritePrep_UncleAndPriest_bounce —
  // so Link sleeps forever (player_sleep_in_bed_state never advances).
  slot.header.world_state = settings->world_state;
  // Phase C — carry the entrance-shuffle axes + accepted goal-retry attempt so
  // slot-load can regenerate the cave permutation π (and install the door
  // overlay) deterministically from the seed. 0/0 when no shuffle was active.
  slot.header.entrance_axes = entrance_axes;
  slot.header.entrance_attempt = entrance_attempt;
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

  // Initialize the target sram.dat slot image (new-file defaults + world-state
  // post-escape start state) via the shared, self-tested helper above. The
  // rando-specific runtime bookkeeping (starting-inventory injection, etc.)
  // happens at game-start time via Rando_TryGrantStartingInventory.
  uint8 *target_sram = g_zenv.sram + slot_index * 0x500;
  Rando_InitNewSlotSram(target_sram, settings->world_state);

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
