// tracker_windows.cpp — Item / Check / Map tracker windows (PC only).
// See tracker_windows.h for the design contract. Phase 0 wires the windows onto
// the imgui_host with placeholder bodies; later phases fill in the live content.
#ifdef Z3R_NATIVE_SETTINGS_WINDOW

#include "imgui.h"

#include <cstdio>   // snprintf

#include "imgui_host.h"
#include "tracker_windows.h"

// Game-side C APIs. rando.h is plain C (its lone _Static_assert is now
// C++-guarded); wrap in extern "C" so the C++ linker resolves the C symbols.
extern "C" {
#include "../rando.h"          // Rando_IsActive, Rando_FillItemView, RandoItemView, ...
}

// ---- Window handles --------------------------------------------------------
static Z3RWindow *s_win[kTracker_Count];

// Full-window borderless panel that fills the OS window (mirrors the settings
// window's framing). Pair with ImGui::End().
static void BeginFullWindow(const char *id) {
  const ImGuiViewport *vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(vp->WorkPos);
  ImGui::SetNextWindowSize(vp->WorkSize);
  ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                           ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                           ImGuiWindowFlags_NoBringToFrontOnFocus;
  ImGui::Begin(id, nullptr, flags);
}

// ---- Shared UI helpers -----------------------------------------------------
static const ImVec4 kHave   = ImVec4(0.18f, 0.62f, 0.30f, 1.0f);  // green
static const ImVec4 kMissing= ImVec4(0.22f, 0.22f, 0.25f, 1.0f);  // dim grey
static const ImVec4 kPartial= ImVec4(0.62f, 0.50f, 0.16f, 1.0f);  // amber

// A fixed-size status chip (non-interactive). `state`: 0 missing, 1 have,
// 2 partial. Chips flow left-to-right and wrap at the window's right edge.
static void Chip(const char *label, int state, float width = 92.0f) {
  ImVec4 col = (state == 1) ? kHave : (state == 2) ? kPartial : kMissing;
  // Wrap: if the next chip would overflow the content region, break the line.
  float avail = ImGui::GetContentRegionAvail().x;
  ImGuiStyle &st = ImGui::GetStyle();
  if (ImGui::GetCursorPosX() > st.WindowPadding.x &&
      avail < width + st.ItemSpacing.x) {
    ImGui::NewLine();
  }
  ImGui::PushStyleColor(ImGuiCol_Button, col);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, col);
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, col);
  ImGui::PushStyleColor(ImGuiCol_Text,
                        state == 0 ? ImVec4(0.55f, 0.55f, 0.58f, 1.0f)
                                   : ImVec4(1, 1, 1, 1));
  ImGui::Button(label, ImVec2(width, 28));
  ImGui::PopStyleColor(4);
  ImGui::SameLine();
}

// Leveled chip: shows "name Lk" (or a custom level label). state derived from level>0.
static void LevelChip(const char *name, int level, const char *level_text = nullptr) {
  char buf[40];
  if (level_text)
    snprintf(buf, sizeof buf, "%s: %s", name, level_text);
  else if (level > 0)
    snprintf(buf, sizeof buf, "%s %d", name, level);
  else
    snprintf(buf, sizeof buf, "%s", name);
  Chip(buf, level > 0 ? 1 : 0);
}

static void SectionHeader(const char *text) {
  ImGui::NewLine();
  ImGui::SeparatorText(text);
}

