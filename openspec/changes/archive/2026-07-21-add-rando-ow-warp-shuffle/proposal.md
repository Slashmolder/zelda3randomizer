# Proposal: OW screen-graph foundation + warp shuffle (flute spots & whirlpools)

## Why

The overworld layout randomizer was selected as the fork's next frontier
(owner decision, 2026-07-19). Its enabling problem is that the logic graph
models the entire overworld as ~11 coarse zones (5 Light World + 6 Dark World
surface regions out of a 40-region generated table): connectivity *within* a zone
is implicit, Magic Mirror / flute reachability is baked into hand-written
location predicates, and whirlpools are not modeled at all. No shuffle that
rewires overworld connections can be expressed against that graph.

This change lands the foundation — a generated **screen-component graph** for
the overworld — and ships its first two player-facing consumers: **flute-spot
shuffle** and **whirlpool shuffle**. These two axes were chosen deliberately:
they exercise the full new pipeline (generated graph data → settings axis →
per-seed layout derivation → reachability retry → runtime override → sidecar
regen → spoiler/race) while carrying **zero screen-transition risk** — both
warps already resolve through the engine's atomic teleport primitive
(`Overworld_LoadBirdTravelPos`), so none of the edge-transition machinery
(scroll tableaux, camera bounds, map16 streaming) is touched. Edge-transition
shuffle (the "layout" headline) builds on this substrate in a follow-up change.

The upstream model is fully grounded: the community overworld randomizer
(codemann8's work, merged into aerinon's ALttPDoorRandomizer) is readable at
the local sibling checkout on branch `origin/OverworldShuffleDev` (MIT license,
verified at `../ALttPDoorRandomizer/LICENSE`). Its `OWEdges.py` +
`OverworldShuffle.py` + `source/overworld/FluteShuffle.py` define the edge
inventory, sub-screen region map (`OWTileRegions`), one-way ledges, whirlpool
pairs, and flute-spot rules this change ports **by committed generator, never
by hand transcription** (the `gen_enemy_shuffle_tables.py` discipline).

## What Changes

- **New generator** `assets/scripts/gen_ow_graph_tables.py`: parses the
  `OverworldShuffleDev` checkout and emits gitignored
  `assets/rando/ow_graph.gen.yaml` — screens, NAME-KEYED sub-screen walkable
  components (upstream `OWTileRegions` carries no geometry; none is needed),
  component↔zone membership, directed one-way drop edges
  (source-component → landing-component), vanilla inter-screen component
  adjacency, whirlpool pair table, and flute-spot candidates INCLUDING their
  upstream arrival tableaux and minimap icon coordinates (`flute_data`
  carries both for all 40 candidates). The generator asserts its own
  invariants (8 whirlpools, 8 vanilla flute spots, every non-pseudo screen
  covered, component names unique, adjacency symmetric where two-way).
- **Logic substrate** (`rando_logic_gen.py` + generated `logic_data.c`):
  overworld screen-component regions appended as a new last-loaded region
  group; static membership/drop/adjacency edges plus PORTAL edges for the
  LW↔DW overworld teleporters hosted on components (flute-critical for
  Desert/Mire); a codegen **quotient cross-check** (component-graph
  adjacency collapsed to zones must match the hand-written zone edge set,
  with a carrier-validated allowlist); a capacity raise
  (`kReachabilityMaxRegions` 256 → 512, the true region ceiling, with
  resized structures, a codegen budget check, and a C static assert) plus
  id-discipline asserts (entrance-edge-override-eligible region ids < 64,
  all pre-existing region ids pinned). Component regions are **inert when
  no OW axis is active** — reachability skips them, preserving current
  behavior byte-for-byte. When `flute_shuffle` IS active, the baked
  `CanFly(world)` vanilla-destination predicates compile to false and
  flute reachability routes solely through the per-seed spot set — the
  logic-data analog of the fork's vanilla-state-proxy discipline.
- **Two settings axes** in canonical byte [30] (no length change, defaults 0 =
  hash-stable): `whirlpool_shuffle` (bit 3, bool) and `flute_shuffle` (bits
  4-5: off / balanced / random). Both normalized off under Inverted at v1.
