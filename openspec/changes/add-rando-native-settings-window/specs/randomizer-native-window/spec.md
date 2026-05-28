## ADDED Requirements

### Requirement: Second OS window for randomizer settings on PC

The host SHALL create a second OS-native window dedicated to randomizer settings on PC builds (Windows, Linux, macOS), distinct from the game window. The window SHALL be created at application startup, before the game's main loop begins, and SHALL persist for the lifetime of the application unless explicitly hidden by the user.

#### Scenario: Both windows visible at startup

- **WHEN** the user launches the application on a PC build
- **THEN** two OS windows are present and visible: the game window (per existing behavior) and the settings window (new), each independently movable, resizable, and minimizable

#### Scenario: Closing the settings window does not close the game

- **WHEN** the user closes the settings window via the OS window-close control
- **THEN** the settings window hides (does not destroy the underlying SDL window) and the game continues running unaffected

#### Scenario: Closing the game window closes the application

- **WHEN** the user closes the game window
- **THEN** the application begins shutdown and the settings window also closes as part of normal teardown

#### Scenario: Switch build does not create the second window

- **WHEN** the application is built for Nintendo Switch (PLATFORM=switch)
- **THEN** the second-window creation code is excluded from the build by the `Z3R_NATIVE_SETTINGS_WINDOW` compile-time guard, and no second window appears

### Requirement: Settings window owns its own OpenGL context

The settings window SHALL be created with its own dedicated `SDL_GLContext`, distinct from any GL context the game window may or may not have. Before any ImGui frame call (`NewFrame`, `RenderDrawData`, `SDL_GL_SwapWindow` on the settings window), the code SHALL call `SDL_GL_MakeCurrent(settings_window, settings_gl_context)`. After completing the settings frame, if the game window uses the optional OpenGL renderer, the code SHALL call `SDL_GL_MakeCurrent(game_window, game_gl_context)` to restore. The settings GL context SHALL NEVER be created against the game window, because the default game renderer is SDL software and creating a GL context against the game window would alter its pixel format.

#### Scenario: Settings GL context is bound before any ImGui call

- **WHEN** the settings window begins or completes any ImGui frame
- **THEN** the immediately preceding code path called `SDL_GL_MakeCurrent(settings_window, settings_gl_context)`

#### Scenario: Game GL context is restored when present

- **WHEN** the settings frame has completed and the game uses the OpenGL renderer
- **THEN** `SDL_GL_MakeCurrent(game_window, game_gl_context)` is called before the next game frame begins

#### Scenario: Settings context never created against game window

- **WHEN** the settings window initializes
- **THEN** its `SDL_GLContext` is created via `SDL_GL_CreateContext(settings_window)` only; no code path passes `g_window` (the game window) to GL context creation for the settings UI

#### Scenario: Game window using software renderer is unaffected

- **WHEN** the user runs the default configuration where the game uses the SDL software renderer (no GL context on the game window)
- **THEN** creating the settings window's GL context does not alter the game window's pixel format and the game continues to render correctly through the software path

### Requirement: Two-window SDL event pump

The main SDL event loop SHALL pump events for both windows and route them by `event.window.windowID`. Events targeting the game window SHALL reach the existing game-input handling unchanged. Events targeting the settings window SHALL be passed to the ImGui SDL2 backend.

#### Scenario: Input to focused window routes correctly

- **WHEN** the player presses a key while the settings window has OS focus
- **THEN** the key event reaches the ImGui backend only and the game's input handler does not see it

#### Scenario: Game-window gameplay continues during settings interaction

- **WHEN** the user interacts with the settings window (clicks, types, scrolls)
- **THEN** the game frame continues to advance at its normal cadence and game input from a connected gamepad continues to reach the game

#### Scenario: Window-close event is routed by windowID

- **WHEN** the user clicks the OS close button on either window
- **THEN** the application identifies which window via `event.window.windowID` and applies the per-window close behavior (hide settings window vs. shut down on game window)

### Requirement: ImGui-rendered settings panels expose every RandoSettings field

