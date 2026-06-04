## 1. Op-code handlers

- [x] 1.1 Implement `OP_TRICK trick_id` handler in `src/rando/rando_logic.c`. Looks up `settings.tricks` bitmask; returns true iff bit at position `trick_id` is set. *(Landed in slice 4 #56 — `eval_trick` at `rando_logic.c:224-229`. Rejects `trick_id >= 8` defensively.)*
- [x] 1.2 Implement `OP_DIFFICULTY_AT_LEAST threshold` handler. Returns `settings.item_pool_difficulty >= threshold` per the enum ordering (`easy=0`, `normal=1`, `hard=2`, `expert=3`). *(Landed in slice 4 — `eval_difficulty` at `rando_logic.c:230-234`.)*
- [x] 1.3 Implement `OP_GLITCH_LEVEL_AT_LEAST threshold` handler. Returns `settings.logic >= threshold` per the enum ordering (`NoGlitches=0`, `OverworldGlitches=1`, `MajorGlitches=2`). Phase D extends to higher thresholds; this change handles 0-2. *(Landed in slice 4 — `eval_glitch` at `rando_logic.c:235-239`.)*
- [x] 1.4 Add the 3 handlers to `Logic_SelfCheck` with positive + negative test cases. *(Landed — coverage at `rando_logic.c:810-829`. Selftest passes 8/8 OK.)*

## 2. Trick bitmask un-pin + CSV

- [x] 2.1 In `src/rando/rando_settings.h`, confirm `tricks` is uint8 (Phase A pinned to 0). Phase B leaves the type unchanged; just allows user input. *(Confirmed — `rando_settings.h:86`, `uint8 tricks`. Type unchanged.)*
- [x] 2.2 Update `src/rando/rando_settings.c` CSV parser to accept `tricks=boots-clip,fake-flippers,...` syntax. Resolve trick names against the `tricks:` table in `assets/rando/op_registry.yaml` (loaded by `assets/rando_logic_gen.py` and emitted into a runtime lookup table). *(Landed — `rando_settings.c:587-642` accepts `tricks=none | 0 | 0xNN | name | n1+n2+n3`. 8-trick table mirrors op_registry.yaml.)*
- [x] 2.3 Unknown trick names are an error: CLI exits non-zero with a clear message naming the unknown trick. *(Landed — `goto bad_value;` at `rando_settings.c:638` produces "Settings_ParseCsv: bad value '...' for key 'tricks'".)*
- [x] 2.4 Settings-screen widget: multi-select trick toggle. *(Done 2026-06-04 — `rando_window.cpp` renders a TreeNode of 8 checkboxes toggling settings.tricks bits; the 3 fork placeholders are labelled + tooltip'd. PC native settings window, not the in-game screen which is compiled out on PC.)*

## 3. Logic level un-pin + CSV

- [x] 3.1 Un-pin `logic` in `rando_settings.h:88`. Accept `OverworldGlitches=1` and `MajorGlitches=2` from user input. Reject `HybridMG=3` and `NoLogic=4` with a "deferred to Phase D" message. *(Landed 2026-05-27 — `rando_settings.c:643-666` accepts NoGlitches/OverworldGlitches/MajorGlitches; rejects HybridMG/NoLogic with the Phase-D message via stderr + bad_value. Verified: `logic=OverworldGlitches`/`logic=major_glitches` both generate clean; `logic=HybridMG` emits "deferred to Phase D" and exits non-zero.)*
- [x] 3.2 CSV parser: accept `logic=overworld_glitches | major_glitches` (snake_case). *(Landed — snake_case + PascalCase + numeric forms all accepted.)*
- [x] 3.3 Settings-screen widget: cycle logic through the 3 supported values. *(Done 2026-06-04 — `rando_window.cpp` "Logic" EnumCombo over {NoGlitches, OverworldGlitches, MajorGlitches}; clamps a loaded 3/4 down to 2.)*

## 4. mode_weapons un-pin (swordless)

<!-- DONE end-to-end (2026-06-04, after the initial BLOCKED assessment
     was overturned — with full C source we ADD the runtime behavior instead of
     patching ROM bytes). Logic + pool + runtime + UI all landed and gated on
     mode_weapons==swordless / Rando_IsSwordlessActive(); every existing corpus
     digest is byte-identical (verified 85/85, swordless branches inert under
     randomized). kGen 51→52. Validated: goal_completable across
     open/standard/inverted/retro × fast_ganon/ganon/dungeons/completionist/
     triforce-hunt with 0 swords placed. Runtime sword-substitution is
     PLAYTEST-PENDING. design.md D5 (can_place on
     LOC_Pyramid_Fairy_Sword) was OBSOLETE — used pool sword-removal instead. -->
- [x] 4.1 Un-pin `mode_weapons` — accept `swordless=3`. *(`rando_settings.h` enum + `parse_weapons`.)*
- [x] 4.2 CSV parser: accept `mode.weapons=swordless`. *(`rando_settings.c` parse_weapons.)*
- [x] 4.3 ~~swordless `can_place` on `LOC_Pyramid_Fairy_Sword`~~ — OBSOLETE (LOCs 210/211 retired). Used a sword-removed item-pool branch (`rando_placement.c`) + a guaranteed SilverArrowUpgrade instead.
- [x] 4.4 Swordless Ganon predicate — `swordless ? (Hammer AND CanShootSilvers()) : HasSword2()` (Std `43_darkworld` + Inv `NorthEast`), per NorthEast.php:227-233 + World.php:269-273. New op `OP_MODEWEAPONS_EQ` (id 18). Also Agahnim 1 (Hammer/net), medallion casts, Skull Woods, Kholdstare, Trinexx, tablets.
- [x] 4.5 Other "has sword" macros — CanMeltThings swordless Bombos branch; CanKillKholdstare + CanKillTrinexx Hammer-kill swordless path (the two that stranded IP/TR crystals).
- [x] 4.6 Settings widget — PC `rando_window.cpp` weapons combo (value-mapped, skips vanilla=2) + Switch `select_file.c` cycle/label.
- [x] 4.7 Runtime (the real un-block): hammer damages Ganon (`sprite.c`), medallions cast w/o sword (`player.c`), tablets hammer-read (`sprite_main.c`), Agahnim/Skull-Woods curtains pre-opened (`rando_generate.c` slot SRAM-init). All gated on `Rando_IsSwordlessActive()`.

## 5. accessibility=none

- [x] 5.1 Un-pin `accessibility` in `rando_settings.h`. Accept `none=2`. *(Landed 2026-05-27 — `kAccessibility_None = 2` no longer commented-out in `rando_settings.h:64`.)*
- [x] 5.2 CSV parser: accept `accessibility=none`. *(Landed — `parse_accessibility` at `rando_settings.c:485` adds the `none` branch.)*
- [x] 5.3 In `Goal_IsCompletable` (`rando_placement.c:1086`), when `settings.accessibility == none`, short-circuit to true (no reachability enforcement). *(Landed — `rando_placement.c:1140-1142` short-circuits before reachability compute. Verified: `--settings=accessibility=none` with a goal that would normally refuse generates clean.)*
- [x] 5.4 At `src/main.c:482`'s strict refusal: skip the refusal when `accessibility == none`. *(Implicit via §5.3 — `Goal_IsCompletable` returns true so the refusal branch never fires. No `main.c` change needed.)*
- [x] 5.5 Emit a spoiler warning: `fallback_warnings: [{"code": "accessibility_none_seed", "detail": "..."}]`. *(Landed 2026-05-27 — `rando_spoiler.c::Spoiler_WriteJson` now appends a `{"kind": "accessibility_none_seed", "detail": "goal-completability not enforced; seed may be unwinnable"}` entry to the `fallback_warnings` array when `s->settings->accessibility == 2`. Per LOW L1 of the fresh-eyes audit of e9f20ad.)*
- [ ] 5.6 Settings-screen widget: add `none` to the accessibility cycle. *(Deferred — settings-screen UI work.)*

## 6. pyramid_bow_upgrade un-pin

<!-- OBSOLETE (2026-06-04). NOT un-pinned — the setting's runtime
     mechanism no longer exists. The fairy-chest-model change (kGen 49→50)
     replaced the Pyramid Fairy bow trade-in with a two-chest model:
     Sprite_WishPond3 (sprite_main.c:1179) now grants LOC_Pyramid_Fairy_Left =
     ITEM_SilverArrowUpgrade directly on contact. Nothing in src/ reads
     pyramid_bow_upgrade. design.md D7's premise (bow trade → bow+arrows vs
     bow+silvers) is invalidated, same as D5/swordless. Un-pinning `arrows`
     would expose a no-op setting. Left pinned to Silvers; stays in the UI
     "Locked settings" block. -->
- [ ] 6.1 ~~Un-pin `region_pyramid_bow_upgrade`~~ — OBSOLETE under the fairy-chest model (no runtime reader).
- [ ] 6.2 ~~CSV `region.pyramidBowUpgrade=false`~~ — OBSOLETE (would be a no-op value).
- [ ] 6.3 ~~`sprite_main.c` bow-trade branch~~ — the bow trade-in was removed by the chest model; Sprite_WishPond3 grants Left/Right directly.
- [ ] 6.4 ~~Settings-screen toggle~~ — kept in Locked settings (Silvers) since it has no effect.

## 7. Trick predicate authoring

<!-- done (2026-06-04 reality map): §7.1-7.3 were LANDED by prior
     "Slice 4 §7 batches 1-4" (acknowledged at §12.6:100) but never ticked. The
     5 real ALTTPR tricks are wired + cited across 208 predicate uses
     (boots-clip×38, fake-flippers×6, bunny-revival×50, dark-room-nav×42,
     pearl-bypass×72, glitch-levels×40). Verified vs an ALTTPR config-flag grep:
     these 5 map to canBootsClip/canFakeFlipper/canBunnyRevive/item.require.Lamp/
     canOWYBA. bits 4/6/7 (bomb-jump/hookshot-clip/lobotomy) are FORK-INVENTED
     placeholders — NO ALTTPR flag exists, so they cannot be PHP-grounded and are
     left unwired (status: placeholder, not authored). -->
- [x] 7.1 Grep ALTTPR for trick references; cross-cite the `op_registry.yaml` `tricks:` table. *(Done in Slice 4 §7; re-verified — ALTTPR flags enumerated, 5 of 8 fork tricks have an upstream flag.)*
- [x] 7.2 Per trick, enumerate per-location applicability + gate the affected predicates. *(Done for the 5 real tricks; the 3 placeholders have no upstream sites to enumerate.)*
- [x] 7.3 Per-location SOURCE citations on every trick-gated predicate. *(Done — `source:` line ranges present throughout `logic_parts/**`.)*
- [x] 7.4 Advance `op_registry.yaml` `tricks:` `status: scaffold` → `authored`. *(5 real tricks → `authored`; 3 fork placeholders → `placeholder` with rationale.)*
- [x] 7.5 Run `Logic_SelfCheck` post-trick-authoring. *(Green: `rando_logic_gen.py --strict` 0 warnings + `--rando-selftest` `[Logic_SelfCheck] OK`.)*

## 8. Per-item bounded rewind (Bug #7)

<!-- IMPLEMENTED but SHIPPED GATED OFF (2026-06-04). The mechanism is
     complete + terminating + completability-preserving (rando_placement.c
     place_assumed_fill_attempt while-loop). Verified byte-identical at
     kPerItemRewindBudget=0 (corpus 79/79). At the design-D1 value 10, 11 hard
     corpus seeds change and ALL stay goal_completable, but it did NOT improve
     placement quality on the hard seeds measured (TH/expert/0xABC123 went 1→3
     forward-fills) — the reshuffle-and-retry just churns RNG. Shipping it on
     would change 11 digests + force a bump for no benefit, and quality can only
     be judged by playtest. Gated to 0; flip to 10 to evaluate. -->
- [x] 8.1 Refactor `place_assumed_fill_attempt` per design D1 (per-item rewind before forward-fill escalation). *(Done — while-loop + prog_slot[] tracking.)*
- [x] 8.2 Define `kPerItemRewindBudget`. *(Done — constant in rando_placement.c, shipped =0. INI override deferred: a placement-affecting INI knob is a share-string-determinism footgun; left as a compile-time constant.)*
- [x] 8.3 Rewind reopens last N slots + recomputes inventory + retries. *(Done — re-adds rewound items to assumed inventory, reshuffles the window, restarts at i-n; tier-clamped to not cross the dungeon boundary.)*
- [x] 8.4 `--budget-seconds` still bounds total time. *(Unchanged — the outer Place_AssumedFill retry loop still owns the wall-clock budget; rewind is bounded per-attempt.)*
- [ ] 8.5 Spoiler `per_item_rewind_used` warning. *(Deferred — PlacementStats gains `per_item_rewind_count` for benchmarking; the spoiler warning is moot while gated off.)*
- [x] 8.6 Prototype on a hard seed; record digest + forward-fill. *(Done — budget=0 byte-identical proof + budget=10 backtrack/forward-fill measurements.)*

## 9. Determinism verification

- [x] 9.1 Bump `kGeneratorVersion` 50→51. *(Done — version-lock for the §12.6 spoiler warning, §47-precedent; 0 placement-digest change.)*
- [x] 9.2 Regenerate corpus + add new-axis seeds. *(Done — `bump_rando_corpus.py --apply` reported "4 digest(s) updated" = only the 4 NEW entries. Added b-tricks-all5 / b-tricks-darkroom / b-logic-owg / b-logic-mg. NO swordless seed (BLOCKED §4); accessibility=none already in corpus as `a1-standard-triforce-hunt-beatable`.)*
- [x] 9.3 **Critical**: default-settings digests byte-identical. *(Verified TWICE: (a) rewind gated to 0 → corpus 79/79 byte-identical pre-bump; (b) the bump regen changed ONLY the 4 new placeholder digests + the version line — every existing digest unchanged, confirmed by `git diff`.)*
- [ ] 9.4 Cross-platform determinism (Linux/macOS/Windows). *(Windows verified here; Linux/macOS is CI's job on merge — unchanged contract.)*

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
- [ ] 12.4 Cross-link this change from the `openspec/changes/` index (README.md).

## 12.5. Performance budget verification

- [ ] 12.5.1 **Generation budget bench (early)**: BEFORE authoring the full 8-trick set (after §1-3 op handlers land, before §7 per-location authoring), generate (a) a Phase A default-settings seed, (b) a tricks=all-8-on seed, (c) a swordless seed. Measure wall-clock against Phase A's 2s desktop / 5s Switch budget.
- [ ] 12.5.2 The Bug #7 per-item rewind (§8) MAY change the wall-clock profile. Re-bench after §8 lands.
- [ ] 12.5.3 If trick-dense seeds exceed budget by >2x: tune per-item rewind N (`kPerItemRewindBudget`, default 10); consider per-trick laziness in predicate evaluation (rewind-tuning risk).
- [ ] 12.5.4 Record final p50/p95/p99 in `audit.md §"Trick-logic generation benchmark"`.

## 12.6. ROM-version trick verification (new requirement — randomizer-logic spec)

Captures the concern raised mid-batch-4 trick authoring (2026-05-27): ALTTPR targets Japanese 1.0; this fork targets US 1.0; some tricks may behave differently or be absent on US 1.0. The trick gates landed across slices 4 §7 batch 1-4 are textually-correct translations of upstream PHP but UNVERIFIED on US 1.0. See `randomizer-logic` spec delta § "Per-trick ROM-version verification status".

<!-- done (2026-06-04): §12.6 scaffolding landed in one commit.
     Validated: codegen 0 warnings; tricks-on seed emits the warning naming
     boots-clip/pearl-bypass/overworld_glitches; dark-room-nav (cross-version)
     does NOT warn; jp10-only temporarily set on a wired trick → codegen --strict
     rejects every referencing gate (reverted). Corpus stayed 79/79 (inert under
     default settings). -->
- [x] 12.6.1 `rom_version_status` field on each `tricks:` entry. *(op_registry.yaml; dark-room-nav→cross-version, rest untested-on-us10.)*
- [x] 12.6.2 `glitch_levels:` table (OverworldGlitches/MajorGlitches/HybridMG/NoLogic), all `untested-on-us10`.
- [x] 12.6.3 Codegen emits `kRandoTrickStatus[]`/`kRandoGlitchLevelStatus[]` + counts (`logic_data.c`; decl in `rando_logic.h`).
- [x] 12.6.4 `unverified_tricks_enabled` warning in the spoiler `fallback_warnings` (`rando_spoiler.c`), for tricks + reached glitch levels with unverified status.
- [x] 12.6.5 Codegen well-formedness rejects any predicate referencing a `jp10-only` trick (`well_formedness` in `rando_logic_gen.py`).
- [x] 12.6.6 Documented in `docs/randomizer.md` § "Tricks / glitch logic — ROM-version verification" (status table + contributor upgrade guide).
- [x] 12.6.7 Per-trick US-1.0 verification kept OUT OF SCOPE — follow-on playtest workstream. *(Kept as a follow-on playtest-pending item.)*
- [x] 12.6.8 Status backfill for already-wired tricks: dark-room-nav → cross-version; all other wired tricks + glitch levels → untested-on-us10.

## 13. Playtest

- [ ] 13.1 Trick-gated seed playtest — verify each wired trick is *performable* on US 1.0 at its gated spot. *(PENDING — this is exactly what `rom_version_status: untested-on-us10` flags; per-trick verification is a follow-on workstream, see §12.6.7. Reachability is headless-validated.)*
- [x] 13.2 Swordless seed beaten end-to-end. *(Done 2026-06-04 — a full swordless game was BEATEN on US 1.0: tablets/medallions/curtains/Evil-Barrier/Agahnim-1&2/Kholdstare/Trinexx/Ganon all clear with the Hammer. Two runtime fixes surfaced + fixed by playtest: the Evil Barrier (hammer-break) and the boss-macro showstoppers. The original wording — `LOC_Pyramid_Fairy_Sword` `can_place` + `CanDamageGanon` — is obsolete; swordless ships via pool sword-removal + the `OP_MODEWEAPONS_EQ` predicates + the runtime patches.)*
- [ ] 13.3 ~~accessibility=none `fallback_warnings` warning~~ — OBSOLETE: the `accessibility_none_seed` warning was removed when the ALTTPR three-way accessibility landed (kGen 45→46); `none` = "beatable only" is still guaranteed completable, so there is no "may be unwinnable" warning to emit.
- [ ] 13.4 ~~pyramid_bow_upgrade=false playtest~~ — NOT shipped (obsolete under the fairy-chest model; see §6).
- [x] 13.5 Default Fast Ganon seed digest unchanged. *(Verified — corpus 83 pre-existing digests byte-identical across the kGen 50→51→52 bumps; every swordless/trick/rewind change is inert under default settings.)*

## 14. Archive readiness

- [x] 14.1 CI guards green; default-settings digest preserved; corpus 87/87. *(Windows verified locally — build 0 warnings, `--rando-selftest` OK, all guard scripts clean, kGen 52. Linux/macOS cross-platform determinism is the merge CI's job, unchanged contract.)*
- [~] 14.2 Manual playtest of the un-pinned axes. *(SWORDLESS beaten end-to-end (§13.2). tricks/glitch reachability headless-validated; their on-US-1.0 performability is the `rom_version_status` follow-on, not a merge blocker. accessibility=none + pyramid_bow scenarios reconciled as obsolete (§13.3-4).)*
- [x] 14.3 Fresh-eyes audit per `[[cluster-audit-cadence]]`. *(TWO independent audit agents: one on the trick-logic/rewind/ROM-version diff (no HIGH/MED), one on the swordless diff (no HIGH / no softlock-class). All findings dispositioned.)*
- [ ] 14.4 `openspec archive add-rando-trick-logic-and-axes` after merge; spec deltas (randomizer-logic adds `OP_MODEWEAPONS_EQ` + the per-trick ROM-version requirement; randomizer-core's swordless/pyramid scenarios reconciled to as-built) merge into `openspec/specs/randomizer-{logic,core}/spec.md`.
