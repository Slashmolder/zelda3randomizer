// Windows headers must precede "types.h": types.h defines BYTE/WORD/DWORD/HIBYTE
// as function-style macros that collide with the same names as typedefs in
// winbase.h. Including windows.h first lets it lay down the typedefs (the macros
// then shadow them, which is fine for project use). Mirrors rando_save.c.
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>  // MoveFileExA, GetLastError
#include <io.h>       // _commit, _fileno
#include <direct.h>   // _mkdir
#endif

#include "config.h"
#include "types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include "features.h"
#include "util.h"
#include "rando/rando_asset_decisions.h"  // Rando_RegisterAssetDecisionFromIni
#include "rando/rando_settings.h"          // kSettingsCanonicalLen
#include "rando/shuffle_cosmetic.h"        // Cosmetic_ParsePaletteMode
#if !defined(_WIN32)
#include <unistd.h>   // fsync
#include <sys/stat.h> // mkdir
#endif

enum {
  kKeyMod_ScanCode = 0x200,
  kKeyMod_Alt = 0x400,
  kKeyMod_Shift = 0x800,
  kKeyMod_Ctrl = 0x1000,
};

Config g_config;

// Native-settings-window persistence. Zero-initialized: no
// settings/geometry until the sidecar is parsed; dark_theme defaults to true
// (set here so a missing sidecar still gives the dark theme).
RandoWindowPrefs g_rando_window_prefs = { .dark_theme = true };

#define REMAP_SDL_KEYCODE(key) ((key) & SDLK_SCANCODE_MASK ? kKeyMod_ScanCode : 0) | (key) & (kKeyMod_ScanCode - 1)
#define _(x) REMAP_SDL_KEYCODE(x)
#define S(x) REMAP_SDL_KEYCODE(x) | kKeyMod_Shift
#define A(x) REMAP_SDL_KEYCODE(x) | kKeyMod_Alt
#define C(x) REMAP_SDL_KEYCODE(x) | kKeyMod_Ctrl
#define N 0
static const uint16 kDefaultKbdControls[kKeys_Total] = {
  0,
  // Controls
  _(SDLK_UP), _(SDLK_DOWN), _(SDLK_LEFT), _(SDLK_RIGHT), _(SDLK_RSHIFT), _(SDLK_RETURN), _(SDLK_x), _(SDLK_z), _(SDLK_s), _(SDLK_a), _(SDLK_c), _(SDLK_v),
  // LoadState
  _(SDLK_F1), _(SDLK_F2), _(SDLK_F3), _(SDLK_F4), _(SDLK_F5), _(SDLK_F6), _(SDLK_F7), _(SDLK_F8), _(SDLK_F9), _(SDLK_F10), N, N, N, N, N, N, N, N, N, N,
  // SaveState
  S(SDLK_F1), S(SDLK_F2), S(SDLK_F3), S(SDLK_F4), S(SDLK_F5), S(SDLK_F6), S(SDLK_F7), S(SDLK_F8), S(SDLK_F9), S(SDLK_F10), N, N, N, N, N, N, N, N, N, N,
  // Replay State
  C(SDLK_F1), C(SDLK_F2), C(SDLK_F3), C(SDLK_F4), C(SDLK_F5), C(SDLK_F6), C(SDLK_F7), C(SDLK_F8), C(SDLK_F9), C(SDLK_F10), N, N, N, N, N, N, N, N, N, N,
  // Load Ref State
  N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N,
  // Replay Ref State
  N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N,
  // CheatLife, CheatKeys, CheatEquipment, CheatWalkThroughWalls
  _(SDLK_w), _(SDLK_o), S(SDLK_w), C(SDLK_e),
  // ClearKeyLog, StopReplay, Fullscreen, Reset, Pause, PauseDimmed, Turbo, ReplayTurbo, WindowBigger, WindowSmaller, DisplayPerf, ToggleRenderer
  _(SDLK_k), _(SDLK_l), A(SDLK_RETURN), C(SDLK_r), S(SDLK_p), _(SDLK_p), _(SDLK_TAB), _(SDLK_t), N, N, _(SDLK_f), _(SDLK_r),
  // VolumeUp, VolumeDown, RandoToggleItemTracker, RandoToggleLocationTracker,
  // RandoRevealSpoiler (all default-unbound), then the rich tracker windows:
  // Ctrl+I = Item, Ctrl+C = Check, Ctrl+M = Map (mnemonic, grouped, conflict-free).
  N, N, N, N, N, C(SDLK_i), C(SDLK_c), C(SDLK_m),
  // OpenSettings — backquote `` ` `` opens the native game-config window.
  _(SDLK_BACKQUOTE),
  // DumpDebugState — F12 writes the developer state dump.
  _(SDLK_F12),
};
#undef _
#undef A
#undef C
#undef S
#undef N

// The flat default table is indexed by expanded command id; a missing/extra
// entry would silently desync the keybind model and the keymap. Pin it.
_Static_assert(countof(kDefaultKbdControls) == kKeys_Total,
               "kDefaultKbdControls must have exactly kKeys_Total entries");

// Editable keybinding model — the source of truth (see config.h). Populated by
// ParseKeyArray/ParseGamepadArray during parse and by RegisterDefaultKeys for
// absent sections; the runtime hashes are derived from these via
// Config_RebuildKeymap. Zero / kGamepadBtn_Invalid initialized = all unbound
// until parse fills them.
uint16 g_keybind_kbd[kKeys_Total];
PadBinding g_keybind_pad[kKeys_Total];

typedef struct KeyNameId {
  const char *name;
  uint16 id, size;
} KeyNameId;

#define M(n) {#n, kKeys_##n, kKeys_##n##_Last - kKeys_##n + 1}
#define S(n) {#n, kKeys_##n, 1}
static const KeyNameId kKeyNameId[] = {
  {"Null", kKeys_Null, 65535},
  M(Controls), M(Load), M(Save), M(Replay), M(LoadRef), M(ReplayRef),
  S(CheatLife), S(CheatKeys), S(CheatEquipment), S(CheatWalkThroughWalls),
  S(ClearKeyLog), S(StopReplay), S(Fullscreen), S(Reset),
  S(Pause), S(PauseDimmed), S(Turbo), S(ReplayTurbo), S(WindowBigger), S(WindowSmaller), S(VolumeUp), S(VolumeDown), S(DisplayPerf), S(ToggleRenderer),
  // Phase B Slice 1 — randomizer tracker overlay toggles.
  S(RandoToggleItemTracker), S(RandoToggleLocationTracker),
  // Phase B Slice 6 §62 — in-binary reveal-spoiler action.
  S(RandoRevealSpoiler),
  // Rich tracker windows (PC). Toggle the item / check / map tracker windows.
  S(RandoItemTrackerWindow), S(RandoCheckTrackerWindow), S(RandoMapTrackerWindow),
  // Native game-config window toggle (config mode). INI key "OpenSettings".
  S(OpenSettings),
  // Developer state dump. INI key "DumpDebugState" (default F12).
  S(DumpDebugState),
};
#undef S
#undef M
// Config_WriteIniFile collects the keymap command names into a fixed keybuf[64].
_Static_assert(countof(kKeyNameId) <= 64, "kKeyNameId exceeds the writer's keybuf[64]");
typedef struct KeyMapHashEnt {
  uint16 key, cmd, next;
} KeyMapHashEnt;

static uint16 keymap_hash_first[255];
static KeyMapHashEnt *keymap_hash;
static int keymap_hash_size;
static bool has_keynameid[countof(kKeyNameId)];

static bool KeyMapHash_Add(uint16 key, uint16 cmd) {
  if ((keymap_hash_size & 0xff) == 0) {
    if (keymap_hash_size > 10000)
      Die("Too many keys");
    keymap_hash = realloc(keymap_hash, sizeof(KeyMapHashEnt) * (keymap_hash_size + 256));
  }
  int i = keymap_hash_size++;
  KeyMapHashEnt *ent = &keymap_hash[i];
  ent->key = key;
  ent->cmd = cmd;
  ent->next = 0;
  int j = (uint32)key % 255;

  uint16 *cur = &keymap_hash_first[j];
  while (*cur) {
    KeyMapHashEnt *ent = &keymap_hash[*cur - 1];
    if (ent->key == key)
      return false;
    cur = &ent->next;
  }
  *cur = i + 1;
  return true;
}

static int KeyMapHash_Find(uint16 key) {
  int i = keymap_hash_first[key % 255];
  while (i) {
    KeyMapHashEnt *ent = &keymap_hash[i - 1];
    if (ent->key == key)
      return ent->cmd;
    i = ent->next;
  }
  return 0;
}

uint16 Config_EncodeKeyEvent(SDL_Keycode code, SDL_Keymod mod) {
  if (code & ~(SDLK_SCANCODE_MASK | 0x1ff))
    return 0;
  int key = 0;
  if (code != SDLK_LALT && code != SDLK_RALT)
    key |=  mod & KMOD_ALT ? kKeyMod_Alt : 0;
  if (code != SDLK_LCTRL && code != SDLK_RCTRL)
    key |= mod & KMOD_CTRL ? kKeyMod_Ctrl : 0;
  if (code != SDLK_LSHIFT && code != SDLK_RSHIFT)
    key |= mod & KMOD_SHIFT ? kKeyMod_Shift : 0;
  key |= REMAP_SDL_KEYCODE(code);
  return (uint16)key;
}

int FindCmdForSdlKey(SDL_Keycode code, SDL_Keymod mod) {
  uint16 key = Config_EncodeKeyEvent(code, mod);
  if (key == 0)
    return 0;
  return KeyMapHash_Find(key);
}

