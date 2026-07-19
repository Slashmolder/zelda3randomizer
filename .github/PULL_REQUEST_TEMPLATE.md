### Description
<!-- What is the purpose of this PR and what it adds? -->

### Will this Pull Request break anything?
<!-- Will it break the compiling? -->

### Suggested Testing Steps
<!-- See if the compiling fails/break anything in the game. -->

### Randomizer changes — spec-drift checklist

If this PR touches `src/rando/*`, `assets/rando/*`, or any behavior described
by an OpenSpec scenario under `openspec/specs/randomizer-*/` (or an active change delta under `openspec/changes/`),
confirm:

- [ ] The change is consistent with the relevant spec scenario, **or** the PR
      includes a spec amendment in the same commit.
- [ ] If a placement-affecting path is modified (predicate VM, placement,
      RNG, logic.yaml, registries, `RandoSettings` serialization order),
      `kGeneratorVersion` in `src/rando/rando.h` is bumped per
      `docs/randomizer.md` § "Generator version bump policy".
- [ ] Grant sites use the public grant transaction API; any low-level
      resolver/delivery call is in the narrow source-guard allowlist with a
      reason.
- [ ] `python assets/scripts/run_rando_validation.py quick` passes while
      iterating, and `full` passes before review when local artifacts apply.
- [ ] Placement-affecting changes include a reviewed corpus regeneration with
      explained digest movement; corpus baselines were not blindly replaced.
- [ ] Runtime/rendering changes record owner gameplay playtest coverage
      separately; automated checks are not presented as completing that gate.

Reviewers SHALL refuse PRs where implementation drifts silently from a
referenced spec scenario without an accompanying amendment.
