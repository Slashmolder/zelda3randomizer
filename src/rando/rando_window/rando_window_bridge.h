// rando_window_bridge.{h,c} — the state bridge between the ImGui settings window
// (UI side) and the game thread. Lives under src/rando/rando_window/ so the Switch
// Makefile's non-recursive src/rando/*.c glob excludes it (Switch keeps the in-game UI).
//
// THREADING / OWNERSHIP: both the UI (ImGui) frame and the game frame run on the SDL
// main thread, so NO mutex is needed. The ownership split is a discipline, not a lock:
//   UI side mutates:   pending, pending_recommended_features0, seed_u64,
//                      target_slot_index, generate_requested, load_requested,
//                      load_slot_index.
//   Game side mutates: generate_in_progress, generate_status, generate_error,
//                      last_generated_* (the spoiler-viewer snapshot).
// No field is written by both sides.
#ifndef ZELDA3_RANDO_RANDO_WINDOW_RANDO_WINDOW_BRIDGE_H_
#define ZELDA3_RANDO_RANDO_WINDOW_RANDO_WINDOW_BRIDGE_H_

#ifdef Z3R_NATIVE_SETTINGS_WINDOW

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// These rando-core headers declare C functions (Settings_*, Share_*,
// placement helpers). They are included INSIDE the extern "C" block so that a
// C++ TU (rando_window.cpp) sees their declarations with C linkage — otherwise
// the linker looks for C++-mangled names and fails. They do not self-wrap in
// extern "C", so the wrapping must happen here at the include site.
#include "../../types.h"
#include "../rando_settings.h"   // RandoSettings, Settings_*
#include "../rando_share.h"      // kShareStringBase32MaxLen, ShareString, Share_Encode
#include "../rando_placement.h"  // RandoPlacementTable, RandoSpheres

typedef struct RandoWindowBridge {
  RandoSettings pending;                 // settings the UI is editing
  uint32 pending_recommended_features0;  // recommended-features panel state (snapshot of g_config.features0 at open)
  uint8 pending_hash[32];                // cached Settings_ComputeHash(pending)
  char share_string[kShareStringBase32MaxLen];  // cached encoded share string
  uint64 seed_u64;                       // UI-chosen seed
  int target_slot_index;                 // kind-toggle target; -1 = none

  bool generate_requested;               // UI sets true; game consumes at frame start
  bool load_requested;                   // UI "Load it now" sets true; game consumes at frame start
  int load_slot_index;                   // slot to load when load_requested (UI sets; game reads)
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
  // Snapshot of the settings/share/seed that produced the placement above, so the
  // Spoiler tab's "Save spoiler" can write an accurate RandoSpoiler even if the
  // user edits `pending` after generating. Written by the game thread alongside
  // last_generated_placement; UI reads. (game owns)
  RandoSettings last_generated_settings;
  char last_generated_share_string[kShareStringBase32MaxLen];
  uint64 last_generated_seed_u64;
  bool last_generated_goal_completable;
} RandoWindowBridge;

extern RandoWindowBridge g_rando_window_bridge;

// Spoiler save (§14.4 file / §14.5 clipboard). Builds a RandoSpoiler from the
// generate-time snapshot (last_generated_*) and calls Spoiler_Write. Lives in the
// C bridge TU because rando_spoiler.h uses a C11 _Static_assert invalid in the
// C++ window TU. `txt_path` may be NULL to skip the text companion (clipboard
// path). Returns true on success. Caller (UI) is on the main thread; this only
// reads bridge snapshot state + writes the named files (no g_ram).
bool RandoWindowBridge_WriteSpoilerFiles(const char *json_path, const char *txt_path);

// Lifecycle / derived state.
void RandoWindowBridge_Init(void);
void RandoWindowBridge_RecomputeDerived(void);  // refresh pending_hash + share_string

// Generate request/response (UI ↔ game).
void RandoWindowBridge_RequestGenerate(int slot_index);
bool RandoWindowBridge_ConsumeGenerateRequest(void);
void RandoWindowBridge_SetGenerateResult(int status, const char *err);

// "Load it now" request (UI ↔ game). UI side requests the just-generated slot be
// loaded; the game-thread consumer performs the file-select load (§13.7).
// ConsumeLoadRequest returns the slot index to load (>=0) or -1 if none pending.
void RandoWindowBridge_RequestLoad(int slot_index);
int RandoWindowBridge_ConsumeLoadRequest(void);

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
