// dbg_snapshots.cpp — Debug > Snapshots panel: save-state slot manager over the
// existing SaveLoadSlot() / kSaveLoad_* API. PC only. This surfaces the save-state
// system that is otherwise only reachable through the Shift/Ctrl+F1-F10 keybinds.
// One row per slot 0..9 with Save / Load / Replay buttons; the on-disk files are
// "saves/save%d.sav" (same path the F1-F10 keys use, slot = key index).
//
// SaveLoadSlot serializes/loads live APU+DSP state that the SDL audio-callback
// thread concurrently mutates, so every call must hold the audio mutex — the
// keybind path wraps HandleCommand_Locked in SDL_LockMutex(g_audio_mutex)
// (main.c: "Everything that might access audio state (like SaveLoad and Reset)
// must have the lock"). This panel renders on the main thread OUTSIDE that
// lock, so it takes ZeldaApuLock() around each call itself.
#ifdef Z3R_NATIVE_SETTINGS_WINDOW

#include "imgui.h"
#include "game_panels.h"

#include <cstdio>

// Minimal C-side surface — avoid pulling in zelda_rtl.h's full include graph.
extern "C" {
  void SaveLoadSlot(int cmd, int which);
  void ZeldaApuLock(void);    // main.c — audio-callback mutex
  void ZeldaApuUnlock(void);
}

// Audio-safe wrapper: hold the audio mutex across the snapshot IO + state
// swap, matching the F1-F10 keybind path.
static void SaveLoadSlotLocked(int cmd, int which) {
  ZeldaApuLock();
  SaveLoadSlot(cmd, which);
  ZeldaApuUnlock();
}
enum { kSaveLoad_Save = 0, kSaveLoad_Load = 1, kSaveLoad_Replay = 2 };

#define kSnapshotSlotCount 10

// Returns true if "saves/save<slot>.sav" exists; on success *out_size gets its
// byte length.
static bool SnapshotSlotInfo(int slot, long *out_size) {
  char name[64];
  snprintf(name, sizeof name, "saves/save%d.sav", slot);
  FILE *f = fopen(name, "rb");
  if (!f)
    return false;
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  fclose(f);
  if (out_size)
    *out_size = sz;
  return true;
}

extern "C" void DbgSnapshots_Render(void) {
  ImGui::TextWrapped(
    "Save-states for slots 0-9. These mirror the in-game hotkeys: "
    "Shift+F1-F10 saves, F1-F10 loads, Ctrl+F1-F10 replays (slot = function-key "
    "index). Save is only meaningful while a game is running.");
  ImGui::Spacing();

  if (ImGui::BeginTable("snapshot_slots", 3,
                        ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg)) {
    ImGui::TableSetupColumn("Slot");
    ImGui::TableSetupColumn("Status");
    ImGui::TableSetupColumn("Actions");
    ImGui::TableHeadersRow();

    // Slot-info cache: stat()ing all 10 files every rendered frame is needless
    // per-frame IO (can stutter on cold/AV-scanned disks). Refresh ~1x/second
    // and immediately after a Save.
    static bool s_exists[kSnapshotSlotCount];
    static long s_size[kSnapshotSlotCount];
    static int s_refresh;
    if (s_refresh-- <= 0) {
      s_refresh = 60;
      for (int i = 0; i < kSnapshotSlotCount; i++)
        s_exists[i] = SnapshotSlotInfo(i, &s_size[i]);
    }

    for (int slot = 0; slot < kSnapshotSlotCount; slot++) {
      ImGui::PushID(slot);
      ImGui::TableNextRow();

      long size = s_size[slot];
      bool exists = s_exists[slot];

      ImGui::TableNextColumn();
      ImGui::Text("Slot %d", slot);

      ImGui::TableNextColumn();
      if (exists) {
        if (size >= 1024)
          ImGui::Text("%.1f KB", size / 1024.0);
        else
          ImGui::Text("%ld B", size);
      } else {
        ImGui::TextDisabled("(empty)");
      }

      ImGui::TableNextColumn();
      if (ImGui::SmallButton("Save")) {
        SaveLoadSlotLocked(kSaveLoad_Save, slot);
        s_refresh = 0;  // pick up the new file next frame
      }

      ImGui::SameLine();
      if (!exists)
        ImGui::BeginDisabled();
      if (ImGui::SmallButton("Load"))
        SaveLoadSlotLocked(kSaveLoad_Load, slot);
      ImGui::SameLine();
      if (ImGui::SmallButton("Replay"))
        SaveLoadSlotLocked(kSaveLoad_Replay, slot);
      if (!exists)
        ImGui::EndDisabled();

      ImGui::PopID();
    }

    ImGui::EndTable();
  }
}
#endif
