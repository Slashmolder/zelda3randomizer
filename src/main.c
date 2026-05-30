#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <SDL.h>
#ifdef _WIN32
#include "platform/win32/volume_control.h"
#include <direct.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include "snes/ppu.h"

#include "types.h"
#include "variables.h"

#include "zelda_rtl.h"
#include "zelda_cpu_infra.h"

#include "config.h"
#include "assets.h"
#include "load_gfx.h"
#include "util.h"
#include "audio.h"

#include "rando/rando.h"  // g_assets_hash declaration (tasks.md §1.1a)
#include "rando/vanilla_assets_hash.h"  // kVanillaAssetsHash + kVanillaAssetsHashKnown
#include "rando/rando_settings.h"
#include "rando/rando_placement.h"
#include "rando/rando_spoiler.h"
#include "rando/rando_share.h"
#include "rando/rando_textfield.h"  // §9.1b — SDL_TEXTINPUT host hooks
#include "rando/rando_logic.h"  // Logic_ComputeReachability for --rando-bench-logic
#include "rando/shuffle_boss.h"  // BossShuffle_Generate (Slice 7 §63)
#include "rando/shuffle_drops.h"  // DropShuffle_Generate (Slice 8 §64)
#include "rando/rando_hints.h"  // Rando_GenerateHints (Slice 5 §3)
#include "third_party/sha256/sha256.h"  // sha256_buffer for the asset hash

#ifdef Z3R_NATIVE_SETTINGS_WINDOW
#include "rando/rando_window/rando_window.h"          // RandoWindow_* (ImGui settings window)
#include "rando/rando_window/rando_window_bridge.h"   // RandoWindowBridge_Init
#include "rando/rando_window/imgui_host.h"            // Z3RHost_* (multi-window host)
#include "rando/rando_window/tracker_windows.h"       // Trackers_* (item/check/map windows)
#include "rando/rando_window/game_config_widgets.h"   // GameConfig_* (native game-config panels)
#include "rando/rando_window/game_cheats.h"            // Cheats_SelfCheck (selftest)
#include "rando/rando_window/game_panels.h"            // Panels_RenderSmokeCheck (selftest)
#include "rando/rando_generate.h"                     // Rando_GenerateSlot (generate consumer)
#include "rando/rando_save.h"                          // Rando_LoadSidecarSlot (--generate-slot round-trip)
#include "rando/shuffle_entrance.h"                    // Phase C entrance shuffle (CLI generate path)
#include "rando/rando_map.h"                          // RandoMap_DumpPpm (map decoder + dev dump)
#include "hud.h"                                       // Hud_RandoBuildIconAtlas (item-icon dev dump)
#endif

static bool g_run_without_emu = 0;

// When true, Die() skips the SDL_ShowSimpleMessageBox popup and just
// prints to stderr + exits. Set by CLI flags like --rando-selftest,
// --generate-seed, --rando-bench-logic, --print-assets-hash. Without
// this, a failure during a headless CI/batch run pops a modal dialog
// on whoever's desktop the process happens to land on (e.g., a worktree
// agent's failed --generate-seed pops dialogs on the developer's screen).
static bool g_headless_mode = 0;

// Forwards
static bool LoadRom(const char *filename);
static void LoadLinkGraphics();
static void RenderNumber(uint8 *dst, size_t pitch, int n, bool big);
static void HandleInput(int keyCode, int modCode, bool pressed);
static void HandleCommand(uint32 j, bool pressed);
static int RemapSdlButton(int button);
static void HandleGamepadInput(int button, bool pressed);
static void HandleGamepadAxisInput(int gamepad_id, int axis, int value);
static void OpenOneGamepad(int i);
static void HandleVolumeAdjustment(int volume_adjustment);
static void LoadAssets();
static void SwitchDirectory();

// Loads assets if a blob is present; returns false (without Die()ing) when none
// exists. LoadAssets() hard-fails on a missing zelda3_assets.dat, but the
// headless rando CLI paths (--generate-seed, --reveal-spoiler) only need assets
// for g_assets_hash (the --assets-must-be-vanilla gate). Placement is driven
// entirely by compiled-in logic/codegen tables and never reads asset bytes
// (no g_asset_ptrs references in src/rando/), so it stays byte-identical with or
// without the blob. This lets those paths — and the CI regression corpus that
// drives them — run on a ROM-less checkout; the vanilla-asset gate just degrades
// to its "hash unknown" warning (g_assets_hash remains all-zeros).
static bool LoadAssetsIfPresent() {
  FILE *f = fopen("zelda3_assets.dat", "rb");
  if (!f) f = fopen("zelda3_assets.bps", "rb");
  if (!f) {
    fprintf(stderr, "note: no zelda3_assets.dat — proceeding without assets "
                    "(placement is asset-independent; --assets-must-be-vanilla inert).\n");
    return false;
  }
  fclose(f);
  LoadAssets();
  return true;
}

enum {
  kDefaultFullscreen = 0,
  kMaxWindowScale = 10,
  kDefaultFreq = 44100,
  kDefaultChannels = 2,
  kDefaultSamples = 2048,
};

static const char kWindowTitle[] = "The Legend of Zelda: A Link to the Past";
static uint32 g_win_flags = SDL_WINDOW_RESIZABLE;
static SDL_Window *g_window;

#ifdef Z3R_NATIVE_SETTINGS_WINDOW
// Native randomizer settings window (Dear ImGui). A SECOND OS window with its
// OWN dedicated GL context, independent of the game window's renderer (the game
// defaults to SDL software with no GL context). Created hidden at startup; shown
// on demand. NEVER pass g_window to the settings GL context — see rando_window.cpp.
static SDL_Window *g_settings_window;
static SDL_GLContext g_settings_gl;
#endif

static uint8 g_paused, g_turbo, g_replay_turbo = true, g_cursor = true;
// Debug Time panel state. g_frame_step is a one-shot consumed by the loop to let
// exactly one frame through while paused; g_game_speed scales the frame-pacing
// delay only (1.0 = normal). Both default to "no effect" so non-PC builds and
// the untouched default behave identically.
static bool g_frame_step;
static float g_game_speed = 1.0f;
static uint8 g_current_window_scale;
static uint8 g_gamepad_buttons;
static int g_input1_state;
static bool g_display_perf;
static int g_curr_fps;
static int g_ppu_render_flags = 0;
static int g_snes_width, g_snes_height;
static int g_sdl_audio_mixer_volume = SDL_MIX_MAXVOLUME;
static struct RendererFuncs g_renderer_funcs;
static uint32 g_gamepad_modifiers;
static uint16 g_gamepad_last_cmd[kGamepadBtn_Count];

void NORETURN Die(const char *error) {
#if defined(NDEBUG) && defined(_WIN32)
  // Skip the modal popup in headless / CLI mode (--rando-selftest,
  // --generate-seed, --rando-bench-logic, --print-assets-hash). Otherwise
  // a failed batch run (e.g., a worktree agent calling --generate-seed
  // without zelda3_assets.dat) pops dialogs on the developer's desktop.
  if (!g_headless_mode)
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, kWindowTitle, error, NULL);
#endif
  fprintf(stderr, "Error: %s\n", error);
  exit(1);
}

void ChangeWindowScale(int scale_step) {
  if ((SDL_GetWindowFlags(g_window) & (SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_FULLSCREEN | SDL_WINDOW_MINIMIZED | SDL_WINDOW_MAXIMIZED)) != 0)
    return;
  int screen = SDL_GetWindowDisplayIndex(g_window);
  if (screen < 0) screen = 0;
  int max_scale = kMaxWindowScale;
  SDL_Rect bounds;
  int bt = -1, bl, bb, br;
  // note this takes into effect Windows display scaling, i.e., resolution is divided by scale factor
  if (SDL_GetDisplayUsableBounds(screen, &bounds) == 0) {
    // this call may take a while before it is reported by Windows (or not at all in my testing)
    if (SDL_GetWindowBordersSize(g_window, &bt, &bl, &bb, &br) != 0) {
      // guess based on Windows 10/11 defaults
      bl = br = bb = 1;
      bt = 31;
    }
    // Allow a scale level slightly above the max that fits on screen
    int mw = (bounds.w - bl - br + g_snes_width / 4) / g_snes_width;
    int mh = (bounds.h - bt - bb + g_snes_height / 4) / g_snes_height;
    max_scale = IntMin(mw, mh);
  }
  int new_scale = IntMax(IntMin(g_current_window_scale + scale_step, max_scale), 1);
  g_current_window_scale = new_scale;
  int w = new_scale * g_snes_width;
  int h = new_scale * g_snes_height;

  //SDL_RenderSetLogicalSize(g_renderer, w, h);
  SDL_SetWindowSize(g_window, w, h);
  if (bt >= 0) {
    // Center the window on top of the mouse
    int mx, my;
    SDL_GetGlobalMouseState(&mx, &my);
    int wx = IntMax(IntMin(mx - w / 2, bounds.x + bounds.w - bl - br - w), bounds.x + bl);
    int wy = IntMax(IntMin(my - h / 2, bounds.y + bounds.h - bt - bb - h), bounds.y + bt);
    SDL_SetWindowPosition(g_window, wx, wy);
  } else {
    SDL_SetWindowPosition(g_window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
  }
}

#ifdef Z3R_NATIVE_SETTINGS_WINDOW
// Absolute window/renderer hooks for the native game-config UI's live-apply
// (Config_ApplyLive). Distinct from the relative ChangeWindowScale and the XOR
// fullscreen toggle in HandleCommand_Locked — those are wrong for "set to X".
int MainHost_SetWindowScale(int scale) {
  if (scale < 1) scale = 1;
  // ChangeWindowScale is a relative step that clamps to a screen-fit max; drive
  // it to the target and report the achieved scale so the caller can persist the
  // value actually honored (avoids re-clamping an unreachable scale each launch).
  ChangeWindowScale(scale - (int)g_current_window_scale);
  g_config.window_scale = g_current_window_scale;
  return g_current_window_scale;
}

void MainHost_SetFullscreen(uint8 mode) {
  if (mode == 1) {  // desktop fullscreen
    g_win_flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    SDL_SetWindowFullscreen(g_window, SDL_WINDOW_FULLSCREEN_DESKTOP);
  } else {          // windowed
    g_win_flags &= ~SDL_WINDOW_FULLSCREEN_DESKTOP;
    SDL_SetWindowFullscreen(g_window, 0);
  }
}

void MainHost_SetNewRenderer(bool on) {
  if (on) g_ppu_render_flags |= kPpuRenderFlags_NewRenderer;
  else g_ppu_render_flags &= ~kPpuRenderFlags_NewRenderer;
}

// Time-control hooks (debug Time panel).
void MainHost_SetPaused(bool paused) { g_paused = paused ? 1 : 0; }
bool MainHost_GetPaused(void) { return g_paused != 0; }
void MainHost_RequestFrameStep(void) { g_frame_step = true; }  // consumed by the loop
void MainHost_SetSpeed(float mult) {
  if (mult < 0.25f) mult = 0.25f;
  if (mult > 4.0f) mult = 4.0f;
  g_game_speed = mult;
}
float MainHost_GetSpeed(void) { return g_game_speed; }
#endif  // Z3R_NATIVE_SETTINGS_WINDOW

#define RESIZE_BORDER 20
static SDL_HitTestResult HitTestCallback(SDL_Window *win, const SDL_Point *pt, void *data) {
  uint32 flags = SDL_GetWindowFlags(win);
  if ((flags & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0 || (flags & SDL_WINDOW_FULLSCREEN) != 0)
    return SDL_HITTEST_NORMAL;

  if ((SDL_GetModState() & KMOD_CTRL) != 0)
    return SDL_HITTEST_DRAGGABLE;

  int w, h;
  SDL_GetWindowSize(win, &w, &h);

  if (pt->y < RESIZE_BORDER) {
    return (pt->x < RESIZE_BORDER) ? SDL_HITTEST_RESIZE_TOPLEFT :
           (pt->x >= w - RESIZE_BORDER) ? SDL_HITTEST_RESIZE_TOPRIGHT : SDL_HITTEST_RESIZE_TOP;
  } else if (pt->y >= h - RESIZE_BORDER) {
    return (pt->x < RESIZE_BORDER) ? SDL_HITTEST_RESIZE_BOTTOMLEFT :
           (pt->x >= w - RESIZE_BORDER) ? SDL_HITTEST_RESIZE_BOTTOMRIGHT : SDL_HITTEST_RESIZE_BOTTOM;
  } else {
    if (pt->x < RESIZE_BORDER) {
      return SDL_HITTEST_RESIZE_LEFT;
    } else if (pt->x >= w - RESIZE_BORDER) {
      return SDL_HITTEST_RESIZE_RIGHT;
    }
  }
  return SDL_HITTEST_NORMAL;
}

static void DrawPpuFrameWithPerf() {
  int render_scale = PpuGetCurrentRenderScale(g_zenv.ppu, g_ppu_render_flags);
  uint8 *pixel_buffer = 0;
  int pitch = 0;

  g_renderer_funcs.BeginDraw(g_snes_width * render_scale,
                             g_snes_height * render_scale,
                             &pixel_buffer, &pitch);
  if (g_display_perf || g_config.display_perf_title) {
    static float history[64], average;
    static int history_pos;
    uint64 before = SDL_GetPerformanceCounter();
    ZeldaDrawPpuFrame(pixel_buffer, pitch, g_ppu_render_flags);
    uint64 after = SDL_GetPerformanceCounter();
    float v = (double)SDL_GetPerformanceFrequency() / (after - before);
    average += v - history[history_pos];
    history[history_pos] = v;
    history_pos = (history_pos + 1) & 63;
    g_curr_fps = average * (1.0f / 64);
  } else {
    ZeldaDrawPpuFrame(pixel_buffer, pitch, g_ppu_render_flags);
  }
  if (g_display_perf)
    RenderNumber(pixel_buffer + pitch * render_scale, pitch, g_curr_fps, render_scale == 4);
  g_renderer_funcs.EndDraw();
}

static SDL_mutex *g_audio_mutex;
static uint8 *g_audiobuffer, *g_audiobuffer_cur, *g_audiobuffer_end;
static int g_frames_per_block;
static uint8 g_audio_channels;

static void SDLCALL AudioCallback(void *userdata, Uint8 *stream, int len) {
  if (SDL_LockMutex(g_audio_mutex)) Die("Mutex lock failed!");
  while (len != 0) {
    if (g_audiobuffer_end - g_audiobuffer_cur == 0) {
      ZeldaRenderAudio((int16*)g_audiobuffer, g_frames_per_block, g_audio_channels);
      g_audiobuffer_cur = g_audiobuffer;
      g_audiobuffer_end = g_audiobuffer + g_frames_per_block * g_audio_channels * sizeof(int16);
    }
    int n = IntMin(len, g_audiobuffer_end - g_audiobuffer_cur);
    if (g_sdl_audio_mixer_volume == SDL_MIX_MAXVOLUME) {
      memcpy(stream, g_audiobuffer_cur, n);
    } else {
      SDL_memset(stream, 0, n);
      SDL_MixAudioFormat(stream, g_audiobuffer_cur, AUDIO_S16, n, g_sdl_audio_mixer_volume);
    }
    g_audiobuffer_cur += n;
    stream += n;
    len -= n;
  }

  ZeldaDiscardUnusedAudioFrames();
  SDL_UnlockMutex(g_audio_mutex);
}

// State for sdl renderer
static SDL_Renderer *g_renderer;
static SDL_Texture *g_texture;
static SDL_Rect g_sdl_renderer_rect;

static bool SdlRenderer_Init(SDL_Window *window) {

  if (g_config.shader)
    fprintf(stderr, "Warning: Shaders are supported only with the OpenGL backend\n");

  SDL_Renderer *renderer = SDL_CreateRenderer(g_window, -1,
                                              g_config.output_method == kOutputMethod_SDLSoftware ? SDL_RENDERER_SOFTWARE :
                                              SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (renderer == NULL) {
    printf("Failed to create renderer: %s\n", SDL_GetError());
    return false;
  }
  SDL_RendererInfo renderer_info;
  SDL_GetRendererInfo(renderer, &renderer_info);
  if (kDebugFlag) {
    printf("Supported texture formats:");
    for (int i = 0; i < renderer_info.num_texture_formats; i++)
      printf(" %s", SDL_GetPixelFormatName(renderer_info.texture_formats[i]));
    printf("\n");
  }
  g_renderer = renderer;
  if (!g_config.ignore_aspect_ratio)
    SDL_RenderSetLogicalSize(renderer, g_snes_width, g_snes_height);
  if (g_config.linear_filtering)
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "best");

  int tex_mult = (g_ppu_render_flags & kPpuRenderFlags_4x4Mode7) ? 4 : 1;
  g_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                g_snes_width * tex_mult, g_snes_height * tex_mult);
  if (g_texture == NULL) {
    printf("Failed to create texture: %s\n", SDL_GetError());
    return false;
  }
  return true;
}

static void SdlRenderer_Destroy() {
  SDL_DestroyTexture(g_texture);
  SDL_DestroyRenderer(g_renderer);
}

static void SdlRenderer_BeginDraw(int width, int height, uint8 **pixels, int *pitch) {
  g_sdl_renderer_rect.w = width;
  g_sdl_renderer_rect.h = height;
  if (SDL_LockTexture(g_texture, &g_sdl_renderer_rect, (void **)pixels, pitch) != 0) {
    printf("Failed to lock texture: %s\n", SDL_GetError());
    return;
  }
}

static void SdlRenderer_EndDraw() {

//  uint64 before = SDL_GetPerformanceCounter();
  SDL_UnlockTexture(g_texture);
//  uint64 after = SDL_GetPerformanceCounter();
//  float v = (double)(after - before) / SDL_GetPerformanceFrequency();
//  printf("%f ms\n", v * 1000);
  SDL_RenderClear(g_renderer);
  SDL_RenderCopy(g_renderer, g_texture, &g_sdl_renderer_rect, NULL);
  SDL_RenderPresent(g_renderer); // vsyncs to 60 FPS?
}

static const struct RendererFuncs kSdlRendererFuncs  = {
  &SdlRenderer_Init,
  &SdlRenderer_Destroy,
  &SdlRenderer_BeginDraw,
  &SdlRenderer_EndDraw,
};

void OpenGLRenderer_Create(struct RendererFuncs *funcs, bool use_opengl_es);

// Parse a uint64 seed from "0x..." hex or decimal. Exits 64 on a bad digit.
static uint64 ParseSeedU64OrExit(const char *p) {
  uint64 v = 0;
  if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
    p += 2;
    while (*p) {
      char c = *p++;
      uint8 d;
      if (c >= '0' && c <= '9') d = c - '0';
      else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
      else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
      else { fprintf(stderr, "--seed: bad hex digit '%c'\n", c); exit(64); }
      v = (v << 4) | d;
    }
  } else {
    while (*p) {
      char c = *p++;
      if (c < '0' || c > '9') { fprintf(stderr, "--seed: bad decimal digit '%c'\n", c); exit(64); }
      v = v * 10 + (uint64)(c - '0');
    }
  }
  return v;
}

