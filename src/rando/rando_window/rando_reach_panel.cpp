// rando_reach_panel.cpp — Randomizer > Reachability panel: read-only view of which
// checks are reachable given the live inventory (Rando_GetLiveReachability +
// Placement_GetActive). PC only.
//
// This mirrors the Check Tracker (tracker_windows.cpp DrawCheckTracker): same
// location->region index, same per-region grouping, same reachability source. It
// is intentionally READ-ONLY — it never writes g_ram. Reachability is recomputed
// live: Rando_GetLiveReachability() is memoized against the current inventory, so
// the view auto-refreshes as items are collected (no manual refresh button).
#ifdef Z3R_NATIVE_SETTINGS_WINDOW

#include "imgui.h"
#include "game_panels.h"

#include <cstdio>   // snprintf
#include <cfloat>   // FLT_MIN (ImGui full-width sentinel)

// Game-side C APIs. rando.h is plain C (its lone _Static_assert is now
// C++-guarded); wrap in extern "C" so the C++ linker resolves the C symbols.
// (Mirrors tracker_windows.cpp's include block precisely to avoid the rando.h
// C++-include pitfall.)
extern "C" {
#include "../rando.h"          // Rando_IsActive, Rando_GetLiveReachability, Rando_GetActiveSettings, Rando_GetItemName, Rando_IsLocationChecked
#include "../rando_logic.h"    // kRandoLocations/Regions, Rando_Get*Name, Reachability_HasLocation, RandoLocationDef
#include "../rando_placement.h"// Placement_GetActive, RandoPlacementTable, RandoPlacement
}

// location_id -> region_id index, built once from the static logic table.
// Identical construction to tracker_windows.cpp::BuildLocRegionIndex so the two
// views group checks the same way.
static uint16 s_loc_region[1024];
static uint8 s_loc_type[1024];
static bool s_loc_region_built = false;
static void BuildLocRegionIndex() {
  for (int i = 0; i < 1024; i++) { s_loc_region[i] = 0xFFFF; s_loc_type[i] = 0xFF; }
  for (uint32 i = 0; i < kRandoLocationsCount; i++) {
    uint16 id = kRandoLocations[i].id;
    if (id < 1024) {
      s_loc_region[id] = kRandoLocations[i].region_id;
      s_loc_type[id]   = kRandoLocations[i].type;
    }
  }
  s_loc_region_built = true;
}

// Medallion-type (13) locations are the MM/TR medallion CONFIG slots (which
// medallion opens the dungeon) — a generation-time setting, not an item check.
// They carry a default-TRUE predicate + 0xFFFF region, so they'd otherwise show
// as permanently "reachable" under "(unbound)" and inflate the totals. Mirror
// the Check Tracker's LocHiddenFromChecks so the two views agree. (audit T2)
static const uint8 kLocType_Medallion = 13;
static inline bool LocHiddenFromChecks(uint16 loc) {
  return loc < 1024 && s_loc_type[loc] == kLocType_Medallion;
}

