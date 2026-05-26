## 0. Phase 0 — Audit and scope decisions (prerequisite, gates all later work)

- [ ] 0.1 Enumerate every write to `link_item_*`, `link_bottle_info[*]`, `link_has_crystals`, `sram_progress_*`, heart-piece/heart-container counters, and the special-case dispatch tables in `misc.c` (`kMemoryLocationToGiveItemTo`, `kValueToGiveItemTo`, the `if (j == ...)` chain in `ItemReceipt_*`). Output: `audit.md` table with file/line per site.
- [ ] 0.2 Classify each entry in `audit.md` as one of: `grant` (rando dispatches), `state-shuffle` (e.g., wishing-pond store/restore — exempt), `cosmetic` (HUD redraw — exempt), or `progress` (event flag, possibly exempt — case by case).
- [ ] 0.3 For every `grant` entry, assign the ALTTPR-equivalent location ID and record it in `audit.md`. Cross-check that every ALTTPR canonical location (~216 entries) is covered by at least one grant site; flag missing coverage.
- [ ] 0.4 Enumerate the item types that need a *new* receive code path in `audit.md`: progressive items (`ProgressiveSword/Shield/Armor/Glove/Bow` — each grant advances the corresponding `link_item_*` by one level via a new helper), small key, big key, map, compass, multi-tier rupees `Rupee1/5/20/100/300`, `Rupoor`, bottle-with-contents (`BottleEmpty/WithFairy/WithBee/WithGoodBee/WithRedPotion/WithGreenPotion/WithBluePotion`), prize items (`Prize_Crystal1..7`, `Prize_GreenPendant/RedPendant/BluePendant`). List existing receivable items confirmed safe to dispatch.
- [ ] 0.4a Enumerate "Reachability-affecting events" in `audit.md`: every progression event-flag write site the logic graph will reference. At minimum: Aga 1 defeat, every dungeon-boss-cleared flag, NPC-satisfied flags (sahasrahla, sick kid, magic-bat / mushroom→powder), pyramid-opened, master-sword-pulled, king's tomb item taken. Each entry records file/line and the one-line `Rando_BumpReachabilityCounter()` patch.
- [ ] 0.5 Document Phase A logic scope in `audit.md` as three independent axes pinned for Phase A: `tricks = none`; `item_pool_difficulty ∈ {easy, normal (default), hard, expert}` (all four supported in Phase A); `logic = NoGlitches`. List trick predicates and Phase B+ glitch-logic-level hooks with their `OP_*` reservations.
- [ ] 0.5a Document Phase A goal set in `audit.md`: Defeat Ganon, Fast Ganon, All Dungeons, Pedestal, Triforce Hunt, Ganon Hunt, Completionist. Document the dual-crystal-setting (`crystals.ganon`, `crystals.tower`) policy.
- [ ] 0.5b Document Phase A shuffle set in `audit.md`: item shuffle, dungeon-item modes (per item class: Vanilla/Dungeon/Wild), **prize shuffle**, **medallion shuffle**. Boss/entrance/drop-pool/cosmetic shuffles deferred.
- [ ] 0.6 Confirm save-storage model (sidecar `sram_rando.dat`, 3 slots paired to `sram.dat`'s 3 slots, no 4th slot anywhere) and snapshot-tail format (appended after the `SaveSnesState` dump per [zelda_rtl.c:533](src/zelda_rtl.c:533); ClearKeyLog forced before save) in `audit.md`.
- [ ] 0.7 Pin exact `kRam_*` offsets. Scan every `g_ram+0x...` reference in `src/*.c`, `src/variables.h`, `src/features.h`. Confirm that 0x659-0x65e (the proposed 6-byte block) is unused by any macro. Record the scan output in `audit.md`. Note that 0x670-0x67F is occupied by `spotlight_*` and SHALL NOT be used.
- [ ] 0.8 Acceptance criteria for Phase 0 (a reviewer can tick these off to declare Phase 0 done):
  - [ ] 0.8a `audit.md` lists ≥ 1 grant site per canonical ALTTPR location; coverage table present.
  - [ ] 0.8b Reverse audit: every `link_item_*` write in `src/*.c` appears as an entry (grant or explicit exemption).
  - [ ] 0.8c New-receive-path items enumerated with target dispatcher entry points.
  - [ ] 0.8d Save model + snapshot model + kRam_* offsets all signed off in `audit.md`.
  - [ ] 0.8e Second-reviewer sign-off comment recorded in `audit.md`.
- [ ] 0.9 **Phase 0 audit is a code-review-blocking gate**: the `audit.md` deliverable with all 0.8a-e checks ticked SHALL exist on master before any section 6 (grant-site integration) task begins. Reviewers SHALL refuse any §6 PR opened before `audit.md` lands.
- [ ] 0.10 **Logic-graph source decision (A0 gate — provenance/effort call, not a legal blocker)**: this should be settled before A1 begins to avoid wasted work. Both this repository and ALTTPR (`alttp_vt_randomizer`, MIT — verified in its LICENSE file and `composer.json "license": "MIT"`) are MIT-licensed; there is no license incompatibility in any option.

  **The actual ALTTPR logic location** (verified by reading the source): `alttp_vt_randomizer/app/Region/{Standard,Open,Inverted}/*.php`, ~4,000 lines of PHP across the three world-states. Each region's `initalize()` method wires per-location predicates as PHP closures:

  ```php
  $this->locations["Eastern Palace - Boss"]->setRequirements(function ($locations, $items) {
      return $items->canShootArrows($this->world)
          && ($items->has('Lamp', ...) || ($items->has('FireRod') && ...))
          && $items->has('BigKeyP1')
          && $this->boss->canBeat($items, $locations);
  });
  ```

  Plus `setFillRules` / `setAlwaysAllow` for placement restrictions and `$this->can_enter` / `$this->can_complete` for region-level predicates. The named macros (`canShootArrows`, `canKillMostThings`, `canGetGoodBee`, `hasBottle`, `canExtendMagic`, `canBlockLasers`, …) are public methods on `app/Support/ItemCollection.php` — 43 of them in total, already factored out by the upstream authors. There is no separate `z3-randomizer-logic` repository; the logic IS these PHP region files.

  Three options:
  - **(a) Clean-room reimplementation** from natural-language behavior descriptions. Slowest; cleanest provenance; no attribution.
  - **(b) Adopt some other community-maintained logic file as a starting point** — only viable if such a file exists with a verified MIT-or-compatible license. (Earlier drafts speculatively named "z3-randomizer-logic" — that repo's existence is unverified; do not assume.)
  - **(c) Hand-translate the ALTTPR PHP closures into our YAML predicates.** MIT-derivative; requires preserving ALTTPR's copyright notice in our `LICENSE`/`NOTICE`. The 43 `ItemCollection` methods become our named-macro set directly; the ~4,000 lines of region PHP become per-location predicates in `logic.yaml` mechanically. **Recommended baseline** because it maps the cleanest to our spec — the predicate-to-PHP-source-line audit table doubles as documentation and as the upstream-drift detector for future logic changes.
  
  Decision and rationale recorded in `audit.md` §"Logic source provenance"; choice drives task 3.3 (logic.yaml authoring) and task 13.9 (attribution if (b) or (c)).

- [ ] 0.11 **Owner assignment (A0 gate)**: Phase A has ~6 concurrent workstreams (audit, CI scaffolding, RNG/share-string, logic-YAML authoring, predicate VM, save sidecar, UI, text-input). Many touch shared C files (`dungeon.c`, `sprite.c`, `messaging.c` — every grant-site dispatch lives here). Without owner assignment merges become contentious. Decide and record in `audit.md`:
  - Solo developer vs. multi-dev. If multi-dev, owners per workstream.
  - **Merge-order rule for §6 grant-site PRs**: serialize through a single integrator OR partition by file (e.g., one owner for `dungeon.c` dispatches, one for `sprite.c`/`sprite_main.c`, one for `messaging.c`). The audit's grant-site table is the partitioning input.
  - **Backup reviewer for logic-translation** (named human, per design.md Risks bus-mitigation).

## 1. Foundation (Phase A0)

### 1.0 CI scaffolding (lands together; gates all later work)

The CI matrix needed to support the rest of Phase A is a multi-week deliverable in itself. Carving it out so it doesn't get squeezed alongside A1's logic-translation work. Each task notes whether it lands **runnable-and-content-driving** in A0 or **scaffolded-but-no-op-until-content** in A0 (becoming live when the referenced later task lands):

- [ ] 1.0a Regression-corpus CI step (Linux, macOS, Windows runners; Switch manual per 12.3a). **Scaffold in A0; runnable but no-op** until task 12.2 fills the corpus. Acceptance for A0: the job runs, parses an empty manifest, exits zero.
- [ ] 1.0b Determinism-grep CI step (forbidden symbols per `randomizer-core / Determinism constraints`). **Runnable in A0** (it scans `src/rando/*.c`; empty dir initially passes; first real source files trigger it).
- [ ] 1.0c Byte-order grep CI step (forbidden `htobe*`/`be*toh` per `randomizer-core / Byte-order pin`). **Runnable in A0** (same shape as 1.0b).
- [ ] 1.0d Init-order replay CI step (per-chapter savestates against vanilla baseline per task 1.2). **Runnable in A0** — the savestates already exist in `saves/ref/`; this runs from the moment task 1.1 lands `kRam_*` cells.
- [ ] 1.0e kGeneratorVersion PR-gate CI rule (per task 13.6). **Runnable in A0** the moment `kGeneratorVersion` is declared (task 1.1). Touches the listed paths trigger the gate.
- [ ] 1.0f Link-time symbol blocklist CI step (per task 1.7 layer 2). **Runnable in A0** for any `src/rando/*.o` that gets built (initially none; first real file triggers it).
- [ ] 1.0g Audit-guard CI step (rejects new `link_item_*` writes outside dispatch per task 6.10). **Scaffold in A0; activates** once `audit.md` lands at end of Phase 0 — until then the guard has no exemption list to consult. Acceptance for A0: the script exists and dry-runs cleanly; it becomes blocking when 0.8e ticks.
- [ ] 1.0h Benchmark CI gates (`Logic_ComputeReachability` 5ms desktop / 20ms manual on Switch per task 3.11). **Scaffold in A0; activates** when `Logic_ComputeReachability` is implemented in A1 (task 3.8).

### 1.x Foundation tasks proper

- [ ] 1.1 Add `kFeatures1_*` enum and `enhanced_features1` macro in `src/features.h`. Add the minimal `kRam_*` block (6 bytes, fully inside the verified-free 0x659-0x66f range): `kRam_Features1` (uint32, 4 bytes at 0x659), `kRam_RandoSlotActive` (1 byte at 0x65d), `kRam_RandoStartingInventoryGranted` (1 byte at 0x65e). Final offsets re-confirmed by Phase 0 task 0.7 before this task starts. Settings hash, share string, settings struct, and placement table live in heap in `src/rando/`, not in `g_ram`. Initialize all new cells in `ZeldaInit` before any game code reads them.
- [ ] 1.1a Compute `g_assets_hash` (the SHA-256 of the loaded asset blob, 32 bytes) once after `LoadAssets()` returns. Store as a global byte array. Code that needs the vanilla-check compares directly: `memcmp(g_assets_hash, kVanillaAssetsHash, 32) == 0`. (An earlier draft introduced a separate `g_assets_are_vanilla` bool; reconciled to just the hash — the bool was redundant and a stale-name risk.)
- [ ] 1.1b Wire `kVanillaAssetsHash` generation: extend `assets/restool.py` (or add a small post-build step in the asset pipeline) to emit `src/rando/vanilla_assets_hash.h` containing the SHA-256 of the produced `zelda3_assets.dat` as a `static const uint8 kVanillaAssetsHash[32]`. The header is regenerated whenever the asset pipeline runs, so the constant stays in lockstep with the bundled assets. Add the header to `.gitignore` (build output) and document a CI check that fails if it would change without an accompanying pipeline change.
- [ ] 1.2 Add a CI step that replays the per-chapter savestates in `saves/ref/` with the new binary in vanilla mode and asserts byte-identity at every newly-added `kRam_*` offset (D7 init-order guard).
- [ ] 1.3 Create `src/rando/` directory and stub headers: `rando.h`, `rando_rng.h`, `rando_logic.h`, `rando_placement.h`, `rando_share.h`, `rando_spoiler.h`, `rando_save.h`, `rando_textfield.h`.
- [ ] 1.4 Add `src/rando/*.c` to the Makefile glob, `Zelda3.sln`/`.vcxproj`, and `src/platform/switch/Makefile`.
- [ ] 1.5 Vendor a single-file SHA-256 implementation under `third_party/sha256/` and wire its build entry across Make, MSVC, Switch make.
- [ ] 1.6 Add `[randomizer]` section to `src/config.c` INI parser: default settings, spoiler-dir, tracker-toggle keybinding, race-mode default. Document keys in `README.md`.
- [ ] 1.6a Implement CLI / headless generation mode in `main.c`. Single-seed form: `--generate-seed --settings=k=v,... --seed=<u64> --out-spoiler=<path> [--out-share-string=<path>] [--budget-seconds=<n>] [--assets-must-be-vanilla]`. **Batch form**: `--generate-seed --manifest=<yaml> [--budget-seconds=<n>] [--out-dir=<path>]` reads a YAML manifest of N (settings, seed_u64) pairs and writes N spoilers in one process invocation. Both forms run without opening the SDL window or running game frames. The regression-corpus CI job uses the batch form (1 binary launch per platform, not 50).

  **Critical implementation detail**: `--generate-seed` SHALL detect the flag during `argv` parsing **before** any `SDL_Init` call and either skip SDL initialization entirely OR call only `SDL_Init(SDL_INIT_TIMER)`. SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) MUST NOT run in CLI mode — a "headless" CLI that still creates a window works on dev machines with X11 forwarding but breaks CI on display-less runners. Add a smoke test: `./zelda3 --generate-seed --settings=... --seed=... --out-spoiler=/tmp/x.json` succeeds with `DISPLAY=` unset on a Linux runner.

- [ ] 1.6b Implement `assets/scripts/check_codegen_wiring.py`: enumerates the set of generated files (`src/rando/logic_data.c`, `src/rando/location_ids.h`, `src/rando/item_ids.h`, `src/rando/vanilla_assets_hash.h`) and asserts the same set appears in `Makefile`, `Zelda3.vcxproj` pre-build steps, and `src/platform/switch/Makefile`. CI runs this as a guard against the recurring "added a generated file to Make but forgot MSVC" failure mode in multi-build-system projects.
- [ ] 1.7 Add the determinism-constraints check. Two layers:
  - Source-level: a grep step that rejects `rand`, `random`, `arc4random`, `time(`, `clock_gettime`, `float `, `double ` in `src/rando/*.c` and `src/rando/*.h`.
  - Link-level: a `nm`-based check on the compiled `src/rando/*.o` that asserts no undefined references to `rand`, `random`, `arc4random`, `time`, `clock_gettime`. This catches indirect calls via helpers or macro splicing that source grep would miss.

## 2. RNG and share string

- [ ] 2.1 Implement xoshiro256** in `src/rando/rando_rng.c` with `Rng_SeedFromU64`, `Rng_NextU32`, `Rng_NextRange`. No `rand()`/`time()` references.
- [ ] 2.2 Add property test: identical seeds produce identical 10k-sample sequences across Linux/macOS/Windows/Switch in CI.
- [ ] 2.3 Implement share-string encoder/decoder in `src/rando/rando_share.c` (4-byte magic, base32 alphabet, 16-bit checksum, `(magic | version | settings_hash | seed_u64 | checksum)` layout).
- [ ] 2.4 Round-trip unit test plus explicit-reject tests for: alttpr.com-format hashes, corrupted base32 chars, wrong-length input, wrong magic prefix.
- [ ] 2.5 Define `RandoSettings` struct and canonical serialization. Compute `settings_hash = SHA-256(canonical(settings))`.

## 3. Logic graph (must precede item-pool work in section 4)

- [ ] 3.1 Create `assets/rando/location_registry.yaml` reserving stable numeric IDs for every Phase A location.
- [ ] 3.2 Create `assets/rando/item_registry.yaml` reserving IDs for every Phase A item: progressive items (`ProgressiveSword/Shield/Armor/Glove/Bow`), all absolute vanilla `link_item_*` items, **`SilverArrowUpgrade`** (absolute-bow mode), **`TriforcePiece`** (Triforce Hunt / Ganon Hunt), **`HalfMagic` / `QuarterMagic`**, bottle-with-contents (`BottleEmpty/WithFairy/WithBee/WithGoodBee/WithRedPotion/WithGreenPotion/WithBluePotion`), **`PieceOfHeart` (quarters mechanic)** and **`BossHeartContainer` (direct +1 max HP)** as distinct IDs, small/big keys per dungeon, maps per dungeon, compasses per dungeon, multi-tier rupees (`Rupee1/5/20/100/300`), junk pool (`SmallMagic/Arrow1/Arrow10/Bombs1/Bombs3/Bombs10`), `Rupoor` (only enters pool when `item_pool_difficulty ∈ {hard, expert}`), prize items (`Prize_Crystal1..7/GreenPendant/RedPendant/BluePendant`). **Virtual items** (no grant path, no dispatcher entry, but assigned IDs so codegen emits `ITEM_*` literals and predicates can reference them): **`StartingHeart`** (placer pre-populates `counts[StartingHeart] = 3` per `randomizer-core / Simulated-inventory model`; Phase B hero-mode would override this constant).
- [ ] 3.3 Author `assets/rando/logic.yaml` describing all locations, regions, edges, and predicates for Open + Standard + Inverted + Retro under Phase A pinned logic axes (`tricks=none`, `logic=NoGlitches`; `item_pool_difficulty` does NOT alter the logic graph — it only changes pool composition). Include named predicate macros (e.g., `CanReachAgahnimTower`, `CanKillMostThings`, `CanDamageBoss`).
- [ ] 3.4 Reserve `OP_*` numeric assignments in `assets/rando/op_registry.yaml`. All **15 Phase A ops**: `OP_HAS_ITEM`, `OP_HAS_AMOUNT`, `OP_HAS_ANY_OF`, **`OP_HAS_ANY_COUNT`**, `OP_WORLDSTATE_EQ`, `OP_GOAL_EQ`, `OP_GOAL_REQUIRES_DUNGEON`, `OP_DUNGEON_CLEARED`, `OP_REGION_REACHABLE`, **`OP_HAS_PRIZE`**, **`OP_MEDALLION_OPENS`**, **`OP_ITEM_IS`** (round-7 addition for `can_place` predicates — evaluates against the candidate-item register, not the inventory counts), `OP_AND`, `OP_OR`, `OP_NOT`. Phase B placeholders: `OP_TRICK`, `OP_DIFFICULTY_AT_LEAST`, `OP_GLITCH_LEVEL_AT_LEAST`.
- [ ] 3.4a Author named predicate macros in `assets/rando/logic.yaml`, sourced from the 43 public methods on `alttp_vt_randomizer/app/Support/ItemCollection.php` (verified by reading the source — these are the macros ALTTPR factored out, e.g., `canShootArrows(world, min_level)`, `canKillMostThings(world, enemies)`, `canGetGoodBee()`, `hasBottle(at_least)`, `canExtendMagic(world, bars)`, `canBlockLasers()`, `hasSword(min_level)`, `hasArmor(min_level)`, etc.). Translation discipline: each macro in our YAML pairs with the line range of the corresponding PHP method in `audit.md` §"Macro provenance". Macros are referenced by every location/edge predicate; raw OP-chains in predicate bodies are flagged at codegen time by the inline-complexity check.
- [ ] 3.5 Write `assets/rando_logic_gen.py` — parses the YAML, validates references, emits THREE generated artifacts:
  - `src/rando/logic_data.c` — `LocationDef[]`, `RegionDef[]`, `EdgeDef[]`, bytecode predicate streams.
  - `src/rando/location_ids.h` — `#define LOC_<DungeonOrRegion>_<Name> <numeric_id>` for every location in `location_registry.yaml`. Dispatch call sites in `dungeon.c`/`sprite.c`/etc. use the LOC_* literals directly (`Rando_OnLocationCheck(LOC_HyruleCastle_BoomerangChest, ITEM_Boomerang)`).
  - `src/rando/item_ids.h` — `#define ITEM_<Name> <numeric_id>` for every item in `item_registry.yaml`, used by both dispatch call sites and the placer.
- [ ] 3.5a Schema-first authoring: before any `logic.yaml` content is written (task 3.3), the schema `assets/rando/logic.schema.yaml` and 3-5 worked examples under `assets/rando/logic_examples/` are checked in and reviewed (per `randomizer-logic / Logic YAML schema`).
- [ ] 3.6 Wire codegen into the build system: Makefile dependency `src/rando/logic_data.c: assets/rando/*.yaml`; MSVC pre-build step; Switch makefile parity; `assets/restool.py` --rando-logic switch.
- [ ] 3.7 Implement the Phase A predicate VM in `src/rando/rando_logic.c` covering all **15 Phase A ops**: HAS_ITEM, HAS_AMOUNT, HAS_ANY_OF, **HAS_ANY_COUNT** (total count across union of IDs ≥ n), WORLDSTATE_EQ, GOAL_EQ, GOAL_REQUIRES_DUNGEON, DUNGEON_CLEARED, REGION_REACHABLE (memoized **per fixed-point iteration**; consults `RegionRemap` overlay), HAS_PRIZE, MEDALLION_OPENS, **ITEM_IS** (used by `can_place`; evaluates the candidate-item register), AND, OR, NOT. **Two evaluation entry points**: `Predicate_Evaluate(p, counts, settings)` for `can_reach`; `Predicate_EvaluatePlacement(p, counts, settings, candidate_item)` for `can_place` — the latter is the only context where `OP_ITEM_IS` may appear (well-formedness check rejects `OP_ITEM_IS` outside `can_place`).
- [ ] 3.7a Implement `RegionRemap[entrance_id] → interior_id` runtime overlay. Phase A initializes it to identity (no remapping); Phase C entrance shuffle swaps in a non-identity map. `OP_REGION_REACHABLE` consults the overlay before traversing static `EdgeDef[]`.
- [ ] 3.8 Implement `Logic_ComputeReachability(inventory, settings)` with iterative fixed-point expansion, sorted iteration order, memoized `OP_REGION_REACHABLE`.
- [ ] 3.9 Implement Phase A goal-completion predicates: Defeat Ganon, Fast Ganon (uses `crystals.ganon` AND `crystals.tower` independently), All Dungeons, Pedestal, Triforce Hunt (pedestal-end), **Ganon Hunt** (Triforce + Ganon defeat), **Completionist** (every location reachable under accumulated inventory).
- [ ] 3.10 Logic-graph well-formedness validation: build-time pass asserts referenced items/locations exist, no orphan locations, every location reachable under full-inventory mock, no `OP_REGION_REACHABLE` cycles outside memoization.
- [ ] 3.11 Benchmark `Logic_ComputeReachability`: median under 5 ms on reference desktop and under 20 ms on Switch across 1000 invocations on the full graph. Failing benchmarks fail CI.

## 4. Item pool and placement

- [ ] 4.1 Implement `BuildItemPool(settings) -> Item[]` covering: base pool (with `mode.weapons` toggle selecting progressive vs absolute weapon items per `Randomizer.php:183-198`), item-pool-difficulty replacements (full table — silvers, mirror shield, all bottles, etc.), per-class dungeon-item mode (small keys, big keys, maps, compasses × {Vanilla, Dungeon, Wild}), glove/sword counts, Triforce-Hunt and Ganon-Hunt piece padding, Rupoor injection in hard/expert pools. Assert `|pool| == |locations|` after junk-padding.
- [ ] 4.1a Implement prize-shuffle module: assigns each of {Crystal 1..7, Green/Red/Blue Pendant} to a dungeon. Default-on in Phase A. Writes the assignment to the spoiler.
- [ ] 4.1b Implement medallion-shuffle module: assigns each of {Misery Mire entrance, Turtle Rock entrance} to {Bombos, Ether, Quake}. Independent of prize shuffle. Default-on in Phase A.
- [ ] 4.2 Implement starting-inventory injection per world-state. Gate via `kRam_RandoStartingInventoryGranted` so injection runs exactly once at new-game init.
- [ ] 4.3 Implement assumed fill in `src/rando/rando_placement.c` with bounded-retry rewind and a forward-fill fallback after the 5-second budget. Sorted iteration over location candidates.
- [ ] 4.4 Compute `placement_digest = SHA-256(canonical_serialize(placement_table))` for regression-corpus diffing.
- [ ] 4.5 Cross-platform determinism property test: same `(generator_version, settings, seed_u64)` → identical placement on Linux, macOS, Windows, Switch in CI.

## 5. Spoiler log

- [ ] 5.1 Implement JSON spoiler writer in `src/rando/rando_spoiler.c` with stable field names: `share_string`, `generator_version`, `settings`, `placements[]`, `sphere_data[]`, `goal_completable`, `generation_wall_clock_ms`, `fallback_warnings[]`.
- [ ] 5.2 Implement text spoiler writer grouped by region for human readability.
- [ ] 5.3 (Phase B) Race-mode stamp (share string + SHA-256 of full spoiler) and a `RevealSpoiler` action that regenerates from the share string and verifies the stamp.
- [ ] 5.4 Wire `[randomizer] spoiler_dir` INI key. Default `<exe-dir>/spoilers/<share-string>.{json,txt}`. The spoiler path is **derived at runtime** from `spoiler_dir + slot.share_string` and is NOT stored in the slot (deliberate; keeps slots invariant under config changes).

## 6. Placement dispatch (grant-site integration)

- [ ] 6.1 Implement `Rando_OnLocationCheck(location_id, vanilla_item_id)` in `src/rando/rando_placement.c`. Resolves placement, dispatches via the standard receive path (or new path for new item types), falls back to `vanilla_item_id` on unknown location ID.
- [ ] 6.2 Add new receive paths from `audit.md` section 0.4: small-key receive (counter increment + new animation), big-key receive, map receive, compass receive, single-rupee placeholder (silent increment), bottle-substitute (route via bottle-insertion).
- [ ] 6.3 Implement dispatch calls at every chest open path in `dungeon.c` per `audit.md` entries (dungeon chest, big-key chest, dungeon-prize chest, plus overworld and cave chests across the codebase).
- [ ] 6.4 Implement dispatch at NPC gift sites in `sprite.c`/`sprite_main.c`/`messaging.c` per `audit.md`: uncle, sahasrahla, magic bat, library, hobo, sick kid, bottle merchant, dwarf brothers, smith bros initial, smith bros tempering, witch, bombo merchant, bee merchant, mushroom→powder.
- [ ] 6.5 Implement dispatch at static-pickup sites per `audit.md`: master sword pedestal, ether tablet, bombos tablet, pyramid plaque, sanctuary chest.
- [ ] 6.6 Implement dispatch at dungeon-prize grants per `audit.md`. **Each boss kill grants TWO distinct rando locations**, each with its own location ID and dispatch call: `<Dungeon>_BossHeart` (the boss-heart drop — defaults to BossHeartContainer when `region.bossHeartsInPool=false` in Phase A) AND `<Dungeon>_Prize` (the crystal/pendant via prize shuffle). The boss-death code path in `dungeon.c` SHALL call `Rando_OnLocationCheck` twice. Preserve the dungeon→reward binding (the reward stays with the dungeon, not with the boss) for Phase B boss shuffle.
- [ ] 6.7 Implement dispatch at event-flag grants affecting inventory per `audit.md`: king zora flippers, zora's lake flippers. **Pyramid Fairy is a SYNTHESIZED multi-slot grant site — net-new code, not a hook over existing single-grant code.** Vanilla LttP (the source this port mirrors) has Pyramid Fairy as a fixed-item shrine (toss in the tempered sword for an upgrade, toss in the bow for silver arrows). ALTTPR exposes this as **two trade locations** that are always present (`pyramid_fairy_sword`, `pyramid_fairy_bow` per `app/Region/Standard/DarkWorld/NorthEast.php:34-35`) and **two additional chest locations** (`pyramid_fairy_left`, `pyramid_fairy_right`) added conditionally based on world config (lines 40-41). §6.7 SHALL add a new code path inside the fairy-event handler that, when `kFeatures1_RandomizerActive` is set, calls `Rando_OnLocationCheck` once for each of the active slots (2 or 4 depending on config), and skips the vanilla fixed-item shrine; when rando is inactive, the vanilla shrine runs unchanged. The audit classifies these as "synthesized" sites (distinct from "grant" instrumentation) and pins the per-slot `vanilla_item_id` fall-back (`L1Sword` for sword slot, `Bow` for bow slot, per `NorthEast.php:59-60`).

  Note: an earlier draft of this task referenced "Fat Fairy" as a separate two-slot grant site — that location does not exist by that name in ALTTPR (searched; zero matches). The Pyramid Fairy 2-or-4 slot pattern above is the only multi-slot fairy grant.
- [ ] 6.8 Implement dispatch at minigame sites per `audit.md`: digging game, treasure chest minigame, hype cave, peg cave.
- [ ] 6.9 Wrap every dispatch in `if (enhanced_features1 & kFeatures1_RandomizerActive)`. Run the per-chapter savestate corpus and assert vanilla `g_ram` byte-identity vs. pre-change binary.
- [ ] 6.10 Add the build-time/test-time audit guard: a script that scans for new writes to `link_item_*`/etc. outside dispatch or without the documented exemption comment, failing the build on violations.

## 7. Optional shuffle modules (deferred)

- [ ] 7.1 (Phase B) Implement boss shuffle (`src/rando/shuffle_boss.c`) — randomize boss-room assignments from configurable pool, keep dungeon→reward binding stable, preserve goal-required bosses.
- [ ] 7.2 (Phase B) Implement drop-pool shuffle with the heart-drop-survives-early-game guarantee; runs after item placement so spheres are known.
- [ ] 7.3 (Phase C) Implement entrance shuffle (`src/rando/shuffle_entrance.c`) — Simple/Restricted/Crossed/Insanity modes; goal-reachability check after shuffle.
- [ ] 7.4 (Phase D) Implement palette and sprite cosmetic shuffles driven by a separate `cosmetic_seed`; assert zero placement-table change vs. the same seed without cosmetic shuffle.
- [ ] 7.5 (Phase B) Add `OP_TRICK`, `OP_DIFFICULTY_AT_LEAST`, `OP_GLITCH_LEVEL_AT_LEAST` handlers in the predicate VM and the corresponding YAML predicates.

## 8. Save format (sidecar) and atomic commit

- [ ] 8.1 Define `RandoSidecarFile` and `RandoSlotHeader` in `src/rando/rando_save.c` matching `randomizer-save` spec's authoritative layout exactly:
  - File header (16 bytes): `magic[4]`, `format_version` (uint16 LE, starts at 1), `slot_count` (uint16 LE), `file_crc` (uint32 LE), `reserved[2]`.
  - Slot header (80 bytes; offsets and widths per the authoritative table in `randomizer-save / Sidecar slot contents`): `magic[4]` @0, `slot_kind` (uint8) @4, `generator_version` (uint16 LE) @5, `settings_hash[16]` @7, `share_string[32]` (raw binary) @23, `last_vanilla_write_version` (uint16 LE) @55, `sram_slot_checksum_at_last_write` (uint32 LE) @57, **`placement_table_size` (uint16 LE) @61 — REQUIRED for cross-version forward compatibility per `randomizer-save / Embedded placement table — upgrade safety`**, `flags` (uint8) @63, `reserved[16]` @64. Total = 80 bytes.
  - Embedded placement table: `placement_table_size` bytes; Phase A baseline is `~212 × uint16 LE = ~424 bytes` (varies by world-state per `randomizer-save / Sidecar slot contents`). Item-ID `0xFFFF` SHALL be reserved as the "no placement / deprecated location" sentinel.
  - Checked-location bitmap: `(placement_table_size / 2 + 7) >> 3` bytes; iterated only over `[0, placement_table_size / 2)` bits per `randomizer-save / Checked-location bitmap read invariant`.
  - `spoiler_path` is intentionally **omitted** from the slot — the path is runtime-derived from `[randomizer] spoiler_dir` config + the slot's `share_string`. This is a deliberate design decision (not an oversight); see design.md §D6 final paragraph.
  - All multi-byte fields SHALL be little-endian on disk (per `randomizer-save / Determinism: endianness pin`).
- [ ] 8.2 Implement read/write paths for `saves/sram_rando.dat` with the atomic-commit protocol from D12: write `sram_rando.dat.tmp`, `fflush`, `fsync`/`_commit`, `rename`, fsync containing directory (POSIX). Same for `sram.dat.tmp`. Save order: sidecar first, then `sram.dat`. Reuse the existing `rename(... .bak)` pattern as the fall-back recovery target.
- [ ] 8.3 On rando-save load: parse sidecar slot, populate `kRam_RandoSlotActive`, install embedded placement table. No regeneration required.
- [ ] 8.4 On rando-save write: serialize slot header (including `last_vanilla_write_version = current generator_version` and `sram_slot_checksum_at_last_write` = checksum of the paired `sram.dat` slot snapshotted at the moment of write) + embedded placement table + checked-location bitmap. Atomic commit per 8.2.
- [ ] 8.5 Cross-version load test: a slot written by `generator_version = N` loads on a binary with `generator_version = N+1` and surfaces a one-time informational warning.
- [ ] 8.6 Downgrade-then-re-upgrade drift detection: write slot on v1.1, switch binary to v1.0 (manual), save, switch back to v1.1, load slot; assert the checksum-drift warning fires and the recovery prompt offers (continue with embedded / convert to vanilla).
- [ ] 8.7 Coexistence test: directory with mixed vanilla (no sidecar) and rando (with sidecar) loads correctly. Renaming `sram_rando.dat` away reverts all slots to vanilla appearance.
- [ ] 8.8 Snapshot integration: implement tail-TLV chain save in `StateRecorder_Save` extension — force `StateRecorder_ClearKeyLog` before save so `base_snapshot` is populated; after the existing 4-chunk write, append one or more TLV entries (`magic[8] + type[4] + length[4] + payload`). Phase A emits a single `TAIL_RANDO_STATE` TLV (payload = generator_version + settings_hash + share_string + placement_table_size + placement_table); fsync. Implement load: after the existing `assert(state.p == state.pend)`, iterate TLV entries — read magic/type/length, dispatch on known types, seek past unknown types, terminate on EOF. **Add a code comment at the post-assert exit point** in `StateRecorder_Load` flagging that any future change adding trailing data here must remain backward-compatible with the rando TLV chain (point to design.md §D11).
- [ ] 8.8a Implement the StateRecorder_Load **ordering invariant**: `LoadSnesState` restores g_ram (including `kRam_RandoSlotActive`) and the TLV reinstall MUST execute in the same call before any game frame can run. Add an explanatory code comment and a debug-build assertion that no `Rando_OnLocationCheck` fires between these two steps.
- [ ] 8.9 Snapshot replay test: take a rando snapshot mid-run, exit, relaunch, `Ctrl+F1`-replay; assert dispatch fires correctly during replay because base_snapshot was forced.
- [ ] 8.10 Older-binary snapshot test: take a rando snapshot on the new binary; open with an older binary (or a build with `RandoSnapshotTail` disabled); confirm graceful degradation — snapshot loads as vanilla, tail bytes ignored.

## 9. UI: text input, file-select, settings

- [ ] 9.1a Implement `RandoTextField` widget in `src/rando/rando_textfield.c` — single-line buffer, cursor, backspace, paste-from-clipboard, base32 constraint.
- [ ] 9.1b Implement SDL_TEXTINPUT routing in `main.c`: register handler when settings screen is active, route chars into the active text field.
- [ ] 9.1c Implement libnx software-keyboard wrapper on the Switch build (typically `swkbdCreate` / `swkbdShow` / `swkbdInputText` per libnx headers; verify exact API surface against the installed libnx version at implementation time), route result into the widget buffer.
- [ ] 9.2 Implement the on-screen alphabet picker (controller-friendly). D-pad navigation, A=add char, B=delete last. Default focus on Switch handheld.
- [ ] 9.3a Implement per-slot kind dispatch in `select_file.c`: read each slot's `slot_kind` from the sidecar; render vanilla banner, rando banner, or "NEW GAME" prompt accordingly. Preserve the existing `kSelectFile_Draw_Y[3]` geometry — do NOT add a 4th entry (no SRAM room; see D8).
- [ ] 9.3b On empty-slot "NEW GAME", show a sub-prompt with three options: Vanilla / New Randomizer / Load Share String. Sub-prompt uses the existing menu styling.
- [ ] 9.3c Implement Copy refusal for cross-kind copies (vanilla↔rando) with a clear error message.
- [ ] 9.4 Implement settings screen rendered via existing menu styling: world-state, goal (all 7 Phase A goals), `crystals.ganon` and `crystals.tower` independent sliders, item-pool difficulty, per-class dungeon-item modes (4 × {Vanilla, Dungeon, Wild}), Triforce-Hunt and Ganon-Hunt piece counts when applicable, prize-shuffle toggle (default-on), medallion-shuffle toggle (default-on), Phase-B-and-beyond shuffles labelled and disabled, race-mode toggle, presets, seed-entry field, live settings hash. Asset-warn dialog: on first generation attempt with non-vanilla `g_assets_hash` and no persisted decision, show the three-choice dialog and honor it.
- [ ] 9.4a Implement the "Recommended for randomizer" panel: list relevant `kFeatures0_*` toggles (fast text, skip intro on keypress, max items in yellow, etc.) initialized to user's current `zelda3.ini` values, plus an "Apply recommendations" button. Starting a slot does not change settings without explicit opt-in.
- [ ] 9.4b Implement 5-icon visual hash widget. Author `assets/rando/icon_atlas.yaml` listing the icon set (curated, not constrained to 32 entries — pin whatever pool the YAML defines). Codegen `kHashIconAtlas[N]` (8×8 tile coordinates into the existing menu-font tile region) from the YAML. The widget computes `index_i = SHA-256(share_string_binary)[i] mod N` for `i ∈ {0..4}` and emits 5 OAM tiles. **Critical: the hash input is `share_string_binary`, NOT `settings_hash` — deriving from settings_hash would give every seed with the same settings the same icons (architectural error caught in round 5 review).** Renders on both the file-select slot banner and the in-game pause menu HUD.
- [ ] 9.5 Implement preset application (Open Ganon, Standard Ganon, Inverted Ganon, Retro, Triforce Hunt Default).
- [ ] 9.6 Share-string paste path: decode via `rando_share.c`, populate settings, surface inline error on invalid/alttpr.com format.
- [ ] 9.7 Implement rando-slot banner on file-select: truncated 12-char share string, world-state abbrev, goal abbrev, "R" badge. Assert no OAM overflow.
- [ ] 9.8 Wire "Generate" action: invoke generator, write spoiler, write sidecar slot, transition to new-game flow.

## 10. UI: in-game trackers (Phase B)

- [ ] 10.1 Implement item-tracker overlay in `hud.c`: fixed-grid layout, inventory-change-triggered re-render only.
- [ ] 10.2 Implement location-tracker overlay: grouped by region, reachable/unreachable/checked coloring driven by cached `Logic_ComputeReachability` (recomputed only on inventory change).
- [ ] 10.3 Add tracker-toggle keybindings to `config.c` `kKeys_*`. Document in `README.md`.
- [ ] 10.4 Ensure overlays render correctly under all renderer backends: SDL software, SDL hardware, OpenGL, OpenGL ES, Switch.
- [ ] 10.5 Persist checked-location bitmap in the sidecar slot per `randomizer-save`.

## 11. RAM-compare and vanilla safety

- [ ] 11.1 In `zelda_cpu_infra.c`, short-circuit the RAM-compare frame check when `kFeatures1_RandomizerActive` is set, UNLESS the dev-only INI override `[randomizer] debug_force_ram_compare = true` is set. The override lets developers attach the original ROM and observe RAM divergences during dispatcher work ("is the dispatcher actually firing on this chest?"). The override is **not** documented in `README.md`'s user-facing key map — it's developer-only. When the override is in effect with a rando slot active, expect frame diffs to spew constantly; this is intentional and used for targeted investigation.
- [ ] 11.2 Replay the per-chapter savestates in `saves/ref/` under the new binary in vanilla mode; assert no diff against pre-change binary at any RAM offset.
- [ ] 11.3 CI matrix: vanilla-mode and rando-mode startup smoke tests; vanilla mode also passes RAM-compare against the attached original ROM (where present).

## 12. Regression corpus

- [ ] 12.1 Build `tests/rando_corpus/` harness: YAML manifest of (settings, seed_u64) → expected placement-table SHA-256 digest pairs; manifest records the `generator_version` it was generated against.
- [ ] 12.2 Populate the corpus with 50 seeds across the world-state × goal × item-pool-difficulty matrix. Include at least 5 **named celebrity seeds** (e.g., "all-bosses-12-min", "swordless-skip-mire", a Triforce-Hunt with `pieces_required = 25 / pieces_placed = 30`, a Standard Ganon Hunt, a Completionist Open) so diagnosing future divergences is easier than scanning unnamed digests. Initial generation runs on the chosen generator version and is the source of truth (not ALTTPR).
- [ ] 12.3 CI step: regenerate every corpus seed on every supported platform; diff against expected digest; fail loudly on any divergence.
- [ ] 12.3a Switch corpus is a **manual gate**, not CI-blocking. DevKitPro CI is unusual and unreliable; instead, before each release tag, the corpus is regenerated manually on a Switch dev unit and diffed locally. CI proper covers Linux + macOS + Windows automatically.

- [ ] 12.3b **Switch BUILD verification** is also a **manual gate** (distinct from the corpus gate). Most cloud runners don't have DevKitPro + `switch-sdl2`; adding a DevKitPro container to CI is out of scope for this change (it would be a separate infrastructure project). Instead: each release-candidate PR triggers a manual Switch-build check by the named Switch-build owner (see task 0.11), who runs `cd src/platform/switch && make` on a dev machine with DevKitPro installed and reports success/failure on the PR. Switch build breakage discovered between releases is filed as a release blocker. Document the build-check procedure (toolchain version, libnx version, SDL2 version) in `docs/randomizer.md` so any contributor with DevKitPro can run it.
- [ ] 12.4 When divergence is found, file investigation notes inline in the corpus directory rather than masking the diff.
- [ ] 12.5 Bump-corpus tool: when `generator_version` advances, a script regenerates the corpus, the manifest's generator-version field is updated, and the commit message references the change that justified the bump.

## 13. Documentation

- [ ] 13.1 Add `docs/randomizer.md`: getting started, settings reference, share-string format, save-pin behavior, race-mode flow (Phase B), troubleshooting (BPS conflict, version drift warning, sidecar atomicity).
- [ ] 13.2 Update top-level `README.md` with a randomizer section.
- [ ] 13.3 Document the `[randomizer]` INI section and tracker hotkeys in the existing key-binding section of `README.md`.
- [ ] 13.4 Document the audit comment convention for grant-site exemptions in `audit.md` and in `docs/randomizer.md`.
- [ ] 13.5 Document explicit non-cross-compatibility with alttpr.com share strings (both directions).
- [ ] 13.6 Document the `generator_version` bump policy in `docs/randomizer.md` AND enforce it as a **CI rule**. The CI gate SHALL fail any PR that modifies any of the listed paths without bumping `kGeneratorVersion`. Bump triggers:
  - `src/rando/rando_logic.c` (predicate VM)
  - `src/rando/rando_placement.c` (placement algorithm)
  - `src/rando/rando_rng.c` (RNG)
  - `assets/rando/logic.yaml` (logic graph)
  - `assets/rando/item_registry.yaml` (item pool / registry)
  - `assets/rando/location_registry.yaml` (location registry — append-only adds advance the count, which is part of the determinism input)
  - `assets/rando/op_registry.yaml` (op-code assignments)
  - `assets/rando/icon_atlas.yaml` (5-icon hash output changes when atlas changes)
  - The `RandoSettings` struct or its canonical serialization order (per `randomizer-core / Settings canonical serialization order`)

  **Important clarification on append-only location-registry bumps**: bumping advances the version and **regenerates the regression corpus** for new seeds, but it does NOT invalidate existing saves. The embedded placement table (per `randomizer-save / Embedded placement table — upgrade safety`) lets older slots load on newer binaries via informational warning. The corpus-regen is the cost; the save side is unaffected.
  
  Document the accompanying regression-corpus regeneration in the same doc.
- [ ] 13.7 Document Phase B+ roadmap items so the audience knows what's planned (not promised). **Phase B explicit**: hint generation system (Sahasrahla telepathic tiles, storyteller text, bookshelves, Murahdahla for Triforce Hunt — per ALTTPR's `app/Services/HintService.php`, ~1000 lines of placement-aware text generation; casual players consider hints integral to the experience and Triforce Hunt is almost unplayable without them); trick logic + glitch-logic-level predicates; `swordless` weapon mode; `pyramid_bow_upgrade=arrows`; `accessibility=none`; race-mode reveal; in-game trackers; boss/drop-pool shuffles. **Phase C**: entrance shuffle (uses RegionRemap overlay). **Phase D**: cosmetic shuffles, customizer mode (uses dispatcher API unchanged), major-glitch logic, auto-tracker server.
- [ ] 13.9 Document the logic-source provenance and any attribution obligations in `docs/randomizer.md`, plus update this repo's `LICENSE` / add a `NOTICE` file if option (b) or (c) from task 0.10 is chosen. ALTTPR's MIT license requires preserving its copyright notice in derivative works — straightforward attribution, not a viral copyleft constraint.

