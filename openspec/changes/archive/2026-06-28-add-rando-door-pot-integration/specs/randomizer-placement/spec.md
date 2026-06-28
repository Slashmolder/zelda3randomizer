## ADDED Requirements

### Requirement: Door-shuffle placement model with active pot checks

Placement SHALL treat active pot checks as ordinary fillable locations under door
shuffle, subject to the selected pot tier and dungeon-item containment.

When door shuffle is active, the door key prover SHALL model the active pot locations
before placement begins. A dungeon's possible small-key sources SHALL be:

- static door-table item locations,
- active non-empty pot locations in that dungeon,
- non-pot drop-key rows that remain free in-context.

Itemized key-pot drop rows SHALL be excluded from free-drop accounting so a key under
an active pot is never counted both as a placement item and as a free drop.
Empty pots in the `all` tier SHALL be pre-pinned to `ITEM_Nothing` and SHALL NOT
increase the prover's possible small-key source count.

The free-drop exclusion SHALL be shared by every door-explorer call path that counts
reachable drops, including placement proving, runtime logic-oracle reachability,
door self-tests, and key-depth dumping.

Small-key-source counts and big-key-availability counts SHALL be distinct. If active
pots are rejected as big-key placements, the prover SHALL also exclude them from
big-key availability. If active pots are counted as big-key-capable locations, the
layout SHALL emit digest-covered `bk_restricted` coverage for the exact pot loc ids
that placement must reject.

#### Scenario: Key pot counted as a location, not a free drop

- **WHEN** a door+pot seed itemizes a dungeon's vanilla pot key
- **THEN** the key-door prover counts the pot as an active key-source location
- **AND** the matching drop-key row does not increase the free-drop key count

#### Scenario: Non-pot drop remains free

- **WHEN** a dungeon has a vanilla small-key drop that is not an active pot key
- **THEN** the prover and logic oracle keep counting it as a free in-context drop

#### Scenario: Logic oracle also excludes itemized pot drops

- **WHEN** active door+pot logic evaluates a key-door threshold
- **THEN** reachable itemized key-pot drop rows do not increase the free-drop key
  budget
- **AND** non-pot drop rows still do

#### Scenario: Empty pot is not a key-source location

- **WHEN** `pot_shuffle=all` activates an empty pot under door shuffle
- **THEN** that pot is reachable and checkable as `ITEM_Nothing`
- **AND** it does not increase the key-door prover's available key-source count

#### Scenario: Big-key placement and prover counts agree

- **WHEN** placement rejects a dungeon big key from an active pot location due to a
  door-shuffle big-key restriction
- **THEN** the prover did not count that location as available for satisfying the
  big-key-open branch

#### Scenario: Assetless build fails closed

- **WHEN** a binary was built without generated pot locations and a user requests
  `door_shuffle=basic,pot_shuffle=keys`
- **THEN** generation fails with the same pot-registry-missing error used for
  non-door pot seeds
- **AND** it does not silently normalize pots off
