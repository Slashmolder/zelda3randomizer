// tracker_windows.cpp — Item / Check / Map tracker windows (PC only).
// See tracker_windows.h for the design contract. Phase 0 wires the windows onto
// the imgui_host with placeholder bodies; later phases fill in the live content.
#ifdef Z3R_NATIVE_SETTINGS_WINDOW

#include "imgui.h"

#include <SDL.h>
#include <SDL_opengl.h>
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
#include "../rando_map.h"      // RandoMap_Decode (overworld map background)
#include "../../hud.h"         // Hud_RandoBuildIconAtlas, kRandoIcon_* (item icons)
}

// ---- Minimal GL texture upload (resolve the few entry points via SDL, like
// imgui_host/rando_window — no GL loader dependency in an ImGui TU). ----------
typedef void(APIENTRY *PFN_glGenTextures)(GLsizei, GLuint *);
typedef void(APIENTRY *PFN_glBindTexture)(GLenum, GLuint);
typedef void(APIENTRY *PFN_glTexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei,
                                         GLint, GLenum, GLenum, const void *);
typedef void(APIENTRY *PFN_glTexParameteri)(GLenum, GLenum, GLint);
static PFN_glGenTextures p_glGenTextures;
static PFN_glBindTexture p_glBindTexture;
static PFN_glTexImage2D p_glTexImage2D;
static PFN_glTexParameteri p_glTexParameteri;

// Upload an RGBA8888 buffer as a GL texture in the CURRENT context; returns the
// texture id (0 on failure). Caller must have the target window's GL context
// current (true inside a draw callback under Z3RHost_RenderAll).
static ImTextureID UploadRgbaTexture(const unsigned char *rgba, int w, int h) {
  if (!p_glGenTextures) {
    p_glGenTextures = (PFN_glGenTextures)SDL_GL_GetProcAddress("glGenTextures");
    p_glBindTexture = (PFN_glBindTexture)SDL_GL_GetProcAddress("glBindTexture");
    p_glTexImage2D = (PFN_glTexImage2D)SDL_GL_GetProcAddress("glTexImage2D");
    p_glTexParameteri = (PFN_glTexParameteri)SDL_GL_GetProcAddress("glTexParameteri");
  }
  if (!p_glGenTextures || !p_glTexImage2D) return (ImTextureID)0;
  GLuint tex = 0;
  p_glGenTextures(1, &tex);
  p_glBindTexture(GL_TEXTURE_2D, tex);
  p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  p_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
  return (ImTextureID)(intptr_t)tex;
}

// ---- Item-icon atlas (real HUD icons) --------------------------------------
static ImTextureID s_icon_tex = (ImTextureID)0;
static bool s_icon_tried = false;

// Draw one item as a 34px icon (atlas slot), dimmed when not owned, with an
// optional small level/count overlay. Flows left-to-right and wraps. Falls back
// to nothing-but-spacing if the atlas texture isn't available.
static void IconChip(int slot, bool have, const char *overlay) {
  const float sz = 34.0f;
  ImGuiStyle &st = ImGui::GetStyle();
  float avail = ImGui::GetContentRegionAvail().x;
  if (ImGui::GetCursorPosX() > st.WindowPadding.x && avail < sz + st.ItemSpacing.x)
    ImGui::NewLine();
  ImVec2 p = ImGui::GetCursorScreenPos();
  const float W = (float)(kRandoIconCount * kRandoIconSize);
  if (s_icon_tex) {
    ImVec2 uv0(slot * (float)kRandoIconSize / W, 0.0f);
    ImVec2 uv1((slot + 1) * (float)kRandoIconSize / W, 1.0f);
    ImVec4 tint = have ? ImVec4(1, 1, 1, 1) : ImVec4(1, 1, 1, 0.16f);
    ImGui::Image(s_icon_tex, ImVec2(sz, sz), uv0, uv1, tint);
  } else {
    ImGui::Dummy(ImVec2(sz, sz));
  }
  if (overlay && overlay[0]) {
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2 ts = ImGui::CalcTextSize(overlay);
    ImVec2 tp(p.x + sz - ts.x - 1.0f, p.y + sz - ts.y);
    dl->AddText(ImVec2(tp.x + 1, tp.y + 1), IM_COL32(0, 0, 0, 230), overlay);
    dl->AddText(tp, IM_COL32(255, 235, 90, 255), overlay);
  }
  ImGui::SameLine();
}

