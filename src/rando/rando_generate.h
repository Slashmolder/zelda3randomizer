// rando_generate.{h,c} — Rando_GenerateSlot(): the shared, UI-agnostic
// playable-slot generation path (Phase P3).
//
// This is a PURE RELOCATION of the body of SelectFile_Settings_HandleGenerate
// (src/select_file.c), parameterized so both the in-game settings screen and
// (later) the native settings window call the same code. There is NO behavior
// change vs. the in-game flow and NO change to seed determinism.
//
// This module is plain C and lives in src/rando/, so it is compiled on Switch
// too (the in-game screen calls Rando_GenerateSlot there). It uses ONLY
// rando-core + g_config + game-side slot helpers; it has NO window/ImGui deps.
#ifndef ZELDA3_RANDO_RANDO_GENERATE_H_
#define ZELDA3_RANDO_RANDO_GENERATE_H_

#include "../types.h"
#include "rando_settings.h"
#include "rando_share.h"     // kShareStringBase32MaxLen
#include "rando_placement.h"  // RandoPlacementTable

typedef struct RandoGenerateResult {
  bool ok;
  bool used_forward_fill;
  bool goal_completable;
  bool race_mode;
  char share_string[kShareStringBase32MaxLen];
  uint8 settings_hash[32];
  RandoPlacementTable placement;  // OWNED malloc'd copy when requested (caller frees); {0} otherwise
} RandoGenerateResult;

// Game-thread only. Runs the full playable-slot generation: placement + share +
// spoiler files + sidecar slot write + SRAM commit + recommended-features apply.
// Does NOT call Placement_Install (install is slot-load-only). budget<0 => race-aware
// default (race?0:10). If out!=NULL, out->placement is an independently malloc'd copy
// the CALLER owns. Returns false + err on failure (no slot written).
bool Rando_GenerateSlot(const RandoSettings *settings, uint64 seed_u64, int budget,
                        int slot_index, uint32 recommended_features0,
                        RandoGenerateResult *out, char *err, size_t err_cap);

extern void SelectFile_NotifySlotWritten(int slot_index);  // defined in select_file.c

#ifdef Z3R_NATIVE_SETTINGS_WINDOW
// Native-window "Load it now" seam (§13.7). Game-thread only; defined in
// select_file.c. Loads the just-generated playable slot exactly as pressing A
// on an occupied file-select slot would (sidecar reload + Rando_ActivateSidecarSlot
// + CopySaveToWRAM). Must run while Module01_FileSelect is active.
extern void SelectFile_LoadRandoSlot(int slot_index);
#endif

#endif  // ZELDA3_RANDO_RANDO_GENERATE_H_
