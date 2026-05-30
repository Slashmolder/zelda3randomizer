#pragma once
#include "types.h"
#include <SDL_keycode.h>

enum {
  kKeys_Null,
  kKeys_Controls,
  kKeys_Controls_Last = kKeys_Controls + 11,
  kKeys_Load,
  kKeys_Load_Last = kKeys_Load + 19,
  kKeys_Save,
  kKeys_Save_Last = kKeys_Save + 19,
  kKeys_Replay,
  kKeys_Replay_Last = kKeys_Replay + 19,
  kKeys_LoadRef,
  kKeys_LoadRef_Last = kKeys_LoadRef + 19,
  kKeys_ReplayRef,
  kKeys_ReplayRef_Last = kKeys_ReplayRef + 19,
  kKeys_CheatLife,
  kKeys_CheatKeys,
  kKeys_CheatEquipment,
  kKeys_CheatWalkThroughWalls,
  kKeys_ClearKeyLog,
  kKeys_StopReplay,
  kKeys_Fullscreen,
  kKeys_Reset,
  kKeys_Pause,
  kKeys_PauseDimmed,
  kKeys_Turbo,
  kKeys_ReplayTurbo,
  kKeys_WindowBigger,
  kKeys_WindowSmaller,
  kKeys_DisplayPerf,
  kKeys_ToggleRenderer,
  kKeys_VolumeUp,
  kKeys_VolumeDown,
  // Phase B Slice 1 — randomizer tracker overlay toggles. Default unbound;
  // both reset to hidden on each launch (in-memory only, not persisted).
  kKeys_RandoToggleItemTracker,
  kKeys_RandoToggleLocationTracker,
  // Phase B Slice 6 §62 — in-binary reveal-spoiler action. Default unbound;
  // when fired, the host calls Rando_RevealSpoiler for the active slot's
  // share string. No-op when no rando slot is active or no suppressed
  // (race-mode) spoiler file exists for that slot.
  kKeys_RandoRevealSpoiler,
  // Rich tracker windows (PC native windows, behind Z3R_NATIVE_SETTINGS_WINDOW).
  // Toggle the item / check / map tracker OS windows. Default unbound. The enum
  // entries are unconditional (so the keymap table stays index-stable across
  // platforms); the handler arms in main.c are gated — they no-op on Switch.
  kKeys_RandoItemTrackerWindow,
  kKeys_RandoCheckTrackerWindow,
  kKeys_RandoMapTrackerWindow,
  // Native game-config window — show/hide the Z3R Settings window in
  // configuration mode (no randomizer slot targeted). Default `` ` `` (backquote).
  // Unconditional enum entry (keymap table index stability); the handler in
  // main.c is gated by Z3R_NATIVE_SETTINGS_WINDOW and no-ops on Switch.
  kKeys_OpenSettings,
  kKeys_Total,
};

enum {
  kOutputMethod_SDL,
  kOutputMethod_SDLSoftware,
  kOutputMethod_OpenGL,
  kOutputMethod_OpenGL_ES,
};

typedef struct Config {
  int window_width;
  int window_height;
  bool enhanced_mode7;
  bool new_renderer;
  bool ignore_aspect_ratio;
  uint8 fullscreen;
  uint8 window_scale;
  bool enable_audio;
  bool linear_filtering;
  uint8 output_method;
  uint16 audio_freq;
  uint8 audio_channels;
  uint16 audio_samples;
  bool autosave;
  uint8 extended_aspect_ratio;
  bool extend_y;
  bool no_sprite_limits;
  bool display_perf_title;
  uint8 enable_msu;
  bool resume_msu;
  bool disable_frame_delay;
  uint8 msuvolume;
  uint32 features0;
  uint32 features1;  // randomizer kFeatures1_* bank (parsed from [randomizer] section, default 0)

  // Randomizer configuration (parsed from [randomizer] section in task 1.6).
  // For A0 these fields are unused but declared so the struct shape is stable.
  const char *rando_spoiler_dir;  // default: "<exe-dir>/spoilers"
  bool rando_race_mode_default;   // default: false
  // Dev-only override per tasks.md §11.1: keep RAM-compare frame check active
  // when rando is on. Lets developers attach the original ROM and observe
  // RAM divergences during dispatcher work. Default false (RAM-compare
  // short-circuits when rando is active). Not documented in user-facing keys.
  bool rando_debug_force_ram_compare;

  const char *link_graphics;
  char *memory_buffer;
  const char *shader;
  const char *msu_path;
  const char *language;
} Config;

