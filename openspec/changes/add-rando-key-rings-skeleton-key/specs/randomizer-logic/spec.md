## ADDED Requirements

### Requirement: Unified ring-aware small-key counts

The predicate VM and every direct small-key-count consumer SHALL use one effective
count model. Outside Retro, a held dungeon Key Ring SHALL saturate its family's
small-key count above every supported threshold; otherwise the ordinary count is
used. Retro's existing GenericKey collapse SHALL remain mutually exclusive. The
model SHALL cover all HAS operators, door oracle inputs/fingerprints, live
reachability, assumed fill, final spheres, and goal verification.
Generated/assumed inventories SHALL carry their ring item counts directly. Live
inventory construction SHALL materialize the derived owned-ring mask as one count
for each held `KeyRing_*`; numeric dungeon key counters alone SHALL NOT imply ring
ownership.

#### Scenario: Every HAS form sees the ring

- **WHEN** the inventory contains `KeyRing_PalaceOfDarkness` but no
  `SmallKey_PalaceOfDarkness`
- **THEN** HAS_ITEM, HAS_AMOUNT, HAS_ANY_OF, and HAS_ANY_COUNT expressions that
  reference Palace of Darkness keys evaluate using the saturated ring count

#### Scenario: Door cache includes ring ownership

- **WHEN** ring ownership changes for a door-shuffled dungeon
- **THEN** the door-oracle key fingerprint changes and cached reachability is not
  reused from the unowned state

#### Scenario: Live counts preserve the ring identity

- **WHEN** a collected ring is reconstructed as owned after load but its numeric
  key counter has subsequently been spent down
- **THEN** live counts still contain that ring item and use its saturated family
  count

#### Scenario: A ring does not waive non-key soul gates

- **WHEN** enemy souls are enabled and the inventory contains
  `KeyRing_HyruleCastleEscape` but not `Soul_Soldier`
- **THEN** Zelda's Cell and the Zelda rescue event remain unreachable in both
  generation and live tracker logic, and become reachable only after the Soldier
  soul is owned (subject to their other requirements)

### Requirement: Skeleton Key is absent from generation logic

Skeleton Key ownership SHALL not modify any predicate, count alias, door oracle,
reachability bit, sphere, accessibility result, or goal-completable result.

#### Scenario: Skeleton-neutral reachability

- **WHEN** otherwise equal inventories differ only in Skeleton Key ownership
- **THEN** their generation-logic reachability results are identical
