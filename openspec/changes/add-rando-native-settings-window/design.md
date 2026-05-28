## Context

The randomizer's Phase A settings UI lives on the SNES screen: `src/select_file.c` handles the kind-toggle entry, `src/rando/rando.c` owns the settings-screen module, and `src/rando/rando_textfield.{c,h}` implements a from-scratch text-input layer fed by SDL_TEXTINPUT events on PC and (via `add-rando-switch-swkbd`) libnx swkbd on Switch. Settings live in a `RandoSettings` struct (`src/rando/rando_settings.h`) serialized through `Settings_CanonicalSerialize` to a 24-byte canonical layout that feeds `Settings_ComputeHash` to produce the `settings_hash` that anchors every seed's identity.

That UI works, but it's expensive to extend (every new field needs tile rendering + cursor navigation + translation handling) and underserves PC players who have mouse, keyboard, and pixel-precise widgets. Phase B will add ~10 more axes; doing them in 256×224 amplifies the cost. Meanwhile, the existing tooling (`Settings_ApplyPreset`, `Settings_ParseCsv`, `Settings_CanonicalSerialize`, the share-string encoder) is already platform-agnostic — only the *presentation* needs to move.

This change introduces a second OS-native window built with Dear ImGui that owns the settings surface on PC builds. It does not change seed generation, save format, snapshot format, or RAM behavior; it changes only the input path that fills `RandoSettings` and triggers `Settings_ComputeHash` + the generation pipeline.

## Goals / Non-Goals

**Goals:**

- Replace the in-game settings screen, on-screen alphabet picker, and text-input layer on PC builds (Windows / Linux / macOS) with a Dear ImGui window that opens at app startup and stays open alongside the game window.
- Render every Phase A `RandoSettings` axis (per `src/rando/rando_settings.h`) as a proper widget — dropdowns for enums, integer fields for crystal counts and piece counts, checkboxes for booleans — with live `settings_hash` and live share-string display.
- Provide a preset gallery built on the existing `Settings_ApplyPreset` API, share-string paste/copy via the OS clipboard, asset-hash warning acknowledgement, and a read-only spoiler/placement viewer tab.
- Keep seed generation deterministic and identical to today: same `RandoSettings` → same canonical bytes → same `settings_hash` → same placement, whether the UI was native or in-game.
- Preserve the Switch build path completely: all removed-on-PC files compile and run unchanged on Switch via a compile-time guard.
- Preserve every non-settings UI surface in-game on all platforms: file-select, kind-toggle, 5-icon hash banner, pause-menu hash, recommended-features panel.

**Non-Goals:**

- Multi-window for the game itself — only the settings UI gets a second window; the game window's renderer pipeline is untouched.
- Background seed generation — Phase A runs generation synchronously on the game thread when triggered from the native window; an ImGui modal blocks UI input during generation. Worker-thread generation is a possible Phase B optimization but explicitly out of scope here.
- Removing the existing file-select slot kind-toggle, slot banner, pause-menu hash, or recommended-features panel — those stay in-game everywhere.
- Switch native window — Switch has no second-window concept; the existing in-game UI plus `add-rando-switch-swkbd` is the Switch story and is untouched.
- Theming, custom skins, or font/DPI configuration beyond ImGui defaults plus a single dark/light toggle. Cosmetic polish is Phase B.
- Live spoiler editing — the spoiler viewer tab is read-only.

## Decisions

### D1. Two SDL windows, one main thread, one event pump

The existing main loop in `src/main.c` will create a second `SDL_Window` (and corresponding `SDL_GLContext` if using OpenGL backend) for the ImGui surface. The same `SDL_PollEvent` loop pumps both windows; events are routed by `event.window.windowID`: game-targeted events (keyboard while game window is focused, gamepad, controller) reach the existing game-input path unchanged; settings-window-targeted events go to `ImGui_ImplSDL2_ProcessEvent`. The game frame runs at the existing cadence; the ImGui frame is built and submitted once per game frame (so the UI naturally runs at the game's framerate without separate timing).

**OpenGL context decoupling — critical detail:** The game's default renderer is SDL software, NOT OpenGL (`src/opengl.c:38-40` confirms the game's GL context is created against the game window only when the user opted into the OpenGL renderer via `g_config.output_method`). For most users, the game window has no GL context at all. The settings window's GL context is therefore **always created as a separate, settings-window-only context**. The implementation MUST:

