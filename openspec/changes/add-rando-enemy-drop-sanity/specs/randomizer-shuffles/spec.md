## ADDED Requirements

### Requirement: Enemy-drop checks and existing shuffle interactions

Enemy-drop checks SHALL be orthogonal to `drop_shuffle`. `drop_shuffle` continues to
randomize the non-forced prize-pack table. An active enemy-drop check bypasses that
table and grants the item placed at its location; it SHALL NOT also emit the vanilla
forced small key or a prize-pack substitute.

`enemy_shuffle` SHALL compose with forced-key enemy-drop checks. Enemy shuffle may
substitute the real sprite type, but it SHALL NOT reorder the dungeon sprite-list
entry, shuffle the forced-key marker, or change the runtime source slot used by
the generated enemy-drop lookup. Active forced-key enemy-drop checks SHALL continue
to resolve by `(room, source_slot, drop_kind)`, not by substituted enemy type.
Ordinary dungeon-enemy checks SHALL be disabled by downgrading requested
`enemy_drop_checks = Dungeon` to effective `Keys` while enemy shuffle is active,
because placement does not currently know the shuffled enemy type or HP scaling.

Pot shuffle MAY compose with enemy-drop checks in Wild/Retro mode through the
generated reach predicates and existing pot behavior. Pot shuffle SHALL also compose
with Dungeon enemy-drop checks through combined free-drop accounting. Door shuffle
SHALL compose with enemy-drop checks through the generated door x enemy-drop bridge:
active enemy DROP rows are removed from vanilla free-drop accounting and counted as
itemized key sources when their door regions are reached.

#### Scenario: Drop shuffle affects only non-check drops
- **WHEN** `drop_shuffle` and active enemy-drop keys are both enabled
- **THEN** non-check enemy prize-pack drops use the shuffled prize table, while active
  enemy-drop checks grant their placed item and do not emit a prize-pack substitute

#### Scenario: Enemy shuffle preserves source-slot identity
- **WHEN** `enemy_shuffle` is active and the user requests `enemy_drop_checks = Keys`
- **THEN** effective settings keep enemy-drop checks active in supported key modes,
  and each forced-drop source resolves through its vanilla room/source-slot lookup

#### Scenario: Door shuffle models itemized enemy drops
- **WHEN** door shuffle and enemy-drop checks are requested together
- **THEN** effective settings keep enemy-drop checks active and the door layout digest
  includes the active enemy-drop bridge digest