extern "C" void RandoReach_Render(void) {
  if (!Rando_IsActive()) {
    ImGui::TextDisabled("Start or load a randomizer slot to view reachability.");
    return;
  }
  if (!s_loc_region_built) BuildLocRegionIndex();

  const RandoPlacementTable *pt = Placement_GetActive();
  const RandoReachability *reach = Rando_GetLiveReachability();  // NULL => unavailable
  bool have_reach = (reach != NULL);

  // Race seeds must never reveal placed item names. Fails closed when the
  // active slot's settings are unknown (snapshot replay / v1 slot).
  bool race = Rando_ActiveSlotHidesSpoiler();

  int n_total = pt ? (int)pt->count : 0;

  // Summary: reachable (or checked) over total. A check counts toward "reachable"
  // if it is already checked OR currently reachable from the live inventory.
  int n_reachable = 0, n_visible = 0;
  for (int i = 0; i < n_total; i++) {
    uint16 loc = pt->entries[i].location_id;
    if (LocHiddenFromChecks(loc)) continue;  // exclude medallion-config slots (audit T2)
    n_visible++;
    if (Rando_IsLocationChecked(loc)) n_reachable++;
    else if (have_reach && Reachability_HasLocation(reach, loc)) n_reachable++;
  }
  ImGui::Text("Reachable: %d / %d", n_reachable, n_visible);
  if (n_visible > 0)
    ImGui::ProgressBar((float)n_reachable / (float)n_visible, ImVec2(-FLT_MIN, 0), "");

  ImGui::TextDisabled("Reachability is recomputed live from the current inventory.");

  if (!have_reach) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.7f, 0.2f, 1));
    ImGui::TextWrapped("Reachability unavailable for this slot (re-generate the seed "
                       "on this build to enable it). Showing checks without a "
                       "reachable state.");
    ImGui::PopStyleColor();
  }
  if (race) {
    ImGui::SameLine();
    ImGui::TextDisabled("(item names hidden: race seed)");
  }

  ImGui::Separator();

  // Status colors (match the Check Tracker palette).
  const ImVec4 col_checked = ImVec4(0.45f, 0.75f, 0.45f, 1.0f);  // green
  const ImVec4 col_reach   = ImVec4(0.95f, 0.85f, 0.35f, 1.0f);  // yellow
  const ImVec4 col_locked  = ImVec4(0.55f, 0.55f, 0.58f, 1.0f);  // dim grey

  // Iterate regions in table order; a trailing pass (region 0xFFFF) catches
  // location entries with no region binding.
  for (uint32 ri = 0; ri <= kRandoRegionsCount; ri++) {
    uint16 region_id = (ri < kRandoRegionsCount) ? kRandoRegions[ri].id : 0xFFFF;

    // Tally this region's locations from the placement table.
    int r_total = 0, r_reach = 0;
    for (int i = 0; i < n_total; i++) {
      uint16 loc = pt->entries[i].location_id;
      if (LocHiddenFromChecks(loc)) continue;  // exclude medallion-config slots (audit T2)
      uint16 lr = (loc < 1024) ? s_loc_region[loc] : 0xFFFF;
      if (lr != region_id) continue;
      r_total++;
      if (Rando_IsLocationChecked(loc)) r_reach++;
      else if (have_reach && Reachability_HasLocation(reach, loc)) r_reach++;
    }
    if (r_total == 0) continue;

    const char *rname = (region_id == 0xFFFF) ? "(unbound)" : Rando_GetRegionName(region_id);
    char header[128];
    snprintf(header, sizeof header, "%s — %d/%d reachable###reg%u",
             rname ? rname : "(region)", r_reach, r_total, region_id);
    if (!ImGui::CollapsingHeader(header, ImGuiTreeNodeFlags_DefaultOpen)) continue;

    // Three columns when item names are shown (non-race); two otherwise.
    int n_cols = race ? 2 : 3;
    char tid[24];
    snprintf(tid, sizeof tid, "##reachtbl%u", region_id);
    if (!ImGui::BeginTable(tid, n_cols,
                           ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                           ImGuiTableFlags_SizingStretchProp))
      continue;
    ImGui::TableSetupColumn("Location");
    ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 90.0f);
    if (!race) ImGui::TableSetupColumn("Item");
    ImGui::TableHeadersRow();

    for (int i = 0; i < n_total; i++) {
      uint16 loc = pt->entries[i].location_id;
      if (LocHiddenFromChecks(loc)) continue;  // exclude medallion-config slots (audit T2)
      uint16 lr = (loc < 1024) ? s_loc_region[loc] : 0xFFFF;
      if (lr != region_id) continue;

      bool checked = Rando_IsLocationChecked(loc);
      bool reachable = have_reach && Reachability_HasLocation(reach, loc);

      ImGui::TableNextRow();

      // Location name.
      ImGui::TableNextColumn();
      const char *lname = Rando_GetLocationName(loc);
      ImGui::TextUnformatted(lname ? lname : "(loc)");

      // Reachable state. Without live reachability we can only show checked vs.
      // an undetermined "—".
      ImGui::TableNextColumn();
      if (checked) {
        ImGui::TextColored(col_checked, "checked");
      } else if (!have_reach) {
        ImGui::TextColored(col_locked, "—");
      } else if (reachable) {
        ImGui::TextColored(col_reach, "reachable");
      } else {
        ImGui::TextColored(col_locked, "locked");
      }

      // Item (spoiler) — never for race seeds.
      if (!race) {
        ImGui::TableNextColumn();
        const char *iname = Rando_GetItemName(pt->entries[i].item_id);
        ImGui::TextUnformatted(iname ? iname : "(item)");
      }
    }

    ImGui::EndTable();
  }
}

#endif  // Z3R_NATIVE_SETTINGS_WINDOW
