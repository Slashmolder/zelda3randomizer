## ADDED Requirements

### Requirement: Ring-aware dungeon-item placement

Key Rings SHALL be progression dungeon items. In Dungeon mode a ring SHALL be
confined exactly like its family's small key; in Wild mode it MAY be placed in any
otherwise valid location. Selected-ring equivalence SHALL apply to `can_place` and
`always_allow` candidate tests. Rings SHALL be placed in the restrictive dungeon-
progression prefix before general progression.

#### Scenario: Forced key slot accepts its ring

- **WHEN** Swamp Palace is selected for a ring and a location requires
  `OP_ITEM_IS(SmallKey_SwampPalace)`
- **THEN** `KeyRing_SwampPalace` satisfies that candidate rule

#### Scenario: Forbidden self-key slot forbids ring

- **WHEN** a location forbids its own dungeon small key and that family is ringed
- **THEN** the corresponding Key Ring is forbidden by the same rule

### Requirement: Customizer preserves key-item cardinality

Customizer pins and pool overrides SHALL preserve exactly one selected-family ring,
zero selected-family ordinary keys, and exactly one Skeleton Key when enabled.
Unselected rings, reintroduced selected-family keys, duplicate rings, and duplicate
or disabled Skeleton Keys SHALL be rejected with a specific validation error.
`pool_overrides` SHALL NOT change the count of any small key, Key Ring, or Skeleton
Key; pins may relocate an already-generated key item but may not mint or remove it.

#### Scenario: Customizer cannot undo collapse

- **WHEN** a customizer manifest adds an ordinary Palace of Darkness key while
  Palace of Darkness is selected for a ring
- **THEN** generation refuses the manifest before assumed fill

### Requirement: Skeleton Key is bonus fill

When enabled, one Skeleton Key SHALL enter the pool before junk padding, replacing
one junk item without increasing active location count. It SHALL not be classified
as progression, dungeon-confined, or trap-replaceable. If the pre-pad pool is
already at target, construction SHALL remove a deterministic junk copy rather than
overflowing or omitting Skeleton Key.

#### Scenario: Skeleton location is not assumed reachable

- **WHEN** accessibility is beatable-only and Skeleton Key lands outside the goal
  spheres
- **THEN** the seed remains valid because no logic predicate or goal requires it
