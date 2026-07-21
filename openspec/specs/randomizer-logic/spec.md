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

### Requirement: Renewable bomb access and explicit Standard escape ammo

`CanBombThings()` SHALL be universally true for ordinary-world logic because bomb
drops, terrain secrets, and shops provide renewable ammo without a permanent
bomb-bag item. The Standard escape combat predicate SHALL NOT inherit that
unconditional result: its bomb-as-weapon branch SHALL explicitly require one of
`Bombs1`, `Bombs3`, or `Bombs10`, allowing the escape-fill runtime to supply the
needed ammo.

#### Scenario: Ordinary bomb-wall checks do not require a shuffled bomb refill
- **WHEN** an ordinary-world cave or wall predicate uses `CanBombThings()`
- **THEN** bomb access is satisfied without a Bombs1/Bombs3/Bombs10 item in the simulated inventory

#### Scenario: Standard escape remains weapon-gated
- **WHEN** a Standard player has no sword, bow, hammer, rod, cane, or bomb-refill item
- **THEN** `CanKillEscapeThings()` remains false even though ordinary `CanBombThings()` is true

#### Scenario: Standard escape accepts a concrete bomb refill
- **WHEN** the Standard escape inventory contains Bombs1, Bombs3, or Bombs10
- **THEN** the bomb branch of `CanKillEscapeThings()` is true and the runtime escape refill can supply combat ammo

### Requirement: Reachability search with measured budget

The engine SHALL provide `Logic_ComputeReachability(inventory, settings) ->
(reachable_locations, cleared_regions)`. For the baseline graph (pot-shuffle off,
~216-328 locations — the 216 logic baseline up to the 328 current maximal pool) it
SHALL complete one invocation in under 5 ms on reference desktop hardware and under
20 ms on Switch. The active location graph grows with the `pot_shuffle` tier (up to
~1127 locations at `All`); for the pot-expanded graph one invocation SHALL complete
in under **30 ms on reference desktop and 120 ms on Switch** (provisional, ≈3.5× the
baseline since the added pot nodes are cheap `can_reach: TRUE()` predicates; to be
confirmed by measurement), and the placer SHALL keep `budget_seconds = 0` for
headless generation so placement stays machine-speed-independent.

#### Scenario: Monotonic in inventory
- **WHEN** an item is added to the inventory snapshot
- **THEN** the reachable-location set returned for the new inventory is a superset
  of the set returned for the previous inventory under the same settings

#### Scenario: Budget benchmark (baseline graph)
- **WHEN** `Logic_ComputeReachability` is called against the baseline graph
  (pot-shuffle off) on reference hardware
- **THEN** the median wall-clock across 1000 invocations is under 5 ms

#### Scenario: Pot-expanded graph stays within budget
- **WHEN** `pot_shuffle = All` and `Logic_ComputeReachability` runs against the
  ~1127-location graph on reference hardware
- **THEN** a single invocation completes in under 30 ms on reference desktop (120
  ms on Switch) — cheap `TRUE()` pot nodes — and generation determinism is preserved
  with `budget_seconds = 0`

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

