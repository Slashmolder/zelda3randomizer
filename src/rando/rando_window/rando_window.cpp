// rando_window.cpp — Dear ImGui native settings window (PC only).
//
// GL-CONTEXT DISCIPLINE (fully implemented in P2; do NOT remove the bind call):
//   Each frame, before ANY ImGui call (ImGui_ImplOpenGL3_NewFrame / NewFrame /
//   Render / RenderDrawData / SDL_GL_SwapWindow(settings_window)), call
//   SDL_GL_MakeCurrent(settings_window, settings_gl_context). After the swap, if
//   the GAME window uses the OpenGL renderer, restore with
//   SDL_GL_MakeCurrent(game_window, game_gl_context). The settings GL context is
//   ALWAYS created against the settings window, never the game window (the default
//   game renderer is SDL software with no GL context). The host (main.c) owns the
//   save/restore of the game's current context around RandoWindow_BeginFrame /
//   RandoWindow_Render; this TU only ever makes the SETTINGS context current.
//
// LOADER ISOLATION: the ImGui OpenGL3 backend uses its OWN embedded loader
// (imgui_impl_opengl3_loader.h). This TU must NEVER include third_party/gl_core
// (the game's loader lives only in src/opengl.c); keeping the two in disjoint TUs
// prevents symbol collisions. <SDL_opengl.h> only declares the gl* entry points
// (glViewport/glClear/...) — it pulls in no loader, so it is safe here.
//
// PARITY (P4): the panels present EXACTLY the in-game settings screen's axis set
// (select_file.c kRow_*), with the SAME canonical labels (RowValueText / the CLI
// grammar in rando_settings.c). No new RNG / hash / serializer / share codec is
// introduced — everything routes through Settings_* / Share_*. The only new
// arithmetic is SplitMix64, used purely to pick a seed INPUT.
#ifdef Z3R_NATIVE_SETTINGS_WINDOW

#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_opengl3.h"

#include <SDL.h>
#include <SDL_opengl.h>

#include <cstdio>   // snprintf
#include <cstring>  // memcmp

#include "rando_window.h"
#include "rando_window_bridge.h"
// kFeatures0_* recommended-features bit constants (compile-time enums; no g_ram
// access — this TU never invokes the enhanced_features0 macro that writes g_ram).
#include "../../features.h"

// Game-side C APIs. rando_window_bridge.h already pulls rando_settings.h /
// rando_share.h / rando_placement.h in under extern "C"; the rest are added
// here (also under extern "C" so the C++ linker resolves the C symbols).
extern "C" {
#include "../rando_asset_decisions.h"  // AssetDecision_FindAllow / _Persist
#include "../rando_logic.h"      // Rando_GetRegionName/LocationName/ItemName, kRandoLocations
#include "../vanilla_assets_hash.h"  // kVanillaAssetsHash, kVanillaAssetsHashKnown
#include "../../config.h"        // g_config (R2: snapshot features0 at open), g_rando_window_prefs
// g_assets_hash is declared in rando.h, but that header uses C11 _Static_assert
// (not valid in this C++ TU), so declare just the symbol we need here.
extern uint8 g_assets_hash[32];
}

// ---- File-static state -----------------------------------------------------
static SDL_Window *s_settings_window = nullptr;
static SDL_GLContext s_settings_gl = nullptr;
static bool s_wants_shown = false;

// Asset-warn "allow once" session bypass — mirrors select_file.c's
// g_asset_warn_session_bypass. Set by the modal's "Allow once" choice, consumed
// by the next Generate gate. File-static so it never leaks across sessions.
static bool s_asset_warn_session_bypass = false;

// Which goal currently owns the forced accessibility=locations lock. When the
// user leaves Completionist we restore the accessibility value they had before
// the lock so the combo isn't stuck on "locations".
static bool s_accessibility_locked = false;
static uint8 s_accessibility_pre_lock = kAccessibility_Items;

