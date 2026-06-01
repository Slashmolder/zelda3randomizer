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

#include <cstdio>   // snprintf, fopen/fread/remove (spoiler clipboard temp file)
#include <cstring>  // memcmp, strlen, strcmp
#include <cstdlib>  // malloc/free (spoiler clipboard buffer)

#include "rando_window.h"
#include "rando_window_bridge.h"
#include "tracker_windows.h"  // Trackers_SetShown/IsShown (Trackers launcher tab)
#include "game_config_widgets.h"  // GameConfig_* (native game-config panels)
#include "game_panels.h"          // Rando*_Render (reachability/hints sub-tabs)
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

// Forward declarations (definitions appear later but are referenced from
// RandoWindow_Init, which is defined earlier in the file).
static int RandoWindow_BuildTabList(bool last_generated_race_mode,
                                    const char **out_tabs, int cap);
static void RandoWindow_TabSelfCheck(void);

// ---- File-static state -----------------------------------------------------
static SDL_Window *s_settings_window = nullptr;
static SDL_GLContext s_settings_gl = nullptr;
static bool s_wants_shown = false;
// One-shot tab selection on the next frame: Game Settings when opened in config
// mode (RandoWindow_ToggleConfig), or the rando "General" tab when opened for a
// new slot (RandoWindow_OpenForNewSlot). At most one is set at a time.
static bool s_select_game_settings_once = false;
static bool s_select_general_once = false;

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