The settings window SHALL render every field of `RandoSettings` (per `src/rando/rando_settings.h`) as an interactive ImGui widget. Enum-typed fields SHALL render as dropdowns (combos) whose option labels match the canonical CLI key grammar exactly (e.g., `fast_ganon`, `triforce-hunt`, `NoGlitches` — the same strings accepted by `Settings_ParseCsv`). Integer-typed fields SHALL render as integer input widgets with the documented valid ranges enforced. Boolean fields SHALL render as checkboxes.

#### Scenario: Every Phase A axis has a widget; Phase A-pinned axes are visibly disabled

- **WHEN** the user opens the settings window
- **THEN** widgets are present for every `RandoSettings` field: world_state, goal, crystals_ganon, crystals_tower, tricks, item_pool_difficulty, logic, mode_weapons, accessibility, pyramid_bow_upgrade, region_boss_hearts_in_pool, dungeon_small_keys_mode, dungeon_big_keys_mode, dungeon_maps_mode, dungeon_compasses_mode, prize_shuffle, medallion_shuffle, race_mode, pieces_required, pieces_placed
- **AND** the following fields render disabled (greyed out) with a tooltip explaining the Phase A pin, because the parent change pins them: `tricks` (pinned 0; `rando_settings.h:86`), `logic` (pinned `NoGlitches`; `:88`), `region_boss_hearts_in_pool` (pinned 1; `:92`), `pyramid_bow_upgrade` (only `Silvers` available in Phase A; `:67-70`). For `mode_weapons` and `accessibility`, only the Phase A subset of enum values appears in the dropdown — Phase B reservations (`Vanilla`, `Swordless`, `None`) are not selectable
- **AND** the disabled state is not just visual: attempting to change a disabled field via clipboard paste of a share string with an out-of-range value SHALL produce the inline error described in `randomizer-core` and SHALL NOT mutate the field

#### Scenario: Triforce-Hunt piece fields are visible only for relevant goals

- **WHEN** the user selects a goal other than Triforce Hunt or Ganon Hunt
- **THEN** the pieces_required and pieces_placed fields are visually disabled or hidden because they have no effect on placement

#### Scenario: Crystals_tower must not exceed crystals_ganon for Fast Ganon

- **WHEN** the user attempts to set crystals_tower > crystals_ganon while goal is Fast Ganon or Ganon Hunt
- **THEN** the UI surfaces an inline validation error and the Generate button is disabled until the values are reconciled

#### Scenario: Pieces_required must not exceed pieces_placed

- **WHEN** the user sets pieces_required > pieces_placed for Triforce Hunt or Ganon Hunt
- **THEN** the UI surfaces an inline validation error and the Generate button is disabled until the values are reconciled

#### Scenario: Completionist forces accessibility to locations

- **WHEN** the user selects goal = Completionist
- **THEN** the accessibility dropdown automatically updates to `locations` and becomes read-only with an explanatory tooltip; selecting another goal restores user control

### Requirement: Preset gallery uses Settings_ApplyPreset

The settings window SHALL display the full list of presets from `SettingsPreset` (per `src/rando/rando_settings.h`) and SHALL apply a preset via `Settings_ApplyPreset` when selected. Each preset entry SHALL show its human-readable name from `Settings_PresetName`.

#### Scenario: Preset selection updates all fields

- **WHEN** the user selects a preset from the gallery
- **THEN** every widget in the settings panel updates to the preset's values, the live `settings_hash` refreshes, and the share-string preview updates accordingly

#### Scenario: Preset names match the API

- **WHEN** the gallery is rendered
- **THEN** each entry's label is exactly the string returned by `Settings_PresetName(preset)` for the corresponding preset enum value

### Requirement: Live settings hash and share-string display

The settings window SHALL display the current `settings_hash` (truncated to a hex preview) and the full encoded share string corresponding to the current widget values. Both displays SHALL update within one ImGui frame of any widget change.

#### Scenario: Hash updates on widget change

- **WHEN** the user changes any widget value
- **THEN** the displayed `settings_hash` recomputes from `Settings_ComputeHash(pending)` on the next ImGui frame and the displayed share string re-encodes accordingly

