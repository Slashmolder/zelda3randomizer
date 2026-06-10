## ADDED Requirements

### Requirement: Door-layout regeneration with a digest hard-fail (sidecar tail @76-79)

The per-seed door layout SHALL NOT be serialized into the sidecar slot. Instead it
SHALL be **regenerated deterministically from `(base_seed, settings,
door_attempt)`** at slot activation, mirroring the entrance-permutation
regeneration. The sidecar header SHALL claim the `reserved[4]` tail (after the
boomerang/bow/`prize_attempt` fields at @73–75) as: `door_attempt` at `@76` (the
accepted generation attempt, so a reject-and-retry generation that accepts attempt
*k* replays to the identical layout on load) and a **24-bit layout digest** at
`@77–79` (3 bytes LE on disk — the low 24 bits of `DoorShuffle_LayoutDigest` over
the ACCEPTED layout: pairings + key doors + thresholds + `bk_restricted`). Both
fields are zero on vanilla-door slots and for pre-field writers. The settings blob
already carries the door-shuffle axis; the share string already carries the seed —
no other persisted state is required.

Drift SHALL be **blocking** — NOT the non-blocking informational warning entrance
shuffle uses. At activation (`Rando_ActivateSidecarSlot`), BEFORE any slot state is
installed, a slot whose effective settings enable door shuffle SHALL regenerate the
layout from the share-string seed + persisted `door_attempt`, recompute the 24-bit
digest, and on generation failure or digest mismatch SHALL **refuse the slot**
(deactivate; treated as no-rando for this session) rather than silently load — a
drifted interior layout can make the certified-beatable serialized placement
unbeatable. The refusal is non-destructive: the slot file is untouched and remains
loadable by the build that wrote it.

#### Scenario: Door layout regenerates identically on load

- **WHEN** a door-shuffle slot is saved and reloaded under the same generator
  version
- **THEN** the layout regenerated from `(seed, settings, door_attempt)` digests
  equal to the persisted `@77–79` value, activation installs it (logic oracle +
  runtime redirect table + `kFeatures1_DoorShuffleActive`), and no door-layout
  bytes are stored in the slot

#### Scenario: Reject-and-retry replays to the accepted attempt

- **WHEN** generation rejected attempts 0..k-1 and accepted attempt k
- **THEN** `door_attempt == k` is persisted at `@76`, and activation regenerates
  attempt k directly to recover the identical accepted layout

#### Scenario: Layout drift blocks activation non-destructively

- **WHEN** a door-shuffle slot's regenerated layout digest differs from the
  persisted `@77–79` value (or regeneration fails outright) — e.g. the door pool or
  stitcher changed across builds
- **THEN** activation refuses the slot (deactivates, with a diagnostic) instead of
  silently loading a layout the placement was not certified against, and the slot
  file is left intact
