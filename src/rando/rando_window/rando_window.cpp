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
#include "file_dialog.h"          // FileDialog::OpenFile (customizer "Browse..." button)
// kFeatures0_* recommended-features bit constants (compile-time enums; no g_ram
// access — this TU never invokes the enhanced_features0 macro that writes g_ram).
#include "../../features.h"

// Game-side C APIs. rando_window_bridge.h already pulls rando_settings.h /
// rando_share.h / rando_placement.h in under extern "C"; the rest are added
// here (also under extern "C" so the C++ linker resolves the C symbols).
extern "C" {
#include "../rando.h"            // Rando_IsActive / Rando_ActiveSlotHidesSpoiler /
                                 // Rando_RevealActiveSlotSpoiler / Rando_CanRevealActiveSlotSpoiler /
                                 // Rando_RevealResultDescription + g_assets_hash. (rando.h's lone
                                 // _Static_assert is C++-guarded — cf. tracker_windows.cpp.)
#include "../rando_asset_decisions.h"  // AssetDecision_FindAllow / _Persist
#include "../rando_logic.h"      // Rando_GetRegionName/LocationName/ItemName, kRandoLocations
#include "../item_ids.h"         // ITEM_Nothing (empty-pot filler omitted from the spoiler list)
#include "../vanilla_assets_hash.h"  // kVanillaAssetsHash, kVanillaAssetsHashKnown
#include "../../config.h"        // g_config (R2: snapshot features0 at open), g_rando_window_prefs
#include "../auto_tracker.h"     // AutoTracker_IsRunning/SetEnabled/GetClientCount/GetBindInfo
#include "../customizer.h"       // Customizer_LoadFile/_Install (Seed Tools manifest picker)
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

// "Randomize seed each generate": when set, TryBeginGenerate() rolls a fresh
// random seed just before requesting generation, so the common flow (tweak
// settings, hit Generate, repeat) gets a new seed every time without the user
// clicking "New random seed" first. Default ON. The checkbox sits in the Seed
// section (Panel_General) and, while on, DISABLES the manual seed input/roll so
// it's obvious the user isn't choosing the seed. Pasting a share string clears
// this (RenderShareRow) so the pasted seed is adopted and the field re-enables.
static bool s_randomize_seed_each_generate = true;

// Customizer manifest state (add-rando-customizer-mode §6.2). The manifest the
// placer consults is a BORROWED pointer (Customizer_Install does not copy), so
// the storage lives here for the whole session. SESSION-ONLY by design: the
// path/loaded state is not persisted, and the startup settings-restore
// (main.c) clears a persisted customizer_active bit so a stale flag can never
// block Generate with no visible cause after a restart.
static CustomizerManifest s_customizer_manifest;
static char s_customizer_path[512];
static bool s_customizer_loaded = false;
static char s_customizer_status[320];  // post-Load summary or error

// Seed-shape filter UI state. Session-only and noncanonical: it chooses which
// candidate seed is accepted, but the accepted seed/settings still reproduce
// through the normal share string.
static bool s_shape_enabled = false;
static int s_shape_search_limit = 100;
static int s_shape_length_mode = 0;  // 0 any, 1 short, 2 long
static bool s_shape_no_unreachable = false;
static bool s_shape_no_forward_fill = false;
static bool s_shape_early_boots = false;
static bool s_shape_early_flute = false;
static bool s_shape_early_mirror = false;
static bool s_shape_early_hookshot = false;
static bool s_shape_early_lamp = false;
static char s_shape_custom_tokens[256];

// Uninstall the manifest + drop the working flag (toggle-off, window close for
// a new slot, or a failed Load). Keeps the typed path for convenience.
static void CustomizerUi_Clear(RandoSettings *s) {
  Customizer_Install(nullptr);
  s_customizer_loaded = false;
  s_customizer_status[0] = '\0';
  if (s != nullptr) s->customizer_active = 0;
}

static void ShapeUi_AppendToken(char *out, size_t out_cap, const char *token) {
  if (out == nullptr || out_cap == 0 || token == nullptr || token[0] == '\0') return;
  size_t used = strlen(out);
  if (used + 1 >= out_cap) return;
  if (used > 0) {
    snprintf(out + used, out_cap - used, ",");
    used = strlen(out);
  }
  if (used + 1 < out_cap)
    snprintf(out + used, out_cap - used, "%s", token);
}

static void ShapeUi_ClearFilter(void) {
  s_shape_enabled = false;
  s_shape_search_limit = 100;
  s_shape_length_mode = 0;
  s_shape_no_unreachable = false;
  s_shape_no_forward_fill = false;
  s_shape_early_boots = false;
  s_shape_early_flute = false;
  s_shape_early_mirror = false;
  s_shape_early_hookshot = false;
  s_shape_early_lamp = false;
  s_shape_custom_tokens[0] = '\0';
  RandoWindowBridge *b = &g_rando_window_bridge;
  b->shape_filter_enabled = false;
  b->shape_filter_valid = true;
  b->shape_search_limit = 100;
  b->shape_filter_desc[0] = '\0';
  b->shape_filter_error[0] = '\0';
  memset(&b->shape_filter, 0, sizeof b->shape_filter);
}

static void ShapeUi_RebuildBridge(void) {
  RandoWindowBridge *b = &g_rando_window_bridge;
  if (s_shape_search_limit < 1) s_shape_search_limit = 1;
  if (s_shape_search_limit > 500) s_shape_search_limit = 500;
  b->shape_filter_enabled = s_shape_enabled;
  b->shape_search_limit = s_shape_search_limit;
  b->shape_filter_desc[0] = '\0';
  b->shape_filter_error[0] = '\0';
  memset(&b->shape_filter, 0, sizeof b->shape_filter);
  if (!s_shape_enabled) {
    b->shape_filter_valid = true;
    return;
  }

  char tokens[512];
  tokens[0] = '\0';
  if (s_shape_length_mode == 1) ShapeUi_AppendToken(tokens, sizeof tokens, "short");
  else if (s_shape_length_mode == 2) ShapeUi_AppendToken(tokens, sizeof tokens, "long");
  if (s_shape_no_unreachable) ShapeUi_AppendToken(tokens, sizeof tokens, "no_unreachable");
  if (s_shape_no_forward_fill) ShapeUi_AppendToken(tokens, sizeof tokens, "no_forward_fill");
  if (s_shape_early_boots) ShapeUi_AppendToken(tokens, sizeof tokens, "early_boots");
  if (s_shape_early_flute) ShapeUi_AppendToken(tokens, sizeof tokens, "early_flute");
  if (s_shape_early_mirror) ShapeUi_AppendToken(tokens, sizeof tokens, "early_mirror");
  if (s_shape_early_hookshot) ShapeUi_AppendToken(tokens, sizeof tokens, "early_hookshot");
  if (s_shape_early_lamp) ShapeUi_AppendToken(tokens, sizeof tokens, "early_lamp");
  ShapeUi_AppendToken(tokens, sizeof tokens, s_shape_custom_tokens);

  if (tokens[0] == '\0') {
    b->shape_filter_valid = false;
    snprintf(b->shape_filter_error, sizeof b->shape_filter_error,
             "Pick at least one seed-shape criterion or turn the filter off.");
    return;
  }
  char err[160];
  if (SeedShape_Parse(tokens, &b->shape_filter, err, sizeof err) != 0) {
    b->shape_filter_valid = false;
    snprintf(b->shape_filter_error, sizeof b->shape_filter_error, "%s", err);
    return;
  }
  if (!b->shape_filter.enabled) {
    b->shape_filter_valid = false;
    snprintf(b->shape_filter_error, sizeof b->shape_filter_error,
             "Pick at least one seed-shape criterion or turn the filter off.");
    return;
  }
  b->shape_filter_valid = true;
  SeedShape_Describe(&b->shape_filter, b->shape_filter_desc,
                     sizeof b->shape_filter_desc);
}

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
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
    // Wrap at ~30 character-widths so long tooltips stay on-screen instead of
    // clipping off the right edge (SetTooltip renders a single unwrapped line).
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
  }
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
// mode_weapons: randomized=0, assured=1, swordless=3 are exposed. vanilla=2 stays
// reserved (out of scope), so the combo maps display rows to enum values
// NON-contiguously (can't use the index==value EnumCombo helper).
static const struct { uint8 value; const char *label; } kModeWeaponsOptions[] = {
    {0, "randomized"}, {1, "assured"}, {3, "swordless"}};
// Accessibility — full ALTTPR three-way (index == enum value):
//   [0] items        = kAccessibility_Items     ("100% Inventory")
//   [1] locations    = kAccessibility_Locations ("100% Locations")
//   [2] beatable only = kAccessibility_None      (ALTTPR "Not Guaranteed")
// All three guarantee the seed is beatable; they differ in how much extra
// reachability is required (locations = every location; items = every
// progression item; beatable only = goal only).
static const char *const kAccessibilityLabels[] = {"items", "locations", "beatable only"};
// Phase B — glitch logic level (index == settings.logic enum value). The UI
// offers the three shipped tiers; HybridMG(3)/NoLogic(4) are Phase D and not
// selectable here. Per add-rando-trick-logic-and-axes §3.3.
static const char *const kLogicLabels[] = {"NoGlitches", "OverworldGlitches", "MajorGlitches",
                                           "HybridMajorGlitches", "NoLogic"};
