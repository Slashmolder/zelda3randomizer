# Tasks

## 1. Acceptance predicate (generator core)
- [x] 1.1 Add `Accessibility_SeedAcceptable(settings, placements)` to
  `rando_placement.c` + declaration in `rando_placement.h`, split into a pure
  graph-free tier rule (`accessibility_reachability_ok`) + the goal/sphere
  wrapper.
- [x] 1.2 Rewire `Goal_ShouldRefuse` to `!Accessibility_SeedAcceptable(...)`
  (drop the `kAccessibility_None` short-circuit).

## 2. Route every acceptance gate through the predicate
- [x] 2.1 CLI `main.c`: entrance-retry accept + final refuse gate; fix the
  user-facing message (drop "accessibility=none opts into unwinnable").
- [x] 2.2 Slot path `rando_generate.c`: entrance-retry accept + the previously
  **ungated** non-entrance path.

## 3. UI
- [x] 3.1 PC native window: `kAccessibilityLabels` → 3 entries, `EnumCombo(... 3)`,
  add a tier-explaining tooltip; Completionist lock unchanged.
- [x] 3.2 Switch in-game screen: relabel `NONE` → `BEAT`; fix the stale comment.

## 4. Serialization / CSV / selftests
- [x] 4.1 Enum comment in `rando_settings.h` (None == "beatable only").
- [x] 4.2 Accept `beatable` CSV alias in `parse_accessibility`.
- [x] 4.3 Add `accessibility=beatable → 2` to `Settings_SelfCheck`.
- [x] 4.4 Add the tier-discrimination selftest to `Placement_SelfCheck`
  (synthetic spheres; pins `locations ⊇ items ⊇ beatable`).

## 5. Spoiler + guards
- [x] 5.1 Delete the `accessibility_none_seed` warning in `rando_spoiler.c`.
- [x] 5.2 Drop `accessibility_none_seed` from `FAILURE_WARNING_KINDS` in
  `check_rando_invariants.py` + fix the docstring rationale.

## 6. Determinism + corpus + docs
- [x] 6.1 Bump `kGeneratorVersion` 45 → 46 (`rando.h`).
- [x] 6.2 Regenerate the corpus (`bump_rando_corpus.py --apply`): version 45→46,
  0 digest changes (Place_AssumedFill unchanged → all seeds byte-identical).
- [x] 6.3 Corpus tier coverage: entry 11 → `accessibility: none` (standard
  triforce-hunt 30 pieces; the key "beatable accepts what items rejects" test),
  new `a1-open-fg-acc-beatable` entry (normal-case beatable), existing
  `a1-open-fg-acc-locations` (locations); `items` is the default of most entries.
- [x] 6.4 Document the three tiers in `docs/randomizer.md`.

## 7. Verification
- [x] 7.1 Worktree build, `-Werror` clean.
- [x] 7.2 `--rando-selftest` green (incl. new tier + beatable-alias selftests).
- [x] 7.3 `run_rando_corpus.py` green (63 entries) + check_rando_invariants +
  check_determinism + check_codegen_wiring green. (check_audit_guard advisory on
  sprite_main.c is pre-existing on main, unrelated to this change.)
- [x] 7.4 Race-mode stamp byte-stability: the race corpus entries (52–54) pass
  the ZRSR reveal round-trip, exercising generate-vs-regen byte-identity.
- [ ] 7.5 Fresh-eyes audit agent pass (new findings only).
- [ ] 7.6 Hand back the branch for the user's playtest.
