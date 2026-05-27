## 1. Op-code handlers

- [ ] 1.1 Implement `OP_TRICK trick_id` handler in `src/rando/rando_logic.c`. Looks up `settings.tricks` bitmask; returns true iff bit at position `trick_id` is set.
- [ ] 1.2 Implement `OP_DIFFICULTY_AT_LEAST threshold` handler. Returns `settings.item_pool_difficulty >= threshold` per the enum ordering (`easy=0`, `normal=1`, `hard=2`, `expert=3`).
- [ ] 1.3 Implement `OP_GLITCH_LEVEL_AT_LEAST threshold` handler. Returns `settings.logic >= threshold` per the enum ordering (`NoGlitches=0`, `OverworldGlitches=1`, `MajorGlitches=2`). Phase D extends to higher thresholds; this change handles 0-2.
- [ ] 1.4 Add the 3 handlers to `Logic_SelfCheck` with positive + negative test cases.

## 2. Trick bitmask un-pin + CSV

- [ ] 2.1 In `src/rando/rando_settings.h`, confirm `tricks` is uint8 (Phase A pinned to 0). Phase B leaves the type unchanged; just allows user input.
- [ ] 2.2 Update `src/rando/rando_settings.c` CSV parser to accept `tricks=boots-clip,fake-flippers,...` syntax. Resolve trick names against the `tricks:` table in `assets/rando/op_registry.yaml` (loaded by `assets/rando_logic_gen.py` and emitted into a runtime lookup table).
- [ ] 2.3 Unknown trick names are an error: CLI exits non-zero with a clear message naming the unknown trick.
- [ ] 2.4 Settings-screen widget: add a multi-select trick toggle UI (Phase A's settings screen supports per-axis enums; tricks is multi-select).

## 3. Logic level un-pin + CSV

- [ ] 3.1 Un-pin `logic` in `rando_settings.h:88`. Accept `OverworldGlitches=1` and `MajorGlitches=2` from user input. Reject `HybridMG=3` and `NoLogic=4` with a "deferred to Phase D" message.
- [ ] 3.2 CSV parser: accept `logic=overworld_glitches | major_glitches` (snake_case).
- [ ] 3.3 Settings-screen widget: cycle the field through the 3 supported values.

## 4. mode_weapons un-pin (swordless)

- [ ] 4.1 Un-pin `mode_weapons` in `rando_settings.h` (Phase A pinned to randomized/assured). Accept `swordless=3`. Still reject `vanilla=2` (out of Phase B scope).
- [ ] 4.2 CSV parser: accept `mode.weapons=swordless`.
- [ ] 4.3 Implement the swordless `can_place` predicate addition on `LOC_Pyramid_Fairy_Sword` per design.md D5.
- [ ] 4.4 Update the `CanDamageGanon` macro in `assets/rando/macros.yaml` with a swordless branch: `mode_weapons == swordless` requires `(HAS_SilverArrowUpgrade AND (HAS_Hammer OR HAS_FireRod))`.
- [ ] 4.5 Update any other macros that reference "has sword" — ensure swordless doesn't silently let the player pass a sword-required check.
- [ ] 4.6 Settings-screen widget: add `swordless` to the mode.weapons cycle.

## 5. accessibility=none

- [ ] 5.1 Un-pin `accessibility` in `rando_settings.h`. Accept `none=2`.
- [ ] 5.2 CSV parser: accept `accessibility=none`.
- [ ] 5.3 In `Goal_IsCompletable` (`rando_placement.c:1086`), when `settings.accessibility == none`, short-circuit to true (no reachability enforcement).
- [ ] 5.4 At `src/main.c:482`'s strict refusal: skip the refusal when `accessibility == none`.
- [ ] 5.5 Emit a spoiler warning: `fallback_warnings: [{"code": "accessibility_none_seed", "detail": "..."}]`.
- [ ] 5.6 Settings-screen widget: add `none` to the accessibility cycle.

## 6. pyramid_bow_upgrade un-pin

- [ ] 6.1 Un-pin `region_pyramid_bow_upgrade` (Phase A pinned to true). Accept `false`.
- [ ] 6.2 CSV parser: accept `region.pyramidBowUpgrade=false`.
- [ ] 6.3 At `sprite_main.c:1273` (Pyramid Fairy bow trade), branch on `region_pyramid_bow_upgrade`:
  - `true`: existing behavior (grants BowAndSilverArrows).
  - `false`: grants ProgressiveBow without advancing to SilverArrows; the `LOC_Pyramid_Fairy_Bow` slot may place SilverArrowUpgrade elsewhere via the standard placement table.
- [ ] 6.4 Settings-screen widget: toggle.

## 7. Trick predicate authoring

- [ ] 7.1 Grep `../alttp_vt_randomizer/app/Region/Standard/**/*.php` for trick references. Cross-cite with the priming `op_registry.yaml` `tricks:` table.
- [ ] 7.2 Per trick (8 total), enumerate per-location applicability. Update `assets/rando/logic_parts/*.yaml` to gate the affected location's `can_reach` predicate on `OR(<base>, AND(<base-tricks>, OP_TRICK <trick_id>))`.
- [ ] 7.3 Per-location SOURCE citations: every trick-gated predicate references its ALTTPR PHP source line.
- [ ] 7.4 Update `assets/rando/op_registry.yaml` `tricks:` table — advance `status: scaffold` → `status: authored` for each trick once per-location applicability is recorded.
- [ ] 7.5 Run `Logic_SelfCheck` post-trick-authoring; assert no predicate well-formedness violations.

## 8. Per-item bounded rewind (Bug #7)

- [ ] 8.1 In `src/rando/rando_placement.c::Place_AssumedFill`, refactor the placement loop per design.md D1:
  - Replace whole-attempt retry with per-item rewind followed by whole-attempt escalation.
  - Track per-item rewind budget separately from whole-attempt budget.
- [ ] 8.2 Define `kPerItemRewindBudget = 10` constant. Allow INI override via `[Randomizer] per_item_rewind_budget`.
- [ ] 8.3 The rewind operation: undo the last N placements, recompute simulated inventory, retry the current item. The "undo" must restore the placer's state machine cleanly.
- [ ] 8.4 Verify the existing `--budget-seconds` wall-clock budget still bounds total generation time.
- [ ] 8.5 Add a spoiler `fallback_warnings` code for per-item rewind triggers: `{"code": "per_item_rewind_used", "detail": "Placer used N per-item rewinds during generation."}`.
- [ ] 8.6 Prototype on a Triforce Hunt + hard pool seed; record before/after `placement_digest_hex` and forward-fill rate.

## 9. Determinism verification

- [ ] 9.1 Bump `kGeneratorVersion` in `src/rando/rando.h`.
- [ ] 9.2 Run `assets/scripts/bump_rando_corpus.py` to regenerate corpus. Add at least 5 corpus seeds with trick combinations + 3 swordless seeds + 2 accessibility=none seeds.
- [ ] 9.3 **Critical**: verify default-settings digests (tricks=0, logic=NoGlitches, etc.) remain byte-identical to pre-change baseline. Per design.md D1, this should hold because default seeds don't trigger rewind.
- [ ] 9.4 Cross-platform determinism: Linux + macOS + Windows produce byte-identical corpus digests.

## 10. Audit-guard sweep

- [ ] 10.1 Run `assets/scripts/check_audit_guard.py`. No new audit-guard failures.
- [ ] 10.2 Run `assets/scripts/check_determinism.py`. No new `rand`/`time`/`htobe*` symbols.

## 11. CI integration

- [ ] 11.1 Add a CI step that runs the corpus with at least one trick-enabled + one swordless + one accessibility=none seed; verify they generate cleanly (not necessarily winnable for accessibility=none).
- [ ] 11.2 Verify Phase A's existing CI guards (audit, determinism, init-order, kGen) all green post-change.

## 12. Documentation

- [ ] 12.1 Update `docs/randomizer.md` settings reference: document `tricks=`, `logic=`, `mode.weapons=swordless`, `accessibility=none`, `region.pyramidBowUpgrade=false` syntax.
- [ ] 12.2 Add "Trick logic" subsection explaining the 8-trick initial set + ALTTPR community context.
- [ ] 12.3 Add "Glitch logic" subsection explaining the OverworldGlitches / MajorGlitches options.
- [ ] 12.4 Update `docs/randomizer_phase_b.md` Slice 4 status: mark complete.

## 12.5. Performance budget verification

- [ ] 12.5.1 **Generation budget bench (early)**: BEFORE authoring the full 8-trick set (after §1-3 op handlers land, before §7 per-location authoring), generate (a) a Phase A default-settings seed, (b) a tricks=all-8-on seed, (c) a swordless seed. Measure wall-clock against Phase A's 2s desktop / 5s Switch budget.
- [ ] 12.5.2 The Bug #7 per-item rewind (§8) MAY change the wall-clock profile. Re-bench after §8 lands.
- [ ] 12.5.3 If trick-dense seeds exceed budget by >2x: tune per-item rewind N (`kPerItemRewindBudget`, default 10); consider per-trick laziness in predicate evaluation. Per `docs/randomizer_phase_b_risks.md` R4 (rewind tuning).
- [ ] 12.5.4 Record final p50/p95/p99 in `audit.md §"Trick-logic generation benchmark"`.

## 13. Playtest

- [ ] 13.1 Generate a Standard / Fast Ganon / tricks=boots-clip,fake-flippers seed; play to verify trick predicates gate locations correctly.
- [ ] 13.2 Generate a swordless seed; play to confirm Pyramid Fairy Sword slot doesn't place a sword and CanDamageGanon resolves via the swordless branch.
- [ ] 13.3 Generate an accessibility=none seed; confirm `fallback_warnings` includes the warning.
- [ ] 13.4 Generate a pyramid_bow_upgrade=false seed; confirm Pyramid Fairy bow trade grants Bow without auto-silvers.
- [ ] 13.5 Verify default Fast Ganon seed digest unchanged.

## 14. Archive readiness

- [ ] 14.1 CI green on Linux + macOS + Windows; corpus matches across platforms; default-settings digest preserved.
- [ ] 14.2 Manual playtest exercises all 5 un-pinned axes.
- [ ] 14.3 Fresh-eyes audit per memory `[[cluster-audit-cadence]]` post-trick-authoring.
- [ ] 14.4 `openspec archive add-rando-trick-logic-and-axes` runs cleanly; spec deltas merge into `openspec/specs/randomizer-{logic,core}/spec.md`.
