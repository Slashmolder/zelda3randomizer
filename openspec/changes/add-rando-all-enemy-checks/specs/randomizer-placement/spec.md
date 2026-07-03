## ADDED Requirements

### Requirement: All-enemy locations participate in placement as real checks

When `enemy_drop_checks=all` is effective, every emitted all-enemy location SHALL be
available to placement as a normal one-shot location. Only generated forced-key and
reviewed one-shot big-key rows from the `Keys` tier SHALL use `EnemyDrop` and
pickup-time dispatch. Ordinary all-enemy sources, including overworld, boss/miniboss,
and finite scripted-spawn child rows, SHALL use `Enemy` and death-time direct grant
unless a later spec defines a separate persistent-pickup collection model. Placement
SHALL use the same effective activation predicate as logic, runtime, spoiler, and
tracker iteration.

If the generated all-enemy registry is missing, stale, has duplicate identities, or
omits an unclassified finite killable source, placement SHALL reject active `all`
generation instead of silently using a partial registry.

#### Scenario: Effective all emits all active locations
- **WHEN** `enemy_drop_checks=all` is effective and the all-enemy registry is fresh
- **THEN** placement includes every emitted compatible all-enemy location in the
  fillable location set

#### Scenario: Requested all is downgraded
- **WHEN** a settings combination requests `all` but derived settings visibly
  downgrade to `dungeon`, `keys`, or `off`
- **THEN** placement includes only the locations for the effective tier

#### Scenario: Missing all registry fails closed
- **WHEN** `enemy_drop_checks=all` is effective but generated all-enemy data is absent
  or stale
- **THEN** placement rejects the seed with a diagnostic

### Requirement: All-enemy item delivery policy is explicit

Customizer pins, traps, and direct-grant item classes SHALL be allowed on all-enemy
locations only when their delivery path is reviewed for that collection model. Item
classes that require a pickup sprite, prolonged receive animation, or special room
state SHALL either use a proven direct-grant/confirmation path or be excluded from
all-enemy placement.

#### Scenario: Trap class lacks safe death-time delivery
- **WHEN** a trap class cannot be safely delivered by an ordinary enemy death-time
  check
- **THEN** placement does not allow that trap class on ordinary all-enemy locations

#### Scenario: Customizer pins supported all-enemy location
- **WHEN** a customizer manifest pins an item to an emitted all-enemy location whose
  item class is supported by that source's collection model
- **THEN** placement honors the pin like other real non-empty checks
