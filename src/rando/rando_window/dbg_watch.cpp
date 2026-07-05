// dbg_watch.cpp — Debug > Watch panel: live, read-only readout of key game state
// (re-read from g_ram every frame) plus the F12 "Dump debug state" button.
// No edit gate — this panel only reads g_ram, never writes it. PC only.
#ifdef Z3R_NATIVE_SETTINGS_WINDOW
#include "imgui.h"
#include <SDL.h>
#include "game_cheats.h"
#include "game_panels.h"
#include "../chains_runtime.h"

extern "C" {
extern uint8 g_ram[0x20000];     // game-state RAM (zelda_rtl.c)
void ZeldaDumpDebugState(void);  // F12 dev diagnostic: dump g_ram/VRAM + state line (zelda_rtl.h)
}

static const char *ChainsReasonName(uint8 reason) {
  switch (reason) {
  case kChainsRtReason_Armed: return "armed";
  case kChainsRtReason_WrongSeam: return "wrong seam";
  case kChainsRtReason_HopRequested: return "hop requested";
  case kChainsRtReason_HopConsumed: return "hop consumed";
  case kChainsRtReason_MissingSyntheticEntrances: return "missing synthetic entrances";
  case kChainsRtReason_PinnedIdentity: return "pinned identity";
  case kChainsRtReason_BadSuccessor: return "bad successor";
  case kChainsRtReason_TerminalExit: return "terminal exit";
  case kChainsRtReason_MissingOrigin: return "missing origin";
  default: return "none";
  }
}

// Little helpers for reading the shared RAM buffer at the verified addresses in
// src/variables.h (e.g. link_x_coord is a uint16 at g_ram+0x22). We read fresh
// each frame so the readout tracks live game state.
static inline unsigned RamByte(uint32 addr) { return g_ram[addr]; }
static inline unsigned RamWord(uint32 addr) { return g_ram[addr] | (g_ram[addr + 1] << 8); }

extern "C" void DbgWatch_Render(void) {
  ImGui::TextDisabled("Live read-only state (refreshes every frame).");
  ImGui::Separator();

  // --- Module --------------------------------------------------------------
  ImGui::SeparatorText("Module");
  ImGui::Text("Main module:  0x%02X", RamByte(0x10));   // main_module_index
  ImGui::Text("Submodule:    0x%02X", RamByte(0x11));   // submodule_index
  ImGui::Text("Randomizer active: %s", Cheats_RandoActive() ? "yes" : "no");
  ImGui::Text("Can edit inventory: %s", Cheats_CanEdit() ? "yes" : "no");

  // --- Position ------------------------------------------------------------
  ImGui::SeparatorText("Position");
  ImGui::Text("Link X / Y:   %u, %u", RamWord(0x22), RamWord(0x20));  // link_x_coord / link_y_coord
  {
    // link_direction_facing (0x2F): low bits select up/down/left/right.
    static const char *const kFacing[] = {"up", "down", "left", "right"};
    unsigned dir = RamByte(0x2F);
    const char *name = (dir < 8 && (dir >> 1) < 4) ? kFacing[dir >> 1] : "?";
    ImGui::Text("Facing:       0x%02X (%s)", dir, name);
  }
  ImGui::Text("Is bunny:     %s", RamByte(0x56) ? "yes" : "no");  // link_is_bunny

  // --- Vitals --------------------------------------------------------------
  ImGui::SeparatorText("Vitals");
  // Health is stored in 1/8-heart units (8 == one full heart container).
  ImGui::Text("Health:       %u / %u  (%.1f / %.1f hearts)",
              RamByte(0xF36D), RamByte(0xF36C),               // current / capacity
              RamByte(0xF36D) / 8.0f, RamByte(0xF36C) / 8.0f);
  ImGui::Text("Magic:        %u / 128", RamByte(0xF36E));     // link_magic_power
  ImGui::Text("Rupees:       %u", RamWord(0xF362));           // link_rupees_actual
  ImGui::Text("Bombs:        %u", RamByte(0xF343));           // link_item_bombs
  ImGui::Text("Arrows:       %u", RamByte(0xF377));           // link_num_arrows
  ImGui::Text("Keys:         %u", RamByte(0xF36F));           // link_num_keys

  // --- World ---------------------------------------------------------------
  ImGui::SeparatorText("World");
  ImGui::Text("Indoors:      %s", RamByte(0x1B) ? "yes" : "no");          // player_is_indoors
  ImGui::Text("Dark world:   %s", RamByte(0xF3CA) ? "yes" : "no");        // savegame_is_darkworld
  ImGui::Text("OW screen:    0x%04X", RamWord(0x8A));                     // overworld_screen_index (uint16)
  ImGui::Text("Dungeon room: 0x%04X", RamWord(0xA0));                     // dungeon_room_index

  // --- Dungeon-chain spike -------------------------------------------------
  ImGui::SeparatorText("Dungeon-chain spike");
  const ChainsRuntimeDebug *chains = Chains_DebugState();
  ImGui::Text("EP->DP armed: %s", chains->spike_armed ? "yes" : "no");
  ImGui::Text("Hop pending:  %s", chains->hop_pending ? "yes" : "no");
  ImGui::Text("Origin armed: %s  room 0x%04X",
              chains->origin_active ? "yes" : "no", chains->origin_exit_room);
  ImGui::Text("Terminal:     %s", chains->terminal_active ? "yes" : "no");
  ImGui::Text("Seam checks:  %u", chains->seam_checks);
  ImGui::Text("Hop req/use:  %u / %u", chains->request_count, chains->consume_count);
  ImGui::Text("Last seam:    0x%04X -> 0x%04X dir %u",
              chains->last_source_room, chains->last_destination_room, chains->last_dir);
  ImGui::Text("Last entry:   0x%02X (%s)", chains->last_entrance,
              ChainsReasonName(chains->last_reason));
  ImGui::BeginDisabled(!Cheats_CanWarp());
  if (ImGui::Button("Arm EP->DP seam")) {
    Chains_DebugArmEpBossToDesert();
  }
  ImGui::SameLine();
  if (ImGui::Button("Clear arm")) {
    Chains_DebugClearEpBossToDesert();
  }
  ImGui::EndDisabled();

  // --- Diagnostics ---------------------------------------------------------
  // Moved out of the inventory panel: the same dump the F12 hotkey triggers.
  ImGui::SeparatorText("Diagnostics");
  static Uint32 s_dumped_until = 0;  // SDL_GetTicks() deadline for the confirmation line
  if (ImGui::Button("Dump debug state")) {
    ZeldaDumpDebugState();
    s_dumped_until = SDL_GetTicks() + 3000;  // show confirmation for 3 seconds
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Writes dump_gram.bin / dump_vram.bin / dump_oam.bin / dump_cgram.bin\n"
                      "and dump_hints.txt next to the executable (same as the F12 hotkey).");
  }
  if (s_dumped_until && SDL_GetTicks() < s_dumped_until) {
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.4f, 1, 0.4f, 1), "wrote dump_*");
  }
}
#endif
