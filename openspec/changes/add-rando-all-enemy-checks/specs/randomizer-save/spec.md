## ADDED Requirements

### Requirement: All-enemy checked state fits persisted storage

The implementation SHALL measure the all-enemy location count with every compatible
location-expansion feature enabled. If the count exceeds current placement or
checked-location capacity, `enemy_drop_checks=all` SHALL NOT ship until placement
tables, checked-location bitmaps, sidecar payloads, snapshot payloads, spoilers,
trackers, and selftests are migrated together.

New all-enemy slots SHALL persist the checked bit for every emitted all-enemy
location and any domain-specific source-identity or suppression metadata required to
recover authored source identity after reload or snapshot restore. Save/reload and
snapshot restore SHALL suppress checked sources exactly as normal reload does and
SHALL preserve later source identities in the same spawn list. Missing or malformed
required metadata SHALL fail closed or deactivate randomizer state.

When door shuffle supports effective `enemy_drop_checks=all`, the non-key all-enemy
door bridge rows, bridge digest, and effective all-enemy tier SHALL be part of door
layout generation, the accepted door layout digest, sidecar activation, and snapshot
type-5 replay validation. Bridge drift, missing bridge metadata, or an effective-tier
mismatch SHALL fail activation/replay through the existing door-layout fail-closed
path.

#### Scenario: Capacity exceeded
- **WHEN** the generated all-enemy registry does not fit existing location or checked
  bitmap capacity
- **THEN** build/selftest fails or generation rejects `all` until a coordinated
  capacity migration is implemented

#### Scenario: Old slot without all state
- **WHEN** a pre-all slot is loaded
- **THEN** existing checked-location state decodes unchanged

#### Scenario: New all slot persists every emitted enemy check
- **WHEN** an all-enemy slot is saved after some all-tier enemy checks are collected
- **THEN** every emitted checked bit and required source-suppression metadata needed
  for those checks is persisted

#### Scenario: Reload suppresses checked sources
- **WHEN** an all-enemy slot is reloaded after an emitted source has been checked
- **THEN** that source is suppressed exactly as on normal room/area reload
- **AND** later sources in the same spawn list keep stable identity

#### Scenario: Snapshot lacks required all metadata
- **WHEN** an all-enemy snapshot restore requires source-identity metadata that is
  missing or malformed
- **THEN** restore fails closed or deactivates randomizer state rather than allowing
  duplicate or lost enemy checks

#### Scenario: Door all-enemy bridge drift fails replay
- **WHEN** a door-shuffle snapshot was written with effective all-enemy bridge data
  and the current build regenerates a different bridge digest or effective tier
- **THEN** snapshot replay fails closed instead of installing mismatched door
  reachability
