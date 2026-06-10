## ADDED Requirements

### Requirement: Door-shuffle reachability via static oracle ops (Arch-2)

Door shuffle SHALL feed the reachability/placement logic through **two static
predicate-VM ops** — `OP_DOORS_ACTIVE(dungeon)` and `OP_DOORS_LOC_REACHABLE(loc)`
(op_registry ids 20/21, evaluated in `rando_logic.c`) — backed by a **reachability
oracle** that runs the SAME crystal-aware explorer the stitcher and key-door prover
use (`DoorExplore_Run`, per the `shuffle_doors.h` contract), so generator and logic
cannot drift. This replaces the originally-designed static per-room-region port
(579 regions + per-seed door-edge arrays + precompiled threshold families): no
region-cap bump, no per-seed edges, no per-location threshold offsets.

The codegen (`rando_logic_gen.py`) SHALL wrap every door-controlled location's
`can_reach` as:

```
(NOT DOORS_ACTIVE(d) AND <vanilla>) OR (DOORS_ACTIVE(d) AND DOORS_LOC_REACHABLE(loc))
```

`OP_DOORS_ACTIVE(d)` SHALL be true iff a door layout is installed AND the layout's
`shuffled_mask` has dungeon `d`'s bit set — so pinned dungeons (Hyrule Castle, Swamp
Palace at MVP) and `doorShuffle == vanilla` evaluate **exactly the vanilla predicate**,
and disabled door shuffle is byte-identical to the baseline (corpus invariant).
`OP_DOORS_LOC_REACHABLE` SHALL return false when no layout is installed.

Key-door edges inside the oracle SHALL gate on the prover's **worst-case** per-door
thresholds, which are conservative-safe: the oracle never certifies a location
reachable with fewer keys than reality, so it cannot ship an unbeatable seed.

#### Scenario: Disabled door shuffle preserves baseline reachability

- **WHEN** `doorShuffle == vanilla` (no layout installed)
- **THEN** `OP_DOORS_ACTIVE` is false for every dungeon, every wrapped predicate
  evaluates its vanilla branch, reachability equals the baseline, and the regression
  corpus digests are unchanged

#### Scenario: Pinned dungeon evaluates vanilla under an active layout

- **WHEN** a basic-shuffle layout is installed and the location's dungeon bit is
  clear in `shuffled_mask` (Hyrule Castle or Swamp Palace at MVP)
- **THEN** `OP_DOORS_ACTIVE(d)` is false and the location's vanilla predicate (and
  vanilla key thresholds) apply unchanged

#### Scenario: Shuffled layout changes a location's key threshold

- **WHEN** a basic shuffle moves a chest from three key-doors deep to behind one
  key door
- **THEN** `OP_DOORS_LOC_REACHABLE` floods the layout with the prover's worst-case
  per-door thresholds and reports the location reachable once one key is available,
  and the placer reflects the new threshold

#### Scenario: Reachability composes with global progression

- **WHEN** a shuffled dungeon location holds a progression item that gates the
  overworld
- **THEN** the oracle is queried inside the normal `Logic_ComputeReachability`
  fixed-point (memoized per dungeon per pass, invalidated each pass because the
  region bitset grows; a stale-by-one-pass cache can only underestimate, and the
  zero-change exit pass evaluates with exact inputs), so reaching that location
  expands global reachability as expected

#### Scenario: Arbitrary per-seed predicate path is not used

- **WHEN** the implementation wires per-seed door reachability
- **THEN** it evaluates the two STATIC ops against per-seed C state (the installed
  `DoorShuffleLayout` — the `OP_MEDALLION_OPENS`/`OP_CAN_KILL_BOSS` precedent), and
  does NOT attempt to synthesize runtime predicate bytecode through
  `Rando_FindPredicateOverride` (which is static-stream-offset only)

### Requirement: Oracle portal seeding from a committed gate table

The oracle SHALL seed its flood from the dungeon's **portal lobbies** — not from a
single entrance — using the committed per-portal gate table
(`assets/rando/door_portals.yaml`, compiled to `kDoorPortalGates`). Each row maps a
reference portal lobby region to the **fork region** whose reachability gates it,
plus an optional extra predicate compiled into the static stream. A portal seeds
the flood only when its fork region is set in the live region bitset AND its
predicate (if any) holds. Gate predicates SHALL reference fork regions and items
only (never `OP_DOORS_*`), keeping the seeding monotone with no oracle recursion.
A null fork region means the portal never independently seeds the flood (it remains
a valid stitch target reachable from inside).

The SAME table SHALL supply the generator's portal analysis, so generation and
logic agree on which portals are enterable.

#### Scenario: Multi-portal dungeon seeds only reachable portals

- **WHEN** the oracle floods a multi-portal dungeon (e.g. Desert Palace) and only
  the front-lobby fork region is reachable without lifting rocks
- **THEN** the flood seeds from the front lobbies only; the Back-lobby portal joins
  the seed set once its gate (`CanLiftRocks()`) holds — a too-strong gate can only
  under-seed (the placer refuses; it never certifies an unbeatable seed)

#### Scenario: Portal gates evaluate without oracle recursion

- **WHEN** a portal gate predicate is evaluated during an oracle flood
- **THEN** it runs in the normal VM context against regions/items only and cannot
  re-enter the oracle (`g_in_door_oracle` guards the invariant)

### Requirement: Drop-key economy shared by prover and oracle

Small-key availability SHALL be computed identically by the key-door prover and
the logic oracle: `held_keys = chest-key count (from RandoCounts) + reachable
drop-key rooms in the current exploration state` (the codegen'd `kDoorTblDropKeys`
table of forced small-key drop rooms). The placer's item counts stay chest-only —
the drop contribution is supplied inside the explorer, never double-counted.

#### Scenario: Reaching a drop-key room raises the effective key count

- **WHEN** the oracle's flood reaches a room in the dungeon's drop-key table while
  Link holds N chest keys
- **THEN** key-door edges of that dungeon evaluate against N+1 (and the prover used
  the same definition when it validated the layout, so the two never desync)
