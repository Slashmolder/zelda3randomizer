## ADDED Requirements

### Requirement: Door-layout regeneration via the header reserved tail

The per-seed door layout SHALL NOT be serialized into the sidecar slot. Instead it SHALL
be **regenerated deterministically from `(base_seed, settings, door_attempt)`** at slot
activation, mirroring the entrance-permutation regeneration. A `door_attempt` byte SHALL
be added at `@76` — the first byte of the sidecar header's `reserved[4]` tail (after the
boomerang/bow/`prize_attempt` fields at @73–75) — so that a reject-and-retry generation
that accepts attempt *k* replays to the identical layout on load. The settings blob already
carries the door-shuffle axes; the share string already carries the seed — no new persisted
settings are required.

Door-layout regeneration is **version-locked**, and for door-shuffle slots version drift
SHALL be **blocking** — NOT the non-blocking informational warning entrance shuffle uses.
Because a drifted interior layout can make the *certified-beatable serialized placement*
unbeatable (e.g. the big key now sits behind one more key-door than the placement assumed),
activation of a door-shuffle slot under a different `generator_version` SHALL refuse, or a
persisted layout digest SHALL hard-fail activation on divergence, rather than silently load
an unbeatable seed.

#### Scenario: Door layout regenerates identically on load

- **WHEN** a door-shuffle slot is saved and reloaded under the same generator version
- **THEN** the door layout, key-door placement, and per-location key thresholds are
  byte-identical to generation (regenerated from `base_seed` + `settings` + the persisted
  `door_attempt`), with no door-layout bytes stored in the slot

#### Scenario: Reject-and-retry replays to the accepted attempt

- **WHEN** generation rejected attempts 0..k-1 and accepted attempt k
- **THEN** `door_attempt == k` is persisted, and activation replays generation to attempt
  k to recover the identical accepted layout

#### Scenario: Version drift on a door-shuffle slot blocks activation

- **WHEN** a door-shuffle slot is loaded under a different `generator_version` than it was
  generated with
- **THEN** activation is refused (or a persisted layout digest hard-fails on divergence) —
  the slot is NOT silently loaded with a regenerated layout that could be unbeatable
