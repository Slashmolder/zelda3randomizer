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

// Draw a centered number/char overlay on the icon just drawn at screen pos `p`
// (square `sz`). White when lit, grey when dim.
static void OverlayCentered(ImVec2 p, float sz, const char *txt, bool lit) {
  ImDrawList *dl = ImGui::GetWindowDrawList();
  ImVec2 ts = ImGui::CalcTextSize(txt);
  ImVec2 tp(p.x + (sz - ts.x) * 0.5f, p.y + (sz - ts.y) * 0.5f);
  dl->AddText(ImVec2(tp.x + 1, tp.y + 1), IM_COL32(0, 0, 0, 220), txt);
  dl->AddText(tp, lit ? IM_COL32(255, 255, 255, 255) : IM_COL32(150, 150, 155, 160), txt);
}

// Draw a dim rounded "?" placeholder cell (sz square): the prize a dungeon
// awards is hidden under prize_shuffle until obtained, so we must not reveal it.
static void UnknownPrizeCell(float sz) {
  ImVec2 p = ImGui::GetCursorScreenPos();
  ImDrawList *dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(p, ImVec2(p.x + sz, p.y + sz), IM_COL32(58, 58, 70, 130), 3.0f);
  dl->AddRect(p, ImVec2(p.x + sz, p.y + sz), IM_COL32(110, 110, 125, 180), 3.0f);
  OverlayCentered(p, sz, "?", false);
  ImGui::Dummy(ImVec2(sz, sz));
}

