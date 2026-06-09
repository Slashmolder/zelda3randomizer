# Tasks — add-rando-major-glitch (Phase D)

Status legend: `[x]` shipped this change · `[~]` partial / frontier documented ·
`[ ]` not started. `<!-- done: -->` marks foundation inherited from Phase B.

## 1. Ground truth + artifacts

- [x] 1.1 STEP-0 reality map + tier/technique survey from `config/logic.php` and
  the Region closures (see `design.md` STEP-0). Finding: glitch tiers are
  per-technique config flags; non-monotonic vs the fork enum (HMG ⊂ MG).
- [x] 1.2 Author `design.md` (this change) + `tasks.md`.
- [x] 1.3 Reconcile both spec deltas to as-built (HMG-has-no-exclusive-locations;
  warning emitted by reading settings directly; placement not short-circuited).

## 2. Enum un-pin (CSV + UI) — D2

- [x] 2.1 `rando_settings.c`: accept `HybridMG|hybrid_mg|hybrid_major_glitches|3`
  → `logic=3` and `NoLogic|no_logic|4` → `logic=4`; update the comment block.
- [x] 2.2 `rando_window.cpp`: `kLogicLabels[]` → 5 entries; remove the
  `if (s->logic>2) s->logic=2` clamp; `EnumCombo` count 3→5; tooltip warns NoLogic
  enforces no reachability + HMG/MG expect US-1.0-unverified tech.
- [x] 2.3 Verify `logic=hybrid_mg` and `logic=no_logic` parse via `--generate-seed`;
  settings_hash differs from `logic=major_glitches`; every existing seed's
  settings_hash byte-identical.

<!-- done: OP_GLITCH_LEVEL_AT_LEAST op-code 17 + eval_glitch shipped in Phase B
     (add-rando-trick-logic-and-axes). The VM already evaluates thresholds 0-4. -->

## 3. NoLogic reachability short-circuit — D1

- [x] 3.1 `Predicate_EvalCtx`: early-return `true` when
  `settings->logic >= NoLogic(4) && placement_context == 0`. Reachability only;
  `can_place` (context 1) untouched.
- [x] 3.2 Verify a `logic=no_logic` seed: generates, writes spoiler,
  `goal_completable=true`, refusal does not fire, slot round-trip OK.
- [x] 3.3 `--rando-selftest`: NoLogic generation covered (RandoGenerate_SelfCheck
  / Placement_SelfCheck exercise the codepath; extend if a gap is found).

## 4. `no_logic_seed` spoiler warning — D3

- [x] 4.1 `rando_spoiler.c`: emit the exact spec `{"code":"no_logic_seed",
  "detail":"..."}` entry when `settings->logic == NoLogic`.
- [x] 4.2 Assert present for NoLogic, absent otherwise (corpus NoLogic entry +
  any non-NoLogic entry).

## 5. Glitch-technique macro fold (OWG/MG un-collapse) — D4

- [x] 5.1 `macros.yaml`: fold `OP_GLITCH_LEVEL_AT_LEAST(overworld_glitches)` into
  CanBootsClip / CanFakeFlippers / CanBunnyRevival (OR-branch); fold the
  MG-exclusive form into CanPearlBypass (canOWYBA). Cite `config/logic.php` lines.
- [x] 5.2 `op_registry.yaml`: update the boots-clip / fake-flippers /
  bunny-revival / pearl-bypass trick notes + the `overworld_glitches` glitch-level
  note (no longer "zero tier-1 gates").
- [x] 5.3 `rando_logic_gen.py --strict` clean; corpus regen + 3-way diff:
  logic=0/tricks-only byte-identical; logic≥1 each PHP-cited.

## 6. ROM-version status — D5

- [x] 6.1 `kRandoGlitchLevelStatus[]` covers tier 4 (NoLogic) `untested-on-us10`;
  `unverified_tricks_enabled` fires for every reached unverified tier.
- [x] 6.2 New tiers/techniques default `untested-on-us10` until playtest.

## 7. Determinism + corpus — D-validation

- [x] 7.1 Bump `kGeneratorVersion` 58→59 (one-line rationale, dense-comment style).
- [x] 7.2 Add corpus entries: ≥1 `logic=hybrid_mg`, ≥1 `logic=no_logic`
  (`accessibility: none`). Keep existing OWG + MG entries (they move — justified).
