# randomizer-grass-rock-sanity Specification

## Purpose
TBD - created by archiving change add-rando-grass-rock-shuffle. Update Purpose after archive.
## Requirements
### Requirement: Terrain check registry — engine-dump enumeration keyed like the vanilla secrets table

The randomizer SHALL enumerate every in-scope overworld terrain object into a generated location registry, keyed `(overworld_screen_index, map16 pos)` — the exact key `Overworld_RevealSecret` receives — with one location per object (big 2x2 rock piles are ONE location keyed by the pile-origin pos that `SmashRockPile_fromLift` normalizes to). In-scope classes: **grass axis** = bushes (map16 `0x036` LW / `0x72a` DW) and cuttable thick grass (`0x37e`); **rock axis** = small and big liftable rocks of both strengths, classified by the engine's map16 → map8 → `kMap8DataToTileAttr` attribute chain and the glove-requirement table (`kGetBestActionToPerformOnTile_a`). The registry SHALL be produced by a `--dump-terrain-table` CLI mode (engine ground truth, mirroring `--dump-pot-table`) feeding a committed generator script (`assets/scripts/gen_terrain_tables.py`) that asserts its own invariants: every map16 id matched by the engine's reveal helpers is classified, signs and dash-only bonk rocks are recognized and excluded, unknown liftable attribute classes hard-fail, and every registered object is present WITH IDENTICAL CLASS in every profile of its WORLD-STATE GROUP — the dump profiles partition into a non-inverted group (Open/Standard/Retro: vanilla, +events, pre-rescue latch states) and an inverted group (Inverted map overrides), which must each be internally consistent but need NOT agree across the boundary (Inverted is a different overworld, not a within-seed latch). Each registered object carries a `world_state_filter` — universal (present-and-equal in both groups), `[open,standard,retro]`, or `[inverted]` — that placement AND reachability both honor, so a group-scoped object is active only in its world_states. An object present in only some latch states of its group is a missable-check vector and is excluded and flagged for override review, as is any class mismatch; an object absent-or-class-inconsistent in every group is excluded entirely. (An earlier all-profile intersection that required presence across the inverted boundary wrongly dropped non-inverted-only Light-World objects, e.g. castle-area bushes, from Open/Standard/Retro seeds.) Location IDs are assigned by `(screen, pos)` sort rank from a fixed base chosen after the counts are measured; any registry change that renumbers is a `generator_version` bump. Two new location types (`LOCTYPE_Grass`, `LOCTYPE_Rock`) distinguish the axes for placement filtering and presentation. The generated registry artifacts are gitignored and mirrored by `setup_worktree.py` as a complete set; the reviewed overrides file is committed.

