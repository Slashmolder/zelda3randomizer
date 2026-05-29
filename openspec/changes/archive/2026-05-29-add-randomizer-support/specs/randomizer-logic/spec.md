## ADDED Requirements

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

### Requirement: Per-seed RegionRemap overlay for entrance shuffle

`OP_REGION_REACHABLE` SHALL consult a runtime `RegionRemap[entrance_id] → interior_id` overlay before traversing static `EdgeDef[]`. Phase A's overlay is the identity map (no remapping); Phase C entrance shuffle swaps in a non-identity overlay. The VM and `logic_data.c` static graph are unchanged across phases; only the overlay swaps.

#### Scenario: Phase A identity overlay
- **WHEN** entrance shuffle is disabled (Phase A default)
- **THEN** `RegionRemap[e] == identity(e)` for every entrance and `OP_REGION_REACHABLE` traverses the static `EdgeDef[]` unchanged

#### Scenario: Phase C non-identity overlay
- **WHEN** entrance shuffle is enabled and assigns entrance E to interior I'
- **THEN** `OP_REGION_REACHABLE` evaluating against entrance E consults `RegionRemap[E] = I'` and uses I' as the traversal target, with the static edges from I' applied

#### Scenario: Append-only op-registry
- **WHEN** a Phase B feature adds `OP_TRICK` to the registry
- **THEN** the new op receives a new numeric ID at the end of the registry, no previously assigned ID changes, and `logic_data.c` regenerates with the new op available to YAML predicates

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