// A fresh UI-input seed. Mixes the high-res performance counter with a
// monotonic salt so two rolls within the same counter tick still differ (e.g.
// opening the window twice in quick succession). Pure generator INPUT — does
// not touch determinism.
static uint64 RollRandomSeed() {
  static uint64 s_salt = 0;
  s_salt += 0x1234567ull;
  return SplitMix64((uint64)SDL_GetPerformanceCounter() ^ s_salt);
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
// Phase-A subset only (spec line 84): mode_weapons = {randomized, assured}.
// Phase B reservations (vanilla/swordless for weapons) are NOT offered.
static const char *const kModeWeaponsLabels[] = {"randomized", "assured"};
// Accessibility — full ALTTPR three-way (index == enum value):
//   [0] items        = kAccessibility_Items     ("100% Inventory")
//   [1] locations    = kAccessibility_Locations ("100% Locations")
//   [2] beatable only = kAccessibility_None      (ALTTPR "Not Guaranteed")
// All three guarantee the seed is beatable; they differ in how much extra
// reachability is required (locations = every location; items = every
// progression item; beatable only = goal only).
static const char *const kAccessibilityLabels[] = {"items", "locations", "beatable only"};

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
  ImGui::TextWrapped(
      "Quality-of-life features baked into the seed you generate here. They ride "
      "along with the generated slot and are NOT part of the settings hash or "
      "share string \xe2\x80\x94 toggling them won't change the seed's identity. "
      "(The same features for normal play live under Game Settings \xe2\x86\x92 "
      "Gameplay; this panel only sets what gets stamped into this seed.)");
  ImGui::Spacing();
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
      // HINTS is a UI-scoped axis, not a preset axis. Settings_ApplyPreset runs
      // Settings_SetDefaults, which resets hints to its default; preserve the
      // user's explicit Hints choice across a preset click so picking a preset
      // doesn't silently flip the Hints checkbox. (PC equivalent of the in-game
      // screen's cece249 fix, which is compiled out on PC.)
      uint8 saved_hints = s->hints;
      Settings_ApplyPreset((SettingsPreset)i, s);
      s->hints = saved_hints;
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
  if (EnumCombo("Accessibility", &s->accessibility, kAccessibilityLabels, 3)) {
    s_accessibility_pre_lock = s->accessibility;  // track latest user choice
    changed = true;
  }
  if (acc_locked) {
    ImGui::EndDisabled();
    HelpTooltip("Forced to 'locations' while goal = completionist.");
  } else {
    HelpTooltip("All tiers guarantee the seed is beatable.\n"
                "items: every progression item is reachable (100% inventory).\n"
                "locations: every location is reachable (100% locations).\n"
                "beatable only: only the goal is reachable; some items or\n"
                "locations may be unreachable.");
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
    // region_boss_hearts_in_pool is INVERTED relative to its name: 1 = boss
    // hearts pinned to their boss slots (NOT in the general pool, the default);
    // 0 = shuffled into the general pool. Map checked -> 0 at the UI layer so the
    // user-facing label reads naturally and the misleading raw field name/value
    // is never shown. Do not rename the field — the canonical byte/CSV keys keep
    // their existing meaning for headless/share-string compat.
    v = (s->region_boss_hearts_in_pool == 0);
    if (ImGui::Checkbox("Shuffle boss heart containers", &v)) {
      s->region_boss_hearts_in_pool = v ? 0 : 1;
      changed = true;
    }
    HelpTooltip("Bosses reward random items instead of hearts.");
  }

  // ---- Locked settings (informational; not yet configurable) ----
  // Rendered as plain read-only text under a collapsed header rather than a row
  // of disabled controls, which read as broken/confusing widgets.
  if (ImGui::CollapsingHeader("Locked settings (fixed in this version)")) {
    ImGui::TextDisabled("These axes aren't configurable yet — every seed uses these values:");
    ImGui::BulletText("Logic: NoGlitches");
    ImGui::BulletText("Tricks: none");
    ImGui::BulletText("Pyramid bow upgrade: Silvers");
  }
  HelpTooltip("pinned in Phase A");

  // Quality-of-Life enhancement bits live in their own top-level tab
  // (Panel_RecommendedFeatures), not here — they are opt-in and not part of the
  // settings hash, so they don't belong among the core seed-defining axes.

  // Window theme/scale moved to Game Settings -> Interface (it's a property of
  // this settings window, not a randomizer axis). Don't duplicate it here.

  // ---- Seed ----
  ImGui::SeparatorText("Seed");
  {
    // Hex uint64 input. ImGui has no native u64 widget; use a text buffer and
    // parse it back. Hex is the natural representation for a 64-bit seed (and
    // matches the share-string ethos); the decimal value is shown read-only
    // below for reference.
    static char seed_buf[20];
    static uint64 seed_buf_mirror = ~0ull;  // forces a re-format on first use
    if (seed_buf_mirror != b->seed_u64) {
      snprintf(seed_buf, sizeof seed_buf, "%016llx", (unsigned long long)b->seed_u64);
      seed_buf_mirror = b->seed_u64;
    }
    if (ImGui::InputText("Seed (hex)", seed_buf, sizeof seed_buf,
                         ImGuiInputTextFlags_CharsHexadecimal)) {
      unsigned long long v = 0;
      // CharsHexadecimal already blocks non-hex input. seed_buf[16]=='\0'
      // means <=16 hex digits typed; reject longer pastes so they can't
      // silently overflow-wrap the u64 under %llx. A shorter value is a
      // legitimate (smaller) seed — the seed is a pure u64 input.
      if (seed_buf[16] == '\0' && sscanf(seed_buf, "%llx", &v) == 1) {
        b->seed_u64 = (uint64)v;
        seed_buf_mirror = b->seed_u64;  // accept; don't reformat under the cursor
        changed = true;
      }
    }
    HelpTooltip("Any 64-bit value (1-16 hex digits). A shorter entry is a smaller seed, not an error.");
    ImGui::TextDisabled("decimal: %llu", (unsigned long long)b->seed_u64);
    if (ImGui::Button("New random seed")) {
      b->seed_u64 = RollRandomSeed();
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

  // Phase C — entrance shuffle (Stage 1: coupled cave shuffle). Live for
  // playable slots (Rando_ActivateSidecarSlot regenerates the door overlay).
  // Open/Standard only — Inverted carries a static region override the per-seed
  // override would clobber, and Retro re-uses cave host-rooms for TakeAny.
  ImGui::SeparatorText("Entrance shuffle");
  {
    bool ws_ok = (s->world_state == kWorldState_Open ||
                  s->world_state == kWorldState_Standard);
    ImGui::BeginDisabled(!ws_ok);

    // Quick presets — one-click bundles over the three groups below (Shuffle /
    // Exits / Cross). The live controls always reflect the resulting state, so a
    // preset followed by a tweak is fully transparent.
    ImGui::TextUnformatted("Quick:"); ImGui::SameLine();
    if (ImGui::SmallButton("Off")) {
      s->shuffle_cave_entrances = 0; s->shuffle_dungeon_entrances = 0;
      s->shuffle_ganons_tower_entrance = 0;
      s->coupled = 0; s->cross_category = 0; s->decoupled = 0; changed = true;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Simple")) {
      s->shuffle_cave_entrances = 1; s->shuffle_dungeon_entrances = 1;
      s->coupled = 1; s->cross_category = 0; s->decoupled = 0; changed = true;
    }
    HelpTooltip("Caves + dungeons shuffled within their own category, coupled "
                "(enter A -> exit A). ALTTPR's Simple/Restricted converge here.");
    ImGui::SameLine();
    if (ImGui::SmallButton("Crossed")) {
      s->shuffle_cave_entrances = 1; s->shuffle_dungeon_entrances = 1;
      s->coupled = 1; s->cross_category = 1; s->decoupled = 0; changed = true;
    }
    HelpTooltip("Caves and dungeons share ONE pool - a cave door can lead to a "
                "dungeon interior and vice versa, coupled.");
    ImGui::SameLine();
    if (ImGui::SmallButton("Insanity")) {
      // Full decoupled: caves AND dungeons, one-way exits (both runtimes built).
      s->shuffle_cave_entrances = 1; s->shuffle_dungeon_entrances = 1;
      s->coupled = 0; s->cross_category = 0; s->decoupled = 1; changed = true;
    }
    HelpTooltip("Decoupled caves AND dungeons - exiting drops you at a DIFFERENT "
                "door (one-way warps). The generator keeps the goal reachable.");

    ImGui::Spacing();
    // ---- Group 1: Shuffle (scope — what gets shuffled) ----
    // `coupled` is fully DERIVED (= a shuffle on && !decoupled); apply_derived_rules
    // is authoritative, but we keep the struct live-consistent here so the canonical
    // is right even before the next normalize. The user never sees a `coupled` widget.
    ImGui::TextUnformatted("Shuffle"); ImGui::SameLine();
    bool cave = s->shuffle_cave_entrances != 0;
    if (ImGui::Checkbox("Caves", &cave)) {
      s->shuffle_cave_entrances = cave;
      if (!cave && !s->shuffle_dungeon_entrances) { s->decoupled = 0; s->cross_category = 0; }
      s->coupled = ((s->shuffle_cave_entrances || s->shuffle_dungeon_entrances) && !s->decoupled) ? 1 : 0;
      changed = true;
    }
    HelpTooltip("Each overworld cave door leads to a different cave interior. "
                "(Open/Standard only in this version.)");
    ImGui::SameLine();
    bool dun = s->shuffle_dungeon_entrances != 0;
    if (ImGui::Checkbox("Dungeons", &dun)) {
      s->shuffle_dungeon_entrances = dun;
      if (!dun) s->shuffle_ganons_tower_entrance = 0;
      if (!dun && !s->shuffle_cave_entrances) { s->decoupled = 0; s->cross_category = 0; }
      s->coupled = ((s->shuffle_cave_entrances || s->shuffle_dungeon_entrances) && !s->decoupled) ? 1 : 0;
      changed = true;
    }
    HelpTooltip("Shuffles 10 of 12 dungeons among themselves. Desert + Turtle Rock "
                "shuffle their MAIN door only; Skull Woods is deferred. Ganon's "
                "Tower is the advanced add-on to the right.");
    ImGui::SameLine();
    ImGui::BeginDisabled(!s->shuffle_dungeon_entrances);
    bool gt = s->shuffle_ganons_tower_entrance != 0;
    if (ImGui::Checkbox("+Ganon's Tower", &gt)) {
      s->shuffle_ganons_tower_entrance = gt;
      changed = true;
    }
    ImGui::EndDisabled();
    HelpTooltip("Adds Ganon's Tower to the dungeon pool (advanced). At high "
                "crystals.tower some seeds need generation retries (the generator "
                "never ships an unreachable seed - it rerolls). If a seed won't "
                "generate, lower crystals.tower (0 always works) or change seed.");

    // ---- Group 2: Exits (coupling — ONE choice, replaces the old two checkboxes) ----
    bool any_shuffle = (s->shuffle_cave_entrances || s->shuffle_dungeon_entrances);
    ImGui::BeginDisabled(!any_shuffle);
    ImGui::TextUnformatted("Exits  "); ImGui::SameLine();
    int exits_mode = s->decoupled ? 1 : 0;  // 0 = coupled, 1 = decoupled
    if (ImGui::RadioButton("Coupled", &exits_mode, 0)) {
      s->decoupled = 0;
      s->coupled = any_shuffle ? 1 : 0;
      changed = true;
    }
    HelpTooltip("Enter A -> exit A: leaving an interior returns you to the door you "
                "came in (the ALTTPR baseline).");
    ImGui::SameLine();
    if (ImGui::RadioButton("Decoupled", &exits_mode, 1)) {
      s->decoupled = 1;
      s->coupled = 0;
      s->cross_category = 0;  // one-way + cross-pool not supported (mutually exclusive)
      changed = true;
    }
    ImGui::EndDisabled();
    HelpTooltip("One-way warps: exiting drops you at a DIFFERENT door than you "
                "entered - applies to whatever is shuffled (caves and/or dungeons). "
                "Mutually exclusive with Cross-category. The generator keeps the "
                "goal reachable.");

    // ---- Group 3: Cross-category (pool mixing — coupled-only, needs both classes) ----
    bool both = (s->shuffle_cave_entrances && s->shuffle_dungeon_entrances);
    ImGui::BeginDisabled(!both || s->decoupled != 0);
    bool cross = s->cross_category != 0;
    if (ImGui::Checkbox("Cross-category (caves & dungeons share one pool)", &cross)) {
      s->cross_category = cross;
      changed = true;
    }
    ImGui::EndDisabled();
    HelpTooltip("A cave door can lead to a dungeon interior and vice versa. Needs "
                "both Caves and Dungeons on, with Coupled exits (not Decoupled). "
                "The generator rerolls until every location is reachable.");
    ImGui::EndDisabled();
    if (!ws_ok) {
      ImGui::TextDisabled("Entrance shuffle is Open/Standard only for now.");
    }
  }

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
    ImGui::Checkbox("Enemy shuffle", &off);
    ImGui::Checkbox("Glitches", &off);
  }
  ImGui::EndDisabled();
  HelpTooltip("not yet active in playable slots");

  if (changed) Pending_Changed();
}

static void Panel_Trackers() {
  ImGui::SeparatorText("Tracker windows");
  ImGui::TextWrapped(
      "Open the rich tracker windows. They stay open into gameplay and auto-"
      "update from live game state. You can also bind hotkeys "
      "(RandoItemTrackerWindow / RandoCheckTrackerWindow / RandoMapTrackerWindow) "
      "in zelda3.ini to toggle them during play.");
  ImGui::Spacing();
  struct { const char *label; int kind; } rows[] = {
      {"Item Tracker", kTracker_Item},
      {"Check Tracker", kTracker_Check},
      {"Map Tracker", kTracker_Map},
  };
  for (int i = 0; i < 3; i++) {
    bool shown = Trackers_IsShown(rows[i].kind);
    ImGui::Text("%-14s", rows[i].label);
    ImGui::SameLine();
    char btn[32];
    snprintf(btn, sizeof btn, "%s##trk%d", shown ? "Hide" : "Open", i);
    if (ImGui::Button(btn, ImVec2(80, 0)))
      Trackers_SetShown(rows[i].kind, !shown);
    ImGui::SameLine();
    ImGui::TextColored(shown ? ImVec4(0.4f, 0.9f, 0.4f, 1.0f) : ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                       shown ? "open" : "closed");
  }
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

// §14.4 "Save spoiler to file..." + §14.5 "Save spoiler to clipboard".
// SDL2 (2.x) has no portable native file dialog (SDL_ShowOpenFileDialog is SDL3),
// so per §14.4 we use a simple text-input path field. Both buttons route through
// RandoWindowBridge_WriteSpoilerFiles, a thin C wrapper that builds a RandoSpoiler
// from the bridge's generate-time snapshot and calls Spoiler_Write (the writer is
// NOT modified). The wrapper lives in the C bridge TU because rando_spoiler.h uses
// a C11 _Static_assert that is invalid in this C++ TU.
static void RenderSpoilerSaveRow() {
  static char s_save_path[512] = "spoiler.json";
  static char s_save_status[256];

  ImGui::SetNextItemWidth(360.0f);
  ImGui::InputText("##spoiler_path", s_save_path, sizeof s_save_path);
  ImGui::SameLine();
  if (ImGui::Button("Save spoiler to file...")) {
    s_save_status[0] = '\0';
    // Derive a .txt companion path next to the chosen .json (replace a trailing
    // ".json", else append ".txt"). The viewer is non-race-only, so Spoiler_Write
    // emits the .txt companion too.
    char txt_path[520];
    snprintf(txt_path, sizeof txt_path, "%s", s_save_path);
    size_t plen = strlen(txt_path);
    if (plen >= 5 && strcmp(txt_path + plen - 5, ".json") == 0)
      snprintf(txt_path + plen - 5, sizeof txt_path - (plen - 5), ".txt");
    else
      snprintf(txt_path + plen, sizeof txt_path - plen, ".txt");
    if (RandoWindowBridge_WriteSpoilerFiles(s_save_path, txt_path))
      snprintf(s_save_status, sizeof s_save_status, "Saved to %s (+ .txt).", s_save_path);
    else
      snprintf(s_save_status, sizeof s_save_status, "Save failed: could not write %s.", s_save_path);
  }
  ImGui::SameLine();
  if (ImGui::Button("Save spoiler to clipboard")) {
    s_save_status[0] = '\0';
    // Spoiler_Write is file-only, so write JSON to a temp file, read it back, and
    // push the text to the clipboard (per §14.5 — do NOT modify the writer).
    char tmp_path[1024];
    char *base = SDL_GetPrefPath("zelda3", "rando");
    if (base && base[0]) {
      snprintf(tmp_path, sizeof tmp_path, "%sspoiler_clip.json", base);
    } else {
      snprintf(tmp_path, sizeof tmp_path, "spoiler_clip.json");
    }
    if (base) SDL_free(base);
    // txt_path = NULL skips the companion; clipboard wants only the JSON text.
    if (RandoWindowBridge_WriteSpoilerFiles(tmp_path, nullptr)) {
      FILE *f = fopen(tmp_path, "rb");
      if (f) {
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (sz > 0 && sz < 8 * 1024 * 1024) {
          char *buf = (char *)malloc((size_t)sz + 1);
          if (buf) {
            size_t rd = fread(buf, 1, (size_t)sz, f);
            buf[rd] = '\0';
            SDL_SetClipboardText(buf);
            free(buf);
            snprintf(s_save_status, sizeof s_save_status, "Copied spoiler JSON to clipboard.");
          } else {
            snprintf(s_save_status, sizeof s_save_status, "Clipboard copy failed: out of memory.");
          }
        } else {
          snprintf(s_save_status, sizeof s_save_status, "Clipboard copy failed: bad temp file size.");
        }
        fclose(f);
      } else {
        snprintf(s_save_status, sizeof s_save_status, "Clipboard copy failed: temp file unreadable.");
      }
      remove(tmp_path);
    } else {
      snprintf(s_save_status, sizeof s_save_status, "Clipboard copy failed: could not write temp file.");
    }
  }
  if (s_save_status[0])
    ImGui::TextColored(ImVec4(0.8f, 0.9f, 0.8f, 1.0f), "%s", s_save_status);
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

  // Save-spoiler controls (§14.4 file, §14.5 clipboard).
  RenderSpoilerSaveRow();
  ImGui::Separator();

  // Build (region_id, location_id, item_id) rows, then sort by (region, loc)
  // so a stable per-region grouping falls out, mirroring rando_spoiler.c's text
  // writer. The per-region tables below get their OWN interactive sort.
  uint16 n = t->count;
  // Display cap, comfortably above any plausible location count
  // (kRandoLocationsCount == 266 today; placements can't exceed it). If a
  // future location-set growth ever exceeds this, we surface a visible note
  // below rather than silently dropping rows.
  enum { kSpoilerMaxRows = 1024 };
  bool truncated = false;
  static struct Row { uint16 region_id; uint16 location_id; uint16 item_id; } rows[kSpoilerMaxRows];
  if (n > kSpoilerMaxRows) { n = kSpoilerMaxRows; truncated = true; }
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

  // Render one collapsing header per region (§14.3 "per-region collapse header"),
  // each owning a sortable two-column table (§14.3 "sortable columns"). Cells are
  // pure text (never editable); the whole cell-render path is bracketed in
  // BeginDisabled/EndDisabled so it reads unambiguously as read-only (§14.6) while
  // the header expand/collapse and the column sort stay interactive (navigation,
  // not editing).
  if (truncated)
    ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.2f, 1.0f),
                       "Note: showing the first %d of %u placements (display cap).",
                       (int)kSpoilerMaxRows, (unsigned)t->count);

  for (uint16 start = 0; start < n;) {
    uint16 region = rows[start].region_id;
    uint16 end = start;
    while (end < n && rows[end].region_id == region) end++;

    char header[160];
    snprintf(header, sizeof header, "%s (%u)###region_%u",
             Rando_GetRegionName(region), (unsigned)(end - start), (unsigned)region);
    if (ImGui::CollapsingHeader(header, ImGuiTreeNodeFlags_DefaultOpen)) {
      char table_id[32];
      snprintf(table_id, sizeof table_id, "##sp_%u", (unsigned)region);
      if (ImGui::BeginTable(table_id, 2,
                            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                ImGuiTableFlags_Resizable | ImGuiTableFlags_Sortable)) {
        ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_DefaultSort);
        ImGui::TableSetupColumn("Item");
        ImGui::TableHeadersRow();

        // Local index list into rows[start..end) so we can re-order per the
        // sort spec without disturbing the region grouping.
        int idx[kSpoilerMaxRows];
        int m = 0;
        for (uint16 i = start; i < end && m < kSpoilerMaxRows; i++) idx[m++] = i;

        if (ImGuiTableSortSpecs *specs = ImGui::TableGetSortSpecs()) {
          if (specs->SpecsCount > 0) {
            int col = specs->Specs[0].ColumnIndex;
            bool asc = specs->Specs[0].SortDirection == ImGuiSortDirection_Ascending;
            // Insertion sort by the chosen column's display name (small N).
            for (int a = 1; a < m; a++) {
              int v = idx[a]; int bb = a;
              const char *va = (col == 0) ? Rando_GetLocationName(rows[v].location_id)
                                          : Rando_GetItemName(rows[v].item_id);
              while (bb > 0) {
                int u = idx[bb - 1];
                const char *vu = (col == 0) ? Rando_GetLocationName(rows[u].location_id)
                                            : Rando_GetItemName(rows[u].item_id);
                int cmp = strcmp(vu, va);
                if (asc ? (cmp > 0) : (cmp < 0)) { idx[bb] = idx[bb - 1]; bb--; }
                else break;
              }
              idx[bb] = v;
            }
          }
        }

        ImGui::BeginDisabled();  // §14.6 — cells are read-only.
        for (int k = 0; k < m; k++) {
          const Row *r = &rows[idx[k]];
          ImGui::TableNextRow();
          ImGui::TableSetColumnIndex(0);
          ImGui::TextUnformatted(Rando_GetLocationName(r->location_id));
          ImGui::TableSetColumnIndex(1);
          ImGui::TextUnformatted(Rando_GetItemName(r->item_id));
        }
        ImGui::EndDisabled();

        ImGui::EndTable();
      }
    }
    start = end;
  }
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
        // §13.7 — route to the existing file-select load path for the just-created
        // slot. Loading touches g_ram/WRAM, so it must run on the game thread:
        // raise a load request the per-frame consumer in main.c honors via
        // SelectFile_LoadRandoSlot(). The game is sitting in Module01_FileSelect
        // right now (where the in-game occupied-slot load path runs), so the load
        // takes effect cleanly. RandoWindow_Hide() clears the kind-toggle target,
        // but the load slot is captured in the request before hiding.
        RandoWindowBridge_RequestLoad(b->target_slot_index);
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
  // Don't let ImGui auto-write its own imgui.ini in the user's run dir — our
  // single full-window panel has no layout worth persisting, and window geometry
  // is persisted via SDL through saves/rando_window.ini (P5).
  ImGui::GetIO().IniFilename = nullptr;
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

  // §21.3 — verify the race-mode Spoiler-tab gate once at init.
  RandoWindow_TabSelfCheck();
}

