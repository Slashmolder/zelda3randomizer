## MODIFIED Requirements

### Requirement: Enemy drop check setting has a dungeon-enemy tier

The `enemy_drop_checks` setting SHALL support values `Off` (0), `Keys` (1), and
`Dungeon` (2). `Dungeon` SHALL be accepted by settings validation, CSV parsing, share
strings, file select, and the native window.

#### Scenario: Dungeon is requested with supported small keys
- **WHEN** `enemy_drop_checks=Dungeon` and effective small keys are Wild/Retro or
  Dungeon
- **THEN** derived settings keep `enemy_drop_checks=Dungeon` when neither door shuffle
  nor enemy shuffle is active

#### Scenario: Dungeon is requested with vanilla small keys
- **WHEN** `enemy_drop_checks=Dungeon` but effective small keys are vanilla
- **THEN** derived settings normalize `enemy_drop_checks` to `Off`

#### Scenario: Dungeon is requested with door shuffle
- **WHEN** `enemy_drop_checks=Dungeon` and door shuffle is active
- **THEN** derived settings normalize `enemy_drop_checks` to `Keys`

#### Scenario: Dungeon is requested with enemy shuffle
- **WHEN** `enemy_drop_checks=Dungeon` and enemy shuffle is active
- **THEN** derived settings normalize `enemy_drop_checks` to `Keys`

## ADDED Requirements

### Requirement: Dungeon-enemy emission must fit existing location capacity

The implementation SHALL prove that ordinary dungeon-enemy checks fit
`kRandoLocationCapacity` together with every other generated location type before
those checks are emitted as locations.

#### Scenario: Emitted rows fit capacity
- **WHEN** the generated ordinary dungeon enemy registry is present
- **THEN** selfchecks verify the emitted `Enemy` location count matches the
  runtime lookup count and the total generated location count fits the active
  capacity

#### Scenario: Emitted rows would exceed capacity
- **WHEN** generated ordinary enemy-check rows would exceed
  `kRandoLocationCapacity`
- **THEN** codegen or selfcheck SHALL fail rather than producing a truncated
  location table
