## ADDED Requirements

### Requirement: Auto-generated region-bound pot locations

Pot locations SHALL be generated, not hand-authored. The committed
`gen_pot_tables.py` (see `randomizer-pot-sanity / Build-time pot enumeration with
stable identity`) SHALL emit one logic entry per pot, each bound to the logic
region that owns its dungeon room, with `can_reach: TRUE()` by default — so a pot
is reachable exactly when its region is, the existing region-default semantics
(`Location and region model with stable IDs`). Every pot entry SHALL carry a
`region:`; a pot with no region (which would encode `0xFFFF` = silent sphere-0
reachability) SHALL be a generator hard-error. **Same-room does NOT imply same
reachability** (split rooms, water states, crystal switches, one-way drops,
bomb/key/big-key doors, dark rooms, dungeon-state variants differ within one room),
so gate derivation SHALL distinguish:
- **Uniform room** (all authored locations in the room share one region and
  predicate): a pot inherits that predicate automatically.
- **Non-uniform room** (authored predicates differ, or the room is on a known
  multi-subregion list): the generator SHALL REQUIRE an explicit reviewed per-pot or
  per-subregion gate in a committed `pot_logic_overrides.yaml` and SHALL FAIL THE
  BUILD until one is supplied — no silent inheritance.

This makes a falsely-`TRUE()` pot impossible to ship without review. Pot locations
SHALL be included in the active location set **only when the `pot_shuffle` tier
selects them** — realized as a skip in the open-location / junk-pad / reachability
loops. `kRandoLocationsCount` (the static registry size) GROWS to ~1163; with
`pot_shuffle = Off` the active/open-location SET (and thus reachability and
placement) is byte-identical to the baseline because pots are skipped in iteration,
NOT because the count stays small. The location-id ceiling and reachability bitset SHALL be raised
from 512 to 2048 (`328 + 835 = 1163` locations at the maximal tier), including the
`LOC__COUNT <= 512` build-time assertion, kept in lockstep with the placement
working-array capacity (`randomizer-placement`).

#### Scenario: Pot is reachable iff its region is reachable
- **WHEN** a generated pot has `region: <room's region>` and `can_reach: TRUE()`
- **THEN** the reachability search marks it reachable exactly when its region is
  reachable, with no per-pot predicate evaluation

#### Scenario: A pot with no region fails the build
- **WHEN** `gen_pot_tables.py` would emit a pot location without a `region:`
- **THEN** the generator aborts with an error identifying the pot, preventing the
  silent sphere-0 reachability trap

#### Scenario: Dark-room pot requires light
- **WHEN** a pot is in a dark room and `pot_logic_overrides.yaml` sets its
  `can_reach` to `(HAS_ITEM(Lamp) OR CanDarkRoomNav())`
- **THEN** the placer treats it as reachable only with a Lamp or the dark-room-nav
  trick, like any dark-room chest

#### Scenario: Non-uniform room without a reviewed gate fails the build
- **WHEN** a room contains authored locations with differing `can_reach` predicates
  (or is on the multi-subregion list) and a pot there has no reviewed override
- **THEN** `gen_pot_tables.py` aborts with an error naming the pot/room, refusing to
  inherit a possibly-wrong predicate

#### Scenario: Active set unchanged with pot-shuffle off (count grows, iteration skips)
- **WHEN** `pot_shuffle = Off` (note `kRandoLocationsCount` has grown to ~1163)
- **THEN** no pot enters the active/open-location set — every pot is skipped in the
  collection / junk-pad / reachability loops — so reachability and placement are
  byte-identical to the pre-change build despite the larger registry

## MODIFIED Requirements

### Requirement: Reachability search with measured budget

The engine SHALL provide `Logic_ComputeReachability(inventory, settings) ->
(reachable_locations, cleared_regions)`. For the baseline graph (pot-shuffle off,
~216-328 locations — the 216 logic baseline up to the 328 current maximal pool) it
SHALL complete one invocation in under 5 ms on reference desktop hardware and under
20 ms on Switch. The active location graph grows with the `pot_shuffle` tier (up to
~1163 locations at `All`); for the pot-expanded graph one invocation SHALL complete
in under **30 ms on reference desktop and 120 ms on Switch** (provisional, ≈3.5× the
baseline since the added pot nodes are cheap `can_reach: TRUE()` predicates; to be
confirmed by measurement), and the placer SHALL keep `budget_seconds = 0` for
headless generation so placement stays machine-speed-independent.

#### Scenario: Monotonic in inventory
- **WHEN** an item is added to the inventory snapshot
- **THEN** the reachable-location set returned for the new inventory is a superset
  of the set returned for the previous inventory under the same settings

#### Scenario: Budget benchmark (baseline graph)
- **WHEN** `Logic_ComputeReachability` is called against the baseline graph
  (pot-shuffle off) on reference hardware
- **THEN** the median wall-clock across 1000 invocations is under 5 ms

#### Scenario: Pot-expanded graph stays within budget
- **WHEN** `pot_shuffle = All` and `Logic_ComputeReachability` runs against the
  ~1163-location graph on reference hardware
- **THEN** a single invocation completes in under 30 ms on reference desktop (120
  ms on Switch) — cheap `TRUE()` pot nodes — and generation determinism is preserved
  with `budget_seconds = 0`
