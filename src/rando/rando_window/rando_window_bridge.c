// rando_window_bridge.c — see rando_window_bridge.h for the ownership contract.
#ifdef Z3R_NATIVE_SETTINGS_WINDOW

#include "rando_window_bridge.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../rando.h"          // kGeneratorVersion
#include "../rando_spoiler.h"  // RandoSpoiler, Spoiler_Write (§14.4/§14.5)
#include "../customizer.h"     // Customizer_GetActive (Validate: manifest loaded?)

RandoWindowBridge g_rando_window_bridge;

typedef struct KeyRingValidationCache {
  bool valid;
  uint8 canonical[kSettingsCanonicalLen];
  uint64 seed_u64;
  bool selection_valid;
} KeyRingValidationCache;

static KeyRingValidationCache g_key_ring_validation_cache;

static bool key_ring_random_selection_valid(const RandoSettings *s,
                                            uint64 seed_u64) {
  uint8 canonical[kSettingsCanonicalLen];
  Settings_CanonicalSerialize(s, canonical);
  KeyRingValidationCache *cache = &g_key_ring_validation_cache;
  if (!cache->valid || cache->seed_u64 != seed_u64 ||
      memcmp(cache->canonical, canonical, sizeof(canonical)) != 0) {
    KeyRingSelection selection;
    cache->selection_valid = KeyRings_Resolve(s, seed_u64, &selection);
    memcpy(cache->canonical, canonical, sizeof(canonical));
    cache->seed_u64 = seed_u64;
    cache->valid = true;
  }
  return cache->selection_valid;
}

bool RandoWindowBridge_WriteSpoilerFiles(const char *json_path, const char *txt_path) {
  const RandoWindowBridge *b = &g_rando_window_bridge;
  if (!b->has_last_generated || b->last_generated_placement.entries == NULL)
    return false;
  // Build the spoiler from the generate-time SNAPSHOT (not `pending`, which the
  // user may have edited since). The sphere table and immutable hint plan are
  // both retained by value, so export has no dependency on active slot globals.
  RandoSpoiler sp;
  memset(&sp, 0, sizeof sp);
  sp.share_string = b->last_generated_share_string;
  sp.seed_u64 = b->last_generated_seed_u64;
  sp.generator_version = (uint32)kGeneratorVersion;
  sp.settings = &b->last_generated_settings;
  sp.placements = &b->last_generated_placement;
  sp.spheres = b->last_generated_has_spheres
                   ? &b->last_generated_spheres
                   : NULL;
  sp.hint_plan = b->last_generated_has_hint_plan
                     ? &b->last_generated_hint_plan
                     : NULL;
  sp.medallion_assignment = b->last_generated_has_medallion_assignment
                                ? b->last_generated_medallion_assignment
                                : NULL;
  sp.goal_completable = b->last_generated_goal_completable;
  return Spoiler_Write(&sp, json_path, txt_path);
}

void RandoWindowBridge_RecomputeDerived(void) {
  RandoWindowBridge *b = &g_rando_window_bridge;
  Settings_ComputeHash(&b->pending, b->pending_hash);
  ShareString ss;
  memset(&ss, 0, sizeof ss);
  ss.version = (uint8)kGeneratorVersion;
  ss.seed_u64 = b->seed_u64;
  // The cached display/copy string is the v2 EXCHANGE form (settings + seed,
  // add-rando-share-string-v2 D1/D2) — EXCEPT under customizer mode, where it
  // stays v1 (seed + hash): customizer placements depend on a local manifest
  // file no share string can carry (design D5). The v1 IDENTITY string
  // (last_generated_share_string, spoiler filename/meta) is produced
  // elsewhere and unaffected.
  int n;
  if (b->pending.customizer_active) {
    memcpy(ss.settings_hash, b->pending_hash, 16);
    n = Share_Encode(&ss, b->share_string, (int)sizeof b->share_string);
  } else {
    Settings_CanonicalSerialize(&b->pending, ss.settings_canonical);
    n = Share_EncodeV2(&ss, b->share_string, (int)sizeof b->share_string);
  }
  if (n <= 0) {
    b->share_string[0] = '\0';
  }
}

void RandoWindowBridge_Init(void) {
  RandoWindowBridge *b = &g_rando_window_bridge;
  memset(b, 0, sizeof *b);
  b->target_slot_index = -1;
  b->load_slot_index = -1;
  b->generate_status = 0;
  b->shape_filter_valid = true;
  b->shape_search_limit = 100;
  Settings_SetDefaults(&b->pending);
  RandoWindowBridge_RecomputeDerived();
}

void RandoWindowBridge_RequestGenerate(int slot_index) {
  RandoWindowBridge *b = &g_rando_window_bridge;
  b->target_slot_index = slot_index;
  b->generate_requested = true;
  b->generate_status = 1;  // running — UI shows the modal until the game reports back
  b->generate_error[0] = '\0';
}

bool RandoWindowBridge_ConsumeGenerateRequest(void) {
  RandoWindowBridge *b = &g_rando_window_bridge;
  if (!b->generate_requested) return false;
  b->generate_requested = false;
  b->generate_in_progress = true;
  return true;
}

void RandoWindowBridge_RequestLoad(int slot_index) {
  RandoWindowBridge *b = &g_rando_window_bridge;
  b->load_slot_index = slot_index;
  b->load_requested = true;
}