// Headless slot-path generation (test hook). Exercises the SHARED playable-slot
// generator Rando_GenerateSlot — the exact code the PC native settings window
// and the in-game settings screen run — which the corpus (--generate-seed,
// a different code path) does NOT cover. Emits a one-line JSON blob so a CI
// harness can: (a) diff the slot-path placement digest against the CLI path for
// the same (settings, seed) [parity]; (b) confirm per-accessibility-tier
// accept/reject; (c) confirm the persisted slot round-trips with the correct
// world_state / slot_kind / settings_hash.
//
// Rando_GenerateSlot writes sram.dat + the sidecar file + a spoiler under the
// CWD, so CI runs this from a scratch directory (and pre-creates saves/).
static void MaybeRunGenerateSlotAndExit(int argc, char **argv, const char *config_file) {
  bool found = false;
  for (int i = 0; i < argc; ++i)
    if (strcmp(argv[i], "--generate-slot") == 0) { found = true; break; }
  if (!found) return;

  const char *settings_csv = NULL;
  const char *seed_u64_str = NULL;
  int slot_index = 0;
  for (int i = 0; i < argc; ++i) {
    const char *a = argv[i];
    if (strncmp(a, "--settings=", 11) == 0) settings_csv = a + 11;
    else if (strncmp(a, "--seed=", 7) == 0) seed_u64_str = a + 7;
    else if (strncmp(a, "--slot=", 7) == 0) slot_index = atoi(a + 7);
  }
  if (seed_u64_str == NULL) {
    fprintf(stderr,
      "Usage: --generate-slot --settings=<k=v,...> --seed=<u64> [--slot=<0..2>]\n");
    exit(64);
  }

  ParseConfigFile(config_file);
  LoadAssetsIfPresent();

  RandoSettings settings;
  Settings_SetDefaults(&settings);
  if (settings_csv != NULL && *settings_csv != '\0') {
    if (Settings_ParseCsv(settings_csv, &settings) != 0) {
      fprintf(stderr, "--generate-slot: --settings= parse failed (see error above)\n");
      exit(64);
    }
  }
  uint64 seed_u64 = ParseSeedU64OrExit(seed_u64_str);

  // Rando_GenerateSlot writes into g_zenv.sram + slot_index*0x500; allocate the
  // 8 KB SRAM image since the headless path never ran full ZeldaInitialize.
  if (g_zenv.sram == NULL) g_zenv.sram = (uint8 *)calloc(8192, 1);

  RandoGenerateResult result;
  memset(&result, 0, sizeof(result));
  char err[256];
  err[0] = '\0';
  // budget = 0: run the placer to its deterministic attempt cap (no wall-clock
  // cutoff) so the digest is machine-independent (matches the corpus path).
  bool ok = Rando_GenerateSlot(&settings, seed_u64, /*budget=*/0, slot_index,
                               g_config.features0, &result, err, sizeof(err));

  uint8 digest[32];
  memset(digest, 0, sizeof(digest));
  if (ok && result.placement.count > 0)
    PlacementTable_ComputeDigest(&result.placement, digest);

  // Round-trip: read the persisted slot back and confirm its placement + header
  // survived serialization. (RandoSave_SelfCheck already covers the serialize/
  // deserialize primitive; this confirms the FULL generate→persist→read path.)
  int roundtrip_ok = 0, world_state = -1, slot_kind = -1;
  if (ok) {
    RandoSidecarSlot slot;
    if (Rando_LoadSidecarSlot(slot_index, &slot)) {
      world_state = slot.header.world_state;
      slot_kind = slot.header.slot_kind;
      RandoPlacementTable rt = { slot.placements, slot.placement_count };
      uint8 rt_digest[32];
      PlacementTable_ComputeDigest(&rt, rt_digest);
      roundtrip_ok = (slot.placement_count == result.placement.count &&
                      memcmp(rt_digest, digest, 32) == 0 &&
                      slot.header.slot_kind == kSlotKind_Randomizer &&
                      slot.header.world_state == settings.world_state) ? 1 : 0;
    }
  }

  // One-line machine-parseable result for the CI harness.
  printf("{\"ok\": %s, \"goal_completable\": %s, \"placement_count\": %u, "
         "\"placement_digest_hex\": \"",
         ok ? "true" : "false",
         (ok && result.goal_completable) ? "true" : "false",
         (unsigned)(ok ? result.placement.count : 0));
  for (int i = 0; i < 32; i++) printf("%02x", digest[i]);
  printf("\", \"settings_hash_hex\": \"");
  for (int i = 0; i < 32; i++) printf("%02x", ok ? result.settings_hash[i] : 0);
  printf("\", \"share_string\": \"%s\", \"world_state\": %d, \"slot_kind\": %d, "
         "\"roundtrip_ok\": %s, \"error\": \"%s\"}\n",
         ok ? result.share_string : "", world_state, slot_kind,
         roundtrip_ok ? "true" : "false", ok ? "" : err);

  if (result.placement.entries != NULL) free(result.placement.entries);
  exit(ok ? 0 : 1);
}

