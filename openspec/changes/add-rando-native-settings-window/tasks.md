## 1. Vendor Dear ImGui and wire it into PC builds

- [ ] 1.1 Download Dear ImGui v1.91.x tarball at the pinned commit and unpack to `third_party/imgui/` (production sources only: `imgui.cpp`, `imgui_draw.cpp`, `imgui_tables.cpp`, `imgui_widgets.cpp`, `imconfig.h`, `imgui.h`, `imgui_internal.h`, `imstb_*.h`). **Do NOT include `imgui_demo.cpp`** — it's ~10 kLOC of widget showcase that bloats the binary and is never reached in production code. If a debug build later wants the demo window, gate it behind a `Z3R_IMGUI_DEMO=1` opt-in flag.
- [ ] 1.2 Copy the SDL2 and OpenGL3 backends to `third_party/imgui/backends/` (`imgui_impl_sdl2.cpp`, `imgui_impl_sdl2.h`, `imgui_impl_opengl3.cpp`, `imgui_impl_opengl3.h`, `imgui_impl_opengl3_loader.h`)
- [ ] 1.3 Add `IMGUI_VERSION` and the pinned commit hash to a header note in `third_party/imgui/PINNED.md` for future bump traceability
- [ ] 1.4 Update root `Makefile` to add dual-language C/C++ compilation. The Makefile today uses a single `$(CC)` rule and globs `src/*.c snes/*.c`. Add a concrete pattern like:
  ```make
  CXX        ?= $(if $(filter clang,$(CC)),clang++,g++)
  CXXFLAGS   ?= $(CFLAGS) -std=c++17 -fno-exceptions -fno-rtti
  IMGUI_SRCS := third_party/imgui/imgui.cpp third_party/imgui/imgui_draw.cpp \
                third_party/imgui/imgui_tables.cpp third_party/imgui/imgui_widgets.cpp \
                third_party/imgui/backends/imgui_impl_sdl2.cpp \
                third_party/imgui/backends/imgui_impl_opengl3.cpp \
                src/rando/rando_window/rando_window.cpp
  IMGUI_OBJS := $(IMGUI_SRCS:.cpp=.o)
  %.o: %.cpp
  	$(CXX) $(CXXFLAGS) -DZ3R_NATIVE_SETTINGS_WINDOW=1 -c $< -o $@
  zelda3: $(C_OBJS) $(IMGUI_OBJS)
  	$(CXX) $(LDFLAGS) $^ -o $@ $(LIBS)   # CXX (not CC) so libstdc++ links
  ```
  Add `-DZ3R_NATIVE_SETTINGS_WINDOW=1` to PC `CFLAGS` too (so C-side guard sites see it). The link step MUST go through `$(CXX)` to pull libstdc++; using `$(CC)` will produce undefined-symbol errors at link time.
- [ ] 1.5 Update `Zelda3.vcxproj` to include the same ImGui sources (excluding `imgui_demo.cpp`) and `src/rando/rando_window/rando_window.cpp`; add `Z3R_NATIVE_SETTINGS_WINDOW=1` to both C and C++ PreprocessorDefinitions. The MSBuild C++ toolchain already handles both `.c` and `.cpp` in the same project.
- [ ] 1.6 Switch-build exclusion: `src/platform/switch/Makefile:43,59` globs `$(SRC_DIR)/rando/*.c` wholesale, so any new `.c` under `src/rando/` lands in the Switch build regardless of the compile guard. To keep the new sources out of Switch:
  - **Place all new window-side sources under `src/rando/rando_window/`** (a subdirectory the Switch glob does NOT recurse into). This includes `rando_window.cpp`, `rando_window.h`, and `rando_window_bridge.c` + `rando_window_bridge.h`.
  - Verify the Switch glob skips the subdirectory by running `make -n` on the Switch build and confirming no rando_window paths appear in the compile list.
  - Confirm `Z3R_NATIVE_SETTINGS_WINDOW` is NOT defined in the Switch Makefile.
- [ ] 1.7 Build PC (Linux `make`, MSBuild) and Switch separately to confirm both compile after the build-system changes with no source touched beyond `Makefile` / `.vcxproj`. Switch build must continue to find `RandoTextField_*` and `Rando_OpenSettingsScreen` symbols; PC build must NOT.

## 2. Create the bridge module

- [ ] 2.1 Add `src/rando/rando_window/rando_window_bridge.h` declaring `struct RandoWindowBridge`: `pending` (`RandoSettings`), `pending_hash[32]`, `share_string[kShareStringBase32MaxLen]` (use the constant from `src/rando/rando_share.h:16`, not a magic 64), `seed_u64`, `generate_requested` (bool), `generate_in_progress` (bool), `generate_status` (int: 0/1/2/-1), `generate_error[256]`, `target_slot_index` (int — -1 sentinel for "no kind-toggle target"), `last_generated_race_mode` (bool — tracks race_mode of the most recent successful generation; gates spoiler-viewer visibility per spec, distinct from `pending.race_mode`), `asset_hash_decision_for_current_blob` (enum), and `extern RandoWindowBridge g_rando_window_bridge;`. (Path note: lives under `src/rando/rando_window/` so the Switch Makefile glob excludes it — see §1.6.)
- [ ] 2.2 Add `src/rando/rando_window/rando_window_bridge.c` with `RandoWindowBridge_Init(void)` that calls `Settings_SetDefaults(&pending)` and refreshes `pending_hash` + `share_string`
- [ ] 2.3 Add `RandoWindowBridge_RecomputeDerived(void)` (callable from UI side after any `pending` field change) that updates `pending_hash` via `Settings_ComputeHash` and rebuilds `share_string` via the existing share-string encoder
- [ ] 2.4 Add `RandoWindowBridge_RequestGenerate(int slot_index)` and `RandoWindowBridge_ConsumeGenerateRequest(void)` — the former is called by UI, the latter by the game side at frame start
- [ ] 2.5 Add `RandoWindowBridge_SetGenerateResult(int status, const char *err)` to write status + error back; called by game side on generation completion
- [ ] 2.6 Document the ownership rule in a top-of-file comment: UI thread mutates `pending`, `seed_u64`, `generate_requested`, `target_slot_index`; game thread mutates `generate_in_progress`, `generate_status`, `generate_error`
- [ ] 2.7 There is no centralized `Rando_Init` startup hook today (verified — grep finds zero callers of any such symbol). Wire `RandoWindowBridge_Init` into `src/main.c` startup directly, under `#ifdef Z3R_NATIVE_SETTINGS_WINDOW`, AFTER the headless `Maybe*` CLI checks (`MaybeRunGenerateSeedAndExit`, `MaybeRunBenchLogicAndExit`, `MaybeRunPrintAssetsHashAndExit`, etc. — they `exit(0)` before reaching SDL, so window init must come after them) and AFTER `SDL_Init(SDL_INIT_VIDEO|...)`. Place it next to the game window creation

