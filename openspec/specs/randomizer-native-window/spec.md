# randomizer-native-window Specification

## Purpose
On PC builds (Windows, Linux, macOS), the randomizer settings UI lives in a dedicated second OS-native window rendered with Dear ImGui, distinct from the game window and owning its own OpenGL context. This capability covers that window's lifecycle and event pump, the ImGui settings panels (at parity with the in-game SNES settings screen, which is compiled out on PC and retained only for Switch), the preset gallery, live settings-hash / share-string display with clipboard copy-paste, the asset-hash warning dialog, the recommended-features opt-in, the settings-state bridge that carries pending settings and the last-generated placement across the UI↔game-frame boundary, synchronous slot generation behind a modal, sidecar-INI persistence of last settings, and a read-only spoiler/hints viewer that respects race-mode suppression. The window never writes to `g_ram` and does not change the generator's determinism contract.
## Requirements
### Requirement: Second OS window for randomizer settings on PC

The host SHALL create a second OS-native window dedicated to randomizer settings on PC builds (Windows, Linux, macOS), distinct from the game window. The window SHALL be created **hidden** at application startup and SHALL remain hidden until the player chooses "New Randomizer" on the file-select kind-toggle (or re-opens it via a hotkey / game menu), at which point it is shown and raised. Vanilla players who never choose New Randomizer SHALL never see the second window.

#### Scenario: Settings window is hidden at startup

- **WHEN** the user launches the application on a PC build and has not chosen New Randomizer
- **THEN** only the game window is visible; the settings window has been created but is hidden (no second window appears on the desktop)

#### Scenario: New Randomizer shows and raises the settings window

- **WHEN** the player selects an empty slot on the file-select and chooses New Randomizer (or invokes the re-open hotkey/menu)
- **THEN** the settings window is shown and raised via `SDL_ShowWindow` + `SDL_RaiseWindow`, independently movable, resizable, and minimizable

#### Scenario: Closing the settings window does not close the game

- **WHEN** the user closes the settings window via the OS window-close control
- **THEN** the settings window hides (does not destroy the underlying SDL window) and the game continues running unaffected; it can be shown again via New Randomizer or the re-open hotkey/menu

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

### Requirement: ImGui-rendered settings panels at parity with the in-game screen

The settings window SHALL render the settings surface **at parity with the in-game settings screen's axis set** (NOT every `RandoSettings` field). The live-editable axes are exactly the ones the in-game `kRow_*` rows expose. Enum-typed fields SHALL render as dropdowns (combos) whose option labels match the canonical CLI key grammar exactly (e.g., `fast_ganon`, `triforce-hunt`, `NoGlitches` — the same strings accepted by `Settings_ParseCsv`). Integer-typed fields SHALL render as integer input widgets with the documented valid ranges enforced. Boolean fields SHALL render as checkboxes. Axes the in-game screen exposes only as disabled "coming soon" placeholders (matching its `kRow_*_Disabled` rows) SHALL render disabled here too; pinned axes (logic, tricks) MAY render read-only.

#### Scenario: Live axes match the in-game screen; disabled/pinned axes are visibly non-editable

- **WHEN** the user opens the settings window
- **THEN** live-editable widgets are present for exactly the in-game screen's axis set plus PC-native shuffle panels: world_state, goal, crystals_ganon, crystals_tower, item_pool_difficulty, dungeon_small_keys_mode, dungeon_big_keys_mode, dungeon_maps_mode, dungeon_compasses_mode, pieces_required, pieces_placed, mode_weapons, accessibility, prize_shuffle, medallion_shuffle, race_mode, hints, traps, entrance shuffle axes, drop_shuffle, boss_shuffle, enemy_shuffle, and door_shuffle
- **AND** traps render as a live combo with `off`, `low`, `medium`, and `high`; glitches remain disabled/read-only until their runtime is implemented
- **AND** retired compatibility axes such as `region_boss_hearts_in_pool` and `pyramid_bow_upgrade` are not rendered as controls; live axes render with the supported option set
- **AND** the disabled state is not just visual: attempting to set a disabled/out-of-range field via clipboard paste of a share string SHALL produce the inline error described in `randomizer-core` and SHALL NOT mutate the field

