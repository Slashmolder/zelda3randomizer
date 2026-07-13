## ADDED Requirements

### Requirement: Pause-map dungeon check-info

The randomizer UI SHALL render, on the pause dungeon map under an active slot, the
per-dungeon remaining-check count (F1), reading the new cached active-placement and
checked-state accessor. Located-check dots are outside this archived change. The
pause-map surface SHALL draw
only counts — never item names, locations, requirements, or full share strings —
so it is race-safe, and SHALL be client-side only (no
`placement_digest` / `settings_hash` effect). When no slot is active or the
governing feature bit is off, the pause map renders exactly as vanilla.

#### Scenario: Dungeon map shows remaining counts
- **WHEN** an active slot's pause dungeon map is shown for a dungeon with remaining
  checks
- **THEN** that dungeon's remaining-check count is drawn on the map, updating as its
  checks are collected

#### Scenario: Count surface draws no spoiler content
- **WHEN** the check-info surface is shown for a race-mode seed
- **THEN** only counts are drawn and no item name, location, or seed requirement is
  revealed

#### Scenario: Vanilla render when inactive
- **WHEN** no randomizer slot is active or the feature bit is off
- **THEN** the pause map renders identically to vanilla with no overlay
