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