1. Create the settings window with `SDL_WINDOW_OPENGL`, create a dedicated `SDL_GLContext` against it, store both handles.
2. Before any ImGui render call (`ImGui_ImplOpenGL3_NewFrame`, `ImGui_ImplOpenGL3_RenderDrawData`, `SDL_GL_SwapWindow(settings_window)`), call `SDL_GL_MakeCurrent(settings_window, settings_gl_context)`.
3. After the ImGui swap, if the game renderer is the optional OpenGL path, call `SDL_GL_MakeCurrent(game_window, game_gl_context)` to restore — otherwise the game's next frame would draw against the wrong (or no) context.
4. Never create the settings GL context against the game window — that would alter the game window's pixel format and break the software/SDL-hardware renderer paths.

Mis-ordering these is a class of "game window goes black after first ImGui frame" bugs that's hard to diagnose; the implementation tasks enforce the order explicitly.

**Alternatives considered:**

- **Separate thread for ImGui** — rejected. Adds thread-safety burden on the bridge (mutexes around `RandoSettings`, snapshot of game state for the viewer tab). ImGui's frame cost is ~0.5–2ms; running it on the main thread is invisible at 60 fps.
- **Embedded ImGui inside the game window** — rejected. Defeats the "proper OS native window" requirement; the user explicitly asked for a second window they can resize, move, and minimize independently.

### D2. Dear ImGui vendored in `third_party/imgui/` with the SDL2 + OpenGL backends

ImGui ships as ~10 C++ source files plus 2 backend files. Vendoring matches the existing pattern (`third_party/SDL2-2.26.3/`, `third_party/gl_core/`, `third_party/opus-1.3.1-stripped/`). The Makefile already mixes C and C++ for nothing today, but adding C++ is trivial via the existing toolchain — `g++` / `clang++` on PC; MSBuild already accepts C++ via the `.vcxproj`. Switch is excluded from ImGui sources entirely via Makefile branching on `PLATFORM`.

**Version pin:** Dear ImGui v1.91.x (latest stable as of 2026-05). Vendored as a tarball at a pinned commit; no submodule.

**Renderer backend:** OpenGL3 backend (matches the existing optional OpenGL renderer the game uses via `src/opengl.c`). The settings window always uses OpenGL even when the game window is configured for the software/SDL-hardware renderer — the two windows are decoupled. Total OpenGL footprint stays the same on PC since `third_party/gl_core` is already linked.

**Alternatives considered:**

- **ImGui as a system package** — rejected. CI matrix is `make` on Linux/macOS with no package install; vendoring keeps that contract.
- **Nuklear** (pure C, header-only) — considered. Cheaper to integrate (no C++) but the widget library and community examples are an order of magnitude smaller. ImGui's mature widget set (combos with search, tables, drag-and-drop) directly serves features we want.

### D3. Single bridge struct owns pending settings + generation trigger

A new `src/rando/rando_window_bridge.{c,h}` exposes:

```c
typedef struct RandoWindowBridge {
  RandoSettings pending;          // Last value written by the UI
  uint8 pending_hash[32];         // Cached SHA-256 of canonical(pending)
  char share_string[64];          // Cached encoded share string
  uint64 seed_u64;                // Random or user-provided
  bool generate_requested;        // UI sets true; game side consumes
  bool generate_in_progress;      // Game side sets true while running
  int generate_status;            // 0=idle, 1=running, 2=success, -1=error
  char generate_error[256];       // Populated on -1
  // ... asset-hash decisions, slot-target index, etc.
} RandoWindowBridge;

extern RandoWindowBridge g_rando_window_bridge;
```

Both sides run on the main thread, so no mutex. The UI side mutates `pending`/`seed_u64`/`generate_requested`; the game side consumes `generate_requested` at frame boundary, runs generation synchronously (blocking the game frame, which is acceptable for a 100-500ms one-off), and writes the result back into the existing `g_rando` state plus the bridge's status field.

**Alternatives considered:**

- **Bridge as message queue** — rejected. Adds queue plumbing for no benefit; settings UI is request/response, not event-stream.
- **Direct UI writes into `g_rando.settings`** — rejected. Game logic reads `g_rando.settings` during frames; live edits would mid-frame change generation inputs. The `pending` copy keeps generation atomic.

### D4. Persistence via new `[rando_window]` section in `zelda3.ini`

`config.c` already parses `zelda3.ini`. A new `[rando_window]` section persists:

- `last_settings_canonical_b64` — base64 of the canonical bytes (`kSettingsCanonicalLen`, currently 28 per `src/rando/rando_settings.h:124`) (the exact bytes that feed `Settings_ComputeHash`). This is the source of truth for "open the window preselected to last config"; no separate format avoids parallel-format drift.
- `last_seed_u64` — for "regenerate last seed" convenience.
- `window_x`, `window_y`, `window_w`, `window_h` — last window geometry.
- `dark_theme` — bool, ImGui style toggle.

Asset-hash "Always allow" decisions explicitly do **not** live in this section. They use the existing `[RandoAssetDecisions]` section (parser at `src/config.c:535-552`, in-memory registration via `Rando_RegisterAssetDecisionFromIni`, in-game state in `src/select_file.c:2229-2245`). The native window reads and writes through that same lookup. Duplicating the storage into `[rando_window]` would create two sources of truth and inevitably drift.

**Alternatives considered:**

- **Separate `settings_window.json`** — rejected. One more file for backup/portability; the INI parser already exists.
- **Persisting individual fields** — rejected. Canonical-bytes round-trip is the integrity-checked path that already exists; reusing it eliminates a class of "field added to struct but forgot to persist" bugs.

### D9. Build the INI writer (prerequisite for any persistence)

`config.c` is read-only today — the in-game settings screen explicitly defers persistence with the note "Phase A: persistence is in-memory only. config.c's HandleIniConfig already accepts the new [RandoAssetDecisions] section so future INI write-back can persist…" (`src/select_file.c:2256-2270`). Every persistence task in this change presumes a writer exists; the writer does not exist.

This change SHALL ship a single new function `Config_WriteIniFile(const char *path)` (or equivalent) that writes the current in-memory config back to `zelda3.ini`, preserving the user's hand-edited keymaps and section ordering. The writer is a hard prerequisite — without it, both `[rando_window]` and the existing `[RandoAssetDecisions]` lookup would lose state on every shutdown, and the settings-screen comment's promise of "future INI write-back" finally lands.

**Preservation rule:** Users edit `zelda3.ini` by hand for keymaps. The writer SHALL NOT round-trip the file as a regenerated canonical form (which would strip comments, reorder sections, and rewrite keymap formatting). It SHALL only mutate the keys we own: `[rando_window].*` and `[RandoAssetDecisions].*`. Implementation strategy: read the existing file as text, identify the two sections by header, replace only those sections in place, write back atomically (write to `zelda3.ini.tmp`, fsync, rename). Unknown sections and all comments are preserved verbatim.

**Cross-platform atomic-rename:** POSIX `fsync` + `rename` is not portable to Windows. Use `#ifdef _WIN32` to branch:
- POSIX: `fsync(fileno(f))` + `rename(tmp, dst)`.
- Win32: `_commit(_fileno(f))` (the MSVCRT analogue of fsync) + `MoveFileExA(tmp, dst, MOVEFILE_REPLACE_EXISTING)` (because Win32 `rename` is non-atomic and fails when target exists).

The project already uses `#ifdef _WIN32` in `main.c:1028` for similar guards; reuse that pattern.

**Encoding for the canonical bytes:** the persisted `last_settings_canonical_b64` key is named for base64, but the codebase has **no base64 helper** today (verified via grep — zero matches for `base64|Base64|b64`). Adding one is a small dep tax with no other consumers. Hex is the better choice: 56 chars for 28 canonical bytes, fits comfortably on one INI line, and the project already has hex encoders/decoders in `main.c:927` and `config.c`'s `ParseHashHex` for `[RandoAssetDecisions]`. Decision: persist as hex under the key `last_settings_canonical_hex`. The base64 name in earlier task drafts is replaced by hex throughout.

**Alternatives considered:**

