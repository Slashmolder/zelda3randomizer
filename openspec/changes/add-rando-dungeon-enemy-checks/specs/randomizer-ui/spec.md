## ADDED Requirements

### Requirement: Dungeon-enemy UI is exposed after runtime and tracker support

The settings UI SHALL expose the dungeon-enemy tier now that dungeon runtime dispatch,
logic, persistence, and tracker grouping are implemented.

#### Scenario: Dungeon tier is available
- **WHEN** the native window or file-select settings present enemy-drop checks
- **AND** effective small keys support enemy-drop checks
- **AND** neither door shuffle nor enemy shuffle is active
- **THEN** they offer `off`, `enemy key drops`, and `dungeon enemies`

#### Scenario: Effective value is downgraded
- **WHEN** raw settings request `dungeon` but derived rules normalize it to `off` or
  `keys`
- **THEN** user-facing effective labels reflect the lower active tier
