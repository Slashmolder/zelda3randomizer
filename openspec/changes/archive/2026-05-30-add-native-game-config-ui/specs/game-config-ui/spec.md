# game-config-ui

## ADDED Requirements

### Requirement: Native game-configuration surface
The native settings window SHALL provide a "Game Settings" tab group with panels for keyboard controls, controller (gamepad) mapping, video, audio, and gameplay-feature configuration, so a PC player can configure the game without hand-editing `zelda3.ini`. The panels SHALL be compiled only on PC (`Z3R_NATIVE_SETTINGS_WINDOW`); Switch retains its existing in-game configuration paths.

#### Scenario: Opening the configuration window
- **WHEN** the player presses the `OpenSettings` command key (default backquote `` ` ``)
- **THEN** the settings window is shown in configuration mode with the "Game Settings" tab focused, no randomizer slot targeted, and the seed-generation controls hidden
- **AND** pressing the key again hides the window

#### Scenario: Switch build excludes the panels
- **WHEN** the project is built with `Z3R_NATIVE_SETTINGS_WINDOW` undefined
- **THEN** the game-config panel translation unit and the `OpenSettings` window calls are excluded, and the build succeeds

### Requirement: Editable keybinding model with live rebuild
Keyboard and gamepad bindings SHALL be stored in editable per-command arrays that are the source of truth, with the runtime lookup hashes derived from them. Rebinding SHALL take effect without restarting the game.

#### Scenario: Rebind a keyboard action live
- **WHEN** the player rebinds a command to a new key and applies the change
- **THEN** the new key triggers the command on its next press and the previous key no longer does, with no restart

#### Scenario: Conflict is resolved by stealing with a warning
- **WHEN** the player binds a key (or gamepad button/combo) already assigned to a different command
- **THEN** the previous owner is unbound, a warning identifies what was unbound, and the resulting keymap contains no duplicate binding

#### Scenario: Gamepad combo capture
- **WHEN** the player rebinds a command while holding one or more modifier buttons and pressing a main button
- **THEN** the binding is stored as that main button plus the held buttons as its combo modifiers, matching the runtime gamepad lookup

### Requirement: Comment-preserving in-place INI persistence
Applying configuration changes SHALL persist them by rewriting only the managed keys in the loaded INI file in place, preserving comments, blank lines, key order, unknown keys, and unknown sections verbatim, and writing atomically. The target SHALL be the file that was loaded at startup (`zelda3.user.ini` when present, otherwise `zelda3.ini`).

#### Scenario: Hand-edited content is preserved
- **WHEN** the loaded INI contains comments, an `!include` directive, an unknown section, and `[Randomizer]`/`[RandoAssetDecisions]` content, and the player changes one managed setting
- **THEN** only the changed managed key's value differs in the written file and all other bytes (comments, unknown sections, randomizer sections) are unchanged
- **AND** a one-time backup of the original file is created before the first rewrite of the session

#### Scenario: Bootstrap when no INI exists
- **WHEN** neither `zelda3.user.ini` nor `zelda3.ini` exists and the player applies a change
- **THEN** a new `zelda3.ini` is created containing the managed sections, and it re-parses to the applied configuration

#### Scenario: Round-trip fidelity
- **WHEN** the current configuration and bindings are written and then re-parsed
- **THEN** the managed scalar fields and all bindings are identical to the values written

### Requirement: Live-apply versus restart policy
Each managed setting SHALL be applied immediately when it is safe and cheap to do so (key/gamepad bindings, gameplay `features0` bits, window scale, windowed/desktop fullscreen, the new-renderer toggle, MSU volume, and values the game reads each frame); all other settings SHALL be persisted immediately but take effect on the next launch, and the UI SHALL clearly mark such settings and surface a restart notice after applying them.

#### Scenario: Gameplay feature toggles live
- **WHEN** the player toggles a `features0` gameplay bit and applies
- **THEN** the change is reflected in the running game without restart

#### Scenario: Restart-required setting is labeled and persisted
- **WHEN** the player changes a restart-only setting (e.g. audio frequency or output method) and applies
- **THEN** the UI shows a restart-required notice, the INI is updated immediately, and the change takes effect after relaunch

#### Scenario: Live apply is deferred during replay
- **WHEN** a snapshot replay is in progress and the player applies a change to a gameplay feature or a key binding
- **THEN** the change is persisted to the INI immediately but the live application is deferred (it does not take effect mid-replay), so the replay stream is not desynchronized

### Requirement: Determinism and randomizer isolation
The game-config UI SHALL NOT alter randomizer settings, the canonical settings serialization, or the generator version, and SHALL NOT write any save, sidecar, or snapshot file other than the user INI.

#### Scenario: Randomizer generation unaffected
- **WHEN** the player uses the game-config tabs and then generates a randomizer seed in the same session
- **THEN** generation behavior, the settings hash, and the regression corpus / self-test results are unchanged
