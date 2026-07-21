# randomizer-logic — delta for add-rando-ow-warp-shuffle

## ADDED Requirements

### Requirement: Generated overworld screen-component substrate underlies the zone graph

The logic graph SHALL gain a generated overworld screen-component region
layer: one region per walkable sub-screen component (ported from the upstream
`OWTileRegions` inventory by `assets/scripts/gen_ow_graph_tables.py` →
`ow_graph.gen.yaml` → `rando_logic_gen.py`), loaded as the LAST region group
so all pre-existing region ids are unchanged, with static edges wiring the
layer: unconditional component→zone edges for every component with
verified outdoor egress (components without any — cave-exit-only ledges,
enclosed waters/islands — are emitted as inert STUBS carrying no edges,
and the generator SHALL assert no warp target lands on a stub);
zone→component edges ONLY for the eight whirlpool water components, gated
on Flippers (general zone→component membership is deliberately absent —
ledges can be walked onto from a different zone across screen edges, so
the direction is unsound precisely where it matters, and no warp-shuffle
consumer needs it); DIRECTED drop edges (source-component →
landing-component, from the upstream one-way-ledge relation); and portal
edges (teleporter-hosting component → opposite-world zone) for
teleporters hosted on FLUTE-CANDIDATE components — the flute-critical
Desert/Mire class; a generic every-teleporter rule would over-grant for
enclosed targets like the Ice Palace island — each portal edge deriving
its predicate from the fork's existing hand-written teleporter zone edge
(cross-referenced at codegen; upstream carries no requirement data for
these) WITH ANY `CanFly` CONJUNCT STRIPPED — the zone edge conflates
reaching the portal (flute) with operating it (glove tier), and verbatim
reuse would compile false under an active flute shuffle, deterministically
severing e.g. Mire; codegen SHALL hard-fail if the stripped remainder is
empty or the predicate shape cannot be safely elided. Inter-screen
component adjacency is extracted as witness data for the quotient
cross-check and the sector flood but SHALL NOT become runtime edges (some
upstream links are item-gated only by source comments; ungated adjacency
would bypass the hand-written zone-edge predicates). The existing hand-written zone regions, zone edges, and
zone-referencing predicates SHALL remain intact and authoritative for
zone-level semantics (flute-class predicates are separately neutralized
under an active flute shuffle per the randomizer-ow-shuffle capability).
Because the component count pushes the region total past the current
reachability ceiling, this change SHALL raise `kReachabilityMaxRegions`
(256 → 512), resize and audit every structure and hardcoded use derived
from it, and add both a codegen budget check and a C static assert tying
the generated region count (plus the door-shuffle dynamic-region ceiling)
to the cap. Codegen SHALL additionally hard-fail unless: every
entrance-edge-override-eligible region id stays below the edge-override cap
(64); and the pinned name→id snapshot of all pre-existing regions matches.

#### Scenario: Existing ids and behavior are untouched
- **WHEN** the substrate lands and codegen runs with no OW axis in any
  settings
- **THEN** the pre-existing region name→id map is unchanged, and the full
  corpus is byte-identical under the 3-way diff ritual

#### Scenario: Capacity violation fails the build
- **WHEN** a future data change pushes total regions past
  `kReachabilityMaxRegions` or moves a dungeon-entry region id past the
  edge-override cap
- **THEN** codegen (and the C static assert) fail with an error naming the
  violated cap and the budget figures, rather than shipping regions the
  reachability walker silently never visits

### Requirement: Component graph is quotient-checked against the zone graph

Codegen SHALL collapse the component adjacency graph by zone membership and
cross-check it against the hand-written zone walk edges in both directions:
every derived zone adjacency must correspond to an existing zone edge or an
entry in a committed, sorted allowlist file, and every walk-class zone edge
must be witnessed by at least one component adjacency. Each allowlist entry
SHALL name the specific predicate, location, or edge that carries the
connectivity in the coarse graph, and codegen SHALL validate that the named
carrier exists and references both zones of the adjacency — a free-text
reason without a resolvable carrier is a codegen error, so the allowlist
cannot decay into a rubber stamp; allowlist growth is a review-visible diff.
Any unexplained mismatch SHALL fail codegen. This check is the substrate's correctness
oracle: the ported overworld data is validated against the fork's
hand-verified zone logic at build time, not by review claim.

#### Scenario: A missing ported edge is caught at build time
- **WHEN** the generated component adjacency omits a connection that the
  hand-written zone graph expresses as a walk edge
- **THEN** codegen fails naming the unwitnessed zone edge

### Requirement: Component regions are inert unless an OW axis is active

Reachability expansion SHALL skip the component region group entirely when no
overworld shuffle axis is active on the evaluated settings: component regions
occupy a contiguous id suffix and the walker bounds before it, so the
substrate adds no per-sphere cost and no behavioral surface to non-OW seeds
(the terrain inert-suffix precedent). With an axis active, component regions
participate fully.

#### Scenario: Non-OW seeds pay nothing
- **WHEN** any seed is generated with both OW axes off
- **THEN** sphere computation never visits component regions, and generation
  wall-time on the corpus's slowest entries is unchanged within noise

### Requirement: Per-seed warp edges ride a widened overlay under a loud cap

Per-seed warp connectivity SHALL be injected through the existing added-edge
overlay (`Rando_AddEntranceEdge`): eight per-seed hub→spot-component edges
and up to twelve directed Flippers-gated component edges for whirlpool
pairs (~20 slots). The `OW_FluteNet` hub region and its zone→hub feeder
edges are STATIC generated data (no overlay budget), and the feeders are
gated on the flute ACTIVATION macro — possession + activation only, never
`CanFly(world)`, whose neutralization under an active flute shuffle would
otherwise dead-end the hub. Because decoupled entrance mode alone adds up to 40 exit edges
(`kEntranceCaveInteriorCount`) and composes with cross-mode edges — already
at the 64-edge overlay cap's doorstep,
whose overflow today drops SILENTLY — this change SHALL raise the overlay
capacity to 128 and add a self-check asserting the combined post-injection
count of ALL consumers (entrance modes + warp axes) stays below the cap,
converting the silent-drop cliff into a loud failure. Edge-transition
shuffle's larger edge volume remains out of scope for the overlay and this
change.

#### Scenario: Maximal composition fits and is verified
- **WHEN** a seed composes decoupled+cross entrance shuffle with both warp
  axes on
- **THEN** all per-seed edges install (none dropped), and the self-check
  proves the combined count is below the widened cap

#### Scenario: Overlay overflow is loud
- **WHEN** any future settings composition would exceed the widened overlay
  capacity
- **THEN** the self-check fails identifying the consumers and counts, rather
  than silently dropping edges into phantom unreachability
