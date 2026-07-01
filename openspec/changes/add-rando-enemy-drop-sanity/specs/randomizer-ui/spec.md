## ADDED Requirements

### Requirement: Enemy-drop checks selector and tracker presentation

The settings UI SHALL expose enemy-drop checks as a selector separate from
`drop_shuffle`. It SHALL expose `Off`, `Enemy key drops`, and `Dungeon
enemies`. The UI SHALL reflect effective behavior: supported Wild/Retro and Dungeon
small-key modes can select `Keys`; vanilla-door Wild/Retro and Dungeon modes can
select `Dungeon`; door shuffle's forced Dungeon mode can select `Keys` but
downgrades a raw `Dungeon` request to `Keys`; vanilla keys show the effective
setting as `Off` or disable the selector. `enemy_shuffle` SHALL NOT disable the
selector.

Spoilers, the native location tracker, reach panel, and auto-tracker output SHALL
group active enemy-drop checks by dungeon and room. Inactive enemy-drop ids SHALL be
hidden from tracker denominators and spoiler placement rows. The spoiler SHALL record
the effective setting so a raw request normalized to `Off` does not imply phantom
enemy-drop checks.

#### Scenario: Selector is separate from drop shuffle
- **WHEN** the user opens the randomizer settings window
- **THEN** `drop_shuffle` remains the prize-pack shuffle control, and enemy-drop
  checks are controlled by a separate selector

#### Scenario: Dungeon tier is exposed
- **WHEN** the user opens the randomizer settings window or file-select settings
- **THEN** the enemy-drop-check selector offers the dungeon-enemy tier

#### Scenario: Trackers show only active enemy-drop checks
- **WHEN** a seed has effective `enemy_drop_checks = Keys`
- **THEN** tracker and spoiler surfaces show the generated enemy-drop locations
  grouped by dungeon/room; when the effective setting is `Off`, those ids are hidden

#### Scenario: Trackers include ordinary enemy checks for dungeon tier
- **WHEN** a seed has effective `enemy_drop_checks = Dungeon`
- **THEN** tracker and spoiler surfaces also show generated ordinary enemy locations
  grouped by dungeon/room
