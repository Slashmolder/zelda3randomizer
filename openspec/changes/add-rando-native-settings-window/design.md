## Context

The randomizer's Phase A settings UI lives on the SNES screen: `src/select_file.c` handles the kind-toggle entry, `src/rando/rando.c` owns the settings-screen module, and `src/rando/rando_textfield.{c,h}` implements a from-scratch text-input layer fed by SDL_TEXTINPUT events on PC and (via `add-rando-switch-swkbd`) libnx swkbd on Switch. Settings live in a `RandoSettings` struct (`src/rando/rando_settings.h`) serialized through `Settings_CanonicalSerialize` to a `kSettingsCanonicalLen`-byte canonical layout (28 bytes) that feeds `Settings_ComputeHash` to produce the `settings_hash` that anchors every seed's identity.

That UI works, but it's expensive to extend (every new field needs tile rendering + cursor navigation + translation handling) and underserves PC players who have mouse, keyboard, and pixel-precise widgets. Phase B will add ~10 more axes; doing them in 256×224 amplifies the cost. Meanwhile, the existing tooling (`Settings_ApplyPreset`, `Settings_ParseCsv`, `Settings_CanonicalSerialize`, the share-string encoder) is already platform-agnostic — only the *presentation* needs to move.

This change introduces a second OS-native window built with Dear ImGui that owns the settings surface on PC builds. It does not change seed generation, save format, snapshot format, or RAM behavior; it changes only the input path that fills `RandoSettings` and triggers `Settings_ComputeHash` + the generation pipeline.

## Goals / Non-Goals

**Goals:**

- On PC builds (Windows / Linux / macOS), compile-guard out the in-game settings screen, on-screen alphabet picker, and text-input layer and route their responsibility to a Dear ImGui window that is **created hidden at startup** and **shown/raised when the player chooses "New Randomizer"** (re-openable via a hotkey / game menu). The in-game code stays compilable behind the guard.
- Render the randomizer settings surface **at parity with the in-game settings screen's axis set** (NOT "every `RandoSettings` field") as proper widgets — dropdowns for enums, integer fields for crystal counts and piece counts, checkboxes for booleans — with live `settings_hash` and live share-string display. Live axes mirror the in-game `kRow_*` rows; boss/drop/entrance/enemy/glitches shuffle render as disabled "coming soon" placeholders (matching the in-game `kRow_*_Disabled` rows).
- Provide a preset gallery built on the existing `Settings_ApplyPreset` API, share-string paste/copy via the OS clipboard, asset-hash warning acknowledgement, the recommended-features opt-in (moved into the window on PC), and a read-only spoiler/placement viewer tab.
- Keep seed generation deterministic and identical to today: same `RandoSettings` → same canonical bytes → same `settings_hash` → same placement, whether the UI was native or in-game. The shared `Rando_GenerateSlot` is a pure relocation of the in-game generate body; the CLI path is untouched and the corpus stays green with no `kGeneratorVersion` bump.
- Preserve the Switch build path completely: all guarded-out-on-PC files compile and run unchanged on Switch via a compile-time guard.
- Preserve every non-settings UI surface in-game on all platforms: file-select, kind-toggle, 5-icon hash banner, pause-menu hash. The recommended-features panel stays in-game on Switch but moves into the native window on PC.

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

ImGui ships as ~10 C++ source files plus 2 backend files. Vendoring matches the existing pattern (`third_party/SDL2-2.32.10/`, `third_party/gl_core/`, `third_party/opus-1.3.1-stripped/`). The Makefile already mixes C and C++ for nothing today, but adding C++ is trivial via the existing toolchain — `g++` / `clang++` on PC; MSBuild already accepts C++ via the `.vcxproj`. Switch is excluded from ImGui sources entirely via Makefile branching on `PLATFORM`.

**Version pin:** Dear ImGui v1.91.x (latest stable as of 2026-05). Vendored as a tarball at a pinned commit; no submodule.

**Renderer backend:** OpenGL3 backend (matches the existing optional OpenGL renderer the game uses via `src/opengl.c`). The settings window always uses OpenGL even when the game window is configured for the software/SDL-hardware renderer — the two windows are decoupled. Total OpenGL footprint stays the same on PC since `third_party/gl_core` is already linked.

**Alternatives considered:**

