# Design: OW screen-graph foundation + warp shuffle

## Context

Three research threads ground this design (all claims re-verified against
source during authoring; upstream citations are on branch
`origin/OverworldShuffleDev` of `../ALttPDoorRandomizer`, MIT):

1. **Engine**: overworld edge transitions resolve in
   `OverworldHandleTransitions` (`src/overworld.c`) by geometric-neighbor
   arithmetic; the destination, map16 streaming offset, mosaic-vs-scroll fork,
   and `Overworld_SetCameraBoundaries` inputs form a coupled tableau. This
   change deliberately does NOT touch that machinery. Flute travel and
   whirlpools resolve through `Overworld_LoadBirdTravelPos` — an atomic
   teleport that installs the full arrival tableau (scroll, coords, camera,
   screen properties) from per-spot rows in runtime assets
   (`kBirdTravel_ScreenIndex` = `g_asset_ptrs[113]` et seq., `assets.h`);
   whirlpool exits are the same table at partner-index + 9
   (`FindPartnerWhirlpoolExit`). The flute select UI runs in `messaging.c`
   (`Module0E_0A_FluteMenu` → `FluteMenu_HandleSelection` /
   `FluteMenu_LoadTransport`).
2. **Upstream model**: `OWEdges.py` defines the edge/sub-region inventory
   (`OWTileRegions` sub-screen regions, `one_way_ledges`,
   `parallel_links`, `default_whirlpool_connections`);
   `OverworldShuffle.py` shuffles whirlpools by world-partitioned re-pairing;
   `source/overworld/FluteShuffle.py` defines spot candidates (`flute_data`,
   keyed by overworld screen id) and the `balanced` proximity rule (flood an
   ignore-set through same-screen/adjacent regions and one-way ledges; reject
   spots whose ignore-set overlaps an already-used one).
3. **Fork logic graph**: the generated region table currently has
   `kRandoRegionsCount = 40` regions (generated `logic_data.c` is the truth —
   YAML region entries merge by id under the last-wins rule, so file entry
   counts mislead; door shuffle additionally consumes dynamic region ids
   above the static table). The overworld surface is ~11 zones;
   mirror/flute reachability lives inside location predicates
   (`CanFly(world)`, `HAS_ITEM(MagicMirror) AND OP_REGION_REACHABLE(<DW
   zone>)`); the flute item's registry name is `OcarinaInactive` (item id
   30, see `logic_parts/31_misery_mire.yaml`); whirlpools are unmodeled.
   Capacity facts (source-verified): the true region-count ceiling is
   `kReachabilityMaxRegions = 256` in `rando_logic.c` (sizes
   `region_bitset`, `g_inverted_pair_set`, and the expansion count);
   `kEntranceRegionOverrideMax` (512) is a LOCATION-id-indexed override
   array, not a region cap; `kEntranceAddedEdgeMax` (64) silently drops
   overflow; decoupled entrance mode adds up to
   `kEntranceCaveInteriorCount = 40` exit edges.

## D1 — Screen-component substrate: components attach to zones, zones stay

**Decision.** Add one generated region per overworld walkable component
(ported from upstream `OWTileRegions`), as a new **last-loaded** region group.
Keep the existing ~11 hand-written zones and ALL their edges untouched.
Wire components to zones with static generated edges:

- `component → zone` edges (unconditional) for every component with
  verified outdoor egress (undirected adjacency, a drop out, an
  ow_connections outbound link, a teleporter, or whirlpool travel) —
  standing on such a component implies reaching the zone's mass by some
  outdoor means. Components WITHOUT outdoor egress (cave-exit-only ledges
  like the Dark Death Mountain ledges, enclosed waters, the Ice Palace
  island) are emitted as **inert stubs**: no `comp → zone` edge, and the
  generator asserts no warp target (flute candidate, whirlpool water, drop
  landing, portal target) can land on one. **`zone → component` edges are
  deliberately absent** except the whirlpool-water case below —
  implementation showed the direction is both unnecessary (spots, drops,
  and portals all point AT components; walk-reachable teleporters keep
  their hand-written zone edges) and unsound for exactly the interesting
  components (Bumper Cave Ledge is walked onto from a DIFFERENT zone
  across screen edges, so "its zone grants it" is false). [Reconciled
  from the pre-implementation draft, which specified bidirectional blob
  membership; the generator's egress audit invalidated that model.]
- `zone → whirlpool-water-component` edges gated `HAS_ITEM(Flippers)` for
  the 8 whirlpool waters only (the entry side of whirlpool travel).
- **directed drop edges** from `one_way_ledges` — modeled as
  `source-component → landing-component` edges (upstream's table maps each
  LANDING to its set of drop SOURCES, and sources are frequently primary
  walkable blobs like `West Death Mountain (Top)`, not "ledge-class"
  components). Drops are an edge property, orthogonal to zone membership: a
  drop source that is a blob member keeps its bidirectional membership
  edges, while components with no walk-in access at all (e.g. `Desert
  Teleporter Ledge`) simply have no inbound walk/membership edge — a fact
  that falls out of the adjacency data rather than a hand-assigned "ledge"
  tag.
- `component ↔ component` vanilla inter-screen adjacency edges (from the
  upstream default connections), so the component layer is a real walk graph,
  not just a membership index.
- **portal edges**: for each teleporter hosted on a FLUTE-CANDIDATE
  component (the class this edge type exists for — notably the Desert/Mire
  teleporter ledge, whose only vanilla access is flute), a
  `component → opposite-world zone` edge. Restricting to candidate hosts
  is load-bearing: a generic every-teleporter rule would emit
  `host → target-zone` edges for enclosed targets (the Ice Palace island
  teleporter would grant the island's whole surrounding zone), and
  walk-reachable teleporters are already correctly modeled by the
  hand-written zone edges. The
  predicate is NOT extracted from upstream (its connection tuples carry no
  requirement field) — codegen derives it from the fork's existing
  hand-written teleporter zone edge for that portal, cross-referenced by
  name, **with any `CanFly` conjunct STRIPPED**. The zone edge conflates
  "get there" (flute) with "operate the portal" (glove tier) — e.g. the
  Mire edge is `CanLiftDarkRocks() AND CanFly(world)` — and under an
  active flute shuffle `CanFly` compiles false (D6), so verbatim reuse
  would make the Mire portal edge permanently false and deterministically
  kill Mire access on every attempt: the exact failure this class exists
  to prevent. The component-level edge carries only the operate-the-portal
  remainder (the "get there" leg is now real graph connectivity through
  the spot's component); codegen hard-fails if the stripped remainder is
  empty or if a teleporter zone edge's predicate shape isn't a
  conjunction it can safely elide `CanFly` from. Without this class,
  neutralizing baked flute predicates (D6) would sever Desert/Mire portal
  routes that a shuffled spot legitimately reaches.

**Why zones stay.** MIRROR-class predicates
(`HAS_ITEM(MagicMirror) AND OP_REGION_REACHABLE(<DW zone>)`) remain valid
without rewrites: the mirror is screen-index-based, warp shuffle never moves
screens, and zone reachability continues to mean "somewhere in the blob"
under vanilla walk connectivity. FLUTE-class predicates do NOT remain valid —
`CanFly(world)` bakes the eight vanilla destinations into region/location
predicates (e.g. the `LightWorld_NorthEast → LightWorld_DeathMountain_West`
edge is flute-gated because vanilla spot 0x03 lands on West DM), so under an
active flute shuffle they over-approximate reachability and the acceptance
gate, which evaluates the same graph, cannot catch the resulting phantom
routes. D6 therefore makes the hub the SOLE source of flute reachability
when the axis is on. The broader predicate re-audit (which specific
component does each MIRROR-ledge predicate need?) remains deferred to the
edge-transition change, where screens/connections actually move.

**Inertness.** When neither OW axis is active, reachability expansion skips
the component group entirely (same pattern as the terrain contiguous-suffix
fast-path: components are a contiguous id-suffix, and the walker bounds at
the group start when inactive). Axes-off output is byte-identical; the corpus
3-way diff is the proof, not a code-review claim.

**Quotient cross-check (the substrate's own oracle).** At codegen time,
collapse the component adjacency graph by zone membership and require the
result to be consistent with the hand-written zone edges: every derived
zone-adjacency must be explainable by an existing zone edge (or an explicit
allowlist entry naming why — e.g. adjacencies the coarse graph expresses via
predicates rather than edges), and every existing walk-class zone edge must be
witnessed by at least one component adjacency. Mismatch = codegen hard-fail.
This turns "did we port the overworld correctly?" into a build-time check
against six years of hand-validated zone logic.

**Alternative rejected — membership-only substrate** (no component↔component
edges): sufficient for flute/whirlpool alone, but it defers the entire hard
part (and the quotient check is impossible without a component walk graph),
leaving "foundation" as an empty label. The walk graph's risk is contained by
inertness + the quotient check, so we take it now.

**Alternative rejected — replace zones with components outright**: forces a
full predicate re-audit in the same change as the substrate, coupling the
riskiest logic work to the first shippable axis. Phased instead.

## D2 — Data source: committed generator over the upstream checkout

**Decision.** `assets/scripts/gen_ow_graph_tables.py` reads the
`OverworldShuffleDev` checkout (path via `--upstream`, defaulting to the
sibling `../ALttPDoorRandomizer`; the branch is read with `git show`, the
working tree is never required to have it checked out) and emits gitignored
`assets/rando/ow_graph.gen.yaml`. `rando_logic_gen.py` consumes it exactly
like `terrain.gen.yaml`. The generator asserts its own invariants:

- exactly 8 whirlpools matching `default_whirlpool_connections`, forming a
  perfect two-way pairing, 6 Light World + 2 Dark World;
- exactly 8 vanilla flute spots; every flute candidate names a component of
  its screen (binding is by NAME — upstream `OWTileRegions` is a
  name → screen map with no geometry, and none is needed). Vanilla spots
  MAY resolve to walk-in-less components — the Desert/Mire teleporter-ledge
  spot does, by design (upstream `flute_data` screen 0x30 serves the
  teleporter ledges, whose only vanilla access is flute) — such spots are
  flagged reachability-critical and their components must carry portal
  edges (D1);
- each flute candidate's ARRIVAL TABLEAU is extracted from `flute_data`'s
  own columns (upstream ships VRAM/BG-scroll/Link-xy/camera values for all
  40 candidates — D4 ports these rather than deriving them); the
  `IconY/IconX` columns are extracted only as a relative-placement
  cross-check (they are in upstream's map space, not the fork's — blips
  are derived, see D4);
- the upstream forced-spot entries (the Desert/Mire access guarantee) are
  extracted; the SECTOR partition is NOT extractable (upstream computes it
  at runtime by flooding a copied world) — the generator REPRODUCES it as
  a topological flood over the vanilla component adjacency, using the
  no-starting-Flippers branch (water components excluded from sector
  connectivity, the conservative default; a future starting-inventory
  feature revisits);
- every non-pseudo screen (< 0x80) is covered by ≥ 1 component; component
  names are unique; pseudo-screens 0x80/0x81 are excluded;
- adjacency symmetry except entries justified by the directed drop edges.

**License**: MIT (verified — `../ALttPDoorRandomizer/LICENSE`, LLCoolDave
2017 lineage). Same footing as the ALTTPR PHP port and the Enemizer-derived
tables.

**Fail-closed wiring** (the grass-rock lesson: an axis that adds NO pool
items does not fail closed for free). Absent/empty graph data must refuse
loudly at every layer: generation refuses a seed requesting either axis
(`settings_need_ow_graph` analog in `BuildItemPool`-adjacent preflight),
slot activation rejects a warp slot when the compiled graph is empty or its
identity digest mismatches, `Placement_SelfCheck` asserts the fail-closed
path on empty builds, and the snapshot cold-replay path enforces the same
guard. `setup_worktree.py` mirrors `ow_graph.gen.yaml` (known fresh-worktree
trap), and the codegen-wiring CI guard lists the new generated header(s).

## D3 — Region id & capacity discipline

Component regions load after everything else (including
`logic_parts/inverted/**`), so all pre-existing region ids are unchanged.
**The binding ceiling is `kReachabilityMaxRegions` (currently 256)** — it
sizes the reachability `region_bitset`, the inverted mirror pair set
(`g_inverted_pair_set`), and the hardcoded expansion count; regions at or
above it are silently never walked (and the bitset helpers do not bounds-
check). The static table is `kRandoRegionsCount = 40` today, but door
shuffle consumes additional dynamic region ids above it, and ~210 upstream
components plus the hub push the total past 256. **This change raises
`kReachabilityMaxRegions` to 512**, resizes every structure derived from it,
audits every hardcoded use (the expansion count, both bitset consumers, the
pair-set loops), and adds the missing enforcement so the cap can never
regress silently again:

1. codegen hard-fails unless (static regions + door dynamic budget +
   component count + hub) ≤ `kReachabilityMaxRegions`, with the budget
   figures printed;
2. a C static assert ties the generated `kRandoRegionsCount` (+ the door
   dynamic ceiling constant) to `kReachabilityMaxRegions`;
3. every entrance-edge-override-eligible region (dungeon entry regions, the
   `to_region` targets of `Rando_SetEntranceEdgeOverride`) keeps id < 64
   (`kEntranceEdgeOverrideMax` is indexed by `to_region` and silently no-ops
   at/above the cap);
4. a pinned name→id snapshot of ALL pre-existing regions from the generated
   table (drift = fail, with a one-line regen instruction in the error).

(Correction recorded: `kEntranceRegionOverrideMax` (512) is a
LOCATION-id-indexed override array and plays no role in region capacity —
an earlier draft misread it.)

## D4 — Runtime: point-of-use overrides, assets never mutated

**Flute.** The 8 destination rows live in read-only assets
(`kBirdTravel_*`). The override is a resolver consulted at the two consumer
sites, active only when `flute_shuffle` is on and a rando slot is active:

- `Rando_OwWarp_FluteSpot(k)` returns the per-seed spot k's ARRIVAL
  TABLEAU {screen, link x/y, map16 offset, BG scroll, camera} — PORTED
  from upstream `flute_data`'s per-candidate columns (upstream ships the
  full tableau for all 40 candidates) — plus its minimap blip x/y, which
  is DERIVED from the spot's screen/position by a documented transform
  into the fork's world-map space. The two datasets deliberately have
  different sources: the tableau is complex engine state upstream already
  authored (port it), while the blip is simple 2D map geometry whose
  upstream `IconY/IconX` columns are in a DIFFERENT coordinate space —
  measured against the fork's vanilla const blip tables they disagree on
  every vanilla spot's Y — so a raw icon port can never satisfy the
  engine-truth oracle. The icon columns are extracted only as a
  relative-placement cross-check.
- Hook sites: `FluteMenu_LoadTransport` (destination install) and the flute
  map's blip/cursor drawing + selection mapping in `messaging.c`
  (`Module0E_0A_FluteMenu` family), so the map UI, the selection order, and
  the landing all agree per seed. **The map blips do NOT come from the
  arrival assets**: `FluteMenu_HandleSelection` draws them from hardcoded
  `static const` minimap tables in `messaging.c` (`kBirdTravel_x_lo/x_hi/
  y_lo/y_hi`, `kBirdTravel_tab1`) via `WorldMap_AddSprite`. The resolver
  therefore also derives each shuffled spot's minimap blip coordinates
  (screen → world-map pixel geometry), and the blip/cursor path consumes
  the resolver, not the const tables, when the axis is active.

**Both datasets are oracle-checked**: `--rando-selftest` takes the 8
VANILLA spots' ported tableaux and requires byte-equality with the
corresponding `kBirdTravel_*` asset rows, AND requires the 8 vanilla
DERIVED blip coordinates to equal the `messaging.c` const blip tables
(`kBirdTravel_x_lo/x_hi/y_lo/y_hi`). Divergence fails the selftest — the
ported tableaux and the blip transform are each pinned to engine truth at
their 8 verifiable points, so any coordinate-space mismatch (scaling,
origin, byte order) surfaces at build time rather than in playtest.

**Whirlpool.** `FindPartnerWhirlpoolExit` finds the entered whirlpool's index
`j` in `kWhirlpoolAreas` (indexed by SOURCE screen) and teleports via
bird-row `j + 9` — and that row's tableau is hosted at j's VANILLA PARTNER's
area (the asset build writes row `n+9` under the partner area's Travel
entry, keying it by `whirlpool_src_area`). So row `m + 9` means "arrive at
`vanilla_partner(m)`". The override therefore remaps in landing-row space:
entering `j` under per-seed matching `μ` loads row
`vanilla_partner(μ(j)) + 9`, which lands at `μ(j)`. Because both `μ` and the
vanilla pairing are involutions, the round trip is symmetric
(entering `μ(j)` loads `vanilla_partner(j) + 9` → lands at `j`). A naive
`j → μ(j)` remap would land at `vanilla_partner(μ(j))` — the new partner's
OLD partner — and break symmetry; the selftest pins the correct mapping.
Landing tableaux are the vanilla asset rows of the destination whirlpool —
**no new arrival data exists in this feature at all**. The spoiler's pair
list is expressed in whirlpool-id (source-screen) space, not row space. v1 pairing is within-world (6 LW
whirlpools re-paired = 15 possible matchings; the single DW pair keeps
vanilla — honest no-op, stated in the spec; cross-world pairing joins the
crossed change where world-flag/bunny sync machinery lives).

**Vanilla untouched.** Both hooks are `(axis on && slot active)`-gated;
otherwise the vanilla lookup runs unchanged (the location-guard discipline).

## D5 — Settings & canonical encoding

`RandoSettings` gains `whirlpool_shuffle` (bool) and `flute_shuffle`
(enum off=0 / balanced=1 / random=2). Canonical byte [30]: bit 3 =
whirlpool, bits 4-5 = flute (append-only; [30] currently holds key_rings
bits 0-1 + skeleton_key bit 2). Defaults 0 → default blob, settings_hash,
and share strings are byte-stable; `kSettingsCanonicalLen` stays 31. The
`Settings_SelfCheck` "undefined [30] bits must be refused" probe currently
uses bit 3 — it moves to bit 6 (still undefined). `apply_derived_rules`
normalizes both axes to 0 under Inverted (v1 scope; Open/Standard/Retro are
in scope — implementation note: the normalization condition is `world_state
== Inverted`, NOT the entrance axes' broader "not Open/Standard" guard,
which would wrongly kill Retro warps). Deserialize refuses newly-undefined
bits exactly as key-rings did.

**Customizer composition (decided)**: the warp axes APPLY under customizer
mode — the layout is orthogonal to item pins, and the digest/retry pipeline
is unchanged (customizer seeds already run the standard acceptance path).
Upstream's customizer warp-PINNING (forcing specific spots/pairs) is NOT
ported at v1; a customizer manifest requesting it is out of scope and
documented as such.

**Standard world state (verified reasoning, documented not gated)**: the
hub edges carry no `RescuedZelda` term because every LW surface zone is
itself only reachable post-rescue in Standard (the `LinksHouse →
LightWorld_NorthEast` edge is rescue-gated); flute travel therefore cannot
fire earlier than vanilla logic allows. Implementation adds a comment at
the hub-edge data citing this transitivity, so a future graph change that
opens a pre-rescue LW zone knows to revisit it.

## D6 — Generation: derivation, retry, digest

Per generation attempt (same loop that already hosts entrance/door/chains
attempts in `rando_generate.c`):

- **Flute spots**: derived from `(seed_u64, attempt, kOwWarpSaltFlute)` via
  the shared xoshiro stream, porting the upstream selection semantics for
  BOTH modes rather than a naive uniform draw: the sector-based distribution
  (spots spread across the disconnected-region sectors so no sector
  monopolizes them), the FORCED spot entries (the Desert/Mire access
  guarantee — without it, layouts omitting the teleporter-ledge spot make
  Mire unreachable and the acceptance gate would reject at a high,
  unbounded rate), and — for `balanced` — the proximity ignore-set rule
  including its deliberate 1-in-32 escape (ported faithfully; the spec
  documents it as "spots avoid proximity" rather than a hard guarantee).
  Construct-valid beats reject-and-retry here; the acceptance gate remains
  the backstop, not the mechanism.
- **Whirlpools**: `(seed_u64, attempt, kOwWarpSaltWhirlpool)` uniform perfect
  matching μ over the 6 LW whirlpools, IDENTITY-EXTENDED over the 2 DW
  indices. Landing-row semantics (corrected by the implementation audit —
  the draft claimed identity "degrades to the vanilla row" via the uniform
  formula, which is arithmetically wrong): identity-μ entries load the
  ENTERED row, whose +9 tableau arrives at the VANILLA partner (unshuffled
  behavior); shuffled entries load `vanilla_partner(μ(j))`'s row, arriving
  at μ(j). The uniform formula applied to identity would instead load
  `vanilla_partner(j)`'s row and arrive back at j — a self-loop that killed
  both DW whirlpools; the branch is selfchecked per case.
- **Flute predicate neutralization (the H1 fix — hub becomes the SOLE flute
  source)**: when `flute_shuffle` is active, the compiled `CanFly(world)`
  macro evaluates FALSE (a settings-conditional term in the macro body, the
  same op family as existing settings-reading predicates), killing every
  baked vanilla-destination disjunct; the hub edges use the flute
  ACTIVATION body — precisely: a NEW macro whose body is `CanFly`'s FULL
  CURRENT body (`HAS_ITEM(OcarinaInactive) AND (OP_INSTANT_FLUTE() OR
  CanActivateOcarina(world))`), unchanged. `CanFly` itself has no
  "destination term" to strip — its destinations are emergent from WHICH
  EDGES reference it — so the split is: the new macro keeps the body
  verbatim (possession + activation), and the neutralization appends
  `AND NOT <the new flute-shuffle-active op>` to `CanFly` ONLY. The neutralization term is a NEW settings-reading predicate
  opcode (nothing existing expresses "flute shuffle active"): registered
  across all five op surfaces — `op_registry.yaml`, the `rando_logic.h` op
  enum, the `rando_logic.c` operand/skip/dispatch/eval surfaces, the
  `rando_logic_gen.py` parser — plus a selftest vector, and it reads the
  NORMALIZED `flute_shuffle` (post `apply_derived_rules`), so Inverted's
  intact `CanFly` uses are unaffected. Flute reachability under shuffle is
  then exactly {hub → the
  seed's 8 spot components} plus whatever walking/portal edges those
  components carry — no phantom vanilla routes survive for the acceptance
  gate to certify against. With the axis off, `CanFly` compiles unchanged
  and the hub is inert (D1), so vanilla logic is byte-identical.
- **Logic injection**: the virtual hub region `OW_FluteNet` and its
  `zone → hub` feeder edges are STATIC generated data in `logic_data.c`
  (seed-independent, zero overlay budget), and the feeders are gated on the
  flute ACTIVATION macro — possession + activation only, per the
  neutralization bullet below; gating them on `CanFly(world)` would
  self-contradict the neutralization and dead-end the hub. Only the 8
  `hub → spot-component` edges plus ≤ 12 directed Flippers-gated whirlpool
  edges are per-seed, injected through the EXISTING added-edge overlay
  (`Rando_AddEntranceEdge`) — ~20 overlay slots total.
- **Overlay cap widening (verified necessity)**: decoupled entrance mode
  alone adds up to `kEntranceCaveInteriorCount` (40) exit edges
  (`Entrance_ApplyDecoupledExitEdges`), composing with cross-mode edges —
  today's compositions already approach the 64-edge `kEntranceAddedEdgeMax`,
  whose overflow behavior is a SILENT drop (phantom unreachability). Warp
  edges on top would cross it. This change raises the cap to 128 (static
  arrays in `rando_logic.c`, trivial memory) AND adds a post-injection
  selfcheck asserting the combined consumer count stays below the cap —
  converting the silent cliff into a loud failure for every current and
  future consumer. Edge-transition shuffle's much larger volume still needs
  its own mechanism; explicitly NOT built here.
- **Acceptance**: when any OW axis is on, the seed passes through the SAME
  single acceptance gate every seed uses — `Accessibility_SeedAcceptable`
  at the seed's effective accessibility tier (verified: the attempt loops
  in `rando_generate.c` gate on it, not on an unconditional all-reachable
  check). This suffices for the flute-severance risk: even at
  `accessibility=none` the tier requires goal completability, so a layout
  that severs the goal (e.g. the flute-or-nothing Mire launch) is rejected
  and retried; at `items`/`locations` the stronger tier guarantees extend
  over the warp layout automatically. Hunt-goal + warp compositions at
  `accessibility=none` remain generatable (corpus convention preserved).
  Accepted attempt → `ow_attempt`; `ow_digest24` = 24-bit fold over the
  installed layout (spot (screen,pos) list in slot order + pair table).
- Attempt-0-only corpus caveat applies as everywhere: the retry loop is
  guarded by the determinism canary, not the corpus.

## D7 — Sidecar, activation, snapshot

The 80-byte slot header is full; fields ride the slot EXTENSION block with a
`format_version` bump (append-only, gated both directions like every prior
bump). Pinned layout: `kRandoSidecar_FileFormatVersion` 11 → 12 (the history
skips some numbers — implementation confirms 12 is unused before claiming
it); ext size 58 → 62 with `ow_attempt` (u8) at ext @58 and `ow_digest24`
(3 bytes LE) at ext @59-61, matching the exact-offset convention of every
prior extension append in `rando_save.h`. Activation regenerates the layout from (seed, canonical settings,
`ow_attempt`), recomputes the digest, and **hard-fails the slot on mismatch**
(door-gate class: a drifted layout can strand a certified placement — not the
entrance warn-only class). The snapshot cold-replay TLV carries the same two
fields (extend the existing settings-restore TLV payload without changing
TLV count, the terrain-precedent trick), so Ctrl+F1 cold replay reconstructs
identical warp state or refuses. Orphaned-sidecar scrub rules apply
unchanged.

## D8 — Spoiler, race, map/tracker

`spoiler.json` gains `ow_warps`: `{flute: [{slot, screen, pos, component}],
whirlpools: [[a,b],...]}` — hidden under race mode by the standard
`Rando_ActiveSlotHidesSpoiler` gating; `--reveal-spoiler` includes it. No new
item locations exist, so check-tracker/reachability UI counts are untouched.
The flute MAP UI is runtime-corrected by D4 (blips follow the seed). The
native-window map overlay (`rando_map.c`) must be audited for vanilla
flute-blip assumptions (it is on the never-audited list) — task, not spec.
Auto-tracker protocol: no schema change (no locations added); a future
`ow_warps` info tag is out of scope.

## D9 — Validation gates ("done" bar)

1. Generator self-asserts + quotient cross-check green.
2. WSL `make clean` build (`-Werror`) + MSVC Release x64 build, both green;
   `make clean` mandatory (header edits + codegen churn; no header deps).
3. `--rando-selftest` green including the new vectors: flute vanilla-row
   oracle (arrival tableaux AND minimap blip tables), added-edge cap assert
   (incl. the decoupled+cross+both-warp-axes composition), canonical [30]
   round-trip + undefined-bit refusal at the relocated probe,
   warp-derivation determinism vector, fail-closed empty-graph probe,
   CanFly-neutralization proof (a vanilla-flute-gated region is NOT
   flute-reachable under an active shuffle that moved its spot, and IS
   reachable via a seed whose spot lands there), region-capacity static
   assert.
4. Corpus: existing entries **byte-identical** via the 3-way diff ritual
   (fresh branch build vs fresh main build, `rm src/rando/logic_data.c`
   first, absolute `--binary`, CRLF restore); new entries added for
   flute-balanced/open, flute-random+whirlpool/open, standard+both,
   retro+flute, and one hunt-goal composition (`accessibility=none` per
   hunt convention). [As-built note: the corpus has no customizer lane, so
   the customizer-compose scenario is validated by the selftest suite +
   the shared-generator architecture (both paths route through
   Rando_GenerateSlotWithShapeFilter) rather than a corpus entry.] One
   `kGeneratorVersion` bump for the whole change [as-built: landed as 155
   after reconciling over the parallel validation-hardening union at 154].
5. CI guards: codegen-wiring list, `check_no_embedded_data`,
   `check_placer_determinism` (unaffected — budget stays 0),
   `check_corpus_version_sync`, `setup_worktree.py` mirror addition.
6. Playtest matrix (the only net for runtime): every flute slot lands where
   the map showed, on both modes; whirlpool entry both directions of a
   shuffled pair; Retro compose; race-mode hide + reveal; M4 cold replay
   with axes on; axes-off vanilla flute/whirlpool behavior unchanged.
7. Independent fresh-eyes review of the change (spec + code) before archive;
   spec deltas reconciled against as-built source at archive time.

## D10 — Trajectory (non-normative)

The follow-up changes this substrate is built for, in order: edge-transition
shuffle (parallel, keepsimilar, terrain-matched pools; per-edge destination
data at the `OverworldHandleTransitions` seam; the predicate re-audit that
retargets mirror-ledge predicates from zones to components; a beyond-64
per-seed edge mechanism), then crossed (world-flag/bunny/music sync — the
decoupled-entrance lessons), then mixed/grid (screen relocation; overlays,
`save_ow_event_info` indexing, mirror semantics — may reasonably never ship).
Nothing in this change presumes their design beyond the substrate itself.