#### Scenario: Common dungeon-item preset is available

- **WHEN** the user clicks the dungeon panel's "Dungeon keys / wild maps" preset
- **THEN** `dungeon_small_keys_mode` and `dungeon_big_keys_mode` are set to `dungeon`
- **AND** `dungeon_maps_mode` and `dungeon_compasses_mode` are set to `wild`
- **AND** no additional serialized setting is introduced; the preset only edits the existing four dungeon-item axes

#### Scenario: Forced dungeon key modes are visible

- **WHEN** door shuffle is effective for the pending settings
- **THEN** the small-key and big-key controls show `dungeon` as read-only with a "forced by door shuffle" indicator
- **WHEN** Retro world state is effective
- **THEN** the small-key control shows `wild` as read-only with a "forced by Retro" indicator

#### Scenario: Triforce-Hunt piece fields are visible only for relevant goals

- **WHEN** the user selects a goal other than Triforce Hunt or Ganon Hunt
- **THEN** the pieces_required and pieces_placed fields are visually disabled or hidden because they have no effect on placement

#### Scenario: Crystals_tower and crystals_ganon are independent (no cross-constraint)

- **WHEN** the user sets crystals_tower greater than crystals_ganon for any goal (including Fast Ganon or Ganon Hunt)
- **THEN** the UI does NOT surface a validation error and Generate stays enabled — the Ganon's-Tower entry crystal gate is independent of the Ganon-vulnerability gate, and the in-game settings screen, the CLI, and `Goal_IsCompletable` all permit it, so the native window matches that behavior (a cross-constraint here would falsely block valid seeds)

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

#### Scenario: Quick presets edit existing axes only

- **WHEN** the user clicks "Open Fast Ganon"
- **THEN** the core progression axes reset to Open / Fast Ganon defaults without changing Seed QoL, Customizer mode, dungeon-item modes, or shuffle-panel axes
- **WHEN** the user clicks "Race-safe"
- **THEN** race_mode is enabled, hints remain enabled, item_pool is Normal, Customizer mode is disabled, and the Dungeon keys / wild maps dungeon-item preset is applied
- **AND** neither quick preset introduces any new serialized setting

### Requirement: Live settings hash and share-string display

The settings window SHALL display the current `settings_hash` (truncated to a hex preview) and the full encoded share string corresponding to the current widget values. Both displays SHALL update within one ImGui frame of any widget change.

#### Scenario: Hash updates on widget change

- **WHEN** the user changes any widget value
- **THEN** the displayed `settings_hash` recomputes from `Settings_ComputeHash(pending)` on the next ImGui frame and the displayed share string re-encodes accordingly

#### Scenario: Hash matches canonical bytes path

- **WHEN** the displayed hash is computed
- **THEN** it equals the value `Settings_ComputeHash` returns when called on the same `RandoSettings` struct — there is no parallel hash computation in UI code

### Requirement: Share-string copy and paste via OS clipboard

The settings window SHALL provide a "Copy share string" button that writes the current share string to the OS clipboard via `SDL_SetClipboardText`, and a "Paste share string" button that reads via `SDL_GetClipboardText` and parses through the shared share-string decoder (`randomizer-core / Share-string format`).

**Copy** SHALL emit the **v2** string encoding the current widget values (`Settings_CanonicalSerialize(pending)`) plus the current seed — except when `pending.customizer_active` is set, where copy SHALL emit the **v1** string (seed + hash; customizer placements depend on a local manifest file that no share string can carry until the deferred `customizer_seed` encoding lands — `add-rando-customizer-mode` tasks §6.4). The live share-string display SHALL show the same string copy would emit.

When "Randomize seed each generate" is enabled, the pre-generate Copy button SHALL be disabled because the seed is not chosen until Generate. After a successful generation, the result popup's Copy button SHALL copy the generated seed's v2 exchange string (or v1 for customizer), and unchecking randomize-each-generate / pasting / typing a seed SHALL re-enable the regular Copy button for that pinned seed.