`OP_GLITCH_LEVEL_AT_LEAST threshold` (op-code 17, added in Phase B #5) SHALL
evaluate true when `settings.logic >= threshold`. Phase B handled thresholds 0-2
(`NoGlitches` / `OverworldGlitches` / `MajorGlitches`). Phase D extends the
supported range to `HybridMajorGlitches` (3) and `NoLogic` (4).

**Tier structure (reconciled to ALTTPR `config/logic.php` at apply-time).** The
glitch levels are sets of per-technique flags, NOT a strict numeric chain. By
technique inclusion: `NoGlitches ⊂ OverworldGlitches ⊂ HybridMajorGlitches ⊂
MajorGlitches` — `MajorGlitches` is the *most* permissive. But the Phase-A enum
numbers `MajorGlitches=2 < HybridMajorGlitches=3`, so HMG is numerically above MG
yet a technique subset of it. Consequences for the monotone `>=` handler:

- **OverworldGlitches-group techniques** (canBootsClip, canFakeFlipper,
  canSuperSpeed, canMirrorClip, canWaterWalk, canDungeonRevive, canSuperBunny —
  enabled at OWG, HMG, MG) use `OP_GLITCH_LEVEL_AT_LEAST(overworld_glitches)`.
- **canOneFrameClipUW** (enabled at HMG and MG only) uses
  `OP_GLITCH_LEVEL_AT_LEAST(major_glitches)` — true at `logic ∈ {MG=2, HMG=3}`.
- **MajorGlitches-exclusive techniques** (canMirrorWrap, canOWYBA,
  canOneFrameClipOW, canTransitionWrapped — enabled at MG only) use
  `OP_GLITCH_LEVEL_AT_LEAST(major_glitches) AND (NOT
  OP_GLITCH_LEVEL_AT_LEAST(hybrid_major_glitches))`, which is true only at
  `logic == MajorGlitches`. This uses existing ops; no new op-code is added.

Because `HybridMajorGlitches ⊂ MajorGlitches`, **there are zero locations
reachable at HybridMajorGlitches but not at MajorGlitches.** Phase D therefore
authors NO bare `OP_GLITCH_LEVEL_AT_LEAST(hybrid_major_glitches)` location gates
(such a gate would open at HMG but close at the more-permissive MG — unfaithful).
HMG's only delta over OWG is `canOneFrameClipUW`, gated at threshold 2 above.

**NoLogic (threshold 4)**: when `settings.logic == NoLogic`, the predicate VM
SHALL short-circuit the **reachability** evaluation (`placement_context == 0`) —
every predicate evaluates true — so goal-completability and the per-tier
accessibility checks pass vacuously. Placement `can_place` predicates
(`placement_context == 1`) are NOT short-circuited, preserving dungeon-item
confinement so the seed stays structurally valid/loadable. The seed carries no
reachability guarantee.

When `logic == NoLogic`, the spoiler SHALL include a `fallback_warnings` entry:
`{"code": "no_logic_seed", "detail": "Seed generated with logic=NoLogic;
reachability is not enforced. The seed may be un-completable."}`. It is emitted
by reading `settings.logic` directly in the spoiler writer (the same pattern as
`unverified_tricks_enabled`), not via a placer counter.

#### Scenario: OWG-group technique opens at OverworldGlitches and above
- **WHEN** a location's reachability depends on an OWG-group technique macro
  (e.g. CanBootsClip) and the seed has `logic = overworld_glitches`
- **THEN** the technique disjunct evaluates true and the location is reachable
- **WHEN** the same seed has `logic = no_glitches` and no trick bit is set
- **THEN** the technique disjunct evaluates false

#### Scenario: canOneFrameClipUW gate opens at HMG and MG, closed at OWG
- **WHEN** a location gates on `OP_GLITCH_LEVEL_AT_LEAST(major_glitches)` (the
  canOneFrameClipUW tier) and the seed has `logic = hybrid_major_glitches`
- **THEN** the predicate evaluates true (HMG ⊇ {OWG + canOneFrameClipUW})
- **WHEN** the same predicate is evaluated with `logic = overworld_glitches`
- **THEN** it evaluates false

#### Scenario: MG-exclusive technique closed at HMG, open at MG
- **WHEN** a location gates on an MG-exclusive technique
  (`OP_GLITCH_LEVEL_AT_LEAST(major_glitches) AND NOT
  OP_GLITCH_LEVEL_AT_LEAST(hybrid_major_glitches)`) and `logic = major_glitches`
- **THEN** the predicate evaluates true
- **WHEN** the same predicate is evaluated with `logic = hybrid_major_glitches`
- **THEN** it evaluates false (HMG does not enable the MG-exclusive techniques)

#### Scenario: NoLogic short-circuit makes everything reachable
- **WHEN** a seed has `logic = no_logic`
- **THEN** every reachability predicate evaluates true; goal-completability
  returns true vacuously; the spoiler emits the `no_logic_seed` warning

#### Scenario: NoLogic disables un-completable refusal
- **WHEN** a seed has `logic = no_logic`
- **THEN** the strict refusal at `main.c` does NOT fire (reachability short-circuit
  means goal-completability and all accessibility tiers pass vacuously);
  generation succeeds; the spoiler warning surfaces the lack of guarantee

#### Scenario: NoLogic preserves placement confinement
- **WHEN** a seed has `logic = no_logic` and `dungeon_small_keys_mode = standard`
- **THEN** placement `can_place` predicates still evaluate normally, so dungeon
  small/big keys land in placeable dungeon slots and the seed round-trips through
  the slot save

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

### Requirement: Generic small-key door reachability

When `genericKeys` is in effect (Retro), key-door reachability SHALL evaluate
against the **shared `GenericKey` count** rather than per-dungeon
`SmallKey_<Dungeon>` possession. The placer SHALL guarantee that the generic-key
pool is reachable in an order that never strands the player behind a locked door
they cannot open (the assumed-fill shared-key invariant — see ALTTPR
`app/Filler/RandomAssumed.php`). A predicate that today reads
`HAS_ITEM_COUNT(SmallKey_<Dungeon>, n)` SHALL, under `genericKeys`, be satisfied
by the shared generic-key count subject to that no-strand guarantee.

When `genericKeys` is NOT in effect, key-door predicates SHALL be byte-identical
to the per-dungeon behavior (the generated logic for non-Retro seeds is
unchanged).

> **As built (archived 2026-06-05):** the chosen mechanism is a predicate-VM
> collapse in `src/rando/rando_logic.c` — under `world_state == Retro`,
> `eval_has_item` / `eval_has_amount` short-circuit ANY per-dungeon small-key
> requirement to `by_item_id[ITEM_GenericKey] >= 1`, a direct port of ALTTPR
> `ItemCollection::has()`'s ShopKey wildcard (`app/Support/ItemCollection.php`).
> `GenericKey` is treated as ordinary fungible progression by the assumed-fill, so
> no per-door floor or logic-graph rewrite is needed; big keys / maps / compasses
> are unaffected. Non-Retro evaluation is byte-identical (the collapse is
> world_state-gated). The acceptance bar remains the playtest of each goal at hard
> pool with no key-strand; the headless `Logic_SelfCheck` adds a cross-dungeon
> collapse assertion.

#### Scenario: Any key opens any door at runtime
- **WHEN** the player holds one or more generic keys in a Retro seed and reaches
  any small-key door in any dungeon
- **THEN** the door opens and the shared key count decreases by one, regardless of
  which dungeon the key was found in

#### Scenario: Placer never strands a Retro seed
- **WHEN** a Retro (genericKeys) seed is generated
- **THEN** every locked door on the path to the goal is reachable with keys the
  player can collect first; the seed is `goal_completable` with no unreachable
  placements across the corpus's Retro entries (including a hard-pool seed)

#### Scenario: Non-Retro reachability unchanged
- **WHEN** logic is generated for a non-Retro seed
- **THEN** key-door predicates gate on per-dungeon `SmallKey_<Dungeon>` exactly as
  before this change

### Requirement: Door-shuffle reachability via static oracle ops (Arch-2)

Door shuffle SHALL feed the reachability/placement logic through **two static
predicate-VM ops** — `OP_DOORS_ACTIVE(dungeon)` and `OP_DOORS_LOC_REACHABLE(loc)`
(op_registry ids 20/21, evaluated in `rando_logic.c`) — backed by a **reachability
oracle** that runs the SAME crystal-aware explorer the stitcher and key-door prover
use (`DoorExplore_Run`, per the `shuffle_doors.h` contract), so generator and logic
cannot drift. This replaces the originally-designed static per-room-region port
(579 regions + per-seed door-edge arrays + precompiled threshold families): no
region-cap bump, no per-seed edges, no per-location threshold offsets.

The codegen (`rando_logic_gen.py`) SHALL wrap every door-controlled location's
`can_reach` as:

```
(NOT DOORS_ACTIVE(d) AND <vanilla>) OR (DOORS_ACTIVE(d) AND DOORS_LOC_REACHABLE(loc))
```

`OP_DOORS_ACTIVE(d)` SHALL be true iff a door layout is installed AND the layout's
`shuffled_mask` has dungeon `d`'s bit set — so pinned dungeons (Hyrule Castle, Swamp
Palace at MVP) and `doorShuffle == vanilla` evaluate **exactly the vanilla predicate**,
and disabled door shuffle is byte-identical to the baseline (corpus invariant).
`OP_DOORS_LOC_REACHABLE` SHALL return false when no layout is installed.

Key-door edges inside the oracle SHALL gate on the prover's **worst-case** per-door
thresholds, which are conservative-safe: the oracle never certifies a location
reachable with fewer keys than reality, so it cannot ship an unbeatable seed.

`OP_DOORS_LOC_REACHABLE(loc)` SHALL resolve both static door-table locations and
generated pot-door locations. Generated pot-door locations SHALL be emitted only
when local pot artifacts are present.

