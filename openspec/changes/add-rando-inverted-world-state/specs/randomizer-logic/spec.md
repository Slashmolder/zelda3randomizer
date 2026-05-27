## ADDED Requirements

### Requirement: Inverted world-state region graph

When `settings.world_state == Inverted`, the predicate VM SHALL evaluate against the Inverted region graph (Light World ↔ Dark World topology swapped, Link starts in Dark World as bunny). The Inverted graph SHALL be authored as YAML files under `assets/rando/logic_parts/` mirroring the Standard structure, with hand-translated per-location predicates sourced from `../alttp_vt_randomizer/app/Region/Inverted/` (2977 lines recursive, 24 files). Per-macro source-line citations SHALL appear in `audit.md §"Macro provenance"` under an Inverted-specific subsection.

The graph SHALL declare `LinksHouse_Inverted` as the start region; `kRandoStartRegionByWorldState[Inverted]` in `src/rando/rando_logic.c` SHALL be populated (currently `0xFFFF`).

> **Stub status**: full SHALLs and scenarios are deferred to `/openspec-explore` at apply-time. The exact set of Inverted regions and their edge predicates depends on the PHP translation findings; pre-specing them risks accuracy drift.

#### Scenario: Inverted graph activates only when world_state matches
- **WHEN** a seed is generated with `settings.world_state == Open` or `Standard`
- **THEN** the Inverted YAML files contribute zero edges to the active logic graph; placement output is byte-identical to seeds generated before Inverted YAML was authored

#### Scenario: Inverted seed has a valid start region
- **WHEN** a seed is generated with `settings.world_state == Inverted`
- **THEN** `kRandoStartRegionByWorldState[Inverted]` is not `0xFFFF`; `Logic_ComputeReachability` runs from the declared start region (`LinksHouse_Inverted`)

### Requirement: RegionRemap overlay activation

When `settings.world_state == Inverted`, the runtime SHALL call `Rando_SetRegionRemap` (scaffolded in Phase A `src/rando/rando_logic.c`) with the Inverted overlay table populated. The overlay SHALL swap Light World ↔ Dark World region accessors so the same `LOC_<...>` location id resolves to the inverted topology at access time.

> **Stub status**: overlay table contents deferred to apply-time. Shape (uint16 region_remap[NUM_REGIONS]) is fixed by Phase A scaffolding.

#### Scenario: Open mode is not remapped
- **WHEN** the world-state is Open
- **THEN** `Rando_SetRegionRemap` is not called; region accessors return the unremapped Light/Dark world topology

#### Scenario: Inverted mode is remapped at generation
- **WHEN** the world-state is Inverted and generation begins
- **THEN** `Rando_SetRegionRemap` is called with the populated Inverted overlay before the first reachability computation

### Requirement: world_state_filter for Inverted-specific locations

Phase A's `location_registry.yaml` carries a `world_state_filter` field per location (currently 0 = "universal — present in all world-states"). Phase B Inverted SHALL populate non-zero filter values for Inverted-specific locations (those that exist only in Inverted) and Inverted-exclusion locations (those that exist in Open/Standard but NOT in Inverted, due to entrance routing).

The set of filter-tagged locations SHALL be authored during the PHP-to-YAML translation pass; a count of filter-tagged locations SHALL appear in the implementing PR's description for review.

> **Stub status**: exact location list deferred to apply-time.

#### Scenario: Universal location appears in all world-states
- **WHEN** a location has `world_state_filter == 0` (universal)
- **THEN** the location is in the placement pool for Open, Standard, Inverted, and Retro seeds

#### Scenario: Inverted-only location appears only in Inverted seeds
- **WHEN** a location has `world_state_filter` bit Inverted set, and no other world-state bit set
- **THEN** the location is in the placement pool only when `settings.world_state == Inverted`
