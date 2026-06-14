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

### Requirement: Custom art for ALTTPR items without a vanilla receive bundle

The randomizer SHALL provide dedicated custom art for the ALTTPR items that have no vanilla receive-item graphics bundle — the Triforce Piece, the Rupoor, and the magic upgrades (Half Magic / Quarter Magic) — so that, under an active slot with field item sprites enabled, they draw a recognizable sprite instead of the location's vanilla fallback or a misleading stand-in. The custom art SHALL load through the same field-item draw path (the shared receive-item VRAM slot) with a dedicated palette so the colour is stable across areas, and the same custom art SHALL drive each item's direct-grant confirmation icon. This requirement is visual-only: it SHALL NOT affect placement, the share string, `settings_hash`, `kGeneratorVersion`, or the regression corpus, and the vanilla (non-rando) path SHALL remain unchanged.

#### Scenario: Triforce Piece on the field
- **WHEN** a Triforce-Hunt seed places a Triforce Piece at a free-standing item location and field item sprites are enabled
- **THEN** the location renders a Triforce Piece sprite (not the vanilla location sprite)
- **AND** collecting it shows the matching Triforce confirmation icon and increments the piece counter

#### Scenario: Rupoor shows its off-black colour
- **WHEN** a seed places a Rupoor at a free-standing item location
- **THEN** the location renders an off-black / grey rupee (its recognizable cue), using a custom palette so the colour is consistent regardless of the area's loaded sprite palettes
- **AND** collecting it drains rupees as before and shows the matching confirmation icon

#### Scenario: Magic upgrades show the decanter
- **WHEN** a seed places a Half Magic or Quarter Magic upgrade at a free-standing item location
- **THEN** the location renders the corresponding magic-decanter sprite (the ALTTPR ½ / ¼ jar)
- **AND** collecting it grants the magic upgrade and shows the matching decanter confirmation icon (these items were previously audio-only)

#### Scenario: No determinism or vanilla impact
- **WHEN** this custom art is built
- **THEN** `kGeneratorVersion` and the corpus are unchanged (visual-only), and with rando inactive every standing-item sprite draws exactly as the original game