- [x] 7.3 `bump_rando_corpus.py --apply`; inspect diff; restore CRLF on manifest;
  drop manifest generator_version below kGen first if pre-bumped.
- [x] 7.4 Full guard suite green (no_embedded_data, determinism, byte_order,
  audit_guard, codegen_wiring, generator_version, corpus_version_sync,
  placer_determinism).

## 8. Fresh-eyes audit

- [x] 8.1 Reviewed `git diff main..HEAD` (NoLogic-seam leak, macro tiers vs PHP,
  last-wins clobbers, enum off-by-one, warning gating). Result: NoLogic seam,
  macro tiers, enum/UI/CSV, DSL precedence all verified correct vs config/logic.php.
  Two LOW findings fixed (commit 25c8480): (1) NoLogic emitted a redundant +
  self-referential `unverified_tricks_enabled` listing — suppressed under
  logic>=4; (2) design.md said `==NoLogic(4)`, code uses robust `>=4` —
  reconciled the doc. Re-validated green.

## 9. Couple glitch-logic seeds to the JP-glitch runtime flag — D6

(Close-out pass, 2026-06-08, after `add-jp-glitch-restoration` made the six
JP-1.0 glitches executable.)

- [x] 9.1 `Rando_SettingsAssumeJpGlitches` (`rando_settings.c`):
  `logic>=OverworldGlitches OR (tricks & fake_flippers)`. Only fake-flippers
  among the 8 tricks maps to a restored glitch.
- [x] 9.2 `Rando_GenerateSlot` ORs the flag into `recommended_features0`;
  `Rando_ActivateSidecarSlot` force-applies it live (config + wanted mirror +
  enhanced) on EVERY slot load — the authoritative reload-safe guarantee.
- [x] 9.3 FORCE-on (not recommend/warn) — a glitch seed without the glitch
  executable is the worst failure mode. Resolves the JP-glitch spec's
  `fake-flippers` "unsupported combination" deferral.
- [x] 9.4 Corpus byte-identical (features0 ≠ canonical; corpus path bypasses
  `Rando_GenerateSlot`). `Rando_SettingsAssumeJpGlitches` assertions in
  `Settings_SelfCheck` (`--rando-selftest`). No kGen bump for D6.

## Frontier

- [x] F1 Per-site reclassification of the raw `OP_GLITCH_LEVEL_AT_LEAST(
  major_glitches)` threshold sites. PHP-verified every standard site against
  `app/Region/**`: all 30 = `canOneFrameClipOW` (MG-exclusive →
  `>=2 AND NOT >=3`, closes at HMG); Swamp/Ice = `canOneFrameClipUW` (HMG+MG,
  digest-neutral rename). Moves ONLY `b-logic-hmg` (verified 1/112). kGen 61→62.
- [x] F2 First-class macros authored (`CanSuperSpeed`/`CanSuperBunny`/
  `CanMirrorClip`/`CanWaterWalk`/`CanDungeonRevive`/`CanOneFrameClipUW`/
  `CanMirrorWrap`/`CanOneFrameClipOW`/`CanTransitionWrapped`), pure level gates,
  each tier cited by `config/logic.php` line. Digest-neutral (unwired until F1).
- [x] F3 Inverted-region parity — all 9 inverted sites = `canOneFrameClipOW`
  (PHP-verified), reclassified. Digest-neutral for existing corpus (all inverted
  entries logic=0); added `b-logic-hmg-inverted-fast-ganon` coverage.
- [~] F4 US-1.0 performability: `fake-flippers` flipped `verified-us10` (Fake
  Flippers restored + playtest-confirmed + D6 forces the flag). The glitch LEVELS
  + `canSuperSpeed` (no isolable entry) + the 10 un-restored techniques stay
  `untested-on-us10` — playtest-pending. See the performability ledger (design.md
  D6). **Note: surfacing the deliberately-DROPPED PHP disjuncts (canSuperSpeed/
  canMirrorClip/canSuperBunny/canWaterWalk/canDungeonRevive at the ~30 sites) is a
  further frontier — F1 only reclassified the EXISTING raw threshold, it did not
  add new reachability paths.**
- [ ] F5 NoLogic `can_place` short-circuit ("items literally anywhere") if a
  use-case needs it — currently confinement is preserved (D1).