For generated pot-door rows, the oracle SHALL use the same installed
`DoorShuffleLayout`, portal gates, inventory counts, big-key state, and
door-explorer flood as static door-table locations. The active door-shuffle branch
for a pot location SHALL be true only when:

- the pot's tier is active under effective settings,
- all mapped door-table regions for that pot are reached by the door explorer, and
- the generated pot-specific predicate, if any, evaluates true.

The inactive branch SHALL preserve the existing vanilla/fork predicate, including
static `POT_KEYS_*` gates for non-door seeds and pinned dungeons.

The generated active door branch SHALL use a base pot predicate captured before
static vanilla-door `POT_KEYS_*` terms are appended. It SHALL NOT reuse the full
non-door pot predicate if that would carry static vanilla key-depth terms into an
active door-shuffle dungeon.

#### Scenario: Disabled door shuffle preserves baseline reachability

- **WHEN** `doorShuffle == vanilla` (no layout installed)
- **THEN** `OP_DOORS_ACTIVE` is false for every dungeon, every wrapped predicate
  evaluates its vanilla branch, reachability equals the baseline, and the regression
  corpus digests are unchanged

#### Scenario: Pinned dungeon evaluates vanilla under an active layout

- **WHEN** a basic-shuffle layout is installed and the location's dungeon bit is
  clear in `shuffled_mask` (Hyrule Castle or Swamp Palace at MVP)
- **THEN** `OP_DOORS_ACTIVE(d)` is false and the location's vanilla predicate (and
  vanilla key thresholds) apply unchanged

#### Scenario: Shuffled layout changes a location's key threshold

- **WHEN** a basic shuffle moves a chest from three key-doors deep to behind one
  key door
- **THEN** `OP_DOORS_LOC_REACHABLE` floods the layout with the prover's worst-case
  per-door thresholds and reports the location reachable once one key is available,
  and the placer reflects the new threshold

#### Scenario: Reachability composes with global progression

- **WHEN** a shuffled dungeon location holds a progression item that gates the
  overworld
- **THEN** the oracle is queried inside the normal `Logic_ComputeReachability`
  fixed-point (memoized per dungeon per pass, invalidated each pass because the
  region bitset grows; a stale-by-one-pass cache can only underestimate, and the
  zero-change exit pass evaluates with exact inputs), so reaching that location
  expands global reachability as expected

#### Scenario: Arbitrary per-seed predicate path is not used

- **WHEN** the implementation wires per-seed door reachability
- **THEN** it evaluates the two STATIC ops against per-seed C state (the installed
  `DoorShuffleLayout` — the `OP_MEDALLION_OPENS`/`OP_CAN_KILL_BOSS` precedent), and
  does NOT attempt to synthesize runtime predicate bytecode through
  `Rando_FindPredicateOverride` (which is static-stream-offset only)

#### Scenario: Door oracle answers a pot location

- **WHEN** a generated pot location is in a dungeon whose bit is set in the installed
  door layout's shuffled mask
- **THEN** its wrapped predicate evaluates the `OP_DOORS_LOC_REACHABLE` branch
- **AND** the result comes from the door explorer instead of the static vanilla
  door-depth predicates

#### Scenario: Pinned dungeon pots use vanilla branch

- **WHEN** door shuffle is active but a pot belongs to a dungeon whose shuffled-mask
  bit is clear
- **THEN** `OP_DOORS_ACTIVE(dungeon)` is false for that dungeon
- **AND** the pot uses its existing vanilla/fork predicate

#### Scenario: Missing bridge artifact fails closed

- **WHEN** local pot artifacts are present but the key-depth artifact lacks
  generated door-pot room bindings
- **THEN** code generation fails with a clear error naming the stale artifact
- **AND** the build does not silently emit a partial door-pot bridge

#### Scenario: Pot outside the door graph keeps vanilla predicate

- **WHEN** a non-key pot's room has no generated door-table room binding
- **THEN** no generated door-pot row is emitted for that pot
- **AND** the pot continues to use the existing vanilla/fork predicate path

#### Scenario: Missing key-pot mapping fails closed

- **WHEN** local pot artifacts are present and a key pot cannot be mapped to its
  exact door-table region and static drop-key index
- **THEN** code generation fails with a clear error naming the key pot
- **AND** the build does not silently duplicate or lose a dungeon small-key source

#### Scenario: Active door pot ignores static key-depth terms

- **WHEN** a generated pot location is in an actively door-shuffled dungeon
- **THEN** the active branch evaluates door-oracle reachability plus the base pot
  predicate
- **AND** it does not also require the static vanilla-door `POT_KEYS_DUNGEON` or
  `POT_KEYS_WILD` depths

### Requirement: Oracle portal seeding from a committed gate table

The oracle SHALL seed its flood from the dungeon's **portal lobbies** — not from a
single entrance — using the committed per-portal gate table
(`assets/rando/door_portals.yaml`, compiled to `kDoorPortalGates`). Each row maps a
reference portal lobby region to the **fork region** whose reachability gates it,
plus an optional extra predicate compiled into the static stream. A portal seeds
the flood only when its fork region is set in the live region bitset AND its
predicate (if any) holds. Gate predicates SHALL reference fork regions and items
only (never `OP_DOORS_*`), keeping the seeding monotone with no oracle recursion.
A null fork region means the portal never independently seeds the flood (it remains
a valid stitch target reachable from inside).

The SAME table SHALL supply the generator's portal analysis, so generation and
logic agree on which portals are enterable.

#### Scenario: Multi-portal dungeon seeds only reachable portals

- **WHEN** the oracle floods a multi-portal dungeon (e.g. Desert Palace) and only
  the front-lobby fork region is reachable without lifting rocks
- **THEN** the flood seeds from the front lobbies only; the Back-lobby portal joins
  the seed set once its gate (`CanLiftRocks()`) holds — a too-strong gate can only
  under-seed (the placer refuses; it never certifies an unbeatable seed)

#### Scenario: Portal gates evaluate without oracle recursion

- **WHEN** a portal gate predicate is evaluated during an oracle flood
- **THEN** it runs in the normal VM context against regions/items only and cannot
  re-enter the oracle (`g_in_door_oracle` guards the invariant)

### Requirement: Drop-key economy shared by prover and oracle

Small-key availability SHALL be computed identically by the key-door prover and
the logic oracle: `held_keys = chest-key count (from RandoCounts) + reachable
drop-key rooms in the current exploration state` (the codegen'd `kDoorTblDropKeys`
table of forced small-key drop rooms). The placer's item counts stay chest-only —
the drop contribution is supplied inside the explorer, never double-counted.

