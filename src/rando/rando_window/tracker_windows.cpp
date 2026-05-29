// tracker_windows.cpp — Item / Check / Map tracker windows (PC only).
// See tracker_windows.h for the design contract. Phase 0 wires the windows onto
// the imgui_host with placeholder bodies; later phases fill in the live content.
#ifdef Z3R_NATIVE_SETTINGS_WINDOW

#include "imgui.h"

#include <cstdio>   // snprintf
#include <cfloat>   // FLT_MIN (ImGui full-width sentinel)

#include "imgui_host.h"
#include "tracker_windows.h"

// Game-side C APIs. rando.h is plain C (its lone _Static_assert is now
// C++-guarded); wrap in extern "C" so the C++ linker resolves the C symbols.
extern "C" {
#include "../rando.h"          // Rando_IsActive, Rando_FillItemView, RandoItemView, ...
#include "../rando_logic.h"    // kRandoLocations/Regions, Rando_Get*Name, Reachability_HasLocation
#include "../rando_placement.h"// Placement_GetActive, RandoPlacementTable
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

// location_id -> region_id index, built once from the static logic table.
static uint16 s_loc_region[1024];
static bool s_loc_region_built = false;
static void BuildLocRegionIndex() {
  for (int i = 0; i < 1024; i++) s_loc_region[i] = 0xFFFF;
  for (uint32 i = 0; i < kRandoLocationsCount; i++) {
    uint16 id = kRandoLocations[i].id;
    if (id < 1024) s_loc_region[id] = kRandoLocations[i].region_id;
  }
  s_loc_region_built = true;
}

// Check status: 0 unreachable, 1 reachable-unchecked, 2 checked.
enum { kCheck_Unreachable = 0, kCheck_Reachable = 1, kCheck_Checked = 2 };

static void DrawCheckTracker(void *) {
  BeginFullWindow("Check Tracker##z3r");

  if (!Rando_IsActive()) {
    ImGui::TextDisabled("No randomizer slot active.");
    ImGui::TextDisabled("Start or load a randomizer slot to track checks.");
    ImGui::End();
    return;
  }
  if (!s_loc_region_built) BuildLocRegionIndex();

  const RandoPlacementTable *pt = Placement_GetActive();
  const RandoReachability *reach = Rando_GetLiveReachability();  // NULL => suppress
  bool have_reach = (reach != NULL);

  // Spoiler availability: never for race seeds. settings carry race_mode.
  const RandoSettings *settings = Rando_GetActiveSettings();
  bool race = settings && settings->race_mode;

  // Persistent UI state.
  static bool s_hide_checked = false;
  static bool s_only_reachable = false;
  static bool s_show_items = false;
  static char s_search[64] = "";
  if (race) s_show_items = false;

  // Compute summary counts.
  int n_total = pt ? (int)pt->count : 0;
  int n_checked = 0, n_reachable = 0;
  for (int i = 0; i < n_total; i++) {
    uint16 loc = pt->entries[i].location_id;
    if (Rando_IsLocationChecked(loc)) n_checked++;
    else if (have_reach && Reachability_HasLocation(reach, loc)) n_reachable++;
  }

  ImGui::Text("Checks: %d checked", n_checked);
  if (have_reach) {
    ImGui::SameLine();
    ImGui::Text("· %d available", n_reachable);
  }
  ImGui::SameLine();
  ImGui::Text("· %d total", n_total);
  if (n_total > 0) {
    ImGui::ProgressBar((float)n_checked / (float)n_total, ImVec2(-FLT_MIN, 0),
                       "");
  }
  if (!have_reach) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.7f, 0.2f, 1));
    ImGui::TextWrapped("Reachability unavailable for this slot (re-generate the "
                       "seed on this build to enable it). Showing checked / "
                       "unchecked only.");
    ImGui::PopStyleColor();
  }

  ImGui::Separator();
  ImGui::Checkbox("Hide checked", &s_hide_checked);
  ImGui::SameLine();
  if (have_reach) { ImGui::Checkbox("Only available", &s_only_reachable); ImGui::SameLine(); }
  ImGui::SetNextItemWidth(160);
  ImGui::InputTextWithHint("##search", "search", s_search, sizeof(s_search));
  if (!race) {
    ImGui::SameLine();
    ImGui::Checkbox("Show items (spoiler)", &s_show_items);
  } else {
    ImGui::SameLine();
    ImGui::TextDisabled("(spoiler hidden: race seed)");
  }

  ImGui::Separator();
  ImGui::BeginChild("##checklist", ImVec2(0, 0), false);

  // Status colors.
  const ImVec4 col_checked = ImVec4(0.45f, 0.75f, 0.45f, 1.0f);
  const ImVec4 col_reach   = ImVec4(0.95f, 0.85f, 0.35f, 1.0f);
  const ImVec4 col_unreach = ImVec4(0.55f, 0.55f, 0.58f, 1.0f);

  // Iterate regions in table order; an extra pass (region 0xFFFF) catches
  // location entries with no region binding.
  for (uint32 ri = 0; ri <= kRandoRegionsCount; ri++) {
    uint16 region_id = (ri < kRandoRegionsCount) ? kRandoRegions[ri].id : 0xFFFF;

    // Tally this region's locations from the placement table.
    int r_total = 0, r_checked = 0, r_avail = 0;
    for (int i = 0; i < n_total; i++) {
      uint16 loc = pt->entries[i].location_id;
      uint16 lr = (loc < 1024) ? s_loc_region[loc] : 0xFFFF;
      if (lr != region_id) continue;
      r_total++;
      if (Rando_IsLocationChecked(loc)) r_checked++;
      else if (have_reach && Reachability_HasLocation(reach, loc)) r_avail++;
    }
    if (r_total == 0) continue;

    const char *rname = (region_id == 0xFFFF) ? "(unbound)" : Rando_GetRegionName(region_id);
    char header[128];
    if (have_reach)
      snprintf(header, sizeof header, "%s — %d avail · %d/%d checked###reg%u",
               rname, r_avail, r_checked, r_total, region_id);
    else
      snprintf(header, sizeof header, "%s — %d/%d checked###reg%u",
               rname, r_checked, r_total, region_id);

    if (!ImGui::CollapsingHeader(header, ImGuiTreeNodeFlags_DefaultOpen)) continue;

    ImGui::Indent();
    for (int i = 0; i < n_total; i++) {
      uint16 loc = pt->entries[i].location_id;
      uint16 lr = (loc < 1024) ? s_loc_region[loc] : 0xFFFF;
      if (lr != region_id) continue;

      bool checked = Rando_IsLocationChecked(loc);
      bool reachable = have_reach && Reachability_HasLocation(reach, loc);
      int status = checked ? kCheck_Checked : (reachable ? kCheck_Reachable : kCheck_Unreachable);

      if (s_hide_checked && checked) continue;
      if (s_only_reachable && status == kCheck_Unreachable) continue;

      const char *lname = Rando_GetLocationName(loc);
      if (s_search[0] && lname) {
        // Case-insensitive substring filter.
        bool match = false;
        for (const char *p = lname; *p && !match; p++) {
          const char *a = p; const char *b = s_search; bool ok = true;
          while (*b) {
            char ca = *a, cb = *b;
            if (ca >= 'A' && ca <= 'Z') ca += 32;
            if (cb >= 'A' && cb <= 'Z') cb += 32;
            if (ca != cb) { ok = false; break; }
            a++; b++;
          }
          if (ok) match = true;
        }
        if (!match) continue;
      }

      ImVec4 c = checked ? col_checked : (reachable ? col_reach : col_unreach);
      const char *mark = checked ? "[x]" : (reachable ? "[ ]" : " - ");
      ImGui::PushStyleColor(ImGuiCol_Text, c);
      if (s_show_items && !race) {
        const char *iname = Rando_GetItemName(pt->entries[i].item_id);
        ImGui::Text("%s %s = %s", mark, lname ? lname : "(loc)", iname ? iname : "(item)");
      } else {
        ImGui::Text("%s %s", mark, lname ? lname : "(loc)");
      }
      ImGui::PopStyleColor();
    }
    ImGui::Unindent();
  }

  ImGui::EndChild();
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
