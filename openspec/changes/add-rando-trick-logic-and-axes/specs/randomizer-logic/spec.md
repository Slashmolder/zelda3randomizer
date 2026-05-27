## ADDED Requirements

### Requirement: OP_TRICK predicate handler

`OP_TRICK trick_id` (op-code 15) SHALL evaluate true when bit `trick_id` is set in `settings.tricks` (uint64 bitmask). The trick_id space is enumerated in `assets/rando/op_registry.yaml` `tricks:` table; each entry has a stable bit position and a kebab-case name (`boots-clip`, `fake-flippers`, `bunny-revival`, etc.).

When `settings.tricks == 0` (Phase A default), every `OP_TRICK` predicate evaluates false; existing Phase A logic-graph behavior is preserved — non-trick seeds have the same reachability output before and after this change.

> **Stub status**: the exact trick set + per-location applicability is deferred to apply-time (depends on ALTTPR PHP grep findings).

#### Scenario: Default tricks=0 reproduces Phase A reachability
- **WHEN** a Phase A default-settings seed is generated (tricks=0)
- **THEN** `Logic_ComputeReachability` returns the same set of reachable locations as before this change; corpus digests match byte-for-byte

#### Scenario: Trick predicate unlocks location only when bit set
- **WHEN** a location's predicate is `AND(<base>, OP_TRICK boots-clip)`
- **AND** the seed has `settings.tricks` with the `boots-clip` bit cleared
- **THEN** the location is unreachable
- **WHEN** the same seed is regenerated with the `boots-clip` bit set
- **THEN** the location is reachable (assuming `<base>` is also satisfied)

### Requirement: OP_DIFFICULTY_AT_LEAST predicate handler

`OP_DIFFICULTY_AT_LEAST threshold` (op-code 16) SHALL evaluate true when `settings.item_pool_difficulty >= threshold`. The threshold encoding mirrors the enum: `easy=0 < normal=1 < hard=2 < expert=3`. Phase A already supports all four pool levels in pool composition; the op exposes the same axis in predicates.

> **Stub status**: per-location applicability is deferred to apply-time.

#### Scenario: Normal-difficulty seed satisfies "at-least-easy" predicate
- **WHEN** a seed has `settings.item_pool_difficulty = normal` and a location's predicate is `OP_DIFFICULTY_AT_LEAST easy`
- **THEN** the predicate evaluates true

#### Scenario: Easy-difficulty seed fails "at-least-hard" predicate
- **WHEN** a seed has `settings.item_pool_difficulty = easy` and a location's predicate is `OP_DIFFICULTY_AT_LEAST hard`
- **THEN** the predicate evaluates false; the location is unreachable

### Requirement: OP_GLITCH_LEVEL_AT_LEAST predicate handler

`OP_GLITCH_LEVEL_AT_LEAST threshold` (op-code 17) SHALL evaluate true when `settings.logic >= threshold`. The threshold encoding: `NoGlitches=0 < OverworldGlitches=1 < MajorGlitches=2`. Phase B exposes the first three; `HybridMG` and `NoLogic` are reserved for later phases.

When `settings.logic == NoGlitches` (Phase A default), every `OP_GLITCH_LEVEL_AT_LEAST` predicate with a non-zero threshold evaluates false; Phase A logic behavior is preserved.

> **Stub status**: per-location glitch predicate authoring deferred to apply-time.

#### Scenario: NoGlitches seed reproduces Phase A reachability
- **WHEN** a seed has `settings.logic == NoGlitches`
- **THEN** every `OP_GLITCH_LEVEL_AT_LEAST` predicate with non-zero threshold evaluates false; logic-graph reachability matches Phase A behavior

#### Scenario: OverworldGlitches unlocks Bumper Cave Ledge
- **WHEN** a seed has `settings.logic == OverworldGlitches` and the Bumper Cave Ledge location's predicate includes `OP_GLITCH_LEVEL_AT_LEAST OverworldGlitches`
- **THEN** the location is reachable (subject to its other predicate constraints)
