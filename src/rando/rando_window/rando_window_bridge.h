// rando_window_bridge.{h,c} — the state bridge between the ImGui settings window
// (UI side) and the game thread. Lives under src/rando/rando_window/ so the Switch
// Makefile's non-recursive src/rando/*.c glob excludes it (Switch keeps the in-game UI).
//
// THREADING / OWNERSHIP: both the UI (ImGui) frame and the game frame run on the SDL
// main thread, so NO mutex is needed. The ownership split is a discipline, not a lock:
//   UI side mutates:   pending, pending_recommended_features0, seed_u64,
//                      target_slot_index, generate_requested.
//   Game side mutates: generate_in_progress, generate_status, generate_error,
//                      last_generated_* (the spoiler-viewer snapshot).
// No field is written by both sides.
#ifndef ZELDA3_RANDO_RANDO_WINDOW_RANDO_WINDOW_BRIDGE_H_
#define ZELDA3_RANDO_RANDO_WINDOW_RANDO_WINDOW_BRIDGE_H_

#ifdef Z3R_NATIVE_SETTINGS_WINDOW

#include <stddef.h>
#include "../../types.h"
#include "../rando_settings.h"   // RandoSettings, Settings_*
#include "../rando_share.h"      // kShareStringBase32MaxLen, ShareString, Share_Encode
#include "../rando_placement.h"  // RandoPlacementTable, RandoSpheres

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RandoWindowBridge {
  RandoSettings pending;                 // settings the UI is editing
  uint32 pending_recommended_features0;  // recommended-features panel state (snapshot of g_config.features0 at open)
  uint8 pending_hash[32];                // cached Settings_ComputeHash(pending)
  char share_string[kShareStringBase32MaxLen];  // cached encoded share string
  uint64 seed_u64;                       // UI-chosen seed
  int target_slot_index;                 // kind-toggle target; -1 = none

  bool generate_requested;               // UI sets true; game consumes at frame start
  bool generate_in_progress;             // game owns
  int generate_status;                   // 0=idle 1=running 2=success -1=error (game owns)
  char generate_error[256];              // populated on -1 (game owns)

  // Spoiler-viewer snapshot — game writes on a successful generate; UI reads.
  // last_generated_placement.entries is an OWNED malloc'd copy (freed on next
  // successful generate and at shutdown). Gates the viewer via last_generated_race_mode.
  bool has_last_generated;
  bool last_generated_race_mode;
  RandoPlacementTable last_generated_placement;
  RandoSpheres last_generated_spheres;
} RandoWindowBridge;

extern RandoWindowBridge g_rando_window_bridge;

// Lifecycle / derived state.
void RandoWindowBridge_Init(void);
void RandoWindowBridge_RecomputeDerived(void);  // refresh pending_hash + share_string

// Generate request/response (UI ↔ game).
void RandoWindowBridge_RequestGenerate(int slot_index);
bool RandoWindowBridge_ConsumeGenerateRequest(void);
void RandoWindowBridge_SetGenerateResult(int status, const char *err);

// Spoiler-viewer snapshot (game side, on success). Takes an owned copy of `table`.
void RandoWindowBridge_StoreGenerated(const RandoPlacementTable *table,
                                      const RandoSpheres *spheres,
                                      bool race_mode);

// Kind-toggle target management.
void RandoWindowBridge_CancelTarget(void);

// Cross-field validation; returns 0 if valid, non-zero with a message in out_err.
int RandoWindowBridge_Validate(const RandoSettings *s, char *out_err, size_t cap);

#ifdef __cplusplus
}
#endif

#endif  // Z3R_NATIVE_SETTINGS_WINDOW
#endif  // ZELDA3_RANDO_RANDO_WINDOW_RANDO_WINDOW_BRIDGE_H_
