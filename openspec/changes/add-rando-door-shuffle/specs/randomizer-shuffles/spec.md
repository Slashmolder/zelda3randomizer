## ADDED Requirements

### Requirement: Door shuffle generation (basic, intensity 1, original door types)

The generator SHALL support a `doorShuffle` axis that randomizes the internal door-to-door
connections of dungeons. The committed scope is `doorShuffle ∈ {vanilla, basic}` at
`intensity = 1` (Normal + spiral-staircase doors only) with `door_type_mode = original`
(vanilla per-dungeon key/door counts) and small keys restricted in-dungeon. `basic`
SHALL shuffle door connections **within a single dungeon** (no cross-dungeon pools).

The generated layout for each dungeon SHALL be **fully connected** (every required room
reachable from the dungeon entrance) and **solvable without a softlock**: for every
reachable ordering of key collection, the player SHALL never be forced to spend the last
available small key (or the big key) on a door that strands all remaining progress.
Generation SHALL be deterministic for a given `(seed, settings, door_attempt)` and SHALL
NOT depend on wall-clock time (`budget_seconds = 0`).

The generator SHALL port, from the reference, (a) sector construction + a crystal-barrier
(blue/orange)-aware reachability primitive, (b) a randomized-proposal + local-repair
stitcher, and (c) a key-door softlock prover (worst-case memoized search). The
cross-dungeon polarity/sector-distribution engine is **out of scope** (single-dungeon
`basic` uses the trivial single-builder path).

#### Scenario: Vanilla door shuffle is a no-op

- **WHEN** `doorShuffle == vanilla` (the default)
- **THEN** no door layout is generated, reachability/placement match the baseline
  byte-for-byte, and the regression corpus digests are unchanged

#### Scenario: Basic shuffle produces a connected, beatable dungeon

- **WHEN** `doorShuffle == basic` and a dungeon is shuffled
- **THEN** every required room (boss, prize, dungeon-item locations) is reachable from the
  dungeon entrance under the generated layout, and the seed is beatable

#### Scenario: Key doors cannot softlock

- **WHEN** the key-door prover validates a candidate layout
- **THEN** it accepts the layout only if no reachable key-collection ordering spends the
  last available small/big key on a door leading solely to a dead end; otherwise it
  reduces the key-door count and retries until a solvable layout is found

#### Scenario: Determinism across platforms

- **WHEN** the same `(seed, settings, door_attempt)` is generated on different platforms
- **THEN** the door layout, key-door placement, and per-location key thresholds are
  identical (iteration-bounded, no time budget)

#### Scenario: Original door-type counts preserved

- **WHEN** `door_type_mode == original`
- **THEN** each dungeon keeps its vanilla count of small-key / big-key doors, relocated
  onto the shuffled connections (no added or removed door types)