#### Scenario: Dump-to-registry counts reconcile with extracted secrets
- **WHEN** the generator runs over the terrain dump
- **THEN** every one of the vanilla overworld secret entries (the extraction's per-screen `Items:` lists) on screens `0x00-0x7F` is either matched to a registry object or reported under an explicit exclusion rule (structural, sign, follower-gated, out-of-scope class); an unmatched secret is a generator hard-fail

#### Scenario: Registry change bumps the generator version
- **WHEN** an object is added to or removed from the enumerated registry (changing sort ranks)
- **THEN** `kGeneratorVersion` is bumped and the corpus regenerated; the registry digest changes so stale-slot activation is refused (see the save-capability guard)

### Requirement: Two independent tier axes — off / junk / all

The randomizer SHALL expose two independent settings axes, `grass_shuffle` and `rock_shuffle`, each with tiers `off = 0`, `junk = 1`, `all = 2`, default `off`. Tier semantics per axis: `off` — the axis's locations are inactive at every placement site (the open-location loop, the junk-pad target count, and `Placement_SelfCheck` expected counts all skip them) and generation is byte-identical to a build without the feature; `junk` — the axis's locations are active but junk-only: they are excluded from the assumed-fill candidate set for pool (progression) items and are filled exclusively by the junk pad; `all` — the axis's locations are ordinary open locations (any item, including progression, may be placed there, with junk padding for the surplus). No `ITEM_Nothing` pinning is used: every active terrain location receives a real item. The vanilla secret items suppressed by an active location (fairies, bees, rupees, hearts) are NOT added to the item pool.

#### Scenario: Off tiers are placement-neutral
- **WHEN** a seed is generated with `grass_shuffle = off` and `rock_shuffle = off`
- **THEN** the placement digest, sphere data, and spoiler placements are byte-identical to the same seed generated before this feature existed (the `settings_hash` alone changes once, from the canonical-length append under this change's `generator_version` bump, and is stable thereafter)

#### Scenario: Junk tier never holds progression
- **WHEN** a seed is generated with an axis at `junk`
- **THEN** no pool/progression item is placed on any of that axis's locations, and `Placement_SelfCheck` fails the build if one is found there

#### Scenario: All tier is a full check family
- **WHEN** a seed is generated with `rock_shuffle = all`
- **THEN** progression items may be placed under rocks, and the assumed-fill reachability model gates them with the rock-strength predicates so the seed remains completable

### Requirement: Logic predicates model rock strength, grass cutting, and world crossing

Every terrain location SHALL carry a reachability predicate composed of: its overworld region (default binding via the `OVERWORLD_REGIONS` screen→region table, with a committed reviewed `terrain_logic_overrides.yaml` for split screens — ledges, fences, river banks — which the generator hard-fails on when a screen is flagged split-but-unreviewed); a tool gate — light rocks and light piles `CanLiftRocks()`, heavy (darker) rocks and heavy piles `CanLiftDarkRocks()`, thick grass a `CanCutGrass()` macro grounded in the engine's consume paths for map16 `0x37e` (sword or bombs; hammer and powder do not consume thick grass), bushes no tool gate (bare-handed lift); and a world-side gate that is world-state aware in a single entry (dark-world objects require `MoonPearl`-or-bypass in Standard/Open/Retro and are home-side in Inverted; light-world objects get the inverse in Inverted). When binding confidence is low the predicate SHALL over-gate rather than under-gate (an over-gate delays a check; an under-gate lets the placer strand progression).

#### Scenario: Heavy rock check requires L2 strength in logic
- **WHEN** a progression item is placed under a darker (heavy) rock in `all` tier
- **THEN** the reachability model requires `CanLiftDarkRocks()` (Titan's Mitt or Progressive Glove ×2) before that placement counts as reachable, and the placer never requires the player to lift that rock without it

#### Scenario: Swordless seed with thick-grass checks stays honest
- **WHEN** a swordless seed has `grass_shuffle = all` and a progression item under thick grass
- **THEN** the `CanCutGrass()` predicate requires an actually-available cutter (bombs, since swords are absent), or the placer routes the item elsewhere

#### Scenario: Bunny cannot collect terrain checks
- **WHEN** the player can reach a dark-world screen only as the bunny (no Moon Pearl, non-Inverted)
- **THEN** that screen's terrain locations are not reachable in logic (mirroring the pot-sanity dark-world rule), because the bunny can neither lift nor cut

### Requirement: Runtime reveal hook — grant once at consume sites only, suppress vanilla, replay after

The runtime SHALL intercept terrain reveals with a hook invoked from the CONSUMING call sites immediately before their `Overworld_RevealSecret` call — `Overworld_LiftingSmallObj` (lift and dash-bush), `SmashRockPile_fromLift` (big piles), the bush/thick-grass branch of `Overworld_ToolAndTileInteraction` (sword-cut; also the powder-in-hand route), and the bush/thick-grass branch of `Overworld_BombTile` — and SHALL NOT be placed inside `Overworld_RevealSecret` itself, because that function is also called speculatively for positions the action does not consume (the bomb path's `label_a` staircase probe, and every blast tile under the super-bomb follower), which would grant checks — including glove-gated rock checks — without the object being lifted or destroyed. For an active, unchecked registered location the hook SHALL: dispatch the placed item through the standard dispatcher (`Rando_DispatchVanillaGrant` → the shared quiet-receive used by pots: direct-grant items via the confirmation-icon path, everything else via the receipt write with immobilize-clear, ancilla-kill, no-free-slot retry, and the shared icon resolver), mark the location checked in the persistent checked-location bitmap, and suppress the vanilla secret: the call site skips `Overworld_RevealSecret` entirely (which also skips `AdjustSecretForPowder`) and sets `dung_secrets_unk1 = 0xFF` — the engine's no-spawn sentinel — so the downstream spawner neither spawns a stale secret nor rolls `Overworld_SubstituteAlternateSecret()` (which a ZEROED secret byte would trigger outdoors for bush/grass classes); the call site then uses its default replacement tile. Multi-object consumes SHALL be supported: one bomb blast can consume up to 9 registered objects in a single frame, and every one of them must be granted and marked (none silently dropped). For a checked or inactive location, and for any `(screen, pos)` not in the registry, vanilla behavior SHALL run untouched (vanilla bush secrets are infinitely refarmable in vanilla and contain no progression, so post-check replay reintroduces no dupes and preserves fairy/bee farming). The hook SHALL be inert when the randomizer feature flag is off so vanilla and RAM-compare runs are unaffected.

#### Scenario: Each consume path grants exactly once
- **WHEN** the player consumes an unchecked active terrain object by any path (lift a rock, cut a bush, bomb a bush, dash into a smashable pile, powder a bush)
- **THEN** the placed item is granted with the quiet-receive cue, the location is marked checked, and no vanilla secret, substituted random item, or powder-random item spawns from that reveal

#### Scenario: A non-consuming reveal probe grants nothing
- **WHEN** a bomb explodes adjacent to a registered rock (a blast tile the bomb does not consume, probed via the bomb path's non-bush branch), or the super-bomb follower's blanket probe touches registered objects
- **THEN** no check is granted and no location is marked checked — grants happen only when the object is actually consumed

#### Scenario: One bomb consuming several bushes grants all of them
- **WHEN** a single bomb blast destroys multiple unchecked active bushes
- **THEN** every destroyed bush's placed item is granted and its location marked checked in that frame, with no grant lost to receipt-slot contention

#### Scenario: Checked fairy bush farms vanilla again
- **WHEN** the player re-cuts a bush whose location is already checked and whose vanilla secret is a fairy
- **THEN** the vanilla fairy spawns (vanilla behavior restored), and no additional check is granted

#### Scenario: Vanilla runs are untouched
- **WHEN** the randomizer feature flag is inactive (vanilla game, or ROM-attached RAM-compare run)
- **THEN** the hook takes no action and `Overworld_RevealSecret` behaves byte-for-byte as vanilla

### Requirement: Structural secrets are never shuffled or suppressed

Terrain objects whose vanilla secret is structural SHALL be excluded from the registry so their vanilla behavior is always preserved: secret data bytes `>= 0x80` (tile-below reveals — holes, water, staircases, the big-rock stairs path that writes `save_ow_event_info |= 0x20`, and the Ice Palace portal rock), and the screen `0x5b` follower-gated secret. Special screens `>= 0x80` are entirely out of scope (the engine cannot reveal secrets there).

#### Scenario: Portal rock keeps its portal
- **WHEN** the player lifts the heavy rock covering the Ice Palace magic portal with terrain shuffle active
- **THEN** the vanilla structural reveal happens exactly as vanilla (portal exposed, event bit written); no check is granted and nothing is suppressed

### Requirement: Composition with other shuffles requires no normalization

The two terrain axes SHALL compose freely with existing shuffles with no `apply_derived_rules` coupling: door shuffle (terrain is overworld-only and invisible to the door key-prover); cave-entrance and dungeon-entrance/chain shuffles — the reason `pot_shuffle` is forced off under cave-entrance shuffle (`Settings_PotShuffleForcedOff`: cave/house pot IDs sit above the per-location entrance region-override range, so interior pots would evaluate from their vanilla overworld region) does NOT apply to terrain, because every terrain location is on the overworld surface and its region binding never routes through an entrance, with `OP_REGION_REACHABLE` resolved by the entrance-aware reachability; `pot_shuffle` and `enemy_drop_checks` (disjoint location key spaces and hooks); Retro (take-any/shop pinning untouched; the junk pad already accommodates Retro); Inverted (handled in-predicate, not by axis normalization); hunt goals (pieces may land under terrain in `all`; the `accessibility=none` hunt convention is unchanged); and the customizer (terrain locations pinnable by location id).

#### Scenario: Door shuffle does not force terrain off
- **WHEN** a seed has `door_shuffle = basic` and `grass_shuffle = all`
- **THEN** both remain active (no normalization), the door prover's key logic is unaffected, and generation succeeds or refuses on its own merits

#### Scenario: Cave-entrance shuffle does not force terrain off
- **WHEN** a seed has `shuffle_cave_entrances` active (Open/Standard) and `rock_shuffle = all`
- **THEN** both remain active — unlike `pot_shuffle`, terrain locations bind to overworld-surface regions that the entrance-aware reachability resolves per seed — and a corpus composition row pins this

#### Scenario: Terrain and pot checks coexist
- **WHEN** a seed has `pot_shuffle = all`, `grass_shuffle = all`, and `rock_shuffle = all`
- **THEN** all three families are active with disjoint location ids and the placement self-check passes

### Requirement: Generation-time self-checks for the terrain family

`--rando-selftest` SHALL verify: the `(screen, pos) → location` lookup table is strictly sorted and round-trips against the registry; the compiled-in terrain registry count agrees with the lookup table (so a corrupt or half-generated registry is caught); a junk-tier generation places no pool item on a junk-only location; and — because an assetless build legitimately ships with NO terrain registry — that a build with no terrain locations FAILS CLOSED for terrain-active settings (`BuildItemPool` returns an empty pool, so generation aborts) rather than silently producing a seed with zero terrain checks. The selftest PASSES on an assetless/empty-registry build (such a build is valid, just terrain-incapable); the "no silent fail-open" guarantee is enforced at generation (the fail-closed pool) and at slot activation (the registry-identity guard refuses a terrain-active slot on an empty/mismatched build), NOT by crashing the selftest.

#### Scenario: Empty-registry build fails closed, and the selftest proves it
- **WHEN** the binary is built with an absent/empty generated terrain registry and `--rando-selftest` runs
- **THEN** the selftest passes (an assetless build is valid), and it asserts that a terrain-active `BuildItemPool` returns an empty pool — proving generation fails closed instead of silently shipping zero terrain checks; a terrain-active seed is then refused at generation and at slot activation, never silently resolved to vanilla drops
