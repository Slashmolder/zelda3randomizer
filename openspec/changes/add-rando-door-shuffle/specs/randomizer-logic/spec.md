## ADDED Requirements

### Requirement: Per-seed dungeon door-graph over per-room regions feeds reachability

Door shuffle SHALL feed the reachability/placement logic through a **per-seed dungeon
door-graph** built over **codegen'd per-room dungeon regions** (the reference's
room-granular region decomposition), NOT through an arbitrary per-seed predicate (the
shipped predicate-override seam carries only static-stream offsets and cannot synthesize a
per-seed predicate). The per-room regions SHALL be **inert when door shuffle is off** (the
base edge graph still routes through the single dungeon region), so `doorShuffle == vanilla`
reachability is byte-identical to the baseline.

When door shuffle is active, the logic SHALL, per seed: (a) reassign each dungeon location
to its **room region**; (b) wire the room-region connectivity of the generated layout as
per-seed door-edges (modeled on the entrance-shuffle per-seed added-edge / region-override
primitives, sized for the door graph); and (c) gate each small-key-door edge with a
**precompiled** `HAS_AMOUNT(SmallKey_<dungeon>, N)` predicate, where `N` is the key-door
prover's **worst-case** threshold for that door (selected by offset from a codegen'd
threshold family — no new VM op). Worst-case `N` is conservative-safe: it never certifies a
location reachable with fewer keys than reality, so it cannot ship an unbeatable seed.

#### Scenario: Disabled door shuffle preserves baseline reachability

- **WHEN** `doorShuffle == vanilla`
- **THEN** the per-room regions are unreferenced, the base edge graph is unchanged,
  reachability equals the baseline, and the regression corpus digests are unchanged

#### Scenario: Shuffled layout changes a location's key threshold

- **WHEN** a basic shuffle moves a chest from three key-doors deep to behind one key door
- **THEN** the per-seed door-edge into that room gates on the precompiled
  `HAS_AMOUNT(SmallKey_X, 1)` predicate (the prover's worst-case for the new layout), not
  the vanilla three, and the placer reflects the new threshold

#### Scenario: Item-gated intra-dungeon path is captured

- **WHEN** the only path to a dungeon location under the layout crosses a room that
  requires bombs (or fire) to traverse
- **THEN** the per-seed door-edge carries that item gate (a precompiled predicate), and the
  placer treats the location as reachable only once the item is held

#### Scenario: Reachability composes with global progression

- **WHEN** a shuffled dungeon location holds a progression item that gates the overworld
- **THEN** the per-room regions and their door-edges are walked inside the normal
  `Logic_ComputeReachability` fixed-point, so reaching that location (given items + keys)
  expands global reachability as expected

#### Scenario: Arbitrary per-seed predicate path is not used

- **WHEN** the implementation wires the per-seed key thresholds
- **THEN** it selects offsets into the codegen'd `HAS_AMOUNT(SmallKey_X, k)` family (and
  per-seed door-edges), and does NOT attempt to synthesize runtime predicate bytecode
  through `Rando_FindPredicateOverride` (which is static-stream-offset only)
