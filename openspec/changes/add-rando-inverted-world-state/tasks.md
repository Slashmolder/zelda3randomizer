## 1. Apply-time pre-flight

- [x] 1.1 Verify `../alttp_vt_randomizer/app/World/Inverted.php` exists; if so, capture line range for RegionRemap pairing table source. If missing, derive the pairing table from `app/Region/Inverted/` collectively. Open question per design.md. — EXISTS (53 lines); region-registry/pairing source at lines 28-55. See audit.md §1.1.
- [x] 1.2 Pin upstream commit hash. Run `git -C ../alttp_vt_randomizer rev-parse HEAD` and record in `audit.md §"Inverted macro provenance"` so translation references a frozen reference. — `219fcafd029dab597b8db400efafd8f56f8b4edb`.
- [x] 1.3 Re-verify Inverted PHP file count + line count: `find ../alttp_vt_randomizer/app/Region/Inverted -name "*.php" | wc -l` should yield 24; `find ... -name "*.php" -exec wc -l {} +` should yield 2977. — 24 files / 2977 lines, both match.
- [x] 1.4 Skim each of the 24 PHP files for `setRequirements` / `setFillRules` / `setAlwaysAllow` / `$this->can_enter` / `$this->can_complete` patterns; count predicates per file; estimate translation effort. — per-file table + effort note in audit.md §1.4.

## 2. Inverted-specific macros

- [x] 2.1 Grep `../alttp_vt_randomizer/app/Region/Inverted/**/*.php` for `$items->can*` and `$this->world->can*` method calls. Cross-reference against Phase A's existing 30 macros in `assets/rando/macros.yaml`. — 12 `$items->can*` macros used; 0 `$this->world->can*`; ALL 12 already in macros.yaml. See audit.md §2.1.
- [x] 2.2 For any macro NOT in Phase A's set: author it in `macros.yaml` with `phase: B-inverted` tag. Record PHP source-line range. — NONE missing; no change to macros.yaml; no kGeneratorVersion bump warranted on macro grounds. See audit.md §2.2.
- [x] 2.3 Re-run `assets/rando_logic_gen.py` to confirm the macro additions parse cleanly. — exit 0, `warnings: 0, macro errors: 0`, generated output byte-identical (working tree clean).

## 3. Codegen pipeline — recurse subdirs

- [x] 3.1 Update `assets/rando_logic_gen.py` to scan `assets/rando/logic_parts/inverted/` and its subdirectories (per design.md D1).
- [x] 3.2 Add a path filter so Phase A's existing flat-directory scan is preserved; only `inverted/` recurses. Avoid accidentally picking up future world-state subdirs that aren't yet wired.
- [x] 3.3 Confirm `assets/scripts/check_codegen_wiring.py` passes — generated headers (`logic_data.c`, `location_ids.h`, `item_ids.h`) are unchanged in name; just their content expands. — exit 0, "6 generated file(s) wired across all build systems"; header names unchanged.

## 4. Translate Inverted YAML (the bulk of the work)

- [x] 4.1 Translate `app/Region/Inverted/HyruleCastleEscape.php` → `assets/rando/logic_parts/inverted/HyruleCastleEscape.yaml`. Use the template in `assets/rando/logic_parts/inverted/README.md`. Cite source lines per predicate.
- [x] 4.2 Translate `HyruleCastleTower.php` → `HyruleCastleTower.yaml`. Especially careful here — Agahnim 1 routing differs in Inverted.
- [x] 4.3 Translate `EasternPalace.php` → `EasternPalace.yaml`.
- [x] 4.4 Translate `DesertPalace.php` → `DesertPalace.yaml`.
- [x] 4.5 Translate `TowerOfHera.php` → `TowerOfHera.yaml`.
- [x] 4.6 Translate `PalaceOfDarkness.php` → `PalaceOfDarkness.yaml`.
- [x] 4.7 Translate `SwampPalace.php` → `SwampPalace.yaml`.
- [x] 4.8 Translate `SkullWoods.php` → `SkullWoods.yaml`.
- [x] 4.9 Translate `ThievesTown.php` → `ThievesTown.yaml`.
- [x] 4.10 Translate `IcePalace.php` → `IcePalace.yaml`.
- [x] 4.11 Translate `MiseryMire.php` → `MiseryMire.yaml`.
- [x] 4.12 Translate `TurtleRock.php` → `TurtleRock.yaml`.
- [x] 4.13 Translate `GanonsTower.php` → `GanonsTower.yaml`. Agahnim 2 routing differs.
- [x] 4.14 Translate `LightWorld/NorthEast.php` → `LightWorld/NorthEast.yaml`.
- [x] 4.15 Translate `LightWorld/NorthWest.php`.
- [x] 4.16 Translate `LightWorld/South.php`.
- [x] 4.17 Translate `LightWorld/DeathMountain/East.php`.
- [x] 4.18 Translate `LightWorld/DeathMountain/West.php`.
- [x] 4.19 Translate `DarkWorld/Mire.php`.
- [x] 4.20 Translate `DarkWorld/NorthEast.php`.
- [x] 4.21 Translate `DarkWorld/NorthWest.php`.
- [x] 4.22 Translate `DarkWorld/South.php`.
- [x] 4.23 Translate `DarkWorld/DeathMountain/East.php`.
- [x] 4.24 Translate `DarkWorld/DeathMountain/West.php`.

