// game_config_panels.cpp — native game-config UI (Controls / Controller / Video
// / Audio / Gameplay). PC only (Z3R_NATIVE_SETTINGS_WINDOW). Renders into the
// "Game Settings" top-level tab of the Z3R Settings window. Edits a WORKING COPY
// of g_config + the keybind model; the Apply button raises a flag and the host
// commits + live-applies + writes the INI on a game frame (game GL context
// current). This TU contains no g_ram writes and is excluded from the audit
// guard (it is .cpp, and the live feature path goes through g_wanted_zelda_features
// in config.c, not a direct g_ram write).
#ifdef Z3R_NATIVE_SETTINGS_WINDOW

#include "imgui.h"
#include <SDL.h>

#include <cstdio>
#include <cstring>
#include <cstdarg>
#if defined(_WIN32)
#include <filesystem>  // ZSPR picker dir scan (Windows only — keeps Linux/macOS CI link-dep-free)
#endif

#include "game_config_widgets.h"
#include "game_cheats.h"   // Cheats_* gate/pokes/widgets (shared cheat-core)
#include "game_panels.h"   // Dbg*_Render / Rando*_Render panel entry points

extern "C" {
#include "../../config.h"     // g_config, g_keybind_*, Config_* helpers, kKeys_*, kGamepadBtn_*
#include "../../features.h"   // kFeatures0_* bit constants
// Randomizer ownership state for the shared item bytes 0xF344 (Mushroom/Powder)
// and 0xF34C (Shovel/Flute). The Debug inventory editor keeps these in sync when
// it edits those bytes under rando (see the Items section). Declared here rather
// than including rando.h to keep this UI TU's dependency surface small.
extern uint8 g_rando_mushroom_held;       // 1 = true Mushroom possession (rando)
extern uint8 g_rando_flute_shovel_owned;  // kRandoFluteShovel_* bits: Shovel=0x01, Flute=0x02
}

// ---------------------------------------------------------------------------
// Working copy + UI state
// ---------------------------------------------------------------------------
static Config     s_cfg;
static char       s_path_link[512], s_path_shader[512], s_path_msu[512], s_lang[32];
static char       s_path_cosmetic_sprite[512];  // cosmetic sprite-shuffle folder
static uint16     s_kbd[kKeys_Total];
static PadBinding s_pad[kKeys_Total];
static int        s_aspect_enum;        // 0=4:3 1=16:9 2=16:10 3=18:9, -1=custom (non-editable)
static bool       s_extend_y, s_unchanged_sprites, s_no_visual_fixes;
static bool       s_dirty;
static uint32     s_restart_mask;       // kCfgRestart_* set after the last Apply
static bool       s_pending_apply;
static bool       s_synced;             // working copy initialized at least once

// Capture state machine.
static int          s_capture_cmd = -1;
static bool         s_capture_pad = false;
static unsigned int s_capture_deadline = 0;

// Transient toast (conflict / timeout notices).
static char         s_toast[256];
static unsigned int s_toast_until = 0;

static const uint32 kGeometryBits = kFeatures0_ExtendScreen64 | kFeatures0_WidescreenVisualFixes;

static void Toast(const char *fmt, ...) {
  va_list ap; va_start(ap, fmt);
  vsnprintf(s_toast, sizeof s_toast, fmt, ap);
  va_end(ap);
  s_toast_until = SDL_GetTicks() + 4000;
}

// ---------------------------------------------------------------------------
// Aspect-ratio composite <-> g_config
// ---------------------------------------------------------------------------
static uint8 AspectValue(int enum_idx, bool extend_y) {
  int h = extend_y ? 240 : 224;
  switch (enum_idx) {
  case 1: return (uint8)((h * 16 / 9 - 256) / 2);
  case 2: return (uint8)((h * 16 / 10 - 256) / 2);
  case 3: return (uint8)((h * 18 / 9 - 256) / 2);
  default: return 0;  // 4:3
  }
}

// The parser is order-dependent (config.c "todo: make it not depend on the
// order"): "16:9, extend_y" computes the ratio at h=224, while "extend_y, 16:9"
// uses h=240 — two different stored values for the same ratio. Match BOTH heights
// so either INI form is recognized + editable (Apply normalizes to the extend_y
// height; the writer emits extend_y first to reproduce it).
static int MatchAspect(uint8 ar) {
  if (ar == 0) return 0;
  for (int n = 1; n <= 3; n++)
    if (ar == AspectValue(n, false) || ar == AspectValue(n, true)) return n;
  return -1;
}

static void DeriveAspectFromCfg(void) {
  s_extend_y = s_cfg.extend_y;
  uint8 ar = s_cfg.extended_aspect_ratio;
  s_aspect_enum = MatchAspect(ar);  // -1 = hand-set custom value (non-editable)
  s_unchanged_sprites = (ar != 0) && !(s_cfg.features0 & kFeatures0_ExtendScreen64);
  s_no_visual_fixes   = (ar != 0) && !(s_cfg.features0 & kFeatures0_WidescreenVisualFixes);
}

// Fold the aspect-combo selection back into s_cfg (scalars + the two geometry
// feature bits), matching config.c's ExtendedAspectRatio parser.
static void ApplyAspectToCfg(void) {
  if (s_aspect_enum < 0) return;  // custom: leave g_config untouched
  uint8 ar = AspectValue(s_aspect_enum, s_extend_y);
  s_cfg.extended_aspect_ratio = ar;
  s_cfg.extend_y = s_extend_y;
  if (ar != 0 && !s_unchanged_sprites) s_cfg.features0 |= kFeatures0_ExtendScreen64;
  else s_cfg.features0 &= ~kFeatures0_ExtendScreen64;
  if (ar != 0 && !s_no_visual_fixes) s_cfg.features0 |= kFeatures0_WidescreenVisualFixes;
  else s_cfg.features0 &= ~kFeatures0_WidescreenVisualFixes;
}

// ---------------------------------------------------------------------------
// Sync working copy from live state
// ---------------------------------------------------------------------------
static void SyncFromLive(void) {
  s_cfg = g_config;
  snprintf(s_path_link, sizeof s_path_link, "%s", g_config.link_graphics ? g_config.link_graphics : "");
  snprintf(s_path_shader, sizeof s_path_shader, "%s", g_config.shader ? g_config.shader : "");
  snprintf(s_path_msu, sizeof s_path_msu, "%s", g_config.msu_path ? g_config.msu_path : "");
  snprintf(s_lang, sizeof s_lang, "%s", g_config.language ? g_config.language : "");
  snprintf(s_path_cosmetic_sprite, sizeof s_path_cosmetic_sprite, "%s",
           g_config.cosmetic_sprite_dir ? g_config.cosmetic_sprite_dir : "");
  memcpy(s_kbd, g_keybind_kbd, sizeof s_kbd);
  memcpy(s_pad, g_keybind_pad, sizeof s_pad);
  DeriveAspectFromCfg();
  s_dirty = false;
  s_synced = true;
}

extern "C" void GameConfig_NotifyWindowOpened(void) {
  SyncFromLive();
  s_restart_mask = 0;
}

// ---------------------------------------------------------------------------
// Command labels for conflict toasts / row headers
// ---------------------------------------------------------------------------
static const char *const kSnesSlotNames[12] = {
  "Up", "Down", "Left", "Right", "Select", "Start", "A", "B", "X", "Y", "L", "R"
};

// Human label for an expanded command id (owning command + slot).
static void CmdLabel(int id, char *out, size_t cap) {
  for (int i = 1; i < Config_CommandCount(); i++) {
    int base = Config_CommandId(i), n = Config_CommandSlots(i);
    if (id >= base && id < base + n) {
      int slot = id - base;
      if (n == 1) { snprintf(out, cap, "%s", Config_CommandName(i)); return; }
      if (base == kKeys_Controls && slot < 12)
        snprintf(out, cap, "%s (%s)", Config_CommandName(i), kSnesSlotNames[slot]);
      else
        snprintf(out, cap, "%s %d", Config_CommandName(i), slot + 1);
      return;
    }
  }
  snprintf(out, cap, "cmd %d", id);
}

