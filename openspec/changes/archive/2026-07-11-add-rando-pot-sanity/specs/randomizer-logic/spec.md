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

This makes a falsely-`TRUE()` pot impossible to ship without review. Additionally, a
pot in a dungeon whose pot keys are shuffled SHALL carry the small-key gate of
`Pot-key small-key logic gating` — a per-pot key-door-depth term layered onto its
region/inheritance predicate (so the `can_reach: TRUE()` default is the *base*, not the
final predicate, once `pot_shuffle` + a shuffled key mode are active). Pot locations
SHALL be included in the active location set **only when the `pot_shuffle` tier
selects them** — realized as a skip in the open-location / junk-pad / reachability
loops. `kRandoLocationsCount` (the static registry size) GROWS to ~1127; with
`pot_shuffle = Off` the active/open-location SET (and thus reachability and
placement) is byte-identical to the baseline because pots are skipped in iteration,
NOT because the count stays small. The location-id ceiling and reachability bitset SHALL be raised
from 512 to 2048 (`328 + 799 = 1127` locations at the maximal tier), including a
`LOC__COUNT <= 2048` build-time assertion, kept in lockstep with the placement
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
- **WHEN** `pot_shuffle = Off` (note `kRandoLocationsCount` has grown to ~1127)
- **THEN** no pot enters the active/open-location set — every pot is skipped in the
  collection / junk-pad / reachability loops — so reachability and placement are
  byte-identical to the pre-change build despite the larger registry

### Requirement: Pot-key small-key logic gating

A pot-bearing dungeon's deep locations and pots SHALL gate on the small-key
requirement that pot_shuffle adds once that dungeon's pot keys become shuffled items.
Three predicate-VM ops drive this, all false (so the wrap is inert and placement is
byte-identical) when pots are off:

- `OP_POT_KEYS_ON` — `Settings_PotKeysActive`: `pot_shuffle >= Keys` AND pots are not
  forced off by effective cave-entrance shuffle (`Settings_PotShuffleForcedOff`) — the
  SAME shared accessor `pot_active` / `BuildItemPool` use, so the gate can never drift
  from which pots are actually pooled. Door shuffle does not force this op false; door
  reachability is handled by the baseline `randomizer-logic / Door-shuffle reachability
  via static oracle ops`. The placer/logic VM consume RAW settings, so this MUST read the
  accessor, never raw `pot_shuffle` or cave fields.
- `OP_POT_KEYS_WILD` — `OP_POT_KEYS_ON` AND small keys are wild (keysanity / Retro).
- `OP_POT_KEYS_DUNGEON` — `OP_POT_KEYS_ON` AND small keys are dungeon (per-dungeon).

A pot-bearing dungeon's affected `can_reach` (its chest/boss/prize locations and its
in-dungeon pots) SHALL be wrapped:

`<vanilla predicate> AND (NOT POT_KEYS_WILD() OR HAS_AMOUNT(SmallKey_X, full)) AND (NOT POT_KEYS_DUNGEON() OR HAS_AMOUNT(SmallKey_X, dungeon))`

- `full` is the prover WORST-CASE key-door count, CAPPED at the pooled key count
  (chest + pot keys): under wild keys the keys live anywhere in the world, so you must
  HOLD that many before reaching; the non-pot drops auto-collect in-context so the cap
  is the true external requirement.
- `dungeon` is the prover SHORTEST-PATH (MIN-depth) key-door count: under dungeon keys
  the keys are collected en route, so the graduated min-depth is necessary + sufficient
  for a known layout — a flat worst-case would be circular (a key sits behind the very
  door it opens). The dungeon term is REQUIRED, not redundant: the vanilla `cur` value
  assumes the pot keys drop FREE, so a location whose keys are now items is UNDER-gated
  until this term raises it to min-depth.
- KEY pots SHALL use their EXACT region min-depth (over-gating a key pot is circular →
  spurious refuse; under-gating strands); loot / empty pots SHALL use the room-MAX
  min-depth (they hold no key, so over-gating only delays the check and never strands);
  chest/boss/prize SHALL use their own location min-depth.

The depths SHALL be generated, not hand-authored: `assets/scripts/gen_pot_key_depth.py`
runs the door key-door prover (`--dump-key-depth`, which emits both worst-case `depth`
and shortest-path `mindepth`) and emits local gitignored
`assets/rando/pot_key_depth.gen.yaml`, cross-checking the per-key-pot depths against a
reviewed table so a join drift fails the build. `rando_logic_gen.py` applies the wrap
from that table.

#### Scenario: Wild keys gate the held worst case
- **WHEN** a seed has wild keys + `pot_shuffle` and the player must reach a deep
  location whose pot keys are now world items
- **THEN** its `can_reach` requires `HAS_AMOUNT(SmallKey_X, full)` (worst case capped
  at chest+pot keys), so the placer never strands a progression item behind keys the
  player cannot yet hold

#### Scenario: Dungeon keys gate the in-context min-depth
- **WHEN** a seed has dungeon keys + `pot_shuffle`
- **THEN** each affected location requires `HAS_AMOUNT(SmallKey_X, dungeon)` (the
  shortest-path key-door count); a key pot uses its exact region depth and a loot/empty
  pot the room max, so the keys collected en route always suffice and a key is never
  placed behind its own door

#### Scenario: Pots off leaves the gating inert
- **WHEN** `pot_shuffle = Off` (or vanilla keys, or effective cave-entrance shuffle
  forces pots off)
- **THEN** `POT_KEYS_WILD` and `POT_KEYS_DUNGEON` both evaluate false, the wrapped
  terms collapse to the vanilla predicate, and reachability + placement are
  byte-identical to the pre-feature build

#### Scenario: Door shuffle keeps pot-key gates active
- **WHEN** `door_shuffle != vanilla`, `pot_shuffle >= Keys`, small keys are shuffled,
  and effective cave-entrance shuffle is off
- **THEN** `POT_KEYS_WILD` or `POT_KEYS_DUNGEON` evaluates according to the effective
  key mode, so door-shuffled active pots are gated by the door-pot baseline rather than
  being collapsed to pots-off

## MODIFIED Requirements

### Requirement: Reachability search with measured budget

The engine SHALL provide `Logic_ComputeReachability(inventory, settings) ->
(reachable_locations, cleared_regions)`. For the baseline graph (pot-shuffle off,
~216-328 locations — the 216 logic baseline up to the 328 current maximal pool) it
SHALL complete one invocation in under 5 ms on reference desktop hardware and under
20 ms on Switch. The active location graph grows with the `pot_shuffle` tier (up to
~1127 locations at `All`); for the pot-expanded graph one invocation SHALL complete
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
  ~1127-location graph on reference hardware
- **THEN** a single invocation completes in under 30 ms on reference desktop (120
  ms on Switch) — cheap `TRUE()` pot nodes — and generation determinism is preserved
  with `budget_seconds = 0`
