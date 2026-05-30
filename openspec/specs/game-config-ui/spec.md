# game-config-ui Specification

## Purpose
TBD - created by archiving change add-native-game-config-ui. Update Purpose after archive.
## Requirements
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

### Requirement: Debug tab live inventory editor
The native settings window SHALL provide a top-level "Debug" tab (rendered after "Game Settings" and "Randomizer", PC-only under `Z3R_NATIVE_SETTINGS_WINDOW`) that reads and edits Link's inventory, equipment, and consumables by reading/writing the canonical `$7EF3xx` (`g_ram`) save block at runtime. The editor is distinct from the configuration tabs' Apply/INI flow: it does not persist to `zelda3.ini` or any save file — edits land in live RAM and reach SRAM only if the player saves normally in-game. Every widget reads `g_ram` live for display and writes on change, so edits reflect on the HUD on the next frame.

#### Scenario: Editing a consumable reflects on the HUD next frame
- **WHEN** a save is loaded, Link is in a gameplay/menu module, and the player changes rupees, bombs, arrows, keys, magic, hearts, or heart-pieces in the Debug tab
- **THEN** the corresponding `g_ram` cell(s) are written and the HUD shows the new value on the next frame, with no INI write and no save-file change

#### Scenario: Debug tab is excluded on Switch
- **WHEN** the project is built with `Z3R_NATIVE_SETTINGS_WINDOW` undefined
- **THEN** the Debug tab and its `g_ram` read/write helpers are excluded and the build succeeds

### Requirement: Debug edits are hard-gated to safe contexts
All Debug-tab writes SHALL be gated by a predicate that permits editing only when a save is loaded and Link is in a stable gameplay or menu module, and that forbids editing during snapshot/input replay and whenever the original ROM is attached for side-by-side RAM comparison. A `ZeldaIsEmulatorAttached()` accessor SHALL report the last condition. When the gate is closed the tab SHALL render disabled with the reason shown; reads still display current bytes. Write helpers SHALL re-check the gate (defense in depth) and clamp every value to its valid range so no out-of-range byte can reach a game-code table index.

#### Scenario: Disabled outside a loaded gameplay state
- **WHEN** the game is at the title screen, file select, or a death/cutscene/transition module
- **THEN** the Debug tab is disabled with an explanatory banner ("Load a save and enter the world") and no `g_ram` write occurs even if a control is interacted with

#### Scenario: Disabled during replay and side-by-side compare
- **WHEN** a snapshot is replaying, or the original ROM is attached for RAM compare (`ZeldaIsEmulatorAttached()` is true)
- **THEN** the Debug tab is disabled with the matching reason and no write occurs, so the replay stream is not corrupted and the RAM comparator cannot diverge

#### Scenario: Out-of-range input is clamped
- **WHEN** a Debug-tab value would exceed its documented bound (e.g. rupees past the feature-aware cap, a capacity that would leave `health_current > health_capacity`)
- **THEN** the write helper clamps the value to the valid range (and re-clamps `health_current` to `health_capacity` on capacity edits) before touching `g_ram`

### Requirement: Debug editor respects shared-byte and randomizer-owned state
For inventory values backed by a shared byte or tracked separately by the randomizer, the Debug tab SHALL avoid the "vanilla state reused as a progress proxy" desync class. Mushroom/Powder (shared byte `0xF344`) SHALL be a single 3-way selector ({none, mushroom, powder}) rather than two independent toggles. The Mushroom/Powder and Flute/Shovel selectors SHALL be disabled while `kFeatures1_RandomizerActive` is set, because the randomizer tracks that ownership state separately (e.g. `g_rando_mushroom_held`, `g_rando_flute_shovel_owned`) and a raw byte write would desync it. Tiered items whose byte is not a linear index SHALL expose only valid options (bow as {none, wood, silver}; boomerang as {none, blue, red}; sword read handles the `0xFF` in-repair sentinel; bottles expose only the values `kHudItemBottles[]` actually indexes).

#### Scenario: Mushroom/Powder never leaves both set
- **WHEN** the player switches the Mushroom/Powder selector in a non-randomizer game
- **THEN** the shared byte `0xF344` holds exactly one of {0 none, 1 mushroom, 2 powder} and the Witch/Potion-shop check remains reachable

#### Scenario: Randomizer-owned selectors are disabled under an active slot
- **WHEN** `kFeatures1_RandomizerActive` is set
- **THEN** the Mushroom/Powder and Flute/Shovel selectors are disabled with a "managed by randomizer" tooltip, so the randomizer's separate ownership tracking is not desynced