#### Scenario: Hash matches canonical bytes path

- **WHEN** the displayed hash is computed
- **THEN** it equals the value `Settings_ComputeHash` returns when called on the same `RandoSettings` struct — there is no parallel hash computation in UI code

### Requirement: Share-string copy and paste via OS clipboard

The settings window SHALL provide a "Copy share string" button that writes the current share string to the OS clipboard via `SDL_SetClipboardText`, and a "Paste share string" button (or paste-into-field) that reads via `SDL_GetClipboardText` and parses through the existing share-string decoder.

#### Scenario: Copy writes the encoded share string

- **WHEN** the user clicks Copy share string
- **THEN** the OS clipboard contains the current share-string text as returned by the existing share-string encoder

#### Scenario: Paste of a valid share string populates fields

- **WHEN** the user pastes a valid share string into the share-string field
- **THEN** the settings widgets update to reflect the decoded settings, the seed input shows the decoded `seed_u64`, and the live `settings_hash` matches the pasted share string's settings half

#### Scenario: Paste of a malformed share string surfaces inline error

- **WHEN** the user pastes a malformed or unrecognized share string
- **THEN** an inline error appears beneath the field describing the failure, the existing widget values are not modified, and the Generate button is disabled until the field is corrected or cleared

### Requirement: Asset-hash warning dialog

The settings window SHALL detect when `g_assets_hash != kVanillaAssetsHash` at the time the user clicks Generate. If no persisted decision exists for the current asset hash, the window SHALL block generation and display a modal explaining that placement may misbehave with non-vanilla asset data, with three choices: Always allow, Allow once, Cancel.

#### Scenario: Vanilla assets do not show the dialog

- **WHEN** the user clicks Generate while `g_assets_hash == kVanillaAssetsHash`
- **THEN** generation proceeds immediately with no dialog

#### Scenario: First-time non-vanilla shows the dialog

- **WHEN** the user clicks Generate while `g_assets_hash != kVanillaAssetsHash` and no decision is persisted for that hash
- **THEN** the asset-hash modal blocks generation and offers Always allow / Allow once / Cancel

#### Scenario: Always-allow persists per hash

- **WHEN** the user previously chose Always allow for the current asset hash
- **THEN** the dialog does not appear and generation proceeds; the decision is persisted in the existing `[RandoAssetDecisions]` section of `zelda3.ini` (same section already parsed by `src/config.c` and read into the in-memory lookup at `src/select_file.c`), NOT duplicated into the new `[rando_window]` section

#### Scenario: Cancel keeps the player in the settings window

- **WHEN** the user chooses Cancel
- **THEN** the modal closes, no generation runs, and the settings widgets remain at their current values

### Requirement: Settings-state bridge owns pending settings

A bridge module (`src/rando/rando_window_bridge.{c,h}`) SHALL own the canonical pending-settings struct and the generation request/status flags. The UI side SHALL only mutate the `pending` field and the generate-request flag. The game side SHALL only mutate the generate-status, in-progress, and error fields. No shared field SHALL be mutated by both sides.

#### Scenario: UI writes do not affect active slot

- **WHEN** the user edits widgets in the settings window while a randomizer slot is loaded
- **THEN** only `bridge.pending` is modified; the active slot's `g_rando.settings` is unchanged until generation completes for a NEW slot

#### Scenario: Generate request crosses the bridge

- **WHEN** the user clicks Generate
- **THEN** the UI sets `bridge.generate_requested = true` and waits for status feedback; the game side at the next `ZeldaRunFrame()` boundary consumes the request, runs generation synchronously, and writes `bridge.generate_status` accordingly

#### Scenario: Bridge survives application shutdown

- **WHEN** the application begins shutdown
- **THEN** the bridge state is not flushed to disk (only `[rando_window]` persistence applies) and any in-progress generation completes before shutdown proceeds

### Requirement: Synchronous generation blocks the UI with a modal

