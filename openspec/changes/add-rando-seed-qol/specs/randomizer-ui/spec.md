## ADDED Requirements

### Requirement: Pause-map dungeon check-info and seed info overlay

The randomizer UI SHALL render, on the pause dungeon map under an active slot, the
per-dungeon remaining-check counts (F1) and, in a later phase, dots at the located
remaining checks — reading the trackers' existing per-dungeon checked/total
accessor rather than a separate placement scan. It SHALL also render the seed info
panel (F2: crystal requirements, hunt `pieces X/Y`, slot identity string) as an
in-game overlay. Both overlays SHALL draw only counts/requirements/identity — never
item names — so they are race-safe, and SHALL be client-side only (no
`placement_digest` / `settings_hash` effect). When no slot is active or the
governing feature bit is off, the pause map and HUD render exactly as vanilla.

#### Scenario: Dungeon map shows remaining counts
- **WHEN** an active slot's pause dungeon map is shown for a dungeon with remaining
  checks
- **THEN** that dungeon's remaining-check count is drawn on the map, updating as its
  checks are collected

#### Scenario: Overlays draw no spoiler content
- **WHEN** the check-info or seed info overlay is shown for a race-mode seed
- **THEN** only counts/requirements/identity are drawn and no item name or location
  is revealed

#### Scenario: Vanilla render when inactive
- **WHEN** no randomizer slot is active or the feature bit is off
- **THEN** the pause map and HUD render identically to vanilla with no overlay