static void ParseKeyArray(char *value, int cmd, int size) {
  char *s;
  int i = 0;
  for (; i < size && (s = NextDelim(&value, ',')) != NULL; i++, cmd += (cmd != 0)) {
    if (*s == 0)
      continue;
    int key_with_mod = 0;
    for (;;) {
      if (StringStartsWithNoCase(s, "Shift+")) {
        key_with_mod |= kKeyMod_Shift, s += 6;
      } else if (StringStartsWithNoCase(s, "Ctrl+")) {
        key_with_mod |= kKeyMod_Ctrl, s += 5;
      } else if (StringStartsWithNoCase(s, "Alt+")) {
        key_with_mod |= kKeyMod_Alt, s += 4;
      } else {
        break;
      }
    }
    SDL_Keycode key = SDL_GetKeyFromName(s);
    if (key == SDLK_UNKNOWN) {
      fprintf(stderr, "Unknown key: '%s'\n", s);
      continue;
    }
    uint16 kwm = (uint16)(key_with_mod | REMAP_SDL_KEYCODE(key));
    // Populate the editable model only when the key actually binds (a duplicate
    // is rejected by KeyMapHash_Add) so the model stays consistent with the hash
    // (no phantom binding the runtime hash dropped).
    if (!KeyMapHash_Add(kwm, cmd))
      fprintf(stderr, "Duplicate key: '%s'\n", s);
    else if (cmd >= 0 && cmd < kKeys_Total)
      g_keybind_kbd[cmd] = kwm;
  }
}

typedef struct GamepadMapEnt {
  uint32 modifiers;
  uint16 cmd, next;
} GamepadMapEnt;

static uint16 joymap_first[kGamepadBtn_Count];
static GamepadMapEnt *joymap_ents;
static int joymap_size;
static bool has_joypad_controls;

// Bitmask of which [rando_window] geometry keys (x=1,y=2,w=4,h=8) were seen
// during the current aux-INI parse pass. Config_LoadAuxIniFile resets it to 0
// before the pass and sets g_rando_window_prefs.has_geometry only when all four
// (== 0xF) were present.
static int g_rwp_geom_seen;

static int CountBits32(uint32 n) {
  int count = 0;
  for (; n != 0; count++)
    n &= (n - 1);
  return count;
}

static void GamepadMap_Add(int button, uint32 modifiers, uint16 cmd) {
  if ((joymap_size & 0xff) == 0) {
    if (joymap_size > 1000)
      Die("Too many joypad keys");
    joymap_ents = realloc(joymap_ents, sizeof(GamepadMapEnt) * (joymap_size + 64));
    if (!joymap_ents) Die("realloc failure");
  }
  uint16 *p = &joymap_first[button];
  // Insert it as early as possible but before after any entry with more modifiers.
  int cb = CountBits32(modifiers);
  while (*p && cb < CountBits32(joymap_ents[*p - 1].modifiers))
    p = &joymap_ents[*p - 1].next;
  int i = joymap_size++;
  GamepadMapEnt *ent = &joymap_ents[i];
  ent->modifiers = modifiers;
  ent->cmd = cmd;
  ent->next = *p;
  *p = i + 1;
}

int FindCmdForGamepadButton(int button, uint32 modifiers) {
  GamepadMapEnt *ent;
  for(int e = joymap_first[button]; e != 0; e = ent->next) {
    ent = &joymap_ents[e - 1];
    if ((modifiers & ent->modifiers) == ent->modifiers)
      return ent->cmd;
  }
  return 0;
}

static int ParseGamepadButtonName(const char **value) {
  const char *s = *value;
  // Longest substring first
  static const char *const kGamepadKeyNames[] = {
    "Back", "Guide", "Start", "L3", "R3",
    "L1", "R1", "DpadUp", "DpadDown", "DpadLeft", "DpadRight", "L2", "R2",
    "Lb", "Rb", "A", "B", "X", "Y"
  };
  static const uint8 kGamepadKeyIds[] = {
    kGamepadBtn_Back, kGamepadBtn_Guide, kGamepadBtn_Start, kGamepadBtn_L3, kGamepadBtn_R3,
    kGamepadBtn_L1, kGamepadBtn_R1, kGamepadBtn_DpadUp, kGamepadBtn_DpadDown, kGamepadBtn_DpadLeft, kGamepadBtn_DpadRight, kGamepadBtn_L2, kGamepadBtn_R2,
    kGamepadBtn_L1, kGamepadBtn_R1, kGamepadBtn_A, kGamepadBtn_B, kGamepadBtn_X, kGamepadBtn_Y,
  };
  for (size_t i = 0; i != countof(kGamepadKeyNames); i++) {
    const char *r = StringStartsWithNoCase(s, kGamepadKeyNames[i]);
    if (r) {
      *value = r;
      return kGamepadKeyIds[i];
    }
  }
  return kGamepadBtn_Invalid;
}

static const uint8 kDefaultGamepadCmds[] = {
  kGamepadBtn_DpadUp, kGamepadBtn_DpadDown, kGamepadBtn_DpadLeft, kGamepadBtn_DpadRight, kGamepadBtn_Back, kGamepadBtn_Start,
  kGamepadBtn_B, kGamepadBtn_A, kGamepadBtn_Y, kGamepadBtn_X, kGamepadBtn_L1, kGamepadBtn_R1,
};

static void ParseGamepadArray(char *value, int cmd, int size) {
  char *s;
  int i = 0;
  for (; i < size && (s = NextDelim(&value, ',')) != NULL; i++, cmd += (cmd != 0)) {
    if (*s == 0)
      continue;
    uint32 modifiers = 0;
    const char *ss = s;
    for (;;) {
      int button = ParseGamepadButtonName(&ss);
      if (button == kGamepadBtn_Invalid) BAD: {
        fprintf(stderr, "Unknown gamepad button: '%s'\n", s);
        break;
      }
      while (*ss == ' ' || *ss == '\t') ss++;
      if (*ss == '+') {
        ss++;
        modifiers |= 1 << button;
      } else if (*ss == 0) {
        if (cmd >= 0 && cmd < kKeys_Total) {
          // Populate the editable model (last binding parsed per command wins).
          g_keybind_pad[cmd].button = (int16)button;
          g_keybind_pad[cmd].modifiers = modifiers;
        }
        GamepadMap_Add(button, modifiers, cmd);
        break;
      } else
        goto BAD;
    }
  }
}

static void RegisterDefaultKeys() {
  for (int i = 1; i < countof(kKeyNameId); i++) {
    if (!has_keynameid[i]) {
      int size = kKeyNameId[i].size, k = kKeyNameId[i].id;
      for (int j = 0; j < size; j++, k++) {
        g_keybind_kbd[k] = kDefaultKbdControls[k];  // model mirrors the default
        KeyMapHash_Add(kDefaultKbdControls[k], k);
      }
    }
  }
  if (!has_joypad_controls) {
    for (int i = 0; i < countof(kDefaultGamepadCmds); i++) {
      g_keybind_pad[kKeys_Controls + i].button = (int16)kDefaultGamepadCmds[i];
      g_keybind_pad[kKeys_Controls + i].modifiers = 0;
      GamepadMap_Add(kDefaultGamepadCmds[i], 0, kKeys_Controls + i);
    }
  }
}

static int GetIniSection(const char *s) {
  if (StringEqualsNoCase(s, "[KeyMap]"))
    return 0;
  if (StringEqualsNoCase(s, "[Graphics]"))
    return 1;
  if (StringEqualsNoCase(s, "[Sound]"))
    return 2;
  if (StringEqualsNoCase(s, "[General]"))
    return 3;
  if (StringEqualsNoCase(s, "[Features]"))
    return 4;
  if (StringEqualsNoCase(s, "[GamepadMap]"))
    return 5;
  if (StringEqualsNoCase(s, "[Randomizer]"))
    return 6;
  if (StringEqualsNoCase(s, "[RandoAssetDecisions]"))
    return 7;
  if (StringEqualsNoCase(s, "[rando_window]"))
    return 8;
  if (StringEqualsNoCase(s, "[AutoTracker]"))
    return 9;
  return -1;
}

// §9.4 — asset-warn persistence. The [RandoAssetDecisions] section maps
// hex(g_assets_hash) keys to "allow" values. Read at startup; the settings
// screen consults the in-memory lookup to skip the warn dialog on
// previously-approved hashes. Writing back the user's "Always allow"
// choice is deferred to a follow-up sprint (config.c is read-only today);
// until then the decision persists for the current session.
// Rando_RegisterAssetDecisionFromIni now lives in the shared decision-store
// module (Phase P3) — included via "rando/rando_asset_decisions.h" above.

static int parse_hex_nibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

static bool ParseHashHex(const char *s, uint8 out[32]) {
  // Accept exactly 64 hex digits; reject everything else.
  for (int i = 0; i < 32; ++i) {
    int hi = parse_hex_nibble(s[i * 2]);
    int lo = parse_hex_nibble(s[i * 2 + 1]);
    if (hi < 0 || lo < 0) return false;
    out[i] = (uint8)((hi << 4) | lo);
  }
  if (s[64] != 0) return false;
  return true;
}

// Generic hex codec over an arbitrary byte length (do NOT reuse
// ParseHashHex's fixed-32 loop). Used for the 28-byte canonical settings blob
// in the rando-window sidecar. Decode requires EXACTLY `nbytes*2` hex digits
// (no trailing chars). Encode writes `nbytes*2` lowercase hex chars plus a NUL
// (out must hold at least nbytes*2+1).
static bool HexDecode(const char *s, uint8 *out, int nbytes) {
  for (int i = 0; i < nbytes; ++i) {
    // Check each nibble for NUL before reading the next, so a string shorter
    // than nbytes*2 cannot read past the terminator (audit MED over-read).
    char c0 = s[i * 2];
    if (c0 == '\0') return false;
    char c1 = s[i * 2 + 1];
    if (c1 == '\0') return false;
    int hi = parse_hex_nibble(c0);
    int lo = parse_hex_nibble(c1);
    if (hi < 0 || lo < 0) return false;
    out[i] = (uint8)((hi << 4) | lo);
  }
  return s[nbytes * 2] == 0;
}

static void HexEncode(const uint8 *bytes, int nbytes, char *out) {
  static const char kHex[] = "0123456789abcdef";
  for (int i = 0; i < nbytes; ++i) {
    out[i * 2] = kHex[(bytes[i] >> 4) & 0xF];
    out[i * 2 + 1] = kHex[bytes[i] & 0xF];
  }
  out[nbytes * 2] = 0;
}

