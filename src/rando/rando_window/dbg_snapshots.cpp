// dbg_snapshots.cpp — Debug > Snapshots panel: save-state slot manager over the
// existing SaveLoadSlot() / kSaveLoad_* API. PC only. This surfaces the save-state
// system that is otherwise only reachable through the Shift/Ctrl+F1-F10 keybinds.
// One row per slot 0..9 with Save / Load / Replay buttons; the on-disk files are
// "saves/save%d.sav" (same path the F1-F10 keys use, slot = key index).
//
// SaveLoadSlot does file IO + state load synchronously on the main thread; calling
// it directly here is fine — the keybind path invokes it the same way.
#ifdef Z3R_NATIVE_SETTINGS_WINDOW

#include "imgui.h"
#include "game_panels.h"

#include <cstdio>

// Minimal C-side surface — avoid pulling in zelda_rtl.h's full include graph.
extern "C" {
  void SaveLoadSlot(int cmd, int which);
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

    for (int slot = 0; slot < kSnapshotSlotCount; slot++) {
      ImGui::PushID(slot);
      ImGui::TableNextRow();

      long size = 0;
      bool exists = SnapshotSlotInfo(slot, &size);

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
      if (ImGui::SmallButton("Save"))
        SaveLoadSlot(kSaveLoad_Save, slot);

      ImGui::SameLine();
      if (!exists)
        ImGui::BeginDisabled();
      if (ImGui::SmallButton("Load"))
        SaveLoadSlot(kSaveLoad_Load, slot);
      ImGui::SameLine();
      if (ImGui::SmallButton("Replay"))
        SaveLoadSlot(kSaveLoad_Replay, slot);
      if (!exists)
        ImGui::EndDisabled();

      ImGui::PopID();
    }

    ImGui::EndTable();
  }
}
#endif