**Paste of a v2 string** SHALL restore ALL settings widgets via `Settings_CanonicalDeserialize`, adopt the decoded `seed_u64`, and pin the seed (clear the randomize-each-generate flag), so that pressing Generate reproduces the sharer's seed. Restored settings SHALL pass the window's cross-field validation (`RandoWindowBridge_Validate`); a validation failure SHALL refuse the paste with the validator's message and leave all widgets untouched. A v2 string whose canonical `customizer_active` bit is set SHALL be refused with a message naming the manifest as the reason (no adoption). A v2 string whose embedded `generator_version` differs from the binary's `kGeneratorVersion` SHALL still restore (settings layout is append-only) but SHALL surface a visible warning that Generate may produce a different placement than the sharer's; a v2 string refused by the decoder ("newer version" `settings_len`) SHALL leave widgets untouched.

**Paste of a v1 string** SHALL keep the legacy behavior: adopt the seed, pin it, and show the settings-mismatch warning when the current settings hash differs from the embedded hash (v1 cannot restore settings).

#### Scenario: Copy writes the v2 share string
- **WHEN** the user clicks Copy share string with `customizer_active` off and the seed pinned
- **THEN** the OS clipboard contains the v2 token encoding the current widget values and seed, identical to the displayed share string

#### Scenario: Auto-randomized seed copies from the result popup
- **WHEN** "Randomize seed each generate" is enabled before generation
- **THEN** the pre-generate Copy button is disabled, and after Generate succeeds the result popup's Copy button copies the generated seed's share string

#### Scenario: v2 paste restores settings and seed losslessly
- **WHEN** the user pastes a valid v2 share string produced by the same `kGeneratorVersion`
- **THEN** every settings widget updates to the decoded settings, the seed input shows the decoded `seed_u64`, the randomize-each-generate flag is cleared, the live `settings_hash` equals `Settings_HashShort` of the decoded settings, and pressing Generate reproduces the sharer's placement

#### Scenario: v1 paste keeps seed-only adoption with warning
- **WHEN** the user pastes a valid v1 (50-char) share string while the current settings' hash differs from the embedded hash
- **THEN** only the seed is adopted (and pinned) and an inline warning states that the settings could not be restored from a v1 string and do not match

#### Scenario: Version-mismatch v2 paste warns
- **WHEN** the user pastes a valid v2 string whose `generator_version` byte differs from the binary's `kGeneratorVersion`
- **THEN** settings and seed are restored and a visible warning states the string came from a different version and Generate may produce a different seed than the sharer's

#### Scenario: Customizer-bearing v2 paste is refused
- **WHEN** the user pastes a v2 string whose canonical `customizer_active` bit is set
- **THEN** no settings or seed are adopted and the inline error explains that customizer seeds require their manifest file and cannot be restored from a share string

#### Scenario: Copy under customizer falls back to v1
- **WHEN** the user clicks Copy share string while `customizer_active` is set
- **THEN** the clipboard receives the v1 (seed + hash) string — today's customizer-sharing behavior, unchanged until `customizer_seed` share encoding lands

#### Scenario: Malformed paste leaves state untouched
- **WHEN** the user pastes a malformed, corrupted, alttpr.com-format, or newer-version string
- **THEN** an inline error describes the specific failure and no widget, seed, or flag changes

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

### Requirement: Recommended-features opt-in renders in the native window on PC

The recommended-features opt-in (which writes `g_config.features0` at generate time) is reachable in-game only through the settings screen, which is compile-guarded out on PC. On PC builds the settings window SHALL therefore render the recommended-features opt-in itself. The UI SHALL edit only a bridge snapshot (`pending_recommended_features0`), taken from `g_config.features0` at window-open; the game thread SHALL apply the snapshot to `g_config.features0` only inside the generate consumer. The opt-in SHALL be determinism-neutral: `features0` is not part of `settings_hash`. On Switch the in-game recommended-features panel is retained and the native window does not exist. The PC Seed QoL tab MAY include PC-only per-slot feature bits that do not fit the fixed-height in-game panel.