// The three GL entry points we call directly (clear + viewport for the frame).
// Resolved via SDL_GL_GetProcAddress (no new link dependency); see P2 notes.
typedef void(APIENTRY *PFN_glViewport)(GLint, GLint, GLsizei, GLsizei);
typedef void(APIENTRY *PFN_glClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
typedef void(APIENTRY *PFN_glClear)(GLbitfield);
static PFN_glViewport s_glViewport = nullptr;
static PFN_glClearColor s_glClearColor = nullptr;
static PFN_glClear s_glClear = nullptr;

// GLSL version string handed to the ImGui OpenGL3 backend.
#ifdef __APPLE__
static const char *s_glsl_version = "#version 150";  // GL 3.2 core (macOS)
#else
static const char *s_glsl_version = "#version 130";  // GL 3.0
#endif

// ---- Small helpers ---------------------------------------------------------

// SplitMix64 — UI-ONLY entropy mix for the "New random seed" button. This is
// NOT the game RNG and NOT DeriveSeedFromState (which mixes the live
// frame_counter); the seed is a pure INPUT to the deterministic generator, so
// this does not touch determinism.
static uint64_t SplitMix64(uint64_t x) {
  x += 0x9E3779B97F4A7C15ull;
  x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
  x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
  return x ^ (x >> 31);
}

// Apply pending edits → refresh the bridge-derived hash + share string.
static void Pending_Changed() { RandoWindowBridge_RecomputeDerived(); }

// Enum combo whose options + canonical labels match the in-game screen. Returns
// true when the user picked a new value (caller refreshes derived state).
static bool EnumCombo(const char *label, uint8 *value,
                      const char *const *labels, int count) {
  bool changed = false;
  int cur = (int)*value;
  const char *preview = (cur >= 0 && cur < count) ? labels[cur] : "?";
  if (ImGui::BeginCombo(label, preview)) {
    for (int i = 0; i < count; i++) {
      bool selected = (cur == i);
      if (ImGui::Selectable(labels[i], selected)) {
        if (i != cur) { *value = (uint8)i; changed = true; }
      }
      if (selected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
  return changed;
}

static void HelpTooltip(const char *text) {
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    ImGui::SetTooltip("%s", text);
}

static void HexBytes(const uint8 *bytes, int n, char *out, int out_cap) {
  int o = 0;
  for (int i = 0; i < n && o + 2 < out_cap; i++)
    o += snprintf(out + o, out_cap - o, "%02x", (unsigned)bytes[i]);
  if (o < out_cap) out[o] = '\0';
}

// ---- Canonical labels (match RowValueText / the CLI grammar exactly) -------
// The labels here are the canonical CLI value strings (rando_settings.c
// parse_*), which the spec requires the combos to present — NOT the truncated
// 5-char in-game tile abbreviations (RAND/LOCS/...), which exist only because
// the file-select font is width-limited.
static const char *const kWorldStateLabels[] = {"open", "standard", "inverted", "retro"};
static const char *const kGoalLabels[] = {
    "ganon", "fast_ganon", "dungeons", "pedestal",
    "triforce-hunt", "ganonhunt", "completionist"};
static const char *const kItemPoolLabels[] = {"easy", "normal", "hard", "expert"};
static const char *const kDungeonModeLabels[] = {"vanilla", "dungeon", "wild"};
// Phase-A subset only (spec line 84): mode_weapons = {randomized, assured};
// accessibility = {items, locations}. Phase B reservations (vanilla/swordless
// for weapons, none for accessibility) are NOT offered.
static const char *const kModeWeaponsLabels[] = {"randomized", "assured"};
static const char *const kAccessibilityLabels[] = {"items", "locations"};

// Apply the Completionist accessibility lock to a settings struct. Returns true
// if the struct was mutated.
static bool ApplyAccessibilityLock(RandoSettings *s) {
  bool mutated = false;
  if (s->goal == kGoal_Completionist) {
    if (!s_accessibility_locked) {
      s_accessibility_pre_lock = s->accessibility;
      s_accessibility_locked = true;
    }
    if (s->accessibility != kAccessibility_Locations) {
      s->accessibility = kAccessibility_Locations;
      mutated = true;
    }
  } else if (s_accessibility_locked) {
    // Restore the user's pre-lock choice when they leave Completionist.
    s->accessibility = s_accessibility_pre_lock;
    s_accessibility_locked = false;
    mutated = true;
  }
  return mutated;
}

// ===========================================================================
// Panels
// ===========================================================================

// Recommended-features opt-in (C10 / spec "Recommended-features opt-in renders
// in the native window on PC"). Mirrors the in-game kRecRowBits[]/kRecRowLabels[]
// set (select_file.c). Edits ONLY bridge.pending_recommended_features0; the game
// thread applies it to g_config.features0 inside the generate consumer. features0
// is NOT part of settings_hash — toggling these does not change the hash/share.
static const uint32 kRecBits[] = {
    kFeatures0_SkipIntroOnKeypress, kFeatures0_ShowMaxItemsInYellow,
    kFeatures0_TurnWhileDashing,    kFeatures0_CollectItemsWithSword,
    kFeatures0_BreakPotsWithSword,  kFeatures0_DisableLowHealthBeep,
    kFeatures0_CarryMoreRupees,     kFeatures0_MiscBugFixes,
    kFeatures0_GameChangingBugFixes, kFeatures0_DimFlashes,
};
static const char *const kRecLabels[] = {
    "Skip intro on keypress",       "Show max items in yellow",
    "Turn while dashing",           "Collect items with sword",
    "Break pots with sword",        "Disable low-health beep",
    "Carry more rupees",            "Misc bug fixes",
    "Game-changing bug fixes",      "Dim flashes",
};
// Recommended ON set (mirrors kRecRecommendedOn[]); GameChangingBugFixes + Dim
// are off by default (Dim is an accessibility option honoring the user's pref).
static const uint8 kRecOn[] = {1, 1, 1, 1, 1, 1, 1, 1, 0, 0};
static const int kRecCount = (int)(sizeof(kRecBits) / sizeof(kRecBits[0]));

static void Panel_RecommendedFeatures() {
  RandoWindowBridge *b = &g_rando_window_bridge;
  uint32 *f = &b->pending_recommended_features0;
  for (int i = 0; i < kRecCount; i++) {
    bool on = (*f & kRecBits[i]) != 0;
    if (ImGui::Checkbox(kRecLabels[i], &on)) {
      if (on) *f |= kRecBits[i]; else *f &= ~kRecBits[i];
    }
  }
  ImGui::Spacing();
  if (ImGui::Button("Apply recommended set")) {
    for (int i = 0; i < kRecCount; i++) {
      if (kRecOn[i]) *f |= kRecBits[i]; else *f &= ~kRecBits[i];
    }
  }
  HelpTooltip("Turns on the recommended quality-of-life bits and turns off the rest.");
  ImGui::TextDisabled("Does not affect settings_hash or the share string.");
}

static void Panel_General() {
  RandoWindowBridge *b = &g_rando_window_bridge;
  RandoSettings *s = &b->pending;
  bool changed = false;

  // ---- Preset gallery ----
  ImGui::SeparatorText("Presets");
  for (int i = 0; i < kPreset__Count; i++) {
    if (i > 0) ImGui::SameLine();
    if (ImGui::Button(Settings_PresetName((SettingsPreset)i))) {
      Settings_ApplyPreset((SettingsPreset)i, s);
      // A preset may move the goal off/onto Completionist; re-evaluate the lock
      // from scratch so we don't restore a stale pre-lock value.
      s_accessibility_locked = false;
      ApplyAccessibilityLock(s);
      changed = true;
    }
    HelpTooltip(Settings_PresetName((SettingsPreset)i));
  }

  // ---- Core axes ----
  ImGui::SeparatorText("World & Goal");
  if (EnumCombo("World state", &s->world_state, kWorldStateLabels, 4)) changed = true;
  if (EnumCombo("Goal", &s->goal, kGoalLabels, 7)) changed = true;

  // Crystals (0..7).
  {
    int cg = (int)s->crystals_ganon;
    if (ImGui::SliderInt("Crystals: Ganon", &cg, 0, 7)) { s->crystals_ganon = (uint8)cg; changed = true; }
    HelpTooltip("Crystals required to make Ganon vulnerable.");
    int ct = (int)s->crystals_tower;
    if (ImGui::SliderInt("Crystals: Tower", &ct, 0, 7)) { s->crystals_tower = (uint8)ct; changed = true; }
    HelpTooltip("Crystals required to enter Ganon's Tower.");
  }

  if (EnumCombo("Item pool difficulty", &s->item_pool_difficulty, kItemPoolLabels, 4)) changed = true;
  if (EnumCombo("Weapons mode", &s->mode_weapons, kModeWeaponsLabels, 2)) changed = true;

  // Accessibility — read-only & forced to "locations" while goal=Completionist.
  bool acc_locked = (s->goal == kGoal_Completionist);
  if (acc_locked) ImGui::BeginDisabled();
  if (EnumCombo("Accessibility", &s->accessibility, kAccessibilityLabels, 2)) {
    s_accessibility_pre_lock = s->accessibility;  // track latest user choice
    changed = true;
  }
  if (acc_locked) {
    ImGui::EndDisabled();
    HelpTooltip("Forced to 'locations' while goal = completionist.");
  }

  // ---- Triforce / Ganon Hunt piece fields (only for those goals) ----
  bool pieces_relevant = (s->goal == kGoal_TriforceHunt || s->goal == kGoal_GanonHunt);
  if (pieces_relevant) {
    ImGui::SeparatorText("Triforce Hunt");
    int pr = (int)s->pieces_required;
    if (ImGui::InputInt("Pieces required", &pr)) {
      if (pr < 1) pr = 1; if (pr > 99) pr = 99;
      s->pieces_required = (uint16)pr; changed = true;
    }
    int pp = (int)s->pieces_placed;
    if (ImGui::InputInt("Pieces placed", &pp)) {
      if (pp < 1) pp = 1; if (pp > 99) pp = 99;
      s->pieces_placed = (uint16)pp; changed = true;
    }
  } else {
    ImGui::SeparatorText("Triforce Hunt");
    ImGui::BeginDisabled();
    int pr = (int)s->pieces_required, pp = (int)s->pieces_placed;
    ImGui::InputInt("Pieces required", &pr);
    ImGui::InputInt("Pieces placed", &pp);
    ImGui::EndDisabled();
    HelpTooltip("Only used by goal = triforce-hunt or ganonhunt.");
  }

  // ---- Boolean toggles ----
  ImGui::SeparatorText("Toggles");
  {
    bool v;
    v = s->prize_shuffle != 0;
    if (ImGui::Checkbox("Prize shuffle", &v)) { s->prize_shuffle = v; changed = true; }
    HelpTooltip("Shuffle crystal/pendant -> dungeon prize assignments.");
    v = s->medallion_shuffle != 0;
    if (ImGui::Checkbox("Medallion shuffle", &v)) { s->medallion_shuffle = v; changed = true; }
    HelpTooltip("Shuffle Bombos/Ether/Quake -> Misery Mire / Turtle Rock entrances.");
    v = s->race_mode != 0;
    if (ImGui::Checkbox("Race mode", &v)) { s->race_mode = v; changed = true; }
    HelpTooltip("Suppress the spoiler for the generated slot.");
    v = s->hints != 0;
    if (ImGui::Checkbox("Hints", &v)) { s->hints = v; changed = true; }
    HelpTooltip("Telepathic-tile hints.");
  }

  // ---- Pinned / disabled axes (greyed + tooltip) ----
  ImGui::SeparatorText("Pinned (Phase A)");
  ImGui::BeginDisabled();
  {
    const char *logic_lbl = "NoGlitches";
    ImGui::LabelText("Logic", "%s", logic_lbl);
    ImGui::LabelText("Tricks", "%s", "none");
    bool t = s->region_boss_hearts_in_pool != 0;
    ImGui::Checkbox("Region boss hearts in pool", &t);
    ImGui::LabelText("Pyramid bow upgrade", "%s", "Silvers");
  }
  ImGui::EndDisabled();
  HelpTooltip("pinned in Phase A");

  // ---- Recommended features (quality-of-life opt-in; not in the hash) ----
  if (ImGui::CollapsingHeader("Recommended features")) {
    Panel_RecommendedFeatures();
  }

  // ---- Display (window theme; persisted to the sidecar) ----
  ImGui::SeparatorText("Display");
  {
    bool dark = g_rando_window_prefs.dark_theme;
    if (ImGui::Checkbox("Dark theme", &dark)) {
      g_rando_window_prefs.dark_theme = dark;  // persisted at shutdown
      if (dark) ImGui::StyleColorsDark(); else ImGui::StyleColorsLight();
    }
    HelpTooltip("Toggles the settings-window theme. Saved to saves/rando_window.ini.");
  }

  // ---- Seed ----
  ImGui::SeparatorText("Seed");
  {
    // Decimal uint64 input. ImGui has no native u64 widget; use a text buffer
    // and parse it back. Display the same value as 16-char hex alongside.
    static char seed_buf[32];
    static uint64 seed_buf_mirror = ~0ull;  // forces a re-format on first use
    if (seed_buf_mirror != b->seed_u64) {
      snprintf(seed_buf, sizeof seed_buf, "%llu", (unsigned long long)b->seed_u64);
      seed_buf_mirror = b->seed_u64;
    }
    if (ImGui::InputText("Seed (decimal)", seed_buf, sizeof seed_buf,
                         ImGuiInputTextFlags_CharsDecimal)) {
      unsigned long long v = 0;
      if (sscanf(seed_buf, "%llu", &v) == 1) {
        b->seed_u64 = (uint64)v;
        seed_buf_mirror = b->seed_u64;  // accept; don't reformat under the cursor
        changed = true;
      }
    }
    // 16-char hex view of the seed (the integer's natural big-endian hex).
    char seed_hex[17];
    snprintf(seed_hex, sizeof seed_hex, "%016llx", (unsigned long long)b->seed_u64);
    ImGui::Text("Seed (hex): %s", seed_hex);
    if (ImGui::Button("New random seed")) {
      b->seed_u64 = SplitMix64((uint64)SDL_GetPerformanceCounter());
      seed_buf_mirror = ~0ull;  // force re-format from the new value
      changed = true;
    }
    HelpTooltip("UI-only entropy (SplitMix64 of the performance counter). The seed is a pure input.");
  }

  // ---- Live read-only derived display ----
  ImGui::SeparatorText("Live (read-only)");
  {
    char hashhex[65];
    HexBytes(b->pending_hash, 32, hashhex, sizeof hashhex);
    ImGui::Text("settings_hash: %s", hashhex);
    ImGui::TextWrapped("share string: %s", b->share_string[0] ? b->share_string : "(none)");
  }

  if (changed) {
    // Goal changes can engage/release the Completionist accessibility lock.
    ApplyAccessibilityLock(s);
    // Keep pieces sane (mirror SettingsValidatePieces) so the hash reflects a
    // valid struct; the Generate validator surfaces the user-facing error.
    if (s->pieces_placed < 1) s->pieces_placed = 1;
    if (s->pieces_placed > 99) s->pieces_placed = 99;
    if (s->pieces_required < 1) s->pieces_required = 1;
    Pending_Changed();
  }
}

static void Panel_Dungeons() {
  RandoWindowBridge *b = &g_rando_window_bridge;
  RandoSettings *s = &b->pending;
  bool changed = false;

  ImGui::SeparatorText("Dungeon item modes");
  if (EnumCombo("Small keys", &s->dungeon_small_keys_mode, kDungeonModeLabels, 3)) changed = true;
  if (EnumCombo("Big keys", &s->dungeon_big_keys_mode, kDungeonModeLabels, 3)) changed = true;
  if (EnumCombo("Maps", &s->dungeon_maps_mode, kDungeonModeLabels, 3)) changed = true;
  if (EnumCombo("Compasses", &s->dungeon_compasses_mode, kDungeonModeLabels, 3)) changed = true;

  ImGui::Spacing();
  ImGui::SeparatorText("Set all");
  auto set_all = [&](uint8 mode) {
    s->dungeon_small_keys_mode = mode;
    s->dungeon_big_keys_mode = mode;
    s->dungeon_maps_mode = mode;
    s->dungeon_compasses_mode = mode;
    changed = true;
  };
  if (ImGui::Button("All vanilla")) set_all(kDungeonItemMode_Vanilla);
  ImGui::SameLine();
  if (ImGui::Button("All dungeon")) set_all(kDungeonItemMode_Dungeon);
  ImGui::SameLine();
  if (ImGui::Button("All wild")) set_all(kDungeonItemMode_Wild);

  if (changed) Pending_Changed();
}

static void Panel_Shuffles() {
  RandoWindowBridge *b = &g_rando_window_bridge;
  RandoSettings *s = &b->pending;
  bool changed = false;

  // boss_shuffle / drop_shuffle live in the settings struct (and the hash), but
  // they are runtime-inert for playable slots (Rando_ActivateSidecarSlot never
  // regenerates them) — matching the in-game screen, we present them as DISABLED
  // "coming soon" placeholders rather than live toggles, so no widget lies.
  ImGui::SeparatorText("Shuffles (coming soon)");
  ImGui::BeginDisabled();
  {
    bool bs = s->boss_shuffle != 0, ds = s->drop_shuffle != 0;
    ImGui::Checkbox("Boss shuffle", &bs);
    ImGui::Checkbox("Drop shuffle", &ds);
    bool off = false;
    ImGui::Checkbox("Entrance shuffle", &off);
    ImGui::Checkbox("Enemy shuffle", &off);
    ImGui::Checkbox("Glitches", &off);
  }
  ImGui::EndDisabled();
  HelpTooltip("not yet active in playable slots");

  if (changed) Pending_Changed();
}

static void Panel_AssetHash() {
  ImGui::SeparatorText("Asset data");
  char cur[65], van[65];
  HexBytes(g_assets_hash, 32, cur, sizeof cur);
  ImGui::TextWrapped("loaded assets hash: %s", cur);
  if (kVanillaAssetsHashKnown) {
    HexBytes(kVanillaAssetsHash, 32, van, sizeof van);
    ImGui::TextWrapped("vanilla assets hash: %s", van);
    bool is_vanilla = (memcmp(g_assets_hash, kVanillaAssetsHash, 32) == 0);
    if (is_vanilla) {
      ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "Status: matches vanilla.");
    } else if (AssetDecision_FindAllow(g_assets_hash)) {
      ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.3f, 1.0f),
                         "Status: differs from vanilla (Always-allow on file).");
    } else if (s_asset_warn_session_bypass) {
      ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.3f, 1.0f),
                         "Status: differs from vanilla (allowed once this session).");
    } else {
      ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.4f, 1.0f),
                         "Status: differs from vanilla (will prompt on Generate).");
    }
  } else {
    ImGui::TextDisabled("vanilla assets hash: unknown (no comparison performed)");
  }
}

