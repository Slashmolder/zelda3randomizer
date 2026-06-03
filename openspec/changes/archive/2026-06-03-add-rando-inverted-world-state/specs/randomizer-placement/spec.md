## MODIFIED Requirements

### Requirement: Starting inventory injection

The placement layer SHALL grant the seed's configured starting inventory exactly once per new-save lifetime at the game-start hook. `Rando_TryGrantStartingInventory` SHALL be invoked at the end of `Module05_LoadFile` so the injection fires before any game frame runs against the new save.

**Cold-boot exploit guard**: the `kRam_RandoStartingInventoryGranted` cell at `g_ram[0x65e]` is OUTSIDE the SRAM-saved range (which covers `0xF000-0xF4FF`) and is re-zeroed by `ZeldaInitialize` on every cold boot. To prevent cold-boot reloads of in-progress saves from re-firing the injection, granters that write to additive HUD-filler registers (escape-fill arrows/magic/bombs) SHALL gate on a save-state-persisted signal — `sram_progress_indicator == 0` is the canonical brand-new-save check (advances to 1+ once Uncle's gift is received). Idempotent grants (e.g., Moon Pearl bit-set via `Link_ReceiveItem`) MAY skip this guard.

**Escape-fill**: when a sphere-0 weapon needs ammo (Bow / FireRod / CaneOfSomaria / CaneOfByrna / bombs-only) at any of the four sphere-0 chests (Link's House, Uncle, HC Map Chest, Secret Passage), the injection SHALL queue 70 arrows / 0x80 magic / 50 bombs into the HUD filler cells. Numbers mirror ALTTPR's `Rom.php` `EscapeRefills.Uncle.*` defaults. This is a one-shot stopgap; a faithful port of `World::setEscapeFills` (per-respawn refills hooked at Uncle's death / Zelda's cell / Sanctuary mantle, plus `rom.EscapeAssist` infinite-ammo toggle) is the v2 follow-up.

**In-session slot-switch reset**: `Rando_DeactivateSlot` SHALL clear `g_rando_starting_inventory_granted` so a user backing out of slot A (post-grant) and loading slot B in the same boot lets slot B's grant fire on its next `Module05_LoadFile`.

Inverted in particular requires this fix: Inverted Link receives `MoonPearl + MagicMirror` per the bunny-state contract. Without the call site, Inverted seeds are unplayable.

#### Scenario: Open mode starting inventory applied at new game
- **WHEN** world-state is Open and the player starts a new randomizer slot
- **THEN** Link begins with the Open-mode starting items reflected in `link_item_*` and the HUD, and `kRam_RandoStartingInventoryGranted` is set

#### Scenario: No double-grant within a single boot
- **WHEN** the player loads a randomizer slot, plays for a while, then reloads the same save without exiting the game
- **THEN** the starting-inventory injection does not run again because `kRam_RandoStartingInventoryGranted` was set on the first load and remains set for the duration of the boot

#### Scenario: No double-grant on cold-boot reload of in-progress save
- **WHEN** the player saves their Standard randomizer run past Uncle's gift, exits the program, restarts the program, and reloads the same slot
- **THEN** the escape-fill ammo grant does NOT re-fire, because the cold-boot exploit guard checks `sram_progress_indicator != 0` and short-circuits the filler-register writes
- **AND** the same-boot gate `kRam_RandoStartingInventoryGranted` is still set after this guard fires, so subsequent calls within the boot also short-circuit

#### Scenario: Standard escape-fill on a brand-new save with bow at Uncle
- **WHEN** the player starts a new Standard randomizer slot where the placement table puts a Bow at the Uncle's slot
- **THEN** `link_arrow_filler` (g_ram[0xF376]) is set to 70 so the HUD drain ticks arrows into `link_num_arrows` over the next few seconds during escape

#### Scenario: Inverted mode receives MoonPearl + MagicMirror
- **WHEN** world-state is Inverted and the player starts a new randomizer slot
- **THEN** Link begins with MoonPearl + MagicMirror in `link_item_*`, the HUD reflects them, and `kRam_RandoStartingInventoryGranted` is set in the same frame

#### Scenario: In-session slot switch re-fires the grant for the new slot
- **WHEN** the player loads slot A (escape-fill fires, gate=1), backs out to file-select, and loads slot B which is a brand-new save in the same boot
- **THEN** slot B's escape-fill fires because `Rando_DeactivateSlot` cleared the gate when the user backed out of slot A