#### Scenario: Reaching a drop-key room raises the effective key count

- **WHEN** the oracle's flood reaches a room in the dungeon's drop-key table while
  Link holds N chest keys
- **THEN** key-door edges of that dungeon evaluate against N+1 (and the prover used
  the same definition when it validated the layout, so the two never desync)

### Requirement: OP_SOULS_TIER_AT_LEAST predicate handler
The predicate VM SHALL provide an `OP_SOULS_TIER_AT_LEAST <tier:u8>` opcode that evaluates true when the seed's `souls_shuffle` tier is at or above the operand (off=0, bosses=1, bosses+enemies=2), enabling `NeedsSoul(X) := NOT SOULS_TIER_AT_LEAST(t) OR HAS_ITEM(Soul_X)`-style macros so a single logic blob serves all tiers.

#### Scenario: Off seed reproduces baseline reachability
- **WHEN** reachability is computed for a `souls_shuffle=off` seed
- **THEN** every souls-conditional predicate evaluates as if the soul requirement were absent, and reachability matches the pre-souls baseline

#### Scenario: Tier gates bind at their tier
- **WHEN** a predicate requires `SOULS_TIER_AT_LEAST(2)` context and the seed is `souls_shuffle=bosses`
- **THEN** the enclosed soul requirement does not bind, while `SOULS_TIER_AT_LEAST(1)` requirements do

### Requirement: Boss kill requires the assigned boss's soul
When a souls tier is active, `OP_CAN_KILL_BOSS` SHALL additionally require possession of the soul of the boss actually assigned to that dungeon, resolved through the same per-seed `boss_assignment` input (vanilla-boss fallback when boss shuffle is off) via a generated boss-pool-index → soul-item table in which both Agahnim entries map to the single Agahnim Soul.

#### Scenario: Soul requirement follows boss shuffle
- **WHEN** boss shuffle assigns Mothula to Eastern Palace in a souls-tier seed
- **THEN** Eastern Palace's boss and prize locations require the Mothula Soul (not the Armos Knights Soul), and the placer guarantees it is reachable before those locations are required

#### Scenario: Vanilla assignment without boss shuffle
- **WHEN** boss shuffle is off in a souls-tier seed
- **THEN** each dungeon's boss/prize locations require that dungeon's vanilla boss's soul

### Requirement: Kill-gated room and enemy-held-key soul requirements
Under the `bosses+enemies` tier, logic SHALL require the resident species' souls for everything a kill-gated room gates, derived from game data by a committed generator (`gen_soul_room_tables.py`): room-header kill tags classify each room (kill→doors vs kill→chest, verified against the `kDungTagroutines` dispatch), room sprite lists give the resident soul sets, and a flood over the committed vanilla door graph (`door_tables.gen.c`) yields the regions/locations reachable only through each room's shutter doors. The generated wraps SHALL cover fork chest/boss/prize locations (by id), the Agahnim 1 event, and generated pot/enemy-check/enemy-drop locations (by door-region/room); rooms whose residents carry no enemy soul are waived (soul-less species always spawn and enemy shuffle substitutes only souled species), and boss-soul rooms (GT refights) are covered by the `CanKill<Boss>` macro terms instead. Generation SHALL fail closed — `BuildItemPool` refuses a `bosses+enemies` seed — when the generated table was absent at codegen (`kRandoSoulRoomsBaked == 0`).

#### Scenario: Kill-gated traversal requires resident souls
- **WHEN** a progression path routes through a kill-to-open-door room under the `bosses+enemies` tier
- **THEN** logic requires the souls of that room's resident species before considering onward locations reachable (e.g. every Ice Palace location requires the Bari and Pengator souls; Agahnim 1 requires the Soldier, Ball-and-chain Trooper, and Keese souls)

#### Scenario: Kill-revealed chest requires resident souls
- **WHEN** a chest is revealed by a kill-clear room tag under the `bosses+enemies` tier
- **THEN** that chest's predicate requires the room's resident souls

#### Scenario: Non-itemized enemy-held keys stay collectable (runtime exemption)
- **WHEN** `enemy_drop_checks` does not itemize forced key drops (effective off) under the `bosses+enemies` tier
- **THEN** the forced-drop holder spawns regardless of soul ownership (static-hook exemption keyed on the vanilla drop-source slot), so the vanilla free-key counting model stays true and key-availability logic needs no soul terms for those drops

#### Scenario: Itemized enemy-held key requires the holder's soul
- **WHEN** `enemy_drop_checks` itemizes a forced key drop as a location under the `bosses+enemies` tier
- **THEN** that location's predicate requires the vanilla holder's soul and normal suppression applies to the holder

#### Scenario: Missing generated table fails generation
- **WHEN** the room → soul-set table is absent and a `bosses+enemies` seed is requested
- **THEN** seed generation fails with a diagnostic instead of producing a seed with silently weakened logic

### Requirement: Souls degrade to off under door shuffle
`Settings_EffectiveSoulsShuffle` SHALL degrade `souls_shuffle` (any tier) to `off` when door shuffle is active: the door-shuffle oracle is species-blind, the enemies tier's generated kill-room requirements are computed against the vanilla door graph, and even the bosses tier's soul-gated boss/prize predicates make the door-layout fill exhaust its attempt budget per layout candidate. The pool, the logic VM, the placer gate, the runtime suppression, and the canonical settings hash SHALL all read the effective value.

#### Scenario: Door shuffle disables souls everywhere
- **WHEN** a seed requests any `souls_shuffle` tier with `door_shuffle=basic`
- **THEN** no souls enter the pool, no suppression binds at runtime, souls-conditional logic evaluates as off, and the canonical settings hash reflects the degraded (off) value

### Requirement: Enemy-check locations require the source species' soul
Under the `bosses+enemies` tier, every enemy-check location (`LOCTYPE_Enemy`) and forced enemy-drop location (`LOCTYPE_EnemyDrop`) SHALL additionally require the soul of its source species, emitted by the enemy-check table generator (which already carries `source_type` per check) into the location's predicate; the three GT-miniboss check locations SHALL instead be covered by the GT-refight boss-soul gates, and the virtual `HyruleCastleBigKey` derivation SHALL inherit its underlying forced-drop check's soul requirement.

#### Scenario: Ordinary enemy check gated on its species
- **WHEN** a `bosses+enemies` seed places an item on an enemy-check location whose source species' soul is un-owned
- **THEN** logic treats that location as unreachable until the soul is in the player's sphere, and the placer orders placements accordingly