## 5. world_state_filter authoring

- [x] 5.1 Grep ALTTPR for Inverted-specific locations (locations that only exist when Link starts as bunny / routes through dark world first). Tag in `assets/rando/location_registry.yaml` with `world_state_filter: 0b1000`.
- [x] 5.2 Identify Inverted-exclusion locations (Light World locations unreachable in Inverted). Tag with `world_state_filter: 0b0111`.
- [x] 5.3 Verify no existing Open/Standard location's filter changes — only Inverted-specific entries get non-zero filters. Existing Phase A locations remain `world_state_filter: 0`.

## 6. RegionRemap overlay table

- [x] 6.1 Author the Inverted overlay table per design.md D2. Hand-translate from `app/World/Inverted.php` (if present) or derive from per-region PHP files.
- [x] 6.2 Emit the overlay as `static const uint16 kInvertedRegionRemap[NUM_REGIONS]` in a new generated file (or extend `src/rando/logic_data.c`).
- [x] 6.3 Wire `Rando_SetRegionRemap(kInvertedRegionRemap)` at generation start when `settings.world_state == Inverted`.
- [x] 6.4 Confirm Open/Standard seeds do NOT call `Rando_SetRegionRemap` (identity is default; overlay is opt-in).

## 7. Start region

- [x] 7.1 Populate `kRandoStartRegionByWorldState[Inverted] = LinksHouse_Inverted` in `src/rando/rando_logic.c`. Declare `LinksHouse_Inverted` as a peer to the existing `LinksHouse` region (or share the same region with the RegionRemap overlay routing the Inverted player to dark-world tiles).
- [x] 7.2 Verify the `Logic_ComputeReachability` initial seed advances from `LinksHouse_Inverted` correctly under the overlay.

## 8. Bug #12 starting-inventory call site

