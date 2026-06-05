# randomizer-logic Specification

## Purpose
TBD - created by archiving change add-randomizer-support. Update Purpose after archive.
## Requirements
### Requirement: Location and region model with stable IDs

The logic engine SHALL represent the world as a directed graph of regions connected by edges, with locations attached to regions. Location IDs SHALL be reserved in a registry file (`assets/rando/location_registry.yaml`) that maps opaque numeric IDs to human-readable names. Renaming a location SHALL leave its numeric ID unchanged; the ID is the source of truth for placements, spoilers, and embedded save tables.

#### Scenario: Rename does not change ID
- **WHEN** a location's display name is changed (e.g., "Pyramid Fairy - Left" → "Pyramid Fairy - Sword") and the generator is rebuilt
- **THEN** the numeric ID assigned in the registry is unchanged and previously written placement tables remain interpretable

#### Scenario: ID removal requires explicit deprecation
- **WHEN** a location is removed from the active logic
- **THEN** the registry retains the ID marked as deprecated and the generator refuses to emit it in new placements, but reads of older placement tables can still resolve the deprecated ID to a name

### Requirement: Predicate VM op set

All inventory ops evaluate against the placer's simulated-inventory `uint16 counts[N]` array (per `randomizer-core / Simulated-inventory model for assumed fill`); implementors SHALL NOT maintain a separate bitfield for `OP_HAS_ITEM` — it is exactly `counts[id] >= 1`.

The predicate VM SHALL support the following operations in Phase A:

- Inventory: `OP_HAS_ITEM` (`counts[id] >= 1`), `OP_HAS_AMOUNT <id> <n>` (`counts[id] >= n`, single ID), `OP_HAS_ANY_OF <id_list>` (`any(counts[id] >= 1 for id in list)`), **`OP_HAS_ANY_COUNT <id_list> <n>`** (`sum(counts[id] for id in list) >= n`)
- State: `OP_WORLDSTATE_EQ`, `OP_GOAL_EQ`, `OP_GOAL_REQUIRES_DUNGEON`, `OP_DUNGEON_CLEARED`, `OP_REGION_REACHABLE`
- Shuffle-aware: `OP_HAS_PRIZE <prize_id>`, `OP_MEDALLION_OPENS <entrance_id>`
- Composition: `OP_AND`, `OP_OR`, `OP_NOT`

The op-code numeric assignments SHALL be append-only in `assets/rando/op_registry.yaml` — new ops receive new IDs at the end, removed ops keep their slot reserved (`deprecated: true`). Op-code bytecode is compiled into `logic_data.c` at build time and is not stored in any save; cross-version save compatibility does not depend on op-code stability.

#### Scenario: Composition of presence and amount
- **WHEN** a predicate is `(HAS_ITEM Boots) AND (HAS_ANY_OF Glove Mitt) AND NOT (WORLDSTATE_EQ Inverted)`
- **THEN** the VM evaluates correctly for any combination of the three operands

#### Scenario: Region-reachability sub-predicate
- **WHEN** a predicate references `OP_REGION_REACHABLE TurtleRock`
- **THEN** the VM evaluates the referenced region's full reachability under the current inventory, memoizing **per fixed-point iteration of the reachability outer loop** (not just per top-level call) — the cache is invalidated on every fixed-point iteration boundary because adding an item between iterations can make a previously unreachable region reachable

#### Scenario: OP_HAS_PRIZE resolves against per-seed prize assignment
- **WHEN** a predicate is `OP_HAS_PRIZE Prize_GreenPendant` and the active seed's prize shuffle assigned the Green Pendant to Eastern Palace
- **THEN** the predicate is true iff Eastern Palace is in the `cleared_dungeons` set

#### Scenario: OP_MEDALLION_OPENS resolves against per-seed medallion assignment
- **WHEN** a predicate is `OP_MEDALLION_OPENS MiseryMireEntrance` and medallion shuffle assigned Ether to Misery Mire
- **THEN** the predicate is true iff the inventory contains the Ether Medallion (and any other prerequisites encoded elsewhere)

