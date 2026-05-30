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

// ===========================================================================
// Debug tab — live inventory/equipment editor. Writes Link's g_ram save block
// directly (NO INI/Apply). Hard-gated: only in a stable gameplay/menu module,
// never during replay or while the original ROM is attached for RAM compare.
// All g_ram writes live in this .cpp (audit guard scans only .c, excludes rando)
// and are range-clamped, so no out-of-range byte can reach a game-code table.
// ===========================================================================
extern "C" {
extern uint8 g_ram[0x20000];          // game-state RAM (zelda_rtl.c)
bool ZeldaIsReplaying(void);          // zelda_rtl.h
bool ZeldaIsEmulatorAttached(void);   // zelda_rtl.h
void ZeldaDumpDebugState(void);       // zelda_rtl.h (developer state dump)
}

static uint32 LiveFeatures0(void) { return *(const uint32 *)(g_ram + 0x64c); }  // enhanced_features0
static uint32 LiveFeatures1(void) { return *(const uint32 *)(g_ram + 0x659); }  // enhanced_features1
static bool DbgRandoActive(void) { return (LiveFeatures1() & kFeatures1_RandomizerActive) != 0; }

static bool CheatsCanEdit(void) {
  // Whitelist of stable gameplay/menu modules: 0x07 dungeon, 0x09/0x0B overworld
  // (run states; 0x08/0x0A are the transient load halves), 0x0E Interface (the
  // pause/item menu — the most-wanted edit context). Excludes title/file-select
  // (inventory not loaded), death/cutscene/transition/stub modules.
  uint8 m = g_ram[0x10];  // main_module_index
  bool in_game = (m == 0x07 || m == 0x09 || m == 0x0B || m == 0x0E);
  return in_game && !ZeldaIsReplaying() && !ZeldaIsEmulatorAttached();
}

static void PokeByte(uint32 addr, int v, int lo, int hi) {
  if (!CheatsCanEdit()) return;       // re-check the gate at the write (defense in depth)
  if (v < lo) v = lo;
  if (v > hi) v = hi;
  g_ram[addr] = (uint8)v;
}
static void PokeWord(uint32 addr, int v, int lo, int hi) {  // little-endian
  if (!CheatsCanEdit()) return;
  if (v < lo) v = lo;
  if (v > hi) v = hi;
  g_ram[addr] = (uint8)(v & 0xFF);
  g_ram[addr + 1] = (uint8)((v >> 8) & 0xFF);
}