static void MaybeRunGenerateSeedAndExit(int argc, char **argv, const char *config_file) {
  // Detect --generate-seed anywhere in argv. If not present, return so main()
  // proceeds with the normal GUI startup path.
  bool found = false;
  for (int i = 0; i < argc; ++i) {
    if (strcmp(argv[i], "--generate-seed") == 0) { found = true; break; }
  }
  if (!found) return;

  // Headless path: parse rando-relevant flags, load config + assets so
  // g_assets_hash is computed, then exit.
  //
  // The single-seed generation pipeline runs below (settings parse →
  // Place_AssumedFill → spoiler/share-string write). The batch/manifest form
  // is not yet implemented and exits 64 (EX_USAGE-ish) so CI can distinguish
  // "not implemented" from a real failure.
  const char *settings_csv = NULL;
  const char *seed_u64_str = NULL;
  const char *out_spoiler = NULL;
  const char *out_share_string = NULL;
  const char *manifest_path = NULL;
  const char *out_dir = NULL;
  int budget_seconds = 5;
  bool assets_must_be_vanilla = false;
  bool race_mode = false;  // Phase B Slice 6 — set settings.race_mode = 1

  for (int i = 0; i < argc; ++i) {
    const char *a = argv[i];
    if (strncmp(a, "--settings=", 11) == 0) settings_csv = a + 11;
    else if (strncmp(a, "--seed=", 7) == 0) seed_u64_str = a + 7;
    else if (strncmp(a, "--out-spoiler=", 14) == 0) out_spoiler = a + 14;
    else if (strncmp(a, "--out-share-string=", 19) == 0) out_share_string = a + 19;
    else if (strncmp(a, "--manifest=", 11) == 0) manifest_path = a + 11;
    else if (strncmp(a, "--out-dir=", 10) == 0) out_dir = a + 10;
    else if (strncmp(a, "--budget-seconds=", 17) == 0) budget_seconds = atoi(a + 17);
    else if (strcmp(a, "--assets-must-be-vanilla") == 0) assets_must_be_vanilla = true;
    else if (strcmp(a, "--race-mode") == 0) race_mode = true;
  }

  // Validate flag combinations.
  bool batch = (manifest_path != NULL);
  bool single = (settings_csv != NULL && seed_u64_str != NULL && out_spoiler != NULL);
  if (!batch && !single) {
    fprintf(stderr,
      "Usage:\n"
      "  Single seed: --generate-seed --settings=<k=v,...> --seed=<u64> --out-spoiler=<path>\n"
      "               [--out-share-string=<path>] [--budget-seconds=<n>] [--assets-must-be-vanilla]\n"
      "  Batch:       --generate-seed --manifest=<yaml> [--budget-seconds=<n>] [--out-dir=<path>]\n");
    exit(64);
  }

  // Load config (so [Randomizer] defaults populate) and assets (so
  // g_assets_hash is computed). Both are safe to call without SDL. Assets are
  // optional here: placement is asset-independent, so a ROM-less run (CI) still
  // generates deterministically (see LoadAssetsIfPresent).
  ParseConfigFile(config_file);
  LoadAssetsIfPresent();

  // Honor --assets-must-be-vanilla per randomizer-placement spec scenario
  // "CLI --assets-must-be-vanilla refuses non-vanilla blobs".
  if (assets_must_be_vanilla) {
    if (!kVanillaAssetsHashKnown) {
      fprintf(stderr,
        "--assets-must-be-vanilla: WARNING the vanilla assets hash is not yet\n"
        "  baked into vanilla_assets_hash.h (Phase A1 placeholder is all-zeros).\n"
        "  Run `python assets/scripts/dump_vanilla_assets_hash.py` against a\n"
        "  clean asset extraction and commit the result to activate this check.\n"
        "  Proceeding with current assets.\n");
    } else if (memcmp(g_assets_hash, kVanillaAssetsHash, 32) != 0) {
      fprintf(stderr, "--assets-must-be-vanilla: assets are NOT vanilla. Refusing.\n  current hash:  ");
      for (int i = 0; i < 32; i++) fprintf(stderr, "%02x", g_assets_hash[i]);
      fprintf(stderr, "\n  expected hash: ");
      for (int i = 0; i < 32; i++) fprintf(stderr, "%02x", kVanillaAssetsHash[i]);
      fprintf(stderr, "\n");
      exit(1);
    }
  }

  // Batch form: still a stub (manifest parsing + iteration not yet
  // implemented). Single-seed form goes through the real pipeline below.
  if (batch) {
    fprintf(stderr,
      "--generate-seed --manifest: batch form is not yet implemented.\n"
      "  Manifest path: %s\n"
      "  Out dir: %s\n"
      "Single-seed form is functional (--generate-seed --settings=... --seed=... --out-spoiler=...).\n",
      manifest_path, out_dir ? out_dir : "(none)");
    exit(64);
  }

  // ----- Phase A1 single-seed pipeline -----
  // Settings: parse --settings=k=v,... via Settings_ParseCsv. Defaults populate
  // the struct first; missing keys keep defaults.
  RandoSettings settings;
  Settings_SetDefaults(&settings);
  if (settings_csv != NULL && *settings_csv != '\0') {
    if (Settings_ParseCsv(settings_csv, &settings) != 0) {
      fprintf(stderr, "--generate-seed: --settings= parse failed (see error above)\n");
      exit(64);
    }
  }

  // Phase B Slice 6 — --race-mode override (highest precedence; can also be
  // set via --settings=race_mode=true, but --race-mode is the canonical CLI).
  // Slice 6 audit M2 — warn if the user explicitly typed
  // `--settings=race_mode=false` alongside `--race-mode`, so a typo doesn't
  // silently produce a non-race-mode seed.
  if (race_mode && settings_csv != NULL && settings.race_mode == 0 &&
      strstr(settings_csv, "race_mode=") != NULL) {
    fprintf(stderr,
      "--generate-seed: WARNING --race-mode overrides --settings=race_mode=false; "
      "generating a race-mode seed.\n");
  }
  if (race_mode) settings.race_mode = 1;

  // Seed: parse hex or decimal uint64 from --seed=<u64>.
  uint64 seed_u64 = 0;
  {
    const char *p = seed_u64_str;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
      p += 2;
      while (*p) {
        char c = *p++;
        uint8 v;
        if (c >= '0' && c <= '9') v = c - '0';
        else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
        else { fprintf(stderr, "--seed: bad hex digit '%c'\n", c); exit(64); }
        seed_u64 = (seed_u64 << 4) | v;
      }
    } else {
      while (*p) {
        char c = *p++;
        if (c < '0' || c > '9') { fprintf(stderr, "--seed: bad decimal digit '%c'\n", c); exit(64); }
        seed_u64 = seed_u64 * 10 + (c - '0');
      }
    }
  }

  // Allocate placement table sized to the location count.
  extern const uint32 kRandoLocationsCount;
  RandoPlacement *entries = (RandoPlacement *)calloc(kRandoLocationsCount, sizeof(RandoPlacement));
  if (entries == NULL) {
    fprintf(stderr, "--generate-seed: out of memory allocating placement table\n");
    exit(64);
  }
  RandoPlacementTable table = { entries, 0 };

  // Measure end-to-end placement+sphere wall-clock for the spoiler's
  // generation_wall_clock_ms field. Includes placement, sphere computation,
  // and goal-completability. Excludes spoiler I/O.
  clock_t gen_start = clock();

  // Run placement (assumed fill with bounded retry + wall-clock budget).
  // Phase B Slice 6 audit H1 — race-mode generation always uses
  // budget_seconds=0 so the placer runs to its deterministic
  // kAssumedFillMaxAttempts cap (matches Rando_RevealSpoiler's budget).
  // Stamp is then reproducible across machines.
  int effective_budget = (settings.race_mode != 0) ? 0 : budget_seconds;
  // Phase C — entrance shuffle: same reject-and-retry as the playable-slot path
  // (rando_generate.c). Draw a cave permutation π, install its region overrides
  // so the placer/goal-check see the shuffled reachability, accept the first π
  // under which the goal is completable. Default-off ⇒ byte-identical placement.
  bool ok = false;
  uint8 cave_assign[kEntranceMaxInteriors]; int cave_count = 0;
  uint8 dun_assign[kEntranceMaxInteriors]; int dun_count = 0;
  uint8 cross_assign[kEntranceMaxInteriors]; int cross_count = 0;
  uint8 decoupled_assign[kEntranceMaxInteriors]; int decoupled_count = 0;
  bool cross_on = Entrance_IsCrossActive(&settings);
  bool cave_on = !cross_on && Entrance_IsActive(&settings);
  bool dun_on = !cross_on && Entrance_IsDungeonActive(&settings);
  bool decoupled_on = Entrance_IsDecoupledActive(&settings);  // Stage 4 D.1/D.2 (logic+gen)
  Entrance_ClearRegionOverrides();
  Entrance_ClearEdgeOverrides();
  if (cross_on || cave_on || dun_on || decoupled_on) {
    for (int att = 0; att < 64; att++) {
      if (cross_on) {
        cross_count = Entrance_ComputeCrossPermutation(&settings, seed_u64, (uint8)att, cross_assign);
        Entrance_ApplyCrossOverrides(cross_assign, cross_count);
      } else {
        if (cave_on) {
          cave_count = Entrance_ComputePermutation(&settings, seed_u64, (uint8)att, cave_assign);
          Entrance_ApplyRegionOverrides(cave_assign, cave_count);
        }
        if (dun_on) {
          dun_count = Entrance_ComputeDungeonPermutation(&settings, seed_u64, (uint8)att, dun_assign);
          Entrance_ApplyEdgeOverrides(dun_assign, dun_count);
        }
      }
      if (decoupled_on) {
        decoupled_count = Entrance_ComputeDecoupledExit(&settings, seed_u64, (uint8)att, decoupled_assign);
        Entrance_ApplyDecoupledExitEdges(decoupled_assign, decoupled_count);
      }
      table.count = 0;
      if (Place_AssumedFill(&settings, seed_u64, effective_budget, &table)) {
        // Accept this entrance permutation only if it meets the active
        // accessibility tier (always beatable, plus per-tier reachability) —
        // this rejects any π that strands placements beyond the tier's bar
        // (e.g. a gated door leading to the dungeon that grants the gating
        // item). See rando_generate.c.
        if (Accessibility_SeedAcceptable(&settings, &table)) {
          ok = true;
          break;
        }
      }
    }
    // Leave overrides active through sphere + spoiler emission below.
  } else {
    ok = Place_AssumedFill(&settings, seed_u64, effective_budget, &table);
  }
  if (!ok) {
    fprintf(stderr, "--generate-seed: placement failed\n");
    free(entries);
    exit(1);
  }

  // Phase B Slice 7+8 §63/§64 — generate boss + drop shuffle assignments.
  // The shuffles consume the placement table (for sphere data — Phase B+
  // refinement) and are deterministic from (settings, seed_u64). Today
  // the assignments are computed but not yet consumed by sprite handlers
  // (per-site instrumentation is task #65). Calling here keeps the
  // algorithms exercised during corpus regression.
  {
    uint8 boss_assignment[16];
    (void)BossShuffle_Generate(&settings, seed_u64, boss_assignment);
    (void)DropShuffle_Generate(&settings, seed_u64, &table, NULL);
  }

  // Compute digest for log + spoiler header.
  uint8 placement_digest[32];
  PlacementTable_ComputeDigest(&table, placement_digest);

  // Compute share string (magic | version | settings_hash | seed_u64 | crc).
  ShareString ss;
  memset(&ss, 0, sizeof(ss));
  ss.version = (uint8)kGeneratorVersion;
  Settings_HashShort(&settings, ss.settings_hash);
  ss.seed_u64 = seed_u64;
  char share_string[kShareStringBase32MaxLen];
  int share_len = Share_Encode(&ss, share_string, sizeof(share_string));
  if (share_len <= 0) {
    fprintf(stderr, "--generate-seed: share string encoding failed\n");
    free(entries);
    exit(1);
  }

  // Compute spheres for sphere_data emission.
  RandoSpheres spheres;
  bool spheres_ok = Logic_ComputeSpheres(&settings, &table, &spheres);
  if (!spheres_ok) {
    fprintf(stderr, "--generate-seed: %u placements unreachable across %u spheres\n",
            (unsigned)spheres.unreachable_count, (unsigned)(spheres.max_sphere + 1));
  }

  // Phase B Slice 5 §3 — populate per-NPC hint texts. No-op when
  // settings.hints == kHintsMode_Off. Output reaches users via the
  // spoiler's `hints[]` array; runtime telepathic-tile intercept is
  // playtest-gated (#85).
  Rando_GenerateHints(&settings, &table, &spheres);

  // Build the spoiler and write it.
  RandoSpoiler spoiler;
  memset(&spoiler, 0, sizeof(spoiler));
  spoiler.share_string = share_string;
  spoiler.seed_u64 = seed_u64;
  spoiler.generator_version = kGeneratorVersion;
  spoiler.settings = &settings;
  // Phase C — entrance_mapping sections (omitted when the respective count is 0).
  spoiler.entrance_assign = (cave_count > 0) ? cave_assign : NULL;
  spoiler.entrance_count = cave_count;
  spoiler.dungeon_assign = (dun_count > 0) ? dun_assign : NULL;
  spoiler.dungeon_count = dun_count;
  spoiler.cross_assign = (cross_count > 0) ? cross_assign : NULL;
  spoiler.cross_count = cross_count;
  spoiler.decoupled_assign = (decoupled_count > 0) ? decoupled_assign : NULL;
  spoiler.decoupled_count = decoupled_count;
  spoiler.placements = &table;
  spoiler.spheres = &spheres;
  {
    clock_t gen_end = clock();
    long elapsed_ms = (long)((double)(gen_end - gen_start) * 1000.0 / CLOCKS_PER_SEC);
    if (elapsed_ms < 0) elapsed_ms = 0;
    spoiler.generation_wall_clock_ms = (uint32)elapsed_ms;
  }
  // Forward placer stats — read from Placement_GetLastStats so the
  // spoiler can surface forward-fill fallbacks.
  {
    const PlacementStats *st = Placement_GetLastStats();
    spoiler.forward_fill_fallback_count = st->forward_fill_fallback_count;
    spoiler.retry_attempts = st->attempts_used;
  }
  // Goal completability — runs Logic_ComputeReachability against the full
  // placement-table inventory and checks the goal-specific locations.
  spoiler.goal_completable = Goal_IsCompletable(&settings, &table);

  // Per `randomizer-core / Generation rejects un-completable seeds`: refuse
  // to write the spoiler when the goal predicate fails. CI corpus + race-mode
  // workflows can't tolerate broken seeds. Set --allow-broken-seed to
  // override (debug / diagnostic use only).
  bool allow_broken_seed = false;
  for (int i = 0; i < argc; ++i) {
    if (strcmp(argv[i], "--allow-broken-seed") == 0) { allow_broken_seed = true; break; }
  }
  if (Goal_ShouldRefuse(&settings, &table) && !allow_broken_seed) {
    fprintf(stderr,
      "--generate-seed: this seed does not meet the accessibility requirement\n"
      "  for goal %u (accessibility=%u) — refusing to write spoiler. All tiers\n"
      "  require the goal be beatable; `locations` additionally requires every\n"
      "  location reachable and `items` every progression item reachable.\n"
      "  (Use --allow-broken-seed to write a diagnostic spoiler anyway.)\n",
      (unsigned)settings.goal, (unsigned)settings.accessibility);
    free(entries);
    exit(1);
  }
  // NOTE: the per-tier refusal above is honored by Accessibility_SeedAcceptable.
  // For `items`/`beatable` a handful of NON-progression items (junk, maps,
  // compasses, hearts) may still land at unreachable slots — the placer's
  // bounded-retry-with-perturbed-seed strategy can't always find valid
  // dungeon-mode key containment placements, and tolerating stranded junk there
  // is intentional (only `locations` demands full reachability). The
  // spoiler's `fallback_warnings: unreachable_placements` rollup surfaces
  // this — users can decide whether to regenerate with a different seed.
  // Phase A2 bounded intra-attempt rewind should reduce these to 0.

  // Phase B Slice 6 — Spoiler_Write branches on settings.race_mode:
  //   race_mode == 0: full JSON + .txt (existing behavior).
  //   race_mode == 1: suppressed binary (ZRSR header) at <out_spoiler>; no .txt.
  size_t out_len = strlen(out_spoiler);
  char *txt_path = (char *)malloc(out_len + 5);
  if (txt_path != NULL) {
    // Strip .json if present, append .txt
    if (out_len >= 5 && strcmp(out_spoiler + out_len - 5, ".json") == 0) {
      memcpy(txt_path, out_spoiler, out_len - 5);
      strcpy(txt_path + out_len - 5, ".txt");
    } else {
      memcpy(txt_path, out_spoiler, out_len);
      strcpy(txt_path + out_len, ".txt");
    }
  }
  if (!Spoiler_Write(&spoiler, out_spoiler, txt_path)) {
    fprintf(stderr, "--generate-seed: failed writing spoiler to %s\n", out_spoiler);
    if (txt_path != NULL) free(txt_path);
    free(entries);
    exit(1);
  }
  if (txt_path != NULL) free(txt_path);

  // Optionally write the share string to its own file.
  if (out_share_string != NULL) {
    FILE *sf = fopen(out_share_string, "wb");
    if (sf != NULL) {
      fputs(share_string, sf);
      fclose(sf);
    }
  }

  // The --assets-must-be-vanilla check ran above, before the placer; no
  // additional work needed here.

  fprintf(stderr,
    "--generate-seed: OK\n"
    "  seed: 0x%016llx\n"
    "  share_string: %s\n"
    "  placements: %u\n"
    "  placement_digest: %02x%02x%02x%02x%02x%02x%02x%02x...\n"
    "  spoiler.json: %s\n",
    (unsigned long long)seed_u64,
    share_string,
    (unsigned)table.count,
    placement_digest[0], placement_digest[1], placement_digest[2], placement_digest[3],
    placement_digest[4], placement_digest[5], placement_digest[6], placement_digest[7],
    out_spoiler);

  free(entries);
  exit(0);
}