#### Scenario: Off and bosses tiers leave enemy-check predicates unchanged
- **WHEN** a seed uses `souls_shuffle=off` or `souls_shuffle=bosses` with any `enemy_drop_checks` tier
- **THEN** ordinary enemy-check predicates evaluate exactly as without the souls feature (GT-miniboss checks gate through boss souls at the `bosses` tier)

#### Scenario: Standard escape big-key guard
- **WHEN** a Standard-mode `bosses+enemies` seed gates the Ball-and-chain guard's big-key drop on that guard's soul
- **THEN** the placer places that soul reachable within the pre-rescue sphere (or refuses the seed), never producing a seed where the escape cannot be completed

### Requirement: Boss-species kill gates bind at the bosses tier
The Ganon's Tower refight rooms (Armos Knights, Lanmolas, Moldorm) SHALL carry static soul requirements for those three bosses under any active souls tier, since boss species suppress at the `bosses` tier and the refight rooms are outside the boss-shuffle room set.

#### Scenario: GT climb requires refight souls
- **WHEN** a `souls_shuffle=bosses` seed's logic evaluates the GT climb past a refight room
- **THEN** progression beyond each refight requires that boss's soul

### Requirement: Goal predicates require Agahnim and Ganon souls
When a souls tier is active, goal-completion and world-progression predicates that require killing Agahnim or Ganon SHALL additionally require the Agahnim Soul or Ganon Soul respectively.

#### Scenario: Ganon goal gated on his soul
- **WHEN** a souls-tier seed's goal requires defeating Ganon
- **THEN** the goal-completion predicate requires the Ganon Soul, and the placer places it reachable before it is required

#### Scenario: Agahnim-dependent dark-world access gated
- **WHEN** logic evaluates a route to the Dark World that goes through defeating Agahnim 1
- **THEN** that route requires the Agahnim Soul (alternative non-Agahnim routes are unaffected)

### Requirement: NPC-driven checks require the involved souls

Every roster-gated check's reachability predicate SHALL AND in `(NOT OP_NPC_SOULS_ACTIVE) OR HAS_ITEM(Soul_<person>)` for EVERY person involved in obtaining the check, injected by the logic codegen from the committed `npc_souls.yaml` gate table (never via logic_parts duplicate entries). Multi-person checks: Maze Race requires both race NPCs' souls; Blacksmith requires Home Smith + Frog; Purple Chest requires Home Smith + Frog + Middle-Aged Man; Pyramid Fairy checks require Pyramid Fairy + Bomb Shop dealer.

#### Scenario: Maze Race gated on both people
- **WHEN** `npc_souls=on` and the player owns the Maze Game Lady Soul but not the Maze Game Guy Soul
- **THEN** the Maze Race check is out of logic, and the placer never requires it before both souls are reachable

#### Scenario: Off-tier collapse
- **WHEN** `npc_souls=off`
- **THEN** every injected soul term evaluates true and reachability is identical to the pre-feature graph (placement-digest-proven)

### Requirement: Kiki's soul gates Palace of Darkness entry

With `npc_souls=on`, the Palace of Darkness entry edge SHALL require the Kiki Soul in world states where vanilla PoD entry is opened by Kiki's payoff; world states (or routes) that legitimately enter PoD without Kiki (e.g. a dungeon-chains seam entry, or Inverted access if the inverted graph does not route through Kiki) SHALL NOT carry the term on those routes.

#### Scenario: PoD locked without Kiki
- **WHEN** an Open-state seed has `npc_souls=on` and the Kiki Soul is not yet reachable
- **THEN** no Palace of Darkness location (including its Boss and Prize) is in logic, and the assumed fill never strands PoD-mandatory progression behind the Kiki Soul's own gate

#### Scenario: Chains seam bypass stays consistent
- **WHEN** dungeon chains route Palace of Darkness as a successor dungeon and the seam teleports the player into its lobby
- **THEN** the seam entry requires no Kiki Soul (matching runtime, where the overworld entrance is not used)

### Requirement: The new predicate op is structurally complete

`OP_NPC_SOULS_ACTIVE` SHALL be appended to the op registry, evaluated by the VM, and covered by the skip walker's operand table; the structural selfcheck walk over all generated predicate blobs SHALL pass with the new wraps emitted.

#### Scenario: Selfcheck catches a skip mismatch
- **WHEN** the op is wired into `eval()` but its operand layout is missing or wrong in `skip_pred`
- **THEN** `Logic_SelfCheck`'s blob walk fails before the build ships

### Requirement: Auto-generated region-bound pot locations

Pot locations SHALL be generated, not hand-authored. The committed
`gen_pot_tables.py` (see `randomizer-pot-sanity / Build-time pot enumeration with
stable identity`) SHALL emit one logic entry per pot, each bound to the logic
region that owns its dungeon room, with `can_reach: TRUE()` by default — so a pot
is reachable exactly when its region is, the existing region-default semantics
(`Location and region model with stable IDs`). Every pot entry SHALL carry a
`region:`; a pot with no region (which would encode `0xFFFF` = silent sphere-0
reachability) SHALL be a generator hard-error. **Same-room does NOT imply same
reachability** (split rooms, water states, crystal switches, one-way drops,
bomb/key/big-key doors, dark rooms, dungeon-state variants differ within one room),
so gate derivation SHALL distinguish:
- **Uniform room** (all authored locations in the room share one region and
  predicate): a pot inherits that predicate automatically.
- **Non-uniform room** (authored predicates differ, or the room is on a known
  multi-subregion list): the generator SHALL REQUIRE an explicit reviewed per-pot or
  per-subregion gate in a committed `pot_logic_overrides.yaml` and SHALL FAIL THE
  BUILD until one is supplied — no silent inheritance.

This makes a falsely-`TRUE()` pot impossible to ship without review. Additionally, a
pot in a dungeon whose pot keys are shuffled SHALL carry the small-key gate of
`Pot-key small-key logic gating` — a per-pot key-door-depth term layered onto its
region/inheritance predicate (so the `can_reach: TRUE()` default is the *base*, not the
final predicate, once `pot_shuffle` + a shuffled key mode are active). Pot locations
SHALL be included in the active location set **only when the `pot_shuffle` tier
selects them** — realized as a skip in the open-location / junk-pad / reachability
loops. `kRandoLocationsCount` (the static registry size) GROWS to ~1127; with
`pot_shuffle = Off` the active/open-location SET (and thus reachability and
placement) is byte-identical to the baseline because pots are skipped in iteration,
NOT because the count stays small. The location-id ceiling and reachability bitset SHALL be raised
from 512 to 2048 (`328 + 799 = 1127` locations at the maximal tier), including a
`LOC__COUNT <= 2048` build-time assertion, kept in lockstep with the placement
working-array capacity (`randomizer-placement`).

