# Proposal: Bonk-sanity (dash-bonk tree objects as randomizer checks)

## Why

Dashing into trees is the last classic overworld interaction with no
randomizer meaning: the placed bonk objects (dormant bee hives and
apple trees) always yield their vanilla swarm/apples. ALTTPR carries a
bonk-prize table (`Rom::setOverworldBonkPrizes`) but never calls it — dead
code — so this is fork-original scope, and the fork already owns the two
patterns that make it cheap: the pot/terrain check-family discipline
(registry → tiers → quiet grant → checked-replay → glint) and a
sprite-layer bonk-check precedent (`Sprite_BonkKey` already converts
dungeon bonk drops into dispatched checks).

**Critical grounding fact (research 2026-07-17):** bonk trees are a SPRITE
mechanism, fully disjoint from the map16 terrain pipeline — placed sprite
ids `0x79` (dormant bee hive) / `0xAC` (apple tree) prepped dormant by
`SpritePrep_OverworldBonkItem` and woken by `Entity_ApplyRumbleToSprites`
(`sprite_E → 0`) when Link's bonk box overlaps. They never touch
`Overworld_RevealSecret`, `kOverworldSecrets`, or tile attributes, so this
is a NEW (bounded) mechanism modeled on `Sprite_BonkKey`, not a third
terrain axis — though it reuses the terrain feature's settings shape,
world-gate macros, registry/guard discipline, and glint machinery.

## What Changes

- **New axis `bonk_shuffle`** (`off`/`junk`/`all`, `TerrainShuffle` enum
  reuse) in canonical byte [29] **bits 5-6** (free after shopsanity's
  bit 4; no length change, default hash untouched).
- **Engine-ground-truth enumeration**: a new `--dump-bonk-table` headless
  mode walks every overworld screen's sprite table across **all three
  sprite-phase tables** (`sram_progress_indicator` stages — the variance
  axis for sprite data; the terrain profiles never visit the middle
  stage) and emits every placed bonk-item sprite (`0x79`/`0xAC` — the
  types prepped by `SpritePrep_OverworldBonkItem`) keyed by
  `(screen, sprite x, y, type)`. A committed generator
  (`gen_bonk_tables.py`) registers only objects identical across all
  three stages (missable-check rule) and emits gitignored
  `assets/rando/bonk.gen.yaml` (ids from 7168; capacity 8192 holds).
  The population's true count is unknown until the dump runs (a plain
  [1,200] bounds tripwire; ALTTPR's dead 62-address table describes the
  DIFFERENT absorbable bonk-prize family and is not compared against).
- **Locations**: every enumerated bonk object becomes a check under `all`
  (progression allowed); `junk` = filler-only class (terrain junk-pad
  discipline). New `LOCTYPE_Bonk`. The Lumberjack Tree (existing Standing
  check id 170, entrance-gating) and dash ROCKS (Pegasus Rocks id 165,
  terrain-excluded attr class) are OUT of the axis.
- **Logic**: new `CanBonk()` macro (`HAS_ITEM(Boots)`), region binding via
  the screen→region map + reviewed overrides, world/bunny gates via the
  existing `TerrainWorldGateLW()/DW()`.
- **Runtime grant hook** at the single activation choke point
  (`Entity_ApplyRumbleToSprites`, where `sprite_E` clears), firing ONLY
  for dash-origin wakes (Quake-medallion rumbles stay vanilla; overlap
  test must have run; not indoors): active + unchecked → quiet-grant the
  placed item, mark checked, despawn the sprite that frame (suppresses
  the 12-bee swarm / apple burst); checked or axis-off → vanilla wake
  (swarm/apples stay farmable — bonk sprites respawn dormant every screen
  load, so checked-replay is native).
- **Fail-closed registry guard**, terrain-pattern, all three layers:
  generation refuses a bonk-active seed with an absent/empty registry,
  sidecar ext gains `bonk_registry_digest/count` (ext version bump),
  snapshot registry TLV extended — plus the slot-writers'
  `Rando_StampSlotRegistries` single helper (the grass HIGH-1 lesson).
- **Glint**: sprite-coordinate variant of the capped nearest-N collector
  feeding the same draw machinery (bonk registry carries sprite coords;
  the map16-pos collector cannot be reused as-is).
- **UI/reporting**: EnumCombo in the Shuffles block; the family is small
  (dump-measured; bounds [1,200]) so rows are ordinary
  tracker rows (no gating toggle needed); spoiler rows as ordinary
  placements; docs.
- **Validation**: corpus rows (junk/all × a composition), selfcheck
  (registry round-trip, count agreement, fail-closed on empty), under the
  branch's unreleased kGeneratorVersion 146.

## Capabilities

### New Capabilities

- `randomizer-bonk-sanity`: enumeration/registry contract, tier semantics,
  logic predicates, sprite-layer grant/suppress behavior, checked-replay,
  fail-closed activation, composition rules.

### Modified Capabilities

- (none as deltas; byte [29] bits 5-6 noted at archive time — ADDED-only
  authoring, same rationale as add-rando-random-crystals. Archive order:
  shopsanity → random-crystals → this.)

## Impact

- **Engine**: `src/sprite.c` (`Entity_ApplyRumbleToSprites` hook), a
  headless dump entry (`src/overworld.c` or `main.c` dispatch),
  `src/sprite_main.c` untouched at the AI level (suppression happens at
  wake).
- **Rando**: settings axis plumbing; placement junk-class + skip-triple +
  fail-closed; `rando.c` grant/checked/glint-collect + activation guard +
  registry stamp; `rando_save` ext fields; spoiler; tracker; window UI.
- **Codegen/assets**: `gen_bonk_tables.py` (committed),
  `bonk.gen.yaml` (gitignored, mirrored by setup_worktree),
  `rando_logic_gen.py` injection + duplicate-id hard-fail already global;
  macros.yaml `CanBonk`.
- **Risks carried from research + plan review**: suppression must kill
  the wake burst atomically (grant only on dash-origin wakes — the Quake
  medallion shares the rumble choke point and must stay vanilla);
  sprite-phase variance handled by the three-stage identity rule; the
  absorbable bonk-prize family (bonk fairies/hearts) is excluded from v1
  and surfaced as the owner's v2 scope call in design.md.
