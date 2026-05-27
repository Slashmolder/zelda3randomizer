## Status (2026-05-27 overnight push)

**Translation: 24 YAML files written.** All 13 dungeon PHP files + 5 LightWorld + 6 DarkWorld translated under `assets/rando/logic_parts/inverted/**`. ~2500 PHP lines → ~3500 YAML lines. Six background agents worked file-disjoint sets in parallel; each produced citation-grounded YAML with PHP file:line refs on every predicate.

**Integration: BLOCKED on world-state-aware predicate merge.** The codegen (`assets/rando_logic_gen.py:_merge_logic_doc`) currently does "last wins" merge per location. Loading the Inverted YAML overwrites Standard predicates for same-named locations, corrupting Standard placements (verified: corpus 50/50 FAILED with recursive glob enabled). The codegen revert to non-recursive glob restores the corpus to 50/50 OK — Inverted files sit on disk as reference until the world-state-aware merge lands.

**Required integration changes** (NOT in this commit):

1. `assets/rando_logic_gen.py::_merge_logic_doc` — replace `loc_preds[name] = pred` with `loc_preds[name][world_state] = pred`. Codegen emits per-world-state predicate tables; runtime VM picks predicate matching `settings.world_state` at reachability time.
2. `src/rando/rando_logic.c` predicate VM — consult `settings.world_state` when looking up location/region predicates.
3. Per-world-state region assignment — Ether Tablet (id 194) and Spectacle Rock (id 195) move from LightWorld_DeathMountain_West (Standard) to East (Inverted) per PHP `LightWorld/DeathMountain/East.php:25-26` vs `West.php:24-25`. Codegen needs to emit per-(loc, world_state) region maps OR move the branching into the predicate body.
4. Re-enable recursive glob in codegen.
5. Bytecode size — ~600 new predicates ≈ 2× can_reach table. Verify against `kPredicateBytes_*` budget.
6. `kGeneratorVersion` bump 10 → 11.
7. Un-gate Inverted in `src/select_file.c` settings-screen picker (currently capped at Standard at line 2509-2511).
8. Manual Inverted playthrough validation.
9. Fresh-eyes audit on translation correctness — every prior cluster audit found 5-10 NEW bugs; translation has high latent-bug surface per `cluster_audit_cadence` memory.