#### Scenario: Pot is reachable iff its region is reachable
- **WHEN** a generated pot has `region: <room's region>` and `can_reach: TRUE()`
- **THEN** the reachability search marks it reachable exactly when its region is
  reachable, with no per-pot predicate evaluation

#### Scenario: A pot with no region fails the build
- **WHEN** `gen_pot_tables.py` would emit a pot location without a `region:`
- **THEN** the generator aborts with an error identifying the pot, preventing the
  silent sphere-0 reachability trap

#### Scenario: Dark-room pot requires light
- **WHEN** a pot is in a dark room and `pot_logic_overrides.yaml` sets its
  `can_reach` to `(HAS_ITEM(Lamp) OR CanDarkRoomNav())`
- **THEN** the placer treats it as reachable only with a Lamp or the dark-room-nav
  trick, like any dark-room chest

#### Scenario: Non-uniform room without a reviewed gate fails the build
- **WHEN** a room contains authored locations with differing `can_reach` predicates
  (or is on the multi-subregion list) and a pot there has no reviewed override
- **THEN** `gen_pot_tables.py` aborts with an error naming the pot/room, refusing to
  inherit a possibly-wrong predicate

#### Scenario: Active set unchanged with pot-shuffle off (count grows, iteration skips)
- **WHEN** `pot_shuffle = Off` (note `kRandoLocationsCount` has grown to ~1127)
- **THEN** no pot enters the active/open-location set — every pot is skipped in the
  collection / junk-pad / reachability loops — so reachability and placement are
  byte-identical to the pre-change build despite the larger registry

### Requirement: Pot-key small-key logic gating

A pot-bearing dungeon's deep locations and pots SHALL gate on the small-key
requirement that pot_shuffle adds once that dungeon's pot keys become shuffled items.
Three predicate-VM ops drive this, all false (so the wrap is inert and placement is
byte-identical) when pots are off:

- `OP_POT_KEYS_ON` — `Settings_PotKeysActive`: `pot_shuffle >= Keys` AND pots are not
  forced off by effective cave-entrance shuffle (`Settings_PotShuffleForcedOff`) — the
  SAME shared accessor `pot_active` / `BuildItemPool` use, so the gate can never drift
  from which pots are actually pooled. Door shuffle does not force this op false; door
  reachability is handled by the baseline `randomizer-logic / Door-shuffle reachability
  via static oracle ops`. The placer/logic VM consume RAW settings, so this MUST read the
  accessor, never raw `pot_shuffle` or cave fields.
- `OP_POT_KEYS_WILD` — `OP_POT_KEYS_ON` AND small keys are wild (keysanity / Retro).
- `OP_POT_KEYS_DUNGEON` — `OP_POT_KEYS_ON` AND small keys are dungeon (per-dungeon).

A pot-bearing dungeon's affected `can_reach` (its chest/boss/prize locations and its
in-dungeon pots) SHALL be wrapped:

`<vanilla predicate> AND (NOT POT_KEYS_WILD() OR HAS_AMOUNT(SmallKey_X, full)) AND (NOT POT_KEYS_DUNGEON() OR HAS_AMOUNT(SmallKey_X, dungeon))`

- `full` is the prover WORST-CASE key-door count, CAPPED at the pooled key count
  (chest + pot keys): under wild keys the keys live anywhere in the world, so you must
  HOLD that many before reaching; the non-pot drops auto-collect in-context so the cap
  is the true external requirement.
- `dungeon` is the prover SHORTEST-PATH (MIN-depth) key-door count: under dungeon keys
  the keys are collected en route, so the graduated min-depth is necessary + sufficient
  for a known layout — a flat worst-case would be circular (a key sits behind the very
  door it opens). The dungeon term is REQUIRED, not redundant: the vanilla `cur` value
  assumes the pot keys drop FREE, so a location whose keys are now items is UNDER-gated
  until this term raises it to min-depth.
- KEY pots SHALL use their EXACT region min-depth (over-gating a key pot is circular →
  spurious refuse; under-gating strands); loot / empty pots SHALL use the room-MAX
  min-depth (they hold no key, so over-gating only delays the check and never strands);
  chest/boss/prize SHALL use their own location min-depth.

The depths SHALL be generated, not hand-authored: `assets/scripts/gen_pot_key_depth.py`
runs the door key-door prover (`--dump-key-depth`, which emits both worst-case `depth`
and shortest-path `mindepth`) and emits local gitignored
`assets/rando/pot_key_depth.gen.yaml`, cross-checking the per-key-pot depths against a
reviewed table so a join drift fails the build. `rando_logic_gen.py` applies the wrap
from that table.

#### Scenario: Wild keys gate the held worst case
- **WHEN** a seed has wild keys + `pot_shuffle` and the player must reach a deep
  location whose pot keys are now world items
- **THEN** its `can_reach` requires `HAS_AMOUNT(SmallKey_X, full)` (worst case capped
  at chest+pot keys), so the placer never strands a progression item behind keys the
  player cannot yet hold

#### Scenario: Dungeon keys gate the in-context min-depth
- **WHEN** a seed has dungeon keys + `pot_shuffle`
- **THEN** each affected location requires `HAS_AMOUNT(SmallKey_X, dungeon)` (the
  shortest-path key-door count); a key pot uses its exact region depth and a loot/empty
  pot the room max, so the keys collected en route always suffice and a key is never
  placed behind its own door

#### Scenario: Pots off leaves the gating inert
- **WHEN** `pot_shuffle = Off` (or vanilla keys, or effective cave-entrance shuffle
  forces pots off)
- **THEN** `POT_KEYS_WILD` and `POT_KEYS_DUNGEON` both evaluate false, the wrapped
  terms collapse to the vanilla predicate, and reachability + placement are
  byte-identical to the pre-feature build

#### Scenario: Door shuffle keeps pot-key gates active
- **WHEN** `door_shuffle != vanilla`, `pot_shuffle >= Keys`, small keys are shuffled,
  and effective cave-entrance shuffle is off
- **THEN** `POT_KEYS_WILD` or `POT_KEYS_DUNGEON` evaluates according to the effective
  key mode, so door-shuffled active pots are gated by the door-pot baseline rather than
  being collapsed to pots-off

