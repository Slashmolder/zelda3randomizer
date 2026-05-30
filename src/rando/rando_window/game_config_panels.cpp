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

#include "game_config_widgets.h"

extern "C" {
#include "../../config.h"     // g_config, g_keybind_*, Config_* helpers, kKeys_*, kGamepadBtn_*
#include "../../features.h"   // kFeatures0_* bit constants
}

// ---------------------------------------------------------------------------
// Working copy + UI state
// ---------------------------------------------------------------------------
static Config     s_cfg;
static char       s_path_link[512], s_path_shader[512], s_path_msu[512], s_lang[32];
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

// Render all slots of a multi-slot command as rows.
static void CommandRows(int cmd_index, bool pad) {
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
      RowsForBase(kKeys_RandoToggleItemTracker, false); RowsForBase(kKeys_RandoToggleLocationTracker, false);
      RowsForBase(kKeys_RandoRevealSpoiler, false); RowsForBase(kKeys_RandoItemTrackerWindow, false);
      RowsForBase(kKeys_RandoCheckTrackerWindow, false); RowsForBase(kKeys_RandoMapTrackerWindow, false);
      ImGui::TreePop();
    }
  }
  ImGui::EndChild();
}

static void Panel_Controller(void) {
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
      if (ImGui::BeginTabItem("Gameplay"))   { Panel_Gameplay();   ImGui::EndTabItem(); }
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

#endif  // Z3R_NATIVE_SETTINGS_WINDOW