- **Sidecar JSON file (`saves/rando_window.json` or `zelda3_settings.json`)** — viable fallback if the in-place INI writer proves harder than expected. Reduces blast radius (no risk of mangling the user's keymaps), at the cost of one more file to back up and a parallel format. Decision: try the INI writer first; if implementation reveals unacceptable risk of clobbering user content, switch to the sidecar JSON before the asset-hash piece lands. Track the decision in tasks §16.

### D10. Extract a callable `Rando_GenerateForBridge()` from `main.c`'s headless pipeline

Today, "generate a seed" is **not** a single function call. The headless CLI path at `src/main.c:392-608` is an inline orchestration: `Settings_SetDefaults` → optional `Settings_ParseCsv` → `--race-mode` override → seed parse → `BuildItemPool` (implicit in `Place_AssumedFill`) → `Place_AssumedFill` → optional `BossShuffle_Generate` / `DropShuffle_Generate` / `Rando_GenerateHints` → `Settings_HashShort` → `Share_Encode` → `Logic_ComputeSpheres` → `Goal_IsCompletable` → `Spoiler_Write` → sidecar slot write → `Placement_Install`.

The native window's "Generate & start new slot" button needs every one of these steps to run in order on the game thread. Calling each individually from the bridge would duplicate ~200 lines and inevitably drift from the CLI path (different goal-completability gating, different fallback handling, different spoiler-write behavior). The native window's determinism guarantee depends on this not happening.

**Decision:** extract the orchestration into a new function

```c
// rando/rando_generate.{c,h} (or appended to rando.c if the file is small)
typedef struct RandoGenerateResult {
  bool ok;                  // false on failure
  bool used_forward_fill;   // assumed-fill exhausted budget
  bool goal_completable;
  char share_string[kShareStringBase32MaxLen];
  uint8 settings_hash[32];
  // ... any other fields the caller needs to write the sidecar slot
} RandoGenerateResult;

// Runs the full Phase A1 generation pipeline. Owns no global state.
// On success, the placement is installed via Placement_Install before returning.
// On failure, returns ok=false with `err` populated; no globals mutated.
bool Rando_GenerateForBridge(const RandoSettings *settings,
                             uint64 seed_u64,
                             int budget_seconds,
                             RandoGenerateResult *out,
                             char *err, size_t err_cap);
```

Step 1 of the implementation: refactor `main.c:392-608` to call `Rando_GenerateForBridge` internally and assert byte-for-byte identical CLI output. **No new generation behavior is introduced — only relocation.** The CLI path becomes the first caller; the bridge becomes the second. The regression corpus catches any drift.

Without this extraction, tasks §12.3-§12.7 are not implementable in a Phase A timeframe. With it, the bridge becomes a 50-line consumer.

### D11. Headless CLI modes must not initialize the settings window

`src/main.c` runs `MaybeRunGenerateSeedAndExit`, `MaybeRunBenchLogicAndExit`, `MaybeRunPrintAssetsHashAndExit`, `MaybeRunSelftestAndExit` etc. BEFORE `SDL_Init(SDL_INIT_VIDEO|…)`. These call `exit(0)` and never reach window-creation code. The settings-window init (`RandoWindow_Init`) MUST live in the `Z3R_NATIVE_SETTINGS_WINDOW` guard AND be placed after the headless `Maybe*` checks, NOT at the top of `main()`. Otherwise every headless CLI invocation (CI seed-gen, batch corpus regeneration, race-admin tooling) tries to open a settings window and produces an SDL error or, worse, a modal dialog the runner can't dismiss. Tasks §3.1 and §4.6 enforce the placement explicitly.

### D5. Compile-time guard: `Z3R_NATIVE_SETTINGS_WINDOW`

PC builds (`make`, MSBuild) define `Z3R_NATIVE_SETTINGS_WINDOW=1`. Switch build does not. The guard wraps:

- `src/rando/rando_textfield.{c,h}` — entire file. Switch keeps it.
- The settings-screen branch of `src/rando/rando.c` (`Rando_OpenSettingsScreen`, the alphabet-picker render path, the in-game settings tile rendering). PC builds compile out those functions; the kind-toggle path in `select_file.c` instead calls `RandoWindow_OpenForNewSlot()`.
- The Switch build's existing `add-rando-switch-swkbd` integration stays in force on Switch and is no-op'd on PC (its files are gated by the existing Switch `PLATFORM` check, not by the new guard).

**Alternatives considered:**

- **Runtime flag in `zelda3.ini`** — rejected. Forces both UIs to be compiled even on PC, doubling maintenance for a setting nobody asked for. The use case "PC user wants the in-game UI" was not in the requirements.
- **Platform check (`__SWITCH__`)** — rejected. The guard is conceptually "is there a native settings window?", not "is this Switch?". A future platform without an OS window (e.g., a console port) should set the same flag.

### D6. Generation runs synchronously on game thread; ImGui shows a blocking modal

When the UI clicks "Generate & start new slot":

1. UI thread writes `bridge.generate_requested = true`, opens an ImGui modal "Generating seed..." (input-blocking, no spinner animation required — modal alone is enough feedback).
2. Game thread, at the next `ZeldaRunFrame()` boundary, checks `bridge.generate_requested`, clears it, sets `bridge.generate_in_progress = true`, and calls a NEW callable function `Rando_GenerateForBridge(const RandoSettings*, uint64 seed, int slot_index, char *err, size_t cap)` synchronously. **This function does not exist today** — generation is currently a ~200-line inline orchestration in `main.c:392-608` (`Place_AssumedFill` + `BossShuffle_Generate` + `DropShuffle_Generate` + `Settings_HashShort` + `Share_Encode` + `Logic_ComputeSpheres` + `Goal_IsCompletable` + `Spoiler_Write` + sidecar write). Extracting this orchestration into a callable function is a hard prerequisite for the bridge; see tasks §11.5–§11.9. The game window freezes for the duration (100-500ms typical, up to ~2s on hard pool with deep retry).
3. On completion: game thread writes `bridge.generate_status` and either commits the generated placement via `Placement_Install` + writes the new sidecar slot, or surfaces the error.
4. UI thread, on next ImGui frame, sees the updated status, closes the modal, and either advances the user to "Slot ready — load now?" or displays the error.

**Alternatives considered:**

- **Worker thread for generation** — deferred to Phase B. Adds thread-safety burden on `g_rando` and the asset blob; benefit is only "game animates during the half-second generation runs", which is not a Phase A goal.

### D7. Switch keeps the in-game UI; the native window is silently absent

Switch builds skip the `third_party/imgui/` sources entirely (Makefile branch on `PLATFORM=switch`). The settings-screen module, alphabet picker, and `rando_textfield` compile in as today. `add-rando-switch-swkbd` continues to provide swkbd integration. From the Switch user's perspective: nothing changes.

### D8. Spoiler viewer is read-only, suppressed for race-mode slots

The viewer tab in the ImGui window reads the in-memory placement table via the existing `Placement_GetActive()` accessor (`src/rando/rando_placement.h:60` — returns the `const RandoPlacementTable *` installed by `Placement_Install` after generation completes) and the spoiler JSON produced by `rando_spoiler.c`. Race-mode hide-gate: the bridge stores the `race_mode` value used at the LAST successful generation (`bridge.last_generated_race_mode`, added in §2.1). It is NOT `bridge.pending.race_mode` (which reflects what the user is editing right now, not what's loaded) and NOT the active save slot's `RandoSettings.race_mode` (correct but coupling the viewer to the active-slot state machine is extra work for no benefit). The viewer cannot edit; if the user wants to change placement, they must Generate again.

## Risks / Trade-offs

- [C++ in the codebase via ImGui] → ImGui is one of the most-vendored libraries in C-centric game/tool codebases (e.g., Dolphin, RPCS3, every roguelike with debug tooling). The C++ surface is contained in `third_party/imgui/` plus one `.cpp` wrapper in `src/rando/rando_window/`; the rest of the project stays C. Mitigation: keep the wrapper as thin as possible — every game-side function is a C function called from the wrapper.
- [Two SDL windows = double the surface for input bugs] → Event routing by `event.window.windowID` is well-trodden ground (most SDL apps with debug overlays use it). Mitigation: explicit unit-style test — open both windows, focus each, confirm input goes to the right path; documented in tasks.md.
- [Settings persistence in `zelda3.ini` could conflict with manual edits] → Users do edit `zelda3.ini` by hand for keymaps. Mitigation: `[rando_window]` section is clearly labeled "managed by the settings window; do not edit", and the canonical-bytes round-trip is base64-checked on load with a clear error on corruption (falls back to defaults, doesn't crash).
- [Synchronous generation freezes the game window briefly] → Acceptable for Phase A (it's a deliberate user action, not background). Mitigation: ImGui modal makes the wait obvious; if generation typically exceeds ~1.5s on real hardware after profiling, escalate to worker-thread in a Phase B follow-up change.
- [ImGui binary size increase] → ~200 KB stripped. Negligible against the existing ~3-5 MB binary plus the ~50 MB asset blob.
- [Window geometry persistence may produce off-screen window on multi-monitor changes] → Standard problem with persisted window positions. Mitigation: on load, clamp the persisted x/y/w/h to the union of currently-attached display bounds; if invalid, recenter on the primary display.
- [Coordinating "the player has an in-progress save and changes settings"] → The bridge only writes to `pending`; the active slot's settings are untouched until the user clicks "Generate & start new slot", which always creates a fresh slot (never overwrites the active one without explicit confirmation). The UI surfaces a "your current slot's settings:" read-only display so the user knows the distinction.
- [Audit-guard impact] → None. The audit guard scans `src/*.c` writes to `g_ram` cells; ImGui code lives outside `src/` (it's in `third_party/imgui/` and `src/rando/rando_window/`, which contains no `g_ram` writes). Confirmed against `audit_guard_indirect_writes.md` memory note.
- [Audio thread reads game state during sync generation] → The SDL audio callback (`AudioCallback` in `src/main.c`) runs on its own thread and consumes from `g_audiobuffer` under `g_audio_mutex`. While generation freezes the main loop for 100-500ms, the audio thread keeps pulling. If the generation pipeline mutates anything the audio thread reads (lazily-loaded SPC samples in `assets.h`, the SPC player's state in `spc_player.c`), a latent race exists. Mitigation: a verification task (tasks §18.4) greps the generation call tree for any touch of audio-shared state; if any is found, either bracket generation with `SDL_LockMutex(g_audio_mutex)` or relocate the offending lazy-load to game-init time. Expected outcome on inspection: generation never touches audio state (it touches placement tables, the asset blob's chest/NPC tables, and SHA-256 buffers — all UI-thread-only).
- [Switch Makefile glob picks up new sources unconditionally] → `src/platform/switch/Makefile:43,59` globs `$(SRC_DIR)/rando/*.c` wholesale. Any new `.c` placed under `src/rando/` lands in the Switch build regardless of the compile guard, and an `#ifdef`-empty `.cpp` would break the Switch toolchain (no g++ rule in that Makefile). Mitigation: new ImGui-side sources live under `src/rando/rando_window/` (a subdirectory the glob does NOT recurse into), and the bridge `.c` is under that same subdirectory so it is also excluded by default. Verified explicitly in tasks §1.6 and §3.6.
- [In-game settings screen state lives in `select_file.c`, not only `rando.c`] → The in-game settings module is split: `g_settings_*` state lives at `src/select_file.c:2196-2226`, `g_alphabet_textfield` and related text-input state are elsewhere in the same file, and the screen dispatch is wired from `select_file.c` flow control. Wrapping only `rando.c` in the compile guard would leave unresolved references at link time on PC. Mitigation: tasks §15.1 enumerates every site that needs the guard, not just `rando.c`.

## Migration Plan

This change is additive on PC (new files + new build flag) and a no-op on Switch. Migration steps:

1. Vendor ImGui under `third_party/imgui/` at the pinned commit. Add to PC build only.
2. Land the bridge + window skeleton with empty panels; verify two-window event routing works.
3. Implement each settings panel (general / world-state / dungeons / shuffles / asset-hash / spoiler viewer) backed by the existing `RandoSettings` and `Settings_*` APIs.
4. Wire the kind-toggle in `select_file.c` to `RandoWindow_OpenForNewSlot()` under `Z3R_NATIVE_SETTINGS_WINDOW`; the in-game settings-screen entry on PC becomes unreachable.
5. Add `Z3R_NATIVE_SETTINGS_WINDOW` to the Makefile and `.vcxproj`. Confirm Switch Makefile does NOT define it and the build still passes.
6. Compile-out `rando_textfield.{c,h}` and the in-game settings screen on PC; Switch unchanged.
7. Add `[rando_window]` persistence to `config.c`.
8. Verify a full round-trip on PC: settings entered in native window → Generate → game loads slot → save/load → re-open → settings preselected to last value.
9. Verify Switch build: Switch settings-screen UI still functional; native window code is excluded.

**Rollback:** the `Z3R_NATIVE_SETTINGS_WINDOW` guard makes rollback a one-define change: undefine it, the in-game UI compiles back in. The bridge file becomes dead code (no callers) but doesn't break anything. Persistence section in `zelda3.ini` is ignored.

## Open Questions

- Which dark-theme palette ships as default — ImGui's "Dark" preset, or a custom palette tuned to look more game-themed? **Proposed answer:** ship ImGui's "Dark" preset as default; defer cosmetic theming to Phase B.
- Should the native window be hide-able while the game runs (e.g., a "Hide window" hotkey)? **Proposed answer:** yes — close-button minimizes to a state where it can be re-opened from a `File → Settings Window` menu in the game window's title bar (or a config-bound hotkey). Spec the close behavior in tasks.md.
- Should the spoiler viewer also expose the regression-corpus comparison feature (diff against an expected spoiler JSON)? **Proposed answer:** no for Phase A — that's a developer tool, not a player tool. Phase B may add a "Developer" tab.