**Known translation flags from the agents**:
- `EasternPalace.yaml`: parent file (Standard) doesn't exist in `logic_parts/` (only worked-example in `logic_examples/`); Inverted EP relies on inherited locations from a future Standard EP file. Cannonball Chest + Map Chest not emitted.
- `HyruleCastleEscape.yaml`: `item_registry.yaml` has no `BigKey_HyruleCastleEscape` / `Compass_HyruleCastleEscape`; PHP `setFillRules` wild-flag bans dropped as vacuous.
- `LightWorld/DeathMountain/East.yaml`: Ether Tablet + Spectacle Rock world-state-conditional region assignment (see #3 above).
- All files drop Phase B branches (canBootsClip, canSuperSpeed, canOneFrameClipOW, canBunnyRevive, canOWYBA, canSuperBunny+MagicMirror, etc.) and document in header.

---

## Context

Phase A scaffolded Inverted-world plumbing without authoring its logic graph:
- `OP_WORLDSTATE_EQ kWorldState_Inverted` evaluates per `assets/rando/op_registry.yaml`.
- `kRandoStartRegionByWorldState[Inverted] = 0xFFFF` (placeholder).
- `Rando_SetRegionRemap` is callable but never invoked.
- `Rando_TryGrantStartingInventory` exists at `src/rando/rando_placement.c:1360` but has no production caller (Phase A1 Bug #12, still open).

Upstream ALTTPR's Inverted region files: 2977 lines of PHP across 24 files (13 top-level + 11 under `DarkWorld/` and `LightWorld/`). This change translates them to YAML predicates, activates RegionRemap with the Inverted overlay, and wires Bug #12's call site.

## Goals / Non-Goals

**Goals**:
- Inverted seeds generate end-to-end with goal-completability honoring the new logic graph.
- `kRandoStartRegionByWorldState[Inverted]` populates with `LinksHouse_Inverted`.
- RegionRemap activates for Inverted seeds; existing Open/Standard seeds unchanged.
- Bug #12 wired: Inverted Link starts with MoonPearl + MagicMirror in `link_item_*`.
- `world_state_filter` populated for Inverted-specific and Inverted-exclusion locations.
- Picker un-gate in `select_file.c` exposes Inverted to users.

**Non-Goals**:
- Retro world-state work (separate change `add-rando-retro-world-state`).
- Trick predicates for Inverted (deferred to `add-rando-trick-logic-and-axes` follow-on after #4a archives).
- Hint generation for Inverted-specific NPCs (Murahdahla is dark-world; lives in `add-rando-hints`).
- Entrance shuffle (Phase C `add-rando-entrance-shuffle`).

## Decisions

### D1: YAML directory layout — mirror PHP subdir or flatten?

**Options**:
- (a) **Mirror PHP layout**: `assets/rando/logic_parts/inverted/{<World>/{<DeathMountain/}<File>.yaml}`. Translation map is clean; codegen must scan recursively.
- (b) **Flatten with prefixes**: `inverted_<File>.yaml` at the top level. Codegen-compatible with Phase A's flat scan; loses upstream-mirror clarity.

**Decision**: **(a) Mirror PHP layout**. Cleaner translation-discipline audit trail; extend the codegen to recurse subdirs (small change to `assets/rando_logic_gen.py`). Phase B's priming directory `assets/rando/logic_parts/inverted/` already creates the mirror structure with stub files.

### D2: RegionRemap overlay shape

Phase A scaffolded `Rando_SetRegionRemap(uint16 overlay[NUM_REGIONS])`. The Inverted overlay swaps Light World ↔ Dark World region accessors so the same `LOC_<...>` location id resolves to the inverted topology at access time.

**Concrete overlay**: a `uint16[NUM_REGIONS]` table where `overlay[LightWorld_NorthEast] = DarkWorld_NorthEast` and vice versa, for every Light/Dark world pair. Regions with no Light/Dark counterpart (e.g., individual dungeons) get identity mapping `overlay[i] == i`.

**Decision recorded here**: the overlay's per-region pairing table is hand-authored in this change, sourced from ALTTPR's `app/World/Inverted.php` (verify at apply-time — file presence to be confirmed by grep). The codegen emits the overlay as a `static const uint16 kInvertedRegionRemap[NUM_REGIONS]`.

### D3: Bug #12 call site

`Rando_TryGrantStartingInventory` exists; needs production caller. Two candidates:
- (a) End of `Module05_LoadFile` (existing save-load entry point in `src/messaging.c` family).
- (b) Start of first `Module06_PreDungeon` (just before the first dungeon entry).

**Decision**: **(a) End of `Module05_LoadFile`**. Reasoning:
- `Module05_LoadFile` is the canonical "new game state initialized" hook; the starting-inventory grants belong here.
- `kRam_RandoStartingInventoryGranted` already gates against double-grant on reload (per Phase A spec); placing the call in Module05 still respects the idempotency contract.
- (b) would delay the grant by one screen transition; (a) is more user-visible (HUD shows starting items at start screen).

The call is gated on `kFeatures1_RandomizerActive && !kRam_RandoStartingInventoryGranted` so vanilla mode is unaffected and reload doesn't re-grant.

### D4: world_state_filter authoring

Phase A's `location_registry.yaml` carries `world_state_filter: 0` (universal) for all 237 locations. Inverted introduces two categories:

- **Inverted-only locations**: present only in Inverted (e.g., locations behind dark-world-start-specific routing). Per ALTTPR Inverted file analysis at apply-time.
- **Inverted-exclusion locations**: present in Open/Standard/Retro but NOT Inverted (some Light World entrances route differently when Link starts as bunny).

**Decision**: encode `world_state_filter` as a 4-bit mask (one bit per world-state). `0b0000` = universal (Phase A default for all 237 entries). Phase B Inverted authoring sets:
- Inverted-only: `0b1000` (Inverted bit set, others clear).
- Inverted-exclusion: `0b0111` (Open + Standard + Retro bits set, Inverted clear).

The codegen `world_state_filter == 0` already means "universal" so existing entries don't need editing; only the locations that materially differ get a non-zero filter.

### D5: Macro provenance — Inverted-specific macros

Phase A's `assets/rando/macros.yaml` has 30 macros sourced from `app/Support/ItemCollection.php`. Inverted may need additional macros (e.g., `canBeInvertedDarkWorld(items, world)` if ALTTPR has such a function). Per `audit.md §0.10` translation discipline:

**Decision**: Inverted-specific macros are appended to `macros.yaml` with `phase: B-inverted` tag and per-method source-line citation. The macro names mirror ALTTPR's PHP method names (snake_case → camelCase as needed). Standard macros remain unchanged.

### D6: Forward-fill fallback under Inverted

Phase A's placer has a forward-fill fallback when assumed-fill exhausts its budget (per `randomizer-core/spec.md:347`). Inverted's logic graph is more constrained than Open's (Link starts in dark world; pearl access is gated). Initial estimate: forward-fill fallback may fire more often for Inverted seeds.

**Decision**: do NOT pre-emptively widen the assumed-fill budget for Inverted. Let the existing budget surface fallback warnings naturally; if corpus runs show high fallback rates, tune at apply-time.

## Risks / Trade-offs

| Risk | Mitigation |
|---|---|
| Translation errors yield un-completable Inverted seeds | Per-macro source-line citation; fresh-eyes audit post-translation (memory [[cluster-audit-cadence]] — every audit finds 5-10 NEW bugs); forward-fill fallback degrades rather than crashes |
| RegionRemap overlay shape disagrees with predicate-VM expectations | D2 declares the overlay shape explicitly; well-formedness pass in `assets/rando_logic_gen.py` catches mismatches |
| Bug #12 call site fires on save-load (double-grant) | `kRam_RandoStartingInventoryGranted` already gates this per Phase A spec (`randomizer-placement / Starting inventory injection`) |
| Inverted YAML breaks Open/Standard digests | `OP_WORLDSTATE_EQ` predicates short-circuit when world_state != Inverted; existing seeds remain byte-identical; CI corpus verifies |
| ALTTPR upstream PHP changes during translation | Pin the upstream commit hash in `audit.md §"Inverted macro provenance"`; translation references the frozen reference (Phase B risk R1) |

## Migration Plan

This is a Phase B change; no migration of existing user data is required. Existing Open/Standard slots remain valid because:
- The Inverted overlay activates only when `settings.world_state == Inverted`.
- Existing slots have `world_state != Inverted` in their stored settings.
- Cross-version load (Phase A → Phase B) honors the embedded placement table per `randomizer-save / Embedded placement table — upgrade safety`.

After archive, `select_file.c` settings-screen picker exposes Inverted; users who select it generate against the new logic graph.

## Open Questions

1. Does `../alttp_vt_randomizer/app/World/Inverted.php` exist? If yes, it contains the RegionRemap pairing table source. Verify at apply-time. (Phase B audit confirmed `app/World/Retro.php` exists; symmetry suggests `Inverted.php` does too, but verify.)
2. Does Inverted require any non-vanilla items added to the registry, or does it reuse Open's item pool? Initial assumption: same pool. Confirm during translation.
3. Forward-fill rate under Inverted — if elevated, widen budget OR tighten predicates. Apply-time empirical question.
