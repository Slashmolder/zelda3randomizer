// game_cheats.cpp — shared cheat-core (gate + clamped pokes + ImGui widgets).
// See game_cheats.h. PC only. No g_ram cell here is an audit-tracked grant site;
// this is a .cpp under src/rando/ (outside the audit guard's scan set).
#ifdef Z3R_NATIVE_SETTINGS_WINDOW

#include "imgui.h"
#include "game_cheats.h"

#include <cstdio>   // snprintf (ComboVals unknown-value preview)

extern "C" {
#include "../../features.h"   // kFeatures0_*, kFeatures1_RandomizerActive
extern uint8 g_ram[0x20000];  // game-state RAM (zelda_rtl.c)
bool ZeldaIsReplaying(void);
bool ZeldaIsEmulatorAttached(void);
}

extern "C" uint32 Cheats_Features0(void) { return *(const uint32 *)(g_ram + 0x64c); }
static uint32 Features1(void) { return *(const uint32 *)(g_ram + 0x659); }
extern "C" bool Cheats_RandoActive(void) { return (Features1() & kFeatures1_RandomizerActive) != 0; }

extern "C" bool Cheats_CanEdit(void) {
  uint8 m = g_ram[0x10];  // main_module_index
  bool in_game = (m == 0x07 || m == 0x09 || m == 0x0B || m == 0x0E);
  return in_game && !ZeldaIsReplaying() && !ZeldaIsEmulatorAttached();
}

extern "C" bool Cheats_CanWarp(void) {
  uint8 m = g_ram[0x10];                                   // main_module_index
  bool run = (m == 0x07 || m == 0x09 || m == 0x0B);        // dungeon/overworld run states
  bool stable = (g_ram[0x11] == 0);                        // submodule_index == 0 (not mid-transition)
  return run && stable && !ZeldaIsReplaying() && !ZeldaIsEmulatorAttached();
}

extern "C" void Cheats_PokeByte(uint32 addr, int v, int lo, int hi) {
  if (!Cheats_CanEdit()) return;
  if (v < lo) v = lo;
  if (v > hi) v = hi;
  g_ram[addr] = (uint8)v;
}
extern "C" void Cheats_PokeWord(uint32 addr, int v, int lo, int hi) {
  if (!Cheats_CanEdit()) return;
  if (v < lo) v = lo;
  if (v > hi) v = hi;
  g_ram[addr] = (uint8)(v & 0xFF);
  g_ram[addr + 1] = (uint8)((v >> 8) & 0xFF);
}
extern "C" void Cheats_PokeBit(uint32 addr, int bit, bool on) {
  if (!Cheats_CanEdit()) return;
  uint8 cur = g_ram[addr];
  g_ram[addr] = (uint8)((cur & ~(1 << bit)) | (on ? (1 << bit) : 0));
}

extern "C" void Cheats_ByteSlider(const char *label, uint32 addr, int lo, int hi) {
  int v = g_ram[addr];
  if (v > hi) v = hi;
  if (ImGui::SliderInt(label, &v, lo, hi)) Cheats_PokeByte(addr, v, lo, hi);
}
extern "C" void Cheats_Toggle(const char *label, uint32 addr) {
  bool v = g_ram[addr] != 0;
  if (ImGui::Checkbox(label, &v)) Cheats_PokeByte(addr, v ? 1 : 0, 0, 1);
}
extern "C" void Cheats_Combo(const char *label, uint32 addr, const char *const *items, int count) {
  int v = g_ram[addr];
  if (v < 0 || v >= count) v = 0;  // sentinel/garbage (e.g. sword 0xFF) -> "None"
  if (ImGui::BeginCombo(label, items[v])) {
    for (int i = 0; i < count; i++) {
      bool sel = (v == i);
      if (ImGui::Selectable(items[i], sel) && i != v) Cheats_PokeByte(addr, i, 0, count - 1);
      if (sel) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
}
extern "C" void Cheats_ComboVals(const char *label, uint32 addr, const char *const *items,
                                 const int *vals, int count) {
  // idx -1 = the live byte matches no listed value. Falling back to items[0]
  // would both LIE (display a state the game is not in) and make selecting
  // entry 0 a silent no-op (the i != idx guard) — render an explicit unknown
  // preview instead, and let ANY selection write.
  int cur = g_ram[addr], idx = -1;
  for (int i = 0; i < count; i++) if (vals[i] == cur) idx = i;
  char unknown[24];
  const char *preview = items[idx >= 0 ? idx : 0];
  if (idx < 0) {
    snprintf(unknown, sizeof unknown, "(unknown 0x%02X)", cur);
    preview = unknown;
  }
  if (ImGui::BeginCombo(label, preview)) {
    for (int i = 0; i < count; i++) {
      bool sel = (idx == i);
      if (ImGui::Selectable(items[i], sel) && i != idx) Cheats_PokeByte(addr, vals[i], 0, 255);
      if (sel) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
}
extern "C" void Cheats_BitCheckbox(const char *label, uint32 addr, int bit) {
  bool on = (g_ram[addr] >> bit) & 1;
  if (ImGui::Checkbox(label, &on)) Cheats_PokeBit(addr, bit, on);
}

#endif  // Z3R_NATIVE_SETTINGS_WINDOW
