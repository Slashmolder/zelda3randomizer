# randomizer-bonk-sanity Specification

## Purpose
Bonk-sanity: the stage-stable placed overworld bonk sprites (bee hives and
apple trees) become one-time checks behind a default-off tiered axis, collected
by either wake method (Boots dash or Quake rumble), glint-marked, and guarded
by a fail-closed local registry.
## Requirements
### Requirement: Bonk objects as tiered check locations

The generator SHALL enumerate every placed overworld bonk-item sprite (the types prepped dormant by `SpritePrep_OverworldBonkItem` — bee hives and apple trees; the dormant bonk-prize ABSORBABLE family is out of scope) from an engine dump that sweeps all three sprite-phase tables (`sram_progress_indicator` stages — not the terrain profiles), registering objects identical across stages 1 AND 2 — the collectable window; the original all-three-stage rule measured zero survivors, and stage-0 presence instead gates the predicate (see the logic requirement) — into individually-checkable locations keyed by `(area, engine block)` (the `sprite_where_in_overworld` packing the runtime recovers from `sprite_N_word`; the sprite type is recorded but not part of runtime identity), activated by a new `bonk_shuffle` axis (`off`/`junk`/`all`, canonical byte [29] bits 5-6, default `off`): `junk` fills every bonk location from the junk pad only (no progression, no logic pressure), `all` makes them ordinary open locations, and `off` is byte-identical to the pre-feature build. The Lumberjack Tree's existing Standing check and dash-rock checks are outside the axis. A build whose local bonk registry is absent or empty SHALL refuse to generate a `bonk_shuffle != off` seed and refuse to activate a bonk-enabled slot (registry digest/count in the sidecar extension and snapshot registry TLV).

#### Scenario: Off is byte-identical
- **WHEN** any seed is generated with `bonk_shuffle=off`
- **THEN** placement output and settings_hash equal the pre-feature build

#### Scenario: Junk tier never carries progression
- **WHEN** a `bonk_shuffle=junk` seed is generated
- **THEN** every bonk location holds filler and the placer's junk-only proof passes

#### Scenario: Fail closed without the registry
- **WHEN** the local bonk registry is absent at codegen or empty at activation
- **THEN** generation of a bonk-enabled seed refuses loudly and a bonk-enabled slot refuses to activate

### Requirement: Wake-gated logic with world gates

Every bonk location's reachability SHALL require the ability to wake the sprite — `CanBonk()` = the Pegasus Boots OR (any sword AND the Quake medallion), matching the runtime's two accepted wake origins and upstream `canGetGoodBee`'s shape; swordless seeds need the Boots because field medallion casts require a sword — plus the object's world-side bunny gate (`TerrainWorldGateLW()`/`TerrainWorldGateDW()`) and the region containing its screen (screen→region map plus reviewed overrides; unreviewed split screens fail codegen). A row whose sprite is ABSENT from stage 0 SHALL additionally require `HAS_ITEM(RescuedZelda)` — its sprite cannot spawn during Standard's pre-escort phase, and without the gate a start-reachable region (Hyrule Castle Escape) let the placer put the Lamp on a tree that needs the Lamp-gated escort to exist (external-review P1 hardlock, pinned by a negative customizer selfcheck).

#### Scenario: No wake method, no bonk checks
- **WHEN** spheres are computed for a `bonk_shuffle=all` seed before the Boots and before a sword + Quake are obtainable
- **THEN** no bonk location is reachable

#### Scenario: Sword + Quake reaches a bonk check without Boots
- **WHEN** spheres are computed for a seed where a sword and the Quake medallion are reachable before the Boots
- **THEN** bonk locations (their other gates met) enter logic at the sword + Quake sphere

### Requirement: First-bonk grant, vanilla replay

The runtime SHALL grant on any RUMBLE wake at the sprite wake choke point — Link's Boots dash bonk or a Quake-medallion rumble, the only two rumble origins (owner playtest decision 2026-07-18: Quake is the other vanilla way to wake these sprites, so it collects too; the original dash-only rule is superseded): when a dormant bonk sprite is woken outdoors and its location is active and unchecked, the placed item is granted through the quiet-receive path, the location is marked checked, and the sprite despawns that frame (the vanilla bee-swarm/apple burst never spawns). One Quake MAY collect multiple on-screen unchecked bonk checks in a single cast; each grants independently through the quiet path. Placement logic matches the runtime: `CanBonk()` accepts the Boots or a sword + Quake (see the logic requirement), so the placer may rely on either wake method. A checked location's sprite SHALL wake with full vanilla behavior on every later rumble (bees and apples remain farmable), and an unresolved wake (any sprite not in the registry) SHALL stay vanilla in all modes.

#### Scenario: First bonk is the check
- **WHEN** the player dashes into an unchecked active bonk tree
- **THEN** the placed item is granted once, the location is marked, and no vanilla swarm/apples spawn that wake

#### Scenario: Quake collects like a bonk
- **WHEN** the player casts Quake on a screen with one or more unchecked active bonk trees
- **THEN** each woken unchecked check grants its placed item quietly and is marked checked; already-checked trees wake vanilla

#### Scenario: Checked trees farm vanilla
- **WHEN** the player re-bonks a checked bee tree on a later screen load
- **THEN** the vanilla 12-bee swarm spawns exactly as in vanilla play

#### Scenario: Reload preserves state
- **WHEN** the player saves, reloads, or cold-replays a snapshot after collecting bonk checks
- **THEN** collected locations replay vanilla and uncollected ones still grant

### Requirement: Presentation

Unchecked active bonk objects SHALL show the gold check glint: live DORMANT bonk sprites whose location resolves unchecked join the overworld glint mask at their sprite coordinates (the 16-slot sprite table is the natural cap; the terrain glints' nearest-N collector is not involved). Tracker/spoiler surfaces present bonk locations as ordinary rows (no visibility gating — the family is small), and the native settings window exposes the tier selector with a durable-facts tooltip.

#### Scenario: Glint marks unchecked bonk objects
- **WHEN** the player stands on a screen with unchecked active bonk checks
- **THEN** the nearest unchecked objects glint and checked ones do not