// ---- Tab list builder (§21.3 race-mode gate) -------------------------------
// Single source of truth for which tabs the window shows. The "Spoiler" tab is
// OMITTED when the last generation was race-mode (bridge.last_generated_race_mode).
// The gate keys off what was LAST GENERATED, never pending.race_mode (§14.1).
// Returns the count; fills `out_tabs` (capacity `cap`) with stable string ptrs.
static const char *const kTab_General         = "General";
static const char *const kTab_Dungeons        = "Dungeons";
static const char *const kTab_Shuffles        = "Shuffles";
static const char *const kTab_QualityOfLife   = "Seed QoL";
static const char *const kTab_Trackers        = "Trackers";
static const char *const kTab_AssetHash       = "Asset Hash";
static const char *const kTab_Spoiler         = "Spoiler";

static int RandoWindow_BuildTabList(bool last_generated_race_mode,
                                    const char **out_tabs, int cap) {
  int n = 0;
  const char *base[] = { kTab_General, kTab_Dungeons, kTab_Shuffles,
                         kTab_QualityOfLife, kTab_Trackers, kTab_AssetHash };
  for (size_t i = 0; i < sizeof base / sizeof base[0]; i++)
    if (n < cap) out_tabs[n++] = base[i];
  // Spoiler tab: visible only when the last generation was NOT race-mode.
  if (!last_generated_race_mode && n < cap)
    out_tabs[n++] = kTab_Spoiler;
  return n;
}