### Requirement: Enemy-drop key logic from generated registry rows

Enemy-drop check locations SHALL be generated into the logic graph with stable ids,
regions, vanilla items, and reviewed reach predicates from
`assets/rando/enemy_drops.gen.yaml`. Missing registry fields, unknown regions,
duplicate `(room, source_slot)` rows, duplicate active rooms, unsupported drop kinds,
or missing one-shot big-key metadata SHALL fail codegen.

When enemy-drop keys are active, the formerly free small keys are item locations and
the lone castle big-key marker is a one-shot item check. Wild effective small-key
mode SHALL use `ENEMY_DROP_KEYS_WILD()` gates for generated worst-case key-depth
requirements; Retro reuses the same effective mode and placement pools
`GenericKey`. Vanilla-door Dungeon mode SHALL use `ENEMY_DROP_KEYS_DUNGEON()` gates
for generated min-depth requirements. Generated enemy-drop locations MAY suppress
their own inherited same-key predicates when replacing them with exact DROP-region
`key_mindepth` terms.

The generated registry SHALL also carry ordinary dungeon location key-depth rows
for every active enemy-drop small-key dungeon. Codegen SHALL wrap matching ordinary
locations and world-state overrides with enemy-drop key-depth requirements so
locations whose vanilla logic assumed forced enemy keys were free cannot appear
reachable before enough itemized keys are held or collected in-context. These
ordinary-location wraps SHALL be additive to avoid weakening existing vanilla or
pot-sanity gates.

The reviewed Hyrule Castle Ball-n-chain big-key marker SHALL expose a logic-visible
virtual `HyruleCastleBigKey` side effect when `enemy_drop_checks = Keys` and the
Ball-n-chain one-shot check is reachable. Runtime counts SHALL also derive the
same virtual item from the live Hyrule Castle big-key bit. Hyrule Castle Zelda's
Cell and the Zelda rescue event SHALL require this virtual side effect when
enemy-drop keys are active.

When door shuffle is active, generated enemy-drop locations SHALL be wrapped by
`DOORS_LOC_REACHABLE(loc_id)`, and the door oracle SHALL evaluate those enemy-drop
locations through the generated door x enemy-drop bridge.

The enemy-drop location itself SHALL use the generated region and predicate. When
enemy-drop checks are inactive, placement/tracker active-location iteration SHALL
skip the generated ids, enemy-drop key-depth predicates SHALL collapse to the
pre-existing reachability rules, and the Hyrule Castle virtual big-key side effect
SHALL not be derived from enemy-drop reachability.

#### Scenario: Active enemy key source has generated logic data
- **WHEN** an enemy forced small-key source becomes a check
- **THEN** its logic entry carries the generated region and reviewed reach predicate,
  and codegen fails if required registry fields are missing

#### Scenario: Dungeon keys use exact generated min-depth
- **WHEN** dungeon small keys are shuffled, door shuffle is vanilla, and enemy-drop keys are active
- **THEN** generated enemy-drop locations participate in reachability with exact
  DROP-region min-depth gates for their own key item

#### Scenario: Ordinary locations get enemy key-depth gates
- **WHEN** a non-enemy dungeon location is behind doors whose forced enemy keys are
  active checks
- **THEN** generated key-depth metadata adds Wild and Dungeon enemy-key gates to
  that location without removing existing vanilla or pot-sanity gates

#### Scenario: Hyrule Castle enemy-drop chain gates Zelda correctly
- **WHEN** `enemy_drop_checks = Keys` and Hyrule Castle small keys are Dungeon
- **THEN** Boomerang Chest/Boomerang Guard require the first HCE key, the
  Ball-n-chain one-shot check requires the second HCE key, Zelda's Cell requires
  the Ball-n-chain-derived `HyruleCastleBigKey`, and the Zelda rescue event also
  requires the sewer key path

#### Scenario: Door shuffle uses generated enemy bridge
- **WHEN** door shuffle and enemy-drop keys are active
- **THEN** generated enemy-drop locations participate in reachability through
  `DOORS_LOC_REACHABLE` and the generated door x enemy-drop bridge

#### Scenario: Wild keys use generated source predicates
- **WHEN** wild small keys are shuffled and enemy-drop keys are active
- **THEN** generated enemy-drop locations participate in reachability through their
  generated source-region predicates and generated worst-case enemy-key gates

#### Scenario: Inactive enemy-drop ids do not affect reachability
- **WHEN** `enemy_drop_checks` is effectively `Off`
- **THEN** enemy-drop ids are skipped by active placement/tracker iteration, generated
  enemy-key gates are inert, and the Hyrule Castle virtual big-key side effect is
  not derived from enemy-drop reachability

### Requirement: Dungeon-enemy locations require sound reach predicates

An ordinary dungeon enemy source SHALL NOT be emitted into logic unless it has a conservative
reach predicate for the room or overworld screen that contains it and a
source-type kill route.

#### Scenario: Candidate lacks reachability coverage
- **WHEN** the static candidate audit finds a killable enemy source but logic
  cannot map it to a reachable region or room predicate
- **THEN** the source remains audit-only and is not a fillable location

#### Scenario: Dungeon room predicate coverage is incomplete
- **WHEN** the audit reports dungeon candidates in rooms without a reusable room
  predicate
- **THEN** codegen SHALL NOT emit those candidates until a reviewed predicate is
  added or the source is explicitly excluded

#### Scenario: Enemy source requires a kill route
- **WHEN** codegen emits an ordinary `Enemy` location
- **THEN** its reach predicate includes a source-type inventory kill predicate or
  a generated thrown-pot kill predicate in addition to room/key access

#### Scenario: Thrown-pot route is count based
- **WHEN** a source type requires multiple thrown-pot hits according to engine HP
  and damage tables
- **THEN** the thrown-pot route requires at least that many reachable pots in the
  room

### Requirement: All-enemy logic includes reachability and kill routes

Every emitted `enemy_drop_checks=all` location SHALL require both source reachability
and a reviewed kill route. Reachability SHALL use the correct dungeon room, reviewed
underworld cave/interior predicate, overworld area/screen, GT-miniboss arena, or
scripted-spawn parent predicate. Kill routes SHALL use the effective enemy type and
HP for the source.

Static overworld source reachability SHALL include the generated logic region, the
active overworld sprite-list stage, and a conservative kill route. Post-Agahnim
stage-2 rows SHALL remain gated on `DefeatAgahnim`; placement SHALL separately
prevent Agahnim-prerequisite item classes from landing there.