#### Scenario: Recommended-features opt-in is present on PC

- **WHEN** the user opens the settings window on a PC build during new-slot creation
- **THEN** a recommended-features opt-in is rendered, bound to `bridge.pending_recommended_features0`, covering the in-game `kRecRowBits[]`/`kRecRowLabels[]` set plus the PC-only `kFeatures0_RestoreJpGlitches` slot toggle

#### Scenario: UI never writes g_config.features0 directly

- **WHEN** the user toggles a recommended-features option
- **THEN** only `bridge.pending_recommended_features0` changes; `g_config.features0` is unchanged until the game thread applies it inside the generate consumer at Generate time

#### Scenario: Recommended actions preserve optional choices unless asked to reset

- **WHEN** the user clicks "Apply recommended set"
- **THEN** every Seed QoL bit is set to its recommended on/off value

#### Scenario: JP gameplay glitches are a per-slot QoL choice

- **WHEN** the pending seed does not assume restored JP glitches
- **THEN** the "Restore JP 1.0 glitches" Seed QoL toggle can be turned on or off and is recommended off
- **AND** it controls `kFeatures0_RestoreJpGlitches` only; it does not enable `kFeatures0_JpOverworldMusic`
- **WHEN** the pending seed assumes restored JP glitches (`logic >= OverworldGlitches` or `tricks=fake-flippers`)
- **THEN** the toggle is shown forced-on because generation and slot activation must enable `kFeatures0_RestoreJpGlitches`

#### Scenario: Recommended-features choice does not affect the hash or share string

- **WHEN** the user changes a recommended-features option
- **THEN** the displayed `settings_hash` and share string are unchanged, because `features0` is not part of the canonical settings bytes

### Requirement: Settings-state bridge owns pending settings

A bridge module (`src/rando/rando_window_bridge.{c,h}`) SHALL own the canonical pending-settings struct and the generation request/status flags. The UI side SHALL only mutate the `pending` field and the generate-request flag. The game side SHALL only mutate the generate-status, in-progress, and error fields. No shared field SHALL be mutated by both sides.

#### Scenario: UI writes do not affect active slot

- **WHEN** the user edits widgets in the settings window while a randomizer slot is loaded
- **THEN** only `bridge.pending` is modified; the active slot's `g_rando.settings` is unchanged until generation completes for a NEW slot

#### Scenario: Generating while another slot is active restores the active slot's runtime state

- **WHEN** a randomizer slot is active and the user generates a NEW slot from the native window
- **THEN** after the generate consumer finishes, the active slot's hints, entrance/door logic overlays, and prize/medallion/boss shuffle assignments are reinstalled (the generation pipeline transiently reuses the global hint/overlay/assignment tables), so in-game hint tiles and the trackers keep reflecting the active seed

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

### Requirement: Persistence of last settings to a sidecar INI

The settings window SHALL persist the user's most recent settings to a `[rando_window]` section in a **sidecar `saves/rando_window.ini`** (it SHALL NOT rewrite the user's hand-edited `zelda3.ini`). Persistence SHALL use the canonical-serialized `kSettingsCanonicalLen` bytes (28; the same bytes that feed `Settings_ComputeHash`), **hex**-encoded as `last_settings_canonical_hex` (56 hex chars). The sidecar SHALL be loaded at startup via a whitelisting loader that parses only `[rando_window]` and `[RandoAssetDecisions]`. On startup, the window SHALL preselect to those settings if present and valid; on parse failure or absence, the window SHALL preselect to `Settings_SetDefaults`.

#### Scenario: Settings round-trip across sessions

- **WHEN** the user configures settings, generates a seed, exits, and re-launches
- **THEN** the settings window opens preselected to the same widget values that were active at exit, restored from `saves/rando_window.ini`

#### Scenario: Corrupt persistence falls back to defaults

- **WHEN** `last_settings_canonical_hex` is malformed, decodes to a length other than `kSettingsCanonicalLen` (28) bytes, or fails the canonical-serialize round-trip
- **THEN** the window preselects to `Settings_SetDefaults` and logs a one-line warning; the application does not crash or refuse to start