// Draw a single atlas icon inline at `sz` px (no flow/overlay), dimmed when not
// owned. For table cells (dungeon items). Falls back to a dash if no atlas.
static void IconImage(int slot, float sz, bool have) {
  if (s_icon_tex) {
    const float W = (float)(kRandoIconCount * kRandoIconSize);
    ImVec2 uv0(slot * (float)kRandoIconSize / W, 0.0f);
    ImVec2 uv1((slot + 1) * (float)kRandoIconSize / W, 1.0f);
    ImVec4 tint = have ? ImVec4(1, 1, 1, 1) : ImVec4(1, 1, 1, 0.16f);
    ImGui::Image(s_icon_tex, ImVec2(sz, sz), uv0, uv1, tint);
  } else {
    ImGui::TextColored(have ? ImVec4(0.45f, 0.85f, 0.45f, 1) : ImVec4(0.45f, 0.45f, 0.48f, 1),
                       have ? "Y" : "-");
  }
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

  // Build the real item-icon atlas once, in this window's GL context.
  if (!s_icon_tried) {
    static uint32 atlas[kRandoIconCount * kRandoIconSize * kRandoIconSize];
    if (Hud_RandoBuildIconAtlas(atlas) > 0)
      s_icon_tex = UploadRgbaTexture((const unsigned char *)atlas,
                                     kRandoIconCount * kRandoIconSize, kRandoIconSize);
    s_icon_tried = true;
  }

  RandoItemView v;
  Rando_FillItemView(&v);
  static const char *kMagic[3] = { "1x", "1/2x", "1/4x" };
  char ov[12];

  ImGui::SeparatorText("Items");
  // Equipment (level/tier overlays).
  snprintf(ov, sizeof ov, "%d", v.sword);  IconChip(kRandoIcon_Sword, v.sword > 0, v.sword > 0 ? ov : nullptr);
  snprintf(ov, sizeof ov, "%d", v.shield); IconChip(kRandoIcon_Shield, v.shield > 0, v.shield > 0 ? ov : nullptr);
  IconChip(kRandoIcon_Armor, true, v.mail == 2 ? "R" : v.mail == 1 ? "B" : nullptr);
  snprintf(ov, sizeof ov, "%d", v.gloves); IconChip(kRandoIcon_Gloves, v.gloves > 0, v.gloves > 0 ? ov : nullptr);
  IconChip(kRandoIcon_Bow, v.bow > 0, v.bow == 2 ? "S" : v.bow == 1 ? "W" : nullptr);
  IconChip(kRandoIcon_Boomerang, v.boomerang > 0, v.boomerang == 2 ? "R" : v.boomerang == 1 ? "B" : nullptr);
  IconChip(kRandoIcon_Hookshot, v.hookshot, nullptr);
  IconChip(kRandoIcon_FireRod, v.firerod, nullptr);
  IconChip(kRandoIcon_IceRod, v.icerod, nullptr);
  IconChip(kRandoIcon_Hammer, v.hammer, nullptr);
  IconChip(kRandoIcon_Lamp, v.lamp, nullptr);
  IconChip(kRandoIcon_Net, v.net, nullptr);
  IconChip(kRandoIcon_Book, v.book, nullptr);
  IconChip(kRandoIcon_Somaria, v.somaria, nullptr);
  IconChip(kRandoIcon_Byrna, v.byrna, nullptr);
  IconChip(kRandoIcon_Cape, v.cape, nullptr);
  IconChip(kRandoIcon_Mirror, v.mirror, nullptr);
  IconChip(kRandoIcon_Boots, v.boots, nullptr);
  IconChip(kRandoIcon_Flippers, v.flippers, nullptr);
  IconChip(kRandoIcon_MoonPearl, v.moon_pearl, nullptr);
  IconChip(kRandoIcon_Bombos, v.bombos, nullptr);
  IconChip(kRandoIcon_Ether, v.ether, nullptr);
  IconChip(kRandoIcon_Quake, v.quake, nullptr);
  IconChip(kRandoIcon_Mushroom, v.mushroom || v.powder, v.powder ? "P" : nullptr);
  IconChip(kRandoIcon_Flute, v.flute, nullptr);
  IconChip(kRandoIcon_Shovel, v.shovel, nullptr);
  snprintf(ov, sizeof ov, "%d", v.bottles); IconChip(kRandoIcon_Bottle, v.bottles > 0, v.bottles > 0 ? ov : nullptr);
  ImGui::NewLine();
  // Magic is a consumption rate (no item sprite) — show as a chip.
  LevelChip("Magic", v.magic, kMagic[v.magic <= 2 ? v.magic : 0]);
  ImGui::NewLine();

  SectionHeader("Prizes");
  { char b[24]; snprintf(b, sizeof b, "Crystals %d/7", v.crystals); Chip(b, v.crystals ? (v.crystals == 7 ? 1 : 2) : 0, 110.0f); }
  { char b[24]; snprintf(b, sizeof b, "Pendants %d/3", v.pendants); Chip(b, v.pendants ? (v.pendants == 3 ? 1 : 2) : 0, 110.0f); }
  Chip("Agahnim", v.agahnim, 92.0f);

  SectionHeader("Stats");
  // Hearts: heart containers + pieces toward the next, clearly labelled.
  ImGui::AlignTextToFramePadding();
  IconImage(kRandoIcon_Heart, 22.0f, v.hearts > 0);
  ImGui::SameLine();
  if (v.heart_pieces > 0)
    ImGui::Text("%d hearts  (+%d/4 piece%s)", v.hearts, v.heart_pieces, v.heart_pieces == 1 ? "" : "s");
  else
    ImGui::Text("%d hearts", v.hearts);

  // Per-dungeon items: small-key count + big-key / map / compass indicators.
  ImGui::SeparatorText("Dungeon Items");
  static const struct { int idx; const char *name; } kDungeonRows[] = {
      {0, "Hyrule Castle"}, {4, "Castle Tower"}, {2, "Eastern"}, {3, "Desert"},
      {10, "Tower of Hera"}, {5, "Pal. of Darkness"}, {6, "Swamp"}, {7, "Skull Woods"},
      {8, "Thieves'"}, {9, "Ice"}, {11, "Misery Mire"}, {12, "Turtle Rock"},
      {13, "Ganon's Tower"},
  };
  if (ImGui::BeginTable("##dungeonitems", 5,
                        ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg)) {
    ImGui::TableSetupColumn("Dungeon");
    ImGui::TableSetupColumn("Keys");
    ImGui::TableSetupColumn("Big");
    ImGui::TableSetupColumn("Map");
    ImGui::TableSetupColumn("Cmp");
    ImGui::TableHeadersRow();
    const ImVec4 on = ImVec4(0.45f, 0.85f, 0.45f, 1.0f);
    const ImVec4 off = ImVec4(0.45f, 0.45f, 0.48f, 1.0f);
    for (int i = 0; i < (int)(sizeof(kDungeonRows) / sizeof(kDungeonRows[0])); i++) {
      int d = kDungeonRows[i].idx;
      uint16 bit = (uint16)(0x8000u >> d);
      int keys = v.dungeon_small_keys[d];
      ImGui::TableNextRow();
      ImGui::TableNextColumn(); ImGui::TextUnformatted(kDungeonRows[i].name);
      // Small keys: the game shows these as a count (no standalone HUD sprite).
      ImGui::TableNextColumn();
      ImGui::TextColored(keys > 0 ? on : off, "x%d", keys);
      // Big key / map / compass: the real dungeon-HUD sprites, dimmed when absent.
      ImGui::TableNextColumn(); IconImage(kRandoIcon_BigKey, 18.0f, (v.bigkey_bits & bit) != 0);
      ImGui::TableNextColumn(); IconImage(kRandoIcon_Map, 18.0f, (v.map_bits & bit) != 0);
      ImGui::TableNextColumn(); IconImage(kRandoIcon_Compass, 18.0f, (v.compass_bits & bit) != 0);
    }
    ImGui::EndTable();
  }

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
      if (s_search[0]) {
        if (!lname) continue;  // can't match a search term with no name
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

// Aggregate a region's placement checks. Outputs total/checked/available.
static void RegionTally(const RandoPlacementTable *pt, const RandoReachability *reach,
                        bool have_reach, uint16 region_id,
                        int *total, int *checked, int *avail) {
  int t = 0, c = 0, a = 0;
  int n = pt ? (int)pt->count : 0;
  for (int i = 0; i < n; i++) {
    uint16 loc = pt->entries[i].location_id;
    uint16 lr = (loc < 1024) ? s_loc_region[loc] : 0xFFFF;
    if (lr != region_id) continue;
    t++;
    if (Rando_IsLocationChecked(loc)) c++;
    else if (have_reach && Reachability_HasLocation(reach, loc)) a++;
  }
  *total = t; *checked = c; *avail = a;
}

// Aggregate status: -1 empty, 2 all-checked, 1 some-available, 0 locked.
static int RegionStatus(int total, int checked, int avail) {
  if (total == 0) return -1;
  if (checked == total) return kCheck_Checked;
  if (avail > 0) return kCheck_Reachable;
  return kCheck_Unreachable;
}

static const ImU32 kColChecked = IM_COL32(115, 191, 115, 255);
static const ImU32 kColReach   = IM_COL32(242, 217, 89, 255);
static const ImU32 kColLocked  = IM_COL32(120, 120, 128, 255);

// Region pins positioned (normalized 0..1) over the decoded overworld maps. The
// dark world shares the light world's geography, so the dark coords mirror it.
struct WorldPin { uint16 region_id; float x, y; };
static const WorldPin kLightPins[] = {
  {16, 0.30f, 0.16f},  // LightWorld_DeathMountain_West
  {15, 0.52f, 0.14f},  // LightWorld_DeathMountain_East
  {18, 0.16f, 0.50f},  // LightWorld_NorthWest (Kakariko)
  {17, 0.64f, 0.44f},  // LightWorld_NorthEast
  {19, 0.45f, 0.82f},  // LightWorld_South
  {20, 0.50f, 0.66f},  // LinksHouse
};
static const WorldPin kDarkPins[] = {
  {1, 0.30f, 0.16f},   // DarkWorld_DeathMountain_West
  {0, 0.52f, 0.14f},   // DarkWorld_DeathMountain_East
  {29, 0.62f, 0.13f},  // TurtleRock_Entrance (DM east)
  {4, 0.16f, 0.50f},   // DarkWorld_NorthWest (Village of Outcasts)
  {3, 0.64f, 0.44f},   // DarkWorld_NorthEast
  {5, 0.45f, 0.80f},   // DarkWorld_South
  {2, 0.20f, 0.84f},   // DarkWorld_Mire
  {22, 0.13f, 0.86f},  // MiseryMire_Entrance
  {21, 0.50f, 0.66f},  // LinksHouse_Inverted
};
static bool IsOverworldPin(uint16 rid) {
  for (int i = 0; i < (int)(sizeof(kLightPins) / sizeof(kLightPins[0])); i++)
    if (kLightPins[i].region_id == rid) return true;
  for (int i = 0; i < (int)(sizeof(kDarkPins) / sizeof(kDarkPins[0])); i++)
    if (kDarkPins[i].region_id == rid) return true;
  return false;
}

// Decoded map textures (created once, in the map window's GL context).
static ImTextureID s_map_tex[2] = {(ImTextureID)0, (ImTextureID)0};  // [0]=light,[1]=dark
static bool s_map_tried = false;

static void DrawMapTracker(void *) {
  BeginFullWindow("Map Tracker##z3r");

  if (!Rando_IsActive()) {
    ImGui::TextDisabled("No randomizer slot active.");
    ImGui::End();
    return;
  }
  if (!s_loc_region_built) BuildLocRegionIndex();

  const RandoPlacementTable *pt = Placement_GetActive();
  const RandoReachability *reach = Rando_GetLiveReachability();
  bool have_reach = (reach != NULL);

  // Decode + upload both overworld maps once (static assets; this window's GL
  // context is current inside the draw callback).
  if (!s_map_tried) {
    static unsigned char buf[kRandoMapPixels * kRandoMapPixels * 4];
    if (RandoMap_Decode(false, buf))
      s_map_tex[0] = UploadRgbaTexture(buf, kRandoMapPixels, kRandoMapPixels);
    if (RandoMap_Decode(true, buf))
      s_map_tex[1] = UploadRgbaTexture(buf, kRandoMapPixels, kRandoMapPixels);
    s_map_tried = true;
  }

  static int s_world = 0;  // 0=light, 1=dark
  ImGui::RadioButton("Light World", &s_world, 0);
  ImGui::SameLine();
  ImGui::RadioButton("Dark World", &s_world, 1);
  ImGui::SameLine();
  ImGui::TextDisabled("(green: checked · yellow: available · grey: locked)");

  const WorldPin *pins = s_world ? kDarkPins : kLightPins;
  int npins = s_world ? (int)(sizeof(kDarkPins) / sizeof(kDarkPins[0]))
                      : (int)(sizeof(kLightPins) / sizeof(kLightPins[0]));

  ImVec2 origin = ImGui::GetCursorScreenPos();
  float side = ImGui::GetContentRegionAvail().x;
  if (side > 480.0f) side = 480.0f;
  ImDrawList *dl = ImGui::GetWindowDrawList();
  ImVec2 br = ImVec2(origin.x + side, origin.y + side);
  if (s_map_tex[s_world])
    dl->AddImage(s_map_tex[s_world], origin, br);
  else
    dl->AddRectFilled(origin, br, IM_COL32(28, 30, 38, 255), 4.0f);

  ImVec2 mouse = ImGui::GetMousePos();
  int hover_region = -1;
  for (int i = 0; i < npins; i++) {
    uint16 rid = pins[i].region_id;
    int total, checked, avail;
    RegionTally(pt, reach, have_reach, rid, &total, &checked, &avail);
    if (total == 0) continue;
    int st = RegionStatus(total, checked, avail);
    ImU32 col = (st == kCheck_Checked) ? kColChecked
                : (st == kCheck_Reachable) ? kColReach : kColLocked;
    ImVec2 c = ImVec2(origin.x + pins[i].x * side, origin.y + pins[i].y * side);
    float radius = 9.0f;
    dl->AddCircleFilled(c, radius, col);
    dl->AddCircle(c, radius, IM_COL32(10, 10, 12, 255), 0, 2.0f);
    char lbl[24];
    if (have_reach) snprintf(lbl, sizeof lbl, "%d", avail);
    else snprintf(lbl, sizeof lbl, "%d", total - checked);
    ImVec2 ts = ImGui::CalcTextSize(lbl);
    dl->AddText(ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), IM_COL32(12, 12, 14, 255), lbl);
    float dx = mouse.x - c.x, dy = mouse.y - c.y;
    if (dx * dx + dy * dy <= radius * radius) hover_region = rid;
  }
  ImGui::Dummy(ImVec2(side, side));

  if (hover_region >= 0) {
    ImGui::BeginTooltip();
    ImGui::TextUnformatted(Rando_GetRegionName((uint16)hover_region));
    ImGui::Separator();
    int n = pt ? (int)pt->count : 0;
    for (int i = 0; i < n; i++) {
      uint16 loc = pt->entries[i].location_id;
      uint16 lr = (loc < 1024) ? s_loc_region[loc] : 0xFFFF;
      if (lr != (uint16)hover_region) continue;
      bool checked = Rando_IsLocationChecked(loc);
      bool reachable = have_reach && Reachability_HasLocation(reach, loc);
      ImVec4 cc = checked ? ImVec4(0.45f, 0.75f, 0.45f, 1)
                  : reachable ? ImVec4(0.95f, 0.85f, 0.35f, 1)
                              : ImVec4(0.55f, 0.55f, 0.58f, 1);
      ImGui::TextColored(cc, "%s %s", checked ? "[x]" : (reachable ? "[ ]" : " - "),
                         Rando_GetLocationName(loc));
    }
    ImGui::EndTooltip();
  }

  // ---- Dungeon regions (interiors have no overworld pin) ----
  ImGui::Spacing();
  ImGui::SeparatorText("Dungeons");
  ImGui::BeginChild("##dungeons", ImVec2(0, 0), false);
  for (uint32 ri = 0; ri < kRandoRegionsCount; ri++) {
    uint16 rid = kRandoRegions[ri].id;
    if (IsOverworldPin(rid)) continue;  // shown as a pin on a map above
    int total, checked, avail;
    RegionTally(pt, reach, have_reach, rid, &total, &checked, &avail);
    if (total == 0) continue;
    int st = RegionStatus(total, checked, avail);
    ImVec4 c = (st == kCheck_Checked) ? ImVec4(0.45f, 0.75f, 0.45f, 1)
               : (st == kCheck_Reachable) ? ImVec4(0.95f, 0.85f, 0.35f, 1)
                                          : ImVec4(0.55f, 0.55f, 0.58f, 1);
    ImGui::PushStyleColor(ImGuiCol_Text, c);
    if (have_reach)
      ImGui::Text("%s — %d avail · %d/%d", Rando_GetRegionName(rid), avail, checked, total);
    else
      ImGui::Text("%s — %d/%d", Rando_GetRegionName(rid), checked, total);
    ImGui::PopStyleColor();
  }
  ImGui::EndChild();
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