bool ParseBool(const char *value, bool *result) {
  bool rv = false;
  switch (*value++ | 32) {
  case '0': if (*value == 0) break; return false;
  case 'f': if (StringEqualsNoCase(value, "alse")) break; return false;
  case 'n': if (StringEqualsNoCase(value, "o")) break; return false;
  case 'o':
    rv = (*value | 32) == 'n';
    if (StringEqualsNoCase(value, rv ? "n" : "ff")) break;
    return false;
  case '1': rv = true; if (*value == 0) break; return false;
  case 'y': rv = true; if (StringEqualsNoCase(value, "es")) break; return false;
  case 't': rv = true; if (StringEqualsNoCase(value, "rue")) break; return false;
  default: return false;
  }
  if (result) {
    *result = rv;
    return true;
  }
  return rv;
}

static bool ParseBoolBit(const char *value, uint32 *data, uint32 mask) {
  bool tmp;
  if (!ParseBool(value, &tmp))
    return false;
  *data = *data & ~mask | (tmp ? mask : 0);
  return true;
}

static bool HandleIniConfig(int section, const char *key, char *value) {
  if (section == 0) {
    for (int i = 0; i < countof(kKeyNameId); i++) {
      if (StringEqualsNoCase(key, kKeyNameId[i].name)) {
        has_keynameid[i] = true;
        ParseKeyArray(value, kKeyNameId[i].id, kKeyNameId[i].size);
        return true;
      }
    }
  } else if (section == 5) {
    for (int i = 0; i < countof(kKeyNameId); i++) {
      if (StringEqualsNoCase(key, kKeyNameId[i].name)) {
        if (i == 1)
          has_joypad_controls = true;
        ParseGamepadArray(value, kKeyNameId[i].id, kKeyNameId[i].size);
        return true;
      }
    }
  } else if (section == 1) {
    if (StringEqualsNoCase(key, "WindowSize")) {
      char *s;
      if (StringEqualsNoCase(value, "Auto")){
        g_config.window_width  = 0;
        g_config.window_height = 0;
        return true;
      }
      while ((s = NextDelim(&value, 'x')) != NULL) {
        if(g_config.window_width == 0) {
          g_config.window_width = atoi(s);
        } else {
          g_config.window_height = atoi(s);
          return true;
        }
      }
    } else if (StringEqualsNoCase(key, "EnhancedMode7")) {
      return ParseBool(value, &g_config.enhanced_mode7);
    } else if (StringEqualsNoCase(key, "NewRenderer")) {
      return ParseBool(value, &g_config.new_renderer);
    } else if (StringEqualsNoCase(key, "IgnoreAspectRatio")) {
      return ParseBool(value, &g_config.ignore_aspect_ratio);
    } else if (StringEqualsNoCase(key, "Fullscreen")) {
      g_config.fullscreen = (uint8)strtol(value, (char**)NULL, 10);
      return true;
    } else if (StringEqualsNoCase(key, "WindowScale")) {
      g_config.window_scale = (uint8)strtol(value, (char**)NULL, 10);
      return true;
    } else if (StringEqualsNoCase(key, "OutputMethod")) {
      g_config.output_method = StringEqualsNoCase(value, "SDL-Software") ? kOutputMethod_SDLSoftware :
                               StringEqualsNoCase(value, "OpenGL") ? kOutputMethod_OpenGL : 
                               StringEqualsNoCase(value, "OpenGL ES") ? kOutputMethod_OpenGL_ES :
                                                                        kOutputMethod_SDL;
      return true;
    } else if (StringEqualsNoCase(key, "LinearFiltering")) {
      return ParseBool(value, &g_config.linear_filtering);
    } else if (StringEqualsNoCase(key, "NoSpriteLimits")) {
      return ParseBool(value, &g_config.no_sprite_limits);
    } else if (StringEqualsNoCase(key, "LinkGraphics")) {
      g_config.link_graphics = value;
      return true;
    } else if (StringEqualsNoCase(key, "Shader")) {
      g_config.shader = *value ? value : NULL;
      return true;
    } else if (StringEqualsNoCase(key, "DimFlashes")) {
      return ParseBoolBit(value, &g_config.features0, kFeatures0_DimFlashes);
    } else if (StringEqualsNoCase(key, "PaletteShuffle")) {
      g_config.cosmetic_palette_mode = Cosmetic_ParsePaletteMode(value);
      return true;
    } else if (StringEqualsNoCase(key, "SpriteShuffle")) {
      g_config.cosmetic_sprite_dir =
          (*value && !StringEqualsNoCase(value, "off")) ? value : NULL;
      return true;
    } else if (StringEqualsNoCase(key, "CosmeticSeed")) {
      g_config.cosmetic_seed = (uint64)strtoull(value, NULL, 0);
      return true;
    } else if (StringEqualsNoCase(key, "FieldItemSprites")) {
      return ParseBool(value, &g_config.field_item_sprites);
    }
  } else if (section == 2) {
    if (StringEqualsNoCase(key, "EnableAudio")) {
      return ParseBool(value, &g_config.enable_audio);
    } else if (StringEqualsNoCase(key, "AudioFreq")) {
      g_config.audio_freq = (uint16)strtol(value, (char**)NULL, 10);
      return true;
    } else if (StringEqualsNoCase(key, "AudioChannels")) {
      g_config.audio_channels = (uint8)strtol(value, (char**)NULL, 10);
      return true;
    } else if (StringEqualsNoCase(key, "AudioSamples")) {
      g_config.audio_samples = (uint16)strtol(value, (char**)NULL, 10);
      return true;
    } else if (StringEqualsNoCase(key, "EnableMSU")) {
        if (StringEqualsNoCase(value, "opuz"))
        g_config.enable_msu = kMsuEnabled_Opuz;
      else if (StringEqualsNoCase(value, "deluxe"))
        g_config.enable_msu = kMsuEnabled_MsuDeluxe;
      else if (StringEqualsNoCase(value, "deluxe-opuz"))
        g_config.enable_msu = kMsuEnabled_MsuDeluxe | kMsuEnabled_Opuz;
      else 
        return ParseBool(value, (bool*)&g_config.enable_msu);
      return true;
    } else if (StringEqualsNoCase(key, "MSUPath")) {
      g_config.msu_path = value;
      return true;
    } else if (StringEqualsNoCase(key, "MSUVolume")) {
      g_config.msuvolume = atoi(value);
      return true;
    } else if (StringEqualsNoCase(key, "ResumeMSU")) {
      return ParseBool(value, &g_config.resume_msu);
    } else if (StringEqualsNoCase(key, "MusicShuffle")) {
      return ParseBool(value, &g_config.cosmetic_music_shuffle);
    }
  } else if (section == 3) {
    if (StringEqualsNoCase(key, "Autosave")) {
      g_config.autosave = (bool)strtol(value, (char**)NULL, 10);
      return true;
    } else if (StringEqualsNoCase(key, "ExtendedAspectRatio")) {
      const char* s;
      int h = 224;
      bool nospr = false, novis = false;
      // todo: make it not depend on the order
      while ((s = NextDelim(&value, ',')) != NULL) {
        if (strcmp(s, "extend_y") == 0)
          h = 240, g_config.extend_y = true;
        else if (strcmp(s, "16:9") == 0)
          g_config.extended_aspect_ratio = (h * 16 / 9 - 256) / 2;
        else if (strcmp(s, "16:10") == 0)
          g_config.extended_aspect_ratio = (h * 16 / 10 - 256) / 2;
        else if (strcmp(s, "18:9") == 0)
          g_config.extended_aspect_ratio = (h * 18 / 9 - 256) / 2;
        else if (strcmp(s, "4:3") == 0)
          g_config.extended_aspect_ratio = 0;
        else if (strcmp(s, "unchanged_sprites") == 0)
          nospr = true;
        else if (strcmp(s, "no_visual_fixes") == 0)
          novis = true;
        else
          return false;
      }
      if (g_config.extended_aspect_ratio && !nospr)
        g_config.features0 |= kFeatures0_ExtendScreen64;
      if (g_config.extended_aspect_ratio && !novis)
        g_config.features0 |= kFeatures0_WidescreenVisualFixes;
      return true;
    } else if (StringEqualsNoCase(key, "DisplayPerfInTitle")) {
      return ParseBool(value, &g_config.display_perf_title);
    } else if (StringEqualsNoCase(key, "DisableFrameDelay")) {
      return ParseBool(value, &g_config.disable_frame_delay);
    } else if (StringEqualsNoCase(key, "Language")) {
      g_config.language = value;
      return true;
    }
  } else if (section == 4) {
    if (StringEqualsNoCase(key, "ItemSwitchLR")) {
      return ParseBoolBit(value, &g_config.features0, kFeatures0_SwitchLR);
    } else if (StringEqualsNoCase(key, "ItemSwitchLRLimit")) {
      return ParseBoolBit(value, &g_config.features0, kFeatures0_SwitchLRLimit);
    } else if (StringEqualsNoCase(key, "TurnWhileDashing")) {
      return ParseBoolBit(value, &g_config.features0, kFeatures0_TurnWhileDashing);
    } else if (StringEqualsNoCase(key, "MirrorToDarkworld")) {
      return ParseBoolBit(value, &g_config.features0, kFeatures0_MirrorToDarkworld);
    } else if (StringEqualsNoCase(key, "CollectItemsWithSword")) {
      return ParseBoolBit(value, &g_config.features0, kFeatures0_CollectItemsWithSword);
    } else if (StringEqualsNoCase(key, "BreakPotsWithSword")) {
      return ParseBoolBit(value, &g_config.features0, kFeatures0_BreakPotsWithSword);
    } else if (StringEqualsNoCase(key, "DisableLowHealthBeep")) {
      return ParseBoolBit(value, &g_config.features0, kFeatures0_DisableLowHealthBeep);
    } else if (StringEqualsNoCase(key, "SkipIntroOnKeypress")) {
      return ParseBoolBit(value, &g_config.features0, kFeatures0_SkipIntroOnKeypress);
    } else if (StringEqualsNoCase(key, "ShowMaxItemsInYellow")) {
      return ParseBoolBit(value, &g_config.features0, kFeatures0_ShowMaxItemsInYellow);
    } else if (StringEqualsNoCase(key, "MoreActiveBombs")) {
      return ParseBoolBit(value, &g_config.features0, kFeatures0_MoreActiveBombs);
    } else if (StringEqualsNoCase(key, "CarryMoreRupees")) {
      return ParseBoolBit(value, &g_config.features0, kFeatures0_CarryMoreRupees);
    } else if (StringEqualsNoCase(key, "MiscBugFixes")) {
      return ParseBoolBit(value, &g_config.features0, kFeatures0_MiscBugFixes);
    } else if (StringEqualsNoCase(key, "GameChangingBugFixes")) {
      return ParseBoolBit(value, &g_config.features0, kFeatures0_GameChangingBugFixes);
    } else if (StringEqualsNoCase(key, "CancelBirdTravel")) {
      return ParseBoolBit(value, &g_config.features0, kFeatures0_CancelBirdTravel);
    } else if (StringEqualsNoCase(key, "EasyMinigames")) {
      return ParseBoolBit(value, &g_config.features0, kFeatures0_EasyMinigames);
    } else if (StringEqualsNoCase(key, "RestoreJpGlitches")) {
      return ParseBoolBit(value, &g_config.features0, kFeatures0_RestoreJpGlitches);
    } else if (StringEqualsNoCase(key, "JpOverworldMusic")) {
      return ParseBoolBit(value, &g_config.features0, kFeatures0_JpOverworldMusic);
    }
  } else if (section == 6) {
    // [Randomizer] section — tasks.md §1.6. Defaults set in ParseConfigFile
    // before the file is read; missing keys keep the defaults.
    if (StringEqualsNoCase(key, "Features1")) {
      g_config.features1 = (uint32)strtoul(value, (char**)NULL, 0);
      return true;
    } else if (StringEqualsNoCase(key, "SpoilerDir")) {
      g_config.rando_spoiler_dir = *value ? value : NULL;
      return true;
    } else if (StringEqualsNoCase(key, "RaceMode")) {
      return ParseBool(value, &g_config.rando_race_mode_default);
    } else if (StringEqualsNoCase(key, "DebugForceRamCompare")) {
      // Dev-only override (tasks.md §11.1). Not in the user-facing README key
      // map. When true + rando active, the dual-runtime frame comparator
      // keeps running (expect spew). Use to verify dispatcher coverage.
      return ParseBool(value, &g_config.rando_debug_force_ram_compare);
    }
    // Future keys (Phase A1+):
    //   GeneratorBudgetSeconds = 5     (default budget for assumed fill)
    //   AssetsMustBeVanilla = false    (auto-block generation on non-vanilla assets)
    //   TrackerEnabled = false         (in-game tracker default state)
  } else if (section == 7) {
    // [RandoAssetDecisions] — §9.4 asset-warn persistence. Each entry is
    //   <64-hex-char hash>=allow
    // The key is a full hex-encoded SHA-256 of the asset blob. Value MUST
    // be "allow" (Phase A only supports the always-allow decision; deny
    // is implicit — the absence of an entry means "ask again").
    if (StringEqualsNoCase(value, "allow")) {
      uint8 hash[32];
      if (ParseHashHex(key, hash)) {
        Rando_RegisterAssetDecisionFromIni(hash);
        return true;
      } else {
        fprintf(stderr, "[RandoAssetDecisions] bad hash key '%s' (need 64 hex chars)\n", key);
        return false;
      }
    }
    return false;
  } else if (section == 8) {
    // [rando_window] — native-settings-window persistence sidecar.
    // All values are copied out (no pointer-into-buffer), so the aux
    // loader can free its temp buffer after the parse pass.
    if (StringEqualsNoCase(key, "last_settings_canonical_hex")) {
      uint8 buf[kSettingsCanonicalLen];
      if (HexDecode(value, buf, kSettingsCanonicalLen)) {
        memcpy(g_rando_window_prefs.settings_canonical, buf, kSettingsCanonicalLen);
        g_rando_window_prefs.has_settings = true;
        return true;
      }
      fprintf(stderr, "[rando_window] bad last_settings_canonical_hex (need 56 hex chars)\n");
      g_rando_window_prefs.has_settings = false;
      return false;
    } else if (StringEqualsNoCase(key, "last_seed_u64")) {
      g_rando_window_prefs.last_seed_u64 = (uint64)strtoull(value, (char**)NULL, 10);
      return true;
    } else if (StringEqualsNoCase(key, "window_x")) {
      g_rando_window_prefs.window_x = atoi(value);
      g_rwp_geom_seen |= 1;
      return true;
    } else if (StringEqualsNoCase(key, "window_y")) {
      g_rando_window_prefs.window_y = atoi(value);
      g_rwp_geom_seen |= 2;
      return true;
    } else if (StringEqualsNoCase(key, "window_w")) {
      g_rando_window_prefs.window_w = atoi(value);
      g_rwp_geom_seen |= 4;
      return true;
    } else if (StringEqualsNoCase(key, "window_h")) {
      g_rando_window_prefs.window_h = atoi(value);
      g_rwp_geom_seen |= 8;
      return true;
    } else if (StringEqualsNoCase(key, "dark_theme")) {
      return ParseBool(value, &g_rando_window_prefs.dark_theme);
    }
    return false;
  } else if (section == 9) {
    // [AutoTracker] — opt-in local tracker server (src/rando/auto_tracker.{c,h}).
    // Defaults set in ParseConfigFile before the file is read; missing keys keep
    // the (off / localhost) defaults so an un-opted build is byte-identical.
    if (StringEqualsNoCase(key, "Enabled")) {
      return ParseBool(value, &g_config.auto_tracker_enabled);
    } else if (StringEqualsNoCase(key, "Port")) {
      g_config.auto_tracker_port = (uint16)strtoul(value, (char**)NULL, 10);
      return true;
    } else if (StringEqualsNoCase(key, "AllowRemote")) {
      return ParseBool(value, &g_config.auto_tracker_allow_remote);
    }
    return false;
  }
  return false;
}

