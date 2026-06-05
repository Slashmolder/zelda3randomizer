## MODIFIED Requirements

### Requirement: Entrance shuffle modes (Phase C)

The entrance-shuffle module SHALL be exposed as **composable boolean axes** —
`shuffle_cave_entrances`, `shuffle_dungeon_entrances`, `coupled` (default on),
`cross_category` — and SHALL maintain logic correctness: every required
dungeon and item remains reachable for the active goal under the resulting entrance
map. The named ALTTPR modes Simple, Restricted, and Crossed SHALL be
realized as **presets** over these axes (plus a Custom mode); each mode scenario
below is the contract its preset must satisfy. The **Insanity** preset (the
`decoupled` axis — per-endpoint cave/dungeon cross-mapping) is carved to follow-up
change `add-rando-entrance-shuffle-insanity`: its generation + logic landed but the
runtime cave-arrival replay path is blocked on an asset fork, so it is not yet
playable. See change `design.md §5a`.

**Phase C activation**: the composable entrance axes are added to the settings. The entrance permutation π SHALL be computed deterministically from `(share_string, generator_version)` and SHALL drive BOTH a runtime door overlay (`kOverworld_Entrance_Id`) and a per-seed logic edge overlay (per `randomizer-logic` — the retired `RegionRemap` scaffold is NOT used; see change `design.md §1`). The shuffle module SHALL run during generation such that the placer sees the entrance-remapped graph when computing reachability.

When all entrance axes are off (the default), the module SHALL be a no-op; no overlay is installed; non-shuffled seeds remain byte-identical in `placement_digest_hex`.

#### Scenario: Simple mode swaps single-entrance dungeons only
- **WHEN** entrance shuffle is Simple
- **THEN** only single-entrance dungeons are shuffled among themselves and multi-entrance dungeons retain their vanilla entrance layout

#### Scenario: Entrance shuffle preserves goal reachability
- **WHEN** an entrance-shuffled seed is generated
- **THEN** the goal-reachability predicate (per `randomizer-logic`) passes for the entrance map

#### Scenario: No entrance shuffle preserves Phase A behavior
- **WHEN** all entrance axes are off (the default)
- **THEN** no entrance overlay is installed; the logic graph behaves identically to Phase A + B non-Inverted seeds; `placement_digest_hex` for the seed matches the equivalent Phase B seed byte-for-byte

#### Scenario: Coupled is the default (enter A returns to A)
- **WHEN** `coupled` is enabled (the default) and the player enters shuffled door A then exits the interior
- **THEN** the player returns to overworld door A (not the interior's vanilla door)

#### Scenario: Single-entrance dungeon swap (low-risk subset)
- **WHEN** `shuffle_dungeon_entrances` is enabled and Eastern Palace's entrance maps to Palace of Darkness's interior
- **THEN** entering EP's overworld door loads PoD's interior, reachability treats PoD's interior as reached via EP's overworld region, and the dungeon's prize/medallion gates remain tied to the dungeon (not the door)

#### Scenario: Restricted mode shuffles within categories
- **WHEN** entrance shuffle is Restricted
- **THEN** overworld-to-cave entrances shuffle among themselves; dungeon-entrance pairs shuffle among themselves; no cross-category mappings

#### Scenario: Crossed mode permits cross-category mappings
- **WHEN** entrance shuffle is Crossed
- **THEN** cross-category mappings are permitted (an overworld door may lead to a dungeon's first room) but mapping pairs are still 1-to-1