// ---- Draw callbacks --------------------------------------------------------
static void DrawItemTracker(void *) {
  BeginFullWindow("Item Tracker##z3r");

  if (!Rando_IsActive()) {
    ImGui::TextDisabled("No randomizer slot active.");
    ImGui::TextDisabled("Item state below reflects the current save's inventory.");
    ImGui::Spacing();
  }

  RandoItemView v;
  Rando_FillItemView(&v);

  static const char *kBow[3] = { "none", "wood", "silver" };
  static const char *kBoom[3] = { "none", "blue", "red" };
  static const char *kMail[3] = { "green", "blue", "red" };
  static const char *kMagic[3] = { "1x", "1/2x", "1/4x" };

  ImGui::SeparatorText("Equipment");
  LevelChip("Sword", v.sword);
  LevelChip("Shield", v.shield);
  LevelChip("Mail", v.mail + 1, kMail[v.mail <= 2 ? v.mail : 0]);  // mail always >=green
  LevelChip("Glove", v.gloves);
  LevelChip("Bow", v.bow, kBow[v.bow <= 2 ? v.bow : 0]);
  LevelChip("Boomerang", v.boomerang, kBoom[v.boomerang <= 2 ? v.boomerang : 0]);

  SectionHeader("Items");
  Chip("Hookshot", v.hookshot);
  Chip("Fire Rod", v.firerod);
  Chip("Ice Rod", v.icerod);
  Chip("Hammer", v.hammer);
  Chip("Lamp", v.lamp);
  Chip("Net", v.net);
  Chip("Book", v.book);
  Chip("Somaria", v.somaria);
  Chip("Byrna", v.byrna);
  Chip("Cape", v.cape);

  SectionHeader("Movement");
  Chip("Boots", v.boots);
  Chip("Flippers", v.flippers);
  Chip("Moon Pearl", v.moon_pearl);
  Chip("Mirror", v.mirror);

  SectionHeader("Medallions");
  Chip("Bombos", v.bombos);
  Chip("Ether", v.ether);
  Chip("Quake", v.quake);

  SectionHeader("Other");
  Chip("Mushroom", v.mushroom);
  Chip("Powder", v.powder);
  Chip("Flute", v.flute);
  Chip("Shovel", v.shovel);
  { char b[24]; snprintf(b, sizeof b, "Bottles %d", v.bottles); Chip(b, v.bottles > 0 ? 1 : 0); }
  LevelChip("Magic", v.magic, kMagic[v.magic <= 2 ? v.magic : 0]);

  SectionHeader("Prizes");
  { char b[24]; snprintf(b, sizeof b, "Crystals %d/7", v.crystals); Chip(b, v.crystals ? (v.crystals == 7 ? 1 : 2) : 0, 110.0f); }
  { char b[24]; snprintf(b, sizeof b, "Pendants %d/3", v.pendants); Chip(b, v.pendants ? (v.pendants == 3 ? 1 : 2) : 0, 110.0f); }
  Chip("Agahnim", v.agahnim, 92.0f);

  SectionHeader("Stats");
  { char b[24]; snprintf(b, sizeof b, "Hearts %d", v.hearts); Chip(b, v.hearts > 0 ? 1 : 0); }
  if (v.heart_pieces > 0) { char b[24]; snprintf(b, sizeof b, "Pieces %d/4", v.heart_pieces); Chip(b, 2); }
  ImGui::NewLine();

  ImGui::End();
}

static void DrawCheckTracker(void *) {
  BeginFullWindow("Check Tracker##z3r");
  ImGui::TextUnformatted("Check Tracker");
  ImGui::Separator();
  ImGui::TextDisabled("(reachable/checked locations — coming in phase 3)");
  ImGui::End();
}

static void DrawMapTracker(void *) {
  BeginFullWindow("Map Tracker##z3r");
  ImGui::TextUnformatted("Map Tracker");
  ImGui::Separator();
  ImGui::TextDisabled("(world map with check pins — coming in phase 4)");
  ImGui::End();
}

// ---- Lifecycle -------------------------------------------------------------
void Trackers_Init(void) {
  s_win[kTracker_Item]  = Z3RHost_Create("Z3R Item Tracker",  360, 520, DrawItemTracker,  nullptr);
  s_win[kTracker_Check] = Z3RHost_Create("Z3R Check Tracker", 420, 680, DrawCheckTracker, nullptr);
  s_win[kTracker_Map]   = Z3RHost_Create("Z3R Map Tracker",   720, 720, DrawMapTracker,   nullptr);
}

void Trackers_Toggle(int kind) {
  if (kind < 0 || kind >= kTracker_Count) return;
  Z3RHost_ToggleShown(s_win[kind]);
}

void Trackers_SetShown(int kind, bool shown) {
  if (kind < 0 || kind >= kTracker_Count) return;
  if (shown) Z3RHost_Show(s_win[kind]);
  else Z3RHost_Hide(s_win[kind]);
}

bool Trackers_IsShown(int kind) {
  if (kind < 0 || kind >= kTracker_Count) return false;
  return Z3RHost_IsShown(s_win[kind]);
}

void Trackers_ApplyGeometry(int kind, int x, int y, int w, int h) {
  if (kind < 0 || kind >= kTracker_Count) return;
  Z3RHost_ApplyGeometry(s_win[kind], x, y, w, h);
}

void Trackers_GetGeometry(int kind, int *x, int *y, int *w, int *h) {
  if (kind < 0 || kind >= kTracker_Count) { if (x) *x = 0; if (y) *y = 0; if (w) *w = 0; if (h) *h = 0; return; }
  Z3RHost_GetGeometry(s_win[kind], x, y, w, h);
}

void Trackers_Shutdown(void) {
  // Tear down every host-owned window (the 3 trackers). The settings window is
  // NOT a host window — it is shut down separately by RandoWindow_Shutdown, which
  // must run BEFORE this so its (current) ImGui context is destroyed first.
  Z3RHost_Shutdown();
  for (int i = 0; i < kTracker_Count; i++) s_win[i] = nullptr;
}

#endif  // Z3R_NATIVE_SETTINGS_WINDOW