static bool ParseOneConfigFile(const char *filename, int depth) {
  char *filedata = (char*)ReadWholeFile(filename, NULL), *p;
  if (!filedata)
    return false;
  
  int section = -2;
  g_config.memory_buffer = filedata;

  for (int lineno = 1; (p = NextLineStripComments(&filedata)) != NULL; lineno++) {
    if (*p == 0)
      continue; // empty line
    if (*p == '[') {
      section = GetIniSection(p);
      if (section < 0)
        fprintf(stderr, "%s:%d: Invalid .ini section %s\n", filename, lineno, p);
    } else if (*p == '!' && SkipPrefix(p + 1, "include ")) {
      char *tt = p + 8;
      char *new_filename = ReplaceFilenameWithNewPath(filename, NextPossiblyQuotedString(&tt));
      if (depth > 10 || !ParseOneConfigFile(new_filename, depth + 1))
        fprintf(stderr, "Warning: Unable to read %s\n", new_filename);
      free(new_filename);
    } else if (section == -2) {
      fprintf(stderr, "%s:%d: Expecting [section]\n", filename, lineno);
    } else {
      char *v = SplitKeyValue(p);
      if (v == NULL) {
        fprintf(stderr, "%s:%d: Expecting 'key=value'\n", filename, lineno);
        continue;
      }
      if (section >= 0 && !HandleIniConfig(section, p, v))
        fprintf(stderr, "%s:%d: Can't parse '%s'\n", filename, lineno, p);
    }
  }
  return true;
}

// The INI file ParseConfigFile resolved at depth-0 (the write target for
// Config_WriteIniFile). Captured here, NEVER inside the recursive
// ParseOneConfigFile (which re-points memory_buffer per !include child).
static char g_loaded_ini_path[512] = "zelda3.ini";

const char *Config_GetLoadedIniPath(void) { return g_loaded_ini_path; }

static void SetLoadedIniPath(const char *p) {
  snprintf(g_loaded_ini_path, sizeof g_loaded_ini_path, "%s", p);
}

void ParseConfigFile(const char *filename) {
  g_config.msuvolume = 100;  // default msu volume, 100%

  // QoL: the Digging Game / Chest Game easy-win. Default OFF for vanilla so the
  // original-ROM side-by-side compare is unaffected; opt in with
  // `EasyMinigames = 1` in zelda3.ini. The randomizer forces it on at
  // point-of-use (kFeatures1_RandomizerActive) without changing this default.

  // [Randomizer] defaults (tasks.md §1.6). Missing INI keys keep these.
  g_config.features1 = 0;                    // rando bank empty by default
  g_config.rando_spoiler_dir = NULL;         // resolved at runtime: <exe-dir>/spoilers
  g_config.rando_race_mode_default = false;
  g_config.rando_debug_force_ram_compare = false;  // dev-only override per §11.1
  g_config.field_item_sprites = true;        // add-rando-field-item-sprites: ON by default

  // [AutoTracker] defaults — opt-in, observation-only, localhost-only.
  g_config.auto_tracker_enabled = false;
  g_config.auto_tracker_port = 17400;
  g_config.auto_tracker_allow_remote = false;

  // Reset the editable keybind model before parsing. Keyboard 0 = unbound;
  // gamepad button -1 (kGamepadBtn_Invalid) = unbound (a zero-init would mean
  // "bound to A"). Parse + RegisterDefaultKeys fill in the bound entries.
  memset(g_keybind_kbd, 0, sizeof g_keybind_kbd);
  for (int i = 0; i < kKeys_Total; i++) {
    g_keybind_pad[i].button = kGamepadBtn_Invalid;
    g_keybind_pad[i].modifiers = 0;
  }

  if (filename != NULL) {
    if (!ParseOneConfigFile(filename, 0))
      fprintf(stderr, "Warning: Unable to read config file %s\n", filename);
    SetLoadedIniPath(filename);
  } else if (ParseOneConfigFile("zelda3.user.ini", 0)) {
    SetLoadedIniPath("zelda3.user.ini");
  } else {
    if (!ParseOneConfigFile("zelda3.ini", 0))
      fprintf(stderr, "Warning: Unable to read config file %s\n", "zelda3.ini");
    SetLoadedIniPath("zelda3.ini");
  }
  RegisterDefaultKeys();
}

// ===========================================================================
// Native-settings-window sidecar (saves/rando_window.ini).
// ===========================================================================