- **ImGui as a system package** — rejected. CI matrix is `make` on Linux/macOS with no package install; vendoring keeps that contract.
- **Nuklear** (pure C, header-only) — considered. Cheaper to integrate (no C++) but the widget library and community examples are an order of magnitude smaller. ImGui's mature widget set (combos with search, tables, drag-and-drop) directly serves features we want.

### D3. Single bridge struct owns pending settings + generation trigger

A new `src/rando/rando_window_bridge.{c,h}` exposes:

```c
typedef struct RandoWindowBridge {
  RandoSettings pending;                  // Last value written by the UI
  uint32 pending_recommended_features0;   // Snapshot of g_config.features0 at window-open; UI edits; game applies at generate
  uint8 pending_hash[32];                 // Cached SHA-256 of canonical(pending)
  char share_string[kShareStringBase32MaxLen];  // Cached encoded share string
  uint64 seed_u64;                        // UI-only entropy or user-provided
  int target_slot_index;                  // -1 = no kind-toggle target
  bool generate_requested;                // UI sets true; game side consumes
  bool generate_in_progress;              // Game side sets true while running
  int generate_status;                    // 0=idle, 1=running, 2=success, -1=error
  char generate_error[256];               // Populated on -1
  // Viewer snapshot (game writes on success; UI reads — NOT Placement_GetActive):
  bool last_generated_race_mode;
  RandoPlacementTable last_generated_placement;  // owned malloc'd copy; freed on next gen / shutdown
  RandoSpheres last_generated_spheres;
  bool has_last_generated;
} RandoWindowBridge;

extern RandoWindowBridge g_rando_window_bridge;
```

Both sides run on the main thread, so no mutex. The UI side mutates `pending`/`pending_recommended_features0`/`seed_u64`/`generate_requested`/`target_slot_index`; the game side consumes `generate_requested` at frame boundary, runs generation synchronously (blocking the game frame, which is acceptable for a 100-500ms one-off), applies `g_config.features0 = pending_recommended_features0` inside the generate consumer only, and writes the result (status + the owned `last_generated_*` viewer snapshot) back into the bridge. `pending_recommended_features0` is snapshotted from `g_config.features0` at window-open (inside `RandoWindow_OpenForNewSlot`, which runs on the game thread via the kind-toggle); the UI thread never writes `g_config.features0`.

**Alternatives considered:**

- **Bridge as message queue** — rejected. Adds queue plumbing for no benefit; settings UI is request/response, not event-stream.
- **Direct UI writes into `g_rando.settings`** — rejected. Game logic reads `g_rando.settings` during frames; live edits would mid-frame change generation inputs. The `pending` copy keeps generation atomic.

### D4. Persistence via a sidecar `saves/rando_window.ini`