#### Scenario: OP_HAS_ANY_COUNT sums distinct IDs
- **WHEN** a predicate is `OP_HAS_ANY_COUNT [BottleEmpty, BottleWithFairy, BottleWithBee, BottleWithGoodBee, BottleWithRedPotion, BottleWithGreenPotion, BottleWithBluePotion] 2` and the inventory contains 1 `BottleEmpty` and 1 `BottleWithFairy`
- **THEN** the predicate evaluates true (total count 2 across the union)

### Requirement: World-state variants

The logic engine SHALL support Open, Standard, Inverted, and Retro world-states. Each variant SHALL be expressible by toggling edges and predicates in the YAML; no hand-written C code per variant.

#### Scenario: Standard mode gates progression on uncle pickup
- **WHEN** world-state is Standard
- **THEN** no progression location outside Link's House and the Sanctuary is reachable until the uncle's item pickup edge is satisfied

#### Scenario: Inverted mode reverses light/dark default and entrance pair
- **WHEN** world-state is Inverted
- **THEN** Link begins in the dark world, the Moon Pearl is not required for dark-world traversal, the mirror is required to enter the light world, the Pyramid/Hyrule-Castle overworld pair is swapped, and Aga 1 is reached via the dark-world equivalent location

#### Scenario: Retro mode adds shop-purchased items to pool
- **WHEN** world-state is Retro
- **THEN** the additional locations (bow/silvers from shop slots, takeable hearts) are present in the location pool and the item pool is padded accordingly

### Requirement: Named predicate macros are mandatory

The logic YAML SHALL author all combat / multi-item predicates as named macros (e.g., `CanKillMostThings`, `CanDamageBoss`, `CanGetGoodBee`, `CanShootArrows`, `CanLightTorches`, `CanCutThickGrass`). Raw OP-chains expressing the same concept inline SHALL be refactored into macros during logic authoring. The generator SHALL fail the build if it detects predicates that exceed a configurable inline-complexity threshold (e.g., > 6 OP nodes) without referencing a named macro.

#### Scenario: Macros keep predicates readable
- **WHEN** a location's predicate is `CanKillMostThings AND HAS_ITEM Hookshot`
- **THEN** the YAML resolves `CanKillMostThings` via a single named-macro reference, not by inlining an OR-chain over 8 weapon item IDs

#### Scenario: Inline complexity check fails the build
- **WHEN** a predicate exceeds the inline-complexity threshold without using a named macro
- **THEN** the generator emits an error naming the file/line and the build fails, prompting the author to extract the macro

### Requirement: Reachability search with measured budget

The engine SHALL provide `Logic_ComputeReachability(inventory, settings) -> (reachable_locations, cleared_regions)` and SHALL complete one invocation in under 5 ms on reference desktop hardware and under 20 ms on Switch for the full ~216-location graph.

#### Scenario: Monotonic in inventory
- **WHEN** an item is added to the inventory snapshot
- **THEN** the reachable-location set returned for the new inventory is a superset of the set returned for the previous inventory under the same settings

#### Scenario: Budget benchmark
- **WHEN** `Logic_ComputeReachability` is called against the full graph on reference hardware
- **THEN** the median wall-clock across 1000 invocations is under 5 ms

### Requirement: Goal-completion predicates

Each Phase A goal (Defeat Ganon, Fast Ganon, All Dungeons, Pedestal, Triforce Hunt, **Ganon Hunt**, **Completionist**) SHALL be expressible as a predicate over inventory, event state, dungeon-cleared flags, prize assignments (via `OP_HAS_PRIZE`), and the independent `crystals.ganon` / `crystals.tower` settings (for goals that use them). The engine SHALL report whether the goal is reachable from a given inventory snapshot.

#### Scenario: Triforce Hunt completion
- **WHEN** the goal is Triforce Hunt with `pieces_required = N` and the inventory contains at least N triforce-piece items
- **THEN** the goal predicate evaluates to true

#### Scenario: Fast Ganon gates on crystals and tower access
- **WHEN** the goal is Fast Ganon, the configured crystal count is satisfied, and the inventory + reachability allows entry to Ganon's Tower
- **THEN** the goal predicate evaluates to true

