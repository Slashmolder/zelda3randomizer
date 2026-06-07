# randomizer-field-item-sprites Specification

## Purpose
TBD - created by archiving change add-rando-field-item-sprites. Update Purpose after archive.
## Requirements
### Requirement: Field item sprite substitution

When a randomizer slot is active (`kFeatures1_RandomizerActive` set) and the client-local `field_item_sprites` toggle is enabled, the game SHALL draw a free-standing item location's **placed** item graphics in place of its vanilla sprite. The placed item SHALL be resolved with the side-effect-free `Placement_Lookup(location_id, vanilla_item_id)`; the location's vanilla appearance SHALL be preserved when the placed item equals the vanilla item, when no placement table is active, or when the placed item's graphics cannot be resolved.

#### Scenario: Placed item differs from vanilla
- **WHEN** a Piece-of-Heart location's placed item is the Bow under an active slot with `field_item_sprites` enabled
- **THEN** the field sprite renders as a Bow (correct gfx + palette), not a Piece of Heart
- **AND** collecting it still grants the Bow via the existing grant dispatch

#### Scenario: Placement equals vanilla
- **WHEN** a location's placed item equals its vanilla item
- **THEN** the field sprite renders the vanilla appearance unchanged

#### Scenario: Unresolvable graphics fall back, never garbage
- **WHEN** the placed item's graphics cannot be loaded into the field-item VRAM slot for the current screen
- **THEN** the location renders its vanilla sprite (a partial or corrupt tile SHALL NOT be drawn)

### Requirement: Vanilla path unchanged when rando inactive

When `kFeatures1_RandomizerActive` is clear, every standing-item prep and draw path SHALL be byte-identical to the unmodified game. The feature SHALL add no RAM writes, OAM changes, or VRAM loads outside the active-rando gate, preserving the side-by-side RAM-compare contract.

#### Scenario: Non-rando play
- **WHEN** the game runs without an active randomizer slot
- **THEN** standing items draw exactly as in the original ROM and the RAM-compare (vanilla side-by-side mode) shows no divergence introduced by this feature

#### Scenario: Toggle off under rando
- **WHEN** an active rando slot is loaded but `field_item_sprites` is disabled in `zelda3.ini`
- **THEN** standing items draw their vanilla sprites, and toggling the value takes effect without a restart (per-frame read)

### Requirement: Per-screen VRAM slot with multi-item fallback

The feature SHALL manage a single per-screen field-item graphics slot. When more than one standing item shares a screen and the slot already holds a different item's graphics, the additional item(s) SHALL fall back to their vanilla sprite rather than display the wrong item or corrupt graphics. Loading the field-item graphics SHALL NOT corrupt an in-flight item-receipt animation that shares the on-demand decompression machinery.

#### Scenario: Two standing items on one screen
- **WHEN** two free-standing item locations are visible on the same screen and the slot can hold only one
- **THEN** the first-resolved location draws its placed item and the other draws its vanilla sprite

#### Scenario: Receipt animation in flight
- **WHEN** the player is mid item-receipt animation while a field item would load its graphics
- **THEN** the receipt animation renders correctly and the field item defers (vanilla fallback) until the shared resource is free

### Requirement: No determinism, save, or settings impact

The `field_item_sprites` toggle SHALL be client-local (read from `zelda3.ini`), SHALL NOT participate in `RandoSettings` canonical serialization, the settings hash, the share string, `kGeneratorVersion`, or the regression corpus. Two players on the same share string with different `field_item_sprites` values SHALL generate and play a byte-identical seed that merely looks different on the field.

#### Scenario: Same seed, different toggle
- **WHEN** two clients load the same share string with `field_item_sprites` on vs. off
- **THEN** placement, spoiler digests, and save state are identical; only the on-field item appearance differs

#### Scenario: No corpus or version cascade
- **WHEN** this feature is built
- **THEN** `kGeneratorVersion` is unchanged and the rando regression corpus does not require regeneration

