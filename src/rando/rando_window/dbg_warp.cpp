// dbg_warp.cpp — Debug > Warp panel: teleport Link by handing off to the game's
// own dungeon loader (module 6 / Module_PreDungeon, which self-spawns Link,
// camera, tileset and sprites the next frame). This panel OWNS the warp g_ram
// sequence (the static DoWarp* helpers below). It is the riskiest debug feature,
// so every write is gated hard by Cheats_CanWarp() and re-checked inside the
// helper (defense in depth). PC only.
//
// Mechanism (verified against src/dungeon.c:8383 Dungeon_LoadEntrance and
// src/misc.c:165 module dispatch table):
//   Module_PreDungeon (dispatch index 6) calls Dungeon_LoadEntrance(), which
//   sets player_is_indoors=1 itself and then branches:
//     if (WORD(follower_indicator)==4 || WORD(death_var4))  -> START-POINT path,
//        loading from kStartingPoint_*[which_starting_point];
//     else                                                  -> ENTRANCE path,
//        loading from kEntranceData_*[which_entrance].
//   Module_PreDungeon then advances main_module_index to 7 (Module07_Dungeon)
//   itself (dungeon.c:6572), so there is no risk of re-running the loader.
//
// We expose two flavors:
//   * Named "safe" destinations -> START-POINT path. which_starting_point 0/1/6
//     (Link's House / Sanctuary / Mountain Cave) are exactly the spawn-select
//     menu locations; misc.c:603-604 documents these as "the only safe spawn
//     points" (escape-only points 2/3/4/5 are traps), so this is the most
//     battle-tested entry the game has.
//   * Raw entrance id -> ENTRANCE path. Experimental; out-of-range / special ids
//     may softlock.
#ifdef Z3R_NATIVE_SETTINGS_WINDOW
#include "imgui.h"
#include "game_cheats.h"
#include "game_panels.h"

extern "C" {
extern uint8 g_ram[0x20000];  // game-state RAM (zelda_rtl.c)
}

// --- g_ram cells this panel touches (all verified against src/variables.h) ---
//   0x10    main_module_index        (uint8)
//   0x11    submodule_index          (uint8)
//   0xB0    subsubmodule_index       (uint8)
//   0x1B    player_is_indoors        (uint8)  -- loader sets this itself; we
//                                                 set it too for clarity.
//   0x10A   death_var5               (uint8)  -- read as WORD in the loader
//   0x4AA   death_var4               (uint8)  -- read as WORD in the loader
//   0x10E   which_entrance           (uint8)
//   0xF3C8  which_starting_point     (uint8)
//   0xF3CC  follower_indicator       (uint8)  -- read as WORD in the loader
static const uint32 kMainModule       = 0x10;
static const uint32 kSubModule        = 0x11;
static const uint32 kSubSubModule     = 0xB0;
static const uint32 kPlayerIsIndoors  = 0x1B;
static const uint32 kDeathVar5        = 0x10A;   // word 0x10A..0x10B
static const uint32 kDeathVar4        = 0x4AA;   // word 0x4AA..0x4AB
static const uint32 kWhichEntrance    = 0x10E;
static const uint32 kWhichStartPoint  = 0xF3C8;
static const uint32 kFollowerIndicator= 0xF3CC;  // word 0xF3CC..0xF3CD

// Common tail: route the module dispatcher to Module_PreDungeon (index 6) at a
// clean submodule. Called only after the branch-select bytes are already set.
static void HandOffToLoader() {
  g_ram[kPlayerIsIndoors] = 1;     // Dungeon_LoadEntrance sets this too; explicit here.
  g_ram[kSubSubModule]    = 0;
  g_ram[kSubModule]       = 0;
  g_ram[kMainModule]      = 6;     // Module_PreDungeon runs next frame.
}