// Phase B Slice 6 — --reveal-spoiler=<path> headless reveal path.
// Reads the suppressed spoiler at <path>, verifies its integrity,
// regenerates the placement deterministically from the embedded settings +
// share-string-derived seed, stamps it, and overwrites <path> with the
// full JSON + writes a .txt sibling. Exits 0 on success, non-zero on any
// failure with a description on stderr.
static void MaybeRunRevealSpoilerAndExit(int argc, char **argv, const char *config_file) {
  const char *reveal_path = NULL;
  for (int i = 0; i < argc; ++i) {
    if (strncmp(argv[i], "--reveal-spoiler=", 17) == 0) {
      reveal_path = argv[i] + 17;
      break;
    }
  }
  if (reveal_path == NULL) return;

  // Load config + assets so the spoiler writer has the same view of the
  // world as a normal generate. Assets aren't strictly needed for reveal
  // (Spoiler_WriteJson computes `placement_digest_hex` from the placement
  // graph, which is asset-independent), so load them only if present — a
  // ROM-less checkout (CI) reveals deterministically all the same.
  ParseConfigFile(config_file);
  LoadAssetsIfPresent();

  RandoRevealResult r = Rando_RevealSpoiler(reveal_path, NULL);
  if (r == kRandoReveal_Ok) {
    fprintf(stderr, "--reveal-spoiler: OK at %s\n", reveal_path);
    exit(0);
  }
  fprintf(stderr, "--reveal-spoiler: FAILED (%d): %s\n  path: %s\n",
          (int)r, Rando_RevealResultDescription(r), reveal_path);
  exit(1);
}

// Comparator for qsort over uint64 samples — ascending order.
static int bench_cmp_u64(const void *a, const void *b) {
  uint64_t lhs = *(const uint64_t *)a;
  uint64_t rhs = *(const uint64_t *)b;
  if (lhs < rhs) return -1;
  if (lhs > rhs) return 1;
  return 0;
}

static void MaybeRunBenchLogicAndExit(int argc, char **argv) {
  bool found = false;
  int iters = 1000;
  bool no_fail = false;
  for (int i = 0; i < argc; ++i) {
    if (strcmp(argv[i], "--rando-bench-logic") == 0) found = true;
    else if (strncmp(argv[i], "--bench-iters=", 14) == 0) iters = atoi(argv[i] + 14);
    else if (strcmp(argv[i], "--bench-no-fail") == 0) no_fail = true;
  }
  if (!found) return;

  if (iters < 1) {
    fprintf(stderr, "--rando-bench-logic: --bench-iters must be >= 1 (got %d)\n", iters);
    exit(64);
  }
  if (iters > 100000) {
    fprintf(stderr, "--rando-bench-logic: --bench-iters capped at 100000 (got %d)\n", iters);
    exit(64);
  }

  // Build representative settings + "full inventory" counts. Phase A1's
  // populated graph today is Open / FastGanon (Standard/Inverted/Retro
  // start_region wiring is a follow-on per audit_phase_a1 line 144). Defaults
  // already pin world_state=Open / goal=FastGanon — the bench just runs them.
  RandoSettings settings;
  Settings_SetDefaults(&settings);

  // Full inventory: every progression item maxed. Mirrors the placer's
  // assumed-fill pre-state (counts populated with every progression item
  // before the assumed-fill loop decrements). The bench is reading the
  // graph at its widest reachable state — that's the worst-case work for
  // Logic_ComputeReachability and the right number to gate against.
  RandoCounts counts;
  memset(&counts, 0, sizeof(counts));
  for (int i = 0; i < 256; i++) counts.by_item_id[i] = 7;  // enough for any HAS_AMOUNT N<=7
  counts.by_item_id[121] = 3;  // StartingHeart (per build_final_inventory pattern)
  counts.by_item_id[122] = 1;  // RescuedZelda (non-Standard pre-grant)

  // Allocate per-iteration sample buffer in performance-counter ticks.
  uint64_t *samples = (uint64_t *)calloc((size_t)iters, sizeof(uint64_t));
  if (samples == NULL) {
    fprintf(stderr, "--rando-bench-logic: out of memory (iters=%d)\n", iters);
    exit(1);
  }

  // SDL_GetPerformanceCounter / SDL_GetPerformanceFrequency are usable
  // without SDL_Init per SDL2 API contract (verified against SDL source —
  // both functions wrap platform raw clocks: QPC on Windows, monotonic
  // clock on POSIX, no init state). Avoiding clock_gettime here keeps the
  // src/rando/ determinism guard clean (no rando file uses time/clock_gettime).
  uint64_t freq = SDL_GetPerformanceFrequency();
  if (freq == 0) {
    fprintf(stderr, "--rando-bench-logic: SDL_GetPerformanceFrequency returned 0\n");
    free(samples);
    exit(1);
  }

  uint64_t bench_start = SDL_GetPerformanceCounter();
  for (int i = 0; i < iters; i++) {
    uint64_t t0 = SDL_GetPerformanceCounter();
    const RandoReachability *r = Logic_ComputeReachability(&counts, &settings);
    uint64_t t1 = SDL_GetPerformanceCounter();
    samples[i] = (t1 >= t0) ? (t1 - t0) : 0;
    // Touch the result so the compiler can't dead-code-eliminate the call.
    if (r == NULL) {
      // Should never happen; bail out so we don't report nonsense numbers.
      fprintf(stderr, "--rando-bench-logic: Logic_ComputeReachability returned NULL at iter %d\n", i);
      free(samples);
      exit(1);
    }
  }
  uint64_t bench_end = SDL_GetPerformanceCounter();
  uint64_t total_ticks = bench_end - bench_start;

  // Sort samples ascending; compute percentiles.
  qsort(samples, (size_t)iters, sizeof(uint64_t), bench_cmp_u64);

  // Percentile indices: round half-up (idx = ceil(p * (n-1)) using integer
  // math). For iters=1000: p50_idx=499, p95_idx=949, p99_idx=989.
  int p50_idx = (iters > 0) ? (iters - 1) / 2 : 0;
  int p95_idx = (iters > 0) ? (int)(((long long)iters * 95 - 100) / 100) : 0;
  int p99_idx = (iters > 0) ? (int)(((long long)iters * 99 - 100) / 100) : 0;
  if (p50_idx < 0) p50_idx = 0;
  if (p95_idx < 0) p95_idx = 0;
  if (p99_idx < 0) p99_idx = 0;
  if (p50_idx >= iters) p50_idx = iters - 1;
  if (p95_idx >= iters) p95_idx = iters - 1;
  if (p99_idx >= iters) p99_idx = iters - 1;

  uint64_t p50_ticks = samples[p50_idx];
  uint64_t p95_ticks = samples[p95_idx];
  uint64_t p99_ticks = samples[p99_idx];

  // Mean computed as total_ticks/iters (excludes loop overhead but the loop
  // overhead is dominated by Logic_ComputeReachability so the difference is
  // immaterial). Use integer arithmetic — the determinism guard forbids
  // `float `/`double ` in src/rando/ but main.c is outside that scope, and
  // we need double here to format percentiles in ms.
  uint64_t mean_ticks = (iters > 0) ? (total_ticks / (uint64_t)iters) : 0;

  // Convert each ticks count to milliseconds. We DO need floating point here
  // for sub-millisecond resolution display — main.c is outside src/rando/
  // and the determinism guard does not apply.
  double ticks_to_ms = 1000.0 / (double)freq;
  double p50_ms = (double)p50_ticks * ticks_to_ms;
  double p95_ms = (double)p95_ticks * ticks_to_ms;
  double p99_ms = (double)p99_ticks * ticks_to_ms;
  double mean_ms = (double)mean_ticks * ticks_to_ms;
  double total_ms = (double)total_ticks * ticks_to_ms;

  // Machine-parseable single-line report on stdout. CI scripts grep for
  // `bench_logic:` and parse fields by name; field names + ordering are
  // stable. Per-iteration tick counts are not surfaced (they're dense and
  // platform-specific); only the rolled-up percentiles are CI-relevant.
  fprintf(stdout,
    "bench_logic: iters=%d p50_ms=%.4f p95_ms=%.4f p99_ms=%.4f mean_ms=%.4f "
    "total_ms=%.2f graph_locations=%u graph_regions=%u graph_edges=%u "
    "p50_ticks=%llu freq=%llu\n",
    iters, p50_ms, p95_ms, p99_ms, mean_ms,
    total_ms,
    (unsigned)kRandoLocationsCount,
    (unsigned)kRandoRegionsCount,
    (unsigned)kRandoEdgesCount,
    (unsigned long long)p50_ticks,
    (unsigned long long)freq);

  free(samples);

  // Gate: p50 must be under 5 ms. CI fails the build when this exceeds.
  // --bench-no-fail suppresses the gate for diagnostic baselines.
  const double kBudgetMs = 5.0;
  if (p50_ms > kBudgetMs && !no_fail) {
    fprintf(stderr,
      "--rando-bench-logic: FAIL — p50_ms=%.4f exceeds budget %.1f ms\n",
      p50_ms, kBudgetMs);
    exit(1);
  }
  if (p50_ms > kBudgetMs && no_fail) {
    fprintf(stderr,
      "--rando-bench-logic: WARN — p50_ms=%.4f exceeds budget %.1f ms "
      "(suppressed by --bench-no-fail)\n",
      p50_ms, kBudgetMs);
  }
  exit(0);
}

// --generate-seed CLI/headless mode (tasks.md §1.6a).
//
// CRITICAL invariant: this function is called BEFORE any SDL_Init in main()
// and either runs to completion + exits, or returns (when the flag is not
// present) so main() can proceed with normal GUI startup. The function never
// initializes SDL_INIT_VIDEO/SDL_INIT_AUDIO; CI smoke-tests it with
// DISPLAY= unset on a Linux runner to confirm headless operation.
//
// The flag is detected, dependent argv is parsed, and configuration + assets
// load so g_assets_hash populates. The single-seed form then runs the full
// generator pipeline; the batch/manifest form is not yet implemented (exits
// 64). Either way the process exits without entering the GUI.
static void MaybeRunGenerateSeedAndExit(int argc, char **argv, const char *config_file);
static void MaybeRunRevealSpoilerAndExit(int argc, char **argv, const char *config_file);

// --rando-bench-logic CLI mode (tasks.md §3.11).
//
// Detects `--rando-bench-logic` in argv. When present, runs
// Logic_ComputeReachability `iters` times (default 1000, override with
// `--bench-iters=N`) against a representative full-inventory snapshot, sorts
// the per-iteration wall-clock samples, and prints p50/p95/p99/mean to stdout
// in a single machine-parseable line:
//
//   bench_logic: iters=1000 p50_ms=0.12 p95_ms=0.18 p99_ms=0.25 mean_ms=0.13
//                total_ms=130 graph_locations=237 graph_regions=N graph_edges=M
//
// Exits non-zero when p50_ms exceeds 5.0 (CI gate per `randomizer-logic /
// Budget benchmark` scenario). `--bench-no-fail` overrides the gate for
// diagnostic runs that just want the numbers.
//
// Runs BEFORE any SDL_Init. Uses SDL_GetPerformanceCounter /
// SDL_GetPerformanceFrequency for sub-microsecond resolution; per the SDL
// API contract these are callable before SDL_Init.
static void MaybeRunBenchLogicAndExit(int argc, char **argv);

#ifdef Z3R_NATIVE_SETTINGS_WINDOW
// Capture the live rando-window settings/seed/geometry/theme from the bridge +
// window into g_rando_window_prefs and atomically write the sidecar. Called
// after every successful Generate (so a later crash/softlock/kill can't lose
// the settings the player just used) AND once more at clean shutdown. The
// settings window must still be alive (RandoWindow_GetGeometry queries SDL).
static void PersistRandoWindowState(void) {
  Settings_CanonicalSerialize(&g_rando_window_bridge.pending,
                              g_rando_window_prefs.settings_canonical);
  g_rando_window_prefs.has_settings = true;
  g_rando_window_prefs.last_seed_u64 = g_rando_window_bridge.seed_u64;
  int gx, gy, gw, gh;
  RandoWindow_GetGeometry(&gx, &gy, &gw, &gh);
  if (gw > 0 && gh > 0) {
    g_rando_window_prefs.window_x = gx;
    g_rando_window_prefs.window_y = gy;
    g_rando_window_prefs.window_w = gw;
    g_rando_window_prefs.window_h = gh;
    g_rando_window_prefs.has_geometry = true;
  }
  Config_SaveRandoWindowIni("saves/rando_window.ini");
}
#endif

