// rando_window_bridge.c — see rando_window_bridge.h for the ownership contract.
#ifdef Z3R_NATIVE_SETTINGS_WINDOW

#include "rando_window_bridge.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../rando.h"  // kGeneratorVersion
#include "../rando_spoiler.h"  // RandoSpoiler, Spoiler_Write (§14.4/§14.5)

RandoWindowBridge g_rando_window_bridge;

bool RandoWindowBridge_WriteSpoilerFiles(const char *json_path, const char *txt_path) {
  const RandoWindowBridge *b = &g_rando_window_bridge;
  if (!b->has_last_generated || b->last_generated_placement.entries == NULL)
    return false;
  // Build the spoiler from the generate-time SNAPSHOT (not `pending`, which the
  // user may have edited since). Hints are NOT re-emitted (re-running
  // Rando_GenerateHints would mutate shared hint state); this is a convenience
  // export of the displayed placement. Spheres are not snapshotted for the native
  // path, so sphere_data is omitted (NULL).
  RandoSpoiler sp;
  memset(&sp, 0, sizeof sp);
  sp.share_string = b->last_generated_share_string;
  sp.seed_u64 = b->last_generated_seed_u64;
  sp.generator_version = (uint32)kGeneratorVersion;
  sp.settings = &b->last_generated_settings;
  sp.placements = &b->last_generated_placement;
  sp.spheres = NULL;
  sp.goal_completable = b->last_generated_goal_completable;
  return Spoiler_Write(&sp, json_path, txt_path);
}

void RandoWindowBridge_RecomputeDerived(void) {
  RandoWindowBridge *b = &g_rando_window_bridge;
  Settings_ComputeHash(&b->pending, b->pending_hash);
  ShareString ss;
  memset(&ss, 0, sizeof ss);
  ss.version = (uint8)kGeneratorVersion;
  memcpy(ss.settings_hash, b->pending_hash, 16);
  ss.seed_u64 = b->seed_u64;
  if (Share_Encode(&ss, b->share_string, (int)sizeof b->share_string) <= 0) {
    b->share_string[0] = '\0';
  }
}

void RandoWindowBridge_Init(void) {
  RandoWindowBridge *b = &g_rando_window_bridge;
  memset(b, 0, sizeof *b);
  b->target_slot_index = -1;
  b->load_slot_index = -1;
  b->generate_status = 0;
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
                                      bool race_mode) {
  RandoWindowBridge *b = &g_rando_window_bridge;
  // Free any prior owned copy before overwriting.
  free(b->last_generated_placement.entries);
  b->last_generated_placement.entries = NULL;
  b->last_generated_placement.count = 0;
  b->has_last_generated = false;

  if (table != NULL && table->entries != NULL && table->count > 0) {
    RandoPlacement *copy = (RandoPlacement *)malloc((size_t)table->count * sizeof(RandoPlacement));
    if (copy != NULL) {
      memcpy(copy, table->entries, (size_t)table->count * sizeof(RandoPlacement));
      b->last_generated_placement.entries = copy;
      b->last_generated_placement.count = table->count;
      if (spheres != NULL) b->last_generated_spheres = *spheres;
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
  // NOTE (audit round 3): crystals_tower may freely exceed crystals_ganon. The
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

  return 0;
}

#endif  // Z3R_NATIVE_SETTINGS_WINDOW