// Spoiler region grouping caveat: the bridge stores only the placement table,
// not the generated world_state, so we group by the BASE region_id from
// kRandoLocations (no per-world-state region override). For Inverted seeds a few
// locations (e.g. Ether Tablet) would group under their Standard region here; the
// runtime/file spoiler honors the override. Threading world_state through the
// bridge is deferred — see TODO. Read-only either way.
static void Panel_Spoiler() {
  const RandoWindowBridge *b = &g_rando_window_bridge;
  if (!b->has_last_generated) {
    ImGui::TextDisabled("No seed generated this session yet.");
    ImGui::TextWrapped("Generate a (non-race) seed to view its placement here.");
    return;
  }

  const RandoPlacementTable *t = &b->last_generated_placement;
  ImGui::Text("Placements: %u (grouped by region)", (unsigned)t->count);
  ImGui::TextDisabled(
      "Region grouping uses base regions; per-world-state overrides (e.g. Inverted) "
      "are not applied here. Read-only.");

  if (!ImGui::BeginTable("##spoiler", 2,
                         ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                             ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable)) {
    return;
  }
  ImGui::TableSetupColumn("Location");
  ImGui::TableSetupColumn("Item");
  ImGui::TableSetupScrollFreeze(0, 1);
  ImGui::TableHeadersRow();

  // Build (region_id, location_id, item_id) rows then sort by (region, loc),
  // mirroring rando_spoiler.c's text writer grouping.
  uint16 n = t->count;
  static struct Row { uint16 region_id; uint16 location_id; uint16 item_id; } rows[512];
  if (n > 512) n = 512;
  for (uint16 i = 0; i < n; i++) {
    rows[i].location_id = t->entries[i].location_id;
    rows[i].item_id = t->entries[i].item_id;
    rows[i].region_id = 0xFFFF;
    for (uint32 j = 0; j < kRandoLocationsCount; j++) {
      if (kRandoLocations[j].id == rows[i].location_id) {
        rows[i].region_id = kRandoLocations[j].region_id;
        break;
      }
    }
  }
  for (uint16 i = 1; i < n; i++) {
    uint16 j = i;
    while (j > 0 &&
           (rows[j - 1].region_id > rows[j].region_id ||
            (rows[j - 1].region_id == rows[j].region_id &&
             rows[j - 1].location_id > rows[j].location_id))) {
      Row tmp = rows[j - 1]; rows[j - 1] = rows[j]; rows[j] = tmp;
      j--;
    }
  }

  uint16 cur_region = 0xFFFE;
  for (uint16 i = 0; i < n; i++) {
    if (rows[i].region_id != cur_region) {
      cur_region = rows[i].region_id;
      ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
      ImGui::TableSetColumnIndex(0);
      ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f), "%s",
                         Rando_GetRegionName(cur_region));
    }
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted(Rando_GetLocationName(rows[i].location_id));
    ImGui::TableSetColumnIndex(1);
    ImGui::TextUnformatted(Rando_GetItemName(rows[i].item_id));
  }
  ImGui::EndTable();
}