#### Scenario: User's zelda3.ini is never rewritten

- **WHEN** the application persists settings on shutdown
- **THEN** only the sidecar `saves/rando_window.ini` is written; the user's hand-edited `zelda3.ini` (keymaps, comments, foreign sections) is left untouched

#### Scenario: Asset-hash decisions persist per hash

- **WHEN** the user chose Always allow for a given asset hash in a previous session
- **THEN** that decision is loaded from the `[RandoAssetDecisions]` section (in the sidecar and/or `zelda3.ini`, union) at startup and applied when Generate is clicked under the same asset hash; it is NOT stored under `[rando_window]`

#### Scenario: Window geometry persists

- **WHEN** the user moves or resizes the settings window and exits
- **THEN** the next launch restores the same position and size, clamped to the union of currently-attached display bounds (off-screen positions are recentered)

#### Scenario: Tracker tiled layout can be applied at startup

- **WHEN** the user enables the Trackers tab's "Apply at startup" tiled-layout preference and exits cleanly
- **THEN** `saves/rando_window.ini` persists that preference under `[rando_window]`
- **AND** the next launch opens the Check, Map, and Item tracker windows and tiles them around the game window on the current display
- **WHEN** the user clicks "Apply tiled layout" during a session
- **THEN** the same layout is applied immediately without changing seed settings, settings hash, or the share string

### Requirement: Read-only spoiler and placement viewer tab

The settings window SHALL include a tab that displays the spoiler/placement of the most recently generated seed in a read-only form, read from the bridge's own owned copy of the just-generated placement (`bridge.last_generated_placement` / `bridge.last_generated_spheres`), NOT from `Placement_GetActive()`. The viewer SHALL show locations grouped by region with their assigned items. The viewer SHALL be hidden entirely when the most recently generated placement was created with `race_mode = true` (tracked by `bridge.last_generated_race_mode`, NOT by `bridge.pending.race_mode` which reflects what the user is editing right now).

#### Scenario: Viewer reads from the bridge's owned placement copy

- **WHEN** the user opens the spoiler viewer tab after a successful generation
- **THEN** the viewer shows every entry from `bridge.last_generated_placement` (the caller-owned copy returned by `Rando_GenerateSlot`), grouped by region; it does NOT read `Placement_GetActive()`, which is populated only at slot load and would be stale right after generating

#### Scenario: Viewer hidden when last generation was race-mode

- **WHEN** `bridge.last_generated_race_mode == true`
- **THEN** the spoiler viewer tab is not visible in the settings window and cannot be accessed via any UI path

#### Scenario: Pending race_mode toggle does not affect viewer visibility

- **WHEN** the user toggles `pending.race_mode` in the General panel without re-generating
- **THEN** the spoiler viewer's visibility does not change; only the next successful Generate (which writes `bridge.last_generated_race_mode`) flips the gate

#### Scenario: Viewer is read-only

- **WHEN** the viewer is open
- **THEN** no widget allows the user to modify any placement entry; the viewer is text and tabular display only

### Requirement: Hints viewer respects race-mode suppression

The settings window's read-only Hints tab lists every hint for the active slot, and hints can name item/location information. This viewer dumps all hints at once (unlike in-game telepathic tiles, which the player is meant to discover by playing), so when the active slot's settings have `race_mode = true` (`Rando_GetActiveSettings()->race_mode`) it SHALL NOT render hint text. Unlike the spoiler viewer (hidden entirely), the Hints tab SHALL remain visible but suppress its spoiling content: it shows the hint count and a race-mode indicator, but no hint strings. The panel SHALL NOT surface a reveal call-to-action — the spoiler is intentionally unavailable during a race. (After the race, the hints remain recoverable out-of-band via the reveal flow: `Rando_RevealSpoiler` regenerates the full JSON spoiler, including the `hints[]` array, with `race_mode` cleared.)

#### Scenario: Hint text hidden in race mode

- **WHEN** the active slot was generated with `race_mode = true` and the user opens the Hints tab
- **THEN** the tab shows the hint count with a "(race mode)" indicator and a note that hint text is hidden so it cannot spoil item locations, and renders no hint string

