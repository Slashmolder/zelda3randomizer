## ADDED Requirements

### Requirement: Per-seed entrance reachability — two mechanisms by interior class

Entrance shuffle SHALL feed the logic graph through **two mechanisms**, matching the
two runtime exit classes (caves vs dungeons), and SHALL NOT use the Phase A
`RegionRemap` scaffold, which is **retired** (it remaps an `OP_REGION_REACHABLE`
operand — a region lookup — and would corrupt the 10+ live predicates that use that
opcode if populated; see change `design.md §1`/§2).

**Caves (single-interior locations).** A cave's chest is a *location* bound to an
overworld region; the graph has no cave-interior region and no door-edge into a cave
(verified: caves are `type: Chest` in `location_registry.yaml`, not regions). When a
cave entrance is shuffled, the reachability computation SHALL treat each cave-location
as belonging to the **overworld region of the door that now leads to it**, via a
per-seed location-region reassignment (the existing `region_override` field, driven by
the entrance permutation π instead of by world_state).

**Dungeons (first-class interior regions).** Dungeon interiors ARE regions with
inbound door-edges. When a dungeon entrance is shuffled, the reachability computation
SHALL traverse a per-seed edge graph in which each **dungeon door-edge's** destination
region is rewritten per π; internal dungeon edges and event gates SHALL remain fixed.

Placement and goal-completability SHALL reflect the active mechanism.

#### Scenario: Shuffled cave changes reachability via region reassignment
- **WHEN** a cave whose vanilla door is in Light World South is shuffled so its door
  is now in a region reachable only later
- **THEN** the cave-location's effective region becomes that later region, and the
  placer treats the cave's check as reachable only when that region is reachable

#### Scenario: Same-region cave swap is a reachability no-op
- **WHEN** two caves whose doors are both in the same overworld region swap entrances
- **THEN** reachability is unchanged (both were reachable iff that region was), though
  the runtime door destinations still differ

#### Scenario: Shuffled dungeon changes reachability via edge overlay
- **WHEN** entrance shuffle maps Eastern Palace's door to Palace of Darkness's interior
- **THEN** the per-seed edge graph routes the EP overworld region's door-edge to the
  PoD region, and reachability/placement treat PoD's interior as reached via EP's door

#### Scenario: Disabled entrance shuffle preserves Phase A reachability byte-for-byte
- **WHEN** all entrance axes are off (the default)
- **THEN** no π-driven region reassignment is applied and the edge graph equals the
  base static graph; reachability matches the Phase A baseline exactly (regression
  corpus digests unchanged)

#### Scenario: Internal dungeon edges are never shuffled
- **WHEN** a seed shuffles dungeon entrances (Stage 2+)
- **THEN** only door-edges (overworld-region → dungeon-region) are rewritten by π;
  edges representing a dungeon's internal room-to-room progression and event gates
  remain fixed

## REMOVED Requirements

### Requirement: Per-seed RegionRemap overlay for entrance shuffle

**Reason**: The Phase A `RegionRemap` accessor-overlay scaffold was retired — it
had no callers and would corrupt the 10+ live `OP_REGION_REACHABLE` predicates if
populated (it remaps a region-lookup operand). Entrance shuffle ships through the
two-mechanism per-seed reachability added by this change (cave location-region
reassignment + dungeon door-edge overlay), not a RegionRemap overlay. The baseline
already records the retirement under the Inverted "static world-state-keyed graph"
requirement.

**Migration**: None — the removed requirement described an unused scaffold. Entrance-
shuffle reachability is now specified by "Per-seed entrance reachability — two
mechanisms by interior class" (added above).