// ===========================================================================
// Share-string copy/paste + Generate flow (rendered below the tab bar)
// ===========================================================================

static void RenderShareRow() {
  RandoWindowBridge *b = &g_rando_window_bridge;
  static char s_paste_error[160];

  if (ImGui::Button("Copy share string")) {
    if (b->share_string[0]) SDL_SetClipboardText(b->share_string);
  }
  ImGui::SameLine();
  if (ImGui::Button("Paste share string")) {
    s_paste_error[0] = '\0';
    char *clip = SDL_GetClipboardText();
    if (clip && clip[0]) {
      ShareString ss;
      ShareDecodeStatus st = Share_Decode(clip, &ss);
      if (st == kShareDecodeOk) {
        // A share string carries seed_u64 + the (one-way) settings_hash, NOT the
        // full settings struct (settings_hash cannot be inverted). So we adopt the
        // decoded seed and warn if the current settings' hash disagrees with the
        // pasted one — we do NOT fabricate widget values from the hash.
        b->seed_u64 = ss.seed_u64;
        Pending_Changed();
        if (memcmp(b->pending_hash, ss.settings_hash, 16) != 0) {
          snprintf(s_paste_error, sizeof s_paste_error,
                   "Seed adopted. Note: current settings differ from the pasted "
                   "string's settings (the share string cannot restore settings — "
                   "pick them to match).");
        }
      } else {
        const char *why = "unrecognized share string";
        switch (st) {
          case kShareDecodeBadLength:    why = "wrong length"; break;
          case kShareDecodeBadBase32:    why = "corrupted base32"; break;
          case kShareDecodeBadMagic:     why = "wrong magic / not a z3r share string"; break;
          case kShareDecodeBadChecksum:  why = "checksum mismatch"; break;
          case kShareDecodeAlttprFormat: why = "alttpr.com-format hashes are not supported"; break;
          default: break;
        }
        snprintf(s_paste_error, sizeof s_paste_error, "Paste failed: %s.", why);
      }
    } else {
      snprintf(s_paste_error, sizeof s_paste_error, "Clipboard is empty.");
    }
    if (clip) SDL_free(clip);
  }
  if (s_paste_error[0])
    ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.3f, 1.0f), "%s", s_paste_error);
}

