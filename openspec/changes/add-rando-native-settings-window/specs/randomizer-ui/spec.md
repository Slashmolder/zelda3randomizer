## MODIFIED Requirements

### Requirement: Text-input infrastructure

On Switch builds, the host SHALL provide a single-line text-input widget (`RandoTextField`) sourcing characters from libnx `swkbd`. The widget SHALL support cursor positioning, backspace, paste from clipboard, and a constrained character set (the base32 alphabet plus separators).

On PC builds (Windows, Linux, macOS) where `Z3R_NATIVE_SETTINGS_WINDOW` is defined, this requirement does NOT apply: text input occurs in the native settings window via OS-standard ImGui text widgets (see `randomizer-native-window / Second OS window for randomizer settings on PC`), and `RandoTextField` is excluded from the build.

#### Scenario: Switch swkbd is invoked

- **WHEN** the settings screen is active on the Switch build and the player activates the share-string field
- **THEN** libnx `swkbd` appears and its result is routed into the widget buffer

#### Scenario: Switch swkbd dismissed without confirming

- **WHEN** the player dismisses libnx `swkbd` without confirming (e.g., pressing B / Cancel)
- **THEN** the widget buffer is left untouched (no partial write), focus returns to the on-screen alphabet picker, and the settings hash is unchanged

#### Scenario: Switch paste from clipboard

- **WHEN** the player triggers the Switch paste shortcut and the clipboard contains a valid share string
- **THEN** the buffer is replaced with the clipboard content and the live settings hash updates

#### Scenario: PC build excludes RandoTextField

- **WHEN** the project is built for Windows, Linux, or macOS with `Z3R_NATIVE_SETTINGS_WINDOW=1`
- **THEN** `rando_textfield.{c,h}` is not compiled into the binary and no in-game text-input widget exists in the build

### Requirement: On-screen alphabet picker

On Switch builds, the settings screen SHALL provide an on-screen alphabet picker as a controller-friendly alternative to keyboard typing. The picker SHALL be the default focus on the Switch undocked build.

On PC builds where `Z3R_NATIVE_SETTINGS_WINDOW` is defined, this requirement does NOT apply: text entry occurs in the native settings window with OS keyboard input, and the on-screen alphabet picker is excluded from the build.

#### Scenario: Switch picker operable with d-pad and two buttons

- **WHEN** the picker is focused on a Switch build
- **THEN** d-pad navigation moves the cursor, A confirms a character, B deletes the last character

#### Scenario: Default focus on Switch handheld

- **WHEN** the settings screen opens on the Switch build in handheld mode
- **THEN** the picker is the focused widget

#### Scenario: PC build has no on-screen alphabet picker

- **WHEN** the project is built for Windows, Linux, or macOS with `Z3R_NATIVE_SETTINGS_WINDOW=1`
- **THEN** no on-screen alphabet picker is reachable from any in-game UI path because the settings screen is excluded from the build

### Requirement: Settings screen

On Switch builds, the in-game settings screen SHALL allow the player to select world-state, goal, item-pool difficulty, dungeon-item mode, Triforce-Hunt piece counts (when goal is Triforce Hunt), and optional shuffle modules; SHALL accept seed entry via random generation or share-string paste; and SHALL display the current settings hash live.

On PC builds where `Z3R_NATIVE_SETTINGS_WINDOW` is defined, this requirement does NOT apply: the equivalent functionality is provided by the native settings window per `randomizer-native-window`, and the in-game settings screen module is excluded from the build. The kind-toggle call sites at `src/select_file.c:1664` and `:1891` (today invoking the file-static `SelectFile_Settings_Activate`) instead call `RandoWindow_OpenForNewSlot` under the same guard.

#### Scenario: Switch preset application

- **WHEN** the player selects a built-in preset on the Switch settings screen
- **THEN** all individual settings update and the settings hash refreshes

#### Scenario: Switch share-string paste populates fields

- **WHEN** the player pastes a valid share string on the Switch settings screen
- **THEN** the settings panel updates to reflect the decoded settings and the seed field shows the decoded `seed_u64`

#### Scenario: Switch invalid share string surfaces inline error

- **WHEN** the player pastes a malformed or alttpr.com-format share string on the Switch settings screen
- **THEN** an inline error appears and generation is blocked until the input is corrected or cleared

#### Scenario: Switch non-vanilla asset data triggers a one-time dialog per hash

- **WHEN** the player attempts to start generation on Switch while `g_assets_hash != kVanillaAssetsHash` and no persisted decision exists for this hash
- **THEN** the Switch settings screen displays a dialog explaining the risk with three choices (Always allow / Allow once / Cancel); the chosen action is honored and "Always allow" persists keyed by hash

#### Scenario: PC build excludes the in-game settings screen

- **WHEN** the project is built for Windows, Linux, or macOS with `Z3R_NATIVE_SETTINGS_WINDOW=1`
- **THEN** no in-game settings-screen tile rendering, navigation, or input handling is compiled into the binary; the call sites at `src/select_file.c:1664` and `:1891` invoke `RandoWindow_OpenForNewSlot` instead of `SelectFile_Settings_Activate` and the in-game module is never reached

#### Scenario: PC build preserves Rando_RegisterAssetDecisionFromIni

- **WHEN** the project is built for Windows, Linux, or macOS with `Z3R_NATIVE_SETTINGS_WINDOW=1`
- **THEN** the externally-visible function `Rando_RegisterAssetDecisionFromIni` (`src/select_file.c:2279`, called from `src/config.c:544`) remains compiled into the binary because the INI parser depends on it; it is NOT inside the compile-out region of the in-game settings screen

#### Scenario: PC build preserves SelectFile_Settings_Deactivate as a callable no-op or stub

- **WHEN** the project is built for Windows, Linux, or macOS with `Z3R_NATIVE_SETTINGS_WINDOW=1`
- **THEN** `SelectFile_Settings_Deactivate` (called from `src/select_file.c:450, :3063, :3441` for file-select state reset, unrelated to opening the settings screen) remains callable — either as an empty stub under the guard or by leaving the function definition outside the compile-out region; removing it produces unresolved-reference link errors
