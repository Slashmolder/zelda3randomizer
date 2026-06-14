## ADDED Requirements

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