static const char *const kTrapFrequencyLabels[] = {"off", "low", "medium", "high", "insanity"};
// add-rando-pot-sanity — pot_shuffle tiers (index == enum value, matches the
// parse_pot_shuffle CLI grammar off|keys|contents|all). The reserved Subset
// value (4) is Phase 7 and not offered here.
static const char *const kPotShuffleLabels[] = {"off", "keys", "contents", "all"};
// add-rando-enemy-drop-sanity — enemy_drop_checks tiers.
static const char *const kEnemyDropCheckLabels[] = {"off", "keys", "dungeon"};
// Phase B tricks (multi-select bitmask; index == settings.tricks bit). Mirrors
// the kTrickNames table in rando_settings.c + op_registry.yaml `tricks:`. The
// three `false`-wired bits (bomb-jump/hookshot-clip/lobotomy) are fork-invented
// placeholders with NO logic gates — toggling them changes nothing.
static const char *const kTrickUiNames[] = {
    "boots-clip", "fake-flippers", "bunny-revival", "dark-room-nav",
    "bomb-jump", "pearl-bypass", "hookshot-clip", "lobotomy"};
static const bool kTrickWired[] = {true, true, true, true, false, true, false, false};

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

static void ApplyDungeonKeysWildMapsPreset(RandoSettings *s) {
  s->dungeon_small_keys_mode = kDungeonItemMode_Dungeon;
  s->dungeon_big_keys_mode = kDungeonItemMode_Dungeon;
  s->dungeon_maps_mode = kDungeonItemMode_Wild;
  s->dungeon_compasses_mode = kDungeonItemMode_Wild;
}

static void ApplyOpenFastGanonCorePreset(RandoSettings *s) {
  s->world_state = kWorldState_Open;
  s->goal = kGoal_FastGanon;
  s->crystals_ganon = 7;
  s->crystals_tower = 7;
  s->item_pool_difficulty = kItemPoolDifficulty_Normal;
  s->mode_weapons = kModeWeapons_Randomized;
  s->accessibility = kAccessibility_Items;
  s->logic = 0;
  s->tricks = 0;
  s->pieces_required = 20;
  s->pieces_placed = 30;
}

static void ApplyRaceSafePreset(RandoSettings *s) {
  s->race_mode = 1;
  s->hints = 1;
  s->item_pool_difficulty = kItemPoolDifficulty_Normal;
  s->customizer_active = 0;  // Race mode refuses customizer manifests.
  ApplyDungeonKeysWildMapsPreset(s);
}

// ===========================================================================
// Panels
// ===========================================================================

// Recommended-features opt-in (C10 / spec "Recommended-features opt-in renders
// in the native window on PC"). Starts from the in-game kRecRowBits[]/
// kRecRowLabels[] set (select_file.c), with PC Seed-QoL extras that do not fit
// the old fixed-height SNES panel. Edits ONLY bridge.pending_recommended_features0;
// the game thread applies it to g_config.features0 inside the generate consumer.
// features0 is NOT part of settings_hash — toggling these does not change the
// hash/share.
static const uint32 kRecBits[] = {
    kFeatures0_SkipIntroOnKeypress, kFeatures0_ShowMaxItemsInYellow,
    kFeatures0_TurnWhileDashing,    kFeatures0_CollectItemsWithSword,
    kFeatures0_BreakPotsWithSword,  kFeatures0_DisableLowHealthBeep,
    kFeatures0_CarryMoreRupees,     kFeatures0_MiscBugFixes,
    kFeatures0_GameChangingBugFixes, kFeatures0_RestoreJpGlitches,
    kFeatures0_DimFlashes,
};
static const char *const kRecLabels[] = {
    "Skip intro on keypress",       "Show max items in yellow",
    "Turn while dashing",           "Collect items with sword",
    "Break pots with sword",        "Disable low-health beep",
    "Carry more rupees",            "Misc bug fixes",
    "Game-changing bug fixes",      "Restore JP 1.0 glitches",
    "Dim flashes",
};
// Recommended ON set (mirrors kRecRecommendedOn[] for the shared rows). Gameplay
// behavior changes, JP glitches, and Dim are off by default; glitch-logic seeds
// still force JP glitches on at generation/slot-load time.
static const uint8 kRecOn[] = {1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0};
static const int kRecCount = (int)(sizeof(kRecBits) / sizeof(kRecBits[0]));

static void Panel_RecommendedFeatures() {
  RandoWindowBridge *b = &g_rando_window_bridge;
  RandoSettings *s = &b->pending;
  uint32 *f = &b->pending_recommended_features0;
  ImGui::SeparatorText("Seed rules");
  bool instant_flute = s->instant_flute != 0;
  if (ImGui::Checkbox("Instant flute activation", &instant_flute)) {
    s->instant_flute = instant_flute ? 1 : 0;
    Pending_Changed();
  }
  HelpTooltip("When you receive the flute, it is immediately treated as bird-woken. "
              "Turning this off restores the old play-for-the-bird activation route. "
              "This changes settings_hash and the share string.");
  ImGui::Spacing();

  ImGui::SeparatorText("Gameplay features");
  ImGui::TextWrapped(
      "These features ride along with the generated slot and are NOT part of the "
      "settings hash or share string - toggling them won't change the seed's identity. "
      "(The same features for normal play live under Game Settings -> "
      "Gameplay; this panel only sets what gets stamped into this seed.)");
  ImGui::Spacing();
  for (int i = 0; i < kRecCount; i++) {
    bool force_jp_glitches =
        kRecBits[i] == kFeatures0_RestoreJpGlitches &&
        Rando_SettingsAssumeJpGlitches(&b->pending);
    bool on = force_jp_glitches || ((*f & kRecBits[i]) != 0);
    if (force_jp_glitches) ImGui::BeginDisabled();
    bool clicked = ImGui::Checkbox(kRecLabels[i], &on);
    if (force_jp_glitches) ImGui::EndDisabled();
    if (!force_jp_glitches && clicked) {
      if (on) *f |= kRecBits[i]; else *f &= ~kRecBits[i];
    }
    if (kRecBits[i] == kFeatures0_RestoreJpGlitches) {
      if (force_jp_glitches) {
        ImGui::SameLine();
        ImGui::TextDisabled("(forced by glitch logic)");
      }
      HelpTooltip(force_jp_glitches
          ? "Forced on because this seed's logic or tricks assume restored JP 1.0 glitches."
          : "Per-slot opt-in for restored JP 1.0 gameplay glitches such as Fake Flippers, Itemdash, Spindash, and Superspeed. Does not enable JP 1.0 overworld music.");
    }
  }
  ImGui::Spacing();
  if (ImGui::Button("Apply recommended set")) {
    for (int i = 0; i < kRecCount; i++) {
      if (kRecOn[i]) *f |= kRecBits[i]; else *f &= ~kRecBits[i];
    }
  }
  HelpTooltip("Sets every Seed QoL toggle to its recommended on/off state.");
  ImGui::TextDisabled("Does not affect settings_hash or the share string.");
}