// ---------------------------------------------------------------------------
// Conflict resolution — steal with warning so the model stays duplicate-free
// ---------------------------------------------------------------------------
static void StealKbd(int keep_cmd, uint16 kwm) {
  if (kwm == 0) return;
  for (int j = 0; j < kKeys_Total; j++) {
    if (j != keep_cmd && s_kbd[j] == kwm) {
      char lbl[96]; CmdLabel(j, lbl, sizeof lbl);
      char keyname[64]; Config_DecodeKeyName(kwm, keyname, sizeof keyname);
      s_kbd[j] = 0;
      Toast("Unbound %s (was %s)", lbl, keyname);
    }
  }
}

static bool PadEq(PadBinding a, PadBinding b) {
  return a.button == b.button && a.modifiers == b.modifiers;
}

static void StealPad(int keep_cmd, PadBinding b) {
  if (b.button < 0) return;
  for (int j = 0; j < kKeys_Total; j++) {
    if (j != keep_cmd && PadEq(s_pad[j], b)) {
      char lbl[96]; CmdLabel(j, lbl, sizeof lbl);
      s_pad[j].button = kGamepadBtn_Invalid;
      s_pad[j].modifiers = 0;
      Toast("Unbound %s (conflict)", lbl);
    }
  }
}

// ---------------------------------------------------------------------------
// Capture (called from the host event pumps)
// ---------------------------------------------------------------------------
static void ArmCapture(int cmd, bool pad) {
  s_capture_cmd = cmd;
  s_capture_pad = pad;
  s_capture_deadline = SDL_GetTicks() + 5000;
}

extern "C" bool GameConfig_WantsKeyCapture(void) { return s_capture_cmd >= 0 && !s_capture_pad; }
extern "C" bool GameConfig_IsCapturingGamepad(void) { return s_capture_cmd >= 0 && s_capture_pad; }
extern "C" void GameConfig_CancelCapture(void) { s_capture_cmd = -1; }

extern "C" void GameConfig_FeedCapturedKey(int code, int mod) {
  if (s_capture_cmd < 0 || s_capture_pad) return;
  uint16 kwm = Config_EncodeKeyEvent((SDL_Keycode)code, (SDL_Keymod)mod);
  if (kwm == 0) { s_capture_cmd = -1; return; }
  StealKbd(s_capture_cmd, kwm);
  s_kbd[s_capture_cmd] = kwm;
  s_dirty = true;
  s_capture_cmd = -1;
}

extern "C" void GameConfig_FeedCapturedButton(int button, unsigned int held_mask) {
  if (s_capture_cmd < 0 || !s_capture_pad) return;
  if (button < 0) { s_capture_cmd = -1; return; }
  PadBinding b;
  b.button = (int16)button;
  b.modifiers = held_mask & ~(1u << button);
  StealPad(s_capture_cmd, b);
  s_pad[s_capture_cmd] = b;
  s_dirty = true;
  s_capture_cmd = -1;
}

extern "C" void GameConfig_CaptureTick(unsigned int now) {
  if (s_capture_cmd >= 0 && now > s_capture_deadline) {
    s_capture_cmd = -1;
    Toast("Rebind timed out");
  }
}

// ---------------------------------------------------------------------------
// Apply (raised by the button, committed on a game frame by the host)
// ---------------------------------------------------------------------------
extern "C" bool GameConfig_HasPendingApply(void) { return s_pending_apply; }

extern "C" void GameConfig_ApplyPending(void) {
  s_pending_apply = false;
  if (!s_synced) return;

  ApplyAspectToCfg();

  Config prev = g_config;

  // Commit scalars. (Copies pointer fields too; we fix the 4 string fields next.)
  g_config = s_cfg;
  // String fields: re-point only if the buffer changed (value compare), via the
  // process-lifetime intern arena (never touches g_config.memory_buffer).
  if (strcmp(s_path_link, prev.link_graphics ? prev.link_graphics : "") != 0)
    g_config.link_graphics = s_path_link[0] ? Config_InternString(s_path_link) : NULL;
  else g_config.link_graphics = prev.link_graphics;
  if (strcmp(s_path_shader, prev.shader ? prev.shader : "") != 0)
    g_config.shader = s_path_shader[0] ? Config_InternString(s_path_shader) : NULL;
  else g_config.shader = prev.shader;
  if (strcmp(s_path_msu, prev.msu_path ? prev.msu_path : "") != 0)
    g_config.msu_path = s_path_msu[0] ? Config_InternString(s_path_msu) : NULL;
  else g_config.msu_path = prev.msu_path;
  if (strcmp(s_lang, prev.language ? prev.language : "") != 0)
    g_config.language = s_lang[0] ? Config_InternString(s_lang) : NULL;
  else g_config.language = prev.language;
  if (strcmp(s_path_cosmetic_sprite, prev.cosmetic_sprite_dir ? prev.cosmetic_sprite_dir : "") != 0)
    g_config.cosmetic_sprite_dir = s_path_cosmetic_sprite[0] ? Config_InternString(s_path_cosmetic_sprite) : NULL;
  else g_config.cosmetic_sprite_dir = prev.cosmetic_sprite_dir;

  // Commit bindings + rebuild the runtime keymap (live).
  memcpy(g_keybind_kbd, s_kbd, sizeof g_keybind_kbd);
  memcpy(g_keybind_pad, s_pad, sizeof g_keybind_pad);
  Config_RebuildKeymap();

  // Live-apply the safe subset; collect restart-needed categories.
  s_restart_mask = Config_ApplyLive(&prev, &g_config);

  // Persist (in-place, comment-preserving) to the loaded INI.
  Config_WriteIniFile(Config_GetLoadedIniPath());

  // Re-sync the working copy (achieved window scale, etc.) and clear dirty.
  SyncFromLive();
}

// ---------------------------------------------------------------------------
// Widget helpers
// ---------------------------------------------------------------------------
static void Help(const char *text) {
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    ImGui::SetTooltip("%s", text);
}

// Marks a control that takes effect only after a restart.
static void RestartTag(void) {
  ImGui::SameLine();
  ImGui::TextDisabled("(restart)");
}