enum {
  kMsuEnabled_Msu = 1,
  kMsuEnabled_MsuDeluxe = 2,
  kMsuEnabled_Opuz = 4,
};
enum {
  kGamepadBtn_Invalid = -1,
  kGamepadBtn_A,
  kGamepadBtn_B,
  kGamepadBtn_X,
  kGamepadBtn_Y,
  kGamepadBtn_Back,
  kGamepadBtn_Guide,
  kGamepadBtn_Start,
  kGamepadBtn_L3,
  kGamepadBtn_R3,
  kGamepadBtn_L1,
  kGamepadBtn_R1,
  kGamepadBtn_DpadUp,
  kGamepadBtn_DpadDown,
  kGamepadBtn_DpadLeft,
  kGamepadBtn_DpadRight,
  kGamepadBtn_L2,
  kGamepadBtn_R2,
  kGamepadBtn_Count,
};

extern Config g_config;

// ---------------------------------------------------------------------------
// Editable keybinding model (native game-config UI).
//
// These arrays are the SOURCE OF TRUTH for key/gamepad bindings; the runtime
// lookup hashes (keymap_hash / joymap_ents in config.c) are a DERIVED index
// rebuilt from them by Config_RebuildKeymap(). They are laid out flat, indexed
// by the EXPANDED command id (the same layout as kDefaultKbdControls), so
// kKeys_Controls+0..11 are 12 separate entries, etc.
//
// g_keybind_kbd[cmd]  : key_with_mod (kKeyMod_* bits | scancode/keycode low 9
//                       bits), 0 = unbound. Same encoding KeyMapHash_Add uses.
// g_keybind_pad[cmd]  : one gamepad binding per command (the model represents a
//                       single binding per action; the runtime hash may hold
//                       alternatives, of which the last parsed is kept here).
// ---------------------------------------------------------------------------
typedef struct PadBinding {
  int16 button;     // kGamepadBtn_* primary button, or kGamepadBtn_Invalid (-1) = unbound
  uint32 modifiers; // held-button bitmask (1<<kGamepadBtn_*); 0 = none
} PadBinding;

extern uint16 g_keybind_kbd[kKeys_Total];
extern PadBinding g_keybind_pad[kKeys_Total];

// Command enumeration for the bindings UI (wraps the static kKeyNameId table).
// Index 0 is the "Null" sentinel; iterate 1..Config_CommandCount()-1. CommandId
// returns the expanded base id (add the slot index 0..CommandSlots-1).
int Config_CommandCount(void);
const char *Config_CommandName(int index);
int Config_CommandId(int index);
int Config_CommandSlots(int index);

// Rebuild the runtime lookup hashes from g_keybind_kbd / g_keybind_pad. Called
// after the UI edits the model so rebinds take effect with no restart. The model
// must be conflict-free (no two commands share a key) — the UI guarantees this.
void Config_RebuildKeymap(void);

// Self-check (invoked from --rando-selftest): asserts that rebuilding the keymap
// from the editable model reproduces the same lookup results as the parse-time
// build (functional equivalence, not byte-identity). Prints OK/FAIL; exits(2) on
// failure. Pins the keybind-model refactor against silent divergence.
void Config_SelfCheckKeymap(void);

// Writer fidelity self-check (invoked from --rando-selftest, after the keymap
// check): round-trips Config_WriteIniFile over a temp INI and asserts managed
// keys update while comments / unknown sections / randomizer sections survive.
void Config_SelfCheckIniWriter(void);

// Compose the 16-bit key_with_mod from an SDL key event, identically to the
// runtime lookup in FindCmdForSdlKey (modifier-key self-exclusion included).
// Pass the EVENT's keysym.mod, not SDL_GetModState().
uint16 Config_EncodeKeyEvent(SDL_Keycode code, SDL_Keymod mod);

// Human-readable name for a key_with_mod, e.g. "Ctrl+F1". Returns false (and
// leaves out empty) when the keycode has no SDL name on the current layout, so
// the caller can preserve the prior INI line rather than emit a drop-on-reload
// empty token. `out` holds at least 64 bytes.
bool Config_DecodeKeyName(uint16 key_with_mod, char *out, size_t cap);

// Canonical gamepad button name (e.g. "L1","DpadUp","A"; never the alias "Lb").
// Returns NULL for kGamepadBtn_Invalid / out of range.
const char *Config_GamepadButtonName(int button);

// Render a PadBinding as "A" / "L1+Start" / "" (unbound). Returns false if the
// primary button is invalid (out left empty).
bool Config_PadBindingName(PadBinding b, char *out, size_t cap);