// Map a prize id (0..9, rando_shuffles.c kPrize_*) to its atlas icon, the
// obtained flag (Rando_FillItemView masks), and crystal number (0 = pendant).
static int PrizeIcon(const RandoItemView &v, uint8 prize, bool *obtained, int *crystal_num) {
  *crystal_num = 0;
  switch (prize) {
    case 0: *obtained = (v.pendant_mask & 1) != 0; return kRandoIcon_PendantGreen;  // green
    case 1: *obtained = (v.pendant_mask & 4) != 0; return kRandoIcon_PendantRed;    // red
    case 2: *obtained = (v.pendant_mask & 2) != 0; return kRandoIcon_PendantBlue;   // blue
    default:
      if (prize >= 3 && prize <= 9) {           // Crystal1..7
        *crystal_num = prize - 2;
        *obtained = (v.crystal_mask & (1 << (prize - 3))) != 0;
        return kRandoIcon_Crystal;
      }
      *obtained = false;
      return -1;
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
  // Tunic / gloves / boomerang: pick the coloured-variant atlas slot so the
  // sprite matches the held tier (blue/red mail, Titan's Mitt, magic boomerang).
  int mail_slot = v.mail == 2 ? kRandoIcon_ArmorRed
                : v.mail == 1 ? kRandoIcon_ArmorBlue : kRandoIcon_Armor;
  IconChip(mail_slot, true, nullptr);
  int glove_slot = v.gloves == 2 ? kRandoIcon_GlovesTitan : kRandoIcon_Gloves;
  IconChip(glove_slot, v.gloves > 0, nullptr);
  IconChip(kRandoIcon_Bow, v.bow > 0, v.bow == 2 ? "S" : v.bow == 1 ? "W" : nullptr);
  int boom_slot = v.boomerang == 2 ? kRandoIcon_BoomerangRed : kRandoIcon_Boomerang;
  IconChip(boom_slot, v.boomerang > 0, nullptr);
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

  SectionHeader("Stats");
  // Hearts: heart containers + pieces toward the next, clearly labelled.
  ImGui::AlignTextToFramePadding();
  IconImage(kRandoIcon_Heart, 22.0f, v.hearts > 0);
  ImGui::SameLine();
  if (v.heart_pieces > 0)
    ImGui::Text("%d hearts  (+%d/4 piece%s)", v.hearts, v.heart_pieces, v.heart_pieces == 1 ? "" : "s");
  else
    ImGui::Text("%d hearts", v.hearts);

  // Per-dungeon: prize (= boss-completion), small-key count, big-key/map/compass.
  // `game` indexes the RandoItemView dungeon arrays; `logic` indexes the prize
  // assignment (rando_logic dungeon ids). `prize` marks the 10 dungeons that
  // award a pendant/crystal (HC/CT/GT do not).
  ImGui::SeparatorText("Dungeons");
  // Dungeon rows come from the canonical kRandoTrackerDungeons table (rando.h) —
  // the single source of truth for each dungeon's game-side index (which drives
  // both the big-key/map/compass bit `0x8000 >> game_index` and the small-key
  // slot) and its prize-assignment index. Centralizing the mapping in one place
  // that Rando_TrackerSelfCheck validates against the game's dispatch tables
  // keeps the bit from silently drifting off the RAM storage convention (a prior
  // Hyrule Castle game_index=1 bug blanked its map/big-key/compass columns).
  const RandoTrackerDungeonInfo *kDungeonRows = kRandoTrackerDungeons;
  const int kDungeonRowCount = kRandoTrackerDungeonCount;
  // Prize assignment + shuffle flag drive the spoiler-safe prize column: an
  // unobtained shuffled prize shows "?" (revealing it would spoil); an obtained
  // one (or any prize when shuffle is off) shows the real icon.
  const uint8 *prize_assign = Rando_GetDungeonPrizeAssignment();  // NULL if no rando
  const RandoSettings *rset = Rando_GetActiveSettings();
  bool shuffle_on = rset && rset->prize_shuffle;
  if (ImGui::BeginTable("##dungeons", 6,
                        ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg)) {
    ImGui::TableSetupColumn("Dungeon");
    ImGui::TableSetupColumn("Prize");
    ImGui::TableSetupColumn("Keys");
    ImGui::TableSetupColumn("Big");
    ImGui::TableSetupColumn("Map");
    ImGui::TableSetupColumn("Cmp");
    ImGui::TableHeadersRow();
    const ImVec4 on = ImVec4(0.45f, 0.85f, 0.45f, 1.0f);
    const ImVec4 off = ImVec4(0.45f, 0.45f, 0.48f, 1.0f);
    const ImVec4 done = ImVec4(0.45f, 0.90f, 0.50f, 1.0f);  // completed dungeon name
    for (int i = 0; i < kDungeonRowCount; i++) {
      int d = kDungeonRows[i].game_index;
      uint16 bit = (uint16)(0x8000u >> d);
      int keys = v.dungeon_small_keys[d];  // small-key slot == game_index

      // Completion: prize obtained for prize dungeons; Agahnim for Castle Tower.
      bool prize_obtained = false; int prize_icon = -1, crystal_num = 0;
      if (kDungeonRows[i].has_prize && prize_assign)
        prize_icon = PrizeIcon(v, prize_assign[kDungeonRows[i].prize_logic], &prize_obtained, &crystal_num);
      bool complete = kDungeonRows[i].game_index == 4 ? v.agahnim : prize_obtained;

      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextColored(complete ? done : ImGui::GetStyleColorVec4(ImGuiCol_Text),
                         "%s", kDungeonRows[i].name);

      // Prize column (doubles as boss-completion indicator).
      ImGui::TableNextColumn();
      if (kDungeonRows[i].game_index == 4) {      // Castle Tower -> Agahnim 1
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList *dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + 18, p.y + 18),
                          v.agahnim ? IM_COL32(90, 40, 90, 200) : IM_COL32(50, 50, 60, 120), 3.0f);
        OverlayCentered(p, 18.0f, "A", v.agahnim);
        ImGui::Dummy(ImVec2(18, 18));
      } else if (prize_icon >= 0 && (prize_obtained || !shuffle_on)) {
        ImVec2 p = ImGui::GetCursorScreenPos();
        IconImage(prize_icon, 18.0f, prize_obtained);
        if (crystal_num) { char nb[4]; snprintf(nb, sizeof nb, "%d", crystal_num);
                           OverlayCentered(p, 18.0f, nb, prize_obtained); }
      } else if (kDungeonRows[i].has_prize && prize_assign) {
        UnknownPrizeCell(18.0f);                  // shuffled + not yet obtained
      } else {
        ImGui::Dummy(ImVec2(18, 18));             // HC / GT / no rando: no prize
      }

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
  {16, 0.50f, 0.35f},  // LightWorld_DeathMountain_West
  {15, 0.72f, 0.27f},  // LightWorld_DeathMountain_East
  {18, 0.30f, 0.50f},  // LightWorld_NorthWest (Kakariko)
  {17, 0.70f, 0.40f},  // LightWorld_NorthEast
  {19, 0.50f, 0.70f},  // LightWorld_South
  {20, 0.52f, 0.60f},  // LinksHouse
};
static const WorldPin kDarkPins[] = {
  {1, 0.50f, 0.35f},   // DarkWorld_DeathMountain_West
  {0, 0.72f, 0.27f},   // DarkWorld_DeathMountain_East
  {29, 0.82f, 0.20f},  // TurtleRock_Entrance (DM east) — ESTIMATED
  {4, 0.30f, 0.50f},   // DarkWorld_NorthWest (Village of Outcasts)
  {3, 0.70f, 0.40f},   // DarkWorld_NorthEast
  {5, 0.50f, 0.70f},   // DarkWorld_South
  {2, 0.22f, 0.82f},   // DarkWorld_Mire — ESTIMATED
  {22, 0.14f, 0.87f},  // MiseryMire_Entrance — ESTIMATED
  {21, 0.52f, 0.60f},  // LinksHouse_Inverted
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
  // Pin-tuning aids. Pin coords (kLightPins/kDarkPins) are ABSOLUTE texture
  // coords (0..1 over the full 512px map). Zoom only crops the view (the
  // playable landmass is the central ~70%; the rest is cloud border that the
  // game crops too), so the cursor readout reports stable absolute coords at
  // any zoom — hover where a pin should sit, read x/y, and that's the value.
  static bool s_show_grid = false;
  static float s_zoom = 1.50f;
  ImGui::Checkbox("Grid", &s_show_grid);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(120);
  ImGui::SliderFloat("Zoom", &s_zoom, 1.0f, 3.0f, "%.2fx");
  ImGui::SameLine();
  ImGui::TextDisabled("(green ok · yellow avail · grey locked)");

  const WorldPin *pins = s_world ? kDarkPins : kLightPins;
  int npins = s_world ? (int)(sizeof(kDarkPins) / sizeof(kDarkPins[0]))
                      : (int)(sizeof(kLightPins) / sizeof(kLightPins[0]));

  ImVec2 origin = ImGui::GetCursorScreenPos();
  float side = ImGui::GetContentRegionAvail().x;
  if (side > 480.0f) side = 480.0f;
  ImDrawList *dl = ImGui::GetWindowDrawList();
  ImVec2 br = ImVec2(origin.x + side, origin.y + side);

  // Crop (UV) window centered on the measured landmass center; zoom shrinks the
  // visible span. Clamp so the window stays inside the [0,1] texture.
  const float kMapCx = 0.523f, kMapCy = 0.523f;
  float half = 0.5f / s_zoom;
  float u0 = kMapCx - half, u1 = kMapCx + half;
  float v0 = kMapCy - half, v1 = kMapCy + half;
  if (u0 < 0) { u1 -= u0; u0 = 0; } if (u1 > 1) { u0 -= (u1 - 1); u1 = 1; if (u0 < 0) u0 = 0; }
  if (v0 < 0) { v1 -= v0; v0 = 0; } if (v1 > 1) { v0 -= (v1 - 1); v1 = 1; if (v0 < 0) v0 = 0; }
  float uspan = (u1 - u0) > 1e-4f ? (u1 - u0) : 1e-4f;
  float vspan = (v1 - v0) > 1e-4f ? (v1 - v0) : 1e-4f;
  auto tex2sx = [&](float tx) { return origin.x + (tx - u0) / uspan * side; };
  auto tex2sy = [&](float ty) { return origin.y + (ty - v0) / vspan * side; };

  if (s_map_tex[s_world])
    dl->AddImage(s_map_tex[s_world], origin, br, ImVec2(u0, v0), ImVec2(u1, v1));
  else
    dl->AddRectFilled(origin, br, IM_COL32(28, 30, 38, 255), 4.0f);

  // Grid: absolute 0.1 marks (brighter every 0.5), projected through the crop;
  // marks scrolled outside the zoomed view are skipped.
  if (s_show_grid) {
    for (int i = 0; i <= 10; i++) {
      float f = (float)i / 10.0f;
      ImU32 c = (i % 5 == 0) ? IM_COL32(255, 255, 255, 130) : IM_COL32(255, 255, 255, 45);
      char lbl[8]; snprintf(lbl, sizeof lbl, "%.1f", f);
      float sx = tex2sx(f), sy = tex2sy(f);
      if (sx >= origin.x && sx <= origin.x + side) {
        dl->AddLine(ImVec2(sx, origin.y), ImVec2(sx, origin.y + side), c);
        dl->AddText(ImVec2(sx + 1, origin.y + 1), IM_COL32(255, 235, 90, 220), lbl);
      }
      if (sy >= origin.y && sy <= origin.y + side) {
        dl->AddLine(ImVec2(origin.x, sy), ImVec2(origin.x + side, sy), c);
        dl->AddText(ImVec2(origin.x + 1, sy + 1), IM_COL32(255, 235, 90, 220), lbl);
      }
    }
  }

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
    ImVec2 c = ImVec2(tex2sx(pins[i].x), tex2sy(pins[i].y));
    if (c.x < origin.x || c.x > origin.x + side || c.y < origin.y || c.y > origin.y + side)
      continue;  // pin scrolled outside the zoomed view
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

  // Live cursor readout (tuning mode): absolute map coords drawn next to the
  // cursor, with a crosshair, so a pin position can be read directly.
  if (s_show_grid && mouse.x >= origin.x && mouse.x <= origin.x + side &&
      mouse.y >= origin.y && mouse.y <= origin.y + side) {
    float tx = u0 + (mouse.x - origin.x) / side * uspan;
    float ty = v0 + (mouse.y - origin.y) / side * vspan;
    char buf[40]; snprintf(buf, sizeof buf, "x %.3f  y %.3f", tx, ty);
    ImVec2 ts = ImGui::CalcTextSize(buf);
    ImVec2 tp(mouse.x + 14, mouse.y + 6);
    if (tp.x + ts.x > origin.x + side) tp.x = mouse.x - 14 - ts.x;
    dl->AddRectFilled(ImVec2(tp.x - 3, tp.y - 2), ImVec2(tp.x + ts.x + 3, tp.y + ts.y + 2),
                      IM_COL32(0, 0, 0, 210), 3.0f);
    dl->AddText(tp, IM_COL32(120, 255, 120, 255), buf);
    dl->AddLine(ImVec2(mouse.x - 7, mouse.y), ImVec2(mouse.x + 7, mouse.y), IM_COL32(120, 255, 120, 190));
    dl->AddLine(ImVec2(mouse.x, mouse.y - 7), ImVec2(mouse.x, mouse.y + 7), IM_COL32(120, 255, 120, 190));
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