Reviewed underworld exceptions SHALL include a direct access predicate and SHALL
not inherit dungeon small-key depth terms unless the row also carries reviewed
key-depth metadata.

Thrown-object routes SHALL be allowed when engine damage data shows that the thrown
object can damage the source and the reachable area contains enough usable throwables
to deal lethal damage. If a source requires N thrown-pot hits, the route SHALL require
at least N reachable pots or equivalent throwables; one pot SHALL NOT satisfy a
two-pot kill. Generated thrown-pot branches SHALL additionally require effective pot
shuffle to be off. While any effective pot-sanity tier is active, those branches
SHALL be disabled and the enemy SHALL require its reviewed inventory-combat route.

#### Scenario: Inventory kill route
- **WHEN** an emitted all-enemy source has a reviewed inventory-based kill route
- **THEN** the location requires the source reach predicate and that combat predicate

#### Scenario: Counted thrown-pot kill route
- **WHEN** a killable source requires two thrown-pot hits and the reachable room has
  exactly two usable pots before the check is collected
- **THEN** logic MAY allow the enemy check through the thrown-pot route

#### Scenario: Insufficient thrown pots
- **WHEN** a killable source requires two thrown-pot hits and the reachable room has
  only one usable pot
- **THEN** logic SHALL NOT allow the enemy check through the thrown-pot route

#### Scenario: Required pot item cannot also be weapon
- **WHEN** a pot's placed item is required before an enemy check and lifting that pot
  consumes the throwable needed for the enemy kill route
- **THEN** effective pot sanity disables the thrown-pot branch for that enemy check
- **AND** the reviewed inventory-combat route remains required

#### Scenario: Independent pot item and weapon ordering
- **WHEN** an enemy's thrown-pot route uses pots that are not needed as prior
  pot-sanity item checks for that same branch
- **THEN** the current conservative model still disables that route while effective
  pot sanity is active
- **AND** a future per-source consumption model MAY safely restore the route

#### Scenario: Unknown kill route blocks honest all
- **WHEN** no conservative kill route can be modeled for a finite enemy source
- **THEN** the source SHALL not be emitted
- **AND** effective `All` SHALL downgrade visibly or reject until a reviewed
  predicate exists

### Requirement: All-enemy interaction logic is explicit

Door shuffle, pot shuffle, enemy shuffle, boss shuffle, and entrance shuffle SHALL
interact with `all` only through explicit generated predicates or explicit visible
normalization/rejection. Door shuffle composes through generated door x ordinary
enemy-check bridge rows and source predicates. Enemy shuffle normalizes requested
`All` to the highest lower tier allowed by existing derived rules, normally `Keys`
but `Off` when the keys tier is unsupported, until a future change explicitly makes
enemy shuffle placement-affecting for all-enemy kill logic and updates digest/corpus
expectations. Boss shuffle composes with `All` because dungeon bosses are excluded
and the emitted GT-miniboss checks are outside the shuffleable room set, unless
another rule lowers the effective tier further. Entrance shuffle normalizes requested `All`
to `Dungeon` until all-enemy overworld/domain reachability is modeled against the
entrance graph, unless another rule lowers the effective tier further.

#### Scenario: Door shuffle uses all-enemy bridge
- **WHEN** `enemy_drop_checks=all` is requested with door shuffle
- **THEN** derived settings keep `All`
- **AND** door reachability uses generated enemy-check bridge rows and source
  predicates instead of treating all-enemy locations as vanilla-door reachable

#### Scenario: Enemy shuffle normalizes all
- **WHEN** `enemy_drop_checks=all` is requested with enemy shuffle
- **THEN** derived settings normalize to the highest lower tier allowed by existing
  derived rules instead of using vanilla kill predicates for shuffled enemies

#### Scenario: Boss shuffle preserves boss-domain all
- **WHEN** `enemy_drop_checks=all` is requested with boss shuffle
- **THEN** derived settings keep `All` unless another rule lowers the effective tier
  further

#### Scenario: Entrance shuffle normalizes all
- **WHEN** `enemy_drop_checks=all` is requested with entrance shuffle before
  all-enemy entrance-graph reachability is modeled
- **THEN** derived settings normalize to `Dungeon` unless another rule lowers the
  effective tier further

### Requirement: Unified ring-aware small-key counts

The predicate VM and every direct small-key-count consumer SHALL use one effective
count model. Outside Retro, a held dungeon Key Ring SHALL saturate its family's
small-key count above every supported threshold; otherwise the ordinary count is
used. Retro's existing GenericKey collapse SHALL remain mutually exclusive. The
model SHALL cover all HAS operators, door oracle inputs/fingerprints, live
reachability, assumed fill, final spheres, and goal verification.
Generated/assumed inventories SHALL carry their ring item counts directly. Live
inventory construction SHALL materialize the derived owned-ring mask as one count
for each held `KeyRing_*`; numeric dungeon key counters alone SHALL NOT imply ring
ownership.

#### Scenario: Every HAS form sees the ring

- **WHEN** the inventory contains `KeyRing_PalaceOfDarkness` but no
  `SmallKey_PalaceOfDarkness`
- **THEN** HAS_ITEM, HAS_AMOUNT, HAS_ANY_OF, and HAS_ANY_COUNT expressions that
  reference Palace of Darkness keys evaluate using the saturated ring count

#### Scenario: Door cache includes ring ownership

- **WHEN** ring ownership changes for a door-shuffled dungeon
- **THEN** the door-oracle key fingerprint changes and cached reachability is not
  reused from the unowned state

#### Scenario: Live counts preserve the ring identity

- **WHEN** a collected ring is reconstructed as owned after load but its numeric
  key counter has subsequently been spent down
- **THEN** live counts still contain that ring item and use its saturated family
  count

#### Scenario: A ring does not waive non-key soul gates

- **WHEN** enemy souls are enabled and the inventory contains
  `KeyRing_HyruleCastleEscape` but not `Soul_Soldier`
- **THEN** Zelda's Cell and the Zelda rescue event remain unreachable in both
  generation and live tracker logic, and become reachable only after the Soldier
  soul is owned (subject to their other requirements)

### Requirement: Skeleton Key is absent from generation logic

Skeleton Key ownership SHALL not modify any predicate, count alias, door oracle,
reachability bit, sphere, accessibility result, or goal-completable result.

#### Scenario: Skeleton-neutral reachability

- **WHEN** otherwise equal inventories differ only in Skeleton Key ownership
- **THEN** their generation-logic reachability results are identical

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