- **Generation**: per-attempt warp layout derived from (seed, attempt, salt);
  tier-scoped acceptance retry (the standard `Accessibility_SeedAcceptable`
  gate evaluated against the installed layout); accepted
  `ow_attempt` + a 24-bit layout digest persisted in the sidecar slot
  extension block (format_version bump); activation regenerates the layout and
  hard-fails on digest mismatch (door-shuffle gate class).
- **Runtime**: point-of-use overrides only — the flute menu destination
  serves ported per-candidate arrival tableaux and the map blips a derived
  map-space position, through one shared resolver whose outputs are
  oracle-checked for the 8 vanilla spots (tableaux byte-equal to the
  `kBirdTravel_*` asset rows; blips equal to the `messaging.c` const blip
  tables); whirlpool partner lookup remaps the pair index in landing-row
  space and **reuses the vanilla landing rows** (no new arrival data). Vanilla paths untouched when axes are off.
- **Spoiler** gains an `ow_warps` section (race-hidden as usual); native
  settings window gains the two controls; docs section added.
- **No new item locations.** The placement pool is unchanged; digests move
  only via reachability when an axis is on. One `kGeneratorVersion` bump.

## Capabilities

### New Capabilities

- `randomizer-ow-shuffle`: the overworld-connection shuffle family — this
  change specifies the warp axes (flute spots, whirlpools); edge-transition
  shuffle will extend this capability in a follow-up change.

### Modified Capabilities

- `randomizer-logic`: ADDED requirements for the screen-component substrate
  (generated regions, quotient cross-check, inert-when-off, capacity asserts)
  and per-seed warp edge injection within the existing added-edge overlay.
- `randomizer-core`: ADDED requirement for the canonical byte [30] bits 3-5
  assignment, hash/share stability, and the Settings_SelfCheck undefined-bit
  probe relocation.
- `randomizer-save`: ADDED requirement for the slot-extension warp fields
  (attempt + digest24), format_version gating both directions, activation
  regeneration with hard-fail, and snapshot cold-replay parity.
- `randomizer-ui`: ADDED requirement for the native-window controls and the
  spoiler-tab / tracker neutrality of the new axes.

## Impact

- **New code**: `assets/scripts/gen_ow_graph_tables.py`; `src/rando/
  shuffle_ow_warp.{c,h}` (derivation, resolver, digest, runtime hooks'
  backend). Modified: `rando_logic_gen.py`, `rando_settings.{c,h}`,
  `rando_generate.c`, `rando.c` (activation), `rando_save.{c,h}`,
  `rando_spoiler.c`, `messaging.c` (flute menu hooks), `overworld.c`
  (whirlpool partner hook), `rando_window.cpp`, docs. Registered in both
  build systems (vcxproj + Makefile + Switch makefile) and the codegen-wiring
  CI guard list.
- **Corpus / determinism**: axes-off output must be **byte-identical** proven
  by the corpus 3-way diff ritual (fresh build of this branch vs fresh main,
  full regen); new corpus entries cover flute balanced/random × whirlpool ×
  Open/Standard/Retro and a hunt-goal composition. `--generate-seed` keeps
  `budget_seconds = 0` (placer-determinism guard unaffected).
- **Risk profile**: LOW on the engine side (teleport primitive only, no
  transition tableaux); MODERATE on the data side (component graph quality) —
  contained by the quotient cross-check, generator self-asserts, inertness
  when off, and the tier-scoped acceptance retry when on; the residual net is the
  playtest matrix (flute menu/map coherence per spot, whirlpool pairs both
  directions, Retro compose, race reveal, M4 cold replay).
- **Effort shape**: the long pole is the generator + substrate correctness
  work (comparable to the terrain feature's Phase 1-2 measurement/codegen
  investment), not the runtime hooks; second-longest is the validation ritual
  (corpus regen ×2 environments + playtest matrix).
- **Out of scope** (deferred to follow-up changes): edge-transition shuffle,
  crossed/mixed/grid modes, Inverted composition, flute-spot placement on
  pseudo-screens (0x80/0x81), cross-world whirlpool pairing. At v1 the two
  Dark World whirlpools keep their vanilla pairing (re-pairing is within-world
  and DW has exactly one pair — stated honestly in the spec).