- [x] 8.1 Add `Rando_TryGrantStartingInventory()` call at end of `Module05_LoadFile` per design.md D3.
- [x] 8.2 Gate the call on `kFeatures1_RandomizerActive && !kRam_RandoStartingInventoryGranted` — already documented in spec, but verify the gate is correctly placed.
- [x] 8.3 Implement the Inverted-specific starting-inventory list inside `Rando_TryGrantStartingInventory`: grant `MoonPearl` + `MagicMirror` when `settings.world_state == Inverted`.
- [x] 8.4 Open mode and Retro: starting-inventory list is empty (no additional grants beyond Phase A defaults). Standard mode: also empty (uncle's-gift handling is separate).
- [x] 8.5 Set `kRam_RandoStartingInventoryGranted = 1` after the grant. Confirm save-reload idempotency: `kRam_RandoStartingInventoryGranted` persists via Phase A's `kRam_*` block.
- [x] 8.6 Add a `// rando-exempt: state-shuffle — bunny-state starting inventory` comment at the write site (per memory `[[audit-guard-exempt-placement]]` discipline).
- [x] 8.7 Run `assets/scripts/check_audit_guard.py` — no new audit-guard failures. — exit 0, "no non-exempt writes (38 tracked offsets)"; `[advisory]` indirect-dispatch notes are pre-existing informational output, not failures.

## 9. Picker un-gate

- [x] 9.1 Locate the `kRow_WorldState` case in `src/select_file.c:2520-2527`. Phase A capped cycle at Standard.
- [x] 9.2 Update the cycle to include Inverted: `Open → Standard → Inverted → Retro → Open` (assuming Retro un-gate ships in parallel) OR `Open → Standard → Inverted → Open` (if Inverted ships first).
- [x] 9.3 Update the stale comment that references "ALTTPR `Region/Inverted/*.php` ~1500 lines" — correct to "2977 lines, 24 files recursive" per audit findings.

## 10. CI + corpus

- [x] 10.1 Bump `kGeneratorVersion` in `src/rando/rando.h`.
- [x] 10.2 Run `assets/scripts/bump_rando_corpus.py` to regenerate `tests/rando_corpus/manifest.yaml`. Add at least 6 Inverted corpus seeds covering: Fast Ganon, All Dungeons, Triforce Hunt, Completionist × item_pool=normal + hard.
- [x] 10.3 Verify Open + Standard + Retro digests remain byte-identical to pre-Inverted-change baseline. — corpus all 55 entries byte-identical (Open/Standard/Retro/Inverted), 2026-05-29.
- [ ] 10.4 Cross-platform determinism: run the new Inverted corpus on Linux + macOS + Windows; all digests byte-identical.

## 11. Spoiler integration

- [x] 11.1 Update `Spoiler_WriteText` to recognize Inverted region grouping (regions in Inverted are reorganized — verify the grouping algorithm handles the overlay).
- [x] 11.2 JSON spoiler: confirm `regions` section reflects the Inverted overlay's region names; `placements` entries reference Inverted-specific location names correctly.

## 12. Audit + cluster-audit cadence

- [x] 12.1 Run `Rando_RunAllSelfChecks` — all selftests must pass post-Inverted YAML authoring. — `--rando-selftest` exit 0; Rando_RunAllSelfChecks all subsystems OK.
- [ ] 12.2 Schedule a **fresh-eyes audit** post-translation per memory `[[cluster-audit-cadence]]`. Every audit on this project finds 5-10 NEW bugs including ≥ 1 HIGH. Treat as workflow, not optional polish.
- [ ] 12.3 Address audit findings before archive.

## 13. Documentation

- [x] 13.1 Update `docs/randomizer.md` Phase A1 status note — remove "Inverted world-state seeds report ~32 unreachable until LinksHouse_Inverted region is declared (Phase A2 follow-on)" caveat.
- [x] 13.2 Cross-link this change from the `openspec/changes/` index (README.md).
- [x] 13.3 Add a "Inverted world-state" subsection to `docs/randomizer.md` explaining bunny-state start, MoonPearl+MagicMirror starting inventory, and the dark-world-first progression. — added under "World-state notes".

## 13.5. Performance budget verification

- [x] 13.5.1 **Generation budget bench (early)**: BEFORE locking in YAML authoring scope (i.e., after §4.1-4.5 land but before §4.6+), generate a Phase A default-settings seed AND a representative Inverted seed; measure wall-clock against Phase A's `randomizer-core / Generation performance budget` SHALL (2s desktop, 5s Switch). — Inverted p50=50ms (see audit.md Headless verification).
- [x] 13.5.2 If Inverted exceeds the budget by >2x on desktop: pause translation work and tune. Options: simplify predicate density (fewer per-location predicates); widen `--budget-seconds`; surface a SHALL relaxation in this change's spec. A generation-time budget regression is treated as a real regression: a budget-exceeding slice must be tuned before it ships. — not triggered: Inverted max 633ms << 2000ms desktop budget.
- [x] 13.5.3 Re-bench after the full translation lands (§4.24). Record p50 / p95 / p99 in `audit.md §"Inverted generation benchmark"`. — recorded p50=50ms/p95=622ms/max=633ms in audit.md.
- [ ] 13.5.4 Switch dev-unit bench is a release-cut gate (per `tasks.md §12.3a`); record manually after the desktop bench is green.

## 14. Playtest

- [ ] 14.1 Generate an Inverted Fast Ganon seed; verify Link starts with MoonPearl + MagicMirror.
- [ ] 14.2 Play to Ganon's Tower entry; verify the path through Inverted's routing.
- [ ] 14.3 Save/load mid-run; verify `kRam_RandoStartingInventoryGranted` doesn't re-grant.
- [x] 14.4 Verify Open seed regression: generate a known-good Phase A Open seed post-Inverted-change; confirm `placement_digest_hex` matches the pre-change baseline. — Open corpus entries byte-identical to pre-change baseline.

## 15. Archive readiness

- [ ] 15.1 CI green on Linux + macOS + Windows; Inverted corpus matches across platforms.
- [ ] 15.2 Fresh-eyes audit findings all addressed.
- [ ] 15.3 Manual playtest confirms Inverted seeds are completable end-to-end.
- [ ] 15.4 `openspec archive add-rando-inverted-world-state` runs cleanly; spec deltas merge into `openspec/specs/randomizer-{logic,placement,ui}/spec.md`.