// Load the sidecar without disturbing g_config state.
// We replicate ParseOneConfigFile's [section]→GetIniSection→SplitKeyValue loop
// here (the section detection lives in the caller, NOT in HandleIniConfig) but
// dispatch ONLY the sections we own — section 7 ([RandoAssetDecisions]) and
// section 8 ([rando_window]). Every other section is skipped entirely, so a
// hand-edited foreign section (e.g. [Graphics]) can never cause HandleIniConfig
// to store a pointer into our temp buffer. All keys we DO consume copy their
// values out (hex/int/bool), so freeing the temp buffer after the pass is safe.
//
// MUST NOT call ParseConfigFile/ParseOneConfigFile: those repoint the
// long-lived g_config.memory_buffer (backing store for pointer-valued keys),
// reset rando defaults, and re-RegisterDefaultKeys().
void Config_LoadAuxIniFile(const char *path) {
  char *filedata = (char*)ReadWholeFile(path, NULL), *p;
  if (!filedata)
    return;  // absent sidecar — graceful no-op.

  // `cursor` is the running read position (advanced by NextLineStripComments);
  // `p` is the stripped line it returns. (Mirrors ParseOneConfigFile, which
  // passes &filedata as the cursor and assigns the line to a separate `p`.)
  char *cursor = filedata;
  g_rwp_geom_seen = 0;
  int section = -2;
  while ((p = NextLineStripComments(&cursor)) != NULL) {
    if (*p == 0)
      continue;  // empty line
    if (*p == '[') {
      section = GetIniSection(p);
      // Unknown / foreign sections become -1 here; the dispatch below skips
      // everything that isn't one of our two whitelisted sections, so we do
      // not warn (a hand-edited sidecar may legitimately carry other content).
    } else if (section == -2) {
      // Lines before any [section] header — ignore.
    } else {
      char *v = SplitKeyValue(p);
      if (v == NULL)
        continue;  // not key=value — ignore.
      // G1 whitelist: ONLY dispatch our own sections to HandleIniConfig.
      if (section == 7 || section == 8)
        HandleIniConfig(section, p, v);
      // All other sections (including the still-valid [Graphics]/[Sound]/etc.)
      // are deliberately NOT passed to HandleIniConfig — they would store raw
      // pointers into `filedata`, which we are about to free.
    }
  }

  g_rando_window_prefs.has_geometry = (g_rwp_geom_seen == 0xF);
  free(filedata);  // safe: all consumed values were copied out.
}

// Ensure the directory portion of `path` exists (best-effort; ignores errors).
static void EnsureParentDir(const char *path) {
  const char *slash = strrchr(path, '/');
  const char *bslash = strrchr(path, '\\');
  if (bslash > slash) slash = bslash;
  if (slash == NULL) return;
  size_t n = (size_t)(slash - path);
  if (n == 0 || n >= 256) return;
  char dir[256];
  memcpy(dir, path, n);
  dir[n] = 0;
#ifdef _WIN32
  _mkdir(dir);
#else
  mkdir(dir, 0755);
#endif
}

// Write the sidecar atomically from g_rando_window_prefs +
// the asset-decision store. NEVER touches zelda3.ini. Writes to "<path>.tmp",
// flushes to disk, then renames over `path`.
void Config_SaveRandoWindowIni(const char *path) {
  EnsureParentDir(path);

  char tmp[512];
  snprintf(tmp, sizeof tmp, "%s.tmp", path);

  FILE *f = fopen(tmp, "wb");
  if (!f) {
    fprintf(stderr, "Config_SaveRandoWindowIni: cannot open %s for writing\n", tmp);
    return;
  }

  fprintf(f, "# Native randomizer settings window state. Auto-generated; safe to delete.\n");
  fprintf(f, "[rando_window]\n");

  if (g_rando_window_prefs.has_settings) {
    char hex[kSettingsCanonicalLen * 2 + 1];
    HexEncode(g_rando_window_prefs.settings_canonical, kSettingsCanonicalLen, hex);
    fprintf(f, "last_settings_canonical_hex = %s\n", hex);
  }
  fprintf(f, "last_seed_u64 = %llu\n",
          (unsigned long long)g_rando_window_prefs.last_seed_u64);
  if (g_rando_window_prefs.has_geometry) {
    fprintf(f, "window_x = %d\n", g_rando_window_prefs.window_x);
    fprintf(f, "window_y = %d\n", g_rando_window_prefs.window_y);
    fprintf(f, "window_w = %d\n", g_rando_window_prefs.window_w);
    fprintf(f, "window_h = %d\n", g_rando_window_prefs.window_h);
  }
  fprintf(f, "dark_theme = %s\n", g_rando_window_prefs.dark_theme ? "true" : "false");

  // [RandoAssetDecisions] — flush all persisted always-allow decisions in the
  // same hash=allow line format config.c's section-7 parser reads back.
  int ndec = AssetDecision_Count();
  if (ndec > 0) {
    fprintf(f, "\n[RandoAssetDecisions]\n");
    for (int i = 0; i < ndec; i++) {
      const uint8 *h = AssetDecision_HashAt(i);
      if (!h) continue;
      char hex[32 * 2 + 1];
      HexEncode(h, 32, hex);
      fprintf(f, "%s = allow\n", hex);
    }
  }

  fflush(f);
#ifdef _WIN32
  _commit(_fileno(f));
  fclose(f);
  if (!MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING)) {
    fprintf(stderr, "Config_SaveRandoWindowIni: rename %s -> %s failed (err %lu)\n",
            tmp, path, (unsigned long)GetLastError());
    remove(tmp);  // don't leave an orphaned .tmp behind
  }
#else
  fsync(fileno(f));
  fclose(f);
  if (rename(tmp, path) != 0) {
    fprintf(stderr, "Config_SaveRandoWindowIni: rename %s -> %s failed\n", tmp, path);
    remove(tmp);  // don't leave an orphaned .tmp behind
  }
#endif
}

// ===========================================================================
// Native game-config UI — keybind model rebuild, name codecs, INI writer,
// live-apply. (config.c is read at startup elsewhere; this section makes it
// writable for the settings UI without ever clobbering user content.)
// ===========================================================================

int Config_CommandCount(void) { return (int)countof(kKeyNameId); }
const char *Config_CommandName(int i) {
  return (i >= 0 && i < (int)countof(kKeyNameId)) ? kKeyNameId[i].name : "";
}
int Config_CommandId(int i) {
  return (i >= 0 && i < (int)countof(kKeyNameId)) ? kKeyNameId[i].id : 0;
}
int Config_CommandSlots(int i) {
  return (i >= 0 && i < (int)countof(kKeyNameId)) ? kKeyNameId[i].size : 0;
}

void Config_SelfCheckKeymap(void) {
  // --rando-selftest runs before the normal startup parse, so populate the model
  // + parse-time hash first (defaults, plus any zelda3.ini in the CWD). Without
  // this the model is zero-initialized garbage (button 0 == A) and the check is
  // meaningless.
  ParseConfigFile(NULL);

  // 1. With the parse-time hash, every bound model entry must look up to itself,
  //    and every bound gamepad entry likewise. (No duplicate keys in defaults.)
  int fails = 0;
  for (int i = 0; i < kKeys_Total; i++) {
    if (g_keybind_kbd[i] != 0 && KeyMapHash_Find(g_keybind_kbd[i]) != i) {
      fprintf(stderr, "[Config_SelfCheckKeymap] FAIL: kbd cmd %d not found pre-rebuild\n", i);
      fails++;
    }
    if (g_keybind_pad[i].button >= 0 &&
        FindCmdForGamepadButton(g_keybind_pad[i].button, g_keybind_pad[i].modifiers) != i) {
      fprintf(stderr, "[Config_SelfCheckKeymap] FAIL: pad cmd %d not found pre-rebuild\n", i);
      fails++;
    }
  }
  // 2. Rebuild from the model and re-verify identical lookup results.
  Config_RebuildKeymap();
  for (int i = 0; i < kKeys_Total; i++) {
    if (g_keybind_kbd[i] != 0 && KeyMapHash_Find(g_keybind_kbd[i]) != i) {
      fprintf(stderr, "[Config_SelfCheckKeymap] FAIL: kbd cmd %d diverged after rebuild\n", i);
      fails++;
    }
    if (g_keybind_pad[i].button >= 0 &&
        FindCmdForGamepadButton(g_keybind_pad[i].button, g_keybind_pad[i].modifiers) != i) {
      fprintf(stderr, "[Config_SelfCheckKeymap] FAIL: pad cmd %d diverged after rebuild\n", i);
      fails++;
    }
  }
  if (fails) {
    fprintf(stderr, "[Config_SelfCheckKeymap] %d failure(s)\n", fails);
    exit(2);
  }
  fprintf(stderr, "[Config_SelfCheckKeymap] OK\n");
}

// Writer fidelity self-check (invoked from --rando-selftest, after the keymap
// check has populated g_config). Writes a known INI with comments / a trailing
// comment / an unknown section / a randomizer section to a temp file, changes a
// managed value, rewrites in place, and asserts the managed key changed while
// everything we don't own is preserved. exits(2) on failure.
void Config_SelfCheckIniWriter(void) {
  const char *path = "_cfg_writer_selftest.ini";
  const char *seed =
      "# test comment\r\n"
      "[Graphics]\r\n"
      "WindowScale = 2  # keep me\r\n"
      "\r\n"
      "[Custom]\r\n"
      "MyKey = MyVal\r\n"
      "[Randomizer]\r\n"
      "Features1 = 0\r\n";
  FILE *f = fopen(path, "wb");
  if (!f) { fprintf(stderr, "[Config_SelfCheckIniWriter] FAIL: cannot create temp\n"); exit(2); }
  fwrite(seed, 1, strlen(seed), f);
  fclose(f);

  uint8 saved_scale = g_config.window_scale;
  g_config.window_scale = 5;  // a managed change the writer must apply
  Config_WriteIniFile(path);
  g_config.window_scale = saved_scale;

  char *out = (char *)ReadWholeFile(path, NULL);
  int fails = 0;
  if (!out) { fprintf(stderr, "[Config_SelfCheckIniWriter] FAIL: temp vanished\n"); exit(2); }
  struct { const char *needle; bool want; } checks[] = {
    {"WindowScale = 5", true},   // managed value updated
    {"WindowScale = 2", false},  // old value gone
    {"# test comment", true},    // leading comment preserved
    {"# keep me", true},         // trailing comment preserved
    {"[Custom]", true},          // unknown section preserved
    {"MyKey = MyVal", true},     // unknown key preserved
    {"[Randomizer]", true},      // unowned section preserved
  };
  for (int i = 0; i < (int)(sizeof checks / sizeof checks[0]); i++) {
    bool present = strstr(out, checks[i].needle) != NULL;
    if (present != checks[i].want) {
      fprintf(stderr, "[Config_SelfCheckIniWriter] FAIL: '%s' %s\n",
              checks[i].needle, checks[i].want ? "missing" : "should be gone");
      fails++;
    }
  }
  free(out);
  remove(path);
  { char b[600]; snprintf(b, sizeof b, "%s.bak", path); remove(b);
    snprintf(b, sizeof b, "%s.tmp", path); remove(b); }
  if (fails) { fprintf(stderr, "[Config_SelfCheckIniWriter] %d failure(s)\n", fails); exit(2); }
  fprintf(stderr, "[Config_SelfCheckIniWriter] OK\n");
}

