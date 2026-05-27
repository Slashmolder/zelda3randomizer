## MODIFIED Requirements

### Requirement: Starting inventory injection

The placement layer SHALL grant the seed's configured starting inventory exactly once at new-game initialization. A `kRam_RandoStartingInventoryGranted` bit SHALL be set after injection so subsequent save reloads do not re-inject.

**Phase B addition (Bug #12 fix)**: `Rando_TryGrantStartingInventory` SHALL be invoked at the game-start hook from production code. Phase A defined the function at `src/rando/rando_placement.c:1360` but Phase A1 audit Bug #12 confirmed no production caller exists. The candidate call sites are end-of-`Module05_LoadFile` or start-of-first-`Module06_PreDungeon`; the decision (which one) SHALL be recorded in this change's `design.md`.

Inverted in particular requires this fix: Inverted Link receives `MoonPearl + MagicMirror` per the bunny-state contract. Without the call site, Inverted seeds are unplayable.

#### Scenario: Open mode starting inventory applied at new game
- **WHEN** world-state is Open and the player starts a new randomizer slot
- **THEN** Link begins with the Open-mode starting items reflected in `link_item_*` and the HUD, and `kRam_RandoStartingInventoryGranted` is set

#### Scenario: No double-grant on reload
- **WHEN** the player saves and reloads a randomizer slot mid-run
- **THEN** the starting-inventory injection does not run again because `kRam_RandoStartingInventoryGranted` is already set in the loaded save

#### Scenario: Inverted mode receives MoonPearl + MagicMirror
- **WHEN** world-state is Inverted and the player starts a new randomizer slot
- **THEN** Link begins with MoonPearl + MagicMirror in `link_item_*`, the HUD reflects them, and `kRam_RandoStartingInventoryGranted` is set in the same frame
