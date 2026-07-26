// panels_selftest.cpp — headless self-checks for the native UI, wired into
// --rando-selftest (PC only). Two checks:
//
//   Cheats_SelfCheck()        — pins the cheat-core safety invariant: the edit
//                               gate keys off the module byte (+ replay/emulator)
//                               and the pokes clamp and no-op when gated off.
//   Panels_RenderSmokeCheck() — creates a throwaway, backend-less ImGui context
//                               and renders every Debug/Randomizer panel once.
//                               Catches crashes / null-derefs / hard window-stack
//                               imbalance across all panels. (Begin/End balance
//                               assertions are ImGui IM_ASSERT == assert(), which
//                               is compiled out under NDEBUG, so this is a crash
//                               catcher rather than a full balance verifier in a
//                               Release build; it still exercises every code path.)
#ifdef Z3R_NATIVE_SETTINGS_WINDOW

#include "imgui.h"
#include <cstdio>
#include <cstdlib>

#include "game_cheats.h"
#include "game_panels.h"
#include "game_config_widgets.h"

extern "C" {
extern uint8 g_ram[0x20000];
bool ZeldaIsReplaying(void);
bool ZeldaIsEmulatorAttached(void);
}

// ---------------------------------------------------------------------------
extern "C" void Cheats_SelfCheck(void) {
  // Precondition for the module-gate assertions below: at --rando-selftest no
  // snapshot is replaying and no ROM is attached. If that ever changes, the gate
  // would always be closed; skip rather than false-fail.
  if (ZeldaIsReplaying() || ZeldaIsEmulatorAttached()) {
    fprintf(stderr, "[Cheats_SelfCheck] SKIP (replay/emulator active)\n");
    return;
  }

  const uint32 kScratch = 0x1FF00;  // unused high RAM; restored at the end
  uint8 s10 = g_ram[0x10], s11 = g_ram[0x11];
  uint8 sa = g_ram[kScratch], sb = g_ram[kScratch + 1];
  int fails = 0;
#define CHECK(cond, msg) do { if (!(cond)) { fprintf(stderr, "[Cheats_SelfCheck] FAIL: %s\n", msg); fails++; } } while (0)

  // 1. Edit gate keys off the module whitelist.
  g_ram[0x10] = 0x07; CHECK(Cheats_CanEdit(),  "CanEdit must be true in dungeon (0x07)");
  g_ram[0x10] = 0x0E; CHECK(Cheats_CanEdit(),  "CanEdit must be true in interface/menu (0x0E)");
  g_ram[0x10] = 0x01; CHECK(!Cheats_CanEdit(), "CanEdit must be false at file-select (0x01)");
  g_ram[0x10] = 0x12; CHECK(!Cheats_CanEdit(), "CanEdit must be false during game-over (0x12)");

  // 2. Pokes clamp to [lo,hi] when editing is allowed.
  g_ram[0x10] = 0x09;  // overworld (editable)
  Cheats_PokeByte(kScratch, 999, 0, 50);  CHECK(g_ram[kScratch] == 50, "PokeByte clamp to hi");
  Cheats_PokeByte(kScratch, -10, 3, 50);  CHECK(g_ram[kScratch] == 3,  "PokeByte clamp to lo");

  // 3. Pokes no-op when the gate is closed.
  g_ram[0x10] = 0x01; g_ram[kScratch] = 42;
  Cheats_PokeByte(kScratch, 7, 0, 50);    CHECK(g_ram[kScratch] == 42, "PokeByte must no-op when !CanEdit");

  // 4. PokeWord writes little-endian; PokeBit sets/clears one bit.
  g_ram[0x10] = 0x09;
  Cheats_PokeWord(kScratch, 0x1234, 0, 0xFFFF);
  CHECK(g_ram[kScratch] == 0x34 && g_ram[kScratch + 1] == 0x12, "PokeWord little-endian");
  g_ram[kScratch] = 0;
  Cheats_PokeBit(kScratch, 3, true);      CHECK(g_ram[kScratch] == 0x08, "PokeBit set");
  Cheats_PokeBit(kScratch, 3, false);     CHECK(g_ram[kScratch] == 0x00, "PokeBit clear");

  // 5. Warp gate: run module + submodule 0 only.
  g_ram[0x10] = 0x07; g_ram[0x11] = 0;    CHECK(Cheats_CanWarp(),  "CanWarp true in dungeon at submodule 0");
  g_ram[0x11] = 5;                        CHECK(!Cheats_CanWarp(), "CanWarp false at submodule != 0");
  g_ram[0x10] = 0x0E; g_ram[0x11] = 0;    CHECK(!Cheats_CanWarp(), "CanWarp false in menu module (0x0E)");

#undef CHECK
  g_ram[0x10] = s10; g_ram[0x11] = s11;
  g_ram[kScratch] = sa; g_ram[kScratch + 1] = sb;
  if (fails) { fprintf(stderr, "[Cheats_SelfCheck] %d failure(s)\n", fails); exit(2); }
  fprintf(stderr, "[Cheats_SelfCheck] OK\n");
}

// ---------------------------------------------------------------------------
extern "C" void Panels_RenderSmokeCheck(void) {
  RandoHints_SelfCheck();
  ImGuiContext *ctx = ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.LogFilename = nullptr;
  io.DisplaySize = ImVec2(1280.0f, 720.0f);
  io.DeltaTime = 1.0f / 60.0f;
  // Build a font atlas so NewFrame has a valid font (no GPU upload needed); the
  // dummy TexID just marks it ready for backends that check.
  unsigned char *pixels = nullptr;
  int tw = 0, th = 0;
  io.Fonts->GetTexDataAsRGBA32(&pixels, &tw, &th);
  io.Fonts->SetTexID((ImTextureID)(intptr_t)1);

  // Two frames: some ImGui layout (auto-sizing, tab bars) settles on frame 2.
  for (int frame = 0; frame < 2; frame++) {
    ImGui::NewFrame();
    ImGui::Begin("##smoke_watch");     DbgWatch_Render();     ImGui::End();
    ImGui::Begin("##smoke_inv");       DbgInventory_Render(); ImGui::End();
    ImGui::Begin("##smoke_snap");      DbgSnapshots_Render(); ImGui::End();
    ImGui::Begin("##smoke_time");      DbgTime_Render();      ImGui::End();
    ImGui::Begin("##smoke_warp");      DbgWarp_Render();      ImGui::End();
    ImGui::Begin("##smoke_flags");     DbgFlags_Render();     ImGui::End();
    ImGui::Begin("##smoke_reach");     RandoReach_Render();   ImGui::End();
    ImGui::Begin("##smoke_hints");     RandoHints_Render();   ImGui::End();
    ImGui::Begin("##smoke_gamecfg");   GameConfig_RenderTab();ImGui::End();
    ImGui::Begin("##smoke_gamedbg");   GameDebug_RenderTab(); ImGui::End();
    ImGui::Render();  // builds ImDrawData; no backend required
  }

  ImGui::DestroyContext(ctx);
  fprintf(stderr, "[Panels_RenderSmokeCheck] OK (10 panels x2 frames)\n");
}

#endif  // Z3R_NATIVE_SETTINGS_WINDOW
