## ADDED Requirements

### Requirement: All-enemy tier composes with shuffle axes explicitly

`enemy_drop_checks=all` SHALL be orthogonal to `drop_shuffle`: prize-pack drops keep
using the drop table, while active enemy checks grant placed items through their
location dispatch model.

With `enemy_shuffle`, forced-key `EnemyDrop` rows keep the existing vanilla
room/source-slot identity and requested `All` normalizes to the highest lower tier
allowed by existing derived rules, normally `Keys` but `Off` when the keys tier is
unsupported. Non-key `Enemy` rows in `Dungeon` or `All` SHALL remain inactive while
enemy shuffle is active until a future change makes enemy shuffle placement-affecting
for all-enemy logic, including substituted type, HP, damage, killability,
settings-hash/corpus expectations, and digest behavior.

With door shuffle, requested `All` SHALL remain effective only when non-key
all-enemy door-region bridges cover every emitted non-key source. The bridge rows,
bridge digest, and effective all-enemy tier SHALL participate in door layout
generation, the accepted `DoorShuffleLayout` identity/digest, sidecar activation,
and snapshot replay validation. Without that bridge/digest/replay support, requested
`All` SHALL normalize to `Keys`.

With boss shuffle, requested `All` SHALL remain effective only after boss/miniboss
all-enemy identity is defined against assigned boss rooms, pinned bosses, secondary
sprites, prizes, heart containers, and scripted progression. Until then, requested
`All` SHALL normalize to `Dungeon` unless another rule lowers the effective tier
further.

With entrance shuffle, including cave entrance shuffle, requested `All` SHALL remain
effective only after all-enemy overworld/domain reachability is modeled against the
entrance graph. Until then, requested `All` SHALL normalize to `Dungeon` unless
another rule lowers the effective tier further. Existing cave-entrance pot/key
derived rules still apply before this normalization.

Generated thrown-pot routes SHALL require effective pot shuffle to be off. While any
effective pot-sanity tier is active, enemy checks SHALL fall back to their reviewed
inventory-combat routes so no shuffled pot can be counted both as a required item
check and a future thrown weapon.

#### Scenario: Drop shuffle remains prize-pack-only
- **WHEN** `drop_shuffle` and effective `enemy_drop_checks=all` are both active
- **THEN** non-check enemy prize-pack drops use the shuffled prize table
- **AND** active enemy checks grant placed items through their location dispatch path

#### Scenario: Enemy shuffle normalizes all to supported lower tier
- **WHEN** `enemy_shuffle` is active and settings request `enemy_drop_checks=all`
- **THEN** derived settings normalize to the highest lower tier allowed by existing
  derived rules
- **AND** ordinary dungeon, overworld, boss, and scripted all-enemy rows are inactive

#### Scenario: Door shuffle requires all-enemy bridge digest
- **WHEN** door shuffle and effective `enemy_drop_checks=all` are active
- **THEN** door layout generation and activation include the non-key all-enemy bridge
  digest and effective all-enemy tier

#### Scenario: Missing all-enemy door bridge normalizes to keys
- **WHEN** door shuffle is active and non-key all-enemy door bridge support is absent
- **THEN** requested `All` normalizes to `Keys`

#### Scenario: Boss shuffle excludes boss-domain all until modeled
- **WHEN** boss shuffle is active before boss-domain all-enemy identity is modeled
- **THEN** requested `All` normalizes to `Dungeon` unless another rule lowers the
  effective tier further

#### Scenario: Entrance shuffle excludes all-domain rows until modeled
- **WHEN** entrance shuffle is active before all-enemy entrance-graph reachability is
  modeled
- **THEN** requested `All` normalizes to `Dungeon` unless another rule lowers the
  effective tier further

#### Scenario: Pot route does not double-count required pot check
- **WHEN** a pot must be lifted to collect a required pot-sanity item before an enemy
  check
- **THEN** the thrown-pot route is inactive for that enemy check
- **AND** the reviewed inventory-combat route remains required