#### Scenario: All Dungeons requires every required dungeon cleared
- **WHEN** the goal is All Dungeons and `OP_DUNGEON_CLEARED` is true for every dungeon for which `OP_GOAL_REQUIRES_DUNGEON` evaluates true under the current goal
- **THEN** the goal predicate evaluates to true

### Requirement: Logic YAML schema and worked examples (Phase A0/A0.5 deliverable)

The logic YAML schema SHALL be pinned in `assets/rando/logic.schema.yaml` (or equivalent JSON Schema) **before** logic.yaml authoring begins in A1. The schema SHALL define:

- The shape of `Region`, `Location`, `Edge`, and `Predicate` records.
- Reserved field names; allowed predicate primitives; macro reference syntax.
- For each `Location`: `can_reach` predicate (reachability) AND `can_place` predicate (per-location placement restriction). Both default to "always true" when omitted.
- The grammar of the predicate expression language (parens, precedence, named-macro references, op invocations).

Phase A0/A0.5 SHALL also commit **3-5 worked examples** under `assets/rando/logic_examples/` exercising the full schema surface so the schema is validated against real predicate shapes before A1's months-long authoring begins. Schema gaps caught at A0.5 are cheap; gaps caught at A1's codegen step (task 3.5) cost weeks of rework.

#### Scenario: Schema published before A1
- **WHEN** Phase A1 begins (logic.yaml authoring task 3.3)
- **THEN** `assets/rando/logic.schema.yaml` is checked in, validated against the 3-5 worked examples, and reviewed by the named logic-translation owner

### Requirement: Per-location can_place + always_allow predicates