static bool Panel_CustomizerSection(RandoSettings *s) {
  bool changed = false;

  // ---- Customizer (add-rando-customizer-mode §6.2) ----
  // Manifest text-entry + Load button (same shape as the spoiler save path:
  // SDL2 has no portable native file dialog). The loaded manifest is installed
  // for the placer; validation (RandoWindowBridge_Validate) blocks Generate
  // while the toggle is on with nothing loaded.
  ImGui::SeparatorText("Customizer");
  bool cz = s->customizer_active != 0;
  if (ImGui::Checkbox("Customizer mode (manual placements)", &cz)) {
    if (cz) {
      s->customizer_active = 1;
    } else {
      CustomizerUi_Clear(s);  // spec: disabling clears the manifest reference
    }
    changed = true;
  }
  HelpTooltip("Pin chosen locations to chosen items from a manifest file; "
              "the normal fill places everything else.");
  if (cz) {
    ImGui::InputTextWithHint("##customizer_path",
                             "path to manifest .yaml (see assets/rando/customizer.example.yaml)",
                             s_customizer_path, sizeof s_customizer_path);
    // Browse... opens the OS-native file picker; on a successful pick it fills
    // the path field and auto-loads (one-click convenience). The text field
    // remains a fallback when no native dialog backend is available.
    bool do_load = false;
    ImGui::SameLine();
    if (ImGui::Button("Browse...")) {
      char picked[sizeof s_customizer_path];
      if (FileDialog::OpenFile("Select customizer manifest", s_customizer_path,
                               "YAML manifest", "*.yaml *.yml",
                               picked, sizeof picked)) {
        snprintf(s_customizer_path, sizeof s_customizer_path, "%s", picked);
        do_load = true;
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Load manifest")) do_load = true;
    if (do_load) {
      char lerr[200];
      CustomizerManifest parsed;
      if (Customizer_LoadFile(s_customizer_path, &parsed, lerr, sizeof lerr) != 0) {
        CustomizerUi_Clear(nullptr);  // uninstall any prior manifest; keep the toggle on
        snprintf(s_customizer_status, sizeof s_customizer_status, "Load failed: %s", lerr);
      } else if (parsed.pin_count == 0 && parsed.pool_add_count == 0 &&
                 parsed.pool_remove_count == 0) {
        // Mirror the CLI's empty-manifest rejection.
        CustomizerUi_Clear(nullptr);
        snprintf(s_customizer_status, sizeof s_customizer_status,
                 "Load failed: manifest contains no placements or pool_overrides.");
      } else {
        s_customizer_manifest = parsed;
        Customizer_Install(&s_customizer_manifest);
        s_customizer_loaded = true;
        int n = snprintf(s_customizer_status, sizeof s_customizer_status,
                         "Loaded: %u placement(s) pinned",
                         (unsigned)s_customizer_manifest.pin_count);
        if (n > 0 && (s_customizer_manifest.pool_add_count ||
                      s_customizer_manifest.pool_remove_count)) {
          snprintf(s_customizer_status + n, sizeof s_customizer_status - (size_t)n,
                   ", pool +%u/-%u",
                   (unsigned)s_customizer_manifest.pool_add_count,
                   (unsigned)s_customizer_manifest.pool_remove_count);
        }
      }
      changed = true;  // settings_hash unchanged, but revalidate the Generate row
    }
    if (s_customizer_status[0]) {
      bool is_err = !s_customizer_loaded;
      ImGui::TextColored(is_err ? ImVec4(0.95f, 0.35f, 0.35f, 1.0f)
                                : ImVec4(0.4f, 0.85f, 0.4f, 1.0f),
                         "%s", s_customizer_status);
    }
    // Per-pin preview so the user can eyeball what will be pinned (capped to
    // keep the panel compact; the spoiler shows the full result).
    if (s_customizer_loaded && ImGui::TreeNode("Pinned placements")) {
      const int kPreviewCap = 24;
      int shown = (s_customizer_manifest.pin_count < kPreviewCap)
                      ? s_customizer_manifest.pin_count : kPreviewCap;
      for (int i = 0; i < shown; i++) {
        ImGui::BulletText("%s: %s",
                          Rando_GetLocationName(s_customizer_manifest.pins[i].location_id),
                          Rando_GetItemName(s_customizer_manifest.pins[i].item_id));
      }
      if (s_customizer_manifest.pin_count > shown)
        ImGui::TextDisabled("... and %u more",
                            (unsigned)(s_customizer_manifest.pin_count - shown));
      ImGui::TreePop();
    }
  }

  return changed;
}

static void Panel_General() {
  RandoWindowBridge *b = &g_rando_window_bridge;
  RandoSettings *s = &b->pending;
  bool changed = false;

  // ---- Race-mode spoiler reveal (active slot) -----------------------------
  // Deferred in-binary UI from add-rando-race-mode-reveal-ui (§1). Shown only
  // for an ACTIVE race-mode slot — gated on Rando_ActiveSlotHidesSpoiler(), the
  // fail-closed helper every spoiler/hint UI must use (see memory
  // race_mode_null_settings_failopen). The reveal action itself is anti-cheat
  // gated in the core (refused until the seed is completed); we pre-gate the
  // button on Rando_CanRevealActiveSlotSpoiler() so the player gets a clear
  // "after you finish" state rather than a confusing FileNotFound. Tournament
  // admins reveal any time via the --reveal-spoiler CLI flag.
  if (Rando_IsActive() && Rando_ActiveSlotHidesSpoiler()) {
    static int s_reveal_result = -1;       // RandoRevealResult; -1 = none yet
    static bool s_reveal_open_result = false;

    ImGui::SeparatorText("Race-mode spoiler");
    // Wording stays accurate when settings couldn't be recovered (v1 slot /
    // snapshot restore): ActiveSlotHidesSpoiler() fails closed there too, and a
    // non-race slot simply returns FileNotFound from the reveal below.
    ImGui::TextWrapped("This slot's spoiler is suppressed on disk. Reveal "
                       "regenerates the placement and writes the spoiler file.");

    bool can_reveal = Rando_CanRevealActiveSlotSpoiler();
    ImGui::BeginDisabled(!can_reveal);
    if (ImGui::Button("Reveal Spoiler"))
      ImGui::OpenPopup("Reveal spoiler?##z3r_reveal_confirm");
    ImGui::EndDisabled();
    if (!can_reveal) {
      ImGui::SameLine();
      ImGui::TextDisabled("(available after you finish the seed)");
      HelpTooltip("Beat the seed to unlock (admins: --reveal-spoiler).");
    }

    // Confirmation modal.
    if (ImGui::BeginPopupModal("Reveal spoiler?##z3r_reveal_confirm", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::TextWrapped("Reveal the spoiler? This writes the placement to the "
                         "seed's spoiler .json on disk. The race-mode bit stays "
                         "set on the slot.");
      ImGui::Separator();
      if (ImGui::Button("Reveal", ImVec2(120, 0))) {
        s_reveal_result = (int)Rando_RevealActiveSlotSpoiler();
        s_reveal_open_result = true;  // promote to the result modal next frame
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel", ImVec2(120, 0)))
        ImGui::CloseCurrentPopup();
      ImGui::EndPopup();
    }

    // Result modal — opened the frame after the action ran (can't nest a second
    // OpenPopup inside the confirm modal's CloseCurrentPopup).
    if (s_reveal_open_result) {
      s_reveal_open_result = false;
      ImGui::OpenPopup("Reveal result##z3r_reveal_result");
    }
    if (ImGui::BeginPopupModal("Reveal result##z3r_reveal_result", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
      RandoRevealResult r = (RandoRevealResult)s_reveal_result;
      bool ok = (r == kRandoReveal_Ok);
      ImGui::TextColored(ok ? ImVec4(0.4f, 0.9f, 0.4f, 1.0f)
                            : ImVec4(1.0f, 0.5f, 0.4f, 1.0f),
                         "%s", ok ? "Spoiler revealed." : "Reveal failed.");
      ImGui::TextWrapped("%s", Rando_RevealResultDescription(r));
      ImGui::Separator();
      if (ImGui::Button("OK", ImVec2(120, 0)))
        ImGui::CloseCurrentPopup();
      ImGui::EndPopup();
    }
    ImGui::Spacing();
  }

  // ---- Full seed presets ----
  ImGui::SeparatorText("Seed presets");
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
  ImGui::TextUnformatted("Utility presets");
  if (ImGui::SmallButton("Open Fast Ganon")) {
    ApplyOpenFastGanonCorePreset(s);
    s_accessibility_locked = false;
    ApplyAccessibilityLock(s);
    changed = true;
  }
  HelpTooltip("Resets the core progression axes to Open / Fast Ganon defaults without touching Seed QoL, customizer, dungeon item modes, or shuffle panels.");
  ImGui::SameLine();
  if (ImGui::SmallButton("Race-safe")) {
    ApplyRaceSafePreset(s);
    s_accessibility_locked = false;
    ApplyAccessibilityLock(s);
    changed = true;
  }
  HelpTooltip("Turns race mode on, keeps hints on, uses Normal item pool, disables Customizer mode, and applies Dungeon keys / wild maps.");

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
  // Weapons mode — custom combo because the exposed values (0,1,3) skip the
  // reserved vanilla=2.
  {
    const char *wpreview = "?";
    for (auto &o : kModeWeaponsOptions)
      if (o.value == s->mode_weapons) { wpreview = o.label; break; }
    if (ImGui::BeginCombo("Weapons mode", wpreview)) {
      for (auto &o : kModeWeaponsOptions) {
        bool sel = (s->mode_weapons == o.value);
        if (ImGui::Selectable(o.label, sel) && !sel) { s->mode_weapons = o.value; changed = true; }
        if (sel) ImGui::SetItemDefaultFocus();
      }
      ImGui::EndCombo();
    }
    HelpTooltip("randomized: swords are shuffled into the item pool.\n"
                "assured: you start with a sword; the rest are shuffled.\n"
                "swordless: no swords - the hammer/net cover sword-only\n"
                "checks (Ganon, Agahnim, medallions, tablets, curtains).");
  }

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
                "beatable only: only the goal is reachable; some items or "
                "locations may be unreachable.");
  }

  // ---- Logic & tricks (Phase B logic-relaxation axes) ----
  ImGui::SeparatorText("Logic & tricks");
  // Glitch logic level. Phase D (add-rando-major-glitch) opens all 5 tiers.
  if (s->logic > 4) s->logic = 4;
  if (EnumCombo("Logic", &s->logic, kLogicLabels, 5)) changed = true;
  HelpTooltip("NoGlitches (default): items are always reachable without glitches.\n"
              "OverworldGlitches / MajorGlitches / HybridMajorGlitches: logic may\n"
              "expect glitch routes, so some items need advanced techniques to reach\n"
              "(MajorGlitches is the most permissive tier; all are unverified on the\n"
              "US 1.0 ROM). NoLogic: reachability is NOT enforced at all — items can\n"
              "land anywhere and the seed may be impossible.");
  // Tricks — multi-select bitmask. Each checkbox toggles one settings.tricks bit.
  if (ImGui::TreeNodeEx("Tricks (out-of-logic techniques)",
                        s->tricks ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
    ImGui::TextDisabled("Enabling a trick assumes the player can perform it.");
    for (int i = 0; i < 8; i++) {
      bool on = (s->tricks & (uint8)(1u << i)) != 0;
      char lbl[64];
      snprintf(lbl, sizeof(lbl), "%s%s", kTrickUiNames[i],
               kTrickWired[i] ? "" : " (placeholder)");
      if (ImGui::Checkbox(lbl, &on)) {
        if (on) s->tricks |= (uint8)(1u << i);
        else    s->tricks &= (uint8)~(1u << i);
        changed = true;
      }
      if (!kTrickWired[i])
        HelpTooltip("Placeholder - this trick has no logic effect yet, "
                    "so toggling it changes nothing.");
    }
    ImGui::TreePop();
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
    // §2.1 — preview the consequence of enabling race mode (deferred from
    // add-rando-race-mode-reveal; the toggle itself shipped earlier).
    if (s->race_mode)
      ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f),
                         "Spoiler will be suppressed until Reveal is invoked.");
    v = s->hints != 0;
    if (ImGui::Checkbox("Hints", &v)) { s->hints = v; changed = true; }
    HelpTooltip("Telepathic-tile hints.");
  }

  // Quality-of-Life controls live in their own top-level tab
  // (Panel_RecommendedFeatures): seed-burned QoL plus per-slot feature bits.

  // Window theme/scale moved to Game Settings -> Interface (it's a property of
  // this settings window, not a randomizer axis). Don't duplicate it here.

  // ---- Seed ----
  ImGui::SeparatorText("Seed");
  {
    // Auto-randomize lives WITH the seed controls and gates them: when on (the
    // default) a fresh seed is rolled at Generate, so the manual seed input/roll
    // below are disabled to make plain the user isn't choosing the seed. Pinning
    // a seed (uncheck + type, or paste a share string — which also unchecks)
    // re-enables them. See TryBeginGenerate / RenderShareRow.
    ImGui::Checkbox("Randomize seed each generate", &s_randomize_seed_each_generate);
    HelpTooltip("On (default): rolls a fresh seed each Generate. Uncheck to "
                "set a specific seed below.");
    // Reason for the greyed controls, placed BEFORE them so the cause precedes
    // the effect (UX review #7).
    if (s_randomize_seed_each_generate)
      ImGui::TextDisabled("A fresh seed is rolled when you press Generate; you'll see it once it's ready.");

    // Manual seed controls — disabled while auto-randomize is on.
    ImGui::BeginDisabled(s_randomize_seed_each_generate);
    // Hex uint64 input. ImGui has no native u64 widget; use a text buffer and
    // parse it back. Hex is the natural representation for a 64-bit seed (and
    // matches the share-string ethos); the decimal value is shown read-only below.
    static char seed_buf[20];
    static uint64 seed_buf_mirror = ~0ull;  // forces a re-format on first use
    if (seed_buf_mirror != b->seed_u64) {
      snprintf(seed_buf, sizeof seed_buf, "%016llx", (unsigned long long)b->seed_u64);
      seed_buf_mirror = b->seed_u64;
    }
    if (s_randomize_seed_each_generate) {
      // While auto-randomize is on the live seed is NOT the one that will be
      // generated (it's rolled at Generate), so showing the concrete value would
      // mislead (UX review #1). Show a read-only placeholder instead.
      char rnd[20]; snprintf(rnd, sizeof rnd, "(random)");
      ImGui::InputText("Seed (hex)", rnd, sizeof rnd, ImGuiInputTextFlags_ReadOnly);
    } else {
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
    }
    if (ImGui::Button("New random seed")) {
      b->seed_u64 = RollRandomSeed();
      seed_buf_mirror = ~0ull;  // force re-format from the new value
      changed = true;
    }
    HelpTooltip("Roll a fresh random seed value.");
    ImGui::EndDisabled();
  }

  // ---- Live read-only derived display ----
  ImGui::SeparatorText("Live (read-only)");
  {
    char hashhex[65];
    HexBytes(b->pending_hash, 32, hashhex, sizeof hashhex);
    ImGui::Text("settings_hash: %s", hashhex);
    // The share string encodes the seed; while auto-randomize is on the seed
    // isn't chosen until Generate, so the cached share string would be stale —
    // show a placeholder rather than a value the player can't actually use yet.
    if (b->shape_filter_enabled)
      ImGui::TextWrapped("share string: (chosen after Seed Shape search)");
    else if (s_randomize_seed_each_generate)
      ImGui::TextWrapped("share string: (rolled at Generate - seed not chosen yet)");
    else
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

static void Panel_SeedTools() {
  RandoWindowBridge *b = &g_rando_window_bridge;
  RandoSettings *s = &b->pending;

  if (Panel_CustomizerSection(s))
    Pending_Changed();

  ImGui::Spacing();
  ImGui::SeparatorText("Seed shape");
  if (ImGui::Checkbox("Use seed shape filter", &s_shape_enabled)) {
    if (s_shape_enabled && s_shape_search_limit < 1) s_shape_search_limit = 100;
  }
  HelpTooltip("Searches candidate seeds until one matches these spoiler/sphere constraints. "
              "The accepted seed is still the only seed stored in the share string.");

  ImGui::BeginDisabled(!s_shape_enabled);
  ImGui::SliderInt("Search limit", &s_shape_search_limit, 1, 500);
  HelpTooltip("Maximum candidate seeds to try. Generation is synchronous, so keep this bounded.");

  ImGui::Spacing();
  ImGui::TextUnformatted("Length");
  ImGui::SameLine();
  ImGui::RadioButton("Any", &s_shape_length_mode, 0);
  ImGui::SameLine();
  ImGui::RadioButton("Short", &s_shape_length_mode, 1);
  HelpTooltip("Accepts max_sphere <= 4.");
  ImGui::SameLine();
  ImGui::RadioButton("Long", &s_shape_length_mode, 2);
  HelpTooltip("Accepts max_sphere >= 6.");

  ImGui::Spacing();
  ImGui::Checkbox("No unreachable placements", &s_shape_no_unreachable);
  ImGui::SameLine();
  ImGui::Checkbox("No forward-fill fallback", &s_shape_no_forward_fill);

  ImGui::Spacing();
  ImGui::TextUnformatted("Early items");
  ImGui::Checkbox("Boots", &s_shape_early_boots);
  ImGui::SameLine();
  ImGui::Checkbox("Flute", &s_shape_early_flute);
  ImGui::SameLine();
  ImGui::Checkbox("Mirror", &s_shape_early_mirror);
  ImGui::SameLine();
  ImGui::Checkbox("Hookshot", &s_shape_early_hookshot);
  ImGui::SameLine();
  ImGui::Checkbox("Lamp", &s_shape_early_lamp);
  HelpTooltip("Checked items must appear in sphere 2 or earlier.");

  ImGui::Spacing();
  ImGui::InputTextWithHint("Advanced tokens", "min_sphere=7,item:Hookshot<=3",
                           s_shape_custom_tokens, sizeof s_shape_custom_tokens);
  HelpTooltip("Comma-separated CLI tokens. Use max_sphere=N, min_sphere=N, "
              "item:<ItemName><=N, or item:<ItemName>>=N.");
  ImGui::SameLine();
  if (ImGui::Button("Clear")) ShapeUi_ClearFilter();
  ImGui::EndDisabled();

  ShapeUi_RebuildBridge();
  if (!s_shape_enabled) {
    ImGui::TextDisabled("off");
  } else if (!b->shape_filter_valid) {
    ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f), "%s",
                       b->shape_filter_error);
  } else {
    ImGui::TextColored(ImVec4(0.4f, 0.85f, 0.4f, 1.0f), "Active: %s",
                       b->shape_filter_desc);
  }
}

static void Panel_Dungeons() {
  RandoWindowBridge *b = &g_rando_window_bridge;
  RandoSettings *s = &b->pending;
  bool changed = false;

  ImGui::SeparatorText("Dungeon item modes");
  // Small keys is forced to "wild" under Retro (ALTTPR region.wildKeys), so render
  // it locked rather than letting it masquerade as a free choice — mirrors the
  // Completionist -> accessibility lock above. The stored value is left untouched
  // (the generator overrides it via Settings_EffectiveSmallKeysMode), so the user's
  // own pick is preserved if they switch off Retro. (Retro forces only small keys;
  // big keys / maps / compasses stay user-controlled.)
  bool sk_forced_wild = (s->world_state == kWorldState_Retro);
  bool keys_forced_dungeon = Settings_EffectiveDoorShuffle(s) != kDoorShuffle_Vanilla;
  if (sk_forced_wild) {
    ImGui::BeginDisabled();
    uint8 sk_shown = (uint8)kDungeonItemMode_Wild;
    EnumCombo("Small keys", &sk_shown, kDungeonModeLabels, 3);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("(forced by Retro)");
    HelpTooltip("Forced to 'wild' by Retro world-state (region.wildKeys).");
  } else if (keys_forced_dungeon) {
    ImGui::BeginDisabled();
    uint8 sk_shown = (uint8)kDungeonItemMode_Dungeon;
    EnumCombo("Small keys", &sk_shown, kDungeonModeLabels, 3);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("(forced by door shuffle)");
    HelpTooltip("Door shuffle requires in-dungeon small keys; generation normalizes this value to 'dungeon'.");
  } else {
    if (EnumCombo("Small keys", &s->dungeon_small_keys_mode, kDungeonModeLabels, 3)) changed = true;
  }
  if (keys_forced_dungeon) {
    ImGui::BeginDisabled();
    uint8 bk_shown = (uint8)kDungeonItemMode_Dungeon;
    EnumCombo("Big keys", &bk_shown, kDungeonModeLabels, 3);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("(forced by door shuffle)");
    HelpTooltip("Door shuffle requires in-dungeon big keys; generation normalizes this value to 'dungeon'.");
  } else {
    if (EnumCombo("Big keys", &s->dungeon_big_keys_mode, kDungeonModeLabels, 3)) changed = true;
  }
  if (EnumCombo("Maps", &s->dungeon_maps_mode, kDungeonModeLabels, 3)) changed = true;
  if (EnumCombo("Compasses", &s->dungeon_compasses_mode, kDungeonModeLabels, 3)) changed = true;

  ImGui::Spacing();
  ImGui::TextUnformatted("Dungeon presets");
  auto set_all = [&](uint8 mode) {
    s->dungeon_small_keys_mode = mode;
    s->dungeon_big_keys_mode = mode;
    s->dungeon_maps_mode = mode;
    s->dungeon_compasses_mode = mode;
    changed = true;
  };
  if (ImGui::SmallButton("Dungeon keys / wild maps")) {
    ApplyDungeonKeysWildMapsPreset(s);
    changed = true;
  }
  HelpTooltip("Small keys and big keys stay in their own dungeon; maps and compasses are shuffled anywhere.");
  ImGui::SameLine();
  if (ImGui::SmallButton("All vanilla")) set_all(kDungeonItemMode_Vanilla);
  ImGui::SameLine();
  if (ImGui::SmallButton("All dungeon")) set_all(kDungeonItemMode_Dungeon);
  ImGui::SameLine();
  if (ImGui::SmallButton("All wild")) set_all(kDungeonItemMode_Wild);

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

    // Entrance presets — one-click bundles over the three groups below (Shuffle /
    // Exits / Cross). The live controls always reflect the resulting state, so a
    // preset followed by a tweak is fully transparent.
    ImGui::TextUnformatted("Entrance presets");
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
    HelpTooltip("Caves and dungeons shuffled within their own category, coupled.");
    ImGui::SameLine();
    if (ImGui::SmallButton("Crossed")) {
      s->shuffle_cave_entrances = 1; s->shuffle_dungeon_entrances = 1;
      s->coupled = 1; s->cross_category = 1; s->decoupled = 0; changed = true;
    }
    HelpTooltip("Caves and dungeons share one pool, coupled.");
    ImGui::SameLine();
    if (ImGui::SmallButton("Insanity")) {
      // Full decoupled: caves AND dungeons, one-way exits (both runtimes built).
      s->shuffle_cave_entrances = 1; s->shuffle_dungeon_entrances = 1;
      s->coupled = 0; s->cross_category = 0; s->decoupled = 1; changed = true;
    }
    HelpTooltip("Caves and dungeons, one-way exits (full decoupled).");

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
    HelpTooltip("Each cave door leads to a different cave interior. (Open/Standard only.)");
    ImGui::SameLine();
    bool dun = s->shuffle_dungeon_entrances != 0;
    if (ImGui::Checkbox("Dungeons", &dun)) {
      s->shuffle_dungeon_entrances = dun;
      if (!dun) s->shuffle_ganons_tower_entrance = 0;
      if (!dun && !s->shuffle_cave_entrances) { s->decoupled = 0; s->cross_category = 0; }
      s->coupled = ((s->shuffle_cave_entrances || s->shuffle_dungeon_entrances) && !s->decoupled) ? 1 : 0;
      changed = true;
    }
    HelpTooltip("Shuffles 10 of 12 dungeons among themselves (Skull Woods deferred).");
    ImGui::SameLine();
    ImGui::BeginDisabled(!s->shuffle_dungeon_entrances);
    bool gt = s->shuffle_ganons_tower_entrance != 0;
    if (ImGui::Checkbox("+Ganon's Tower", &gt)) {
      s->shuffle_ganons_tower_entrance = gt;
      changed = true;
    }
    ImGui::EndDisabled();
    HelpTooltip("Adds Ganon's Tower to the dungeon shuffle pool.");

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
    HelpTooltip("Enter A, exit A (the ALTTPR baseline).");
    ImGui::SameLine();
    if (ImGui::RadioButton("Decoupled", &exits_mode, 1)) {
      s->decoupled = 1;
      s->coupled = 0;
      changed = true;
    }
    ImGui::EndDisabled();
    HelpTooltip("One-way warps: you exit at a different door than you entered. "
                "Works with Cross-category too.");

    // ---- Group 3: Cross-category (pool mixing — needs both classes) ----
    bool both = (s->shuffle_cave_entrances && s->shuffle_dungeon_entrances);
    ImGui::BeginDisabled(!both);
    bool cross = s->cross_category != 0;
    if (ImGui::Checkbox("Cross-category (caves & dungeons share one pool)", &cross)) {
      s->cross_category = cross;
      changed = true;
    }
    ImGui::EndDisabled();
    HelpTooltip("A cave door can lead to a dungeon and vice versa. Needs both "
                "Caves and Dungeons on. Combine with Decoupled for one-way mixing.");
    ImGui::EndDisabled();
    if (!ws_ok) {
      ImGui::TextDisabled("Entrance shuffle is Open/Standard only for now.");
    }
  }

  // Drop + boss shuffle are both LIVE. Drop sprites use the always-loaded common
  // prize GFX. Boss shuffle now redirects a shuffled boss room's sprite-data +
  // sprite-graphics to the assigned boss's home boss room (the Enemizer
  // pointer-redirect model), so the substituted boss renders with the right tiles
  // and the right formation/count, and the logic gates each prize on the shuffled
  // boss's kill predicate (OP_CAN_KILL_BOSS) so it can't strand.
  ImGui::SeparatorText("Shuffles (experimental)");
  {
    bool ds = s->drop_shuffle != 0;
    if (ImGui::Checkbox("Drop shuffle", &ds)) {
      s->drop_shuffle = ds ? 1 : 0;
      changed = true;
    }
    HelpTooltip("Shuffle which prizes enemies drop; weak early enemies still "
                "drop hearts so you aren't starved for health.");

    bool bs = s->boss_shuffle != 0;
    if (ImGui::Checkbox("Boss shuffle", &bs)) {
      s->boss_shuffle = bs ? 1 : 0;
      changed = true;
    }
    HelpTooltip("Randomize which boss guards each dungeon. "
                "Agahnim, Ganon, Blind, Kholdstare, and Trinexx stay put.");

    bool es = s->enemy_shuffle != 0;
    if (ImGui::Checkbox("Enemy shuffle", &es)) {
      s->enemy_shuffle = es ? 1 : 0;
      changed = true;
    }
    HelpTooltip("Randomizes which enemies appear in each room.");

    // add-rando-enemy-drop-sanity — forced enemy key drops / dungeon enemies as
    // checks. Disabled when generation would normalize the setting off.
    {
      uint8 enemy_drop_key_mode = Settings_EffectiveSmallKeysMode(s);
      bool enemy_drops_off =
          enemy_drop_key_mode != kDungeonItemMode_Wild &&
          enemy_drop_key_mode != kDungeonItemMode_Dungeon;
      uint8 shown = Settings_EffectiveEnemyDropChecks(s);
      uint8 label_count =
          (!enemy_drops_off &&
           Settings_EffectiveDoorShuffle(s) == kDoorShuffle_Vanilla &&
           !s->enemy_shuffle) ? 3 : 2;
      if (s->enemy_drop_checks != shown) {
        s->enemy_drop_checks = shown;
        changed = true;
      }
      ImGui::BeginDisabled(enemy_drops_off);
      if (EnumCombo("Enemy drop checks", &shown, kEnemyDropCheckLabels, label_count)) {
        s->enemy_drop_checks = shown;
        changed = true;
      }
      HelpTooltip("Keys turns forced enemy key drops into checks. Dungeon also turns eligible dungeon enemies into checks; under door shuffle or enemy shuffle Dungeon currently behaves as Keys.");
      ImGui::EndDisabled();
      if (enemy_drops_off) {
        ImGui::TextDisabled("Enemy drop checks require Wild, Retro, or Dungeon small keys.");
      }
    }

    bool drs = s->door_shuffle != 0;
    if (ImGui::Checkbox("Door shuffle", &drs)) {
      s->door_shuffle = drs ? kDoorShuffle_Basic : kDoorShuffle_Vanilla;
      changed = true;
    }
    HelpTooltip("Shuffles each dungeon's interior door connections; key doors "
                "move too. Hyrule Castle and Swamp Palace stay vanilla.");

    // add-rando-pot-sanity — pot shuffle tier. Disabled whenever generation
    // forces pots off (Settings_PotShuffleForcedOff: cave-entrance shuffle).
    // Door shuffle composes through the generated door x pot bridge.
    {
      bool pot_off = Settings_PotShuffleForcedOff(s);
      ImGui::BeginDisabled(pot_off);
      if (EnumCombo("Pot shuffle", &s->pot_shuffle, kPotShuffleLabels, 4)) changed = true;
      HelpTooltip("Turns dungeon pots into randomizer checks. Keys = key pots "
                  "only; Contents adds loot pots; All adds the empty pots too.");
      ImGui::EndDisabled();
      if (pot_off)
        ImGui::TextDisabled("Pot shuffle is unavailable while Cave entrance shuffle is on.");
    }

    if (EnumCombo("Traps", &s->traps, kTrapFrequencyLabels, 5)) {
      changed = true;
    }
    HelpTooltip("Replaces junk items with masquerade traps (low/medium/high = "
                "4/8/16; insanity = every eligible junk pickup). They look like "
                "normal pickups, but spring a surprise effect when collected.");

    // add-rando-trap-catalog — per-category enable checkboxes (disabled when
    // Traps is off). The stored mask is the set of CHECKED categories; "all
    // checked" normalizes to 0, the canonical all-categories sentinel, so default
    // seeds stay byte-identical and "all on" has a single canonical encoding.
    {
      ImGui::BeginDisabled(s->traps == kTrapFrequency_Off);
      uint8 active = s->trap_categories ? s->trap_categories : (uint8)kTrapCategory_All;
      struct { const char *label; uint8 bit; const char *tip; } cats[] = {
        { "Hazard traps",   kTrapCategory_Hazard,   "Bombs, ambushes, angry cuccos. Can hurt Link." },
        { "Impair traps",   kTrapCategory_Impair,   "Freeze, reversed/scrambled, or disabled controls." },
        { "Drain traps",    kTrapCategory_Drain,    "Drains rupees, magic, or ammo." },
        { "Scare traps",    kTrapCategory_Scare,    "Harmless screen shake, darkness, and fakeouts." },
        { "Displace traps", kTrapCategory_Displace, "Warps Link to a safe start point." },
      };
      uint8 newmask = 0;
      bool toggled = false;
      for (int i = 0; i < 5; i++) {
        bool on = (active & cats[i].bit) != 0;
        if (ImGui::Checkbox(cats[i].label, &on)) toggled = true;
        if (on) newmask |= cats[i].bit;
        HelpTooltip(cats[i].tip);
      }
      if (toggled) {
        // All-checked (and the incoherent all-unchecked) both collapse to the
        // 0 = "all categories" sentinel — Traps=off is how you disable them.
        if (newmask == (uint8)kTrapCategory_All) newmask = 0;
        s->trap_categories = newmask;
        changed = true;
      }
      ImGui::EndDisabled();
    }

    // Not-yet-playable placeholders.
    ImGui::BeginDisabled();
    bool off = false;
    ImGui::Checkbox("Glitches", &off);
    ImGui::EndDisabled();
    HelpTooltip("not yet implemented");
  }

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
  if (ImGui::Button("Apply tiled layout")) {
    Trackers_ApplyTiledLayout();
  }
  HelpTooltip("Tiles the Check Tracker on the left, the game in the center, and Map/Item trackers stacked on the right. Windowed mode only.");
  ImGui::SameLine();
  bool auto_tile = g_rando_window_prefs.tracker_tiled_layout_on_startup;
  if (ImGui::Checkbox("Apply at startup", &auto_tile)) {
    g_rando_window_prefs.tracker_tiled_layout_on_startup = auto_tile;
  }
  HelpTooltip("Recreates the tiled tracker layout automatically when the app starts.");
  bool follow_focus = g_rando_window_prefs.tracker_follow_game_focus;
  if (ImGui::Checkbox("Bring trackers with game focus", &follow_focus)) {
    g_rando_window_prefs.tracker_follow_game_focus = follow_focus;
  }
  HelpTooltip("When the game regains focus, restacks already-open tracker windows with it without moving or reopening them.");
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

  // Auto-tracker server: publishes the same live state as newline-delimited JSON
  // over a local TCP socket for external consumers (custom OBS overlays, scripts).
  // The toggle starts/stops the listener live this session; the boot default and
  // the bind config live in zelda3.ini [AutoTracker]. Observation-only.
  ImGui::SeparatorText("Auto-tracker server (external clients)");
  ImGui::TextWrapped(
      "Publish live inventory / reachability / checked-location state as "
      "newline-delimited JSON over a local TCP socket, so external tools can "
      "subscribe without reading emulator memory. Observation-only and "
      "spoiler-safe (never reveals placement). Set the boot default, port, and "
      "remote access in zelda3.ini [AutoTracker].");
  ImGui::Spacing();

  bool running = AutoTracker_IsRunning();
  uint16 at_port = 0;
  bool at_remote = false;
  AutoTracker_GetBindInfo(&at_port, &at_remote);

  bool toggle = running;
  if (ImGui::Checkbox("Enable auto-tracker server", &toggle))
    running = AutoTracker_SetEnabled(toggle);  // reflects the actual result (a failed bind stays off)

  if (running) {
    ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "listening on %s:%u  -  %d client(s)",
                       at_remote ? "0.0.0.0" : "127.0.0.1", (unsigned)at_port,
                       AutoTracker_GetClientCount());
    if (at_remote)
      ImGui::TextColored(ImVec4(0.95f, 0.7f, 0.2f, 1.0f),
                         "Remote access is ON - reachable from the local network.");
  } else {
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "off  (would bind %s:%u)",
                       at_remote ? "0.0.0.0" : "127.0.0.1", (unsigned)at_port);
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
  static char s_save_status[576];  // >= s_save_path[512] + longest literal, so snprintf can't truncate

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

// Case-insensitive substring test (empty needle matches everything).
static bool SpoilerCiContains(const char *hay, const char *needle) {
  if (!needle[0]) return true;
  for (; *hay; ++hay) {
    const char *h = hay, *n = needle;
    while (*h && *n) {
      char a = *h, b = *n;
      if (a >= 'A' && a <= 'Z') a += 32;
      if (b >= 'A' && b <= 'Z') b += 32;
      if (a != b) break;
      ++h; ++n;
    }
    if (!*n) return true;
  }
  return false;
}

// Resolve the same static world-state region override used by reachability and
// file spoilers. Per-seed entrance-shuffle cave overrides are still generation
// overlays, not stored in this viewer snapshot.
static uint16 SpoilerEffectiveRegion(uint16 loc, uint8 world_state) {
  uint16 region = 0xFFFF;
  for (uint32 j = 0; j < kRandoLocationsCount; j++) {
    if (kRandoLocations[j].id == loc) {
      region = kRandoLocations[j].region_id;
      break;
    }
  }
  const RandoLocationPredOverride *ov =
      Rando_FindPredicateOverride(loc, world_state);
  if (ov != nullptr && ov->region_override != 0xFFFF)
    region = ov->region_override;
  return region;
}

// True if a spoiler row matches the search text (location, item, or region).
static bool SpoilerRowMatches(const char *filter, uint16 loc, uint16 item,
                              uint16 region) {
  if (!filter[0]) return true;
  return SpoilerCiContains(Rando_GetLocationName(loc), filter) ||
         SpoilerCiContains(Rando_GetItemName(item), filter) ||
         SpoilerCiContains(Rando_GetRegionName(region), filter);
}

static bool SpoilerLocHidden(uint16 loc) {
  for (uint32 i = 0; i < kRandoLocationsCount; i++) {
    if (kRandoLocations[i].id == loc)
      return kRandoLocations[i].type == LOCTYPE_Medallion;
  }
  return false;
}

static uint16 SpoilerVisiblePlacementCount(const RandoPlacementTable *t) {
  if (t == nullptr) return 0;
  uint16 n = 0;
  for (uint16 i = 0; i < t->count; i++) {
    if (SpoilerLocHidden(t->entries[i].location_id)) continue;
    // add-rando-pot-sanity — empty pots (ITEM_Nothing) are omitted from the
    // listed rows below, so exclude them here too and the header count matches.
    if (t->entries[i].item_id == ITEM_Nothing) continue;
    n++;
  }
  return n;
}

static void Panel_Spoiler() {
  const RandoWindowBridge *b = &g_rando_window_bridge;
  if (!b->has_last_generated) {
    ImGui::TextDisabled("No seed generated this session yet.");
    ImGui::TextWrapped("Generate a (non-race) seed to view its placement here.");
    return;
  }

  const RandoPlacementTable *t = &b->last_generated_placement;
  ImGui::Text("Placements: %u (grouped by generated region)",
              (unsigned)SpoilerVisiblePlacementCount(t));
  if (b->last_generated_has_medallion_assignment) {
    ImGui::Text("Medallions: Mire %s, Turtle Rock %s",
                Rando_GetItemName(b->last_generated_medallion_assignment[0]),
                Rando_GetItemName(b->last_generated_medallion_assignment[1]));
  }

  // Save-spoiler controls (§14.4 file, §14.5 clipboard).
  RenderSpoilerSaveRow();

  // Search/filter the placement list (case-insensitive substring).
  static char s_spoiler_filter[64] = "";
  ImGui::SetNextItemWidth(-1.0f);
  ImGui::InputTextWithHint("##spoiler_filter", "Search locations / items / regions...",
                           s_spoiler_filter, sizeof s_spoiler_filter);
  ImGui::Separator();

  // Build (region_id, location_id, item_id) rows, then sort by (region, loc)
  // so a stable per-region grouping falls out, mirroring rando_spoiler.c's text
  // writer. The per-region tables below get their OWN interactive sort.
  uint16 n = t->count;
  // Display cap = the module-wide location ceiling (rando_logic.h), so the
  // spoiler view can list every placed entry without dropping pot rows ≥ 1024.
  // If a future location-set growth ever exceeds this, we surface a visible note
  // below rather than silently dropping rows.
  enum { kSpoilerMaxRows = kRandoLocationCapacity };
  bool truncated = false;
  static struct Row { uint16 region_id; uint16 location_id; uint16 item_id; } rows[kSpoilerMaxRows];
  if (n > kSpoilerMaxRows) { n = kSpoilerMaxRows; truncated = true; }
  uint16 row_n = 0;
  for (uint16 i = 0; i < n; i++) {
    if (SpoilerLocHidden(t->entries[i].location_id)) continue;
    // add-rando-pot-sanity — omit empty pots (no placed item); loot/key pots,
    // which carry a real item, stay listed under their region.
    if (t->entries[i].item_id == ITEM_Nothing) continue;
    rows[row_n].location_id = t->entries[i].location_id;
    rows[row_n].item_id = t->entries[i].item_id;
    rows[row_n].region_id = SpoilerEffectiveRegion(
        rows[row_n].location_id, b->last_generated_settings.world_state);
    row_n++;
  }
  n = row_n;
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
  // Apply the search filter: compact rows[] to matching entries so empty
  // regions don't render a header and per-region counts reflect the filter.
  if (s_spoiler_filter[0]) {
    uint16 w = 0;
    for (uint16 i = 0; i < n; i++) {
      if (SpoilerRowMatches(s_spoiler_filter, rows[i].location_id,
                            rows[i].item_id, rows[i].region_id))
        rows[w++] = rows[i];
    }
    n = w;
    if (n == 0)
      ImGui::TextDisabled("No placements match this search.");
  }

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
  // Sized to hold "Paste failed: " + a full RandoWindowBridge_Validate
  // message (256) without -Wformat-truncation.
  static char s_paste_error[320];
  static char s_copy_status[32];
  static int s_copy_status_frames = 0;
  static bool s_copy_status_ok = false;

  const char *copy_share =
      (!s_randomize_seed_each_generate && !b->shape_filter_enabled) ? b->share_string : NULL;
  bool can_copy_share = copy_share != NULL && copy_share[0] != '\0';
  ImGui::BeginDisabled(!can_copy_share);
  if (ImGui::Button("Copy share string")) {
    if (can_copy_share && SDL_SetClipboardText(copy_share) == 0) {
      snprintf(s_copy_status, sizeof s_copy_status, "Copied");
      s_copy_status_ok = true;
    } else {
      snprintf(s_copy_status, sizeof s_copy_status, "Copy failed");
      s_copy_status_ok = false;
    }
    s_copy_status_frames = 120;
  }
  ImGui::EndDisabled();
  if (!can_copy_share) {
    HelpTooltip(b->shape_filter_enabled
                    ? "Seed Shape search chooses the final seed at Generate. Use the result popup's copy button."
                    : (s_randomize_seed_each_generate
                           ? "Uncheck Randomize seed each generate to copy a pinned seed. After Generate, use the result popup's copy button."
                           : "No share string is available to copy."));
  }
  ImGui::SameLine();
  if (ImGui::Button("Paste share string")) {
    s_paste_error[0] = '\0';
    s_copy_status_frames = 0;
    char *clip = SDL_GetClipboardText();
    if (clip && clip[0]) {
      ShareString ss;
      ShareDecodeStatus st = Share_Decode(clip, &ss);
      if (st == kShareDecodeOk && ss.format == kShareFormatV2) {
        // v2 carries the FULL canonical settings (add-rando-share-string-v2
        // D2): restore every widget + the seed, gated by deserialize, the
        // customizer manifest fence (D5), and cross-field validation (D3
        // rule 4). Any refusal leaves all widgets untouched.
        RandoSettings tmp;
        char verr[256];
        if (Settings_CanonicalDeserialize(ss.settings_canonical, &tmp) != 0) {
          snprintf(s_paste_error, sizeof s_paste_error,
                   "Paste failed: the share string carries invalid settings values.");
        } else if (tmp.customizer_active) {
          // D5 — manifest pins do not travel in settings; restoring the bit
          // without the manifest would be a non-reproducible placement.
          snprintf(s_paste_error, sizeof s_paste_error,
                   "Paste failed: customizer seeds need their manifest file - "
                   "settings can't be restored from a share string.");
        } else if (RandoWindowBridge_Validate(&tmp, verr, sizeof verr) != 0) {
          snprintf(s_paste_error, sizeof s_paste_error, "Paste failed: %s", verr);
        } else {
          b->pending = tmp;
          b->seed_u64 = ss.seed_u64;
          s_randomize_seed_each_generate = false;  // pasted seed is intentional; keep it
          ShapeUi_ClearFilter();  // pasted seeds are literal; do not search away from them
          Pending_Changed();
          // Arm the D6 Generate-mismatch check. pending_hash was just
          // recomputed from the restored settings; its first 16 bytes ARE
          // Settings_HashShort, so the check matches by construction until
          // the user edits a widget.
          memcpy(b->last_pasted_settings_hash16, b->pending_hash, 16);
          b->paste_armed = true;
          if (ss.version != (uint8)kGeneratorVersion) {
            // D3 rule 2 — the canonical layout is append-only, so restore
            // still happens; only the resulting placement may differ.
            snprintf(s_paste_error, sizeof s_paste_error,
                     "Settings and seed restored. This string is from randomizer "
                     "version %u (this build: %u) - Generate may produce a "
                     "different placement than the sharer's.",
                     (unsigned)ss.version, (unsigned)kGeneratorVersion);
          } else {
            snprintf(s_paste_error, sizeof s_paste_error,
                     "Settings and seed restored from share string.");
          }
        }
      } else if (st == kShareDecodeOk) {
        // v1 — carries seed_u64 + the (one-way) settings_hash, NOT the full
        // settings struct (the hash cannot be inverted). Adopt the decoded
        // seed and warn if the current settings' hash disagrees with the
        // pasted one — we do NOT fabricate widget values from the hash.
        b->seed_u64 = ss.seed_u64;
        s_randomize_seed_each_generate = false;  // pasted seed is intentional; keep it
        ShapeUi_ClearFilter();  // pasted seeds are literal; do not search away from them
        Pending_Changed();
        // Arm the D6 Generate-mismatch check with the EMBEDDED hash: since v1
        // cannot restore settings, the modal fires at Generate whenever the
        // local settings don't match the sharer's (the loud interim fix for
        // the v1-paste silent-divergence incident).
        memcpy(b->last_pasted_settings_hash16, ss.settings_hash, 16);
        b->paste_armed = true;
        bool hash_differs = memcmp(b->pending_hash, ss.settings_hash, 16) != 0;
        if (ss.version != (uint8)kGeneratorVersion) {
          // FIX #17c — Share_Decode validates magic + CRC but no caller compared
          // the version byte (every encoder writes kGeneratorVersion): a string
          // from a different randomizer version regenerates a DIFFERENT placement
          // from the same seed. Warn on the same surface as the settings-hash
          // note below; don't refuse (consistent with that precedent).
          snprintf(s_paste_error, sizeof s_paste_error,
                   "Seed adopted. WARNING: this share string came from randomizer "
                   "version %u but this build is version %u - the generated "
                   "placement will differ from the original seed.%s",
                   (unsigned)ss.version, (unsigned)kGeneratorVersion,
                   hash_differs ? " (Settings differ from the pasted string's too.)" : "");
        } else if (hash_differs) {
          snprintf(s_paste_error, sizeof s_paste_error,
                   "Seed adopted. A v1 share string cannot restore settings - "
                   "current settings differ from the sharer's (match them by "
                   "hand, or ask for a v2 string).");
        }
      } else {
        const char *why = "unrecognized share string";
        switch (st) {
          case kShareDecodeBadLength:    why = "wrong length"; break;
          case kShareDecodeBadBase32:    why = "corrupted base32"; break;
          case kShareDecodeBadMagic:     why = "wrong magic / not a z3r share string"; break;
          case kShareDecodeBadChecksum:  why = "checksum mismatch"; break;
          case kShareDecodeAlttprFormat: why = "alttpr.com-format hashes are not supported"; break;
          case kShareDecodeNewerSettings:
            why = "made by a newer randomizer version - update this build to use it";
            break;
          default: break;
        }
        snprintf(s_paste_error, sizeof s_paste_error, "Paste failed: %s.", why);
      }
    } else {
      snprintf(s_paste_error, sizeof s_paste_error, "Clipboard is empty.");
    }
    if (clip) SDL_free(clip);
  }
  if (s_copy_status_frames > 0) {
    ImGui::SameLine();
    ImGui::TextColored(s_copy_status_ok ? ImVec4(0.4f, 0.85f, 0.4f, 1.0f)
                                        : ImVec4(0.95f, 0.35f, 0.35f, 1.0f),
                       "%s", s_copy_status);
    s_copy_status_frames--;
  }
  if (s_paste_error[0])
    ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.3f, 1.0f), "%s", s_paste_error);
}