int RandoWindowBridge_ConsumeLoadRequest(void) {
  RandoWindowBridge *b = &g_rando_window_bridge;
  if (!b->load_requested) return -1;
  b->load_requested = false;
  int slot = b->load_slot_index;
  b->load_slot_index = -1;
  return slot;
}

void RandoWindowBridge_SetGenerateResult(int status, const char *err) {
  RandoWindowBridge *b = &g_rando_window_bridge;
  b->generate_in_progress = false;
  b->generate_status = status;
  if (err != NULL) {
    strncpy(b->generate_error, err, sizeof b->generate_error - 1);
    b->generate_error[sizeof b->generate_error - 1] = '\0';
  } else {
    b->generate_error[0] = '\0';
  }
}

void RandoWindowBridge_StoreGenerated(const RandoPlacementTable *table,
                                      const RandoSpheres *spheres,
                                      const uint8 *medallion_assignment,
                                      const RandoHintPlan *hint_plan,
                                      bool race_mode) {
  RandoWindowBridge *b = &g_rando_window_bridge;
  // Free any prior owned copy before overwriting.
  free(b->last_generated_placement.entries);
  b->last_generated_placement.entries = NULL;
  b->last_generated_placement.count = 0;
  b->last_generated_has_spheres = false;
  b->last_generated_has_medallion_assignment = false;
  b->last_generated_has_hint_plan = false;
  b->has_last_generated = false;

  if (table != NULL && table->entries != NULL && table->count > 0) {
    RandoPlacement *copy = (RandoPlacement *)malloc((size_t)table->count * sizeof(RandoPlacement));
    if (copy != NULL) {
      memcpy(copy, table->entries, (size_t)table->count * sizeof(RandoPlacement));
      b->last_generated_placement.entries = copy;
      b->last_generated_placement.count = table->count;
      if (spheres != NULL) {
        b->last_generated_spheres = *spheres;
        b->last_generated_has_spheres = true;
      }
      if (medallion_assignment != NULL) {
        memcpy(b->last_generated_medallion_assignment, medallion_assignment,
               sizeof(b->last_generated_medallion_assignment));
        b->last_generated_has_medallion_assignment = true;
      }
      if (hint_plan != NULL) {
        b->last_generated_hint_plan = *hint_plan;
        b->last_generated_has_hint_plan = true;
      }
      b->last_generated_race_mode = race_mode;
      b->has_last_generated = true;
    }
  }
}

void RandoWindowBridge_CancelTarget(void) {
  g_rando_window_bridge.target_slot_index = -1;
}

int RandoWindowBridge_Validate(const RandoSettings *s, char *out_err, size_t cap) {
  // Reject ONLY configurations the placer itself refuses — matching the in-game
  // path (select_file.c) and the CLI. Do NOT add constraints the game allows:
  //   - goal ∈ {triforce-hunt, ganonhunt}: pieces_required must not exceed
  //     pieces_placed (BuildItemPool refuses pieces_required > pieces_placed).
  // NOTE: crystals_tower may freely exceed crystals_ganon. The
  // in-game UI (CycleRow allows 0..7 on both), the CLI, and Goal_IsCompletable
  // all permit it — the Ganon's-Tower entry crystal gate is independent of the
  // Ganon-vulnerability gate — so the native window must NOT block it. The
  // Completionist→accessibility=locations rule is enforced in the UI (auto-set +
  // read-only combo) and normalized by Settings_CanonicalSerialize, so it is not
  // a failure condition here.
  if (out_err != NULL && cap > 0) out_err[0] = '\0';
  if (s == NULL) return 0;

  if ((s->goal == kGoal_TriforceHunt || s->goal == kGoal_GanonHunt) &&
      s->pieces_required > s->pieces_placed) {
    if (out_err != NULL && cap > 0)
      snprintf(out_err, cap,
               "Pieces required (%u) cannot exceed pieces placed (%u).",
               (unsigned)s->pieces_required, (unsigned)s->pieces_placed);
    return 1;
  }

  if (Settings_EffectiveKeyRings(s) == kKeyRings_Random &&
      !key_ring_random_selection_valid(s, g_rando_window_bridge.seed_u64)) {
    if (out_err != NULL && cap > 0)
      snprintf(out_err, cap,
               "Random key rings need at least two eligible dungeon key families.");
    return 1;
  }

  // add-rando-customizer-mode — both rejections mirror Rando_GenerateSlot's
  // fail-closed guards; validating here surfaces them as inline UI errors
  // (disabled Generate button) instead of a post-generate failure modal.
  if (s->customizer_active) {
    if (Customizer_GetActive() == NULL) {
      if (out_err != NULL && cap > 0)
        snprintf(out_err, cap,
                 "Customizer mode is on but no manifest is loaded. Load one "
                 "below or turn customizer mode off.");
      return 1;
    }
    if (s->race_mode) {
      if (out_err != NULL && cap > 0)
        snprintf(out_err, cap,
                 "Race mode cannot be combined with customizer mode (the race "
                 "reveal cannot regenerate manifest pins).");
      return 1;
    }
  }

  return 0;
}

#endif  // Z3R_NATIVE_SETTINGS_WINDOW