// §21.3 regression self-check: assert the tab-list builder OMITS "Spoiler" when
// last_generated_race_mode == true, and INCLUDES it otherwise. Runs once at
// init (matches the Settings/Placement_SelfCheck "exit nonzero on failure"
// pattern). Catches a future refactor that breaks the race-mode gate.
static bool TabListContains(const char **tabs, int n, const char *name) {
  for (int i = 0; i < n; i++) if (tabs[i] == name) return true;
  return false;
}
static void RandoWindow_TabSelfCheck(void) {
  const char *tabs[8];
  int n = RandoWindow_BuildTabList(/*race_mode=*/true, tabs, 8);
  if (TabListContains(tabs, n, kTab_Spoiler)) {
    fprintf(stderr,
            "[rando_window] SELF-CHECK FAILED: Spoiler tab present under "
            "race-mode (must be omitted). (§21.3)\n");
    exit(2);
  }
  n = RandoWindow_BuildTabList(/*race_mode=*/false, tabs, 8);
  if (!TabListContains(tabs, n, kTab_Spoiler)) {
    fprintf(stderr,
            "[rando_window] SELF-CHECK FAILED: Spoiler tab missing for a "
            "non-race generation (must be present). (§21.3)\n");
    exit(2);
  }
}

void RandoWindow_ProcessEvent(const void *sdl_event) {
  const SDL_Event *e = (const SDL_Event *)sdl_event;
  // Keyboard binding capture intercepts the next non-modifier keydown while a
  // rebind is armed (the settings window has focus, so this keydown carries its
  // windowID). Escape cancels. Pure modifier keys are forwarded so ImGui's
  // modifier state stays consistent and the user can build a modified binding.
  if (GameConfig_WantsKeyCapture() && e->type == SDL_KEYDOWN) {
    SDL_Keycode sym = e->key.keysym.sym;
    if (sym == SDLK_ESCAPE) { GameConfig_CancelCapture(); return; }
    if (sym == SDLK_LSHIFT || sym == SDLK_RSHIFT || sym == SDLK_LCTRL || sym == SDLK_RCTRL ||
        sym == SDLK_LALT || sym == SDLK_RALT || sym == SDLK_LGUI || sym == SDLK_RGUI) {
      ImGui_ImplSDL2_ProcessEvent(e);
      return;
    }
    GameConfig_FeedCapturedKey((int)sym, (int)e->key.keysym.mod);
    return;  // consume the terminating keydown (its keyup is forwarded normally)
  }
  ImGui_ImplSDL2_ProcessEvent(e);
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
    // The visible tab set comes from the single-source-of-truth builder so the
    // race-mode Spoiler gate (§21.3) can be self-checked independently of render.
    const char *tabs[8];
    int ntabs = RandoWindow_BuildTabList(b->last_generated_race_mode, tabs, 8);
    // Two top-level tabs: "Game Settings" (the native game-config panels) and
    // "Randomizer" (all the rando panels nested under one tab, so the top level
    // stays clean and symmetric). Game Settings is first so the config-mode open
    // path (RandoWindow_ToggleConfig) defaults here; the kind-toggle entry opens
    // straight to the Randomizer tab via s_select_general_once.
    if (ImGui::BeginTabBar("##z3r_tabs")) {
      ImGuiTabItemFlags gsflags = s_select_game_settings_once ? ImGuiTabItemFlags_SetSelected : 0;
      if (ImGui::BeginTabItem("Game Settings", nullptr, gsflags)) {
        GameConfig_RenderTab();
        ImGui::EndTabItem();
      }
      s_select_game_settings_once = false;

      ImGuiTabItemFlags rflags = s_select_general_once ? ImGuiTabItemFlags_SetSelected : 0;
      if (ImGui::BeginTabItem("Randomizer", nullptr, rflags)) {
        if (ImGui::BeginTabBar("##rando_tabs")) {
          for (int i = 0; i < ntabs; i++) {
            ImGuiTabItemFlags tflags =
                (s_select_general_once && tabs[i] == kTab_General) ? ImGuiTabItemFlags_SetSelected : 0;
            if (ImGui::BeginTabItem(tabs[i], nullptr, tflags)) {
              if (tabs[i] == kTab_General)            Panel_General();
              else if (tabs[i] == kTab_Dungeons)      Panel_Dungeons();
              else if (tabs[i] == kTab_Shuffles)      Panel_Shuffles();
              else if (tabs[i] == kTab_QualityOfLife) Panel_RecommendedFeatures();
              else if (tabs[i] == kTab_Trackers)      Panel_Trackers();
              else if (tabs[i] == kTab_AssetHash)     Panel_AssetHash();
              else if (tabs[i] == kTab_Spoiler)       Panel_Spoiler();
              ImGui::EndTabItem();
            }
          }
          // Read-only logic views (their own files).
          if (ImGui::BeginTabItem("Reachability")) { RandoReach_Render(); ImGui::EndTabItem(); }
          if (ImGui::BeginTabItem("Hints"))        { RandoHints_Render(); ImGui::EndTabItem(); }
          ImGui::EndTabBar();
        }
        // Generate flow lives inside the Randomizer tab, below its sub-tabs, but
        // only when a slot is targeted (the kind-toggle "New Randomizer" entry).
        // In config-mode opens (target_slot_index < 0) it stays hidden.
        if (b->target_slot_index >= 0)
          RenderGenerateRow();
        ImGui::EndTabItem();
      }
      s_select_general_once = false;

      // Debug — live inventory/equipment editor (writes g_ram directly; gated to
      // in-game, non-replay, non-emulator-attached).
      if (ImGui::BeginTabItem("Debug")) {
        GameDebug_RenderTab();
        ImGui::EndTabItem();
      }
      ImGui::EndTabBar();
    }
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
  // Free the bridge's owned spoiler-viewer placement copy (passing NULL frees +
  // clears without re-storing) so it isn't leaked at process exit. (audit LOW)
  RandoWindowBridge_StoreGenerated(NULL, NULL, false);
}

