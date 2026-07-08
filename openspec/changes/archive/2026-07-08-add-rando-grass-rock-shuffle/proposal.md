# Proposal: Grass & Rock Drop Shuffle (per-object overworld terrain checks)

## Why

The randomizer's check families now cover chests, NPCs/events, dungeon pots (pot
sanity), and enemy drops (enemy-drop checks) — but overworld terrain objects
(bushes, cuttable thick grass, and liftable rocks) still always yield their
vanilla hidden secrets. The owner wants two new shuffle tiers — **grass** and
**rocks** — that randomize the drops from cutting/lifting grass and from lifting
both rock strength tiers (lighter rocks requiring L1 strength / Power Glove,
darker rocks requiring L2 strength / Titan's Mitt), following the established
pot-sanity pattern.

Owner decisions locked at kickoff (2026-07-06):

- **Grass scope = bushes/shrubs + cuttable thick grass**, i.e. the map16 objects
  that participate in the vanilla overworld secrets system (`0x036`/`0x72a`
  bushes, `0x37e` thick grass). Low walk-through grass tufts stay cosmetic —
  they have no vanilla drop path (verified: `Overworld_ToolAndTileInteraction`
  only reveals secrets for bush/thick-grass/shovel attrs).
- **Per-object checks** — every bush / thick-grass tile / light rock / heavy
  rock is its own enumerated check location, exactly like the ~800 pot-sanity
  pots (including vanilla-empty objects).
- **Tiered settings** — each axis gets tiers (`off` / `junk` / `all`) so players
  choose per seed whether progression can hide under terrain.

## What Changes

- **Two new settings axes**: `grass_shuffle` and `rock_shuffle`, each
  `off / junk / all`, packed into a NEW appended canonical byte `[29]`
  (bits 0-1 / 2-3; `kSettingsCanonicalLen` 29→30 — byte [28]'s free bits
  were taken by the enemy/NPC souls axes). Defaults `off` keep default
  PLACEMENT byte-identical; the canonical-length append changes
  settings_hash once, under this change's `kGeneratorVersion` bump (same as
  the [28] append before it).
- **New generated location family** enumerated from the engine (a
  `--dump-terrain-table` CLI mode mirroring `--dump-pot-table`): every liftable
  / cuttable terrain object on overworld screens `0x00-0x7F` becomes a check
  location keyed by `(overworld_screen_index, map16 pos)` — the same key the
  vanilla secrets table (`kOverworldSecrets`) already uses. Special screens
  `>= 0x80` are out of scope (the engine's `Overworld_RevealSecret` bails on
  them; they cannot reveal secrets).
- **Structural secrets stay vanilla**: objects hiding terrain reveals (secret
  data byte `>= 0x80`: holes, water, big-rock stairs, staircases, the Ice
  Palace portal rock) are excluded from the registry so warps/stairs are never
  suppressed.
- **Runtime grant hook** at the four consuming call sites that feed
  `Overworld_RevealSecret` (lift, big-pile smash, sword-cut, bomb — the
  dash path reuses the lift helpers; the hook deliberately does NOT sit
  inside `Overworld_RevealSecret`, which the bomb path also calls
  speculatively for tiles it does not consume), following the
  `Rando_PotBreakHook` pattern: active + unchecked location → grant the
  placed item via the pot-style quiet-receive, mark checked, suppress the
  vanilla secret spawn via the engine's `0xFF` no-spawn sentinel; checked or
  inactive → vanilla behavior (bush fairies etc. remain farmable after the
  check).
- **Logic integration**: locations bound to overworld regions via the existing
  `OVERWORLD_REGIONS` screen→region table (gen_enemy_check_tables.py) plus a
  reviewed overrides YAML for split screens (ledges/fences/river banks);
  predicates from existing macros — `CanLiftRocks()` for light rocks,
  `CanLiftDarkRocks()` for heavy rocks, a cutter predicate for thick grass,
  and the Moon-Pearl / world-state gates mirroring the pot-sanity dark-world
  rule.
- **Placer integration**: `all` tier locations are ordinary open locations
  (progression can land there, junk-padded); `junk` tier locations are a new
  junk-only fill class (excluded from progression assumed fill, filled from
  the junk pad). The pot-sanity "skip-triple" discipline (open-location loop,
  junk-pad target, `Placement_SelfCheck` expected count) is extended to the
  new location types.
- **Persistence & activation guard**: checked bits reuse the existing
  checked-location bitmap; the sidecar slot ext gains a terrain registry
  digest/count pair mirroring `pot_registry_digest/count`, so an
  empty-registry binary refuses to activate a terrain-shuffle slot (the
  chest_lookup fail-open class).
- **UI/reporting**: native settings window tier selectors, check-tracker /
  reach-panel / map-tracker treatment (gated rows + summary counts, like
  "Show pots"), spoiler emission (empty objects omitted), auto-tracker
  metadata, hints and customizer treatment mirroring pots.
- **`kGeneratorVersion` bump + corpus entries** for the new axes and their
  compositions (retro, inverted, door shuffle, enemy drops, pot shuffle).

## Capabilities

### New Capabilities

- `randomizer-grass-rock-sanity`: the grass/rock check family — enumeration &
  registry contract, tier semantics, logic predicates and region binding,
  runtime grant/suppress behavior, activation guard, and composition rules
  with other shuffles.

### Modified Capabilities

- `randomizer-core`: "Settings canonical serialization order (normative)" —
  a new appended canonical byte `[29]` carries the two 2-bit terrain axes
  (`kSettingsCanonicalLen` 29→30); the restated table also reconciles the
  as-built byte [28] souls fields and the [25] `dungeon_chains` bit.
- `randomizer-save`: "Sidecar slot contents" — slot ext block gains
  `terrain_registry_digest` / `terrain_registry_count` guard fields (ext
  version bump), mirroring the pot registry guard.
- `randomizer-ui`: new requirement for the grass/rock tier selectors and
  tracker/spoiler presentation of terrain checks.

## Impact

- **Engine**: `src/overworld.c` (one hook call in `Overworld_RevealSecret`),
  headless area-load path for the dump tool.
- **Rando module**: `src/rando/rando_settings.{c,h}` (axes + canonical),
  `src/rando/rando_placement.c` (tier filter, junk-only class, skip-triple),
  `src/rando/rando.c` (hook, grant, activation guard, registry digest),
  `src/rando/rando_save.{c,h}` (ext fields), spoiler/hints/customizer/
  auto-tracker/tracker consumers, `src/rando/rando_window/` (UI).
- **Codegen & assets**: new `assets/scripts/gen_terrain_tables.py` +
  `assets/rando/terrain.gen.yaml` (gitignored, mirrored by
  `setup_worktree.py`) + reviewed `assets/rando/terrain_logic_overrides.yaml`
  (committed); `assets/rando_logic_gen.py` injection; new location types in
  the logic schema.
- **Capacity**: location registry grows by the enumerated object count
  (measured in Phase 1; estimated low thousands — 890 vanilla secrets is the
  floor). May require raising `kRandoLocationCapacity` (4096) with the known
  Makefile no-header-deps ABI hazard mitigations (`make clean`, cross-TU size
  selfcheck).
- **Validation**: `kGeneratorVersion` bump, corpus regen + new manifest
  entries, `--rando-selftest` additions, CI guards; end-to-end playtest is
  the only net for the runtime grant path (corpus/selftest are
  generation-only).
