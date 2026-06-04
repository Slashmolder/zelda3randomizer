// dbg_flags.cpp — Debug > Flags panel: story/progress flag editor over a curated,
// source-verified map of progress bytes (gated; rando-unsafe flags disabled under
// the randomizer). PC only.
//
// All edits go through the shared cheat-core (game_cheats.h): one safety gate
// (Cheats_CanEdit), clamped/gated g_ram pokes. Same banner/BeginDisabled pattern
// as DbgInventory_Render in game_config_panels.cpp.
//
// Address + bit meanings below are source-verified (see provenance comments):
//   sram_progress_indicator   0xF3C5  variables.h:1115  (story stage value 0..3)
//   sram_progress_flags        0xF3C6  variables.h:1116  (story bits)
//   sram_progress_indicator_3  0xF3C9  variables.h:1119  (NPC-trade bits)
//   link_which_pendants        0xF374  variables.h:1107  (pendant bits)
//   link_has_crystals          0xF37A  variables.h:1112  (crystal bits)
//   save_ow_event_info         0xF280  variables.h:1061  (per-screen OW event bits)
#ifdef Z3R_NATIVE_SETTINGS_WINDOW
#include "imgui.h"
#include "game_cheats.h"
#include "game_panels.h"

extern "C" {
extern uint8 g_ram[0x20000];          // game-state RAM (zelda_rtl.c)
bool ZeldaIsReplaying(void);          // zelda_rtl.h
bool ZeldaIsEmulatorAttached(void);   // zelda_rtl.h
}