## 3. Create the second SDL window and event-pump routing

- [ ] 3.1 In `src/main.c` under `#ifdef Z3R_NATIVE_SETTINGS_WINDOW`, AFTER all headless `Maybe*` CLI checks (they call `exit(0)`; window creation in front of them runs SDL init on every CI seed-gen invocation) and AFTER the game window is created, create a second `SDL_Window` named "Z3R Settings" with `SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI` at default 720×900
- [ ] 3.2 Create a dedicated `SDL_GLContext` for the settings window via `SDL_GL_CreateContext(settings_window)`. **Never pass `g_window` (the game window) to GL context creation for the settings UI** — the default game renderer is SDL software (no GL on game window), and forcing a GL context onto it changes its pixel format and breaks software/SDL-hardware paths. Store both the settings window handle and its GL context as file-statics for the main loop to bind/restore each frame.
- [ ] 3.3 Persist + restore both window positions/sizes from `zelda3.ini` `[rando_window]` keys at create time; clamp restored geometry to the union of `SDL_GetDisplayBounds` across all displays
- [ ] 3.4 In the main `SDL_PollEvent` loop, branch on `event.type` FIRST:
  - **Global events without a `window.windowID` field** — these MUST bypass the per-window routing or they'll be silently dropped:
    - `SDL_QUIT` → application shutdown
    - `SDL_CONTROLLERDEVICEADDED` / `SDL_CONTROLLERDEVICEREMOVED` / `SDL_CONTROLLERAXISMOTION` / `SDL_CONTROLLERBUTTONDOWN` / `SDL_CONTROLLERBUTTONUP` → always game (game uses controller; settings window is mouse+keyboard)
    - `SDL_JOYDEVICEADDED` / `SDL_JOYDEVICEREMOVED` / `SDL_JOY*` → always game
    - `SDL_AUDIODEVICEADDED` / `SDL_AUDIODEVICEREMOVED` → existing audio handler
    - `SDL_KEYMAPCHANGED` → both (notify ImGui via `ImGui_ImplSDL2_ProcessEvent`, also notify game-input layer)
  - **Window-targeted events** (`SDL_WINDOWEVENT`, `SDL_KEYDOWN`, `SDL_KEYUP`, `SDL_TEXTINPUT`, `SDL_TEXTEDITING`, `SDL_MOUSEMOTION`, `SDL_MOUSEBUTTONDOWN`, `SDL_MOUSEBUTTONUP`, `SDL_MOUSEWHEEL`, `SDL_DROPFILE`) — these DO carry a window ID. Branch on `event.window.windowID` (or `event.key.windowID` / `event.motion.windowID` / etc. depending on event type — SDL2's union convention) against `SDL_GetWindowID(game_window)` and `SDL_GetWindowID(settings_window)`; pass settings-window events to `ImGui_ImplSDL2_ProcessEvent`; pass game-window events to the existing input path
  - Verify the bypass list against `SDL_events.h` — any future SDL2 event type the project starts using needs to be added here
- [ ] 3.5 Handle `SDL_WINDOWEVENT_CLOSE` separately per window: closing the game window initiates app shutdown (existing behavior); closing the settings window calls `SDL_HideWindow` (and writes a re-open hint to a status bar in the game window if practical) instead of destroying
- [ ] 3.6 On Switch / when `Z3R_NATIVE_SETTINGS_WINDOW` is undefined, none of §3 compiles in; verify by grep that every line of new `main.c` code is inside the guard
- [ ] 3.7 **GL context binding/restoration order each frame** (this is a debugging trap if wrong):
  1. Before any ImGui call (`ImGui_ImplOpenGL3_NewFrame`, `ImGui::NewFrame`, `ImGui::Render`, `ImGui_ImplOpenGL3_RenderDrawData`, `SDL_GL_SwapWindow(settings_window)`): call `SDL_GL_MakeCurrent(settings_window, settings_gl_context)`.
  2. After the settings frame finishes its swap: if `g_config.output_method == kOutputMethod_OpenGL`, call `SDL_GL_MakeCurrent(game_window, game_gl_context)` to restore. Otherwise no restoration is needed (the game window has no GL context).
  3. Document this ordering at the top of `rando_window.cpp` so future contributors don't accidentally remove the bind call.

## 4. Initialize ImGui and the SDL2 + OpenGL3 backends

- [ ] 4.1 Add `src/rando/rando_window/rando_window.h` declaring `RandoWindow_Init(SDL_Window *window, SDL_GLContext gl)`, `RandoWindow_BeginFrame(void)`, `RandoWindow_Render(void)`, `RandoWindow_Shutdown(void)`, and `RandoWindow_OpenForNewSlot(int slot_index)` — all guarded behind `Z3R_NATIVE_SETTINGS_WINDOW`
- [ ] 4.2 Add `src/rando/rando_window/rando_window.cpp` implementing the wrapper: `ImGui::CreateContext()`, `ImGui::StyleColorsDark()`, `ImGui_ImplSDL2_InitForOpenGL`, `ImGui_ImplOpenGL3_Init` with the GLSL version string. **macOS-specific setup (must run BEFORE the settings window is created):**
  ```c
  #ifdef __APPLE__
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
    const char *glsl_version = "#version 150";
  #else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    const char *glsl_version = "#version 130";
  #endif
  ```
  Pass `glsl_version` to `ImGui_ImplOpenGL3_Init`. Also call `SDL_GL_GetDrawableSize` (not `SDL_GetWindowSize`) when computing the ImGui viewport, because `SDL_WINDOW_ALLOW_HIGHDPI` produces a 2× framebuffer on macOS retina and Windows high-DPI displays; failing to do this renders ImGui at half size in the lower-left quadrant.
- [ ] 4.3 `RandoWindow_BeginFrame` calls `ImGui_ImplOpenGL3_NewFrame`, `ImGui_ImplSDL2_NewFrame`, `ImGui::NewFrame` and renders the top-level window with a tab bar (placeholder tabs: General, Dungeons, Shuffles, Asset Hash, Spoiler)
- [ ] 4.4 `RandoWindow_Render` calls `ImGui::Render`, makes the settings GL context current, sets viewport, clears, calls `ImGui_ImplOpenGL3_RenderDrawData`, and swaps the settings window
- [ ] 4.5 `RandoWindow_Shutdown` calls the backend shutdown functions and `ImGui::DestroyContext()`
- [ ] 4.6 Wire `RandoWindow_Init` into `main.c` startup right after the second window is created (and AFTER all headless `Maybe*Exit` calls — see §3.1); wire `RandoWindow_BeginFrame`/`RandoWindow_Render` into the main loop after game rendering each frame; wire `RandoWindow_Shutdown` into normal teardown BEFORE `SDL_Quit` is called, but AFTER the audio thread has been joined (otherwise an in-flight audio callback could outlive its dependencies)
- [ ] 4.7 Confirm building with `-Werror` succeeds; resolve any C-vs-C++ linkage notes by `extern "C"` around the C-headers ImGui code calls into

## 5. Build the General settings panel

- [ ] 5.1 Inside the General tab, render a dropdown for `world_state` with the four enum values, labels matching CLI grammar (`open`, `standard`, `inverted`, `retro`)
- [ ] 5.2 Render a dropdown for `goal` (all 7 enum values, labels: `ganon`, `fast_ganon`, `dungeons`, `pedestal`, `triforce-hunt`, `ganonhunt`, `completionist`)
- [ ] 5.3 Render integer inputs for `crystals_ganon` (0-7) and `crystals_tower` (0-7); enforce range via `ImGui::DragInt` clamping
- [ ] 5.4 Render dropdown for `item_pool_difficulty` (4 values, labels: `easy`, `normal`, `hard`, `expert`); render dropdown for `logic` (pinned to `NoGlitches` in Phase A, visually disabled with tooltip)
- [ ] 5.5 Render dropdowns for `mode_weapons`, `accessibility`, `pyramid_bow_upgrade` using their canonical labels
- [ ] 5.6 Render integer inputs for `pieces_required` and `pieces_placed`; visually hide both when `goal` is not Triforce Hunt or Ganon Hunt
- [ ] 5.7 Render checkboxes for `prize_shuffle`, `medallion_shuffle`, `race_mode`
- [ ] 5.8 After any widget change, call `RandoWindowBridge_RecomputeDerived` so hash and share string update for the next frame
- [ ] 5.9 Display the live `settings_hash` (first 16 hex chars) and full share string in a read-only text box below the widgets

## 6. Build the Dungeons settings panel

- [ ] 6.1 In a new tab, render four side-by-side dropdowns for `dungeon_small_keys_mode`, `dungeon_big_keys_mode`, `dungeon_maps_mode`, `dungeon_compasses_mode` — each with the three `DungeonItemMode` values (`vanilla`, `dungeon`, `wild`)
- [ ] 6.2 Render `region_boss_hearts_in_pool` checkbox with a tooltip explaining it is pinned in Phase A
- [ ] 6.3 Add a "Set all to vanilla" / "Set all to dungeon" / "Set all to wild" trio of helper buttons

## 7. Build the Preset gallery

- [ ] 7.1 In the General tab top section, render a horizontal row of buttons for every value in `SettingsPreset` (`kPreset__Count` entries); each button's label is `Settings_PresetName(preset)`
- [ ] 7.2 Clicking a preset button calls `Settings_ApplyPreset(preset, &g_rando_window_bridge.pending)` then `RandoWindowBridge_RecomputeDerived`
- [ ] 7.3 Hover-tooltip on each preset button shows a brief description of what it sets (e.g., "Open / Fast Ganon — the defaults")

## 8. Build the cross-field validators and live error display

- [ ] 8.1 Add `RandoWindowBridge_Validate(const RandoSettings *s, char *out_err, size_t cap)` returning non-zero on invalid; check `crystals_tower > crystals_ganon` only when goal ∈ {Fast Ganon, Ganon Hunt}, `pieces_required > pieces_placed` only when goal ∈ {Triforce Hunt, Ganon Hunt}, and Completionist forcing accessibility = locations
- [ ] 8.2 In `RandoWindow_BeginFrame`, call `RandoWindowBridge_Validate` and display the resulting error message in red below the panel; disable the Generate button if validation fails
- [ ] 8.3 When goal changes to Completionist, auto-set `accessibility = kAccessibility_Locations` and mark the dropdown read-only via ImGui flag; restore editability when goal changes back

## 9. Build the share-string entry + copy/paste

- [ ] 9.1 Add a text input bound to a local buffer for "Paste share string here"; on Enter or focus loss, call the existing share-string decoder
- [ ] 9.2 On successful decode, write the decoded `RandoSettings` and `seed_u64` into `bridge.pending` / `bridge.seed_u64`, then `RandoWindowBridge_RecomputeDerived`
- [ ] 9.3 On decode failure, display the decoder's error message inline below the input; do NOT mutate `pending`
- [ ] 9.4 Add "Copy current share string" button that calls `SDL_SetClipboardText(bridge.share_string)`
- [ ] 9.5 Add "Paste from clipboard" button that calls `SDL_GetClipboardText`, writes the result into the share-string input, and triggers the decode flow

## 10. Build the seed input

- [ ] 10.1 Render a uint64 input for `seed_u64`; default-fill via the existing rando RNG (`Rando_RandomSeedU64` or equivalent) on window first-open
- [ ] 10.2 Add a "New random seed" button that overwrites `seed_u64` from the RNG and updates the displayed share string
- [ ] 10.3 Display the seed as both decimal (full uint64) and 16-char hex for race-admin convenience

## 11. Build the Asset Hash panel and warning modal

- [ ] 11.1 In an Asset Hash tab, display the current `g_assets_hash` (hex), the `kVanillaAssetsHash` (hex), and the persisted decision for the current hash (None / Always allow / Never asked) — read the decision via the existing in-memory lookup populated by `Rando_RegisterAssetDecisionFromIni` (`src/config.c:544`)
- [ ] 11.2 On Generate click, if `g_assets_hash != kVanillaAssetsHash` and decision is None, open an ImGui modal "Asset data differs from vanilla" with body text and three buttons: Always allow, Allow once, Cancel
- [ ] 11.3 "Always allow" persists the decision via the existing `[RandoAssetDecisions]` storage path (the same lookup the in-game UI uses at `src/select_file.c:2256-2270`) — do NOT invent a new `[rando_window].asset_hash_decisions` key, which would create two sources of truth. Reuse `AssetDecision_Persist` (rename to a public symbol if needed) so PC and Switch share one in-memory store. Continues generation after persisting.
- [ ] 11.4 "Allow once" continues generation without persisting; uses the existing `g_asset_warn_session_bypass` one-shot pattern (`src/select_file.c:2222`) or its equivalent for the native window
- [ ] 11.5 "Cancel" closes the modal without generating; settings widgets remain at their current values
- [ ] 11.6 Vanilla asset hash bypasses the modal entirely
- [ ] 11.7 Verify that the Switch in-game UI and PC native window see the same persisted decisions after a round-trip — boot Switch, choose Always allow for hash H; boot PC, confirm hash H is recognized as Allow without re-prompting (manual cross-platform test)

## 12. Extract a callable generate function (HARD PREREQUISITE for §13)

There is NO `Rando_Generate(settings, seed)` function today. "Generate a seed" is a ~200-line inline orchestration in `src/main.c:392-608` (the headless CLI path): `Settings_SetDefaults` → optional `Settings_ParseCsv` → `--race-mode` override → seed parse → `Place_AssumedFill` → optional `BossShuffle_Generate` / `DropShuffle_Generate` / `Rando_GenerateHints` → `Settings_HashShort` → `Share_Encode` → `Logic_ComputeSpheres` → `Goal_IsCompletable` → `Spoiler_Write` → sidecar write → `Placement_Install`. The bridge consumer needs all of these to run in identical order or it diverges from the CLI/regression-corpus path.

Extract first, then build the bridge consumer in §13. Without this, §13 is a multi-day refactor disguised as one task.

- [ ] 12.1 Add `src/rando/rando_generate.h` declaring:
  ```c
  typedef struct RandoGenerateResult {
    bool ok;
    bool used_forward_fill;
    bool goal_completable;
    char share_string[kShareStringBase32MaxLen];
    uint8 settings_hash[32];
    // anything else the sidecar writer or UI needs to display
  } RandoGenerateResult;

  bool Rando_GenerateForBridge(const RandoSettings *settings,
                               uint64 seed_u64,
                               int budget_seconds,
                               RandoGenerateResult *out,
                               char *err, size_t err_cap);
  ```
- [ ] 12.2 Add `src/rando/rando_generate.c` implementing `Rando_GenerateForBridge` by relocating (NOT rewriting) the orchestration from `main.c:392-608`. Preserve the exact call order, the exact `Placement_Install` timing, the exact race-mode handling. On any step's failure (logic-impossible, fill exhausted, asset blob unavailable), populate `err` and return false; on success, populate `out` and return true.
- [ ] 12.3 Refactor `main.c`'s headless CLI path to call `Rando_GenerateForBridge` internally, then run the regression corpus per `randomizer-core` and assert byte-identical placement and spoiler output for every (settings, seed) pair in the corpus. **No behavior change permitted.** This is the gate that proves the extraction was clean.
- [ ] 12.4 Also extract the "write generated state to a sidecar slot" sub-step into `Rando_WriteSlotFromGenerateResult(int slot_index, const RandoSettings *settings, uint64 seed_u64, const RandoGenerateResult *result)` — the CLI path does this inline today; both consumers need it.
- [ ] 12.5 Document `Rando_GenerateForBridge`'s thread-safety contract in the header: caller must be on the game thread; function does not return until generation completes; it installs the placement via `Placement_Install` before returning (so the spoiler viewer can read it via `Placement_GetActive` immediately).

## 13. Build the Generate flow (depends on §12)

- [ ] 13.1 Add a prominent "Generate & start new slot" button at the bottom of the window; disabled when validation fails; tooltip explains why if disabled
- [ ] 13.2 On click, the UI side checks asset-hash decision (per §11) then sets `bridge.generate_requested = true`, opens an ImGui modal "Generating seed..."
- [ ] 13.3 On game side, add a per-frame check at the top of `ZeldaRunFrame()` (or equivalent main-loop hook) under `#ifdef Z3R_NATIVE_SETTINGS_WINDOW` that calls `RandoWindowBridge_ConsumeGenerateRequest` and, if a request was pending, calls `Rando_GenerateForBridge(&bridge.pending, bridge.seed_u64, g_config.rando_budget_seconds, &result, err, sizeof err)`
- [ ] 13.4 On success: call `Rando_WriteSlotFromGenerateResult(bridge.target_slot_index, ...)`, write `bridge.last_generated_race_mode = bridge.pending.race_mode` (gates spoiler-viewer visibility per spec), and call `RandoWindowBridge_SetGenerateResult(2, "")`
- [ ] 13.5 On failure: call `RandoWindowBridge_SetGenerateResult(-1, err)` with the message populated by §12.2
- [ ] 13.6 On UI side, the modal polls `bridge.generate_status` each ImGui frame; on `2` (success) close the modal and show a follow-up dialog "Slot N is ready — Load it now? [Yes / No]"; on `-1` (error) update modal body to the error and show OK
- [ ] 13.7 "Load it now" routes to the existing file-select load path for the new slot
- [ ] 13.8 Confirm that the game thread is blocked for the generation duration (UI shows the modal; the game window's frame counter does not advance) and that this is acceptable for the expected 100-500ms generation time

## 14. Build the read-only Spoiler / Placement viewer tab

- [ ] 14.1 Add a Spoiler tab; hide entirely when `bridge.last_generated_race_mode == true` (set by §13.4 after a successful generation). The gate is on what was LAST GENERATED, not on `bridge.pending.race_mode` (which is what the user is editing right now).
- [ ] 14.2 Render the placement table from `Placement_GetActive()` (`src/rando/rando_placement.h:60` — returns `const RandoPlacementTable *`) grouped by region; columns: Location, Item, Source (Vanilla/Dungeon/Wild for dungeon items). If `Placement_GetActive()` returns NULL (no generation has run yet this session, or rando mode inactive), show an empty-state message: "Generate a seed to see its placement here."
- [ ] 14.3 Use ImGui's table widget with sortable columns and a per-region collapse header
- [ ] 14.4 Provide a "Save spoiler to file..." button that re-uses `rando_spoiler.c` to write the spoiler JSON to a user-chosen path via SDL's portable file-dialog approach (or a simple text input path if SDL2 lacks native dialog)
- [ ] 14.5 Provide a "Save spoiler to clipboard" button that puts the JSON into the OS clipboard
- [ ] 14.6 Ensure NO ImGui widget in this tab is editable (use `ImGui::BeginDisabled` around any cell-render path that might be misread as editable)

## 15. Wire the kind-toggle on PC file-select to the native window

- [ ] 15.1 In `src/select_file.c`, locate the two existing `SelectFile_Settings_Activate(g_kind_picker_target_slot, ...)` call sites at lines `:1664` and `:1891` — these are the "New Randomizer" entry points in the empty-slot kind-toggle. **`Rando_OpenSettingsScreen` does NOT exist as a function** (verified by grep — zero matches); `SelectFile_Settings_Activate` is the real, file-static entry point declared at `src/select_file.c:190,2723`.
- [ ] 15.2 Under `#ifdef Z3R_NATIVE_SETTINGS_WINDOW`, replace each `SelectFile_Settings_Activate(...)` call with `RandoWindow_OpenForNewSlot(g_kind_picker_target_slot)`. Use `#ifdef`/`#else`/`#endif` to keep the Switch path (which calls `SelectFile_Settings_Activate`) unchanged. **Do NOT** also `#ifndef`-out the `SelectFile_Settings_Activate` function definition itself — that's part of §16's compile-out work and must be coordinated with §16.5 (it's file-static so the link error only surfaces if the function-internal body references compiled-out state).
- [ ] 15.3 `RandoWindow_OpenForNewSlot(int slot_index)`: writes `slot_index` into `bridge.target_slot_index`, calls `SDL_ShowWindow` + `SDL_RaiseWindow` on the settings window, and returns immediately (no blocking)
- [ ] 15.4 Game side returns control to the file-select loop; the file-select shows an unobtrusive "Configuring slot N in the settings window..." status until the bridge reports success
- [ ] 15.5 Cancel handling: when the user closes the settings window via the OS close control while `bridge.target_slot_index >= 0` (kind-toggle target set, generation not yet complete), `RandoWindowBridge_CancelTarget()` clears the target index back to -1 and the file-select status string disappears on its next render. No sidecar slot is written.
- [ ] 15.6 Re-targeting: if the user invokes New Randomizer on a different empty slot M while the settings window is already open with a different target slot N, the second call overwrites `bridge.target_slot_index = M`; pending settings edits are preserved (not reset).
- [ ] 15.7 On Switch / undefined guard, both call sites continue to invoke `SelectFile_Settings_Activate` unchanged.

## 16. Compile-out the in-game settings screen and text-input layer on PC

The in-game settings module is split across `rando.c` AND `select_file.c` — guarding only `rando.c` will leave unresolved references at link time. **Critical sites that must NOT be removed even on PC**:
- `Rando_RegisterAssetDecisionFromIni` (`src/select_file.c:2279`) — externally called from `src/config.c:544` to feed the `[RandoAssetDecisions]` parser. Guarding it out breaks the parser at link time. Either keep it outside the compile-out region, OR move its body to a new file `src/rando/rando_asset_decisions.c` (preferred — see §17.6).
- `SelectFile_Settings_Deactivate` (`src/select_file.c:2757`) — called from `src/select_file.c:450, :3063, :3441` for state-reset purposes (file-select entry, BG transitions) UNRELATED to opening the settings screen. Must remain callable: either as an empty stub under the PC guard, or by leaving the function definition outside the compile-out region.

Enumerate every call site:

- [ ] 16.1 In `src/rando/rando.c`, wrap any settings-screen module entry points and tile-rendering paths that are present there in `#ifndef Z3R_NATIVE_SETTINGS_WINDOW ... #endif`. (Verify by grep what's actually in this file vs. `select_file.c` — much of the settings UI state lives in `select_file.c`, not `rando.c`.)
- [ ] 16.2 In `src/select_file.c`, wrap the in-game settings-screen state and functions in `#ifndef Z3R_NATIVE_SETTINGS_WINDOW`. Comprehensive list (confirm by grep; this is the floor not the ceiling):
  - **Module state**: `g_settings_active`, `g_settings_view`, `g_settings_target_slot`, `g_settings_cursor`, `g_settings_scroll_offset`, `g_settings_preset_index`, `g_settings_working`, `g_settings_seed_field`, `g_settings_seed_value`, `g_settings_seed_parse_ok`, `g_settings_prepopulated_seed`, `g_settings_seed_prepopulated` (`src/select_file.c:2196-2208`)
  - **Recommended-features state**: `g_rec_cursor`, `g_rec_working_features0`, `kRecRowBits[]`, `kRecRecommendedOn[]`, `kRecRowLabels[]`
  - **Asset-warn dialog state**: `g_asset_warn_cursor`, `g_asset_warn_pending`, `g_asset_warn_session_bypass`
  - **Generate-progress state**: `g_settings_generate_in_progress`, `g_settings_hash_short`
  - **Alphabet picker**: `g_alphabet_textfield` and every site that calls `TextField_*` from the in-game settings flow (grep `src/select_file.c` for `TextField_`)
  - **Enums**: `kRow_*`, `kSettingsView_*`, `kAssetWarnChoice_*`, `kRecRow_*`
  - **File-static helpers**: `RowLabel`, `RowValueText`, `SettingsHashRefresh`, `SettingsValidatePieces`, `CycleRow`, `DeriveSeedFromState`, `ParseSeedField`
  - **Entry point**: `SelectFile_Settings_Activate` (`:190, :2723`) — both forward decl and definition
  - **Render dispatch** in the file-select main loop that calls into the settings screen module
- [ ] 16.3 **Do NOT guard `Rando_RegisterAssetDecisionFromIni` (`src/select_file.c:2279`)** even on PC — `config.c:544` calls it from the `[RandoAssetDecisions]` parser. Move its body to a new `src/rando/rando_asset_decisions.{c,h}` if §17.6 promotes the AssetDecision helpers; otherwise leave the function definition outside the compile-out region.
- [ ] 16.4 **Do NOT guard `SelectFile_Settings_Deactivate` (`src/select_file.c:2757`)** to removal — callers at `:450, :3063, :3441` are state-reset paths unrelated to the settings screen. Either keep the function definition outside the compile-out region, OR convert it to a `#ifdef Z3R_NATIVE_SETTINGS_WINDOW`-conditional no-op stub:
  ```c
  static void SelectFile_Settings_Deactivate(void) {
  #ifndef Z3R_NATIVE_SETTINGS_WINDOW
    g_settings_active = false;
    g_settings_view = kSettingsView_Main;
    // ... rest of the existing reset
  #endif
  }
  ```
- [ ] 16.5 In `src/rando/rando_textfield.c` and `src/rando/rando_textfield.h`, wrap the entire file content in `#ifndef Z3R_NATIVE_SETTINGS_WINDOW` so PC builds compile to empty translation units. Also update the root Makefile to skip building `rando_textfield.c` on PC (avoids the empty-TU warning under some toolchains); Switch still builds it. Verify by `make -n` that `rando_textfield.o` does NOT appear in the PC build.
- [ ] 16.6 Compile PC. Resolve any unresolved-symbol errors by adding the appropriate guard to the offending site; this is the work the grep in §16.2 is meant to front-load. **Expect at least one missed site.**
- [ ] 16.7 Verify with a PC build that no symbol named `RandoTextField_*`, `g_settings_*`, or `SelectFile_Settings_Activate` exists in the linked binary (`nm zelda3 | grep -E "RandoTextField|g_settings_|SelectFile_Settings_Activate"` returns empty). Confirm `Rando_RegisterAssetDecisionFromIni` and `SelectFile_Settings_Deactivate` DO still exist (they must, per §16.3, §16.4).
- [ ] 16.8 Verify with a Switch build that all the above symbols still exist and the in-game UI is fully functional (boot a Switch build manually, walk through the in-game settings screen end-to-end)

## 17. Persistence: build the INI writer, then wire the new section

**Hard prerequisite** — `config.c` is read-only today (`src/select_file.c:2235` documents the deferral: "config.c is read-only today"). Every persistence task below depends on the writer existing. Build it first.

- [ ] 17.1 Implement `Config_WriteIniFile(const char *path)` in `src/config.c`:
  - Read the existing `zelda3.ini` as text (preserve user's hand-edited keymaps, comments, section ordering).
  - Identify the `[rando_window]` and `[RandoAssetDecisions]` sections by header. Replace each in place with the current in-memory values; leave every other section verbatim.
  - Write to `zelda3.ini.tmp`, sync, atomic-rename to `zelda3.ini`. Never truncate the original until rename succeeds.
  - Cross-platform `#ifdef _WIN32` branch (POSIX `fsync` + `rename` is not portable):
    ```c
    #ifdef _WIN32
      _commit(_fileno(f));           // MSVCRT fsync analogue
      fclose(f);
      MoveFileExA(tmp, dst, MOVEFILE_REPLACE_EXISTING);  // atomic; rename() on Windows fails when target exists
    #else
      fsync(fileno(f));
      fclose(f);
      rename(tmp, dst);
    #endif
    ```
    The project already uses `#ifdef _WIN32` at `src/main.c:1028` — match that pattern.
  - If `zelda3.ini` did not previously contain `[rando_window]`, append the new section at the file end before the rename.
- [ ] 17.1a **Fallback if §17.1 proves too risky** (e.g., the file format has edge cases that risk clobbering keymaps): persist `[rando_window].*` to a sidecar `saves/rando_window.ini` with the same key format, and keep `[RandoAssetDecisions]` writes targeting the main `zelda3.ini`. Decide which path to ship after writing §17.1 and inspecting the risk concretely. Document the choice in this tasks.md line so future readers know which path is live.
- [ ] 17.2 In `src/config.c`, add parsing for the `[rando_window]` section with keys: `last_settings_canonical_hex` (string — hex-encoded canonical bytes; **NOT base64**, see §17.4), `last_seed_u64` (uint64 decimal), `window_x`, `window_y`, `window_w`, `window_h` (ints), `dark_theme` (bool). (`asset_hash_decisions` is NOT a `[rando_window]` key — it uses the existing `[RandoAssetDecisions]` section per §11.3.)
- [ ] 17.3 On `RandoWindow_Init`, after `RandoWindowBridge_Init`, hex-decode `last_settings_canonical_hex`; if it decodes to exactly `kSettingsCanonicalLen` bytes (currently 28 per `src/rando/rando_settings.h:124`) and re-serializes to the same bytes (round-trip check), write into `bridge.pending` and call `RandoWindowBridge_RecomputeDerived`
- [ ] 17.4 **Use hex, not base64.** The codebase has no base64 helper today (verified — grep of `src/` for `base64|Base64|b64` returns zero matches). Hex is 56 chars for 28 canonical bytes, fits on one INI line, and the project already has hex helpers (`src/main.c:927`, `ParseHashHex` in `src/config.c` for the asset-decision parser). Reuse those — do NOT introduce a base64 codec.
- [ ] 17.5 On corruption (length mismatch, hex decode failure, round-trip mismatch), log a one-line warning and keep `bridge.pending` at defaults
- [ ] 17.6 On `RandoWindow_Shutdown`, call `Config_WriteIniFile(path_to_zelda3_ini)` after updating the in-memory `[rando_window]` keys from `bridge.pending`. Writer also flushes `[RandoAssetDecisions]` so the in-game UI's existing in-memory decisions finally persist to disk (the long-promised "future INI write-back" the comment at `src/select_file.c:2268-2270` predicted).
- [ ] 17.7 Asset-hash decisions: use the existing `[RandoAssetDecisions]` infrastructure (`src/config.c:535-552`, `src/select_file.c:2229-2270`). Do NOT add a parallel in-memory map for the native window — read/write the same `g_asset_decisions` array. If `AssetDecision_FindAllow` / `AssetDecision_Persist` are file-static in `select_file.c`, promote them to a small public header (`src/rando/rando_asset_decisions.h`) so both UIs share one implementation. The new home naturally also hosts `Rando_RegisterAssetDecisionFromIni` (see §16.3) so the PC compile-out doesn't touch externally-called symbols.
- [ ] 17.8 Window geometry: on every settings-window move/resize event, update the in-memory geometry; persist on shutdown only (not per-event) to avoid INI thrash

## 18. Determinism cross-check

- [ ] 18.1 Add a one-shot startup self-check guarded by a debug flag: construct a `RandoSettings` via `Settings_SetDefaults`, compute `Settings_ComputeHash`, write the hex hash to stderr; this exact value MUST match the in-game Switch UI for the same defaults (verify manually against a Switch build run with the same defaults)
- [ ] 18.2 Add an integration test (manual is OK for Phase A): enter a specific `RandoSettings` via the native window on PC, capture the share string; enter the same `RandoSettings` via the Switch in-game UI, capture the share string; assert byte-identical
- [ ] 18.3 Add a regression-corpus run that calls `Rando_GenerateForBridge` (extracted in §12) on every (settings, seed) pair in the existing corpus; assert byte-identical placement and spoiler output vs. the pre-extraction CLI baseline. This is the gate that proves §12.2's extraction was clean.
- [ ] 18.4 **Decode-encode round-trip identity test** — Triforce-Hunt-disabled goals leave `pieces_required`/`pieces_placed` as struct fields whose interpretation is meaningless. The native window MUST NOT zero or normalize such fields differently from the Switch in-game UI, or two share strings from the two UIs will diverge despite "identical" user input. Test: for each `SettingsPreset` value, call `Settings_ApplyPreset(p, &s)`, encode `s` to a share string, decode that share string back to `s'`, assert `memcmp(&s, &s', sizeof(s)) == 0`. Ship this as a startup self-check guarded by the same debug flag as §18.1.
- [ ] 18.5 Audit the native-window code for any new SHA-256 implementation, canonical-serialization routine, share-string encoder/decoder, or RNG call — there must be none. All such operations route through the existing `Settings_*` and `Share_*` APIs. Grep `src/rando/rando_window/` for `SHA`, `sha256`, `serialize`, `Encode`, `rand`, etc., and confirm zero matches that introduce a parallel implementation.

## 19. Audit-guard and thread-safety verification

- [ ] 19.1 Run the existing audit-guard scripts against the new files (`third_party/imgui/**`, `src/rando/rando_window/**`, `src/rando/rando_generate.c`, `main.c` additions) to confirm zero `g_ram` writes are flagged
- [ ] 19.2 If any false positive arises, add an exempt marker per the `audit_guard_exempt_placement` memory note (must sit directly above the offending line)
- [ ] 19.3 Double-grep the new files for raw `g_ram[` references in case the regex misses an indirect pointer write per `audit_guard_indirect_writes`
- [ ] 19.4 **Audio-thread safety audit of synchronous generation.** During the 100-500ms generation freeze, the SDL audio callback (`AudioCallback` in `src/main.c`) keeps running on its own thread and reads from `g_audiobuffer` under `g_audio_mutex`. Walk the generation call tree (entry point → `Place_AssumedFill` → asset lookups → SHA-256) and grep for any read/write of shared audio state: anything touched by `spc_player.c`, `audio.c`, any lazily-loaded SPC sample data in `assets.h`. Expected outcome: generation never touches audio state and no mitigation is needed. If anything IS touched: either bracket the generation call with `SDL_LockMutex(g_audio_mutex)` / `SDL_UnlockMutex` (acceptable for a one-time op the user just triggered) or relocate the lazy-load to game-init time. Document the finding (and chosen mitigation, if any) inline in `Rando_GenerateForBridge`'s header comment.
- [ ] 19.5 **Shutdown-order audit.** `RandoWindow_Shutdown` must run BEFORE `SDL_Quit` and AFTER the audio thread has been joined (otherwise an in-flight audio callback could outlive its dependencies). Add an explicit step in `main.c` shutdown: `RandoWindow_Shutdown(); SDL_CloseAudioDevice(g_audio_device); SDL_Quit();` — confirm by reading the existing shutdown path.

## 20. Two-window input bug sweep

- [ ] 20.1 Manual test: focus the game window, press arrow keys — game responds, settings window does not
- [ ] 20.2 Manual test: focus the settings window, type letters into a text field — letters appear in the field, game character does not move
- [ ] 20.3 Manual test: gamepad connected — input always reaches the game regardless of which OS window has focus (proves the `SDL_CONTROLLER*` bypass list in §3.4 works)
- [ ] 20.4 Manual test: minimize the settings window, confirm game continues; restore the settings window, confirm state is preserved
- [ ] 20.5 Manual test: close the settings window via OS X-button, confirm game continues, confirm settings window can be re-opened (via the documented hotkey or game-window menu)
- [ ] 20.6 Manual test: drag the settings window to a second monitor, exit, re-launch — window opens on the same monitor at the same position
- [ ] 20.7 Manual test: drag the settings window so most of it is off-screen, edit `[rando_window].window_x/y` in `zelda3.ini` to be wildly off-screen, re-launch — window recenters
- [ ] 20.8 Manual test: headless CLI invocations (`./zelda3 --generate-seed=...`, `./zelda3 --print-assets-hash`, etc.) do NOT open any settings window (proves the §3.1 ordering keeps `RandoWindow_Init` behind the `Maybe*Exit` checks)

## 21. Spoiler viewer race-mode gate

- [ ] 21.1 Manual test: generate a slot with `race_mode = true`, confirm Spoiler tab is not visible
- [ ] 21.2 Manual test: generate a slot with `race_mode = false`, confirm Spoiler tab is visible and shows the correct placement from `Placement_GetActive()`
- [ ] 21.3 Add a regression unit-style check: when `bridge.last_generated_race_mode == true`, the function that builds the tab list omits "Spoiler"
- [ ] 21.4 Manual test: with a non-race active placement, toggle `pending.race_mode` ON in the settings panel WITHOUT re-generating; confirm Spoiler tab remains visible (gate is on last-generated, not pending)

## 22. Cross-platform CI verification

- [ ] 22.1 Confirm `make -j$(nproc) zelda3` passes on Linux with the new ImGui sources and `Z3R_NATIVE_SETTINGS_WINDOW=1`
- [ ] 22.2 Confirm `make` passes on macOS with the macOS-specific GL attributes from §4.2 and `Z3R_NATIVE_SETTINGS_WINDOW=1` defined. **Run the binary** (not just compile) and confirm the settings window opens at correct retina scale (not half-size in lower-left); failure here usually means `SDL_GL_GetDrawableSize` was not used per §4.2
- [ ] 22.3 Confirm MSBuild Release build passes on Windows with the updated `.vcxproj`. Verify the `#ifdef _WIN32` branch in §17.1 was actually used (the POSIX-only path would have failed at link time on Windows)
- [ ] 22.4 Confirm `cd src/platform/switch && make` passes on Switch with the new sources excluded and `Z3R_NATIVE_SETTINGS_WINDOW` undefined. Verify by `make -n` that no `imgui`, `rando_window`, or `rando_generate` paths appear in the Switch compile list
- [ ] 22.5 Run the regression-corpus comparator (per `randomizer-core`) on a small sample to confirm seeds generated through the native window match the corpus exactly (byte-identical placements for the same `(settings, seed_u64)` pairs)

## 23. Fresh-eyes audit and archive sequencing

- [ ] 23.1 Per CLAUDE.md's "Fresh-eyes audit cadence", after the implementation lands, spawn a parallel review agent with a self-contained prompt to look for new bugs — particularly around the two-window event-routing edge cases, the bridge ownership rule, the GL-context bind/restore order per frame, the INI writer's atomic-rename safety on both platforms, the `Rando_GenerateForBridge` extraction's behavior preservation, and the compile-time guard coverage on Switch
- [ ] 23.2 Document the audit findings in this change directory under `audit.md`; address every HIGH finding before declaring the change complete
- [ ] 23.3 Verify that no audit finding requires a determinism-affecting change (settings_hash, canonical bytes, generator version, share-string format); if any does, escalate as a generator_version bump per the parent change's rules
- [ ] 23.4 **Archive sequencing**: this change MODIFIES three `randomizer-ui` requirements (Text-input infrastructure, On-screen alphabet picker, Settings screen) that were ADDED by the parent `add-randomizer-support` and ALSO touches behavior that `add-rando-switch-swkbd` ADDS. Required archive order is `add-randomizer-support` → `add-rando-switch-swkbd` → `add-rando-native-settings-window`. Document this order in the archive command sequence and verify by dry-running `openspec validate --strict` after each simulated archive step. If the parent or swkbd change is archived AFTER this one, the MODIFIED requirement headers will not match a Switch-rephrased version of the originals and the archive will fail validation. Per the `openspec_authoring_patterns` memory: a future Phase B change touching these same three requirements should prefer ADDED over MODIFIED to avoid stacking the same conflict.
