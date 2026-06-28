## MODIFIED Requirements

### Requirement: Door-shuffle reachability via static oracle ops (Arch-2)

`OP_DOORS_LOC_REACHABLE(loc)` SHALL resolve both static door-table locations and
generated pot-door locations. Generated pot-door locations SHALL be emitted only when
local pot artifacts are present.

For generated pot-door rows, the oracle SHALL use the same installed
`DoorShuffleLayout`, portal gates, inventory counts, big-key state, and
door-explorer flood as static door-table locations. The active door-shuffle branch
for a pot location SHALL be true only when:

- the pot's tier is active under effective settings,
- all mapped door-table regions for that pot are reached by the door explorer, and
- the generated pot-specific predicate, if any, evaluates true.

The inactive branch SHALL preserve the existing vanilla/fork predicate, including
static `POT_KEYS_*` gates for non-door seeds and pinned dungeons.

The generated active door branch SHALL use a base pot predicate captured before
static vanilla-door `POT_KEYS_*` terms are appended. It SHALL NOT reuse the full
non-door pot predicate if that would carry static vanilla key-depth terms into an
active door-shuffle dungeon.

#### Scenario: Door oracle answers a pot location

- **WHEN** a generated pot location is in a dungeon whose bit is set in the installed
  door layout's shuffled mask
- **THEN** its wrapped predicate evaluates the `OP_DOORS_LOC_REACHABLE` branch
- **AND** the result comes from the door explorer instead of the static vanilla
  door-depth predicates

#### Scenario: Pinned dungeon pots use vanilla branch

- **WHEN** door shuffle is active but a pot belongs to a dungeon whose shuffled-mask
  bit is clear
- **THEN** `OP_DOORS_ACTIVE(dungeon)` is false for that dungeon
- **AND** the pot uses its existing vanilla/fork predicate

#### Scenario: Missing bridge artifact fails closed

- **WHEN** local pot artifacts are present but the key-depth artifact lacks
  generated door-pot room bindings
- **THEN** code generation fails with a clear error naming the stale artifact
- **AND** the build does not silently emit a partial door-pot bridge

#### Scenario: Pot outside the door graph keeps vanilla predicate

- **WHEN** a non-key pot's room has no generated door-table room binding
- **THEN** no generated door-pot row is emitted for that pot
- **AND** the pot continues to use the existing vanilla/fork predicate path

#### Scenario: Missing key-pot mapping fails closed

- **WHEN** local pot artifacts are present and a key pot cannot be mapped to its
  exact door-table region and static drop-key index
- **THEN** code generation fails with a clear error naming the key pot
- **AND** the build does not silently duplicate or lose a dungeon small-key source

#### Scenario: Active door pot ignores static key-depth terms

- **WHEN** a generated pot location is in an actively door-shuffled dungeon
- **THEN** the active branch evaluates door-oracle reachability plus the base pot
  predicate
- **AND** it does not also require the static vanilla-door `POT_KEYS_DUNGEON` or
  `POT_KEYS_WILD` depths