static bool ComboInt(const char *label, int *v, const char *const *items, int count) {
  bool changed = false;
  const char *preview = (*v >= 0 && *v < count) ? items[*v] : "?";
  if (ImGui::BeginCombo(label, preview)) {
    for (int i = 0; i < count; i++) {
      bool sel = (*v == i);
      if (ImGui::Selectable(items[i], sel) && i != *v) { *v = i; changed = true; }
      if (sel) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
  return changed;
}

static bool FeatureCheckbox(const char *label, uint32 mask, const char *help) {
  bool v = (s_cfg.features0 & mask) != 0;
  if (ImGui::Checkbox(label, &v)) {
    if (v) s_cfg.features0 |= mask; else s_cfg.features0 &= ~mask;
    s_dirty = true;
  }
  if (help) Help(help);
  return true;
}

// One bindings row: label, current binding, [Rebind], [Clear].
static void BindingRow(const char *label, int cmd, bool pad) {
  ImGui::PushID(cmd + (pad ? 100000 : 0));
  ImGui::TextUnformatted(label);
  ImGui::SameLine(220);
  char bind[96];
  bool have;
  if (pad) have = Config_PadBindingName(s_pad[cmd], bind, sizeof bind);
  else     have = Config_DecodeKeyName(s_kbd[cmd], bind, sizeof bind) ;
  bool bound = pad ? (s_pad[cmd].button >= 0) : (s_kbd[cmd] != 0);
  const bool capturing = (s_capture_cmd == cmd && s_capture_pad == pad);
  if (capturing) ImGui::TextColored(ImVec4(1, 0.85f, 0.3f, 1), pad ? "press a button..." : "press a key...");
  else if (!bound) ImGui::TextDisabled("(unbound)");
  else if (!have) ImGui::TextDisabled("(unnamed)");
  else ImGui::TextUnformatted(bind);
  ImGui::SameLine(380);
  if (ImGui::SmallButton(capturing ? "Cancel" : "Rebind")) {
    if (capturing) s_capture_cmd = -1; else ArmCapture(cmd, pad);
  }
  ImGui::SameLine();
  if (ImGui::SmallButton("Clear")) {
    if (pad) { s_pad[cmd].button = kGamepadBtn_Invalid; s_pad[cmd].modifiers = 0; }
    else s_kbd[cmd] = 0;
    s_dirty = true;
    if (capturing) s_capture_cmd = -1;
  }
  // Conflict badge (live scan).
  if (bound) {
    int dup = 0;
    for (int j = 0; j < kKeys_Total; j++) {
      if (j == cmd) continue;
      if (pad) { if (PadEq(s_pad[j], s_pad[cmd])) dup++; }
      else { if (s_kbd[j] == s_kbd[cmd]) dup++; }
    }
    if (dup) { ImGui::SameLine(); ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "conflict"); }
  }
  ImGui::PopID();
}

// Optional case-insensitive name filter for the bindings lists (set by
// Panel_Controls; cleared by Panel_Controller). When set, CommandRows skips any
// command whose name doesn't contain the filter substring.
static const char *g_bind_filter;

static bool NameMatchesFilter(const char *name) {
  if (!g_bind_filter || !g_bind_filter[0]) return true;
  // naive case-insensitive substring search
  for (const char *h = name; *h; h++) {
    const char *a = h, *b = g_bind_filter;
    while (*a && *b && ((*a | 32) == (*b | 32))) { a++; b++; }
    if (!*b) return true;
  }
  return false;
}

// Render all slots of a multi-slot command as rows.
static void CommandRows(int cmd_index, bool pad) {
  if (!NameMatchesFilter(Config_CommandName(cmd_index))) return;
  int base = Config_CommandId(cmd_index), n = Config_CommandSlots(cmd_index);
  for (int s = 0; s < n; s++) {
    char lbl[96];
    if (n == 1) snprintf(lbl, sizeof lbl, "%s", Config_CommandName(cmd_index));
    else if (base == kKeys_Controls && s < 12) snprintf(lbl, sizeof lbl, "%s", kSnesSlotNames[s]);
    else snprintf(lbl, sizeof lbl, "Slot %d", s + 1);
    BindingRow(lbl, base + s, pad);
  }
}

// Find the kKeyNameId index whose base id == the given expanded base.
static int CmdIndexForBase(int base) {
  for (int i = 1; i < Config_CommandCount(); i++)
    if (Config_CommandId(i) == base) return i;
  return -1;
}

static void RowsForBase(int base, bool pad) {
  int i = CmdIndexForBase(base);
  if (i >= 0) CommandRows(i, pad);
}

// ---------------------------------------------------------------------------
// Panels
// ---------------------------------------------------------------------------
static void Panel_Controls(void) {
  ImGui::TextWrapped("Click Rebind, then press a key (Esc cancels). This window must be focused.");
  static char s_filter[64];
  ImGui::SetNextItemWidth(220);
  ImGui::InputTextWithHint("##ctrlfilter", "Filter actions...", s_filter, sizeof s_filter);
  ImGui::SameLine();
  if (ImGui::SmallButton("Clear")) s_filter[0] = 0;
  g_bind_filter = s_filter;  // applied by CommandRows below
  ImGui::Separator();
  if (ImGui::BeginChild("##kbd", ImVec2(0, 0))) {
    ImGui::SeparatorText("Game controls");
    RowsForBase(kKeys_Controls, false);

    ImGui::SeparatorText("System & window");
    static const int kSys[] = { kKeys_OpenSettings, kKeys_Fullscreen, kKeys_Pause, kKeys_PauseDimmed,
      kKeys_Turbo, kKeys_ReplayTurbo, kKeys_Reset, kKeys_WindowBigger, kKeys_WindowSmaller,
      kKeys_DisplayPerf, kKeys_ToggleRenderer, kKeys_VolumeUp, kKeys_VolumeDown,
      kKeys_StopReplay, kKeys_ClearKeyLog };
    for (int k = 0; k < (int)(sizeof kSys / sizeof kSys[0]); k++) RowsForBase(kSys[k], false);

    if (ImGui::TreeNode("Save / load states")) {
      RowsForBase(kKeys_Load, false); RowsForBase(kKeys_Save, false); RowsForBase(kKeys_Replay, false);
      ImGui::TreePop();
    }
    if (ImGui::TreeNode("Reference replay (advanced)")) {
      RowsForBase(kKeys_LoadRef, false); RowsForBase(kKeys_ReplayRef, false);
      ImGui::TreePop();
    }
    if (ImGui::TreeNode("Cheats")) {
      RowsForBase(kKeys_CheatLife, false); RowsForBase(kKeys_CheatKeys, false);
      RowsForBase(kKeys_CheatEquipment, false); RowsForBase(kKeys_CheatWalkThroughWalls, false);
      ImGui::TreePop();
    }
    if (ImGui::TreeNode("Randomizer")) {
      // The legacy OAM-overlay toggles (RandoToggleItemTracker /
      // RandoToggleLocationTracker) are superseded on PC: main.c routes them to
      // the SAME Trackers_Toggle() calls as the window keys below, so exposing
      // them here just produced two bindable rows that do the identical thing
      // (the "duplicate hotkeys" report). They stay in the keymap so any existing
      // ini binding keeps working — we simply don't surface them as separate
      // rebindable actions. Bind the Item / Check / Map window keys instead.
      RowsForBase(kKeys_RandoItemTrackerWindow, false);
      RowsForBase(kKeys_RandoCheckTrackerWindow, false);
      RowsForBase(kKeys_RandoMapTrackerWindow, false);
      RowsForBase(kKeys_RandoRevealSpoiler, false);
      ImGui::TreePop();
    }
  }
  ImGui::EndChild();
}

static void Panel_Controller(void) {
  g_bind_filter = nullptr;  // the filter is keyboard-panel-only
  ImGui::TextWrapped("Click Rebind, then press a controller button. Hold modifier buttons while "
                     "pressing the main button to bind a combo (e.g. L1+Start). Only one binding "
                     "per action is editable here.");
  ImGui::Separator();
  if (ImGui::BeginChild("##pad", ImVec2(0, 0))) {
    ImGui::SeparatorText("Game controls");
    RowsForBase(kKeys_Controls, true);
    ImGui::SeparatorText("System & window");
    static const int kSys[] = { kKeys_OpenSettings, kKeys_Fullscreen, kKeys_Pause, kKeys_Turbo,
      kKeys_Reset, kKeys_DisplayPerf, kKeys_ToggleRenderer, kKeys_VolumeUp, kKeys_VolumeDown };
    for (int k = 0; k < (int)(sizeof kSys / sizeof kSys[0]); k++) RowsForBase(kSys[k], true);
  }
  ImGui::EndChild();
}

#if defined(_WIN32)
// Scan a few likely directories for *.zspr (Link sprite) files. Exception-free
// (std::error_code overloads) so it's safe regardless of the EH mode. Returns the
// count; fills names[] with relative paths.
static int ScanZspr(char names[][260], int max) {
  int n = 0;
  const char *dirs[] = {".", "sprites"};  // (Windows is case-insensitive; one "sprites" entry)
  for (int d = 0; d < 2 && n < max; d++) {
    std::error_code ec;
    std::filesystem::directory_iterator it(dirs[d], ec), end;
    for (; !ec && it != end; it.increment(ec)) {
      std::error_code fec;
      if (!it->is_regular_file(fec)) continue;
      std::string ext = it->path().extension().string();
      for (char &c : ext) if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
      if (ext == ".zspr" && n < max) {
        snprintf(names[n], 260, "%s", it->path().string().c_str());
        n++;
      }
    }
  }
  return n;
}
#endif

static void Panel_Video(void) {
  int scale = s_cfg.window_scale;
  if (ImGui::SliderInt("Window scale", &scale, 1, 8)) { s_cfg.window_scale = (uint8)scale; s_dirty = true; }
  Help("Integer scaling of the game window.");

  static const char *const kFs[] = { "Windowed", "Desktop fullscreen", "Exclusive fullscreen" };
  int fs = s_cfg.fullscreen; if (fs < 0 || fs > 2) fs = 0;
  if (ComboInt("Fullscreen", &fs, kFs, 3)) { s_cfg.fullscreen = (uint8)fs; s_dirty = true; }
  if (fs == 2) RestartTag();

  bool autosz = (s_cfg.window_width == 0 && s_cfg.window_height == 0);
  if (ImGui::Checkbox("Auto window size", &autosz)) {
    if (autosz) { s_cfg.window_width = 0; s_cfg.window_height = 0; }
    else { s_cfg.window_width = 1024; s_cfg.window_height = 896; }
    s_dirty = true;
  }
  RestartTag();
  if (!autosz) {
    int w = s_cfg.window_width, h = s_cfg.window_height;
    if (ImGui::InputInt("Width", &w))  { s_cfg.window_width  = w < 0 ? 0 : w; s_dirty = true; }
    if (ImGui::InputInt("Height", &h)) { s_cfg.window_height = h < 0 ? 0 : h; s_dirty = true; }
  }

  static const char *const kOut[] = { "SDL", "SDL-Software", "OpenGL", "OpenGL ES" };
  int om = s_cfg.output_method; if (om < 0 || om > 3) om = 0;
  if (ComboInt("Renderer", &om, kOut, 4)) { s_cfg.output_method = (uint8)om; s_dirty = true; }
  RestartTag();

  bool nr = s_cfg.new_renderer;
  if (ImGui::Checkbox("New (optimized) PPU renderer", &nr)) { s_cfg.new_renderer = nr; s_dirty = true; }
  bool em7 = s_cfg.enhanced_mode7;
  if (ImGui::Checkbox("Enhanced Mode 7 (hi-res map)", &em7)) { s_cfg.enhanced_mode7 = em7; s_dirty = true; }
  RestartTag();
  bool lf = s_cfg.linear_filtering;
  if (ImGui::Checkbox("Linear filtering", &lf)) { s_cfg.linear_filtering = lf; s_dirty = true; }
  RestartTag();
  bool nsl = s_cfg.no_sprite_limits;
  if (ImGui::Checkbox("Remove sprite-per-line limit", &nsl)) { s_cfg.no_sprite_limits = nsl; s_dirty = true; }
  bool iar = s_cfg.ignore_aspect_ratio;
  if (ImGui::Checkbox("Ignore 4:3 aspect ratio", &iar)) { s_cfg.ignore_aspect_ratio = iar; s_dirty = true; }
  RestartTag();
  bool dim = (s_cfg.features0 & kFeatures0_DimFlashes) != 0;
  if (ImGui::Checkbox("Dim screen flashes", &dim)) {
    if (dim) s_cfg.features0 |= kFeatures0_DimFlashes; else s_cfg.features0 &= ~kFeatures0_DimFlashes;
    s_dirty = true;
  }

  ImGui::SeparatorText("Widescreen");
  if (s_aspect_enum < 0) {
    ImGui::TextDisabled("Custom ExtendedAspectRatio set in the INI — not editable here.");
  } else {
    static const char *const kAr[] = { "4:3", "16:9", "16:10", "18:9" };
    if (ComboInt("Aspect ratio", &s_aspect_enum, kAr, 4)) s_dirty = true;
    RestartTag();
    if (ImGui::Checkbox("Extend to 240 lines (extend_y)", &s_extend_y)) s_dirty = true;
    RestartTag();
    if (s_aspect_enum != 0) {
      if (ImGui::Checkbox("Keep original sprite range (unchanged_sprites)", &s_unchanged_sprites)) s_dirty = true;
      if (ImGui::Checkbox("Disable widescreen visual fixes (no_visual_fixes)", &s_no_visual_fixes)) s_dirty = true;
    }
  }

  ImGui::SeparatorText("Paths");
  bool shader_ok = (s_cfg.output_method == kOutputMethod_OpenGL || s_cfg.output_method == kOutputMethod_OpenGL_ES);
  if (!shader_ok) ImGui::BeginDisabled();
  if (ImGui::InputText("Shader (.glsl/.glslp)", s_path_shader, sizeof s_path_shader)) s_dirty = true;
  if (!shader_ok) { ImGui::EndDisabled(); Help("Shaders apply to the OpenGL renderers only."); }
  RestartTag();
  if (ImGui::InputText("Link graphics (.zspr)", s_path_link, sizeof s_path_link)) s_dirty = true;
  RestartTag();
#if defined(_WIN32)
  {
    static char s_zspr[64][260];
    static int s_nzspr = -1;  // -1 = not scanned yet
    if (s_nzspr < 0) s_nzspr = ScanZspr(s_zspr, 64);
    ImGui::SetNextItemWidth(320);
    if (ImGui::BeginCombo("##zsprpick", s_nzspr > 0 ? "Pick a .zspr..." : "(no .zspr found)")) {
      for (int i = 0; i < s_nzspr; i++)
        if (ImGui::Selectable(s_zspr[i])) { snprintf(s_path_link, sizeof s_path_link, "%s", s_zspr[i]); s_dirty = true; }
      ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Rescan")) s_nzspr = ScanZspr(s_zspr, 64);
    ImGui::SameLine();
    ImGui::TextDisabled("(scans . and sprites/)");
  }
#endif
  bool perf = s_cfg.display_perf_title;
  if (ImGui::Checkbox("Show FPS in title bar", &perf)) { s_cfg.display_perf_title = perf; s_dirty = true; }
}

static void Panel_Audio(void) {
  bool ea = s_cfg.enable_audio;
  if (ImGui::Checkbox("Enable audio", &ea)) { s_cfg.enable_audio = ea; s_dirty = true; }
  RestartTag();

  static const int kFreqs[] = { 11025, 22050, 44100, 48000 };
  static const char *const kFreqLbl[] = { "11025", "22050", "44100", "48000" };
  int fi = 2;
  for (int i = 0; i < 4; i++) if (s_cfg.audio_freq == kFreqs[i]) fi = i;
  if (ComboInt("Frequency (Hz)", &fi, kFreqLbl, 4)) { s_cfg.audio_freq = (uint16)kFreqs[fi]; s_dirty = true; }
  RestartTag();

  static const char *const kCh[] = { "Mono", "Stereo" };
  int ci = (s_cfg.audio_channels == 1) ? 0 : 1;
  if (ComboInt("Channels", &ci, kCh, 2)) { s_cfg.audio_channels = (uint8)(ci == 0 ? 1 : 2); s_dirty = true; }
  RestartTag();

  static const int kSamp[] = { 512, 1024, 2048, 4096 };
  static const char *const kSampLbl[] = { "512", "1024", "2048", "4096" };
  int si = 2;
  for (int i = 0; i < 4; i++) if (s_cfg.audio_samples == kSamp[i]) si = i;
  if (ComboInt("Buffer size", &si, kSampLbl, 4)) { s_cfg.audio_samples = (uint16)kSamp[si]; s_dirty = true; }
  RestartTag();

  ImGui::SeparatorText("MSU-1 music");
  static const char *const kMsu[] = { "Off", "On", "Deluxe", "OPUZ", "Deluxe + OPUZ" };
  int mi = 0;
  bool deluxe = (s_cfg.enable_msu & kMsuEnabled_MsuDeluxe) != 0;
  bool opuz   = (s_cfg.enable_msu & kMsuEnabled_Opuz) != 0;
  bool msu    = (s_cfg.enable_msu & kMsuEnabled_Msu) != 0;
  if (deluxe && opuz) mi = 4; else if (deluxe) mi = 2; else if (opuz) mi = 3; else if (msu) mi = 1; else mi = 0;
  if (ComboInt("MSU support", &mi, kMsu, 5)) {
    uint8 m = 0;
    switch (mi) {
    case 1: m = kMsuEnabled_Msu; break;
    case 2: m = kMsuEnabled_MsuDeluxe; break;
    case 3: m = kMsuEnabled_Opuz; break;
    case 4: m = kMsuEnabled_MsuDeluxe | kMsuEnabled_Opuz; break;
    default: m = 0; break;
    }
    s_cfg.enable_msu = m; s_dirty = true;
  }
  RestartTag();
  if (ImGui::InputText("MSU path prefix", s_path_msu, sizeof s_path_msu)) s_dirty = true;
  RestartTag();
  bool resume = s_cfg.resume_msu;
  if (ImGui::Checkbox("Resume MSU position per area", &resume)) { s_cfg.resume_msu = resume; s_dirty = true; }
  int vol = s_cfg.msuvolume;
  if (ImGui::SliderInt("MSU volume", &vol, 0, 100)) { s_cfg.msuvolume = (uint8)vol; s_dirty = true; }
}

// UI-ONLY entropy for the cosmetics "New random seed" button (mirrors
// rando_window.cpp's RollRandomSeed). NOT the game RNG — the cosmetic seed is a
// pure input to the deterministic cosmetic generator, so this never touches
// determinism. SplitMix64 over the high-res counter + a monotonic salt so two
// rolls in the same tick still differ.
static uint64 CosmeticRollSeed(void) {
  static uint64 s_salt = 0;
  s_salt += 0x1234567ull;
  uint64 x = (uint64)SDL_GetPerformanceCounter() ^ s_salt;
  x += 0x9E3779B97F4A7C15ull;
  x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
  x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
  return x ^ (x >> 31);
}

// Cosmetic shuffles are a LOCAL presentation layer: they do not affect the seed,
// share string, settings hash, or logic (see openspec add-rando-cosmetic-shuffles).
// Palette mode + music toggle apply live; the sprite-folder pick is launch-time.
static void Panel_Cosmetics(void) {
  ImGui::TextWrapped("Local look only. Cosmetics do NOT change the seed, share string, "
                     "or logic. Two players with the same share string but different "
                     "Cosmetic seed play the same game with a different look.");
  ImGui::Separator();

  static const char *const kPal[] = { "Vanilla (off)", "Shuffled", "Grayscale", "Negative" };
  int pi = s_cfg.cosmetic_palette_mode;
  if (pi < 0 || pi > 3) pi = 0;
  if (ComboInt("Palette shuffle", &pi, kPal, 4)) { s_cfg.cosmetic_palette_mode = (uint8)pi; s_dirty = true; }
  Help("Recolors the palette. Applies live.");

  bool ms = s_cfg.cosmetic_music_shuffle;
  if (ImGui::Checkbox("Music shuffle (area background songs)", &ms)) { s_cfg.cosmetic_music_shuffle = ms; s_dirty = true; }
  Help("Remaps the area background music. Applies live. Honors an MSU-1 pack if loaded.");

  // Hex u64 seed input + a "New random seed" button (matches the rando window's
  // seed field). 0 = derive the look from the loaded slot's seed.
  static char s_seed_buf[20];
  static uint64 s_seed_mirror = ~0ull;  // forces a re-format on first use / re-sync
  if (s_seed_mirror != s_cfg.cosmetic_seed) {
    snprintf(s_seed_buf, sizeof s_seed_buf, "%016llx", (unsigned long long)s_cfg.cosmetic_seed);
    s_seed_mirror = s_cfg.cosmetic_seed;
  }
  ImGui::SetNextItemWidth(220);
  if (ImGui::InputText("Cosmetic seed (hex)", s_seed_buf, sizeof s_seed_buf,
                       ImGuiInputTextFlags_CharsHexadecimal)) {
    unsigned long long v = 0;
    if (s_seed_buf[16] == '\0' && sscanf(s_seed_buf, "%llx", &v) == 1) {
      s_cfg.cosmetic_seed = (uint64)v;
      s_seed_mirror = s_cfg.cosmetic_seed;  // accept; don't reformat under the cursor
      s_dirty = true;
    }
  }
  Help("Any 64-bit value (1-16 hex digits). 0 = derive the look from the loaded "
       "slot's seed; any other value fixes the look across seeds.");
  ImGui::SameLine();
  if (ImGui::Button("New random seed")) {
    s_cfg.cosmetic_seed = CosmeticRollSeed();
    s_seed_mirror = ~0ull;  // force re-format from the new value
    s_dirty = true;
  }
  ImGui::TextDisabled("decimal: %llu", (unsigned long long)s_cfg.cosmetic_seed);

  ImGui::SeparatorText("Sprite shuffle");
  if (ImGui::InputText("Sprite folder (.zspr)", s_path_cosmetic_sprite, sizeof s_path_cosmetic_sprite)) s_dirty = true;
  RestartTag();
  ImGui::TextDisabled("A folder of .zspr files; one is picked by the cosmetic seed at launch.");
  ImGui::TextDisabled("Empty = use the single Link graphics from the Video tab.");
}

static void Panel_Gameplay(void) {
  ImGui::TextWrapped("These take effect immediately.");
  ImGui::SeparatorText("Convenience");
  FeatureCheckbox("Skip intro on keypress", kFeatures0_SkipIntroOnKeypress, "Skip the title/intro with any key.");
  FeatureCheckbox("Turn while dashing", kFeatures0_TurnWhileDashing, "Allow turning while dashing.");
  FeatureCheckbox("Cancel bird travel", kFeatures0_CancelBirdTravel, "Press X to cancel bird travel.");
  FeatureCheckbox("Switch items with L/R", kFeatures0_SwitchLR, "L/R cycle the selected item.");
  FeatureCheckbox("Limit L/R cycle to 4 items", kFeatures0_SwitchLRLimit, "Restrict L/R cycling to the first 4 items.");
  FeatureCheckbox("Show maxed counts in yellow", kFeatures0_ShowMaxItemsInYellow, "Max rupees/bombs/arrows shown in yellow.");
  FeatureCheckbox("Disable low-health beep", kFeatures0_DisableLowHealthBeep, "Mute the low-health warning.");

  ImGui::SeparatorText("Combat & items");
  FeatureCheckbox("Collect items with sword", kFeatures0_CollectItemsWithSword, "Sword swing collects nearby items.");
  FeatureCheckbox("Break pots with sword", kFeatures0_BreakPotsWithSword, "Lv2-4 sword breaks pots.");
  FeatureCheckbox("More active bombs", kFeatures0_MoreActiveBombs, "Allow 4 active bombs instead of 2.");
  FeatureCheckbox("Carry more rupees (9999)", kFeatures0_CarryMoreRupees, "Raise the rupee cap to 9999.");
  FeatureCheckbox("Mirror warps to Dark World", kFeatures0_MirrorToDarkworld, "Let the Mirror warp to the Dark World.");

  ImGui::SeparatorText("Bug fixes");
  FeatureCheckbox("Misc bug fixes", kFeatures0_MiscBugFixes, "Various fixes that don't change behavior.");
  FeatureCheckbox("Game-changing bug fixes", kFeatures0_GameChangingBugFixes, "Advanced fixes that alter game behavior.");

  ImGui::SeparatorText("General");
  bool autosave = s_cfg.autosave;
  if (ImGui::Checkbox("Autosave on quit / reload on start", &autosave)) { s_cfg.autosave = autosave; s_dirty = true; }
  bool dfd = s_cfg.disable_frame_delay;
  if (ImGui::Checkbox("Disable frame delay (exact-60Hz displays)", &dfd)) { s_cfg.disable_frame_delay = dfd; s_dirty = true; }
  if (ImGui::InputText("Language code", s_lang, sizeof s_lang)) s_dirty = true;
  RestartTag();
}

// ---------------------------------------------------------------------------
// Tab + action row
// ---------------------------------------------------------------------------
// Window appearance (applies immediately, not through Apply/INI). Theme persists
// via the rando_window.ini sidecar's dark_theme pref; UI scale is session-only.
static void Panel_Interface(void) {
  ImGui::TextDisabled("Appearance of this settings window. Applies immediately.");
  ImGui::SeparatorText("Theme");
  static const char *const kThemes[] = {"Dark", "Light"};
  int theme = g_rando_window_prefs.dark_theme ? 0 : 1;
  if (ComboInt("Theme", &theme, kThemes, 2)) {
    g_rando_window_prefs.dark_theme = (theme == 0);
    if (theme == 0) ImGui::StyleColorsDark();
    else ImGui::StyleColorsLight();
  }
  Help("Saved to saves/rando_window.ini.");
  ImGui::SeparatorText("Scale");
  float scale = ImGui::GetIO().FontGlobalScale;
  if (ImGui::SliderFloat("UI scale", &scale, 0.8f, 2.0f, "%.2fx"))
    ImGui::GetIO().FontGlobalScale = scale;
  ImGui::TextDisabled("Scales this window's text. Session-only (not saved).");
}

extern "C" void GameConfig_RenderTab(void) {
  if (!s_synced) SyncFromLive();

  // Reserve space for the bottom action row.
  float footer = ImGui::GetFrameHeightWithSpacing() + ImGui::GetTextLineHeightWithSpacing() + 8.0f;
  if (ImGui::BeginChild("##gc_body", ImVec2(0, -footer))) {
    if (ImGui::BeginTabBar("##gc_tabs")) {
      if (ImGui::BeginTabItem("Controls"))   { Panel_Controls();   ImGui::EndTabItem(); }
      if (ImGui::BeginTabItem("Controller")) { Panel_Controller(); ImGui::EndTabItem(); }
      if (ImGui::BeginTabItem("Video"))      { Panel_Video();      ImGui::EndTabItem(); }
      if (ImGui::BeginTabItem("Audio"))      { Panel_Audio();      ImGui::EndTabItem(); }
      if (ImGui::BeginTabItem("Cosmetics"))  { Panel_Cosmetics();  ImGui::EndTabItem(); }
      if (ImGui::BeginTabItem("Gameplay"))   { Panel_Gameplay();   ImGui::EndTabItem(); }
      if (ImGui::BeginTabItem("Interface"))  { Panel_Interface();  ImGui::EndTabItem(); }
      ImGui::EndTabBar();
    }
  }
  ImGui::EndChild();

  ImGui::Separator();
  if (!s_dirty) ImGui::BeginDisabled();
  if (ImGui::Button("Apply")) s_pending_apply = true;
  if (!s_dirty) ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Revert")) { SyncFromLive(); }
  Help("Discard unsaved edits and reload the current configuration.");

  if (s_restart_mask) {
    ImGui::TextColored(ImVec4(1, 0.85f, 0.3f, 1),
                       "Some changes (renderer/audio/video/language) take effect after you restart the game.");
  } else if (SDL_GetTicks() < s_toast_until) {
    ImGui::TextColored(ImVec4(1, 0.85f, 0.3f, 1), "%s", s_toast);
  } else {
    ImGui::TextDisabled("Bindings and gameplay toggles apply immediately; some video/audio options need a restart.");
  }
}

// ===========================================================================
// Debug tab. GameDebug_RenderTab is a nested-tab dispatcher; the inventory
// editor lives here (DbgInventory_Render), the other sub-panels in their own
// files. All edits go through the shared cheat-core (game_cheats.h): one safety
// gate, clamped/gated g_ram pokes — never an out-of-range byte to game code.
// ===========================================================================
extern "C" {
extern uint8 g_ram[0x20000];          // game-state RAM (zelda_rtl.c)
bool ZeldaIsReplaying(void);          // zelda_rtl.h
bool ZeldaIsEmulatorAttached(void);   // zelda_rtl.h
}

// --- Dungeon items (maps / compasses / big keys / small keys) --------------
// Maps/compasses/big keys are per-dungeon WORD bitfields (link_compass 0xF364,
// link_bigkey 0xF366, link_dungeon_map 0xF368). The bit for game-side dungeon
// index D is 0x8000 >> D (see rando.c:200-213). For a little-endian word at
// `base`, that bit lands in byte `base + (D<=7 ? 1 : 0)`, bit position
// (15 - D) & 7 — so we reuse the byte-oriented Cheats_BitCheckbox.
struct DbgDungeon { int gidx; const char *name; };
static const DbgDungeon kDbgDungeons[] = {
    {0,  "Hyrule Castle / Sewers"},
    {2,  "Eastern Palace"},
    {3,  "Desert Palace"},
    {10, "Tower of Hera"},
    {4,  "Hyrule Castle Tower"},
    {5,  "Palace of Darkness"},
    {6,  "Swamp Palace"},
    {7,  "Skull Woods"},
    {8,  "Thieves' Town"},
    {9,  "Ice Palace"},
    {11, "Misery Mire"},
    {12, "Turtle Rock"},
    {13, "Ganon's Tower"},
};

static void DbgDungeonBit(const char *id, uint32 wordbase, int gidx) {
  Cheats_BitCheckbox(id, wordbase + (gidx <= 7 ? 1 : 0), (15 - gidx) & 7);
}

static void DbgDungeonKeys(int gidx) {
  int v = g_ram[0xF37C + gidx];
  if (v > 99) v = 99;
  ImGui::SetNextItemWidth(96);
  if (ImGui::SliderInt("##keys", &v, 0, 99)) {
    Cheats_PokeByte(0xF37C + gidx, v, 0, 99);  // saved per-dungeon slot
    // If Link is standing in this dungeon, also bump the live counter so the
    // HUD/doors see it immediately (mirrors SaveDungeonKeys; HC proper raw
    // index 2 folds into slot 0). cur_palace_index_x2 low byte = 0xff outside.
    uint8 cur = (uint8)g_ram[0x40C];
    uint8 cur_slot = (cur == 0xff) ? 0xff : ((cur == 2) ? 0 : (cur >> 1));
    if (cur != 0xff && cur_slot == gidx) Cheats_PokeByte(0xF36F, v, 0, 99);
  }
}

void DbgInventory_Render(void) {
  bool can = Cheats_CanEdit();
  if (!can) {
    const char *why = ZeldaIsEmulatorAttached()
                          ? "Disabled while the original ROM is attached (RAM-compare mode)."
                      : ZeldaIsReplaying() ? "Disabled during snapshot replay."
                                           : "Load a save and enter the world to edit inventory.";
    ImGui::TextColored(ImVec4(1, 0.7f, 0.3f, 1), "%s", why);
  } else {
    ImGui::TextDisabled("Edits write live save RAM (shown on the HUD next frame). Save in-game to keep them.");
  }
  ImGui::Separator();

  if (!can) ImGui::BeginDisabled();
  if (ImGui::BeginChild("##dbg_inv", ImVec2(0, 0))) {
    bool rando = Cheats_RandoActive();
    int rupcap = (Cheats_Features0() & kFeatures0_CarryMoreRupees) ? 9999 : 999;

    ImGui::SeparatorText("Consumables");
    {
      int v = g_ram[0xF362] | (g_ram[0xF363] << 8);
      if (v > rupcap) v = rupcap;
      if (ImGui::SliderInt("Rupees", &v, 0, rupcap)) {  // set actual AND goal for an instant change
        Cheats_PokeWord(0xF362, v, 0, rupcap);
        Cheats_PokeWord(0xF360, v, 0, rupcap);
      }
    }
    Cheats_ByteSlider("Bombs", 0xF343, 0, 50);
    // Capacity-upgrade levels (link_bomb_upgrades 0xF370 / link_arrow_upgrades
    // 0xF371) index kMaxBombsForLevel[]/kMaxArrowsForLevel[] (hud.c:33-34), so
    // they must stay in [0,7]. Labels show the resulting max the HUD enforces.
    { static const char *const k[] = {"10", "15", "20", "25", "30", "35", "40", "50"};
      Cheats_Combo("Bomb capacity", 0xF370, k, 8); }
    Cheats_ByteSlider("Arrows", 0xF377, 0, 70);
    { static const char *const k[] = {"30", "35", "40", "45", "50", "55", "60", "70"};
      Cheats_Combo("Arrow capacity", 0xF371, k, 8); }
    Cheats_ByteSlider("Keys (current dungeon)", 0xF36F, 0, 99);
    Cheats_ByteSlider("Magic (128 = full)", 0xF36E, 0, 128);
    // Magic upgrade (link_magic_consumption 0xF37B): indexes the magic-cost
    // tables, so it must stay in [0,2]. 2 (Quarter) is a randomizer addition;
    // vanilla only ever has 0/1. The combo clamps to valid indices.
    { static const char *const k[] = {"None", "Half (1/2)", "Quarter (1/4)"};
      Cheats_Combo("Magic upgrade", 0xF37B, k, 3); }
    Cheats_ByteSlider("Heart pieces", 0xF36B, 0, 3);

    ImGui::SeparatorText("Hearts");
    {
      int containers = g_ram[0xF36C] / 8;
      if (containers < 1) containers = 1;
      if (ImGui::SliderInt("Heart containers", &containers, 1, 20)) {
        Cheats_PokeByte(0xF36C, containers * 8, 8, 160);
        if (g_ram[0xF36D] > g_ram[0xF36C]) Cheats_PokeByte(0xF36D, g_ram[0xF36C], 0, 160);  // clamp current<=capacity
      }
    }
    if (ImGui::Button("Refill hearts")) Cheats_PokeByte(0xF36D, g_ram[0xF36C], 0, 160);
    ImGui::SameLine();
    if (ImGui::Button("Refill magic")) Cheats_PokeByte(0xF36E, 0x80, 0, 128);

    ImGui::SeparatorText("Equipment");
    { static const char *const k[] = {"None", "Fighter", "Master", "Tempered", "Gold"}; Cheats_Combo("Sword", 0xF359, k, 5); }
    { static const char *const k[] = {"None", "Blue", "Red", "Mirror"}; Cheats_Combo("Shield", 0xF35A, k, 4); }
    { static const char *const k[] = {"Green", "Blue", "Red"}; Cheats_Combo("Armor", 0xF35B, k, 3); }
    { static const char *const k[] = {"None", "Power Glove", "Titan's Mitt"}; Cheats_Combo("Gloves", 0xF354, k, 3); }

    ImGui::SeparatorText("Items");
    // Bow byte 0xF340 has FIVE states: 0=none, 1=bow/no-arrows, 2=bow/arrows,
    // 3=silver/no-arrows, 4=silver/arrows. The HUD re-derives the low (has-arrows)
    // bit every frame from link_num_arrows (hud.c:1488-1499, :428-432), so a plain
    // exact-value combo {0,1,3} got stranded at "None" the moment the player had
    // arrows and the byte was bumped to 2/4. Normalize on READ to the tier, and on
    // WRITE store the no-arrows base (1 or 3); the HUD bumps it to 2/4 if arrows
    // are present. Tier (wood vs silver) keys off `>= 3`, unaffected by the bump.
    {
      static const char *const k[] = {"None", "Bow", "Silver Bow"};
      int raw = g_ram[0xF340];
      int idx = (raw == 0) ? 0 : (raw >= 3 ? 2 : 1);
      if (ImGui::BeginCombo("Bow", k[idx])) {
        for (int i = 0; i < 3; i++) {
          bool sel = (idx == i);
          if (ImGui::Selectable(k[i], sel) && i != idx)
            Cheats_PokeByte(0xF340, (i == 0) ? 0 : (i == 1) ? 1 : 3, 0, 4);
          if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
      }
    }
    { static const char *const k[] = {"None", "Blue", "Red"}; Cheats_Combo("Boomerang", 0xF341, k, 3); }  // no "both" — red replaces blue
    Cheats_Toggle("Hookshot", 0xF342);
    Cheats_Toggle("Lamp", 0xF34A);
    Cheats_Toggle("Fire Rod", 0xF345);
    Cheats_Toggle("Ice Rod", 0xF346);
    Cheats_Toggle("Hammer", 0xF34B);
    Cheats_Toggle("Bug Net", 0xF34D);
    Cheats_Toggle("Book of Mudora", 0xF34E);
    Cheats_Toggle("Cane of Somaria", 0xF350);
    Cheats_Toggle("Cane of Byrna", 0xF351);
    Cheats_Toggle("Magic Cape", 0xF352);
    // Mirror byte 0xF353: 1 = the "scroll" graphic (renders as a map-like icon and
    // misbehaves — hud.c:1260, misc.c:683, sprite_main.c:11598 all special-case 1),
    // 2 = the real Magic Mirror. The normal grant gives 2 (misc.c
    // kValueToGiveItemTo[26]==2), so a plain on/off toggle that wrote 1 showed the
    // wrong icon. Read "present" as nonzero; write 2 when enabling so it's always
    // the proper mirror.
    {
      bool on = (g_ram[0xF353] != 0);
      if (ImGui::Checkbox("Magic Mirror", &on)) Cheats_PokeByte(0xF353, on ? 2 : 0, 0, 2);
    }
    // Boots are special: the dash *action* is gated by link_ability_flags
    // (0xF379) bit 2, not the inventory byte (player.c attempt_action via
    // kAbilityBitmasks). The normal receive path sets both (misc.c:843), so the
    // plain item-byte toggle leaves boots visible in the menu but unusable.
    // Toggle the ability bit alongside 0xF355 to keep them coupled.
    // (Flippers needs no such fix: CheckAbilityToSwim() reads link_item_flippers
    // directly, so the plain item-byte toggle already enables swimming.)
    {
      bool v = g_ram[0xF355] != 0;
      if (ImGui::Checkbox("Pegasus Boots", &v)) {
        Cheats_PokeByte(0xF355, v ? 1 : 0, 0, 1);
        Cheats_PokeBit(0xF379, 2, v);  // link_ability_flags dash bit (0x04)
      }
    }
    Cheats_Toggle("Flippers", 0xF356);
    Cheats_Toggle("Moon Pearl", 0xF357);
    Cheats_Toggle("Bombos Medallion", 0xF347);
    Cheats_Toggle("Ether Medallion", 0xF348);
    Cheats_Toggle("Quake Medallion", 0xF349);

    // Mushroom/Powder (0xF344) and Shovel/Flute (0xF34C) each share ONE vanilla
    // byte but have separate randomizer ownership state (g_rando_mushroom_held,
    // g_rando_flute_shovel_owned). Editing the byte alone would desync rando
    // logic/tracking, so under an active slot we also write the matching
    // ownership state on change. These are single-select combos, so they can't
    // express "own both Shovel AND Flute" — they set ownership to exactly the
    // chosen item, which is the intuitive debug behavior.
    {
      static const char *const k[] = {"None", "Mushroom", "Magic Powder"};
      int v = g_ram[0xF344]; if (v < 0 || v > 2) v = 0;
      if (ImGui::BeginCombo("Mushroom / Powder", k[v])) {
        for (int i = 0; i < 3; i++) {
          bool sel = (v == i);
          if (ImGui::Selectable(k[i], sel) && i != v) {
            Cheats_PokeByte(0xF344, i, 0, 2);
            // Sync true-Mushroom possession: only "Mushroom" counts as held.
            if (rando && Cheats_CanEdit()) g_rando_mushroom_held = (i == 1) ? 1 : 0;
          }
          if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
      }
    }
    {
      static const char *const k[] = {"None", "Shovel", "Flute"};
      int v = g_ram[0xF34C]; if (v < 0 || v > 2) v = (v == 3) ? 2 : 0;  // 3=active flute -> "Flute"
      if (ImGui::BeginCombo("Shovel / Flute", k[v])) {
        for (int i = 0; i < 3; i++) {
          bool sel = (v == i);
          if (ImGui::Selectable(k[i], sel) && i != v) {
            Cheats_PokeByte(0xF34C, i, 0, 2);  // 0=none, 1=shovel, 2=flute(inactive)
            // Sync ownership bits to exactly the selected item (Shovel=0x01, Flute=0x02).
            if (rando && Cheats_CanEdit())
              g_rando_flute_shovel_owned = (i == 1) ? 0x01 : (i == 2) ? 0x02 : 0x00;
          }
          if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
      }
    }
    if (rando)
      ImGui::TextDisabled("(Editing these also updates the randomizer's ownership tracking.)");

    ImGui::SeparatorText("Bottles");
    {
      static const char *const k[] = {"No bottle", "Empty", "Red Potion", "Green Potion",
                                      "Blue Potion", "Fairy", "Bee", "Good Bee"};
      static const int v[] = {0, 2, 3, 4, 5, 6, 7, 8};  // kHudItemBottles[] index values (skip 1)
      for (int i = 0; i < 4; i++) {
        char lbl[24];
        snprintf(lbl, sizeof lbl, "Bottle %d", i + 1);
        Cheats_ComboVals(lbl, 0xF35C + i, k, v, 8);
      }
    }

    if (ImGui::TreeNode("Dungeon items (maps / compasses / big keys / keys)")) {
      ImGui::TextDisabled("Maps, compasses and big keys are per-dungeon flags; keys is each dungeon's saved count.");
      if (ImGui::BeginTable("##dbg_dungeon", 5,
                            ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("Dungeon", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Map");
        ImGui::TableSetupColumn("Compass");
        ImGui::TableSetupColumn("Big Key");
        ImGui::TableSetupColumn("Keys");
        ImGui::TableHeadersRow();
        for (int i = 0; i < (int)(sizeof kDbgDungeons / sizeof kDbgDungeons[0]); i++) {
          int d = kDbgDungeons[i].gidx;
          ImGui::TableNextRow();
          ImGui::PushID(d);
          ImGui::TableNextColumn(); ImGui::TextUnformatted(kDbgDungeons[i].name);
          ImGui::TableNextColumn(); DbgDungeonBit("##map", 0xF368, d);
          ImGui::TableNextColumn(); DbgDungeonBit("##comp", 0xF364, d);
          ImGui::TableNextColumn(); DbgDungeonBit("##bk", 0xF366, d);
          ImGui::TableNextColumn(); DbgDungeonKeys(d);
          ImGui::PopID();
        }
        ImGui::EndTable();
      }
      ImGui::TreePop();
    }

    if (ImGui::TreeNode("Progress (advanced \xe2\x80\x94 will not update randomizer prize/goal tracking)")) {
      ImGui::TextDisabled("Pendants");
      // Bit order per rando.c:312-314 (vanilla bit allocation): bit0=Blue/ToH=
      // Wisdom, bit1=Red/DP=Power, bit2=Green/EP=Courage.
      Cheats_BitCheckbox("Pendant of Wisdom", 0xF374, 0);
      Cheats_BitCheckbox("Pendant of Power", 0xF374, 1);
      Cheats_BitCheckbox("Pendant of Courage", 0xF374, 2);
      ImGui::TextDisabled("Crystals");
      for (int b = 0; b < 7; b++) {
        char l[16];
        snprintf(l, sizeof l, "Crystal %d", b + 1);
        Cheats_BitCheckbox(l, 0xF37A, b);
      }
      ImGui::TreePop();
    }

    ImGui::SeparatorText("Quick actions");
    if (ImGui::Button("Max consumables")) {
      Cheats_PokeWord(0xF362, rupcap, 0, rupcap);
      Cheats_PokeWord(0xF360, rupcap, 0, rupcap);
      Cheats_PokeByte(0xF343, 50, 0, 50);
      Cheats_PokeByte(0xF377, 70, 0, 70);
      Cheats_PokeByte(0xF36F, 99, 0, 99);
      Cheats_PokeByte(0xF36E, 0x80, 0, 128);
      Cheats_PokeByte(0xF36D, g_ram[0xF36C], 0, 160);
    }
    Help("Tops off rupees, bombs, arrows, current-dungeon keys, magic and hearts. "
         "Leaves your items, equipment and progress untouched.");
    ImGui::SameLine();
    // "Everything at max": every equipment tier, item, medallion, capacity and
    // counter at its ceiling. Each write goes through the clamped/gated pokes, so
    // a no-edit gate (replay / emulator / wrong module) leaves RAM untouched. The
    // few coupled bytes (boots ability bit, bow tier, shared Mushroom/Powder &
    // Shovel/Flute ownership, mirror value-2) are handled the same way their
    // individual widgets above handle them.
    if (ImGui::Button("Max inventory (everything)")) {
      // Consumables + capacities.
      Cheats_PokeWord(0xF362, rupcap, 0, rupcap);
      Cheats_PokeWord(0xF360, rupcap, 0, rupcap);
      Cheats_PokeByte(0xF343, 50, 0, 50);    // bombs
      Cheats_PokeByte(0xF370, 7, 0, 7);      // bomb capacity (-> 50)
      Cheats_PokeByte(0xF377, 70, 0, 70);    // arrows
      Cheats_PokeByte(0xF371, 7, 0, 7);      // arrow capacity (-> 70)
      Cheats_PokeByte(0xF36F, 99, 0, 99);    // keys (current dungeon)
      Cheats_PokeByte(0xF36E, 0x80, 0, 128); // magic (full)
      Cheats_PokeByte(0xF37B, 2, 0, 2);      // magic upgrade (1/4)
      // Hearts: 20 containers, fully healed.
      Cheats_PokeByte(0xF36C, 20 * 8, 8, 160);
      Cheats_PokeByte(0xF36D, 20 * 8, 0, 160);
      // Equipment tiers.
      Cheats_PokeByte(0xF359, 4, 0, 4);  // sword: Gold
      Cheats_PokeByte(0xF35A, 3, 0, 4);  // shield: Mirror
      Cheats_PokeByte(0xF35B, 2, 0, 2);  // armor: Red
      Cheats_PokeByte(0xF354, 2, 0, 2);  // gloves: Titan's Mitt
      // Items.
      Cheats_PokeByte(0xF340, 3, 0, 4);  // bow: Silver (HUD bumps to 4 with arrows)
      Cheats_PokeByte(0xF341, 2, 0, 3);  // boomerang: Red
      Cheats_PokeByte(0xF342, 1, 0, 1);  // hookshot
      Cheats_PokeByte(0xF345, 1, 0, 1);  // fire rod
      Cheats_PokeByte(0xF346, 1, 0, 1);  // ice rod
      Cheats_PokeByte(0xF34A, 1, 0, 1);  // lamp
      Cheats_PokeByte(0xF34B, 1, 0, 1);  // hammer
      Cheats_PokeByte(0xF34D, 1, 0, 1);  // bug net
      Cheats_PokeByte(0xF34E, 1, 0, 1);  // book of mudora
      Cheats_PokeByte(0xF350, 1, 0, 1);  // cane of somaria
      Cheats_PokeByte(0xF351, 1, 0, 1);  // cane of byrna
      Cheats_PokeByte(0xF352, 1, 0, 1);  // magic cape
      Cheats_PokeByte(0xF353, 2, 0, 2);  // magic mirror (value 2, not the scroll)
      Cheats_PokeByte(0xF355, 1, 0, 1);  // pegasus boots (item byte)
      Cheats_PokeBit(0xF379, 2, true);   // ... + dash ability bit (keeps boots usable)
      Cheats_PokeByte(0xF356, 1, 0, 1);  // flippers
      Cheats_PokeByte(0xF357, 1, 0, 1);  // moon pearl
      Cheats_PokeByte(0xF347, 1, 0, 1);  // bombos
      Cheats_PokeByte(0xF348, 1, 0, 1);  // ether
      Cheats_PokeByte(0xF349, 1, 0, 1);  // quake
      // Shared single-select bytes: give the more-capable variant and keep the
      // randomizer ownership state in sync (mirrors the combos above).
      Cheats_PokeByte(0xF344, 2, 0, 2);  // Mushroom/Powder -> Magic Powder
      Cheats_PokeByte(0xF34C, 2, 0, 2);  // Shovel/Flute -> Flute (inactive)
      if (rando && Cheats_CanEdit()) {
        g_rando_mushroom_held = 0;       // Powder, not the raw Mushroom
        g_rando_flute_shovel_owned = 0x02;  // Flute
      }
      // Four empty bottles (the inventory slots; contents stay consumable).
      for (int i = 0; i < 4; i++) Cheats_PokeByte(0xF35C + i, 2, 0, 8);
      // All pendants + crystals (advanced: does not update rando prize/goal tracking).
      Cheats_PokeByte(0xF374, 0x07, 0, 0x07);  // 3 pendant bits
      Cheats_PokeByte(0xF37A, 0x7F, 0, 0x7F);  // 7 crystal bits
      // Dungeon maps / compasses / big keys (every game-index bit), and a full key
      // count in each dungeon's saved slot.
      Cheats_PokeWord(0xF364, 0xFFFF, 0, 0xFFFF);  // compasses
      Cheats_PokeWord(0xF366, 0xFFFF, 0, 0xFFFF);  // big keys
      Cheats_PokeWord(0xF368, 0xFFFF, 0, 0xFFFF);  // maps
      for (int i = 0; i < (int)(sizeof kDbgDungeons / sizeof kDbgDungeons[0]); i++)
        Cheats_PokeByte(0xF37C + kDbgDungeons[i].gidx, 99, 0, 99);
    }
    Help("Gives every item/equipment tier, all medallions, full capacities, "
         "20 hearts, all dungeon items, pendants and crystals. Debug edits do "
         "NOT update the randomizer's prize/goal/check tracking.");
  }
  ImGui::EndChild();
  if (!can) ImGui::EndDisabled();
}

// Nested-tab dispatcher for the top-level "Debug" tab.
extern "C" void GameDebug_RenderTab(void) {
  if (ImGui::BeginTabBar("##dbg_tabs")) {
    if (ImGui::BeginTabItem("Watch"))     { DbgWatch_Render();     ImGui::EndTabItem(); }
    if (ImGui::BeginTabItem("Inventory")) { DbgInventory_Render(); ImGui::EndTabItem(); }
    if (ImGui::BeginTabItem("Snapshots")) { DbgSnapshots_Render(); ImGui::EndTabItem(); }
    if (ImGui::BeginTabItem("Time"))      { DbgTime_Render();      ImGui::EndTabItem(); }
    if (ImGui::BeginTabItem("Warp"))      { DbgWarp_Render();      ImGui::EndTabItem(); }
    if (ImGui::BeginTabItem("Flags"))     { DbgFlags_Render();     ImGui::EndTabItem(); }
    ImGui::EndTabBar();
  }
}

#endif  // Z3R_NATIVE_SETTINGS_WINDOW