Persistence ships as a **sidecar `saves/rando_window.ini`** (NOT an in-place rewrite of the user's `zelda3.ini`; see D9). It holds two sections:

`[rando_window]`:
- `last_settings_canonical_hex` — **hex** of the canonical bytes (`kSettingsCanonicalLen` = 28 per `src/rando/rando_settings.h`, → 56 hex chars; the exact bytes that feed `Settings_ComputeHash`). This is the source of truth for "open the window preselected to last config"; no separate format avoids parallel-format drift.
- `last_seed_u64` — for "regenerate last seed" convenience.
- `window_x`, `window_y`, `window_w`, `window_h` — last window geometry.
- `dark_theme` — bool, ImGui style toggle.

`[RandoAssetDecisions]`:
- Asset-hash "Always allow" decisions live here, NOT under `[rando_window]`. They use the existing `[RandoAssetDecisions]` parser (`src/config.c`, the asset-decision helpers promoted out of `src/select_file.c` into `rando_asset_decisions.c`, registered via `Rando_RegisterAssetDecisionFromIni` called from `config.c`). The native window reads and writes through that same lookup. Duplicating the storage into `[rando_window]` would create two sources of truth and inevitably drift. `zelda3.ini`'s `[RandoAssetDecisions]` is still parsed too (union), so hand-added entries still load.

The sidecar is loaded at startup via a new whitelisting `Config_LoadAuxIniFile` (only `[rando_window]` + `[RandoAssetDecisions]`; see D9).

**Alternatives considered:**

- **In-place rewrite of `zelda3.ini`** — deferred (D9). Sidecar eliminates the keymap-clobber risk class entirely for v1.
- **Persisting individual fields** — rejected. Canonical-bytes round-trip is the integrity-checked path that already exists; reusing it eliminates a class of "field added to struct but forgot to persist" bugs.
- **Base64 encoding** — rejected. No base64 helper exists in the codebase; hex is 56 chars for 28 bytes, fits one INI line, and reuses existing hex digit logic (see D9).

### D9. Ship a sidecar `saves/rando_window.ini`; defer the in-place INI writer

`config.c` is read-only today — the in-game settings screen explicitly defers persistence with the note "Phase A: persistence is in-memory only. config.c's HandleIniConfig already accepts the new [RandoAssetDecisions] section so future INI write-back can persist…" (in the asset-warn/persistence comment in `src/select_file.c`). Every persistence task in this change needs a way to read settings back at startup without clobbering the user's hand-edited `zelda3.ini`.

**Decision: v1 ships a sidecar.** Persistence writes a sidecar `saves/rando_window.ini` containing both `[rando_window]` and `[RandoAssetDecisions]`. This **never rewrites the user's `zelda3.ini`** — keymaps and comments are preserved by construction — eliminating the keymap-clobber risk class entirely. The in-place `Config_WriteIniFile` rewriter is **deferred** (documented in tasks §17.1a).

**Startup load — `Config_LoadAuxIniFile(const char *path)`:** read the sidecar into a temporary buffer, then parse it with a **section whitelist** and free the buffer. Critical correctness details (verified against `config.c`):
- `HandleIniConfig(section, key, value)` is the per-line *consumer* keyed by an int section id; the `[section]`→`GetIniSection`→`SplitKeyValue` state machine lives in the **caller** (`ParseOneConfigFile`), NOT inside `HandleIniConfig`. So `Config_LoadAuxIniFile` MUST replicate that section/key/value loop itself.
- `HandleIniConfig` stores raw `value` pointers into the source buffer for several foreign sections (e.g. `link_graphics`, `shader`, `rando_spoiler_dir`). Therefore `Config_LoadAuxIniFile` MUST **whitelist only the sections we own** — the new `[rando_window]` section + `[RandoAssetDecisions]` — and skip every other section entirely (do not pass foreign lines to `HandleIniConfig`). The only keys it consumes are hex/int/bool (copied out), never pointer-into-buffer values, so it is then safe to `free()` the temp buffer with no dangling `g_config.*` pointers.
- It MUST NOT call `ParseConfigFile`/`ParseOneConfigFile` — those repoint the long-lived `g_config.memory_buffer`, reset rando defaults, and re-`RegisterDefaultKeys()`.
- Precondition: add the `[rando_window]` entry to `GetIniSection` (which currently returns -1 for it) before the loader runs.
- `zelda3.ini`'s `[RandoAssetDecisions]` is still parsed too (union, so hand-added entries still load); `Rando_RegisterAssetDecisionFromIni` gains an in-place existence check (mirroring `AssetDecision_Persist`) so union-reading the same hash from both files does not insert duplicates into the `kAssetDecisions_Max` (16) cap.

**Encoding for the canonical bytes:** the codebase has **no base64 helper** today (verified via grep — zero matches for `base64|Base64|b64`). Hex is the choice: 56 chars for 28 canonical bytes, fits comfortably on one INI line. Use a small generic hex codec (28-byte canonical) — do NOT reuse `config.c`'s `ParseHashHex` (it is fixed 32-byte-only). Persist under the key `last_settings_canonical_hex`. The base64 name in earlier drafts is replaced by hex throughout.

**Deferred in-place writer (for reference, NOT shipped in v1):** an eventual `Config_WriteIniFile(const char *path)` would read `zelda3.ini` as text, replace only the `[rando_window]` and `[RandoAssetDecisions]` sections in place, and write back atomically (`#ifdef _WIN32`: `_commit(_fileno(f))` + `MoveFileExA(tmp, dst, MOVEFILE_REPLACE_EXISTING)`; POSIX: `fsync(fileno(f))` + `rename(tmp, dst)`), preserving comments and unknown sections verbatim. Out of scope for this change.

### D10. Extract a callable `Rando_GenerateSlot()` from the **in-game** generate flow

Today, "generate a playable randomizer slot" is **not** a single function call. There are two distinct generation paths and the bridge must mirror the **in-game playable-slot** one, NOT the CLI one:
- The headless **CLI path** (`MaybeRunGenerateSeedAndExit`, ~`main.c:393-616`) writes spoiler files and `exit()`s; it does NOT install a placement or write a randomizer slot. **It is untouched by this change.**
- The **in-game playable-slot flow** is `SelectFile_Settings_HandleGenerate` (~`select_file.c:3318`): an inline orchestration that produces a sidecar slot the player can load. This is what the native window must mirror.

The native window's "Generate & start new slot" button needs the in-game flow's steps to run in order on the game thread. Reimplementing them in the bridge would duplicate the body and inevitably drift. The native window's determinism guarantee depends on this not happening.

**Decision:** extract the body of `SelectFile_Settings_HandleGenerate` into a new UI-agnostic shared function that both the in-game screen and the native window call:

```c
// src/rando/rando_generate.{c,h}
typedef struct RandoGenerateResult {
  bool ok;                  // false on failure
  bool used_forward_fill;   // assumed-fill exhausted budget
  bool goal_completable;
  char share_string[kShareStringBase32MaxLen];
  uint8 settings_hash[16];
  bool race_mode;
  RandoPlacementTable placement;  // owned copy written only when out != NULL (caller frees)
  RandoSpheres spheres;
} RandoGenerateResult;

// Mirrors the IN-GAME generate flow exactly (pure relocation):
//   ComputeHash → Place_AssumedFill → Share_Encode/PackBinary → spheres → Goal_IsCompletable
//   → Spoiler_Write(files) → build RandoSidecarSlot → Rando_WriteSidecarSlot + ZeldaWriteSram
//   → apply recommended_features0 to g_config.features0 → SelectFile_NotifySlotWritten(slot)
// Does NOT Placement_Install (install is slot-load-only). Does NOT exit. Does NOT
// refactor the CLI path. Game thread only.
bool Rando_GenerateSlot(const RandoSettings *settings,
                        uint64 seed_u64,
                        int budget_seconds,
                        int slot_index,
                        uint32 recommended_features0,
                        RandoGenerateResult *out,
                        char *err, size_t err_cap);
```

`SelectFile_Settings_HandleGenerate` is refactored to call `Rando_GenerateSlot` (with `out == NULL`, so no copy is made), so in-game behavior is identical and provable by flipping the guard on Windows + diffing `sram_rando.dat`. The bridge consumer passes a non-NULL `out` to receive an independently malloc'd `out->placement` copy that it owns (freed on next successful generate and at shutdown).

**Two correctness invariants (verified):**
- `Rando_GenerateSlot` does **NOT** call `Placement_Install`. The in-game flow installs the placement only at slot *load* (`rando.c:738`); installing at generate would clobber an in-progress slot's active placement. The spoiler viewer therefore reads the **bridge's own owned copy** of the just-generated table (`bridge.last_generated_placement` + `bridge.last_generated_spheres`), never `Placement_GetActive()` (which is populated only at slot load).
- The pre-existing **in-game-vs-CLI divergence is preserved, not fixed**: the in-game generate flow does NOT run boss/drop shuffle inline (and `Rando_ActivateSidecarSlot` regenerates hints but not boss/drop), whereas the CLI path can. Reconciling that would force a `kGeneratorVersion` bump + corpus regen and is **explicitly out of scope**. This is a UI change: the CLI path is untouched, `kGeneratorVersion` is **not** bumped, and the regression corpus stays green as a pure regression guard.

`Rando_GenerateSlot` is **not** dependency-free: it depends on select_file internals (`SelectFile_ResetSidecarCache` + `selectfile_arr1[slot]` via a new non-static `SelectFile_NotifySlotWritten(slot)` seam) and rando-core. It contains **no window/ImGui code** ⇒ Switch-safe (lives in `src/rando/*.c`, intentionally compiled on Switch so the in-game screen calls it too).

### D11. Headless CLI modes must not initialize the settings window

`src/main.c` runs the headless `Maybe*AndExit` handlers — `MaybeRunGenerateSeedAndExit`, `MaybeRunBenchLogicAndExit`, `MaybeRunRevealSpoilerAndExit` (and the inline `--print-assets-hash` / `--rando-selftest` `return 0` handlers, which are NOT separate functions) — BEFORE `SDL_Init(SDL_INIT_VIDEO|…)` (`main.c:984`). These exit before reaching window-creation code. The settings-window init (`RandoWindow_Init`) MUST live in the `Z3R_NATIVE_SETTINGS_WINDOW` guard AND be placed after the headless checks and after `SDL_Init`, NOT at the top of `main()`. Otherwise every headless CLI invocation (CI seed-gen, batch corpus regeneration, race-admin tooling) tries to open a settings window and produces an SDL error or, worse, a modal dialog the runner can't dismiss. Tasks §3.1 and §4.6 enforce the placement explicitly.

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
2. Game thread, at the next `ZeldaRunFrame()` boundary, checks `bridge.generate_requested`, clears it, sets `bridge.generate_in_progress = true`, and calls the shared function `Rando_GenerateSlot(&bridge.pending, bridge.seed_u64, budget, bridge.target_slot_index, bridge.pending_recommended_features0, &result, err, cap)` synchronously. **This function is extracted from the in-game flow `SelectFile_Settings_HandleGenerate`** (NOT the CLI path); see tasks §12. The game window freezes for the duration (100-500ms typical, up to ~2s on hard pool with deep retry).
3. On completion: `Rando_GenerateSlot` has already written the sidecar slot (via `Rando_WriteSidecarSlot` + `SelectFile_NotifySlotWritten`) and applied `recommended_features0` to `g_config.features0`; it does **NOT** `Placement_Install` (install is slot-load-only). The game thread stores the owned `result.placement`/`result.spheres`/`result.race_mode` viewer snapshot into the bridge and writes `bridge.generate_status`, or surfaces the error.
4. UI thread, on next ImGui frame, sees the updated status, closes the modal, and either advances the user to "Slot ready — load now?" or displays the error.

**Alternatives considered:**

- **Worker thread for generation** — deferred to Phase B. Adds thread-safety burden on `g_rando` and the asset blob; benefit is only "game animates during the half-second generation runs", which is not a Phase A goal.

### D7. Switch keeps the in-game UI; the native window is silently absent

Switch builds skip the `third_party/imgui/` sources entirely (Makefile branch on `PLATFORM=switch`). The settings-screen module, alphabet picker, and `rando_textfield` compile in as today. `add-rando-switch-swkbd` continues to provide swkbd integration. From the Switch user's perspective: nothing changes.

### D8. Spoiler viewer is read-only, suppressed for race-mode slots

The viewer tab in the ImGui window reads **the bridge's own owned copy** of the just-generated placement table (`bridge.last_generated_placement` + `bridge.last_generated_spheres`), NOT `Placement_GetActive()`. `Placement_GetActive()` (`src/rando/rando_placement.h:60`) is populated only at slot *load* via `Placement_Install` — and `Rando_GenerateSlot` deliberately does NOT install at generate (install is slot-load-only) — so reading it would show stale or empty data right after a generate. Instead, `Rando_GenerateSlot` returns an owned `RandoPlacementTable` copy that the game thread stores in the bridge; the viewer renders from that. Race-mode hide-gate: the bridge stores the `race_mode` value used at the LAST successful generation (`bridge.last_generated_race_mode`). It is NOT `bridge.pending.race_mode` (which reflects what the user is editing right now, not what's loaded) and NOT the active save slot's `RandoSettings.race_mode` (correct but coupling the viewer to the active-slot state machine is extra work for no benefit). The viewer cannot edit; if the user wants to change placement, they must Generate again.