Each location SHALL carry up to three predicates (matching ALTTPR's three-slot pattern at `app/Location.php` — `setRequirements` / `setFillRules` / `setAlwaysAllow`):

- `can_reach` — reachability under the simulated inventory (corresponds to ALTTPR's `setRequirements`).
- `can_place` — placement restriction under the simulated inventory + candidate item (corresponds to ALTTPR's `setFillRules`). Default true if omitted.
- `always_allow` — override that permits placement even when `can_place` would reject (corresponds to ALTTPR's `setAlwaysAllow`). Used in vanilla ALTTPR for cases like "the dungeon's own compass/map IS allowed at the boss-fight slot even though general fill rules would say no." Default false if omitted.

Placer order: `can_place(item) OR always_allow(item)` is consulted as the placement gate.

`can_place` evaluates against the simulated inventory, settings struct, AND a **`candidate_item` register**; it is consulted by the placer before putting an item into the location.

**Two VM evaluation entry points**:

- `Predicate_Evaluate(predicate, counts, settings) -> bool` — used for `can_reach`. Cacheable per `(predicate, inventory_snapshot)` because it does not depend on which item the placer is considering.
- `Predicate_EvaluatePlacement(predicate, counts, settings, candidate_item) -> bool` — used for `can_place`. Cacheable per `(predicate, candidate_item)` when the predicate does not reference inventory.

`OP_ITEM_IS <item_id>` evaluates the `candidate_item` register and SHALL appear only inside `can_place` predicates; the well-formedness pass rejects bytecode that uses `OP_ITEM_IS` in `can_reach`. Inventory predicates (`OP_HAS_ITEM` etc.) operate on `counts[]` regardless of context.

**Placer evaluation order**: for each `(location, item_candidate)` pair, the placer SHALL evaluate `can_reach` first, then (only if true) `can_place`. `can_reach` is cached per inventory snapshot across all candidate items; `can_place` is consulted per candidate. This minimizes work given that `can_reach` typically excludes a large fraction of locations.

**YAML surface syntax** for `OP_ITEM_IS` is pinned by the logic schema (task 3.5a): the schema chooses ONE of (a) sugar `(item IS X)` resolving to `OP_ITEM_IS X` at codegen, OR (b) explicit `OP_ITEM_IS(X)` only. Pick at schema commit; do NOT support both surfaces. The choice is documented in `logic.schema.yaml`.

Common uses:

- **Standard-mode uncle**: `can_place: NOT OP_ITEM_IS(SilverArrowUpgrade) AND NOT OP_ITEM_IS(MirrorShield) AND ...` — restricts the uncle's slot to plausible starting items.
- **Dungeon-item shuffle modes**: when `dungeon_items.small_keys = Dungeon`, every non-dungeon-X chest has `can_place: NOT OP_ITEM_IS(SmallKey_DungeonX)`.
- **Swordless-mode** restrictions (Phase B): non-sword locations have `can_place: NOT IS_ANY_OF_SWORDS(candidate)` where `IS_ANY_OF_SWORDS` is a **codegen-expanded macro** (the schema expands wildcards like `L*Sword` to explicit OR-chains over `OP_ITEM_IS` at YAML→bytecode time; the VM has no wildcard runtime feature — keeps the VM minimal).

#### Scenario: Standard-mode uncle restriction
- **WHEN** world-state is Standard and the placer attempts to place `MirrorShield` into the uncle's location
- **THEN** the uncle's `can_place` predicate returns false, the placer skips this location for the mirror shield, and tries another candidate

#### Scenario: Dungeon-mode small key stays in its dungeon
- **WHEN** `dungeon_items.small_keys = Dungeon` and the placer attempts to place `SmallKey_EasternPalace` into a Palace of Darkness chest
- **THEN** the PoD chest's `can_place` predicate returns false; only Eastern Palace chests accept the key

#### Scenario: always_allow overrides can_place rejection
- **WHEN** the Eastern Palace boss location's `can_place` rejects `CompassP1` (because the world disallows generic compass placement in this slot) BUT `always_allow` is set to "if `region.bossNormalLocation = true` and item is the dungeon's own Compass/Map"
- **THEN** the override fires, placement is permitted, and the result mirrors ALTTPR's behavior at `app/Region/Standard/EasternPalace.php` lines 102-105

### Requirement: Logic-graph well-formedness validation

The build-time logic generator SHALL run a validation pass on every commit. The pass SHALL assert all of: every requirement predicate parses, every referenced item ID exists in the item registry, every referenced location ID exists in the location registry, every region is reachable from the start under a full-inventory mock, there are no orphan locations, and `OP_REGION_REACHABLE` references do not form a non-memoized cycle.

#### Scenario: Missing item reference fails the build
- **WHEN** a YAML predicate references an item ID that is not in the registry
- **THEN** the generator emits an error naming the YAML file/line and the build fails

#### Scenario: Orphan location fails the build
- **WHEN** a location is defined without a parent region
- **THEN** the validator reports the orphan and the build fails

#### Scenario: Unreachable-under-full-inventory location fails the build
- **WHEN** a location cannot be reached even with every item in the pool
- **THEN** the validator reports it and the build fails (it is a logic-graph error, not a placement difficulty)

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

### Requirement: OP_TRICK predicate handler

`OP_TRICK trick_id` (op-code 15) SHALL evaluate true when bit `trick_id` is set in `settings.tricks` (uint64 bitmask). The trick_id space is enumerated in `assets/rando/op_registry.yaml` `tricks:` table; each entry has a stable bit position and a kebab-case name (`boots-clip`, `fake-flippers`, `bunny-revival`, etc.).

When `settings.tricks == 0` (Phase A default), every `OP_TRICK` predicate evaluates false; existing Phase A logic-graph behavior is preserved — non-trick seeds have the same reachability output before and after this change.

> **Stub status**: the exact trick set + per-location applicability is deferred to apply-time (depends on ALTTPR PHP grep findings).

#### Scenario: Default tricks=0 reproduces Phase A reachability
- **WHEN** a Phase A default-settings seed is generated (tricks=0)
- **THEN** `Logic_ComputeReachability` returns the same set of reachable locations as before this change; corpus digests match byte-for-byte

#### Scenario: Trick predicate unlocks location only when bit set
- **WHEN** a location's predicate is `AND(<base>, OP_TRICK boots-clip)`
- **AND** the seed has `settings.tricks` with the `boots-clip` bit cleared
- **THEN** the location is unreachable
- **WHEN** the same seed is regenerated with the `boots-clip` bit set
- **THEN** the location is reachable (assuming `<base>` is also satisfied)

### Requirement: OP_DIFFICULTY_AT_LEAST predicate handler

`OP_DIFFICULTY_AT_LEAST threshold` (op-code 16) SHALL evaluate true when `settings.item_pool_difficulty >= threshold`. The threshold encoding mirrors the enum: `easy=0 < normal=1 < hard=2 < expert=3`. Phase A already supports all four pool levels in pool composition; the op exposes the same axis in predicates.

> **Stub status**: per-location applicability is deferred to apply-time.

#### Scenario: Normal-difficulty seed satisfies "at-least-easy" predicate
- **WHEN** a seed has `settings.item_pool_difficulty = normal` and a location's predicate is `OP_DIFFICULTY_AT_LEAST easy`
- **THEN** the predicate evaluates true

#### Scenario: Easy-difficulty seed fails "at-least-hard" predicate
- **WHEN** a seed has `settings.item_pool_difficulty = easy` and a location's predicate is `OP_DIFFICULTY_AT_LEAST hard`
- **THEN** the predicate evaluates false; the location is unreachable

### Requirement: OP_GLITCH_LEVEL_AT_LEAST predicate handler

`OP_GLITCH_LEVEL_AT_LEAST threshold` (op-code 17) SHALL evaluate true when `settings.logic >= threshold`. The threshold encoding: `NoGlitches=0 < OverworldGlitches=1 < MajorGlitches=2`. Phase B exposes the first three; `HybridMG` and `NoLogic` are reserved for later phases.

When `settings.logic == NoGlitches` (Phase A default), every `OP_GLITCH_LEVEL_AT_LEAST` predicate with a non-zero threshold evaluates false; Phase A logic behavior is preserved.

> **Stub status**: per-location glitch predicate authoring deferred to apply-time.

#### Scenario: NoGlitches seed reproduces Phase A reachability
- **WHEN** a seed has `settings.logic == NoGlitches`
- **THEN** every `OP_GLITCH_LEVEL_AT_LEAST` predicate with non-zero threshold evaluates false; logic-graph reachability matches Phase A behavior

#### Scenario: OverworldGlitches unlocks Bumper Cave Ledge
- **WHEN** a seed has `settings.logic == OverworldGlitches` and the Bumper Cave Ledge location's predicate includes `OP_GLITCH_LEVEL_AT_LEAST OverworldGlitches`
- **THEN** the location is reachable (subject to its other predicate constraints)

### Requirement: OP_MODEWEAPONS_EQ predicate handler

`OP_MODEWEAPONS_EQ mode_weapons` (op-code 18) SHALL evaluate true when `settings.mode_weapons == mode_weapons`. The operand encoding mirrors the enum: `randomized=0`, `assured=1`, `vanilla=2`, `swordless=3`. The op exists so the logic graph can express swordless-mode relaxations (`mode.weapons=swordless`) without conflating them with the world-state or goal axes.

When `settings.mode_weapons != swordless` (the default `randomized`), every `OP_MODEWEAPONS_EQ(swordless)` predicate evaluates false; the swordless logic branches collapse to their non-swordless form so default-settings reachability — and the corpus placement/sphere digests — are preserved byte-for-byte.

Each swordless logic branch SHALL be authored as a pure inert addition: a branch of the form `(OP_MODEWEAPONS_EQ(swordless) AND X) OR ((NOT OP_MODEWEAPONS_EQ(swordless)) AND Y)` SHALL reduce to `Y` when not swordless, and a branch of the form `(OP_MODEWEAPONS_EQ(swordless) OR Z)` SHALL reduce to `Z`.

#### Scenario: Default seed reproduces non-swordless reachability
- **WHEN** a seed has `settings.mode_weapons == randomized` (or `assured`)
- **THEN** every `OP_MODEWEAPONS_EQ(swordless)` predicate evaluates false; the Ganon / Agahnim / medallion-cast / boss / tablet predicates reduce to their existing non-swordless form and the corpus digests are unchanged

#### Scenario: Swordless Ganon requires Hammer + silver arrows
- **WHEN** a seed has `settings.mode_weapons == swordless`
- **THEN** the Ganon predicate's weapon clause requires the Hammer AND a silver-arrow source (instead of the Master Sword); the runtime honors this (Hammer damages Ganon, silver arrows finish him)

### Requirement: Per-trick ROM-version verification status

ALTTPR upstream targets the Japanese 1.0 ROM; this fork targets the US 1.0 ROM. Tricks and glitch-level mechanics that ALTTPR's logic graph assumes available may have JP/US timing differences, mechanic differences, or be entirely absent on US 1.0. The randomizer SHALL track per-trick ROM-version verification status so users can distinguish "ALTTPR says this trick exists" from "we have confirmed this trick works on US 1.0."

> **Stub status**: scaffolding (op_registry `rom_version_status` field, codegen guard rejecting `jp10-only`, `fallback_warnings` emission for unverified tricks) deferred to apply-time per tasks §12.6.1-8. The fork-vs-ALTTPR ROM-version provenance gap is real (see `rom_version_unverified_tricks` memory) but is currently surfaced via task tracking, not by the binary.

Each entry in `assets/rando/op_registry.yaml`'s `tricks:` table SHALL carry a `rom_version_status` field with one of these values:

- `untested-on-us10` — trick is in the upstream logic graph but no contributor has confirmed it on US 1.0 (DEFAULT for newly-added tricks).
- `verified-us10` — trick has been performed end-to-end on a real US 1.0 build by a named contributor with the date recorded.
- `cross-version` — trick is a pure player skill (e.g., `dark-room-nav` — memorize the layout) OR a mechanic that has been verified to behave identically on JP 1.0 and US 1.0.
- `jp10-only` — trick has been confirmed to NOT work on US 1.0; SHALL NOT be used in trick gates.
- `us10-different` — trick exists on both ROMs but with different timing/mechanics; the upstream logic graph's assumptions may not transfer; needs per-site verification.

The same field SHALL be added to the `glitch_levels:` table in `op_registry.yaml` (OverworldGlitches, MajorGlitches, HybridMG, NoLogic), with the same value space.

The generator SHALL emit a spoiler `fallback_warnings` entry of kind `unverified_tricks_enabled` when any active trick bit (`settings.tricks`) or non-zero glitch level (`settings.logic`) corresponds to a trick whose `rom_version_status` is `untested-on-us10`, `jp10-only`, or `us10-different`. The warning detail SHALL list the offending trick names so race admins and seed validators can decide whether to accept the seed.

Tricks with `rom_version_status: jp10-only` SHALL NOT appear in any predicate body in `logic.yaml` or `logic_parts/*.yaml`; the codegen well-formedness pass SHALL reject any predicate that references them.

#### Scenario: Default seed has no unverified-tricks warning
- **WHEN** a seed is generated with `settings.tricks == 0` and `settings.logic == NoGlitches`
- **THEN** the spoiler's `fallback_warnings` array does NOT contain an `unverified_tricks_enabled` entry

#### Scenario: Enabling an untested-on-us10 trick surfaces warning
- **WHEN** a seed is generated with `settings.tricks` enabling a trick whose `rom_version_status` is `untested-on-us10`
- **THEN** the spoiler's `fallback_warnings` array contains an `unverified_tricks_enabled` entry naming that trick
- **AND** the seed is still generated (the warning is informational, not blocking)

#### Scenario: jp10-only trick rejected at codegen
- **WHEN** the codegen pass encounters a `logic.yaml` or `logic_parts/*.yaml` predicate that references a trick whose `rom_version_status` is `jp10-only`
- **THEN** codegen SHALL fail with an error citing the offending file and trick

#### Scenario: cross-version trick does not surface warning
- **WHEN** a seed is generated with `settings.tricks` enabling only tricks whose `rom_version_status` is `cross-version` or `verified-us10`
- **THEN** the spoiler's `fallback_warnings` array does NOT contain an `unverified_tricks_enabled` entry

#### Scenario: Glitch-level threshold same shape as trick verification
- **WHEN** a seed is generated with `settings.logic == OverworldGlitches` and `glitch_levels: [{name: OverworldGlitches, rom_version_status: untested-on-us10}]` in the registry
- **THEN** the spoiler's `fallback_warnings` array contains an `unverified_tricks_enabled` entry naming `OverworldGlitches`

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

