## ADDED Requirements

### Requirement: Door shuffle generation (basic, intensity 1, original door types)

The generator SHALL support a `door_shuffle` axis that randomizes the internal
door-to-door connections of dungeons. The committed scope is `door_shuffle ∈
{vanilla, basic}` at `intensity = 1` (Normal + spiral-staircase doors only) with
`door_type_mode = original` — vanilla per-dungeon SMALL-key-door **counts** with
positions re-chosen and prover-validated; big-key doors are NOT relocated. `basic`
SHALL shuffle door connections **within a single dungeon** (no cross-dungeon
pools).

MVP compatibility pins (normalized in `apply_derived_rules`, the entrance-axis
convention — silently coerced so the `settings_hash` matches the actually-generated
seed):

- Door shuffle is honored only on **Open/Standard** world states and only under
  **NoGlitches** logic (the door oracle models no glitch traversal; Retro collapses
  per-key-door thresholds; Inverted has its own logic tree); otherwise
  `door_shuffle` coerces to `vanilla`.
- Door shuffle is **mutually exclusive with entrance shuffle** (both redirect
  dungeon topology); door shuffle yields to an explicit entrance-shuffle request.
- Door shuffle **forces in-dungeon small AND big keys** (the prover's containment
  assumption + the `bk_restricted` ban require both).
- **Hyrule Castle is pinned in ALL world states** (forced escape start, Zelda
  escort, standard-mode key special-casing, sewers cross-exit modeling) and
  **Swamp Palace is pinned** (the only negated-event rule cluster + drain/flood
  runtime risk) — `kDoorShuffle_MvpDungeonMask` clears both bits; pinned dungeons
  keep their vanilla layout AND vanilla key doors.

The generated layout for each shuffled dungeon SHALL be **fully connected** (every
required room reachable) and **softlock-free**: no reachable ordering of key
collection forces spending the last available small key (or the big key) on a door
that strands all remaining progress. Generation SHALL be deterministic for a given
`(seed, settings, door_attempt)` — RNG seeded from `(base_seed, door_attempt)`,
every candidate list door-id-ordered before a draw, iteration-bounded, never
wall-clock (`budget_seconds = 0`).

The generator (implemented against the `shuffle_doors.h` contract:
`DoorShuffle_Generate` / `DoorShuffle_LayoutDigest` / `DoorExplore_Run`) SHALL
port, from the reference's **live** pipeline:

- the per-dungeon pool/builder path (`main_dungeon_pool` →
  `simple_dungeon_builder`) with the shared **portal analysis** fed by the same
  committed portal table the logic oracle uses — including reachable-vs-
  inaccessible portals, sole-entrance/required-passage marking, and the **Desert
  Back intensity-1 waiver** (the back lobby's split-dungeon constraint is waived at
  intensity 1, per the reference);
- the live stitcher `generate_dungeon` (`source/dungeon/DungeonStitcher.py` — NOT
  the deprecated `DungeonGenerator` sibling): `create_random_proposal` →
  `explore_proposal` (dual blue/orange crystal exploration) → `check_valid` (all
  regions + required paths) → `modify_proposal` local repairs, capped, exhaustion
  bumps `door_attempt`;
- the key-door prover: candidate search (including the pos<4 both-halves
  stateful-door constraint from `randomizer-door-runtime`),
  `find_valid_combination` with **deterministic sampling** (integer-only unbiased
  bounded draws, multiplicative `ncr` with overflow assert — no doubles, no
  platform-dependent modulo), and `validate_key_layout` (worst-case memoized
  search, drop-key economy, big-chest exclusion, self-locking-key rejection)
  emitting per-door worst-case thresholds and `bk_restricted`.

#### Scenario: Vanilla door shuffle is a no-op

- **WHEN** `door_shuffle == vanilla` (the default)
- **THEN** no door layout is generated or installed, reachability/placement match
  the baseline byte-for-byte, and the regression corpus digests are unchanged

#### Scenario: Incompatible settings coerce door shuffle off

- **WHEN** `door_shuffle == basic` is requested together with Inverted or Retro
  world state, a glitched logic tier, or entrance shuffle
- **THEN** `apply_derived_rules` normalizes `door_shuffle` to `vanilla` before
  serialization, so the canonical settings (and `settings_hash`) reflect what was
  actually generated

#### Scenario: Pinned dungeons keep vanilla layout and keys

- **WHEN** a basic seed is generated
- **THEN** Hyrule Castle and Swamp Palace are absent from the shuffled mask, keep
  their vanilla connections and vanilla key doors, and their logic evaluates the
  vanilla predicates

#### Scenario: Basic shuffle produces a connected, beatable dungeon

- **WHEN** `door_shuffle == basic` and a dungeon is shuffled
- **THEN** every required room (boss, prize, dungeon-item locations) is reachable
  under the generated layout from the dungeon's enterable portals, and the seed is
  beatable

#### Scenario: Key doors cannot softlock

- **WHEN** the key-door prover validates a candidate layout
- **THEN** it accepts the layout only if no reachable key-collection ordering
  spends the last available small/big key on a door leading solely to a dead end;
  otherwise it reduces the key-door count and retries, and exhaustion bumps
  `door_attempt`

#### Scenario: Determinism across platforms

- **WHEN** the same `(seed, settings, door_attempt)` is generated on different
  platforms
- **THEN** the door layout, key-door placement, per-door thresholds, and
  `DoorShuffle_LayoutDigest` are identical (id-sorted draw lists,
  iteration-bounded, no time budget, no floating point in sampling)

#### Scenario: Original door-type counts preserved

- **WHEN** `door_type_mode == original`
- **THEN** each shuffled dungeon keeps its vanilla count of small-key doors
  (relocated onto the new connections) and its big-key doors stay at their vanilla
  positions (no added or removed door types)
