## ADDED Requirements

### Requirement: RegionRemap entrance-shuffle overlay shape

When `settings.entrance_shuffle != none`, `Rando_SetRegionRemap` SHALL be called with an entrance-permutation overlay table. The overlay shape generalizes the Phase B #4a Inverted overlay (Light↔Dark world swap) to a per-entrance permutation.

The overlay table SHALL be a `uint16[NUM_ENTRANCES]` array where index `i` maps the canonical entrance i to its shuffled-destination entrance id. Identity mapping (`overlay[i] == i`) SHALL be permitted for entrances that don't move under the active mode (e.g., Simple mode leaves multi-entrance dungeons identity-mapped).

The overlay SHALL be deterministically derived from `(share_string, generator_version, entrance_shuffle_mode)`. Same inputs SHALL produce byte-identical overlays across platforms.

> **Stub status**: exact `NUM_ENTRANCES` count + per-entrance ID assignment table deferred to `assets/rando/entrance_registry.yaml` authored at Phase C apply-time.

#### Scenario: Non-shuffle mode leaves overlay identity
- **WHEN** `settings.entrance_shuffle == none`
- **THEN** `Rando_SetRegionRemap` is either not called OR called with `overlay[i] == i` for all i; logic-graph behavior matches Phase A + Phase B non-Inverted

#### Scenario: Overlay determinism across platforms
- **WHEN** the same `(share_string, generator_version, entrance_shuffle=insanity)` is generated on Linux + macOS + Windows + Switch
- **THEN** the resulting entrance-permutation overlay bytes are identical across platforms; the corpus-determinism CI step catches drift

#### Scenario: Overlay consumed by predicate evaluation
- **WHEN** a predicate references `OP_REGION_REACHABLE <region_id>` and an entrance-shuffle overlay is active
- **THEN** the region accessor returns the shuffled destination's location set, NOT the canonical region's; reachability computation honors the overlay