void Config_RebuildKeymap(void) {
  // Re-derive the runtime lookup hashes from the editable model. The realloc'd
  // backing buffers (keymap_hash / joymap_ents) are reused — reset the sizes and
  // the bucket-head index arrays, then re-add. The model is conflict-free (the
  // UI guarantees no duplicate key), so no KeyMapHash_Add rejection occurs.
  keymap_hash_size = 0;
  memset(keymap_hash_first, 0, sizeof keymap_hash_first);
  joymap_size = 0;
  memset(joymap_first, 0, sizeof joymap_first);
  has_joypad_controls = false;
  for (int i = 0; i < kKeys_Total; i++) {
    if (g_keybind_kbd[i] != 0)
      KeyMapHash_Add(g_keybind_kbd[i], (uint16)i);
    if (g_keybind_pad[i].button >= 0)
      GamepadMap_Add(g_keybind_pad[i].button, g_keybind_pad[i].modifiers, (uint16)i);
  }
}

bool Config_DecodeKeyName(uint16 k, char *out, size_t cap) {
  if (cap == 0) return false;
  out[0] = 0;
  uint16 low = k & (kKeyMod_ScanCode - 1);  // low 9 bits
  SDL_Keycode code = (k & kKeyMod_ScanCode)
                         ? (SDL_Keycode)(low | SDLK_SCANCODE_MASK)
                         : (SDL_Keycode)low;
  const char *name = SDL_GetKeyName(code);
  if (name == NULL || name[0] == 0)
    return false;  // unnamed on this layout — caller preserves the existing line
  char buf[96];
  buf[0] = 0;
  if (k & kKeyMod_Ctrl)  strncat(buf, "Ctrl+",  sizeof buf - strlen(buf) - 1);
  if (k & kKeyMod_Shift) strncat(buf, "Shift+", sizeof buf - strlen(buf) - 1);
  if (k & kKeyMod_Alt)   strncat(buf, "Alt+",   sizeof buf - strlen(buf) - 1);
  strncat(buf, name, sizeof buf - strlen(buf) - 1);
  snprintf(out, cap, "%s", buf);
  return true;
}

const char *Config_GamepadButtonName(int b) {
  switch (b) {
  case kGamepadBtn_A: return "A";
  case kGamepadBtn_B: return "B";
  case kGamepadBtn_X: return "X";
  case kGamepadBtn_Y: return "Y";
  case kGamepadBtn_Back: return "Back";
  case kGamepadBtn_Guide: return "Guide";
  case kGamepadBtn_Start: return "Start";
  case kGamepadBtn_L3: return "L3";
  case kGamepadBtn_R3: return "R3";
  case kGamepadBtn_L1: return "L1";
  case kGamepadBtn_R1: return "R1";
  case kGamepadBtn_DpadUp: return "DpadUp";
  case kGamepadBtn_DpadDown: return "DpadDown";
  case kGamepadBtn_DpadLeft: return "DpadLeft";
  case kGamepadBtn_DpadRight: return "DpadRight";
  case kGamepadBtn_L2: return "L2";
  case kGamepadBtn_R2: return "R2";
  default: return NULL;
  }
}

bool Config_PadBindingName(PadBinding b, char *out, size_t cap) {
  if (cap == 0) return false;
  out[0] = 0;
  if (b.button < 0) return false;
  const char *primary = Config_GamepadButtonName(b.button);
  if (primary == NULL) return false;
  char buf[96];
  buf[0] = 0;
  // Held buttons first ("L1+Start"), matching ParseGamepadArray's grammar.
  for (int i = 0; i < kGamepadBtn_Count; i++) {
    if (b.modifiers & (1u << i)) {
      const char *n = Config_GamepadButtonName(i);
      if (n) {
        strncat(buf, n, sizeof buf - strlen(buf) - 1);
        strncat(buf, "+", sizeof buf - strlen(buf) - 1);
      }
    }
  }
  strncat(buf, primary, sizeof buf - strlen(buf) - 1);
  snprintf(out, cap, "%s", buf);
  return true;
}

const char *Config_InternString(const char *s) {
  if (s == NULL) return NULL;
  size_t n = strlen(s) + 1;
  char *p = (char *)malloc(n);
  if (!p) return NULL;
  memcpy(p, s, n);
  return p;  // process-lifetime; intentionally never freed
}

// ---- Managed-key value rendering ------------------------------------------
// Returns false to SKIP this key entirely (preserve any existing line and do
// NOT insert it). An empty out-string is a valid value (used to clear a key);
// the insert pass additionally skips empty values so absent+empty keys aren't
// materialized.

static const char *const kGfxKeys[] = {
  "WindowSize", "Fullscreen", "WindowScale", "NewRenderer", "EnhancedMode7",
  "IgnoreAspectRatio", "NoSpriteLimits", "LinearFiltering", "OutputMethod",
  "Shader", "LinkGraphics", "DimFlashes",
  "PaletteShuffle", "SpriteShuffle", "CosmeticSeed",  // cosmetic shuffles (local)
};
static const char *const kSndKeys[] = {
  "EnableAudio", "AudioFreq", "AudioChannels", "AudioSamples", "EnableMSU",
  "MSUPath", "MSUVolume", "ResumeMSU",
  "MusicShuffle",  // cosmetic music shuffle (local)
};
static const char *const kGenKeys[] = {
  "Autosave", "ExtendedAspectRatio", "DisplayPerfInTitle", "DisableFrameDelay",
  "Language",
};
static const char *const kFeatKeys[] = {
  "ItemSwitchLR", "ItemSwitchLRLimit", "TurnWhileDashing", "MirrorToDarkworld",
  "CollectItemsWithSword", "BreakPotsWithSword", "DisableLowHealthBeep",
  "SkipIntroOnKeypress", "ShowMaxItemsInYellow", "MoreActiveBombs",
  "CarryMoreRupees", "MiscBugFixes", "GameChangingBugFixes", "CancelBirdTravel",
  "EasyMinigames", "RestoreJpGlitches", "JpOverworldMusic",
};
static const uint32 kFeatMasks[] = {
  kFeatures0_SwitchLR, kFeatures0_SwitchLRLimit, kFeatures0_TurnWhileDashing,
  kFeatures0_MirrorToDarkworld, kFeatures0_CollectItemsWithSword,
  kFeatures0_BreakPotsWithSword, kFeatures0_DisableLowHealthBeep,
  kFeatures0_SkipIntroOnKeypress, kFeatures0_ShowMaxItemsInYellow,
  kFeatures0_MoreActiveBombs, kFeatures0_CarryMoreRupees, kFeatures0_MiscBugFixes,
  kFeatures0_GameChangingBugFixes, kFeatures0_CancelBirdTravel,
  kFeatures0_EasyMinigames, kFeatures0_RestoreJpGlitches,
  kFeatures0_JpOverworldMusic,
};
_Static_assert(countof(kFeatKeys) == countof(kFeatMasks),
               "feature key/mask tables must stay aligned");

// Render the comma-joined binding list for a [KeyMap] (section 0) or
// [GamepadMap] (section 5) command. Trailing unbound slots are trimmed.
static bool RenderBindingLine(int section, const KeyNameId *kn, char *out, size_t cap) {
  char buf[1024];
  buf[0] = 0;
  int last_bound = -1;
  for (int j = 0; j < kn->size; j++) {
    int k = kn->id + j;
    bool bound = (section == 0) ? (g_keybind_kbd[k] != 0) : (g_keybind_pad[k].button >= 0);
    if (bound) last_bound = j;
  }
  for (int j = 0; j <= last_bound; j++) {
    int k = kn->id + j;
    char tok[96];
    tok[0] = 0;
    if (section == 0) {
      if (g_keybind_kbd[k] != 0 && !Config_DecodeKeyName(g_keybind_kbd[k], tok, sizeof tok))
        return false;  // an unnamed key — preserve the whole existing line
    } else {
      if (g_keybind_pad[k].button >= 0 && !Config_PadBindingName(g_keybind_pad[k], tok, sizeof tok))
        return false;
    }
    if (j > 0) strncat(buf, ", ", sizeof buf - strlen(buf) - 1);
    strncat(buf, tok, sizeof buf - strlen(buf) - 1);
  }
  snprintf(out, cap, "%s", buf);
  return true;  // last_bound == -1 ⇒ empty value (clears the key)
}