// Modal IDs.
static const char *kAssetModalId = "Asset data differs from vanilla";
static const char *kGenModalId = "Generating seed...";

// Open-request flags handed to ImGui::OpenPopup at the top of the frame (must be
// called from the same ID stack scope where BeginPopupModal lives).
static bool s_open_asset_modal = false;
static bool s_open_gen_modal = false;

// Begin generation: gate on the asset hash; either open the asset modal or fire.
static void TryBeginGenerate() {
  RandoWindowBridge *b = &g_rando_window_bridge;
  bool needs_gate = kVanillaAssetsHashKnown &&
                    memcmp(g_assets_hash, kVanillaAssetsHash, 32) != 0 &&
                    !AssetDecision_FindAllow(g_assets_hash) &&
                    !s_asset_warn_session_bypass;
  if (needs_gate) {
    s_open_asset_modal = true;
    return;
  }
  // Gate passed (vanilla, persisted, or session-bypassed) → request generate.
  RandoWindowBridge_RequestGenerate(b->target_slot_index);
  s_open_gen_modal = true;
}

static void RenderGenerateRow() {
  RandoWindowBridge *b = &g_rando_window_bridge;

  // Cross-field validation (red error + disabled button when invalid).
  char err[256];
  int invalid = RandoWindowBridge_Validate(&b->pending, err, sizeof err);

  ImGui::Separator();
  RenderShareRow();

  if (invalid) {
    ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f), "%s", err);
  }

  bool no_target = (b->target_slot_index < 0);
  bool disabled = (invalid != 0) || b->generate_in_progress || no_target;
  if (disabled) ImGui::BeginDisabled();
  if (ImGui::Button("Generate & start new slot")) {
    TryBeginGenerate();
  }
  if (disabled) {
    ImGui::EndDisabled();
    if (invalid)
      HelpTooltip("Fix the validation error above before generating.");
    else if (no_target)
      HelpTooltip("Open this window from the file-select \"New Randomizer\" entry to pick a target slot.");
    else
      HelpTooltip("Generation already in progress.");
  }
  ImGui::SameLine();
  ImGui::TextDisabled("target slot: %d", b->target_slot_index);
}

