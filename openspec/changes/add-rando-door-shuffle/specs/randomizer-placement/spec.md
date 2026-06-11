## ADDED Requirements

### Requirement: Assumed-fill awareness of per-seed dungeon key-door logic

The assumed-fill placer SHALL see the per-seed door layout through the wrapped
location predicates (the `OP_DOORS_*` oracle from `randomizer-logic` — per-door
worst-case key thresholds + item gates), AND SHALL honor the prover's
`bk_restricted` set as a **big-key placement ban** evaluated alongside
`dungeon_mode_accepts_item` (`DoorShuffle_BkRestricted` against the installed
layout): the big key SHALL NOT be placed at any location the prover marks as
reachable only past the dungeon's big-key door — a self-locking placement the
reachability gate alone cannot catch. The ban is inert when no layout is
installed. In-dungeon containment SHALL remain consistent with the generated
layout: the committed scope forces `dungeon_small_keys_mode == Dungeon` AND
`dungeon_big_keys_mode == Dungeon` (normalized in `apply_derived_rules`, see
`randomizer-shuffles`), so containment + the `bk_restricted` ban together keep the
big key beatably placed. The generation pipelines SHALL wrap placement in a
`door_attempt` retry loop: a layout whose placement or accessibility check fails
tries the next attempt. Placement determinism SHALL be preserved
(`budget_seconds = 0`). When `door_shuffle == vanilla`, placement is
byte-identical to the baseline.

#### Scenario: Placer respects per-seed key thresholds

- **WHEN** door shuffle is active and the placer evaluates a dungeon location
- **THEN** the wrapped predicate queries the oracle under the seed's layout (not
  the vanilla key count), so the assumed-fill certification matches what the
  player can actually reach

#### Scenario: In-dungeon key containment under shuffle

- **WHEN** `door_shuffle == basic` with the forced in-dungeon small + big keys
- **THEN** each dungeon's small keys are placed only within that dungeon,
  consistent with the door layout, and the seed remains beatable

#### Scenario: Big key is never placed behind its own big-key door

- **WHEN** the prover marks a set of dungeon locations as reachable only past the
  big-key door (`bk_restricted`)
- **THEN** the placer forbids the big key from every such location, so no seed
  locks the big key behind the door it opens

#### Scenario: Failed placement tries the next door attempt

- **WHEN** a generated layout passes the prover but assumed-fill or the
  accessibility tier rejects the resulting placement
- **THEN** the pipeline uninstalls the layout, bumps `door_attempt`, and retries
  (bounded), persisting the accepted attempt + digest with the slot

#### Scenario: Disabled door shuffle preserves placement

- **WHEN** `door_shuffle == vanilla`
- **THEN** the placer never consults the door oracle or the ban, and placement is
  byte-identical to the baseline (regression corpus digests unchanged)
