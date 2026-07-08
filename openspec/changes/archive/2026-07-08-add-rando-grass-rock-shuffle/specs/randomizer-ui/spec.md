# randomizer-ui — delta for add-rando-grass-rock-shuffle

## ADDED Requirements

### Requirement: Grass/rock shuffle tier selectors and terrain-check presentation

The native settings window SHALL expose two selectors in the Shuffles block — "Grass shuffle" and "Rock shuffle" — each an `EnumCombo` with lowercase labels `{"off", "junk", "all"}` matching the CLI grammar, defaulting to `off`, with tooltips limited to 1-2 durable player-facing facts (what the axis covers and what `junk` vs `all` means; no status or implementation detail). Neither selector is disable-coupled to any other axis (terrain composes freely, including under door shuffle). Presentation of terrain checks SHALL follow the pot-sanity pattern: the SNES HUD location-tracker grid skips `LOCTYPE_Grass`/`LOCTYPE_Rock` rows entirely; the native check tracker and reach panel gate terrain rows behind a session "Show terrain" toggle (default off, shown only when terrain locations exist in the slot) while ALL summary counts — global and per-region denominators — include terrain checks, with a per-region "+N terrain checks" note when rows are hidden that respects the active hide-checked/availability/search filters; the map-tracker pin hover tooltip summarizes terrain locations as a count line rather than enumerating them. The auto-tracker catalog SHALL tag the two new location types so external trackers can filter them. The spoiler surfaces (JSON, text, in-window) SHALL emit all terrain placements as ordinary rows (every active terrain location holds a real item — there is no empty-row class to filter), and the in-window row handling SHALL be verified against the enlarged location count.

#### Scenario: Selectors default off and read back
- **WHEN** the user opens the native settings window on a fresh profile
- **THEN** both selectors show `off`, and choosing `junk` or `all` round-trips through slot generation into the slot's stored canonical settings

#### Scenario: Tracker denominators count hidden terrain rows
- **WHEN** a grass-all seed is loaded and "Show terrain" is off
- **THEN** terrain rows are hidden, but the global and per-region completion counts include terrain checks, and regions with filtered-visible terrain checks show a "+N terrain checks" note

#### Scenario: HUD grid stays legible
- **WHEN** the SNES in-game location tracker renders for a seed with thousands of terrain checks
- **THEN** no terrain rows appear in the 8px grid; only the base location families render

### Requirement: In-world capped nearest-N terrain check glint

The runtime SHALL show an in-world "check" cue on unchecked, active overworld terrain objects so a player can identify remaining checks without re-cutting every bush and rock. Because the densest overworld screens hold more in-scope objects than the SNES sprite (OAM) budget can carry, the cue SHALL be capped: it marks only the nearest N (implementation constant, ~20) unchecked active terrain objects to Link on the current screen, and updates as Link moves. The cue SHALL reuse the established gold-sparkle glint (the same visual as the enemy-drop overworld marker), SHALL appear for every active tier (junk and all), SHALL disappear the frame after its object is checked, and SHALL be inert when the randomizer feature flag is off or no terrain axis is active. It is cosmetic only — it does not affect placement, logic, or the checked-location bitmap.

#### Scenario: Nearest unchecked terrain objects glint
- **WHEN** the player stands on an overworld screen that has unchecked active terrain checks with grass or rock shuffle enabled
- **THEN** the nearest few unchecked ones show the gold "check" glint, and objects already checked show no glint

#### Scenario: Glint never overruns the sprite budget
- **WHEN** the screen holds far more in-scope terrain objects than the OAM budget (e.g. a grass-dense lake/swamp screen)
- **THEN** only the nearest capped number glint; the game does not drop or corrupt other sprites, and the cue re-targets as Link walks toward other objects

#### Scenario: Glint is inert off-feature
- **WHEN** the seed has both terrain axes off, or the randomizer is inactive
- **THEN** no terrain glint is drawn and the overworld render is unaffected
