## ADDED Requirements

### Requirement: Assumed-fill awareness of per-seed dungeon key-door logic

The assumed-fill placer SHALL consume the per-seed dungeon door-graph (the per-door
worst-case key thresholds + item gates from `randomizer-logic`) when evaluating whether a
dungeon location is reachable for placement, AND SHALL honor the prover's `bk_restricted`
set as a **big-key placement ban** (the big key SHALL NOT be placed at any location the
prover marks as reachable only after passing the big-key door — a self-locking placement
the reachability gate alone cannot catch). In-dungeon containment
(`dungeon_mode_accepts_item`) SHALL remain consistent with the generated door layout: door
shuffle's committed scope requires `dungeon_small_keys_mode == Dungeon` AND
`dungeon_big_keys_mode == Dungeon`, so containment + the `bk_restricted` ban together keep
the big key beatably placed; an incompatible key mode SHALL be coerced (with a spoiler
note) or refused at generation. Placement determinism SHALL be preserved
(`budget_seconds = 0`). When `doorShuffle == vanilla`, placement is byte-identical to the
baseline.

#### Scenario: Placer respects per-seed key thresholds

- **WHEN** door shuffle is active and the placer evaluates a dungeon location
- **THEN** it uses the seed's per-location key threshold (not the vanilla count), so the
  assumed-fill certification matches what the player can actually reach

#### Scenario: In-dungeon key containment under shuffle

- **WHEN** `doorShuffle == basic` with in-dungeon small + big keys
- **THEN** each dungeon's small keys are placed only within that dungeon, consistent with
  the door layout, and the seed remains beatable

#### Scenario: Big key is never placed behind its own big-key door

- **WHEN** the prover marks a set of dungeon locations as reachable only after the big-key
  door (`bk_restricted`)
- **THEN** the placer forbids the big key from every such location, so no seed locks the
  big key behind the door it opens

#### Scenario: Incompatible key mode is coerced or refused

- **WHEN** a door-shuffle slot is requested with `dungeon_small_keys_mode` other than
  `Dungeon` (committed scope)
- **THEN** generation coerces the key mode to `Dungeon` with a spoiler note, or refuses
  the slot — it never certifies a layout whose containment assumption is violated

#### Scenario: Disabled door shuffle preserves placement

- **WHEN** `doorShuffle == vanilla`
- **THEN** the placer ignores the door solver, and placement is byte-identical to the
  baseline (regression corpus digests unchanged)