#undef main
int main(int argc, char** argv) {
  argc--, argv++;

  // Detect headless-mode CLI flags up-front and set the global before any
  // Die() can fire (e.g., from LoadAssets in a worktree without
  // zelda3_assets.dat). Without this guard, batch tooling that fails its
  // setup pops modal dialogs on the developer's desktop.
  for (int i = 0; i < argc; ++i) {
    if (strcmp(argv[i], "--rando-selftest") == 0 ||
        strcmp(argv[i], "--rando-bench-logic") == 0 ||
        strcmp(argv[i], "--generate-seed") == 0 ||
        strcmp(argv[i], "--generate-slot") == 0 ||
        strcmp(argv[i], "--print-assets-hash") == 0 ||
        strncmp(argv[i], "--vanilla-ram-check=", 20) == 0) {
      g_headless_mode = 1;
      break;
    }
  }

  const char *config_file = NULL;
  if (argc >= 2 && strcmp(argv[0], "--config") == 0) {
    config_file = argv[1];
    argc -= 2, argv += 2;
  } else {
    SwitchDirectory();
  }

  // Check for --rando-selftest BEFORE any SDL_Init. Runs the sub-system
  // self-tests (SHA-256 NIST vectors + xoshiro256** determinism + future
  // checks) and exits. CI invokes this on every Linux/macOS/Windows runner
  // to guard cross-platform byte-identity (tasks.md §2.2).
  for (int i = 0; i < argc; ++i) {
    if (strcmp(argv[i], "--rando-selftest") == 0) {
      Config_SelfCheckKeymap();    // keybind-model <-> rebuilt-hash equivalence (game-config UI)
      Config_SelfCheckIniWriter(); // in-place INI writer fidelity (game-config UI)
#ifdef Z3R_NATIVE_SETTINGS_WINDOW
      Cheats_SelfCheck();          // cheat-core gate + clamp invariants (debug panels)
      Panels_RenderSmokeCheck();   // headless render of every Debug/Randomizer panel
#endif
      Rando_RunAllSelfChecks();
      return 0;
    }
  }

  // Check for --rando-bench-logic BEFORE any SDL_Init. Runs the
  // Logic_ComputeReachability benchmark (tasks.md §3.11) and exits. When
  // the flag is absent this returns; main() then continues normally.
  MaybeRunBenchLogicAndExit(argc, argv);

  // Check for --generate-seed BEFORE any SDL_Init. If present, run headless
  // and exit; otherwise this returns and main() continues to the GUI path.
  MaybeRunGenerateSeedAndExit(argc, argv, config_file);
  MaybeRunGenerateSlotAndExit(argc, argv, config_file);
  MaybeRunRevealSpoilerAndExit(argc, argv, config_file);

  // Dev/verification: decode the overworld map (asset 66/67/68/93) to PPM files
  // and exit. Used to visually verify the Map Tracker background decoder.
  for (int i = 0; i < argc; ++i) {
    if (strcmp(argv[i], "--dump-overworld-map") == 0) {
      const char *prefix = (i + 1 < argc) ? argv[i + 1] : "overworld_map";
      LoadAssets();
      bool ok = RandoMap_DumpPpm(prefix);
      fprintf(stderr, "--dump-overworld-map: %s\n", ok ? "OK" : "FAILED");
      return ok ? 0 : 1;
    }
  }
  // Dev/verification: decode the HUD item-icon atlas to a PPM and exit.
  for (int i = 0; i < argc; ++i) {
    if (strcmp(argv[i], "--dump-item-icons") == 0) {
      const char *path = (i + 1 < argc) ? argv[i + 1] : "item_icons.ppm";
      LoadAssets();
      static uint32 atlas[kRandoIconCount * kRandoIconSize * kRandoIconSize];
      int n = Hud_RandoBuildIconAtlas(atlas);
      int W = kRandoIconCount * kRandoIconSize, H = kRandoIconSize;
      FILE *f = fopen(path, "wb");
      if (f) {
        fprintf(f, "P6\n%d %d\n255\n", W, H);
        for (int p = 0; p < W * H; p++) {
          const uint8 *px = (const uint8 *)&atlas[p];
          fputc(px[0], f); fputc(px[1], f); fputc(px[2], f);  // RGB (drop alpha)
        }
        fclose(f);
      }
      fprintf(stderr, "--dump-item-icons: %d icons -> %s\n", n, path);
      return f ? 0 : 1;
    }
  }


  // --vanilla-ram-check=<savestate-path>: init-order replay guard
  // (tasks.md §11.2 / §1.2 / §1.0d). Boots the engine in headless mode,
  // loads the chapter savestate via the replay-mode StateRecorder path
  // (which restores g_ram from the snapshot's base_snapshot without
  // running any frames), then prints the post-load values of the new
  // kRam_* offsets. The script that drives this asserts each cell is
  // zero — proof that pre-rando code paths in the savestate didn't
  // touch the addresses we've claimed for randomizer state.
  for (int i = 0; i < argc; ++i) {
    if (strncmp(argv[i], "--vanilla-ram-check=", 20) == 0) {
      const char *sav_path = argv[i] + 20;
      // No ParseConfigFile/LoadAssets here: the savestate restore reads
      // g_ram directly from the snapshot, not from asset tables. ZeldaInitialize
      // sets up the dma/ppu/sram pointers LoadSnesState needs but doesn't
      // require the asset blob. This lets the check run in CI without
      // zelda3_assets.dat (CI doesn't extract from the ROM).
      ZeldaInitialize();
      if (!ZeldaLoadSavestateForRamDump(sav_path)) {
        fprintf(stderr, "--vanilla-ram-check: unable to open savestate '%s'\n", sav_path);
        return 2;
      }
      // Read the three new kRam_* offsets from g_ram. Format is keyed so
      // the python driver can parse robustly.
      uint32 features1 = (uint32)g_ram[kRam_Features1] |
                         ((uint32)g_ram[kRam_Features1 + 1] << 8) |
                         ((uint32)g_ram[kRam_Features1 + 2] << 16) |
                         ((uint32)g_ram[kRam_Features1 + 3] << 24);
      uint8 slot_active = g_ram[kRam_RandoSlotActive];
      uint8 starting_inv = g_ram[kRam_RandoStartingInventoryGranted];
      bool ok = (features1 == 0 && slot_active == 0 && starting_inv == 0);
      fprintf(stdout,
        "ram_check savestate=%s ok=%d "
        "features1=0x%08x slot_active=0x%02x starting_inv=0x%02x\n",
        sav_path, ok ? 1 : 0,
        (unsigned)features1, (unsigned)slot_active, (unsigned)starting_inv);
      return ok ? 0 : 1;
    }
  }

  // --print-assets-hash: load assets, dump the SHA-256, exit. Lets the user
  // capture the vanilla hash for vanilla_assets_hash.h activation.
  for (int i = 0; i < argc; ++i) {
    if (strcmp(argv[i], "--print-assets-hash") == 0) {
      ParseConfigFile(config_file);
      LoadAssets();
      fprintf(stdout, "assets SHA-256: ");
      for (int b = 0; b < 32; b++) fprintf(stdout, "%02x", g_assets_hash[b]);
      fprintf(stdout, "\n");
      return 0;
    }
  }

  ParseConfigFile(config_file);
  LoadAssets();
  LoadLinkGraphics();

  ZeldaInitialize();
  g_zenv.ppu->extraLeftRight = UintMin(g_config.extended_aspect_ratio, kPpuExtraLeftRight);
  g_snes_width = (g_config.extended_aspect_ratio * 2 + 256);
  g_snes_height = (g_config.extend_y ? 240 : 224);


  // Delay actually setting those features in ram until any snapshots finish playing.
  g_wanted_zelda_features = g_config.features0;
  g_wanted_zelda_features1 = g_config.features1;  // randomizer flags (defaults to 0 until [randomizer] section parsed in 1.6)

  g_ppu_render_flags = g_config.new_renderer * kPpuRenderFlags_NewRenderer |
                       g_config.enhanced_mode7 * kPpuRenderFlags_4x4Mode7 |
                       g_config.extend_y * kPpuRenderFlags_Height240 |
                       g_config.no_sprite_limits * kPpuRenderFlags_NoSpriteLimits;
  ZeldaEnableMsu(g_config.enable_msu);
  ZeldaSetLanguage(g_config.language);

  if (g_config.fullscreen == 1)
    g_win_flags ^= SDL_WINDOW_FULLSCREEN_DESKTOP;
  else if (g_config.fullscreen == 2)
    g_win_flags ^= SDL_WINDOW_FULLSCREEN;

  // Window scale (1=100%, 2=200%, 3=300%, etc.)
  g_current_window_scale = (g_config.window_scale == 0) ? 2 : IntMin(g_config.window_scale, kMaxWindowScale);

  // audio_freq: Use common sampling rates (see user config file. values higher than 48000 are not supported.)
  if (g_config.audio_freq < 11025 || g_config.audio_freq > 48000)
    g_config.audio_freq = kDefaultFreq;

  // Currently, the SPC/DSP implementation only supports up to stereo.
  if (g_config.audio_channels < 1 || g_config.audio_channels > 2)
    g_config.audio_channels = kDefaultChannels;

  // audio_samples: power of 2
  if (g_config.audio_samples <= 0 || ((g_config.audio_samples & (g_config.audio_samples - 1)) != 0))
    g_config.audio_samples = kDefaultSamples;

  // set up SDL
  if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
    printf("Failed to init SDL: %s\n", SDL_GetError());
    return 1;
  }

  bool custom_size  = g_config.window_width != 0 && g_config.window_height != 0;
  int window_width  = custom_size ? g_config.window_width  : g_current_window_scale * g_snes_width;
  int window_height = custom_size ? g_config.window_height : g_current_window_scale * g_snes_height;

  if (g_config.output_method == kOutputMethod_OpenGL ||
      g_config.output_method == kOutputMethod_OpenGL_ES) {
    g_win_flags |= SDL_WINDOW_OPENGL;
    OpenGLRenderer_Create(&g_renderer_funcs, (g_config.output_method == kOutputMethod_OpenGL_ES));
  } else {
    g_renderer_funcs = kSdlRendererFuncs;
  }

  SDL_Window* window = SDL_CreateWindow(kWindowTitle, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, window_width, window_height, g_win_flags);
  if(window == NULL) {
    printf("Failed to create window: %s\n", SDL_GetError());
    return 1;
  }
  g_window = window;
  SDL_SetWindowHitTest(window, HitTestCallback, NULL);

  if (!g_renderer_funcs.Initialize(window))
    return 1;

#ifdef Z3R_NATIVE_SETTINGS_WINDOW
  // Native settings window (PC only). Created AFTER SDL_Init and AFTER the game
  // window — and therefore after every headless `Maybe*AndExit` / inline
  // `--rando-selftest` / `--print-assets-hash` handler, all of which return or
  // exit() before SDL_Init. Headless CLI thus never reaches here / opens a window.
  //
  // The settings window has its OWN dedicated GL context. Set the GL attributes
  // BEFORE creating it. (The game renderer — software by default, or its own GL
  // context when output_method is OpenGL — has already been initialized above; we
  // never reuse the game window for the settings context.)
#ifdef __APPLE__
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
#else
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#endif
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

  // Capture the game's current GL context (the OpenGL renderer left it current
  // after Initialize above; NULL under the default SDL software renderer) so we
  // can restore it after the settings window's GL setup makes the settings
  // context current — otherwise the game would render against the settings
  // context and the per-frame restore would bind the wrong context. (audit HIGH)
  SDL_GLContext game_gl_ctx = SDL_GL_GetCurrentContext();

  g_settings_window = SDL_CreateWindow(
      "Z3R Settings", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 720, 900,
      SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_HIDDEN);
  if (g_settings_window == NULL) {
    printf("Failed to create settings window: %s\n", SDL_GetError());
    return 1;
  }
  g_settings_gl = SDL_GL_CreateContext(g_settings_window);  // NEVER g_window here
  if (g_settings_gl == NULL) {
    printf("Failed to create settings GL context: %s\n", SDL_GetError());
    return 1;
  }
  RandoWindowBridge_Init();

  // P5 — load the rando-window sidecar (saves/rando_window.ini) BEFORE
  // RandoWindow_Init so the persisted dark_theme is applied at ImGui init.
  // The sidecar is parsed into g_rando_window_prefs (never touches zelda3.ini
  // / g_config; see Config_LoadAuxIniFile). Absent file → graceful defaults.
  Config_LoadAuxIniFile("saves/rando_window.ini");

  RandoWindow_Init(g_settings_window, g_settings_gl);

  // P5 — apply persisted settings to the bridge. RandoWindowBridge_Init already
  // set Settings_SetDefaults; override with the saved canonical bytes only if
  // they deserialize AND round-trip back to identical bytes (rejects a struct
  // whose layout/version drifted since it was written).
  // Couple the prefs buffer size to the canonical length so the memcmp below
  // can never overrun settings_canonical[] if kSettingsCanonicalLen changes.
  _Static_assert(sizeof(g_rando_window_prefs.settings_canonical) == kSettingsCanonicalLen,
                 "RandoWindowPrefs.settings_canonical size must equal kSettingsCanonicalLen");
  if (g_rando_window_prefs.has_settings) {
    RandoSettings restored;
    uint8 reser[kSettingsCanonicalLen];
    if (Settings_CanonicalDeserialize(g_rando_window_prefs.settings_canonical, &restored) == 0 &&
        Settings_CanonicalSerialize(&restored, reser) == kSettingsCanonicalLen &&
        memcmp(reser, g_rando_window_prefs.settings_canonical, kSettingsCanonicalLen) == 0) {
      g_rando_window_bridge.pending = restored;
    } else {
      Settings_SetDefaults(&g_rando_window_bridge.pending);
    }
  }
  g_rando_window_bridge.seed_u64 = g_rando_window_prefs.last_seed_u64;
  RandoWindowBridge_RecomputeDerived();

  // P5 — restore the saved window geometry (clamped to on-screen; re-centers if
  // fully off-screen). Applied while the window is still hidden, so no jump.
  if (g_rando_window_prefs.has_geometry) {
    RandoWindow_ApplyGeometry(g_rando_window_prefs.window_x, g_rando_window_prefs.window_y,
                              g_rando_window_prefs.window_w, g_rando_window_prefs.window_h);
  }
  // Tracker windows (item/check/map) — created hidden on the multi-window host.
  // Z3RHost_Create saves/restores the current ImGui + GL context, so the settings
  // window's context stays current and the game context is untouched here.
  Trackers_Init();

  // Restore the game's GL context: the settings window's GL setup above left the
  // settings context current. NULL under the software renderer → nothing to do.
  if (game_gl_ctx)
    SDL_GL_MakeCurrent(g_window, game_gl_ctx);