- [ ] 13.10 Add a **spec-drift prevention rule** to the project's PR template (or contributor docs): *"Any code change that contradicts an OpenSpec scenario in `openspec/changes/add-randomizer-support/specs/` MUST include a spec amendment in the same PR. Reviewers SHALL refuse PRs where implementation drifts silently from spec."* Without this rule, spec docs decay into aspirational fiction within weeks of implementation start. Optionally add a CI lint that diffs the changed `src/rando/*` files against listed spec scenarios and posts a "did you mean to update spec X?" comment — best-effort, not blocking.
- [ ] 13.8 Document the uncle's-sword note from `audit.md`: Standard mode in ALTTPR makes the uncle's gift part of the placement pool (it can be any item, not just a sword), and Link starts swordless until the chosen item is collected. Spec the receive path used and the starting-inventory interaction.

## 14. Release gating (Phase A is sub-phased A0 / A0.5 / A1 / A2)

- [ ] 14.1a Phase A0 acceptance (foundation): Phase 0 audit done (all 0.8 checks ticked); RNG, share string, SHA-256, asset-hash warn dialog wired (not yet user-visible), feature flag plumbing, init-order CI guard all green; `Rando_OnLocationCheck` dispatcher implemented with one wired demo grant site; CLI generation mode runs end-to-end against a hardcoded settings struct; vanilla RAM-compare still clean; dev-only flag (no UI) flips rando mode and grants a hardcoded substitute item from the demo site.
- [ ] 14.1a.5 Phase A0.5 acceptance (vertical-slice demo): one world state (Open), one goal (Fast Ganon defaults), the **pinned 20-site set** below, hardcoded C-side logic for those sites, no YAML codegen yet, real sidecar save/load with atomic-commit protocol, real snapshot tail-TLV including replay mode, file-select kind-toggle rendering, asset-hash warn dialog user-tested, recommended-features panel, 5-icon hash banner. Acceptance: playable demo from settings screen → boss-kill with every integration path exercised at least once. **Resolve the file-select Copy/Erase placement open question** during A0.5 review (currently listed in design.md Open Questions). Documented learnings feed into A1/A2 scope. **A0.5 lives on a feature branch (`phase-a0.5-vertical-slice`)**, NOT on master with a `#ifdef PHASE_A05_DEMO` guard — the guard approach would double CI matrix (demo-on / demo-off builds) for weeks of negligible value. The feature branch merges into master only after A1 lands and the throwaway code is replaced by real YAML-driven logic; the branch is then deleted.

  **Pinned A0.5 grant-site set (20 sites)** — chosen for full integration-surface coverage with minimal logic complexity:
  1. Uncle (Link's House interior) — Open mode skips this in logic terms but the dispatcher entry exercises NPC-gift code path
  2. Sanctuary chest — early dungeon-prize path
  3. Master Sword Pedestal — static-pickup with goal-relevant logic
  4. Eastern Palace × 6: Compass Chest, Big Chest, Cannonball Chest, Big Key Chest, Map Chest, the chest after Armos (exercises dungeon-item modes for the 6 chest types and dungeon-prize binding)
  5. Eastern Palace BossHeart slot (identity-placed per Phase A bossHeartsInPool policy — exercises the dispatch even when identity)
  6. Eastern Palace Prize slot (exercises prize-shuffle dispatch — Phase A defaults randomize this)
  7. Sahasrahla NPC gift (gated on `OP_HAS_PRIZE Prize_GreenPendant`, exercises shuffle-aware predicate)
  8. Magic Bat (exercises mushroom→powder turn-in path, an event-flag site)
  9. Sick Kid in Bug-Catching Kid's house (NPC bottle-required gift, exercises `HAS_ANY_OF Bottle*`)
  10. Library (boots-dash check, exercises `HAS_ITEM Boots`)
  11. Hobo bottle (overworld bottle grant)
  12. Bottle Merchant (early-game NPC gift)
  13. King Zora flippers (event-flag site)
  14. Zora's Lake (event-flag site)
  15. Mire Shed L (cave chest pair example)
  16. Mire Shed R (pair completion)
  17. Smith Brothers initial rescue (NPC tagalong)
  18. Smith Brothers tempering (sword-upgrade NPC follow-up — exercises ProgressiveSword grant)
  19. PyramidFairy_Slot1 (synthesized two-slot grant — exercises new rando-only code path)
  20. PyramidFairy_Slot2 (synthesized two-slot grant)
  
  This set exercises: chest dispatch, NPC gift dispatch, static pickup, event-flag dispatch, dungeon-prize boss dispatch, boss-heart identity placement, multi-site (Eastern Palace), `HAS_PRIZE` predicate, `HAS_ANY_OF` predicate, prize shuffle, progressive-item grant, synthesized two-slot dispatch. ~20 sites was the right cardinality.
- [ ] 14.1b Phase A1 acceptance (logic + placement): `logic.yaml` covers all Phase A locations across 4 world-states; predicate VM passes the **per-registry full op-suite test** (all Phase A ops per `assets/rando/op_registry.yaml` — currently 15 ops including HAS_ANY_COUNT, HAS_PRIZE, MEDALLION_OPENS, and the round-7 addition ITEM_IS); reachability median time green on reference and Switch; goal predicates green for all 7 Phase A goals; assumed fill with forward-fill fallback passes the 50-seed regression corpus diff-clean across Linux/macOS/Windows (Switch corpus is a manual gate, not CI-blocking — see task 12.3a); spoiler writer schema-validates against the ALTTPR-mirrored meta block; `randomizer-core` and `randomizer-logic` spec scenarios green.
- [ ] 14.1c Phase A2 acceptance (save + UI + integration): sidecar atomic-commit protocol passes crash-recovery test; file-select kind-toggle renders all three kinds; text-input infrastructure + libnx swkbd + on-screen alphabet picker all functional; snapshot tail integration including replay mode passes; all remaining grant sites wired per audit; full Phase A spec scenarios green; vanilla RAM-compare still clean; manual end-to-end playthrough from settings screen → Ganon's Tower entry produces no dispatch warnings.
- [ ] 14.1 Overall Phase A acceptance: A0 + A1 + A2 all ticked.
- [ ] 14.2 Phase B acceptance: trick + difficulty VM ops, boss shuffle, drop-pool shuffle, in-game trackers, race-mode spoiler suppression with reveal.
- [ ] 14.3 Phase C acceptance: entrance shuffle (Simple/Restricted/Crossed/Insanity) with goal-reachability preserved.
- [ ] 14.4 Phase D acceptance: cosmetic shuffles, major-glitch logic level.
- [ ] 14.5 Run `openspec archive add-randomizer-support` after Phase A ships. Phases B-D each become their own follow-on OpenSpec changes referencing this one.