When the UI requests generation, the settings window SHALL display an input-blocking ImGui modal "Generating seed..." that prevents widget interaction until the game side reports completion. The game side SHALL run the existing seed-generation pipeline synchronously on the game thread.

#### Scenario: Modal appears immediately after Generate

- **WHEN** the user clicks Generate
- **THEN** within one ImGui frame the modal is visible and all other settings widgets are non-interactive

#### Scenario: Modal closes on success

- **WHEN** the game side reports `generate_status = 2` (success)
- **THEN** the modal closes, the settings widgets become interactive, and a follow-up notification offers to load the newly-created slot

#### Scenario: Modal shows error and unblocks on failure

- **WHEN** the game side reports `generate_status = -1` (error)
- **THEN** the modal text updates to show `bridge.generate_error` and an OK button closes the modal; no slot is created

### Requirement: Persistence of last settings to zelda3.ini

The settings window SHALL persist the user's most recent settings to a new `[rando_window]` section in `zelda3.ini`. Persistence SHALL use the canonical-serialized 24 bytes (the same bytes that feed `Settings_ComputeHash`), base64-encoded as `last_settings_canonical_b64`. On startup, the window SHALL preselect to those settings if present and valid; on parse failure or absence, the window SHALL preselect to `Settings_SetDefaults`.

#### Scenario: Settings round-trip across sessions

- **WHEN** the user configures settings, generates a seed, exits, and re-launches
- **THEN** the settings window opens preselected to the same widget values that were active at exit

#### Scenario: Corrupt persistence falls back to defaults

- **WHEN** `last_settings_canonical_b64` is malformed, decodes to the wrong length, or fails the canonical-serialize round-trip
- **THEN** the window preselects to `Settings_SetDefaults` and logs a one-line warning; the application does not crash or refuse to start

#### Scenario: Asset-hash decisions persist per hash

- **WHEN** the user chose Always allow for a given asset hash in a previous session
- **THEN** that decision is loaded from `[rando_window]` at startup and applied when Generate is clicked under the same asset hash

#### Scenario: Window geometry persists

- **WHEN** the user moves or resizes the settings window and exits
- **THEN** the next launch restores the same position and size, clamped to the union of currently-attached display bounds (off-screen positions are recentered)

### Requirement: Read-only spoiler and placement viewer tab

The settings window SHALL include a tab that displays the spoiler/placement of the most recently generated seed in a read-only form. The viewer SHALL show locations grouped by region with their assigned items. The viewer SHALL be hidden entirely when the most recently generated placement was created with `race_mode = true` (tracked by `bridge.last_generated_race_mode`, NOT by `bridge.pending.race_mode` which reflects what the user is editing right now).

#### Scenario: Viewer reads from Placement_GetActive

- **WHEN** the user opens the spoiler viewer tab after a successful generation
- **THEN** the viewer shows every entry returned by `Placement_GetActive()` (`src/rando/rando_placement.h:60`), grouped by region; no other placement source is consulted

#### Scenario: Viewer hidden when last generation was race-mode

- **WHEN** `bridge.last_generated_race_mode == true`
- **THEN** the spoiler viewer tab is not visible in the settings window and cannot be accessed via any UI path

#### Scenario: Pending race_mode toggle does not affect viewer visibility

- **WHEN** the user toggles `pending.race_mode` in the General panel without re-generating
- **THEN** the spoiler viewer's visibility does not change; only the next successful Generate (which writes `bridge.last_generated_race_mode`) flips the gate

#### Scenario: Viewer is read-only

- **WHEN** the viewer is open
- **THEN** no widget allows the user to modify any placement entry; the viewer is text and tabular display only

### Requirement: PC kind-toggle on file-select opens the native window

On PC builds, when the player selects an empty slot on the file-select screen and chooses "New Randomizer", the game SHALL call `RandoWindow_OpenForNewSlot(slot_index)` at each of the call sites that today invoke `SelectFile_Settings_Activate` (`src/select_file.c:1664, :1891`). The native window SHALL surface focus, populate the target slot index in the bridge, and accept settings entry as usual. On generation success, the new randomizer slot SHALL be created at the requested slot index.