### D12. Recommended-features opt-in moves into the native window on PC

The recommended-features opt-in (the in-game `kSettingsView_Recommended` sub-view that writes `g_config.features0` at generate, mirroring the in-game logic around `select_file.c:3544-3545`) is **only reachable through the in-game settings screen**, which is guarded out on PC. So on PC the native window must render this opt-in itself. The flow is determinism-neutral: `features0` is not part of `settings_hash`. The UI snapshots the working features into `bridge.pending_recommended_features0` at window-open (read from `g_config.features0` on the game thread, inside `RandoWindow_OpenForNewSlot`); the UI thread edits only that snapshot; the **game thread** applies `g_config.features0 = pending_recommended_features0` inside the generate consumer (and `Rando_GenerateSlot` carries the `recommended_features0` argument). On Switch the in-game recommended-features panel stays.

### D13. UI-only random seed source

The native "New random seed" button derives `seed_u64` from a dedicated `SplitMix64(SDL_GetPerformanceCounter())`. There is **no** `Rando_RandomSeedU64` in the codebase (verified), and the CLI requires an explicit `--seed`. The button must **not** use `DeriveSeedFromState` (which mixes live `frame_counter`). The seed is a pure input to generation, so a UI-only entropy source is determinism-neutral.

### D14. Spoiler "save to clipboard" via temp-file round-trip