#endif  // Z3R_NATIVE_SETTINGS_WINDOW

  SDL_AudioDeviceID device = 0;
  SDL_AudioSpec want = { 0 }, have;
  g_audio_mutex = SDL_CreateMutex();
  if (!g_audio_mutex) Die("No mutex");

  if (g_config.enable_audio) {
    want.freq = g_config.audio_freq;
    want.format = AUDIO_S16;
    want.channels = g_config.audio_channels;
    want.samples = g_config.audio_samples;
    want.callback = &AudioCallback;
    device = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (device == 0) {
      printf("Failed to open audio device: %s\n", SDL_GetError());
      return 1;
    }
    g_audio_channels = have.channels;
    g_frames_per_block = (534 * have.freq) / 32000;
    g_audiobuffer = malloc(g_frames_per_block * have.channels * sizeof(int16));
  }

  if (argc >= 1 && !g_run_without_emu)
    LoadRom(argv[0]);

#if defined(_WIN32)
  _mkdir("saves");
#else
  mkdir("saves", 0755);
#endif

  ZeldaReadSram();

  for (int i = 0; i < SDL_NumJoysticks(); i++)
    OpenOneGamepad(i);

  bool running = true;
  SDL_Event event;
  uint32 lastTick = SDL_GetTicks();
  uint32 curTick = 0;
  uint32 frameCtr = 0;
  bool audiopaused = true;

  if (g_config.autosave)
    HandleCommand(kKeys_Load + 0, true);

  // §9.1b — track SDL text-input state. SDL_StartTextInput / StopTextInput
  // are global toggles that enable/disable SDL_TEXTINPUT event delivery. We
  // only call them on transition so we don't spam SDL with redundant calls.
  bool sdl_text_input_started = false;

  while(running) {
    // §9.1b — sync SDL text-input enable state with the game's UI flag set
    // by the alphabet picker (g_rando_text_input_active). Start text input
    // when the picker activates a text field; stop it when the picker exits.
    if (g_rando_text_input_active && !sdl_text_input_started) {
      SDL_StartTextInput();
      sdl_text_input_started = true;
      // Clear any joypad bits held over from the keypress that activated
      // the picker. Without this, the SDL_KEYDOWN that triggered the
      // transition already set g_input1_state; its matching SDL_KEYUP is
      // swallowed below (while text input is active), so the bit would
      // stay set forever. Mirror clearing for gamepad modifiers since
      // gamepad buttons are tracked separately. Also reset the host-
      // pending submit/cancel one-shots so a stale flag from a prior
      // session doesn't fire on the first frame of this picker.
      g_input1_state = 0;
      g_gamepad_buttons = 0;
      g_rando_text_input_submit_pending = false;
      g_rando_text_input_cancel_pending = false;
    } else if (!g_rando_text_input_active && sdl_text_input_started) {
      SDL_StopTextInput();
      sdl_text_input_started = false;
      // Same logic in reverse — clear any stale bits from typing keys held
      // when the picker closed (the keyup wasn't routed, so we'd inherit
      // phantom presses on the file-select screen).
      g_input1_state = 0;
      g_gamepad_buttons = 0;
      g_rando_text_input_submit_pending = false;
      g_rando_text_input_cancel_pending = false;
    }

    while(SDL_PollEvent(&event)) {
#ifdef Z3R_NATIVE_SETTINGS_WINDOW
      // Two-window event routing. GLOBAL / no-windowID events (SDL_QUIT, all
      // SDL_CONTROLLER*/SDL_JOY*, audio device add/remove, SDL_KEYMAPCHANGED)
      // are NOT window-targeted and fall through to the game path unchanged.
      // WINDOW-targeted events carry a windowID in their event-union sub-struct;
      // if that ID is the settings window, hand the event to ImGui and DO NOT
      // pass it to the game input path. Otherwise it's a game-window event and
      // flows to the existing switch below.
      // Tracker windows (host-owned) consume their own events first. Their
      // windowIDs are disjoint from the settings/game windows.
      if (Z3RHost_ProcessEvent(&event))
        continue;
      {
        Uint32 settings_wid = g_settings_window ? SDL_GetWindowID(g_settings_window) : 0;
        Uint32 wid = 0;
        bool is_windowed = true;
        switch (event.type) {
        case SDL_WINDOWEVENT:    wid = event.window.windowID; break;
        case SDL_KEYDOWN:
        case SDL_KEYUP:          wid = event.key.windowID;    break;
        case SDL_TEXTINPUT:      wid = event.text.windowID;   break;
        case SDL_TEXTEDITING:    wid = event.edit.windowID;   break;
        case SDL_MOUSEMOTION:    wid = event.motion.windowID; break;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:  wid = event.button.windowID; break;
        case SDL_MOUSEWHEEL:     wid = event.wheel.windowID;  break;
        case SDL_DROPFILE:
        case SDL_DROPTEXT:
        case SDL_DROPBEGIN:
        case SDL_DROPCOMPLETE:   wid = event.drop.windowID;   break;
        default:                 is_windowed = false;         break;  // global → game path
        }
        if (is_windowed && settings_wid != 0 && wid == settings_wid) {
          if (event.type == SDL_WINDOWEVENT &&
              event.window.event == SDL_WINDOWEVENT_CLOSE) {
            // Settings-window close button: hide, don't quit the app.
            RandoWindow_Hide();
          } else {
            RandoWindow_ProcessEvent(&event);
          }
          continue;  // settings-window event consumed; never reaches the game
        }
      }
#endif  // Z3R_NATIVE_SETTINGS_WINDOW
      switch(event.type) {
      case SDL_CONTROLLERDEVICEADDED:
        OpenOneGamepad(event.cdevice.which);
        break;
      case SDL_CONTROLLERAXISMOTION:
        HandleGamepadAxisInput(event.caxis.which, event.caxis.axis, event.caxis.value);
        break;
      case SDL_CONTROLLERBUTTONDOWN:
      case SDL_CONTROLLERBUTTONUP: {
        int b = RemapSdlButton(event.cbutton.button);
#ifdef Z3R_NATIVE_SETTINGS_WINDOW
        // Gamepad binding capture: while a rebind is armed, the next button-down
        // (with currently-held buttons as the combo) is consumed by the config UI
        // instead of the game. g_gamepad_modifiers does not yet include this
        // button (we intercept before HandleGamepadInput toggles it).
        if (b >= 0 && event.type == SDL_CONTROLLERBUTTONDOWN && GameConfig_IsCapturingGamepad()) {
          GameConfig_FeedCapturedButton(b, g_gamepad_modifiers);
          break;
        }
#endif
        if (b >= 0)
          HandleGamepadInput(b, event.type == SDL_CONTROLLERBUTTONDOWN);
        break;
      }
      case SDL_MOUSEWHEEL:
        if (SDL_GetModState() & KMOD_CTRL && event.wheel.y != 0)
          ChangeWindowScale(event.wheel.y > 0 ? 1 : -1);
        break;
      case SDL_MOUSEBUTTONDOWN:
        if (event.button.button == SDL_BUTTON_LEFT && event.button.state == SDL_PRESSED && event.button.clicks == 2) {
          if ((g_win_flags & SDL_WINDOW_FULLSCREEN_DESKTOP) == 0 && (g_win_flags & SDL_WINDOW_FULLSCREEN) == 0 && SDL_GetModState() & KMOD_SHIFT) {
            g_win_flags ^= SDL_WINDOW_BORDERLESS;
            SDL_SetWindowBordered(g_window, (g_win_flags & SDL_WINDOW_BORDERLESS) == 0);
          }
        }
        break;
      case SDL_TEXTINPUT: {
        // §9.1b — route typed chars into the active rando text field. SDL
        // delivers UTF-8 in event.text.text; we feed bytes through
        // TextField_HandleChar which applies the base32 filter. Multi-byte
        // chars are filtered out silently (base32 is ASCII only).
        if (g_rando_text_input_active && g_rando_active_textfield != NULL) {
          const char *s = event.text.text;
          for (int i = 0; s[i] != 0 && i < (int)sizeof(event.text.text); i++) {
            TextField_HandleChar(g_rando_active_textfield, s[i]);
          }
        }
        break;
      }
      case SDL_KEYDOWN:
        // §9.1b — while text input is active, intercept editing keys
        // (backspace/delete/arrows/home/end/enter/escape/Ctrl+V) and route
        // them through TextField_HandleKey. Critically we MUST NOT also
        // dispatch these keys through HandleInput while text input is
        // active: otherwise, e.g. typing 'A' would both insert 'A' (via
        // SDL_TEXTINPUT) AND fire the SNES "A" button (via the kKbdRemap
        // table in HandleCommand), so the user's keystrokes would also
        // drive Link or menu cursors.
        if (g_rando_text_input_active && g_rando_active_textfield != NULL) {
          RandoTextField *tf = g_rando_active_textfield;
          SDL_Keycode k = event.key.keysym.sym;
          SDL_Keymod m = event.key.keysym.mod;
          switch (k) {
            case SDLK_BACKSPACE: TextField_HandleKey(tf, kTextFieldKey_Backspace); break;
            case SDLK_DELETE:    TextField_HandleKey(tf, kTextFieldKey_Delete);    break;
            case SDLK_LEFT:      TextField_HandleKey(tf, kTextFieldKey_Left);      break;
            case SDLK_RIGHT:     TextField_HandleKey(tf, kTextFieldKey_Right);     break;
            case SDLK_HOME:      TextField_HandleKey(tf, kTextFieldKey_Home);      break;
            case SDLK_END:       TextField_HandleKey(tf, kTextFieldKey_End);       break;
            case SDLK_RETURN:
            case SDLK_KP_ENTER:
              // Set the host-pending submit flag so the active UI surface
              // (alphabet picker) decodes the buffer on its next Update tick.
              // Also call HandleKey for symmetry / self-check observability.
              //
              // §9 cluster-2 audit MED-3: skip auto-repeat KEYDOWNs. SDL
              // emits repeat KEYDOWN every ~30ms when a key is held, which
              // would re-fire submit every frame and prevent the OK overlay
              // countdown from completing while the user held Enter.
              if (!event.key.repeat) {
                TextField_HandleKey(tf, kTextFieldKey_Submit);
                g_rando_text_input_submit_pending = true;
              }
              break;
            case SDLK_ESCAPE:
              // Cancel — host-pending so the UI surface can close cleanly.
              // Skip auto-repeat: same rationale as Enter above.
              if (!event.key.repeat) {
                g_rando_text_input_cancel_pending = true;
              }
              break;
            case SDLK_v:
              if (m & KMOD_CTRL) {
                char *clip = SDL_GetClipboardText();
                if (clip) {
                  TextField_PasteString(tf, clip);
                  SDL_free(clip);
                }
              }
              // Note: bare 'v' (no Ctrl) falls through to SDL_TEXTINPUT for
              // normal char entry. V IS a base32 letter so the filter
              // ACCEPTS it — we want that. Ctrl+V emits no SDL_TEXTINPUT
              // (control characters suppressed), so the paste path above
              // does not double-insert with a bare 'v' from this case.
              break;
            default:
              // All other keys (alphabetic, digits, punctuation) flow through
              // SDL_TEXTINPUT for char entry. We just need to swallow them
              // here to suppress the keyboard→joypad path; do not call
              // HandleInput.
              break;
          }
          // Suppress HandleInput entirely — see comment above.
          break;
        }
        // The debug-state dump (default F12, rebindable as kKeys_DumpDebugState)
        // writes several files; ignore key auto-repeat so holding the key can't
        // thrash the disk. The first (non-repeat) press flows through HandleInput
        // -> HandleCommand -> kKeys_DumpDebugState like any other command.
        if (event.key.repeat &&
            FindCmdForSdlKey(event.key.keysym.sym, event.key.keysym.mod) == kKeys_DumpDebugState)
          break;
        HandleInput(event.key.keysym.sym, event.key.keysym.mod, true);
        break;
      case SDL_KEYUP:
        // §9.1b — mirror the keydown suppression on key release. While text
        // input is active, no SDL_KEYUP reaches HandleInput either. A key
        // held across the activation/deactivation boundary would otherwise
        // strand a joypad bit; the transition block at the top of the
        // while(running) loop zeroes g_input1_state on both transitions
        // (activate AND deactivate) to handle that edge case.
        if (g_rando_text_input_active) {
          break;
        }
        HandleInput(event.key.keysym.sym, event.key.keysym.mod, false);
        break;
      case SDL_QUIT:
        running = false;
        break;
#ifdef Z3R_NATIVE_SETTINGS_WINDOW
      case SDL_WINDOWEVENT:
        // Only game-window window-events reach here (settings-window events were
        // routed to ImGui above). With a second window open, SDL may not emit
        // SDL_QUIT when the game window's close button is pressed, so handle the
        // game-window close explicitly → app shutdown.
        if (event.window.event == SDL_WINDOWEVENT_CLOSE)
          running = false;
        break;
#endif
      }
    }

    if (g_paused != audiopaused) {
      audiopaused = g_paused;
      if (device)
        SDL_PauseAudioDevice(device, audiopaused);
    }

    // Paused: don't advance the game, but (PC) keep the settings/tracker windows
    // interactive so you can unpause / frame-step / edit from the UI. A pending
    // frame-step lets exactly one frame through (g_paused stays set).
    if (g_paused && !g_frame_step) {
#ifdef Z3R_NATIVE_SETTINGS_WINDOW
      if (GameConfig_HasPendingApply())
        GameConfig_ApplyPending();
      GameConfig_CaptureTick(SDL_GetTicks());
      if (RandoWindow_WantsShown()) {
        SDL_GLContext prev = SDL_GL_GetCurrentContext();
        RandoWindow_BeginFrame();
        RandoWindow_Render();
        if (prev)
          SDL_GL_MakeCurrent(g_window, prev);
      }
      Z3RHost_RenderAll();
#endif
      SDL_Delay(16);
      continue;
    }
    g_frame_step = false;  // consume the one-shot; run exactly one frame this pass

    // Clear gamepad inputs when joypad directional inputs to avoid wonkiness
    int inputs = g_input1_state;
    if (g_input1_state & 0xf0)
      g_gamepad_buttons = 0;
    inputs |= g_gamepad_buttons;

    SDL_LockMutex(g_audio_mutex);
    bool is_replay = ZeldaRunFrame(inputs);
    SDL_UnlockMutex(g_audio_mutex);

#ifdef Z3R_NATIVE_SETTINGS_WINDOW
    // Game-config apply consumer: the settings UI raises a pending-apply flag;
    // commit the working copy + rebuild the keymap + live-apply + write the INI
    // here on the game thread (game GL context current). Runs BEFORE the generate
    // consumer so a just-applied config is reflected if a generate follows.
    if (GameConfig_HasPendingApply())
      GameConfig_ApplyPending();
    GameConfig_CaptureTick(SDL_GetTicks());  // time out a stuck rebind capture

    // Game-side generate consumer: when the settings window requested a generate,
    // run it synchronously on this (game) thread. Blocks the game frame for the
    // generation duration — expected; the UI shows an input-blocking modal.
    if (RandoWindowBridge_ConsumeGenerateRequest()) {
      RandoGenerateResult res; char err[256] = {0};
      RandoWindowBridge *b = &g_rando_window_bridge;
      bool ok = Rando_GenerateSlot(&b->pending, b->seed_u64, -1, b->target_slot_index,
                                   b->pending_recommended_features0, &res, err, sizeof err);
      if (ok) {
        RandoWindowBridge_StoreGenerated(&res.placement, NULL, res.race_mode);  // bridge copies
        free(res.placement.entries);                                            // free our owned copy
        // Snapshot the settings/share/seed that produced this placement so the
        // Spoiler tab's "Save spoiler" writes an accurate RandoSpoiler even after
        // the user edits `pending`. (game thread owns last_generated_*.)
        b->last_generated_settings = b->pending;
        b->last_generated_seed_u64 = b->seed_u64;
        b->last_generated_goal_completable = res.goal_completable;
        strncpy(b->last_generated_share_string, res.share_string,
                sizeof b->last_generated_share_string - 1);
        b->last_generated_share_string[sizeof b->last_generated_share_string - 1] = '\0';
        RandoWindowBridge_SetGenerateResult(2, "");
        // Persist the settings the player just generated with NOW (not only at
        // exit) so a crash/softlock/kill can't revert them — makes settings
        // sticky for the next seed.
        PersistRandoWindowState();
      } else {
        RandoWindowBridge_SetGenerateResult(-1, err);
      }
    }

    // "Load it now" consumer (§13.7): the UI raises a load request after a
    // successful generate; perform the file-select load here on the game thread
    // (touches g_ram/WRAM, so it cannot run from the ImGui side). At this point
    // Module01_FileSelect is the active module sitting at FileSelect_Main, which
    // is exactly the state the in-game occupied-slot load path expects.
    {
      int load_slot = RandoWindowBridge_ConsumeLoadRequest();
      if (load_slot >= 0)
        SelectFile_LoadRandoSlot(load_slot);
    }
#endif  // Z3R_NATIVE_SETTINGS_WINDOW

    frameCtr++;

    if ((g_turbo ^ (is_replay & g_replay_turbo)) && (frameCtr & (g_turbo ? 0xf : 0x7f)) != 0) {
      continue;
    }

    DrawPpuFrameWithPerf();

#ifdef Z3R_NATIVE_SETTINGS_WINDOW
    // Render the native settings window after the game frame. Save the game's
    // current GL context (NULL when the game uses the SDL software renderer) and
    // restore it after — RandoWindow_BeginFrame/Render make the SETTINGS context
    // current internally, so without this restore an OpenGL game renderer would
    // be left bound to the wrong context. No coupling to opengl.c internals.
    if (RandoWindow_WantsShown()) {
      SDL_GLContext prev = SDL_GL_GetCurrentContext();
      RandoWindow_BeginFrame();
      RandoWindow_Render();
      if (prev)
        SDL_GL_MakeCurrent(g_window, prev);
    }
    // Render any visible tracker windows. Z3RHost_RenderAll saves/restores the
    // current SDL GL + ImGui context itself, so the game renderer and the
    // settings window are unaffected regardless of which (if any) is shown.
    Z3RHost_RenderAll();
#endif

    if (g_config.display_perf_title) {
      char title[60];
      snprintf(title, sizeof(title), "%s | FPS: %d", kWindowTitle, g_curr_fps);
      SDL_SetWindowTitle(g_window, title);
    }

    // if vsync isn't working, delay manually
    curTick = SDL_GetTicks();

    if (!g_config.disable_frame_delay) {
      static const uint8 delays[3] = { 17, 17, 16 }; // 60 fps
      // Speed multiplier (debug Time panel): scale the pacing delay only — slower
      // speed => larger increment => longer sleep; faster => shorter. Audio is
      // produced on its own thread at the device clock, so it stays real-time.
      lastTick += (uint32)((float)delays[frameCtr % 3] / g_game_speed + 0.5f);

      if (lastTick > curTick) {
        uint32 delta = lastTick - curTick;
        if (delta > 500) {
          lastTick = curTick - 500;
          delta = 500;
        }
//        printf("Sleeping %d\n", delta);
        SDL_Delay(delta);
      } else if (curTick - lastTick > 500) {
        lastTick = curTick;
      }
    }
  }
  if (g_config.autosave)
    HandleCommand(kKeys_Save + 0, true);

  // clean sdl
  if (g_config.enable_audio) {
    SDL_PauseAudioDevice(device, 1);
    SDL_CloseAudioDevice(device);
  }

  SDL_DestroyMutex(g_audio_mutex);
  free(g_audiobuffer);

  g_renderer_funcs.Destroy();

  // §9.1b — defensive: SDL_StopTextInput if we exited while text input
  // was active (e.g. user closed window during share-string entry).
  // SDL_Quit() would clean up state anyway but explicit is clearer.
  if (SDL_IsTextInputActive()) SDL_StopTextInput();