static bool RenderManagedValue(int section, const char *key, char *out, size_t cap) {
  // Keymap / gamepad: dispatch by command name.
  if (section == 0 || section == 5) {
    for (int i = 1; i < (int)countof(kKeyNameId); i++) {
      if (StringEqualsNoCase(key, kKeyNameId[i].name))
        return RenderBindingLine(section, &kKeyNameId[i], out, cap);
    }
    return false;
  }
  if (section == 1) {  // [Graphics]
    if (StringEqualsNoCase(key, "WindowSize")) {
      if (g_config.window_width == 0 && g_config.window_height == 0) snprintf(out, cap, "Auto");
      else snprintf(out, cap, "%dx%d", g_config.window_width, g_config.window_height);
    } else if (StringEqualsNoCase(key, "Fullscreen")) snprintf(out, cap, "%d", g_config.fullscreen);
    else if (StringEqualsNoCase(key, "WindowScale")) snprintf(out, cap, "%d", g_config.window_scale);
    else if (StringEqualsNoCase(key, "NewRenderer")) snprintf(out, cap, "%s", g_config.new_renderer ? "true" : "false");
    else if (StringEqualsNoCase(key, "EnhancedMode7")) snprintf(out, cap, "%s", g_config.enhanced_mode7 ? "true" : "false");
    else if (StringEqualsNoCase(key, "IgnoreAspectRatio")) snprintf(out, cap, "%s", g_config.ignore_aspect_ratio ? "true" : "false");
    else if (StringEqualsNoCase(key, "NoSpriteLimits")) snprintf(out, cap, "%s", g_config.no_sprite_limits ? "true" : "false");
    else if (StringEqualsNoCase(key, "LinearFiltering")) snprintf(out, cap, "%s", g_config.linear_filtering ? "true" : "false");
    else if (StringEqualsNoCase(key, "OutputMethod"))
      snprintf(out, cap, "%s", g_config.output_method == kOutputMethod_SDLSoftware ? "SDL-Software"
                               : g_config.output_method == kOutputMethod_OpenGL ? "OpenGL"
                               : g_config.output_method == kOutputMethod_OpenGL_ES ? "OpenGL ES" : "SDL");
    else if (StringEqualsNoCase(key, "Shader")) snprintf(out, cap, "%s", g_config.shader ? g_config.shader : "");
    else if (StringEqualsNoCase(key, "LinkGraphics")) snprintf(out, cap, "%s", g_config.link_graphics ? g_config.link_graphics : "");
    else if (StringEqualsNoCase(key, "DimFlashes")) snprintf(out, cap, "%s", (g_config.features0 & kFeatures0_DimFlashes) ? "true" : "false");
    else if (StringEqualsNoCase(key, "PaletteShuffle")) {
      uint8 m = g_config.cosmetic_palette_mode;
      snprintf(out, cap, "%s", m == kCosmeticPalette_Shuffled ? "shuffled"
                               : m == kCosmeticPalette_Grayscale ? "grayscale"
                               : m == kCosmeticPalette_Negative ? "negative" : "vanilla");
    }
    else if (StringEqualsNoCase(key, "SpriteShuffle")) snprintf(out, cap, "%s", g_config.cosmetic_sprite_dir ? g_config.cosmetic_sprite_dir : "off");
    else if (StringEqualsNoCase(key, "CosmeticSeed")) snprintf(out, cap, "%llu", (unsigned long long)g_config.cosmetic_seed);
    else return false;
    return true;
  }
  if (section == 2) {  // [Sound]
    if (StringEqualsNoCase(key, "EnableAudio")) snprintf(out, cap, "%s", g_config.enable_audio ? "true" : "false");
    else if (StringEqualsNoCase(key, "AudioFreq")) snprintf(out, cap, "%d", g_config.audio_freq);
    else if (StringEqualsNoCase(key, "AudioChannels")) snprintf(out, cap, "%d", g_config.audio_channels);
    else if (StringEqualsNoCase(key, "AudioSamples")) snprintf(out, cap, "%d", g_config.audio_samples);
    else if (StringEqualsNoCase(key, "EnableMSU")) {
      uint8 m = g_config.enable_msu;
      bool deluxe = (m & kMsuEnabled_MsuDeluxe) != 0, opuz = (m & kMsuEnabled_Opuz) != 0;
      if (deluxe && opuz) snprintf(out, cap, "deluxe-opuz");
      else if (deluxe) snprintf(out, cap, "deluxe");
      else if (opuz) snprintf(out, cap, "opuz");
      else snprintf(out, cap, "%s", (m & kMsuEnabled_Msu) ? "true" : "false");
    } else if (StringEqualsNoCase(key, "MSUPath")) snprintf(out, cap, "%s", g_config.msu_path ? g_config.msu_path : "");
    else if (StringEqualsNoCase(key, "MSUVolume")) snprintf(out, cap, "%d", g_config.msuvolume);
    else if (StringEqualsNoCase(key, "ResumeMSU")) snprintf(out, cap, "%s", g_config.resume_msu ? "true" : "false");
    else if (StringEqualsNoCase(key, "MusicShuffle")) snprintf(out, cap, "%s", g_config.cosmetic_music_shuffle ? "true" : "false");
    else return false;
    return true;
  }
  if (section == 3) {  // [General]
    if (StringEqualsNoCase(key, "Autosave")) snprintf(out, cap, "%d", g_config.autosave ? 1 : 0);  // parser uses strtol
    else if (StringEqualsNoCase(key, "DisplayPerfInTitle")) snprintf(out, cap, "%s", g_config.display_perf_title ? "true" : "false");
    else if (StringEqualsNoCase(key, "DisableFrameDelay")) snprintf(out, cap, "%s", g_config.disable_frame_delay ? "true" : "false");
    else if (StringEqualsNoCase(key, "Language")) snprintf(out, cap, "%s", g_config.language ? g_config.language : "");
    else if (StringEqualsNoCase(key, "ExtendedAspectRatio")) {
      // Recover the ratio token from the stored value. The parser is
      // order-dependent (computes the ratio at whatever height it has seen so
      // far), so a given ratio can be stored as either its h=224 or h=240 value
      // — match BOTH. Emit `extend_y` FIRST when set so the parser recomputes the
      // ratio at h=240, reproducing the value Config_ApplyLive/the UI committed.
      uint8 ar = g_config.extended_aspect_ratio;
      const char *ratio = NULL;
      if (ar == 0) ratio = "4:3";
      else if (ar == (uint8)((224 * 16 / 9 - 256) / 2) || ar == (uint8)((240 * 16 / 9 - 256) / 2)) ratio = "16:9";
      else if (ar == (uint8)((224 * 16 / 10 - 256) / 2) || ar == (uint8)((240 * 16 / 10 - 256) / 2)) ratio = "16:10";
      else if (ar == (uint8)((224 * 18 / 9 - 256) / 2) || ar == (uint8)((240 * 18 / 9 - 256) / 2)) ratio = "18:9";
      if (ratio == NULL) return false;  // hand-set custom value — leave untouched
      char tmp[64];
      tmp[0] = 0;
      if (g_config.extend_y) strncat(tmp, "extend_y,", sizeof tmp - strlen(tmp) - 1);
      strncat(tmp, ratio, sizeof tmp - strlen(tmp) - 1);
      if (ar != 0 && !(g_config.features0 & kFeatures0_ExtendScreen64))
        strncat(tmp, ",unchanged_sprites", sizeof tmp - strlen(tmp) - 1);
      if (ar != 0 && !(g_config.features0 & kFeatures0_WidescreenVisualFixes))
        strncat(tmp, ",no_visual_fixes", sizeof tmp - strlen(tmp) - 1);
      snprintf(out, cap, "%s", tmp);
    } else return false;
    return true;
  }
  if (section == 4) {  // [Features]
    for (int i = 0; i < (int)countof(kFeatKeys); i++) {
      if (StringEqualsNoCase(key, kFeatKeys[i])) {
        snprintf(out, cap, "%s", (g_config.features0 & kFeatMasks[i]) ? "true" : "false");
        return true;
      }
    }
    return false;
  }
  return false;
}

// ---- In-place INI writer ---------------------------------------------------

typedef struct IniLines { char **v; int *sec; int n, cap; } IniLines;

static void IniLines_Push(IniLines *L, char *line, int section) {
  if (L->n == L->cap) {
    L->cap = L->cap ? L->cap * 2 : 64;
    L->v = (char **)realloc(L->v, sizeof(char *) * L->cap);
    L->sec = (int *)realloc(L->sec, sizeof(int) * L->cap);
  }
  L->v[L->n] = line;
  L->sec[L->n] = section;
  L->n++;
}

static void IniLines_InsertAfter(IniLines *L, int idx, char *line, int section) {
  IniLines_Push(L, NULL, 0);  // grow by one
  for (int i = L->n - 1; i > idx + 1; i--) {
    L->v[i] = L->v[i - 1];
    L->sec[i] = L->sec[i - 1];
  }
  L->v[idx + 1] = line;
  L->sec[idx + 1] = section;
}

static char *DupRange(const char *p, size_t n) {
  char *r = (char *)malloc(n + 1);
  memcpy(r, p, n);
  r[n] = 0;
  return r;
}

// Is `line` a comment / blank (first non-space char absent or '#').
static bool LineIsCommentOrBlank(const char *line) {
  while (*line == ' ' || *line == '\t') line++;
  return *line == 0 || *line == '#';
}

// If `line` is a "[Section]" header (ignoring trailing comment + whitespace),
// return its GetIniSection id; else return INT_MIN.
static int LineHeaderSection(const char *line) {
  const char *p = line;
  while (*p == ' ' || *p == '\t') p++;
  if (*p != '[') return -2147483647 - 1;  // INT_MIN sentinel: not a header
  // copy up to end / '#'
  char hdr[64];
  size_t n = 0;
  while (p[n] && p[n] != '#' && n < sizeof hdr - 1) n++;
  // trim trailing whitespace
  while (n > 0 && (p[n - 1] == ' ' || p[n - 1] == '\t')) n--;
  memcpy(hdr, p, n);
  hdr[n] = 0;
  return GetIniSection(hdr);
}

// Extract the key (before '=') from a non-comment key line into `keyout`;
// returns the offset of the value (just past '='), or -1 if no '='.
static int LineKey(const char *line, char *keyout, size_t cap) {
  const char *eq = strchr(line, '=');
  if (!eq) return -1;
  const char *ks = line;
  while (*ks == ' ' || *ks == '\t') ks++;
  const char *ke = eq;
  while (ke > ks && (ke[-1] == ' ' || ke[-1] == '\t')) ke--;
  size_t n = (size_t)(ke - ks);
  if (n >= cap) n = cap - 1;
  memcpy(keyout, ks, n);
  keyout[n] = 0;
  return (int)(eq - line) + 1;
}

