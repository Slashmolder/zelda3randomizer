## ADDED Requirements

### Requirement: Enemy-drop-check canonical settings axis

The `RandoSettings` struct SHALL gain an enum axis `enemy_drop_checks` with values
`Off` (0), `Keys` (1), and `All` (2). The canonical serialization
SHALL append one byte after the existing settings layout for this axis, bumping
`kSettingsCanonicalLen`. Existing shorter canonical blobs and share strings SHALL
deserialize with `enemy_drop_checks = Off`.

Adding this byte SHALL update every fixed-length coupling point: v2 share-string
encode/decode and settings-length/CRC validation, sidecar slot settings replay,
snapshot settings TLV replay, suppressed-spoiler fixed settings length, UI ini
persistence, expected canonical/hash selfchecks, and the regression-corpus manifest.
`kGeneratorVersion` SHALL advance because non-off enemy-drop checks are a new
generation/runtime axis.

Derived rules SHALL normalize `enemy_drop_checks` to `Off` when effective small keys
are vanilla. Wild/Retro and Dungeon small-key modes SHALL keep `Keys` effective.
Vanilla-door Wild/Retro and Dungeon small-key modes SHALL keep `All` effective.
Door shuffle's forced Dungeon mode SHALL keep `Keys` effective and SHALL downgrade a
raw `All` request to `Keys` until ordinary enemy rows have a door-region bridge.
`enemy_shuffle` SHALL compose with the forced-key `Keys` tier and SHALL downgrade a
raw `All` request to `Keys` until ordinary enemy logic can consume per-seed shuffled
type and HP data. This normalization SHALL be applied to the canonical copy used
for the settings hash and to every effective accessor used by placement, logic,
UI, spoiler, and runtime.

#### Scenario: Old settings decode as off
- **WHEN** a pre-enemy-drop share string or canonical settings blob is decoded
- **THEN** `enemy_drop_checks` defaults to `Off` and all existing settings fields keep
  their prior values

#### Scenario: Selecting keys changes the settings hash only when effective
- **WHEN** `enemy_drop_checks = Keys` and effective small keys are in a supported mode
- **THEN** the appended canonical byte is non-zero and the settings hash changes
  relative to `Off`

#### Scenario: Vanilla key mode normalizes enemy-drop checks off
- **WHEN** `enemy_drop_checks = Keys` but effective small keys are vanilla
- **THEN** derived settings serialize as `Off`, and placement/runtime behavior is
  byte-identical to enemy-drop checks off

#### Scenario: Dungeon key mode keeps enemy-drop checks active
- **WHEN** `enemy_drop_checks = Keys` and effective small keys are Dungeon
- **THEN** derived settings keep `enemy_drop_checks = Keys`

#### Scenario: Door shuffle keeps enemy-drop checks active
- **WHEN** `enemy_drop_checks = Keys` and `door_shuffle = basic`
- **THEN** derived settings serialize with `enemy_drop_checks = Keys` because door
  shuffle forces effective Dungeon small keys

#### Scenario: All tier downgrades under door shuffle
- **WHEN** `enemy_drop_checks = All` and `door_shuffle = basic`
- **THEN** derived settings serialize with `enemy_drop_checks = Keys`

#### Scenario: All tier downgrades under enemy shuffle
- **WHEN** `enemy_drop_checks = All` and `enemy_shuffle = true`
- **THEN** derived settings serialize with `enemy_drop_checks = Keys`
