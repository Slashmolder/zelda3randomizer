## MODIFIED Requirements

### Requirement: Boss shuffle (Phase B)

The boss-shuffle module SHALL randomize the boss assigned to each dungeon's boss room from a configurable pool while keeping the dungeon's reward (crystal or pendant from prize shuffle) tied to the dungeon, not to the boss. Bosses required by the active goal (e.g., Agahnim 1 in Standard mode) SHALL remain at their required locations.

**Phase B activation**: `boss_shuffle` settings axis un-pinned (currently grayed out per Phase A). When `boss_shuffle == true`, the module runs at generation time after item placement so the boss assignments don't affect placement reachability (boss-kill predicates use boss-class identity macros like `CanKillMostThings`, not per-boss IDs).

Goal-required boss exception list (preserved at their canonical slots regardless of shuffle):
- Agahnim 1 (Hyrule Castle Tower) — Standard mode requires; Goal=Dungeons/AllDungeons may also require.
- Agahnim 2 (Ganon's Tower top) — Fast Ganon / Defeat Ganon goals require.
- Ganon (Pyramid) — every Ganon-requiring goal.

> **Stub status**: full configurable-pool definition + non-required boss enumeration deferred to apply-time ALTTPR PHP grep.

#### Scenario: Prize stays with the dungeon (not the boss)
- **WHEN** boss shuffle moves the Helmasaur King boss to Skull Woods AND prize shuffle has assigned the Skull Woods crystal as Crystal 3
- **THEN** defeating Helmasaur King in Skull Woods grants Crystal 3, not the Palace of Darkness vanilla crystal

#### Scenario: Required boss is preserved
- **WHEN** the goal is Standard and boss shuffle is enabled
- **THEN** Agahnim 1 remains at Hyrule Castle Tower regardless of shuffle settings

#### Scenario: Boss shuffle runs after item placement
- **WHEN** boss shuffle is enabled
- **THEN** the boss-assignment table is computed after `Place_AssumedFill` completes; the seed's spoiler `boss_assignments` section lists per-dungeon assignments

#### Scenario: Disabled boss shuffle preserves vanilla bosses
- **WHEN** `boss_shuffle == false`
- **THEN** every dungeon's boss is its vanilla boss; the spoiler `boss_assignments` section is omitted

### Requirement: Drop-pool shuffle (Phase B)

When enabled, the drop-pool shuffle SHALL randomize the contents of the eight tiered enemy drop tables. Drop-pool generation SHALL run after item placement so reachable spheres are known.

**Phase B activation**: `drop_pool_shuffle` settings axis un-pinned. Heart-drop guarantee SHALL be enforced: at least one drop table reachable in the first three placement spheres SHALL contain a heart-drop entry — preventing HP-starvation in early-game zones.

> **Stub status**: full per-tier drop-pool definition + heart-drop-guarantee algorithm deferred.

#### Scenario: Heart drop survives early game
- **WHEN** drop-pool shuffle is enabled
- **THEN** at least one of the drop tables reachable in the first three placement spheres contains a heart-drop entry

#### Scenario: Drop table is deterministic
- **WHEN** the same seed and drop-pool-shuffle setting are used
- **THEN** the generated drop tables are byte-identical across generations

#### Scenario: Drop-pool runs after item placement
- **WHEN** drop-pool shuffle is enabled
- **THEN** the drop tables are computed after `Place_AssumedFill` AND sphere computation completes; the spoiler `drop_tables` section is populated

#### Scenario: Disabled drop-pool preserves vanilla drops
- **WHEN** `drop_pool_shuffle == false`
- **THEN** every drop table is the vanilla table; the spoiler `drop_tables` section is omitted
