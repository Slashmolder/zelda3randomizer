# Vanilla NPC randomized-item hint redirects — tasks

## 1. Spec and audit

- [x] 1.1 Create the focused OpenSpec change from `a82667d9`.
- [x] 1.2 Re-sweep current `assets/dialogue.txt` against the current item registry;
  reconcile one-based dialogue numbers with runtime indices and record handlers,
  interception state, and collision/discriminator findings in `audit.md`.
- [x] 1.3 Validate the change strictly and reconcile the delta against the final
  as-built code before handoff.

## 2. Runtime implementation

- [x] 2.1 Add the table-driven redirect resolver and deterministic lowest-location
  duplicate policy to `rando_hints.c`.
- [x] 2.2 Render complete, bounded fixed-item hints through the existing US encoder;
  safely preserve vanilla on every failed gate and on non-US buffers.
- [x] 2.3 Add the pure active-dynamic-hint predicate and exempt active redirects
  (including `0x36`) from story-dialogue fast-forward.
- [x] 2.4 Extend F12 diagnostics with redirect classification, source, item,
  resolved location, and skip reason.

## 3. Automated coverage and versioning

- [x] 3.1 Add synthetic-placement self-checks for Book, Moon Pearl, hints off,
  inactive slot, missing settings/item, adjacent IDs, and duplicate determinism.
- [x] 3.2 Pin generated tile/fork mappings as unchanged and cover `0x36` dynamic
  readability plus F12 redirect diagnostics.
- [x] 3.3 Cover complete three-row formatting across every active location and the
  deliberate non-US fallback.
- [x] 3.4 Bump `kGeneratorVersion` and corpus manifest version 145 to 146.

## 4. Validation

- [x] 4.1 `openspec validate add-rando-vanilla-npc-hint-redirects --strict`.
- [x] 4.2 Generator/version, codegen-wiring, logic-override, and diff checks.
- [x] 4.3 Release MSVC build and `--rando-selftest`.
- [x] 4.4 Clean WSL/GCC `-Werror` build and self-test.
- [x] 4.5 Full local randomizer corpus with ROM-derived artifacts.
- [x] 4.6 Fresh assetless codegen/build/self-test.
- [x] 4.7 Independent fresh-eyes review for ID collisions, vanilla/hints-off
  preservation, truncation, fast-forward, diagnostics, and restored-slot state;
  address all concrete findings and revalidate.

## 5. Required owner playtest — do not merge first

- [ ] 5.1 Seed with Book and Moon Pearl outside vanilla locations: test Aginah
  before Book, Dark-World bully without Pearl, old mountain man without Pearl,
  and post-Agahnim warning without Pearl.
- [ ] 5.2 Repeat an applicable case with hints off and confirm vanilla mode is
  unchanged.
- [ ] 5.3 Press F12 on each surface and confirm source, target item, resolved
  placement, and active/skip status.
- [ ] 5.4 Archive the OpenSpec change, squash/merge, and push only after owner
  playtest approval.