// Modal IDs.
static const char *kAssetModalId = "Asset data differs from vanilla";
static const char *kGenModalId = "Generating seed...";
static const char *kPasteMismatchModalId = "Settings differ from pasted share string";

// Open-request flags handed to ImGui::OpenPopup at the top of the frame (must be
// called from the same ID stack scope where BeginPopupModal lives).
static bool s_open_asset_modal = false;
static bool s_open_gen_modal = false;
static bool s_open_paste_mismatch_modal = false;

// Begin generation: gate on the asset hash; either open the asset modal or fire.
static void TryBeginGenerate() {
  RandoWindowBridge *b = &g_rando_window_bridge;
  // add-rando-share-string-v2 D6 — when settings drifted from the last pasted
  // share string, interpose the confirmation modal. Checked BEFORE the seed
  // reroll below so a Cancel leaves the seed untouched (spec scenario
  // "Mismatched Generate is interrupted loudly"). The Generate-anyway button
  // disarms and re-enters this function for the normal chain.
  if (b->paste_armed &&
      memcmp(b->pending_hash, b->last_pasted_settings_hash16, 16) != 0) {
    s_open_paste_mismatch_modal = true;
    return;
  }
  // Fresh seed per generate (unless the user pinned one) — see
  // s_randomize_seed_each_generate. Recompute derived (hash/share string) so the
  // confirmation modal and any copy reflect the rolled seed.
  if (s_randomize_seed_each_generate) {
    b->seed_u64 = RollRandomSeed();
    Pending_Changed();
  }
  // Inert-header case: a --placeholder vanilla_assets_hash.h (all-zeros,
  // kVanillaAssetsHashKnown==0) makes the comparison below vacuous, so the asset
  // modal never opens. Warn once (mirrors the CLI path in main.c) so a modified-
  // assets build doesn't pass the anti-cheat check silently. Dead branch when the
  // real hash is baked in (kVanillaAssetsHashKnown==1).
  if (!kVanillaAssetsHashKnown) {
    static bool warned_inert_asset_gate = false;
    if (!warned_inert_asset_gate) {
      warned_inert_asset_gate = true;
      fprintf(stderr,
        "[rando_window] WARNING the vanilla assets hash is not baked into\n"
        "  vanilla_assets_hash.h (--placeholder build); the modified-assets\n"
        "  check is INERT and Generate will not prompt. Run\n"
        "  `python assets/scripts/dump_vanilla_assets_hash.py` and rebuild to activate it.\n");
    }
  }
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
  if (!invalid && b->shape_filter_enabled && !b->shape_filter_valid) {
    snprintf(err, sizeof err, "Seed Shape: %s",
             b->shape_filter_error[0] ? b->shape_filter_error : "invalid filter");
    invalid = 1;
  }

  ImGui::Separator();
  RenderShareRow();

  if (invalid) {
    ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f), "%s", err);
  } else if (b->shape_filter_enabled && b->shape_filter.enabled) {
    ImGui::TextDisabled("Seed Shape: %s (up to %d candidate seed(s))",
                        b->shape_filter_desc, b->shape_search_limit);
  }

  bool no_target = (b->target_slot_index < 0);
  bool disabled = (invalid != 0) || b->generate_in_progress || no_target;
  if (disabled) ImGui::BeginDisabled();
  // Customizer mode swaps the button label (spec: "Generate from manifest"
  // replaces the standard Generate) so the mode is visible at the action site.
  const char *gen_label = b->pending.customizer_active
                              ? "Generate from manifest & start new slot"
                              : "Generate & start new slot";
  if (ImGui::Button(gen_label)) {
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

// Pasted-settings mismatch modal (add-rando-share-string-v2 D6): opened by
// TryBeginGenerate when settings drifted from the last pasted share string.
static void RenderPasteMismatchModal() {
  RandoWindowBridge *b = &g_rando_window_bridge;
  if (s_open_paste_mismatch_modal) {
    ImGui::OpenPopup(kPasteMismatchModalId);
    s_open_paste_mismatch_modal = false;
  }
  ImGui::SetNextWindowSize(ImVec2(440, 0), ImGuiCond_Appearing);
  if (ImGui::BeginPopupModal(kPasteMismatchModalId, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextWrapped(
        "Settings no longer match the pasted share string. Generating now "
        "makes a different seed.");
    ImGui::Spacing();
    if (ImGui::Button("Generate anyway")) {
      // Disarm, then re-enter the normal Generate chain (seed reroll if
      // enabled + the asset gate) — the D6 check no longer fires.
      b->paste_armed = false;
      ImGui::CloseCurrentPopup();
      TryBeginGenerate();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
      ImGui::CloseCurrentPopup();  // no generation; stays armed for next time
    }
    ImGui::EndPopup();
  }
}

// Generating-seed modal: input-blocking; polls bridge.generate_status.
static void RenderGenerateModal() {
  RandoWindowBridge *b = &g_rando_window_bridge;
  static char s_modal_copy_status[32];
  static int s_modal_copy_status_frames = 0;
  static bool s_modal_copy_status_ok = false;
  if (s_open_gen_modal) {
    ImGui::OpenPopup(kGenModalId);
    s_open_gen_modal = false;
    s_modal_copy_status_frames = 0;
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
      // Surface the seed the player actually got — especially important with
      // auto-randomize on, where they never chose it (UX review #2). Offer the
      // share string right here so the "I got a good seed, how do I keep it?"
      // moment has an affordance without digging back through the tabs.
      ImGui::Text("Seed: %016llx", (unsigned long long)b->last_generated_seed_u64);
      if (b->last_generated_shape_filter_used) {
        ImGui::TextWrapped("Seed Shape: %s", b->last_generated_shape_desc);
        ImGui::Text("Shape attempts: %u/%d",
                    (unsigned)b->last_generated_shape_attempts_used,
                    b->last_generated_shape_search_limit);
        ImGui::TextDisabled("max_sphere=%u, unreachable=%u, forward_fill=%u",
                            (unsigned)b->last_generated_shape_metrics.max_sphere,
                            (unsigned)b->last_generated_shape_metrics.unreachable_count,
                            (unsigned)b->last_generated_shape_metrics.forward_fill_fallback_count);
      }
      if (ImGui::Button("Copy share string")) {
        // Prefer the lossless v2 exchange string (restores settings+seed on
        // paste); fall back to the v1 identity string for customizer seeds,
        // which have no v2 form (design D5).
        const char *to_copy = b->last_generated_share_string_v2[0]
                                  ? b->last_generated_share_string_v2
                                  : b->last_generated_share_string;
        if (to_copy[0] && SDL_SetClipboardText(to_copy) == 0) {
          snprintf(s_modal_copy_status, sizeof s_modal_copy_status, "Copied");
          s_modal_copy_status_ok = true;
        } else {
          snprintf(s_modal_copy_status, sizeof s_modal_copy_status, "Copy failed");
          s_modal_copy_status_ok = false;
        }
        s_modal_copy_status_frames = 120;
      }
      HelpTooltip("Copy this seed's share string to the clipboard (to reproduce or share it later).");
      if (s_modal_copy_status_frames > 0) {
        ImGui::SameLine();
        ImGui::TextColored(s_modal_copy_status_ok ? ImVec4(0.4f, 0.85f, 0.4f, 1.0f)
                                                  : ImVec4(0.95f, 0.35f, 0.35f, 1.0f),
                           "%s", s_modal_copy_status);
        s_modal_copy_status_frames--;
      }
      ImGui::Spacing();
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
static const char *const kTab_SeedTools       = "Seed Tools";
static const char *const kTab_Dungeons        = "Dungeons";
static const char *const kTab_Shuffles        = "Shuffles";
static const char *const kTab_QualityOfLife   = "Seed QoL";
static const char *const kTab_Trackers        = "Trackers";
static const char *const kTab_AssetHash       = "Asset Hash";
static const char *const kTab_Spoiler         = "Spoiler";

static int RandoWindow_BuildTabList(bool last_generated_race_mode,
                                    const char **out_tabs, int cap) {
  int n = 0;
  const char *base[] = { kTab_General, kTab_SeedTools, kTab_Dungeons, kTab_Shuffles,
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
  const char *tabs[10];
  int n = RandoWindow_BuildTabList(/*race_mode=*/true, tabs, 10);
  if (TabListContains(tabs, n, kTab_Spoiler)) {
    fprintf(stderr,
            "[rando_window] SELF-CHECK FAILED: Spoiler tab present under "
            "race-mode (must be omitted).\n");
    exit(2);
  }
  n = RandoWindow_BuildTabList(/*race_mode=*/false, tabs, 10);
  if (!TabListContains(tabs, n, kTab_Spoiler)) {
    fprintf(stderr,
            "[rando_window] SELF-CHECK FAILED: Spoiler tab missing for a "
            "non-race generation (must be present).\n");
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
  // Let the OpenSettings hotkey CLOSE the window while the settings window has
  // focus. This keydown carries the settings window's windowID, so the host's
  // two-window router delivers it here instead of to the game's kKeys_OpenSettings
  // handler — without this branch the toggle key opens the window but can never
  // close it (the "can't press it again to close" report). Rebind capture above
  // already returned, so we never steal a key being assigned; skip when ImGui
  // wants text input (a focused field) so the key can still be typed; ignore
  // auto-repeat so a held key doesn't thrash open/closed.
  if (e->type == SDL_KEYDOWN && !e->key.repeat && !ImGui::GetIO().WantTextInput &&
      FindCmdForSdlKey(e->key.keysym.sym, (SDL_Keymod)e->key.keysym.mod) == kKeys_OpenSettings) {
    RandoWindow_Hide();
    return;
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
    const char *tabs[10];
    int ntabs = RandoWindow_BuildTabList(b->last_generated_race_mode, tabs, 10);
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
              else if (tabs[i] == kTab_SeedTools)     Panel_SeedTools();
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
  // same frame so OpenPopup requests from this frame take effect. The paste-
  // mismatch modal renders FIRST so its Generate-anyway re-entry can open the
  // asset/generating modals in this same frame (same chaining as asset → gen).
  RenderPasteMismatchModal();
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
  RandoWindowBridge_StoreGenerated(NULL, NULL, NULL, false);
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
  // Re-sync the Game Settings panels to the live config, mirroring
  // RandoWindow_ToggleConfig. Without this, opening via the file-select "New
  // Randomizer" entry leaves the working copy stale; if the user then edits any
  // Game-Settings field and clicks Apply, the whole stale copy is committed and
  // reverts live config that drifted since the last sync (window scale, volume,
  // a live features0 toggle). (audit NW1)
  GameConfig_NotifyWindowOpened();
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
