### Description
<!-- What is the purpose of this PR and what it adds? -->

### Will this Pull Request break anything?
<!-- Will it break the compiling? -->

### Suggested Testing Steps
<!-- See if the compiling fails/break anything in the game. -->

### Randomizer changes — spec-drift checklist

If this PR touches `src/rando/*`, `assets/rando/*`, or any behavior described
by an OpenSpec scenario under `openspec/changes/add-randomizer-support/specs/`,
confirm:

- [ ] The change is consistent with the relevant spec scenario, **or** the PR
      includes a spec amendment in the same commit.
- [ ] If a placement-affecting path is modified (predicate VM, placement,
      RNG, logic.yaml, registries, `RandoSettings` serialization order),
      `kGeneratorVersion` in `src/rando/rando.h` is bumped per
      `docs/randomizer.md` § "Generator version bump policy".
- [ ] New writes to inventory cells (`link_item_*`, `link_bottle_info[*]`,
      etc.) flow through `Rando_OnLocationCheck` **or** carry an explicit
      `// rando-exempt: <reason>` comment per `docs/randomizer.md` § "Audit
      comment convention".
- [ ] Self-checks (`./zelda3 --rando-selftest`) still pass.

Reviewers SHALL refuse PRs where implementation drifts silently from a
referenced spec scenario without an accompanying amendment.
