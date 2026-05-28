// rando_window_bridge.c — see rando_window_bridge.h for the ownership contract.
#ifdef Z3R_NATIVE_SETTINGS_WINDOW

#include "rando_window_bridge.h"

#include <stdlib.h>
#include <string.h>

#include "../rando.h"  // kGeneratorVersion

RandoWindowBridge g_rando_window_bridge;

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
  // Full cross-field validation (crystals_tower<=crystals_ganon for Fast/Ganon-Hunt;
  // pieces_required<=pieces_placed for Triforce/Ganon-Hunt; Completionist forces
  // accessibility=locations) lands in P4 §8. Stub accepts.
  (void)s;
  if (out_err != NULL && cap > 0) out_err[0] = '\0';
  return 0;
}

#endif  // Z3R_NATIVE_SETTINGS_WINDOW