#ifdef Z3R_NATIVE_SETTINGS_WINDOW
  // P5 — persist the rando-window state to the sidecar before teardown (same
  // capture used after each successful Generate). The settings window is still
  // alive here (RandoWindow_GetGeometry queries SDL directly).
  PersistRandoWindowState();

  // Tear down the settings window unconditionally (NOT gated on enable_audio).
  RandoWindow_Shutdown();
  // Then the tracker windows. RandoWindow_Shutdown destroyed the (current)
  // settings ImGui context first, so the host's teardown won't clobber it.
  Trackers_Shutdown();
  if (g_settings_gl)
    SDL_GL_DeleteContext(g_settings_gl);
  if (g_settings_window)
    SDL_DestroyWindow(g_settings_window);
#endif

  SDL_DestroyWindow(window);
  SDL_Quit();
  //SaveConfigFile();
  return 0;
}

static void RenderDigit(uint8 *dst, size_t pitch, int digit, uint32 color, bool big) {
  static const uint8 kFont[] = {
    0x1c, 0x36, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x36, 0x1c,
    0x18, 0x1c, 0x1e, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x7e,
    0x3e, 0x63, 0x60, 0x30, 0x18, 0x0c, 0x06, 0x03, 0x63, 0x7f,
    0x3e, 0x63, 0x60, 0x60, 0x3c, 0x60, 0x60, 0x60, 0x63, 0x3e,
    0x30, 0x38, 0x3c, 0x36, 0x33, 0x7f, 0x30, 0x30, 0x30, 0x78,
    0x7f, 0x03, 0x03, 0x03, 0x3f, 0x60, 0x60, 0x60, 0x63, 0x3e,
    0x1c, 0x06, 0x03, 0x03, 0x3f, 0x63, 0x63, 0x63, 0x63, 0x3e,
    0x7f, 0x63, 0x60, 0x60, 0x30, 0x18, 0x0c, 0x0c, 0x0c, 0x0c,
    0x3e, 0x63, 0x63, 0x63, 0x3e, 0x63, 0x63, 0x63, 0x63, 0x3e,
    0x3e, 0x63, 0x63, 0x63, 0x7e, 0x60, 0x60, 0x60, 0x30, 0x1e,
  };
  const uint8 *p = kFont + digit * 10;
  if (!big) {
    for (int y = 0; y < 10; y++, dst += pitch) {
      int v = *p++;
      for (int x = 0; v; x++, v >>= 1) {
        if (v & 1)
          ((uint32 *)dst)[x] = color;
      }
    }
  } else {
    for (int y = 0; y < 10; y++, dst += pitch * 2) {
      int v = *p++;
      for (int x = 0; v; x++, v >>= 1) {
        if (v & 1) {
          ((uint32 *)dst)[x * 2 + 1] = ((uint32 *)dst)[x * 2] = color;
          ((uint32 *)(dst+pitch))[x * 2 + 1] = ((uint32 *)(dst + pitch))[x * 2] = color;
        }
      }
    }
  }
}

static void RenderNumber(uint8 *dst, size_t pitch, int n, bool big) {
  char buf[32], *s;
  int i;
  sprintf(buf, "%d", n);
  for (s = buf, i = 2 * 4; *s; s++, i += 8 * 4)
    RenderDigit(dst + ((pitch + i + 4) << big), pitch, *s - '0', 0x404040, big);
  for (s = buf, i = 2 * 4; *s; s++, i += 8 * 4)
    RenderDigit(dst + (i << big), pitch, *s - '0', 0xffffff, big);
}

static void HandleCommand_Locked(uint32 j, bool pressed);

static void HandleCommand(uint32 j, bool pressed) {
  if (j <= kKeys_Controls_Last) {
    static const uint8 kKbdRemap[] = { 0, 4, 5, 6, 7, 2, 3, 8, 0, 9, 1, 10, 11 };
    if (pressed)
      g_input1_state |= 1 << kKbdRemap[j];
    else
      g_input1_state &= ~(1 << kKbdRemap[j]);
    return;
  }

  if (j == kKeys_Turbo) {
    g_turbo = pressed;
    return;
  }

  // Everything that might access audio state
  // (like SaveLoad and Reset) must have the lock.
  SDL_LockMutex(g_audio_mutex);
  HandleCommand_Locked(j, pressed);
  SDL_UnlockMutex(g_audio_mutex);
}

void ZeldaApuLock() {
  SDL_LockMutex(g_audio_mutex);
}

void ZeldaApuUnlock() {
  SDL_UnlockMutex(g_audio_mutex);
}


static void HandleCommand_Locked(uint32 j, bool pressed) {
  if (!pressed)
    return;
  if (j <= kKeys_Load_Last) {
    SaveLoadSlot(kSaveLoad_Load, j - kKeys_Load);
  } else if (j <= kKeys_Save_Last) {
    SaveLoadSlot(kSaveLoad_Save, j - kKeys_Save);
  } else if (j <= kKeys_Replay_Last) {
    SaveLoadSlot(kSaveLoad_Replay, j - kKeys_Replay);
  } else if (j <= kKeys_LoadRef_Last) {
    SaveLoadSlot(kSaveLoad_Load, 256 + j - kKeys_LoadRef);
  } else if (j <= kKeys_ReplayRef_Last) {
    SaveLoadSlot(kSaveLoad_Replay, 256 + j - kKeys_ReplayRef);
  } else {
    switch (j) {
    case kKeys_CheatLife: PatchCommand('w'); break;
    case kKeys_CheatEquipment: PatchCommand('W'); break;
    case kKeys_CheatKeys: PatchCommand('o'); break;
    case kKeys_CheatWalkThroughWalls: PatchCommand('E'); break;
    case kKeys_ClearKeyLog: PatchCommand('k'); break;
    case kKeys_StopReplay: PatchCommand('l'); break;
    case kKeys_Fullscreen:
      g_win_flags ^= SDL_WINDOW_FULLSCREEN_DESKTOP;
      SDL_SetWindowFullscreen(g_window, g_win_flags & SDL_WINDOW_FULLSCREEN_DESKTOP);
      g_cursor = !g_cursor;
      SDL_ShowCursor(g_cursor);
      break;
    case kKeys_Reset:
      ZeldaReset(true);
      break;
    case kKeys_Pause: g_paused = !g_paused; break;
    case kKeys_PauseDimmed:
      g_paused = !g_paused;
      // SDL_RenderPresent may not be called more than once per frame.
      // Seems to work on Windows still. Temporary measure until it's fixed.
#ifdef _WIN32
      if (g_paused) {
        SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 159);
        SDL_RenderFillRect(g_renderer, NULL);
        SDL_RenderPresent(g_renderer);
      }
#endif
      break;
    case kKeys_ReplayTurbo: g_replay_turbo = !g_replay_turbo; break;
    case kKeys_WindowBigger: ChangeWindowScale(1); break;
    case kKeys_WindowSmaller: ChangeWindowScale(-1); break;
    case kKeys_DisplayPerf: g_display_perf ^= 1; break;
    case kKeys_ToggleRenderer: g_ppu_render_flags ^= kPpuRenderFlags_NewRenderer; break;
    case kKeys_VolumeUp:
    case kKeys_VolumeDown: HandleVolumeAdjustment(j == kKeys_VolumeUp ? 1 : -1); break;
    // Phase B Slice 1 — tracker overlay toggles. On PC the OAM overlay is
    // superseded by the rich ImGui windows, so these legacy keys open the
    // corresponding window (so existing bindings keep working); on Switch they
    // toggle the OAM overlay as before. In-memory only; reset to hidden each launch.
    case kKeys_RandoToggleItemTracker:
#ifdef Z3R_NATIVE_SETTINGS_WINDOW
      Trackers_Toggle(kTracker_Item);
#else
      g_rando_show_item_tracker = !g_rando_show_item_tracker;
#endif
      break;
    case kKeys_RandoToggleLocationTracker:
#ifdef Z3R_NATIVE_SETTINGS_WINDOW
      Trackers_Toggle(kTracker_Check);
#else
      g_rando_show_location_tracker = !g_rando_show_location_tracker;
#endif
      break;
    // Phase B Slice 6 §62 — reveal the active slot's race-mode ZRSR spoiler.
    // The reveal action logs its outcome to stderr; on success the on-disk
    // file is overwritten with the full JSON + .txt companion.
    case kKeys_RandoRevealSpoiler:
      (void)Rando_RevealActiveSlotSpoiler();
      break;
    // Rich tracker windows. PC toggles the OS windows; on Switch (no
    // Z3R_NATIVE_SETTINGS_WINDOW) these keys exist in the keymap but have no
    // windows — degrade to a no-op rather than hitting default: assert(0) if a
    // user hand-binds one in the ini.
    case kKeys_RandoItemTrackerWindow:
    case kKeys_RandoCheckTrackerWindow:
    case kKeys_RandoMapTrackerWindow:
#ifdef Z3R_NATIVE_SETTINGS_WINDOW
      Trackers_Toggle(j == kKeys_RandoItemTrackerWindow ? kTracker_Item
                      : j == kKeys_RandoCheckTrackerWindow ? kTracker_Check
                                                           : kTracker_Map);
#endif
      break;
    // Native game-config window toggle (config mode). PC: open/hide the Z3R
    // Settings window on its Game Settings tab. Switch: no window — no-op (the
    // unconditional case label keeps the keymap index stable; cf. tracker keys).
    case kKeys_OpenSettings:
#ifdef Z3R_NATIVE_SETTINGS_WINDOW
      RandoWindow_ToggleConfig();
#endif
      break;
    // Developer state dump (g_ram/VRAM/OAM/CGRAM + hint state + a state line to
    // the log). Available on all platforms (ZeldaDumpDebugState is core).
    case kKeys_DumpDebugState:
      ZeldaDumpDebugState();
      break;
    default: assert(0);
    }
  }
}

