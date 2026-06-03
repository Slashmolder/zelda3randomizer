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

### Requirement: World-state-keyed graph selection (Inverted)

When `settings.world_state == Inverted`, the predicate VM SHALL evaluate reachability against the Inverted override graph rather than the base (Standard/Open) graph. The override graph is built at codegen time from `assets/rando/logic_parts/inverted/**` into per-world-state override maps keyed by `world_state_id` (`kWorldState_Inverted`): `world_state_edges[Inverted]` carries the Inverted region edges and `world_state_overrides[loc_id][Inverted]` carries per-location predicate overrides (see `assets/rando_logic_gen.py`). At reachability time the generator selects these world-state-specific edges/overrides for Inverted seeds and the base maps for all other world states.

The Phase A `Rando_SetRegionRemap` accessor-overlay scaffold is NOT the activation mechanism — it had no callers and was retired in Phase C (see `src/rando/rando_logic.h`: "RETIRED in Phase C"). Inverted ships entirely through the static world-state-keyed graph above.

#### Scenario: Open/Standard mode uses the base graph
- **WHEN** the world-state is Open or Standard
- **THEN** reachability uses the base edge/override maps; the Inverted override map (`world_state_id == kWorldState_Inverted`) contributes nothing and `placement_digest_hex` is byte-identical to pre-Inverted seeds

#### Scenario: Inverted mode uses the world-state override graph at generation
- **WHEN** the world-state is Inverted and generation begins
- **THEN** the generator selects `world_state_edges[Inverted]` and `world_state_overrides[*][Inverted]` for the first and all subsequent reachability computations; no `Rando_SetRegionRemap` call occurs

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