// Asset-warn modal: Always allow / Allow once / Cancel.
static void RenderAssetModal() {
  RandoWindowBridge *b = &g_rando_window_bridge;
  if (s_open_asset_modal) {
    ImGui::OpenPopup(kAssetModalId);
    s_open_asset_modal = false;
  }
  ImGui::SetNextWindowSize(ImVec2(480, 0), ImGuiCond_Appearing);
  if (ImGui::BeginPopupModal(kAssetModalId, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextWrapped(
        "The loaded asset data differs from vanilla zelda3_assets.dat. Placement "
        "may misbehave with non-vanilla assets. How would you like to proceed?");
    ImGui::Spacing();
    if (ImGui::Button("Always allow")) {
      AssetDecision_Persist(g_assets_hash);
      ImGui::CloseCurrentPopup();
      RandoWindowBridge_RequestGenerate(b->target_slot_index);
      s_open_gen_modal = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Allow once")) {
      s_asset_warn_session_bypass = true;
      ImGui::CloseCurrentPopup();
      RandoWindowBridge_RequestGenerate(b->target_slot_index);
      s_open_gen_modal = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
      ImGui::CloseCurrentPopup();  // no generation
    }
    ImGui::EndPopup();
  }
}

// Generating-seed modal: input-blocking; polls bridge.generate_status.
static void RenderGenerateModal() {
  RandoWindowBridge *b = &g_rando_window_bridge;
  if (s_open_gen_modal) {
    ImGui::OpenPopup(kGenModalId);
    s_open_gen_modal = false;
  }
  ImGui::SetNextWindowSize(ImVec2(420, 0), ImGuiCond_Appearing);
  if (ImGui::BeginPopupModal(kGenModalId, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    int status = b->generate_status;
    if (status == 1 || b->generate_in_progress) {
      ImGui::TextUnformatted("Generating seed... please wait.");
      // The game thread consumes the request and runs Rando_GenerateSlot
      // synchronously (one game frame), then sets generate_status. The modal
      // stays input-blocking until then.
    } else if (status == 2) {
      ImGui::Text("Slot %d is ready.", b->target_slot_index);
      ImGui::TextUnformatted("Load it now?");
      ImGui::Spacing();
      if (ImGui::Button("Yes")) {
        // TODO(P5/later): route to the file-select load path for the just-created
        // slot. That wiring lands in a later phase; for now we just hide the
        // settings window so the player returns to the game/file-select.
        ImGui::CloseCurrentPopup();
        b->generate_status = 0;
        RandoWindow_Hide();
      }
      ImGui::SameLine();
      if (ImGui::Button("No")) {
        ImGui::CloseCurrentPopup();
        b->generate_status = 0;
      }
    } else if (status == -1) {
      ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.4f, 1.0f), "Generation failed:");
      ImGui::TextWrapped("%s", b->generate_error[0] ? b->generate_error : "(unknown error)");
      ImGui::Spacing();
      if (ImGui::Button("OK")) {
        ImGui::CloseCurrentPopup();
        b->generate_status = 0;
      }
    } else {
      // status == 0 (idle) but modal somehow open — close defensively.
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
}

// ---- Lifecycle -------------------------------------------------------------
void RandoWindow_Init(SDL_Window *window, SDL_GLContext gl_context) {
  s_settings_window = window;
  s_settings_gl = gl_context;

  // Make the settings context current before any ImGui/backend init touches GL.
  SDL_GL_MakeCurrent(s_settings_window, s_settings_gl);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  // Apply the persisted theme (default dark). g_rando_window_prefs is populated
  // by Config_LoadAuxIniFile before RandoWindow_Init in main.c; it defaults to
  // dark_theme=true if no sidecar was found.
  if (g_rando_window_prefs.dark_theme)
    ImGui::StyleColorsDark();
  else
    ImGui::StyleColorsLight();

  ImGui_ImplSDL2_InitForOpenGL(s_settings_window, s_settings_gl);
  ImGui_ImplOpenGL3_Init(s_glsl_version);

  // Resolve the few GL entry points we call directly (context is current now).
  s_glViewport = (PFN_glViewport)SDL_GL_GetProcAddress("glViewport");
  s_glClearColor = (PFN_glClearColor)SDL_GL_GetProcAddress("glClearColor");
  s_glClear = (PFN_glClear)SDL_GL_GetProcAddress("glClear");
}

void RandoWindow_ProcessEvent(const void *sdl_event) {
  ImGui_ImplSDL2_ProcessEvent((const SDL_Event *)sdl_event);
}

void RandoWindow_BeginFrame(void) {
  SDL_GL_MakeCurrent(s_settings_window, s_settings_gl);

  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplSDL2_NewFrame();
  ImGui::NewFrame();

  const ImGuiViewport *vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(vp->WorkPos);
  ImGui::SetNextWindowSize(vp->WorkSize);
  ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                           ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                           ImGuiWindowFlags_NoBringToFrontOnFocus;
  if (ImGui::Begin("Z3R Settings##main", nullptr, flags)) {
    const RandoWindowBridge *b = &g_rando_window_bridge;
    if (ImGui::BeginTabBar("##z3r_tabs")) {
      if (ImGui::BeginTabItem("General"))   { Panel_General();   ImGui::EndTabItem(); }
      if (ImGui::BeginTabItem("Dungeons"))  { Panel_Dungeons();  ImGui::EndTabItem(); }
      if (ImGui::BeginTabItem("Shuffles"))  { Panel_Shuffles();  ImGui::EndTabItem(); }
      if (ImGui::BeginTabItem("Asset Hash")){ Panel_AssetHash(); ImGui::EndTabItem(); }
      // Spoiler tab hidden entirely when the last generation was race-mode.
      if (!b->last_generated_race_mode) {
        if (ImGui::BeginTabItem("Spoiler")) { Panel_Spoiler(); ImGui::EndTabItem(); }
      }
      ImGui::EndTabBar();
    }

    // Generate flow lives below the tabs so it's always visible/actionable.
    RenderGenerateRow();
  }
  ImGui::End();

  // Modals are opened/polled outside the main window's Begin/End but inside the
  // same frame so OpenPopup requests from this frame take effect.
  RenderAssetModal();
  RenderGenerateModal();
}

void RandoWindow_Render(void) {
  SDL_GL_MakeCurrent(s_settings_window, s_settings_gl);

  ImGui::Render();

  int w = 0, h = 0;
  SDL_GL_GetDrawableSize(s_settings_window, &w, &h);  // HiDPI-correct framebuffer size
  if (s_glViewport) s_glViewport(0, 0, w, h);
  if (s_glClearColor) s_glClearColor(0.10f, 0.10f, 0.12f, 1.0f);
  if (s_glClear) s_glClear(GL_COLOR_BUFFER_BIT);
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
  SDL_GL_SwapWindow(s_settings_window);
}

void RandoWindow_Shutdown(void) {
  if (ImGui::GetCurrentContext() != nullptr) {
    // Make the settings context current first: the per-frame restore may have
    // left the GAME context current (OpenGL renderer), and the backend's
    // glDelete* in ImGui_ImplOpenGL3_Shutdown must target the settings
    // context (audit HIGH).
    if (s_settings_window && s_settings_gl)
      SDL_GL_MakeCurrent(s_settings_window, s_settings_gl);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
  }
}

// ---- Show / hide -----------------------------------------------------------
void RandoWindow_OpenForNewSlot(int slot_index) {
  g_rando_window_bridge.target_slot_index = slot_index;
  // R2 (PLAN.md §9): snapshot the LIVE config features0 into the bridge so the
  // recommended-features panel starts from the user's current configuration.
  // OpenForNewSlot runs on the game thread (kind-toggle entry), so reading
  // g_config here is safe and current. The UI edits only this snapshot; the game
  // thread applies it back to g_config.features0 inside the generate consumer.
  g_rando_window_bridge.pending_recommended_features0 = g_config.features0;
  if (s_settings_window) {
    SDL_ShowWindow(s_settings_window);
    SDL_RaiseWindow(s_settings_window);
  }
  s_wants_shown = true;
}

void RandoWindow_Hide(void) {
  if (s_settings_window)
    SDL_HideWindow(s_settings_window);
  s_wants_shown = false;
  // Closing the window cancels a not-yet-consumed generate request AND clears
  // the kind-toggle target, so the game-frame consumer cannot run a generate
  // against a cleared (-1) slot (audit BLOCKER — defense in depth with the
  // slot-index guard in Rando_GenerateSlot).
  g_rando_window_bridge.generate_requested = false;
  if (g_rando_window_bridge.target_slot_index >= 0)
    RandoWindowBridge_CancelTarget();
}

bool RandoWindow_WantsShown(void) { return s_wants_shown; }

// ---- Geometry persistence (P5) --------------------------------------------
// Apply a restored window rect, clamped to the union of all connected displays.
// If the saved rect does not intersect ANY display (e.g. a monitor was
// unplugged), the position is ignored and the window is re-centered; the size
// is still honored (clamped to something sane). Called BEFORE the window is
// shown, so SetWindowPosition/Size take effect without a visible jump.
void RandoWindow_ApplyGeometry(int x, int y, int w, int h) {
  if (!s_settings_window) return;

  // Sanitize the size first (reject absurd / non-positive values).
  if (w < 200) w = 200;
  if (h < 200) h = 200;
  if (w > 16384) w = 16384;
  if (h > 16384) h = 16384;

  // Does the saved rect intersect any display? Build the union of display
  // bounds; treat the rect as on-screen if it overlaps at least one.
  bool on_screen = false;
  int ndisp = SDL_GetNumVideoDisplays();
  for (int i = 0; i < ndisp; i++) {
    SDL_Rect db;
    if (SDL_GetDisplayBounds(i, &db) != 0) continue;
    SDL_Rect want = { x, y, w, h };
    SDL_Rect isect;
    if (SDL_IntersectRect(&want, &db, &isect) && isect.w > 0 && isect.h > 0) {
      on_screen = true;
      break;
    }
  }

  SDL_SetWindowSize(s_settings_window, w, h);
  if (on_screen) {
    SDL_SetWindowPosition(s_settings_window, x, y);
  } else {
    // Off-screen (or no displays reported the rect): re-center.
    SDL_SetWindowPosition(s_settings_window, SDL_WINDOWPOS_CENTERED,
                          SDL_WINDOWPOS_CENTERED);
  }
}

// Read back the live window geometry (logical position + size, not the HiDPI
// drawable size). Any out param may be NULL.
void RandoWindow_GetGeometry(int *x, int *y, int *w, int *h) {
  int px = 0, py = 0, pw = 0, ph = 0;
  if (s_settings_window) {
    SDL_GetWindowPosition(s_settings_window, &px, &py);
    SDL_GetWindowSize(s_settings_window, &pw, &ph);
  }
  if (x) *x = px;
  if (y) *y = py;
  if (w) *w = pw;
  if (h) *h = ph;
}

#endif  // Z3R_NATIVE_SETTINGS_WINDOW
