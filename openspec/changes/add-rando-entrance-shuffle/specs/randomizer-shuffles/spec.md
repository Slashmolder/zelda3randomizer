## MODIFIED Requirements

### Requirement: Entrance shuffle modes (Phase C)

The entrance-shuffle module SHALL support four modes — Simple, Restricted, Crossed, Insanity — and SHALL maintain logic correctness: every required dungeon and item remains reachable for the active goal under the resulting entrance map.

**Phase C activation**: `entrance_shuffle` settings axis un-pinned. The entrance permutation SHALL be computed deterministically from `(share_string, generator_version)` and produced as a `RegionRemap_*` overlay consumed by the logic graph. The shuffle module SHALL run during generation, AFTER prize/medallion shuffle and item placement (so the placer sees the entrance-remapped graph when computing reachability).

When `entrance_shuffle == none` (Phase A default), the module SHALL be a no-op; the RegionRemap overlay is not installed; non-Phase-C seeds remain byte-identical in `placement_digest_hex`.

> **Stub status**: full per-mode algorithm + goal-preservation retry strategy deferred to Phase C `/openspec-explore`. RegionRemap overlay shape depends on Phase B #4a Inverted's production-grade activation.

#### Scenario: Simple mode swaps single-entrance dungeons only
- **WHEN** entrance shuffle is Simple
- **THEN** only single-entrance dungeons are shuffled among themselves and multi-entrance dungeons retain their vanilla entrance layout

#### Scenario: Insanity mode permits cave-to-dungeon mappings
- **WHEN** entrance shuffle is Insanity
- **THEN** any overworld entrance may map to any interior, including cross-mappings between cave and dungeon interiors

#### Scenario: Entrance shuffle preserves goal reachability
- **WHEN** an entrance-shuffled seed is generated
- **THEN** the goal-reachability predicate (per `randomizer-logic`) passes for the entrance map

#### Scenario: No entrance shuffle preserves Phase A behavior
- **WHEN** `entrance_shuffle == none`
- **THEN** RegionRemap is not installed (or installed as identity); the logic graph behaves identically to Phase A + B non-Inverted seeds; `placement_digest_hex` for the seed matches the equivalent Phase B seed byte-for-byte

#### Scenario: Restricted mode shuffles within categories
- **WHEN** entrance shuffle is Restricted
- **THEN** overworld-to-cave entrances shuffle among themselves; dungeon-entrance pairs shuffle among themselves; no cross-category mappings

#### Scenario: Crossed mode permits cross-category mappings
- **WHEN** entrance shuffle is Crossed
- **THEN** cross-category mappings are permitted (an overworld door may lead to a dungeon's first room) but mapping pairs are still 1-to-1