extern "C" void DbgFlags_Render(void) {
  bool can = Cheats_CanEdit();
  if (!can) {
    const char *why = ZeldaIsEmulatorAttached()
                          ? "Disabled while the original ROM is attached (RAM-compare mode)."
                      : ZeldaIsReplaying() ? "Disabled during snapshot replay."
                                           : "Load a save and enter the world to edit flags.";
    ImGui::TextColored(ImVec4(1, 0.7f, 0.3f, 1), "%s", why);
  } else {
    ImGui::TextDisabled("Edits write live save RAM. Save in-game to keep them.");
  }
  ImGui::TextColored(ImVec4(1, 0.85f, 0.4f, 1),
                     "Debug flags \xe2\x80\x94 setting these can put the game in an inconsistent "
                     "story state; for testing only.");
  ImGui::Separator();

  if (!can) ImGui::BeginDisabled();
  if (ImGui::BeginChild("##dbg_flags", ImVec2(0, 0))) {
    bool rando = Cheats_RandoActive();

    // -- Story stage (sram_progress_indicator 0xF3C5, a VALUE 0..3) ----------
    // Set to 1 alongside sram_progress_flags|=1 in Uncle_InPassage
    // (sprite_main.c:5872, "GiveSwordAndShield"). Rando Open/Inverted/Retro
    // seeds start this at 0x02 (post-escape) — see rando_generate.c:77/82 —
    // and softlock below 2 (the intro/escape never runs). So under rando we
    // expose only {Zelda saved, Master Sword} via ComboVals (values {2,3}),
    // which makes "below 2" unwritable; vanilla exposes the full 0..3.
    ImGui::SeparatorText("Story stage");
    if (rando) {
      static const char *const k[] = {"Zelda saved", "Master Sword"};
      static const int v[] = {2, 3};
      Cheats_ComboVals("Story stage", 0xF3C5, k, v, 2);
      ImGui::TextDisabled("(randomizer: cannot drop below \"Zelda saved\" \xe2\x80\x94 lower values softlock)");
    } else {
      static const char *const k[] = {"Intro", "Uncle left", "Zelda saved", "Master Sword"};
      Cheats_Combo("Story stage", 0xF3C5, k, 4);
    }

    // -- Story flags (sram_progress_flags 0xF3C6, bits) ----------------------
    // bit0 0x01 "Uncle gave sword"  set in Uncle_InPassage (sprite_main.c:5871)
    //                               alongside sram_progress_indicator=1. CAUTION:
    //                               coupled to the intro/sword sequence.
    // bit1 0x02 "Sanctuary cutscene" set in Priest path (sprite_main.c:11288 when
    //                               indicator>=3; also 5706).
    // bit2 0x04 "Zelda rescued"     set when Zelda follows to Sanctuary
    //                               (sprite_main.c:11304, follower_indicator==1).
    // bit4 0x10 "Uncle left house"  set in Uncle_ApplyTelepathyFollower
    //                               (sprite_main.c:5832); gates Link's-house room
    //                               0x104 in dungeon.c:6528/6564.
    ImGui::SeparatorText("Story flags");
    Cheats_BitCheckbox("Uncle gave sword (CAUTION: intro sequence)", 0xF3C6, 0);
    Cheats_BitCheckbox("Sanctuary rescue cutscene seen", 0xF3C6, 1);
    Cheats_BitCheckbox("Zelda rescued", 0xF3C6, 2);
    Cheats_BitCheckbox("Uncle left house", 0xF3C6, 4);

    // -- World events (save_ow_event_info 0xF280, per-screen overworld bits) -----
    // Pyramid of Power screen = 0x5b; bit5 0x20 = "Ganon's hole carved" (set by
    // CreatePyramidHole after Agahnim 2, overworld.c:3931). The hole renders on the
    // next load of that screen (Overworld_LoadEventOverlay case 0x5b). Rando-safe:
    // it's an overworld access state, not a tracked location, so placement is
    // unaffected — kept enabled under the randomizer (the whole point is testing
    // the Ganon fight without clearing Ganon's Tower).
    ImGui::SeparatorText("World events");
    Cheats_BitCheckbox("Pyramid of Power hole open", 0xF280 + 0x5b, 5);

    // -- NPC trades (sram_progress_indicator_3 0xF3C9, bits) -----------------
    // Each maps to a LOC_* whose checked-state the randomizer tracks separately,
    // so they are RANDO-UNSAFE — disabled under rando to avoid desync.
    //   bit0 0x01 Hobo bottle      (sprite_main.c:11157, LOC_Hobo)
    //   bit1 0x02 Bottle Merchant  (sprite_main.c:6371,  LOC_Bottle_Merchant)
    //   bit3 0x08 Flute Boy        (sprite_main.c:10322, FluteAardvark)
    //   bit4 0x10 Purple Chest     (sprite_main.c:11061, LOC_Purple_Chest)
    //   bit5 0x20 Blacksmiths      (sprite_main.c:10391, ReturningSmithy)
    ImGui::SeparatorText("NPC trades");
    if (rando) ImGui::BeginDisabled();
    Cheats_BitCheckbox("Hobo (gave bottle)", 0xF3C9, 0);
    Cheats_BitCheckbox("Bottle Merchant (bought bottle)", 0xF3C9, 1);
    Cheats_BitCheckbox("Flute Boy (gave flute)", 0xF3C9, 3);
    Cheats_BitCheckbox("Purple Chest (delivered)", 0xF3C9, 4);
    Cheats_BitCheckbox("Blacksmiths (freed/tempered)", 0xF3C9, 5);
    if (rando) {
      ImGui::EndDisabled();
      ImGui::TextDisabled("(disabled under randomizer \xe2\x80\x94 managed separately)");
    }

    // -- Pendants & Crystals -------------------------------------------------
    // Pendant bits (link_which_pendants 0xF374), per rando.c:312-314:
    //   bit2 0x04 = Green Pendant  (Courage, EP)
    //   bit1 0x02 = Red Pendant    (Power,   DP)
    //   bit0 0x01 = Blue Pendant   (Wisdom,  ToH)
    // Crystal bits (link_has_crystals 0xF37A), per rando.c:315-321 (dungeon map):
    //   0x01 SW, 0x02 SP, 0x04 IP, 0x08 TR, 0x10 PoD, 0x20 MM, 0x40 TT.
    // These won't update the randomizer's prize/goal tracking and desync under
    // prize_shuffle, so they live behind an "advanced" node AND are disabled
    // under rando.
    if (ImGui::TreeNode("Pendants & Crystals (advanced \xe2\x80\x94 will not update randomizer prize/goal tracking)")) {
      if (rando) ImGui::BeginDisabled();

      ImGui::SeparatorText("Pendants");
      Cheats_BitCheckbox("Pendant of Courage (green)", 0xF374, 2);
      Cheats_BitCheckbox("Pendant of Power (red)", 0xF374, 1);
      Cheats_BitCheckbox("Pendant of Wisdom (blue)", 0xF374, 0);

      ImGui::SeparatorText("Crystals");
      // Labels are by RAM bit position (mask = 1<<b). Dungeon names per
      // rando.c:315-321: 0x01 SW, 0x02 SP, 0x04 IP, 0x08 TR, 0x10 PoD, 0x20 MM,
      // 0x40 TT. (The numbered "Crystal N" in rando.c is the goal-counter name,
      // which does NOT track the bit order; we name by dungeon to avoid that
      // ambiguity.)
      static const char *const kCrystalLabel[7] = {
          "Skull Woods",         // bit0 0x01
          "Swamp Palace",        // bit1 0x02
          "Ice Palace",          // bit2 0x04
          "Turtle Rock",         // bit3 0x08
          "Palace of Darkness",  // bit4 0x10
          "Misery Mire",         // bit5 0x20
          "Thieves' Town",       // bit6 0x40
      };
      for (int b = 0; b < 7; b++) Cheats_BitCheckbox(kCrystalLabel[b], 0xF37A, b);

      if (rando) {
        ImGui::EndDisabled();
        ImGui::TextDisabled("(disabled under randomizer \xe2\x80\x94 prize_shuffle desync)");
      }
      ImGui::TreePop();
    }
  }
  ImGui::EndChild();
  if (!can) ImGui::EndDisabled();
}
#endif
