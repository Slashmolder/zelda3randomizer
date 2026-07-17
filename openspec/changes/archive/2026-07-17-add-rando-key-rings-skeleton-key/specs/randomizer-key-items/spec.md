## ADDED Requirements

### Requirement: Deterministic per-dungeon key-ring modes

The randomizer SHALL expose `key_rings=off|random|all`, default `off`. An eligible
family is a dungeon-specific small-key family with at least one copy in the
pre-ring shuffled item pool after active chest, pot, enemy-drop, and other
registered key sources are counted. `all` SHALL select every eligible family.
`random` SHALL use a seed-derived RNG domain independent of assumed-fill RNG and
SHALL select a non-empty proper subset whenever at least two families are eligible.
Generation with **effective** Random SHALL be refused with a diagnostic when fewer
than two families are eligible, because no mixed result exists. Requested Random
that resolves to effective Off under Vanilla/Retro SHALL generate normally with a
zero selected mask. The selection SHALL be reproducible from requested canonical
settings, their derived effective mode, and seed.

#### Scenario: Random always produces a mix

- **WHEN** `key_rings=random` and at least two key families are eligible
- **THEN** at least one eligible family uses a Key Ring and at least one eligible
  family retains ordinary small keys

#### Scenario: Random cannot silently stop being mixed

- **WHEN** `key_rings=random` remains effective and resolves against fewer than two
  eligible families
- **THEN** generation is refused with an eligibility diagnostic rather than
  silently producing Off or All behavior

#### Scenario: Effective Off does not require eligible families

- **WHEN** requested Random resolves to effective Off under Vanilla or Retro keys
- **THEN** generation succeeds with a zero selected-family mask

#### Scenario: Eastern is data-driven

- **WHEN** Eastern Palace has no shuffled small-key copy in the pre-ring pool
- **THEN** it is ineligible and no Eastern Key Ring is emitted
- **AND WHEN** an active itemized key source contributes an Eastern small key
- **THEN** Eastern becomes eligible under the same rule as every other family

#### Scenario: Selection does not perturb fill RNG

- **WHEN** two otherwise identical seeds differ only between `key_rings=off` and
  a ring mode
- **THEN** ring-family selection consumes no draw from the main assumed-fill RNG

### Requirement: One-check key-ring pool collapse

For every selected family, the shuffled pool SHALL contain exactly one
dungeon-specific Key Ring and zero ordinary shuffled small keys of that family.
All removed copies SHALL be replaced through normal junk padding so active
location count remains unchanged. Active pot and enemy key checks SHALL contribute
their key copies before the collapse. Non-itemized vanilla/free key drops MAY
remain as harmless surplus pickups.

#### Scenario: Selected key-heavy dungeon becomes one progression check

- **WHEN** Palace of Darkness is selected and its pre-ring pool contains six base
  keys plus active itemized key-source copies
- **THEN** the final pool contains exactly one Palace of Darkness Key Ring, no
  Palace of Darkness small-key items, and junk for the released slots

#### Scenario: Unselected family is unchanged

- **WHEN** Thieves' Town is eligible but not selected by Random
- **THEN** every ordinary shuffled Thieves' Town small-key copy remains in the
  pool and no Thieves' Town Key Ring is present

### Requirement: Key Ring logic and runtime grant

Holding a dungeon's Key Ring SHALL satisfy every small-key count predicate and
door-shuffle threshold for that dungeon, while retaining that dungeon's item
identity and Dungeon/Wild placement rules. Collecting it SHALL max-write the
existing saved/live per-dungeon key counter to the complete authored stock without
lowering a larger current value. Forced and forbidden placement predicates that
name the family's small key SHALL treat the selected ring as the same candidate.

#### Scenario: Ring satisfies a high key threshold

- **WHEN** a player/assumed inventory holds the Turtle Rock Key Ring and no
  ordinary Turtle Rock keys
- **THEN** `HAS_AMOUNT(SmallKey_TurtleRock, 4)` and the equivalent door-oracle
  threshold are satisfied

#### Scenario: Ring collected outside its dungeon

- **WHEN** the Palace of Darkness Key Ring is collected outside Palace of Darkness
- **THEN** the Palace of Darkness saved key slot receives the full stock and the
  current dungeon's live key counter is not credited

#### Scenario: Ring cannot lower keys

- **WHEN** the target saved/live counter already exceeds the generated full-stock
  value and its ring is collected
- **THEN** the counter remains at the larger value

### Requirement: Optional logic-neutral Skeleton Key

The randomizer SHALL expose `skeleton_key=false|true`, default false. Enabling it
SHALL add exactly one non-progression Skeleton Key by replacing one junk slot.
Once collected, it SHALL open any small-key door without decrementing a dungeon or
GenericKey counter. It SHALL NOT open big-key doors, set a big-key bit, satisfy any
small-key or big-key logic predicate, enter assumed inventory, or affect seed
beatability certification.

#### Scenario: Skeleton Key preserves regular keys

- **WHEN** the player owns Skeleton Key and three regular/current-dungeon keys and
  opens a locked small-key door
- **THEN** the door opens and the regular key count remains three

#### Scenario: Skeleton Key opens with zero keys

- **WHEN** the player owns Skeleton Key and has zero regular/current-dungeon keys
- **THEN** a locked small-key door opens through the normal persistent door-open
  path

#### Scenario: Big-key door remains locked

- **WHEN** the player owns Skeleton Key but not the dungeon's Big Key
- **THEN** a big-key door remains locked and shows the normal missing-Big-Key
  behavior

#### Scenario: Seed is certified without Skeleton Key

- **WHEN** a seed generated with `skeleton_key=true` is evaluated with Skeleton Key
  removed from every assumed/collected inventory
- **THEN** its reachability, sphere, and goal-completable results are unchanged

### Requirement: Key-item ownership is derived and reload-stable

The runtime SHALL derive Key Ring and Skeleton Key ownership from the installed
placement table plus checked-location bitmap, cache it for hot door/tracker queries,
and rebuild it after slot activation, checked-bitmap load, snapshot replay,
placement reinstall, and race reveal. Slot deactivation SHALL clear it. No separate
authoritative ownership bit SHALL be required.

#### Scenario: Reload rebuilds ownership

- **WHEN** a save with a checked Key Ring location and checked Skeleton Key location
  is reactivated
- **THEN** both ownership caches are restored before door or tracker queries

#### Scenario: Unchecked placement is not ownership

- **WHEN** the placement table contains Skeleton Key but its location is unchecked
- **THEN** the runtime reports Skeleton Key unowned
