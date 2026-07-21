# randomizer-ow-shuffle — delta for add-rando-ow-warp-shuffle

## ADDED Requirements

### Requirement: Warp-shuffle axes are opt-in, world-state-scoped, and fail closed

The generator SHALL expose two independent settings axes — `whirlpool_shuffle`
(boolean, default off) and `flute_shuffle` (`off`/`balanced`/`random`, default
`off`) — available under the Open, Standard, and Retro world states and
normalized to off under Inverted by `apply_derived_rules` (v1 scope; Inverted
flute semantics differ and its composition is deferred). When either axis is
requested and the compiled overworld graph data is absent or empty
(missing/failed `ow_graph.gen.yaml` codegen), generation SHALL refuse the seed
loudly rather than silently producing a vanilla-warp seed, and slot activation
SHALL likewise refuse a warp slot against an empty compiled graph — the
explicit fail-closed wiring an axis that adds no pool items requires.

#### Scenario: Defaults are inert and hash-stable
- **WHEN** a seed is generated with both axes at their defaults
- **THEN** placement, spoiler, settings_hash, and share string are
  byte-identical to a pre-change build at the same inputs, and the runtime
  flute menu and whirlpools behave exactly as vanilla

#### Scenario: Inverted normalizes the axes off
- **WHEN** settings request `flute_shuffle=random` under `mode.state=inverted`
- **THEN** canonicalization stores both axes as 0, the settings hash reflects
  the normalized values, and no warp layout is generated or installed

#### Scenario: Missing graph data refuses generation
- **WHEN** `--generate-seed` runs with `whirlpool_shuffle=true` on a build
  whose overworld graph tables are empty
- **THEN** generation fails with an explicit error naming the missing graph
  data, and no placement, spoiler, or slot is emitted

#### Scenario: Customizer seeds compose
- **WHEN** a customizer-mode seed is generated with either warp axis on
- **THEN** the warp layout applies exactly as in a non-customizer seed
  (item pins are orthogonal; upstream-style warp PINNING is out of scope
  and not honored), and the digest/acceptance pipeline is unchanged

### Requirement: Flute-spot shuffle relocates the eight travel destinations coherently

With `flute_shuffle` active, the generator SHALL select eight distinct flute
destinations from the Light World candidate set (candidate positions ported
from the upstream `flute_data`; pseudo-screens 0x80+ excluded) using the
ported upstream selection semantics for both modes: sector-based
distribution across disconnected region groups, the upstream FORCED spot
entries (the Desert/Mire access guarantee, whose teleporter-ledge spot is
the only vanilla route to those areas), and — for `balanced` — the
proximity ignore-set rule (flooded through same-screen/adjacent components
and one-way ledges, avoiding overlap with previously used spots, including
the upstream rule's deliberate rare escape) on top of `random`'s
unconstrained draw within those invariants.
At runtime the flute map UI, the selection cycling order, and the installed
landing SHALL all reflect the same per-seed spot for every slot: the blip
drawn for slot k, the destination previewed by cycling to slot k, and the
tableau installed on confirming slot k are one spot. The vanilla flute paths
SHALL be untouched whenever the axis is off or no rando slot is active.

#### Scenario: Map, cycle order, and landing agree
- **WHEN** the player opens the flute map on a `flute_shuffle=random` seed and
  confirms the third blip
- **THEN** Link lands at the third blip's screen and position, with scroll,
  camera, and screen properties valid for that screen (no out-of-bounds
  camera, no stale scroll clamp)

#### Scenario: Vanilla spots when off
- **WHEN** any seed is played with `flute_shuffle=off`
- **THEN** all eight vanilla destinations behave byte-identically to an
  unmodified build, including under RAM-compare side-by-side verification

### Requirement: Flute arrival tableaux and map blips are pinned to engine truth

The runtime resolver SHALL serve a shuffled spot's arrival tableau (screen,
Link coordinates, map16 offset, BG scroll, camera) from the PORTED upstream
per-candidate rows, and its world-map blip coordinates as follows: spots on
the eight VANILLA flute screens reuse the engine's hand-tuned const blip
positions verbatim (measurement showed those are curated placements, not
formula outputs), and spots on other screens derive a world-space
link-centered position (cosmetic; playtest-tunable). The flute map draws
blips from hardcoded const tables in `messaging.c`, not from the arrival
assets, and upstream's icon columns are in a different coordinate space —
neither is a valid blip source for non-vanilla screens. Both SHALL be validated by `--rando-selftest` oracles at the eight
vanilla spots: ported tableaux MUST byte-match the corresponding vanilla
`kBirdTravel_*` asset rows, and derived blips MUST match the vanilla const
blip tables. Either oracle failing SHALL be treated as a data/transform
bug, never worked around by editing the expected rows.

#### Scenario: Oracles pin both datasets
- **WHEN** `--rando-selftest` runs on any build of this feature
- **THEN** the flute-tableau and flute-blip oracle vectors pass, proving the
  ported rows and the blip transform reproduce the engine's own arrival and
  map data for all eight vanilla spots

### Requirement: Flute logic routes solely through the shuffled spots