#### Scenario: Hint text shown outside race mode

- **WHEN** the active slot's `race_mode = false`
- **THEN** the Hints tab renders each NPC's hint string in full

### Requirement: PC kind-toggle on file-select opens the native window

On PC builds, the file-select kind-picker (Vanilla / New-Randomizer / From-share chooser) stays compiled, but its two rando branches redirect to the native window under `Z3R_NATIVE_SETTINGS_WINDOW`. When the player selects an empty slot and chooses "New Randomizer" (the `SelectFile_Settings_Activate` call site at `src/select_file.c:1703`) or "From share string" (which on Switch activates the in-game alphabet picker), the game SHALL call `RandoWindow_OpenForNewSlot(slot_index)` instead. The native window SHALL surface focus (show + raise the startup-hidden window), populate the target slot index in the bridge, and accept settings entry (and share-string paste) as usual. On generation success, the new randomizer slot SHALL be created at the requested slot index. The post-decode `SelectFile_Settings_Activate` call at `src/select_file.c:1990` lives inside the alphabet-picker update flow (part of the guarded-out text-input layer) and therefore compiles out on PC with no redirect; the decoded-seed handoff is moot on PC because the user pastes the share string directly into the native window.

#### Scenario: Kind-toggle opens the native window on PC

- **WHEN** the user selects an empty slot on PC and chooses New Randomizer
- **THEN** the native window is shown and raised via `SDL_ShowWindow` + `SDL_RaiseWindow`, the bridge records the target slot index, and the in-game settings screen does not appear

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

### Requirement: Generate confirmation on pasted-settings mismatch

The settings window SHALL record the settings hash associated with the most recent successful share-string paste (v1: the embedded `settings_hash`; v2: the hash recomputed from the restored settings). When the user activates Generate while a pasted hash is recorded and differs from the current settings' hash, the window SHALL interpose a confirmation modal stating that the settings no longer match the pasted share string and that generating now produces a different seed, with explicit Generate-anyway and Cancel choices. Confirming SHALL proceed with generation and clear the recorded paste; cancelling SHALL return to the window unchanged; a subsequent paste SHALL re-arm the check with the new hash. The modal copy SHALL be one to two short, durable player-facts (per the project's UI-brevity feedback rule).

#### Scenario: Mismatched Generate is interrupted loudly
- **WHEN** the user pastes a share string and then changes any settings widget so the current hash differs, and presses Generate
- **THEN** the confirmation modal appears before any generation starts, and Cancel leaves settings, seed, and slots untouched

#### Scenario: Confirming generates once and disarms
- **WHEN** the user chooses Generate-anyway in the modal
- **THEN** generation proceeds with the current (edited) settings and the recorded paste is cleared, so the next Generate with unchanged settings does not re-prompt

#### Scenario: Matching settings generate without interruption
- **WHEN** the user pastes a v2 string and presses Generate without editing settings
- **THEN** no modal appears (the hashes match by construction) and generation reproduces the pasted seed

### Requirement: Enemy-shuffle live settings control

The native settings window SHALL render `enemy_shuffle` as a **live checkbox** in the rando-settings panel. Toggling it SHALL update the pending settings, the live `settings_hash`, and the share string exactly as the other shuffle checkboxes do. The control SHALL ship live only when the runtime substitution is actually installed for playable slots (`Rando_ActivateSidecarSlot` regenerates the enemy substitution), so the widget never lies about an inert axis. PC native window only; the in-game screen stays compiled out on PC.

#### Scenario: Enemy-shuffle checkbox is live and reflected in the share string
- **WHEN** the user toggles the enemy-shuffle checkbox in the native settings window
- **THEN** the pending settings update, the displayed `settings_hash` / share string change, and generating a slot with it on produces a runtime-active enemy substitution

#### Scenario: Tooltip is a durable player-fact
- **WHEN** the user hovers the enemy-shuffle checkbox
- **THEN** the tooltip states a concise durable fact (e.g. "Randomizes which enemies appear in each room") with no status, caveat, or implementation detail