// Warp to one of the spawn-select start points via the START-POINT branch of
// Dungeon_LoadEntrance. Forcing the branch needs WORD(death_var4) != 0; we set
// the low byte to 1 (any nonzero works, and the loader clears death_var4 once it
// has consumed it at dungeon.c:8461).
static void DoWarpStartPoint(uint8 sp) {
  if (!Cheats_CanWarp()) return;   // defense in depth: re-check at the write site.
  g_ram[kWhichStartPoint] = sp;
  // Clear death_var5 word so the loader's "save current exit state" path runs
  // normally (death_var5 set would skip it — irrelevant for a warp, but keep it
  // deterministic).
  g_ram[kDeathVar5] = 0;
  g_ram[kDeathVar5 + 1] = 0;
  // Force the start-point branch: WORD(death_var4) truthy.
  g_ram[kDeathVar4] = 1;
  g_ram[kDeathVar4 + 1] = 0;
  // Clear any active follower (tagalong). Vanilla spawn-select reaches these
  // points follower-free; warping with a stale follower can leave one wrongly
  // placed in the destination. death_var4 still forces the start-point branch.
  g_ram[kFollowerIndicator] = 0;
  HandOffToLoader();
}

// Warp to a raw entrance id via the ENTRANCE branch. Forcing that branch needs
// BOTH WORD(follower_indicator) != 4 AND WORD(death_var4) == 0, so we clear the
// full death_var4 word, the death_var5 word, and the follower_indicator low byte
// (low byte 0 makes WORD(follower_indicator) impossible to equal 4).
static void DoWarpEntrance(uint8 entrance) {
  if (!Cheats_CanWarp()) return;   // defense in depth.
  g_ram[kWhichEntrance] = entrance;
  g_ram[kDeathVar4] = 0;
  g_ram[kDeathVar4 + 1] = 0;
  g_ram[kDeathVar5] = 0;
  g_ram[kDeathVar5 + 1] = 0;
  g_ram[kFollowerIndicator] = 0;   // WORD can no longer equal 4.
  HandOffToLoader();
}

extern "C" void DbgWarp_Render(void) {
  const bool can = Cheats_CanWarp();

  ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f),
                     "EXPERIMENTAL - verify by playtest; some destinations may softlock.");
  ImGui::TextDisabled("Hands off to the game's own dungeon loader (module 6).");
  ImGui::Separator();

  if (!can) {
    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                       "Warp from normal gameplay only -- not menus/transitions/replay/RAM-compare.");
  }

  ImGui::BeginDisabled(!can);

  // --- Safe, battle-tested destinations (start-point path) -----------------
  ImGui::SeparatorText("Safe destinations");
  ImGui::TextDisabled("Spawn-select start points -- the most battle-tested entries.");
  if (ImGui::Button("Link's House"))  DoWarpStartPoint(0);
  ImGui::SameLine();
  if (ImGui::Button("Sanctuary"))     DoWarpStartPoint(1);
  ImGui::SameLine();
  if (ImGui::Button("Mountain Cave")) DoWarpStartPoint(6);

  // --- Advanced (raw entrance id, entrance path) ---------------------------
  ImGui::Spacing();
  if (ImGui::CollapsingHeader("Advanced (experimental)")) {
    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
                       "Out-of-range / special entrance ids may softlock or load a broken room.");
    static int s_entrance = 0;
    ImGui::SetNextItemWidth(120.0f);
    // Hex entry — entrance ids are conventionally written in hex (0x3C..0x84).
    ImGui::InputScalar("Entrance id (hex)", ImGuiDataType_S32, &s_entrance, NULL, NULL, "%02X",
                       ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_CharsUppercase);
    if (s_entrance < 0)    s_entrance = 0;
    if (s_entrance > 0x84) s_entrance = 0x84;  // entrance ids run to 0x84 (cave ids 0x83-0x84)
    ImGui::SameLine();
    if (ImGui::Button("Warp to entrance")) DoWarpEntrance((uint8)s_entrance);
    ImGui::TextDisabled("Range clamped to 0..0x84; verify each id by playtest.");
  }

  ImGui::EndDisabled();
}
#endif