// ---- Show / hide -----------------------------------------------------------
void RandoWindow_OpenForNewSlot(int slot_index) {
  g_rando_window_bridge.target_slot_index = slot_index;
  // Snapshot the LIVE config features0 into the bridge so the
  // recommended-features panel starts from the user's current configuration.
  // OpenForNewSlot runs on the game thread (kind-toggle entry), so reading
  // g_config here is safe and current. The UI edits only this snapshot; the game
  // thread applies it back to g_config.features0 inside the generate consumer.
  g_rando_window_bridge.pending_recommended_features0 = g_config.features0;
  // Fresh random seed every time the window opens for a new slot, so "start a
  // new seed" defaults to a NEW random seed rather than reusing the last one
  // (the user can still type a specific seed or paste a share string, which
  // overwrites this). Settings persist across opens; the seed does not.
  g_rando_window_bridge.seed_u64 = RollRandomSeed();
  RandoWindowBridge_RecomputeDerived();  // refresh share string for the new seed
  s_select_general_once = true;  // open to the rando "General" tab, not Game Settings
  if (s_settings_window) {
    SDL_ShowWindow(s_settings_window);
    SDL_RaiseWindow(s_settings_window);
  }
  s_wants_shown = true;
}

// Open (or close) the window in CONFIG mode — a pure game-settings surface with
// no randomizer slot targeted (so the generate row is hidden). Toggles.
void RandoWindow_ToggleConfig(void) {
  if (s_wants_shown) { RandoWindow_Hide(); return; }
  // Ensure no slot is targeted so the window is config-only (cancels a stale
  // kind-toggle target left from a prior, hidden, rando open).
  if (g_rando_window_bridge.target_slot_index >= 0)
    RandoWindowBridge_CancelTarget();
  GameConfig_NotifyWindowOpened();   // sync the panels to the live config
  s_select_game_settings_once = true;
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
  GameConfig_CancelCapture();  // a hidden window can't complete a rebind capture
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