static void HandleInput(int keyCode, int keyMod, bool pressed) {
  int j = FindCmdForSdlKey(keyCode, keyMod);
  if (j != 0)
    HandleCommand(j, pressed);
}

static void OpenOneGamepad(int i) {
  if (SDL_IsGameController(i)) {
    SDL_GameController *controller = SDL_GameControllerOpen(i);
    if (!controller)
      fprintf(stderr, "Could not open gamepad %d: %s\n", i, SDL_GetError());
  }
}

static int RemapSdlButton(int button) {
  switch (button) {
  case SDL_CONTROLLER_BUTTON_A: return kGamepadBtn_A;
  case SDL_CONTROLLER_BUTTON_B: return kGamepadBtn_B;
  case SDL_CONTROLLER_BUTTON_X: return kGamepadBtn_X;
  case SDL_CONTROLLER_BUTTON_Y: return kGamepadBtn_Y;
  case SDL_CONTROLLER_BUTTON_BACK: return kGamepadBtn_Back;
  case SDL_CONTROLLER_BUTTON_GUIDE: return kGamepadBtn_Guide;
  case SDL_CONTROLLER_BUTTON_START: return kGamepadBtn_Start;
  case SDL_CONTROLLER_BUTTON_LEFTSTICK: return kGamepadBtn_L3;
  case SDL_CONTROLLER_BUTTON_RIGHTSTICK: return kGamepadBtn_R3;
  case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return kGamepadBtn_L1;
  case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return kGamepadBtn_R1;
  case SDL_CONTROLLER_BUTTON_DPAD_UP: return kGamepadBtn_DpadUp;
  case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return kGamepadBtn_DpadDown;
  case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return kGamepadBtn_DpadLeft;
  case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return kGamepadBtn_DpadRight;
  default: return -1;
  }
}

static void HandleGamepadInput(int button, bool pressed) {
  if (!!(g_gamepad_modifiers & (1 << button)) == pressed)
    return;
  g_gamepad_modifiers ^= 1 << button;
  if (pressed)
    g_gamepad_last_cmd[button] = FindCmdForGamepadButton(button, g_gamepad_modifiers);
  if (g_gamepad_last_cmd[button] != 0)
    HandleCommand(g_gamepad_last_cmd[button], pressed);
}

static void HandleVolumeAdjustment(int volume_adjustment) {
#if SYSTEM_VOLUME_MIXER_AVAILABLE
  int current_volume = GetApplicationVolume();
  int new_volume = IntMin(IntMax(0, current_volume + volume_adjustment * 5), 100);
  SetApplicationVolume(new_volume);
  printf("[System Volume]=%i\n", new_volume);
#else
  g_sdl_audio_mixer_volume = IntMin(IntMax(0, g_sdl_audio_mixer_volume + volume_adjustment * (SDL_MIX_MAXVOLUME >> 4)), SDL_MIX_MAXVOLUME);
  printf("[SDL mixer volume]=%i\n", g_sdl_audio_mixer_volume);
#endif
}

// Approximates atan2(y, x) normalized to the [0,4) range
// with a maximum error of 0.1620 degrees
// normalized_atan(x) ~ (b x + x^2) / (1 + 2 b x + x^2)
static float ApproximateAtan2(float y, float x) {
  uint32 sign_mask = 0x80000000;
  float b = 0.596227f;
  // Extract the sign bits
  uint32 ux_s = sign_mask & *(uint32 *)&x;
  uint32 uy_s = sign_mask & *(uint32 *)&y;
  // Determine the quadrant offset
  float q = (float)((~ux_s & uy_s) >> 29 | ux_s >> 30);
  // Calculate the arctangent in the first quadrant
  float bxy_a = b * x * y;
  if (bxy_a < 0.0f) bxy_a = -bxy_a;  // avoid fabs
  float num = bxy_a + y * y;
  float atan_1q = num / (x * x + bxy_a + num + 0.000001f);
  // Translate it to the proper quadrant
  uint32_t uatan_2q = (ux_s ^ uy_s) | *(uint32 *)&atan_1q;
  return q + *(float *)&uatan_2q;
}

static void HandleGamepadAxisInput(int gamepad_id, int axis, int value) {
  static int last_gamepad_id, last_x, last_y;
  if (axis == SDL_CONTROLLER_AXIS_LEFTX || axis == SDL_CONTROLLER_AXIS_LEFTY) {
    // ignore other gamepads unless they have a big input
    if (last_gamepad_id != gamepad_id) {
      if (value > -16000 && value < 16000)
        return;
      last_gamepad_id = gamepad_id;
      last_x = last_y = 0;
    }
    *(axis == SDL_CONTROLLER_AXIS_LEFTX ? &last_x : &last_y) = value;
    int buttons = 0;
    if (last_x * last_x + last_y * last_y >= 10000 * 10000) {
      // in the non deadzone part, divide the circle into eight 45 degree
      // segments rotated by 22.5 degrees that control which direction to move.
      // todo: do this without floats?
      static const uint8 kSegmentToButtons[8] = {
        1 << 4,           // 0 = up
        1 << 4 | 1 << 7,  // 1 = up, right
        1 << 7,           // 2 = right
        1 << 7 | 1 << 5,  // 3 = right, down
        1 << 5,           // 4 = down
        1 << 5 | 1 << 6,  // 5 = down, left
        1 << 6,           // 6 = left
        1 << 6 | 1 << 4,  // 7 = left, up
      };
      uint8 angle = (uint8)(int)(ApproximateAtan2(last_y, last_x) * 64.0f + 0.5f);
      buttons = kSegmentToButtons[(uint8)(angle + 16 + 64) >> 5];
    }
    g_gamepad_buttons = buttons;
  } else if ((axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT || axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT)) {
    if (value < 12000 || value >= 16000)  // hysteresis
      HandleGamepadInput(axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT ? kGamepadBtn_L2 : kGamepadBtn_R2, value >= 12000);
  }
}

static bool LoadRom(const char *filename) {
  // Side-by-side RAM-compare mode is a dev feature; failing to load the
  // ROM here MUST NOT block the game from starting. The C reimplementation
  // runs standalone from zelda3_assets.dat. Warn to stderr and continue
  // without the emulated comparison if the ROM can't be read or isn't a
  // valid SNES image.
  size_t length = 0;
  uint8 *file = ReadWholeFile(filename, &length);
  if (!file) {
    fprintf(stderr, "Side-by-side ROM '%s' unreadable; continuing without\n"
                    "RAM-compare mode. The C reimplementation runs from\n"
                    "zelda3_assets.dat.\n", filename);
    return false;
  }
  bool result = EmuInitialize(file, length);
  free(file);
  if (!result) {
    fprintf(stderr, "Side-by-side ROM '%s' failed EmuInitialize (not a\n"
                    "valid SNES image?); continuing without RAM-compare.\n",
                    filename);
  }
  return result;
}

static bool ParseLinkGraphics(uint8 *file, size_t length) {
  if (length < 27 || memcmp(file, "ZSPR", 4) != 0)
    return false;
  uint32 pixel_offs = DWORD(file[9]);
  uint32 pixel_length = WORD(file[13]);
  uint32 palette_offs = DWORD(file[15]);
  uint32 palette_length = WORD(file[19]);
  if ((uint64)pixel_offs + pixel_length > length ||
      (uint64)palette_offs + palette_length > length ||
      pixel_length != 0x7000)
    return false;
  if (kPalette_ArmorAndGloves_SIZE != 150 || kLinkGraphics_SIZE != 0x7000)
    Die("ParseLinkGraphics: Invalid asset sizes");
  memcpy(kLinkGraphics, file + pixel_offs, 0x7000);
  if (palette_length >= 120)
    memcpy(kPalette_ArmorAndGloves, file + palette_offs, 120);
  if (palette_length >= 124)
    memcpy(kGlovesColor, file + palette_offs + 120, 4);
  return true;
}

static void LoadLinkGraphics() {
  if (g_config.link_graphics) {
    fprintf(stderr, "Loading Link Graphics: %s\n", g_config.link_graphics);
    size_t length = 0;
    uint8 *file = ReadWholeFile(g_config.link_graphics, &length);
    if (file == NULL || !ParseLinkGraphics(file, length))
      Die("Unable to load file");
    free(file);
  }
}


const uint8 *g_asset_ptrs[kNumberOfAssets];
uint32 g_asset_sizes[kNumberOfAssets];

static void LoadAssets() {
  size_t length = 0;
  uint8 *data = ReadWholeFile("zelda3_assets.dat", &length);
  if (!data) {
    size_t bps_length, bps_src_length;
    uint8 *bps, *bps_src;
    bps = ReadWholeFile("zelda3_assets.bps", &bps_length);
    if (!bps)
      Die("Failed to read zelda3_assets.dat. Please see the README for information about how you get this file.");
    bps_src = ReadWholeFile("zelda3.sfc", &bps_src_length);
    if (!bps_src)
      Die("Missing file: zelda3.sfc");
    data = ApplyBps(bps_src, bps_src_length, bps, bps_length, &length);
    if (!data)
      Die("Unable to apply zelda3_assets.bps. Please make sure you got the right version of 'zelda3.sfc'");
  }

  static const char kAssetsSig[] = { kAssets_Sig };

  if (length < 16 + 32 + 32 + 8 + kNumberOfAssets * 4 ||
      memcmp(data, kAssetsSig, 48) != 0 ||
      *(uint32*)(data + 80) != kNumberOfAssets)
    Die("Invalid assets file");

  uint32 offset = 88 + kNumberOfAssets * 4 + *(uint32 *)(data + 84);

  for (size_t i = 0; i < kNumberOfAssets; i++) {
    uint32 size = *(uint32 *)(data + 88 + i * 4);
    offset = (offset + 3) & ~3;
    if ((uint64)offset + size > length)
      Die("Assets file corruption");
    g_asset_sizes[i] = size;
    g_asset_ptrs[i] = data + offset;
    offset += size;
  }

  if (g_config.features0 & kFeatures0_DimFlashes) { // patch dungeon floor palettes
    kPalette_DungBgMain[0x484] = 0x70;
    kPalette_DungBgMain[0x485] = 0x95;
    kPalette_DungBgMain[0x486] = 0x57;
  }

  // Compute SHA-256 of the entire asset blob for the randomizer's
  // vanilla-asset comparison (tasks.md §1.1a). Hashed once; the
  // value lives in g_assets_hash and is compared against
  // kVanillaAssetsHash (generated by assets/restool.py per §1.1b)
  // before any randomizer slot is allowed to dispatch.
  //
  // NOTE: `data` and `length` are the raw blob — header, asset offset
  // table, and all asset bytes — exactly what the codegen pipeline
  // hashes too, guaranteeing parity.
  sha256_buffer(data, length, g_assets_hash);
}

// Go some steps up and find zelda3.ini
static void SwitchDirectory() {
  // Walk up the cwd looking for zelda3.ini so the game finds its assets
  // when the user launches from a build subdirectory (e.g., bin/x64-Release/
  // via a double-click). Walking 6 levels covers deeper build layouts
  // (e.g., bin/x64-Release/ + cmake-style out-of-source trees).
  char buf[4096];
  if (!getcwd(buf, sizeof(buf) - 32))
    return;
  size_t pos = strlen(buf);

  for (int step = 0; pos != 0 && step < 6; step++) {
    memcpy(buf + pos, "/zelda3.ini", 12);
    FILE *f = fopen(buf, "rb");
    if (f) {
      fclose(f);
      buf[pos] = 0;
      if (step != 0) {
        printf("Found zelda3.ini in %s\n", buf);
        int err = chdir(buf);
        (void)err;
      }
      return;
    }
    pos--;
    while (pos != 0 && buf[pos] != '/' && buf[pos] != '\\')
      pos--;
  }
}

MemBlk FindInAssetArray(int asset, int idx) {
  return FindIndexInMemblk((MemBlk) { g_asset_ptrs[asset], g_asset_sizes[asset] }, idx);
}