When `flute_shuffle` is active, compiled logic SHALL evaluate the
destination-semantics flute macro (`CanFly(world)`) as false — every baked
predicate disjunct encoding a vanilla flute destination (e.g. the
flute-gated West Death Mountain zone edge) is thereby neutralized — and
flute reachability SHALL flow exclusively through the flute hub's edges to
the seed's eight spot components. The hub feeder predicate is a new macro
whose body is `CanFly`'s full current body verbatim (flute possession +
activation — `CanFly` contains no destination term; destinations are
emergent from which edges reference it), and the neutralization consists
solely of appending the flute-shuffle-active conjunct to `CanFly` itself. With the axis off, the macro SHALL compile unchanged and the
hub SHALL be inert, leaving vanilla logic byte-identical. This is the
mechanism that prevents the acceptance gate from certifying placements
against vanilla flute routes that the shuffled layout no longer provides.

#### Scenario: A moved spot's vanilla route is dead in logic
- **WHEN** a `flute_shuffle` seed relocates the spot that vanilla logic used
  to flute-gate a region (no other route applies) and no shuffled spot lands
  in that region
- **THEN** the region is unreachable in compiled logic, and the acceptance
  gate refuses any placement requiring it — never certifying a phantom
  vanilla flute route

#### Scenario: A shuffled spot grants its landing region
- **WHEN** a `flute_shuffle` seed places a spot on a screen of a region
  vanilla flute never served
- **THEN** with the flute obtained and activated, that region is reachable
  in compiled logic via the hub edge to the spot's component

### Requirement: Whirlpool shuffle re-pairs within-world using vanilla landing data

With `whirlpool_shuffle` active, the generator SHALL derive a uniform perfect
re-pairing μ of the six Light World whirlpools from the seed, with μ
identity-extended over the two Dark World indices so the uniform runtime
landing-row formula degrades to vanilla for them; the single Dark World
pair thereby keeps its vanilla pairing at v1 (within-world constraint —
cross-world pairing is deferred to the crossed change; this no-op is
documented player-facing). Whirlpool travel SHALL remain two-way (entering
either end of a pair emerges at the other), and every landing SHALL use the
destination whirlpool's vanilla arrival row — the feature introduces no new
arrival data. Because the engine's arrival row for index m encodes "arrive
at m's VANILLA partner", the runtime remap SHALL operate in landing-row
space (entering j under matching μ loads the row of `vanilla_partner(μ(j))`)
so the player emerges at μ(j); a self-check SHALL pin this mapping, and the
spoiler pair list SHALL use whirlpool-id (source-screen) space. Logic SHALL model each active pair as bidirectional
component-level edges gated on Flippers.

#### Scenario: Shuffled pair is symmetric
- **WHEN** whirlpool A is re-paired with whirlpool B and the player enters A
- **THEN** they emerge at B's vanilla landing, and entering B emerges at A's
  vanilla landing

#### Scenario: Dark World pair is vanilla at v1
- **WHEN** any `whirlpool_shuffle=true` seed is generated
- **THEN** the two Dark World whirlpools remain vanilla partners, and the
  spoiler's pair list shows them as such

### Requirement: Warp layouts are deterministic, retried for reachability, and drift-gated

The warp layout SHALL derive purely from (seed, accepted attempt, per-axis
salt) on the generator's seeded stream — no wall-clock, no platform variance —
and regenerate identically at slot activation and snapshot cold replay. When
any warp axis is on, seed acceptance SHALL run through the standard
`Accessibility_SeedAcceptable` gate at the seed's effective accessibility
tier, evaluated against the installed warp layout — flute-spot relocation
can sever no-glitches routes (e.g. the flute-only Misery Mire approach), and
the tier handles it: `none` still enforces goal completability, while
`items`/`locations` extend their stronger guarantees over the layout, so
hunt-goal compositions at `accessibility=none` remain generatable. The
accepted attempt index and a 24-bit digest of the installed layout SHALL
persist with the slot, and activation SHALL hard-fail on digest mismatch (a
drifted layout can strand a certified placement — door-gate class, not the
entrance warn-only class).

#### Scenario: Same inputs, same layout
- **WHEN** the same share string is generated twice on different platforms
- **THEN** spot assignments, whirlpool pairs, spoiler `ow_warps`, and
  placement digest are identical

#### Scenario: Generator drift refuses activation
- **WHEN** a slot generated by an older binary is activated by a build whose
  warp derivation produces a different layout for the stored
  (seed, attempt)
- **THEN** activation refuses the slot with the digest-mismatch error instead
  of silently installing a layout the placement was not certified against

### Requirement: Spoiler and race handling cover warp layouts

The spoiler SHALL include an `ow_warps` section (per-slot flute spots with
screen/position/component; whirlpool pair list) exactly when at least one
warp axis is active AND spoilers are permitted: with both axes off the
section is entirely absent (preserving axes-off spoiler byte-identity), and
under race mode it is hidden by the active-slot spoiler gate while
`--reveal-spoiler` includes it.

#### Scenario: Race seed hides the layout
- **WHEN** a race-mode seed with both axes on is generated and its slot is
  active
- **THEN** no spoiler file is written and no UI surface reveals spot or pair
  assignments, while `--reveal-spoiler` on the share string reproduces the
  full `ow_warps` section