static const char *SectionName(int section) {
  switch (section) {
  case 0: return "[KeyMap]";
  case 1: return "[Graphics]";
  case 2: return "[Sound]";
  case 3: return "[General]";
  case 4: return "[Features]";
  case 5: return "[GamepadMap]";
  default: return NULL;
  }
}

// Build "<key> = <value>[ <trailing-comment>]". `srcline` (may be NULL) supplies
// a trailing '#...' comment to preserve.
static char *FormatKeyLine(const char *key, const char *value, const char *srcline) {
  const char *comment = NULL;
  if (srcline) {
    const char *eq = strchr(srcline, '=');
    if (eq) {
      const char *h = strchr(eq, '#');
      if (h) comment = h;
    }
  }
  char buf[1280];
  if (comment)
    snprintf(buf, sizeof buf, "%s = %s  %s", key, value, comment);
  else
    snprintf(buf, sizeof buf, "%s = %s", key, value);
  return DupRange(buf, strlen(buf));
}

static bool AtomicWriteBytes(const char *path, const char *data, size_t len) {
  EnsureParentDir(path);
  char tmp[520];
  snprintf(tmp, sizeof tmp, "%s.tmp", path);
  FILE *f = fopen(tmp, "wb");
  if (!f) {
    fprintf(stderr, "Config_WriteIniFile: cannot open %s\n", tmp);
    return false;
  }
  if (len) fwrite(data, 1, len, f);
  fflush(f);
#ifdef _WIN32
  _commit(_fileno(f));
  fclose(f);
  if (!MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING)) {
    fprintf(stderr, "Config_WriteIniFile: rename %s -> %s failed (err %lu)\n",
            tmp, path, (unsigned long)GetLastError());
    remove(tmp);
    return false;
  }
#else
  fsync(fileno(f));
  fclose(f);
  if (rename(tmp, path) != 0) {
    fprintf(stderr, "Config_WriteIniFile: rename %s -> %s failed\n", tmp, path);
    remove(tmp);
    return false;
  }
#endif
  return true;
}

// One-shot per session: copy the original file to "<path>.bak" before the first
// rewrite, so a botched edit is recoverable.
static bool g_ini_backed_up;

static void BackupOnce(const char *path) {
  if (g_ini_backed_up) return;
  g_ini_backed_up = true;
  size_t len = 0;
  char *orig = (char *)ReadWholeFile(path, &len);
  if (!orig) return;  // no original to back up
  char bak[520];
  snprintf(bak, sizeof bak, "%s.bak", path);
  AtomicWriteBytes(bak, orig, len);
  free(orig);
}

void Config_WriteIniFile(const char *path) {
  BackupOnce(path);

  char *data = (char *)ReadWholeFile(path, NULL);  // NUL-terminated; NULL if absent
  const char *eol = "\n";
  if (data) {
    const char *nl = strchr(data, '\n');
    if (nl && nl > data && nl[-1] == '\r') eol = "\r\n";
  }

  IniLines L = {0};
  if (data) {
    char *p = data;
    int cur = -2;  // before any [section]
    while (*p) {
      char *nl = strchr(p, '\n');
      size_t linelen = nl ? (size_t)(nl - p) : strlen(p);
      size_t l = linelen;
      if (l > 0 && p[l - 1] == '\r') l--;  // strip CR (EOL re-added on write)
      char *line = DupRange(p, l);
      int hs = LineHeaderSection(line);
      if (hs != (-2147483647 - 1)) cur = hs;  // header line updates current section
      IniLines_Push(&L, line, cur);
      if (!nl) break;
      p = nl + 1;
    }
    free(data);
  }

  // Drive the replace/insert passes over every managed (section, key).
  struct { int section; const char *const *keys; int count; } groups[] = {
    {0, NULL, 0}, {5, NULL, 0},  // keymap / gamepad handled by name table below
    {1, kGfxKeys, (int)countof(kGfxKeys)},
    {2, kSndKeys, (int)countof(kSndKeys)},
    {3, kGenKeys, (int)countof(kGenKeys)},
    {4, kFeatKeys, (int)countof(kFeatKeys)},
  };

  // For each section, build its key list (keymap/gamepad come from kKeyNameId).
  for (int g = 0; g < (int)countof(groups); g++) {
    int section = groups[g].section;
    int nkeys;
    const char *keybuf[64];
    const char *const *keys;
    if (section == 0 || section == 5) {
      nkeys = 0;
      for (int i = 1; i < (int)countof(kKeyNameId) && nkeys < 64; i++)
        keybuf[nkeys++] = kKeyNameId[i].name;
      keys = keybuf;
    } else {
      keys = groups[g].keys;
      nkeys = groups[g].count;
    }

    for (int ki = 0; ki < nkeys; ki++) {
      const char *key = keys[ki];
      char value[1024];
      if (!RenderManagedValue(section, key, value, sizeof value))
        continue;  // skip (preserve existing / custom / unnamed)

      // Replace: last matching key line across all runs of this section.
      int found = -1;
      char lk[96];
      for (int i = 0; i < L.n; i++) {
        if (L.sec[i] != section) continue;
        if (LineIsCommentOrBlank(L.v[i])) continue;
        if (LineKey(L.v[i], lk, sizeof lk) < 0) continue;
        if (StringEqualsNoCase(lk, key)) found = i;
      }
      if (found >= 0) {
        char *nl = FormatKeyLine(key, value, L.v[found]);
        free(L.v[found]);
        L.v[found] = nl;
        continue;
      }

      // Insert: skip empty values (don't materialize absent+empty keys).
      if (value[0] == 0) continue;

      // Find the last line of this section; insert after it.
      int last_in_sec = -1;
      for (int i = 0; i < L.n; i++)
        if (L.sec[i] == section) last_in_sec = i;
      if (last_in_sec >= 0) {
        IniLines_InsertAfter(&L, last_in_sec, FormatKeyLine(key, value, NULL), section);
      } else {
        // Section absent — create header then the key at EOF.
        const char *sn = SectionName(section);
        if (sn) {
          IniLines_Push(&L, DupRange(sn, strlen(sn)), section);
          IniLines_Push(&L, FormatKeyLine(key, value, NULL), section);
        }
      }
    }
  }

  // Serialize.
  size_t total = 0;
  size_t eollen = strlen(eol);
  for (int i = 0; i < L.n; i++) total += strlen(L.v[i]) + eollen;
  char *outbuf = (char *)malloc(total + 1);
  size_t pos = 0;
  for (int i = 0; i < L.n; i++) {
    size_t sl = strlen(L.v[i]);
    memcpy(outbuf + pos, L.v[i], sl); pos += sl;
    memcpy(outbuf + pos, eol, eollen); pos += eollen;
  }
  AtomicWriteBytes(path, outbuf, pos);

  free(outbuf);
  for (int i = 0; i < L.n; i++) free(L.v[i]);
  free(L.v);
  free(L.sec);
}

// ---- Live-apply ------------------------------------------------------------

extern uint32 g_wanted_zelda_features;  // defined in zelda_rtl.c
bool ZeldaIsReplaying(void);            // zelda_rtl.h (avoid heavy include here)

uint32 Config_ApplyLive(const Config *prev, const Config *now) {
  uint32 restart = 0;
  const uint32 GEOM = kFeatures0_ExtendScreen64 | kFeatures0_WidescreenVisualFixes;

  // features0: live-update the non-geometry bits via the existing per-frame
  // mirror (g_wanted_zelda_features -> g_ram+0x64c). The two geometry bits keep
  // their running value (framebuffer width is fixed at init). Deferred during
  // replay (the mirror does not run on replay frames).
  if (!ZeldaIsReplaying()) {
    g_wanted_zelda_features = (g_wanted_zelda_features & GEOM) | (now->features0 & ~GEOM);
  } else if ((prev->features0 & ~GEOM) != (now->features0 & ~GEOM)) {
    restart |= kCfgRestart_Features;  // surfaced as "applies after replay/restart"
  }
  if ((prev->features0 & GEOM) != (now->features0 & GEOM))
    restart |= kCfgRestart_Video;  // geometry bits need a restart

#ifdef Z3R_NATIVE_SETTINGS_WINDOW
  if (prev->window_scale != now->window_scale)
    MainHost_SetWindowScale(now->window_scale);  // updates g_config.window_scale to achieved
  if (prev->fullscreen != now->fullscreen) {
    if (now->fullscreen <= 1) MainHost_SetFullscreen(now->fullscreen);
    else restart |= kCfgRestart_Video;  // exclusive fullscreen is restart-only
  }
  if (prev->new_renderer != now->new_renderer)
    MainHost_SetNewRenderer(now->new_renderer);
#endif

  // Restart-only categories.
  if (prev->output_method != now->output_method ||
      prev->linear_filtering != now->linear_filtering ||
      prev->enhanced_mode7 != now->enhanced_mode7 ||
      prev->ignore_aspect_ratio != now->ignore_aspect_ratio ||
      prev->window_width != now->window_width ||
      prev->window_height != now->window_height ||
      prev->extended_aspect_ratio != now->extended_aspect_ratio ||
      prev->extend_y != now->extend_y ||
      prev->shader != now->shader ||
      prev->link_graphics != now->link_graphics)
    restart |= kCfgRestart_Video;
  if (prev->enable_audio != now->enable_audio ||
      prev->audio_freq != now->audio_freq ||
      prev->audio_channels != now->audio_channels ||
      prev->audio_samples != now->audio_samples ||
      prev->enable_msu != now->enable_msu ||
      prev->msu_path != now->msu_path)
    restart |= kCfgRestart_Audio;
  if (prev->language != now->language)
    restart |= kCfgRestart_Language;

  // Cosmetic shuffles: the palette mode + music toggle are read live at their
  // point of use each frame, so they apply immediately. A changed cosmetic_seed
  // needs the derived palette/music tables rebuilt (seed 0 here resolves to the
  // slot seed on the next slot load). The sprite-folder pick happens at launch.
  if (prev->cosmetic_seed != now->cosmetic_seed)
    Cosmetic_SetSeed(now->cosmetic_seed, 0);
  if (prev->cosmetic_sprite_dir != now->cosmetic_sprite_dir)
    restart |= kCfgRestart_Video;

  return restart;
}