// ---------------------------------------------------------------------------
// Native settings window persistence (rando_window.ini sidecar).
//
// Persisted to/from the SIDECAR saves/rando_window.ini — NEVER the user's
// hand-edited zelda3.ini. All fields are copied out of the parse buffer (no
// pointer-into-buffer values), so Config_LoadAuxIniFile can free its temp
// buffer immediately after the parse pass.
//
// settings_canonical holds kSettingsCanonicalLen (=28) bytes of
// Settings_CanonicalSerialize output, encoded as 56 hex chars in the INI
// (key last_settings_canonical_hex). The window's startup-load round-trips it
// through Settings_CanonicalDeserialize and falls back to Settings_SetDefaults
// if the hex is corrupt or fails the round-trip check.
// ---------------------------------------------------------------------------
typedef struct RandoWindowPrefs {
  bool has_settings;             // true when last_settings_canonical_hex parsed OK
  uint8 settings_canonical[28];  // kSettingsCanonicalLen; canonical-serialized RandoSettings
  uint64 last_seed_u64;          // last UI-chosen seed
  int window_x, window_y, window_w, window_h;  // last settings-window geometry
  bool has_geometry;             // true when all four geometry keys were present
  bool dark_theme;               // ImGui theme (default true = dark)
} RandoWindowPrefs;

extern RandoWindowPrefs g_rando_window_prefs;

void ParseConfigFile(const char *filename);

// Load the rando-window sidecar (saves/rando_window.ini). Parses ONLY the
// [rando_window] + [RandoAssetDecisions] sections into g_rando_window_prefs /
// the asset-decision store; every other section is skipped (G1 whitelist). The
// temp buffer is freed before return (all consumed values are copied out).
// Returns gracefully (no-op) when the file is absent. MUST NOT be used in place
// of ParseConfigFile — it never repoints g_config.memory_buffer or re-registers
// default keys.
void Config_LoadAuxIniFile(const char *path);

// Atomically write the rando-window sidecar from g_rando_window_prefs +
// the asset-decision store. Creates the saves dir if missing. Writes to
// "<path>.tmp" then renames over `path` (_commit+MoveFileExA on Win32,
// fsync+rename elsewhere).
void Config_SaveRandoWindowIni(const char *path);

int FindCmdForSdlKey(SDL_Keycode code, SDL_Keymod mod);
int FindCmdForGamepadButton(int button, uint32 modifiers);

// ---------------------------------------------------------------------------
// Native game-config UI persistence + live-apply (config.c).
// ---------------------------------------------------------------------------

// The INI file ParseConfigFile actually read at startup (zelda3.user.ini when
// present, else zelda3.ini; or the explicit filename). Captured at depth-0 in
// ParseConfigFile (never inside the recursive ParseOneConfigFile). Defaults to
// "zelda3.ini" before any parse. This is the write target for Config_WriteIniFile.
const char *Config_GetLoadedIniPath(void);

// Rewrite ONLY the keys this UI manages ([KeyMap],[GamepadMap],[Graphics],
// [Sound],[General],[Features]) in place, preserving comments (# only), blank
// lines, key order, line endings (CRLF/LF), unknown keys, and unknown sections
// (incl. [Randomizer]/[RandoAssetDecisions]) verbatim. Writes atomically. On the
// first rewrite of the session a one-shot "<path>.bak" of the original is made.
// Values are taken from g_config + the keybind model, so persist == live.
void Config_WriteIniFile(const char *path);

// Categories that changed but need a restart (returned by Config_ApplyLive for
// the UI's restart notice).
enum {
  kCfgRestart_Video = 1,
  kCfgRestart_Audio = 2,
  kCfgRestart_Language = 4,
  kCfgRestart_Features = 8,
};

// Apply the safe live subset of (now vs prev): key/gamepad rebuild, the live
// features0 bits (geometry bits masked out and preserved), and the window hooks.
// Returns a bitmask of kCfgRestart_* for fields that changed but need a restart.
// The live feature/keymap step is skipped while a snapshot replay is active.
// Window ops go through the MainHost_Set* hooks (provided by main.c).
uint32 Config_ApplyLive(const Config *prev, const Config *now);

// Process-lifetime string arena for UI-set string fields (link_graphics, shader,
// msu_path, language). Returns an owned copy that lives until process exit, so
// g_config.* can point at it without touching g_config.memory_buffer.
const char *Config_InternString(const char *s);

// Window/renderer hooks implemented in main.c (absolute setters — NOT the
// relative ChangeWindowScale or the XOR fullscreen toggle). Declared here so
// Config_ApplyLive can call them; defined under Z3R_NATIVE_SETTINGS_WINDOW.
// SetWindowScale returns the achieved (possibly screen-fit-clamped) scale.
int  MainHost_SetWindowScale(int scale);
void MainHost_SetFullscreen(uint8 mode);   // 0=windowed, 1=desktop (2=exclusive is restart-only)
void MainHost_SetNewRenderer(bool on);
