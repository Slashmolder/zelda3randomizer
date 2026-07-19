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
// We expose three flavors:
//   * Named "safe" destinations -> START-POINT path. which_starting_point 0/1/6
//     (Link's House / Sanctuary / Mountain Cave) are exactly the spawn-select
//     menu locations; misc.c:603-604 documents these as "the only safe spawn
//     points" (escape-only points 2/3/4/5 are traps), so this is the most
//     battle-tested entry the game has.
//   * Raw entrance id -> ENTRANCE path. Experimental; out-of-range / special ids
//     may softlock.
//   * Room index -> reverse-looks-up the entrance that lands in that room, then
//     takes the ENTRANCE path. Entrance ids (0..0x84) and room indices are
//     DIFFERENT namespaces; this lets you reach cave/house pot rooms (>= 0x100)
//     by room number. Most dungeon-interior rooms have no direct entrance (the
//     button greys out) -- warp to the dungeon's entrance and walk instead.
#ifdef Z3R_NATIVE_SETTINGS_WINDOW
#include "imgui.h"
#include "game_cheats.h"
#include "game_panels.h"
#include <stdlib.h>   // strtol (parse the hex room field)
#include <stdio.h>    // snprintf (per-entrance button labels)

extern "C" {
extern uint8 g_ram[0x20000];          // game-state RAM (zelda_rtl.c)
extern const uint8 *g_asset_ptrs[];   // asset 11 = kEntranceData_rooms (entrance->room map)
extern uint32 g_asset_sizes[];        // bytes; /2 = entry count (uint16 rooms)
void SaveDungeonKeys(void);           // dungeon.c — persist link_num_keys to its
                                      // per-dungeon (or Retro generic) slot;
                                      // no-op outside dungeons (idx 0xff).
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
// Mirrors the two state-saving steps every vanilla route into
// Dungeon_LoadEntrance performs first:
//   1. Persist small keys. Module_PreDungeon overwrites link_num_keys from the
//      DESTINATION's per-dungeon slot right after the loader runs, so a warp
//      out of a dungeon without SaveDungeonKeys() silently discards every key
//      collected since entering (all vanilla routes — mirror, transitions,
//      death — call it; see SaveDungeonKeys callers in dungeon.c/messaging.c).
//   2. Suppress the exit-tableau snapshot for INDOOR origins. With
//      WORD(death_var5)==0 the loader caches the CURRENT state (link/BG2/camera
//      coords, scroll targets, tile themes) as the overworld exit; from a
//      dungeon interior those values poison the tableau, and the next exterior
//      door exit restores coordinates the masked-equality scroll terminators
//      never match (the equality-latched-tableau class — see CLAUDE.md).
//      Vanilla's only indoor start-point load (death respawn) sets death_var5
//      nonzero for exactly this reason. Overworld origins keep 0: caching the
//      pre-warp spot as the exit is correct there.
static void HandOffToLoader() {
  SaveDungeonKeys();               // no-op unless warping out of a dungeon
  g_ram[kDeathVar5]     = g_ram[kPlayerIsIndoors] ? 1 : 0;
  g_ram[kDeathVar5 + 1] = 0;
  g_ram[kPlayerIsIndoors] = 1;     // Dungeon_LoadEntrance sets this too; explicit here.
  g_ram[kSubSubModule]    = 0;
  g_ram[kSubModule]       = 0;
  g_ram[kMainModule]      = 6;     // Module_PreDungeon runs next frame.
}

// Follower handling, death-path parity (messaging.c): vanilla clears only the
// disposable follower ids {6, 9, 10, 13} on respawn and keeps story escorts
// (Zelda = 1, the old man = 4) attached. A blanket clear silently deletes an
// escort mid-quest. The entrance path must ADDITIONALLY clear the old man (4):
// WORD(follower_indicator)==4 would divert the loader into the start-point
// branch.
static void ClearDisposableFollowers(bool also_old_man) {
  uint8 f = g_ram[kFollowerIndicator];
  if (f == 6 || f == 9 || f == 10 || f == 13 || (also_old_man && f == 4))
    g_ram[kFollowerIndicator] = 0;
}

// Warp to one of the spawn-select start points via the START-POINT branch of
// Dungeon_LoadEntrance. Forcing the branch needs WORD(death_var4) != 0; we set
// the low byte to 1 (any nonzero works, and the loader clears death_var4 once it
// has consumed it at dungeon.c:8461).
static void DoWarpStartPoint(uint8 sp) {
  if (!Cheats_CanWarp()) return;   // defense in depth: re-check at the write site.
  g_ram[kWhichStartPoint] = sp;
  // Force the start-point branch: WORD(death_var4) truthy. (death_var5 is set
  // by HandOffToLoader based on the warp ORIGIN — see its comment.)
  g_ram[kDeathVar4] = 1;
  g_ram[kDeathVar4 + 1] = 0;
  // Drop disposable followers only (death parity); story escorts stay attached.
  // death_var4 forces the start-point branch regardless of follower id.
  ClearDisposableFollowers(/*also_old_man=*/false);
  HandOffToLoader();
}

// Warp to a raw entrance id via the ENTRANCE branch. Forcing that branch needs
// BOTH WORD(follower_indicator) != 4 AND WORD(death_var4) == 0, so we clear the
// full death_var4 word and (only when it is the old man, id 4) the follower.
// (death_var5 is set by HandOffToLoader based on the warp ORIGIN.)
static void DoWarpEntrance(uint8 entrance) {
  if (!Cheats_CanWarp()) return;   // defense in depth.
  g_ram[kWhichEntrance] = entrance;
  g_ram[kDeathVar4] = 0;
  g_ram[kDeathVar4 + 1] = 0;
  ClearDisposableFollowers(/*also_old_man=*/true);  // WORD can no longer equal 4.
  HandOffToLoader();
}

// Collect EVERY entrance whose destination is `room` (into out[0..cap)), and
// return how many exist. The entrance->room map is asset 11 (kEntranceData_rooms);
// the loader consumes it at dungeon.c:8771 and rando.c:2875 scans it identically.
// Cave/house interiors (rooms >= 0x100) each have an entrance, so this resolves
// them; most dungeon-interior rooms have no direct entrance (returns 0).
//
// MORE THAN ONE entrance can land in the SAME room for shared/split interiors —
// Pond of Wishing 0x114 is reached by both the LW-waterfall fairy side (pot-LESS)
// and the DW-mire side (the pot side), and refill cave 0x11b is split into two
// pot clusters with different access. Returning only the first match could warp
// to the pot-less / wrong-cluster half and give false playtest evidence for
// exactly the pot rooms this branch validates, so we expose every match and let
// the tester pick the side.
static int EntrancesForRoom(int room, int *out, int cap) {
  const uint16 *rooms = (const uint16*)g_asset_ptrs[11];
  uint32 count = g_asset_sizes[11] / 2u;
  int n = 0;
  for (uint32 i = 0; i < count; i++) {
    if (rooms[i] == (uint16)room) {
      if (n < cap) out[n] = (int)i;
      n++;
    }
  }
  return n;
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

    // Warp by ROOM index. Entrance ids and room indices are different namespaces
    // -- a cave/house pot room like 0x114 is reached via its entrance, not by
    // typing 0x114 into the entrance field above. We reverse-look-up the entrance
    // that lands in the room and take the entrance path. Blank by default; the
    // button stays disabled until the field holds a room an entrance can reach
    // (dungeon interiors have none).
    ImGui::Spacing();
    static char s_room[8] = "";
    ImGui::SetNextItemWidth(120.0f);
    ImGui::InputText("Room (hex)", s_room, sizeof(s_room),
                     ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_CharsUppercase);
    int ents[8];
    int n_ents = 0;
    if (s_room[0]) {
      long room = strtol(s_room, NULL, 16);
      if (room >= 0 && room <= 0x14F) n_ents = EntrancesForRoom((int)room, ents, 8);
    }
    if (n_ents <= 0) {
      // No direct entrance (dungeon interiors), or empty field: keep the disabled
      // button so the layout doesn't jump.
      ImGui::SameLine();
      ImGui::BeginDisabled(true);
      ImGui::Button("Warp to room");
      ImGui::EndDisabled();
      if (s_room[0])
        ImGui::TextDisabled("No direct entrance lands in that room (dungeon interiors have none).");
    } else if (n_ents == 1) {
      ImGui::SameLine();
      if (ImGui::Button("Warp to room")) DoWarpEntrance((uint8)ents[0]);
    } else {
      // Shared / split interior: more than one entrance reaches this room and
      // only one side holds the pots. Expose each so the tester picks the side
      // (warping to "the first match" can land on the pot-less / wrong half).
      ImGui::TextDisabled("Shared interior: %d entrances reach this room - pick a side:", n_ents);
      int shown = n_ents < 8 ? n_ents : 8;
      for (int k = 0; k < shown; k++) {
        char lbl[24];
        snprintf(lbl, sizeof(lbl), "via 0x%02X##rm%d", ents[k], k);
        if (ImGui::Button(lbl)) DoWarpEntrance((uint8)ents[k]);
        if (k + 1 < shown) ImGui::SameLine();
      }
    }
  }

  ImGui::EndDisabled();
}
#endif
