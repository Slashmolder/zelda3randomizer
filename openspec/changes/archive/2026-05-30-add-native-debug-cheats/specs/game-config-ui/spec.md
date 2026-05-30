# game-config-ui

## ADDED Requirements

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