#### Scenario: Kind-toggle opens the native window on PC

- **WHEN** the user selects an empty slot on PC and chooses New Randomizer
- **THEN** the native window receives OS focus via `SDL_RaiseWindow`, the bridge records the target slot index, and the in-game settings screen does not appear

#### Scenario: Generation lands in the requested slot

- **WHEN** generation completes successfully after the kind-toggle path
- **THEN** the new randomizer slot is written to the sidecar at the slot index recorded by the bridge and the file-select banner for that slot updates accordingly on the next render

#### Scenario: Cancel by closing the native window

- **WHEN** the user closes the native window via the OS close control while a kind-toggle target slot index is set, before generation completes
- **THEN** the bridge's target slot index is cleared, the file-select returns to normal slot navigation, and no sidecar slot is written; re-opening the native window does not implicitly retarget the previous slot

#### Scenario: Re-targeting a different empty slot mid-configuration

- **WHEN** the user has the native window open with target slot N and on the file-select chooses New Randomizer on a different empty slot M
- **THEN** `RandoWindow_OpenForNewSlot(M)` is called, the bridge's target slot index is overwritten with M, any in-progress settings edits are preserved (pending values are not reset), and on Generate the result lands in slot M

#### Scenario: Switch kind-toggle path is unchanged

- **WHEN** the user selects New Randomizer on Switch
- **THEN** `SelectFile_Settings_Activate` is invoked per the existing Switch flow because `Z3R_NATIVE_SETTINGS_WINDOW` is not defined; `RandoWindow_OpenForNewSlot` does not exist on Switch builds

### Requirement: Determinism contract is unchanged

The native window SHALL produce identical `settings_hash` and identical generated placement for any given `RandoSettings + seed_u64` pair compared to the in-game UI on Switch. The window SHALL NOT introduce any new RNG, new canonical-bytes layout, new hash function, or any other determinism-affecting code path.

#### Scenario: Same settings produce same hash regardless of UI

- **WHEN** the user enters a given `RandoSettings` via the native window on PC and another user enters the same `RandoSettings` via the in-game UI on Switch
- **THEN** `Settings_ComputeHash` returns the same 32-byte value on both platforms

#### Scenario: Same share string produces same placement regardless of UI

- **WHEN** the user pastes the same share string on PC (native window) and on Switch (in-game UI), and clicks Generate on both
- **THEN** the resulting placements are byte-identical when compared via the regression-corpus comparator

#### Scenario: Decode-encode round trip is identity for every preset

- **WHEN** for each preset `p` in `SettingsPreset`, the code calls `Settings_ApplyPreset(p, &s)`, encodes the result through the share-string encoder to produce `share_p`, and decodes `share_p` back through the share-string decoder to produce `s'`
- **THEN** `s` and `s'` are byte-identical for every field — including fields like `pieces_required` and `pieces_placed` that are meaningless when goal is not Triforce Hunt or Ganon Hunt; the native window MUST NOT zero or normalize such fields differently from the Switch in-game UI

#### Scenario: Native window does not introduce a new RNG, hash, or serialization path

- **WHEN** auditing the native-window codebase (`src/rando/rando_window/`, the bridge, the ImGui wrapper)
- **THEN** no new SHA-256 implementation, no new canonical-serialization routine, no new share-string encoder/decoder, and no new RNG call exists; all such operations route through the existing `Settings_*` and `Share_*` APIs

### Requirement: ImGui code does not write to g_ram

No code under `third_party/imgui/`, `src/rando/rando_window/`, or the ImGui wrapper SHALL write to any byte of `g_ram` or any address-mapped macro defined in `src/variables.h`. All game-state mutation SHALL go through the bridge and the existing game-side functions (`Rando_*`, `Settings_*`, `g_rando.*`).

#### Scenario: Audit guard sees no new g_ram writes from window code

- **WHEN** the post-merge audit guard runs against the new files
- **THEN** it finds zero direct or indirect writes to `g_ram` from `third_party/imgui/`, `src/rando/rando_window/`, or any new file added by this change