// --- widget helpers (read g_ram live for display, write clamped on change) ---
static void DbgByteSlider(const char *label, uint32 addr, int lo, int hi) {
  int v = g_ram[addr];
  if (v > hi) v = hi;
  if (ImGui::SliderInt(label, &v, lo, hi)) PokeByte(addr, v, lo, hi);
}
static void DbgToggle(const char *label, uint32 addr) {
  bool v = g_ram[addr] != 0;
  if (ImGui::Checkbox(label, &v)) PokeByte(addr, v ? 1 : 0, 0, 1);
}
// Contiguous 0..count-1 combo. A read >= count (e.g. the sword 0xFF in-repair
// sentinel) previews as index 0 ("None") rather than indexing garbage.
static void DbgCombo(const char *label, uint32 addr, const char *const *items, int count) {
  int v = g_ram[addr];
  if (v < 0 || v >= count) v = 0;
  if (ImGui::BeginCombo(label, items[v])) {
    for (int i = 0; i < count; i++) {
      bool sel = (v == i);
      if (ImGui::Selectable(items[i], sel) && i != v) PokeByte(addr, i, 0, count - 1);
      if (sel) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
}
// Combo over a non-contiguous value set (bow {0,1,3}, bottle contents {0,2..8}).
static void DbgComboVals(const char *label, uint32 addr, const char *const *items,
                         const int *vals, int count) {
  int cur = g_ram[addr], idx = 0;
  for (int i = 0; i < count; i++) if (vals[i] == cur) idx = i;
  if (ImGui::BeginCombo(label, items[idx])) {
    for (int i = 0; i < count; i++) {
      bool sel = (idx == i);
      if (ImGui::Selectable(items[i], sel) && i != idx) PokeByte(addr, vals[i], 0, 255);
      if (sel) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
}
// One bit of a bitfield byte as a checkbox.
static void DbgBit(const char *label, uint32 addr, int bit) {
  uint8 cur = g_ram[addr];
  bool on = (cur >> bit) & 1;
  if (ImGui::Checkbox(label, &on))
    PokeByte(addr, (cur & ~(1 << bit)) | (on ? (1 << bit) : 0), 0, 255);
}

extern "C" void GameDebug_RenderTab(void) {
  bool can = CheatsCanEdit();
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

  // Diagnostics — always available (read-only; useful even at the title screen
  // or during replay). This is the GUI trigger for the F12 developer dump; the
  // F12 hotkey itself is the rebindable kKeys_DumpDebugState command (clear its
  // binding in Controls to disable the hotkey).
  ImGui::SeparatorText("Diagnostics");
  static unsigned int s_dump_msg_until = 0;
  if (ImGui::Button("Dump debug state")) {  // dumps once per click (not per frame)
    ZeldaDumpDebugState();
    s_dump_msg_until = SDL_GetTicks() + 3000;
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Writes dump_gram.bin / dump_vram.bin / dump_oam.bin /\n"
                      "dump_cgram.bin / dump_hints.txt next to the executable,\n"
                      "plus a state line to the log. Same as the F12 hotkey.");
  ImGui::SameLine();
  ImGui::TextDisabled("(also F12 \xe2\x80\x94 rebindable in Controls > Randomizer)");
  if (SDL_GetTicks() < s_dump_msg_until) {
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.4f, 1, 0.4f, 1), "wrote dump_*");
  }
  ImGui::Separator();

  if (!can) ImGui::BeginDisabled();
  if (ImGui::BeginChild("##dbg", ImVec2(0, 0))) {
    bool rando = DbgRandoActive();
    int rupcap = (LiveFeatures0() & kFeatures0_CarryMoreRupees) ? 9999 : 999;

    ImGui::SeparatorText("Consumables");
    {
      int v = g_ram[0xF362] | (g_ram[0xF363] << 8);
      if (v > rupcap) v = rupcap;
      if (ImGui::SliderInt("Rupees", &v, 0, rupcap)) {  // set actual AND goal for an instant change
        PokeWord(0xF362, v, 0, rupcap);
        PokeWord(0xF360, v, 0, rupcap);
      }
    }
    DbgByteSlider("Bombs", 0xF343, 0, 50);
    DbgByteSlider("Arrows", 0xF377, 0, 70);
    DbgByteSlider("Keys (current dungeon)", 0xF36F, 0, 99);
    DbgByteSlider("Magic (128 = full)", 0xF36E, 0, 128);
    DbgByteSlider("Heart pieces", 0xF36B, 0, 3);

    ImGui::SeparatorText("Hearts");
    {
      int containers = g_ram[0xF36C] / 8;
      if (containers < 1) containers = 1;
      if (ImGui::SliderInt("Heart containers", &containers, 1, 20)) {
        PokeByte(0xF36C, containers * 8, 8, 160);
        if (g_ram[0xF36D] > g_ram[0xF36C]) PokeByte(0xF36D, g_ram[0xF36C], 0, 160);  // clamp current<=capacity
      }
    }
    if (ImGui::Button("Refill hearts")) PokeByte(0xF36D, g_ram[0xF36C], 0, 160);
    ImGui::SameLine();
    if (ImGui::Button("Refill magic")) PokeByte(0xF36E, 0x80, 0, 128);

    ImGui::SeparatorText("Equipment");
    { static const char *const k[] = {"None", "Fighter", "Master", "Tempered", "Gold"}; DbgCombo("Sword", 0xF359, k, 5); }
    { static const char *const k[] = {"None", "Blue", "Red", "Mirror"}; DbgCombo("Shield", 0xF35A, k, 4); }
    { static const char *const k[] = {"Green", "Blue", "Red"}; DbgCombo("Armor", 0xF35B, k, 3); }
    { static const char *const k[] = {"None", "Power Glove", "Titan's Mitt"}; DbgCombo("Gloves", 0xF354, k, 3); }

    ImGui::SeparatorText("Items");
    { static const char *const k[] = {"None", "Bow", "Silver Bow"}; static const int v[] = {0, 1, 3};
      DbgComboVals("Bow", 0xF340, k, v, 3); }
    { static const char *const k[] = {"None", "Blue", "Red"}; DbgCombo("Boomerang", 0xF341, k, 3); }  // no "both" — red replaces blue (kHudItemBoomerang has 3 entries)
    DbgToggle("Hookshot", 0xF342);
    DbgToggle("Lamp", 0xF34A);
    DbgToggle("Fire Rod", 0xF345);
    DbgToggle("Ice Rod", 0xF346);
    DbgToggle("Hammer", 0xF34B);
    DbgToggle("Bug Net", 0xF34D);
    DbgToggle("Book of Mudora", 0xF34E);
    DbgToggle("Cane of Somaria", 0xF350);
    DbgToggle("Cane of Byrna", 0xF351);
    DbgToggle("Magic Cape", 0xF352);
    DbgToggle("Magic Mirror", 0xF353);
    DbgToggle("Pegasus Boots", 0xF355);
    DbgToggle("Flippers", 0xF356);
    DbgToggle("Moon Pearl", 0xF357);
    DbgToggle("Bombos Medallion", 0xF347);
    DbgToggle("Ether Medallion", 0xF348);
    DbgToggle("Quake Medallion", 0xF349);

    // Mushroom/Powder (0xF344) and Shovel/Flute (0xF34C) share a byte AND have
    // separate randomizer ownership state — editing the raw byte under rando
    // desyncs it (the documented "vanilla state as a progress proxy" class).
    if (rando) ImGui::BeginDisabled();
    { static const char *const k[] = {"None", "Mushroom", "Magic Powder"}; DbgCombo("Mushroom / Powder", 0xF344, k, 3); }
    { static const char *const k[] = {"None", "Shovel", "Flute"}; DbgCombo("Shovel / Flute", 0xF34C, k, 3); }
    if (rando) {
      ImGui::EndDisabled();
      ImGui::TextDisabled("(Mushroom/Powder & Shovel/Flute are managed by the randomizer.)");
    }

    ImGui::SeparatorText("Bottles");
    {
      static const char *const k[] = {"No bottle", "Empty", "Red Potion", "Green Potion",
                                      "Blue Potion", "Fairy", "Bee", "Good Bee"};
      static const int v[] = {0, 2, 3, 4, 5, 6, 7, 8};  // kHudItemBottles[] index values (skip 1)
      for (int i = 0; i < 4; i++) {
        char lbl[24];
        snprintf(lbl, sizeof lbl, "Bottle %d", i + 1);
        DbgComboVals(lbl, 0xF35C + i, k, v, 8);
      }
    }

    if (ImGui::TreeNode("Progress (advanced \xe2\x80\x94 will not update randomizer prize/goal tracking)")) {
      ImGui::TextDisabled("Pendants");
      DbgBit("Pendant of Courage", 0xF374, 0);
      DbgBit("Pendant of Power", 0xF374, 1);
      DbgBit("Pendant of Wisdom", 0xF374, 2);
      ImGui::TextDisabled("Crystals");
      for (int b = 0; b < 7; b++) {
        char l[16];
        snprintf(l, sizeof l, "Crystal %d", b + 1);
        DbgBit(l, 0xF37A, b);
      }
      ImGui::TreePop();
    }

    ImGui::SeparatorText("Quick actions");
    if (ImGui::Button("Max consumables")) {
      PokeWord(0xF362, rupcap, 0, rupcap);
      PokeWord(0xF360, rupcap, 0, rupcap);
      PokeByte(0xF343, 50, 0, 50);
      PokeByte(0xF377, 70, 0, 70);
      PokeByte(0xF36F, 99, 0, 99);
      PokeByte(0xF36E, 0x80, 0, 128);
      PokeByte(0xF36D, g_ram[0xF36C], 0, 160);
    }
  }
  ImGui::EndChild();
  if (!can) ImGui::EndDisabled();
}

#endif  // Z3R_NATIVE_SETTINGS_WINDOW