`Spoiler_Write(const RandoSpoiler*, json_path, txt_path)` is **file-only** — it cannot target a clipboard or memory buffer. The "Save spoiler to clipboard" button therefore writes the spoiler JSON to a temp file, reads it back, and feeds the text to `SDL_SetClipboardText`, rather than touching the spoiler writer. (A buffer variant is a possible alternative but is avoided to keep the writer unchanged.)

## Risks / Trade-offs

- [C++ in the codebase via ImGui] → ImGui is one of the most-vendored libraries in C-centric game/tool codebases (e.g., Dolphin, RPCS3, every roguelike with debug tooling). The C++ surface is contained in `third_party/imgui/` plus one `.cpp` wrapper in `src/rando/rando_window/`; the rest of the project stays C. Mitigation: keep the wrapper as thin as possible — every game-side function is a C function called from the wrapper.
- [Two SDL windows = double the surface for input bugs] → Event routing by `event.window.windowID` is well-trodden ground (most SDL apps with debug overlays use it). Mitigation: explicit unit-style test — open both windows, focus each, confirm input goes to the right path; documented in tasks.md.
- [Settings persistence could conflict with manual edits] → Users do edit `zelda3.ini` by hand for keymaps. Mitigation: v1 ships a **sidecar `saves/rando_window.ini`** (D9) so the user's `zelda3.ini` is never rewritten — keymaps/comments are preserved by construction. The canonical-bytes round-trip is hex-checked on load with a clear error on corruption (falls back to defaults, doesn't crash). Startup load goes through the whitelisting `Config_LoadAuxIniFile`, which only consumes `[rando_window]` + `[RandoAssetDecisions]`.
- [Synchronous generation freezes the game window briefly] → Acceptable for Phase A (it's a deliberate user action, not background). Mitigation: ImGui modal makes the wait obvious; if generation typically exceeds ~1.5s on real hardware after profiling, escalate to worker-thread in a Phase B follow-up change.
- [ImGui binary size increase] → ~200 KB stripped. Negligible against the existing ~3-5 MB binary plus the ~50 MB asset blob.
- [Window geometry persistence may produce off-screen window on multi-monitor changes] → Standard problem with persisted window positions. Mitigation: on load, clamp the persisted x/y/w/h to the union of currently-attached display bounds; if invalid, recenter on the primary display.
- [Coordinating "the player has an in-progress save and changes settings"] → The bridge only writes to `pending`; the active slot's settings are untouched until the user clicks "Generate & start new slot", which always creates a fresh slot (never overwrites the active one without explicit confirmation). The UI surfaces a "your current slot's settings:" read-only display so the user knows the distinction.
- [Audit-guard impact] → None. The audit guard scans `src/*.c` writes to `g_ram` cells; ImGui code lives outside `src/` (it's in `third_party/imgui/` and `src/rando/rando_window/`, which contains no `g_ram` writes). Confirmed against `audit_guard_indirect_writes.md` memory note.
- [Audio thread reads game state during sync generation] → The SDL audio callback (`AudioCallback` in `src/main.c`) runs on its own thread and consumes from `g_audiobuffer` under `g_audio_mutex`. While generation freezes the main loop for 100-500ms, the audio thread keeps pulling. If the generation pipeline mutates anything the audio thread reads (lazily-loaded SPC samples in `assets.h`, the SPC player's state in `spc_player.c`), a latent race exists. Mitigation: a verification task (tasks §19.4) walks the `Rando_GenerateSlot` call tree for any touch of audio-shared state; if any is found, either bracket generation with `SDL_LockMutex(g_audio_mutex)` or relocate the offending lazy-load to game-init time. Expected outcome on inspection: generation never touches audio state (it touches placement tables, the asset blob's chest/NPC tables, and SHA-256 buffers — all UI-thread-only). (Note: the in-game generate path already runs the same `Rando_GenerateSlot` body today on the same thread, so this race class is not introduced by this change.)
- [Switch Makefile glob picks up new sources unconditionally] → the Switch Makefile globs `$(SRC_DIR)/rando/*.c` **non-recursive** (`:59`). Any new `.c` placed directly under `src/rando/` lands in the Switch build regardless of the compile guard, and an `#ifdef`-empty `.cpp` would break the Switch toolchain (no g++ rule in that Makefile). Mitigation: all window/ImGui-side sources (`rando_window.cpp/.h`, `rando_window_bridge.c/.h`) live under `src/rando/rando_window/` — a subdirectory the non-recursive glob does NOT pick up. By contrast `rando_generate.c` and `rando_asset_decisions.c` live in `src/rando/*.c` and **intentionally** compile on Switch (the in-game screen calls them too) — they MUST carry zero ImGui/window deps. Verified explicitly in tasks §1.6 and §3.6.
- [In-game settings screen state lives in `select_file.c`, not only `rando.c`] → The in-game settings module is split: the `g_settings_*` state block (around `src/select_file.c:2295-2306`), the asset-warn dialog state (around `:2311-2320`), generate-progress state (around `:2322-2324`), `g_alphabet_textfield` and related text-input state elsewhere in the same file, and the screen dispatch wired from `select_file.c` flow control. Wrapping only `rando.c` in the compile guard would leave unresolved references at link time on PC. Mitigation: tasks §16 enumerates every site that needs the guard (referenced by symbol, not line, per the project's "comment anchors not line numbers" convention), not just `rando.c`.

## Migration Plan

This change is additive on PC (new files + new build flag) and a no-op on Switch. Migration steps:

1. Vendor ImGui under `third_party/imgui/` at the pinned commit. Add to PC build only.
2. Extract the in-game generate body into the shared `Rando_GenerateSlot` (and promote the asset-decision helpers); refactor `SelectFile_Settings_HandleGenerate` to call it; corpus stays green (no `kGeneratorVersion` bump).
3. Land the bridge + window skeleton (hidden by default) with empty panels; verify two-window event routing and the show-on-New-Randomizer path work.
4. Implement each settings panel (general / dungeons / recommended-features / asset-hash / spoiler viewer) backed by the existing `RandoSettings` and `Settings_*` APIs.
5. Wire the kind-toggle "New Randomizer" / "From share string" branches in `select_file.c` to `RandoWindow_OpenForNewSlot()` under `Z3R_NATIVE_SETTINGS_WINDOW`; the in-game settings-screen entry on PC becomes unreachable.
6. Add `Z3R_NATIVE_SETTINGS_WINDOW` to the Makefile and `.vcxproj`. Confirm Switch Makefile does NOT define it and the build still passes.
7. Compile-guard out `rando_textfield.{c,h}` and the in-game settings screen on PC (NOT delete — flip the guard off to rebuild); Switch unchanged.
8. Add the sidecar `saves/rando_window.ini` load (`Config_LoadAuxIniFile`) + parse to `config.c`.
9. Verify a full round-trip on PC: settings entered in native window → Generate → game loads slot → save/load → re-open → settings preselected to last value.
10. Verify Switch build (guard undefined): Switch settings-screen UI still functional; native window code is excluded; in-game screen also still compiles on Windows with the guard flipped off.

**Rollback:** the `Z3R_NATIVE_SETTINGS_WINDOW` guard makes rollback a one-define change: undefine it, the in-game UI compiles back in. The bridge file becomes dead code (no callers) but doesn't break anything. The sidecar `saves/rando_window.ini` is simply not loaded.

## Open Questions

- Which dark-theme palette ships as default — ImGui's "Dark" preset, or a custom palette tuned to look more game-themed? **Proposed answer:** ship ImGui's "Dark" preset as default; defer cosmetic theming to Phase B.
- Should the native window be hide-able while the game runs (e.g., a "Hide window" hotkey)? **Resolved:** the window is **hidden-until-needed** — created hidden at startup, shown/raised only when the player chooses "New Randomizer", and re-openable via a config-bound hotkey / game-window menu. The OS close-button hides (does not destroy) it. Vanilla players never see a second window.
- Should the spoiler viewer also expose the regression-corpus comparison feature (diff against an expected spoiler JSON)? **Proposed answer:** no for Phase A — that's a developer tool, not a player tool. Phase B may add a "Developer" tab.
