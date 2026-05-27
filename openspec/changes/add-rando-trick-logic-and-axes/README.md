# add-rando-trick-logic-and-axes

Phase B Slice 4 + folded misc items. Un-pins 5 Phase A settings axes (`tricks`, `logic`, `swordless`, `accessibility=none`, `pyramid_bow_upgrade=arrows`), activates 3 reserved op-codes (`OP_TRICK`, `OP_DIFFICULTY_AT_LEAST`, `OP_GLITCH_LEVEL_AT_LEAST`), and implements Phase A1 audit Bug #7 (per-item bounded rewind in `Place_AssumedFill`).

## Status

**Fully authored.** Authored: 2026-05-26. Promoted from stub to full content with design.md (decisions on per-item rewind algorithm, trick bit-positions, CSV syntax, swordless/accessibility=none semantics).

## Read these in order

| File | Purpose | Status |
|---|---|---|
| [proposal.md](proposal.md) | Why, what changes, capabilities, impact | ✅ authored |
| [design.md](design.md) | Per-item rewind algorithm; trick bit-positions; CSV syntax; swordless predicate impact; accessibility=none semantics | ✅ authored |
| [specs/randomizer-logic/spec.md](specs/randomizer-logic/spec.md) | 3 op-code handlers + trick predicate convention | ✅ authored |
| [specs/randomizer-core/spec.md](specs/randomizer-core/spec.md) | Settings axes (full Phase A quote + Phase B un-pins) + per-item rewind | ✅ authored |
| [tasks.md](tasks.md) | Implementation checklist (14 sections, ~60 tasks) | ✅ authored |

Priming directory: `assets/rando/op_registry.yaml` `tricks:` section holds the initial 8 trick bit-position scaffolds (#29).

## Effort

**2-3 weeks of focused work.** Trick-predicate authoring is the long pole; op handlers are small (~50 lines each); per-item rewind is a focused refactor.

## Folded items

| Item | Source | Why here |
|---|---|---|
| `OP_TRICK` / `OP_DIFFICULTY_AT_LEAST` / `OP_GLITCH_LEVEL_AT_LEAST` | Slice 4 | Headline of this slice |
| Trick predicates per dungeon | Slice 4 | Headline of this slice |
| `tricks` settings bitmask un-pin | Slice 4 | Headline of this slice |
| `logic` glitch-level settings axis un-pin | Slice 4 | Headline of this slice |
| `swordless` mode.weapons value | Source doc misc | Logic-relaxation peer axis |
| `accessibility=none` axis value | Source doc misc | Logic-relaxation peer axis |
| `pyramid_bow_upgrade=arrows` value | Source doc misc | Settings-axis peer; small per-site change at Pyramid Fairy |
| Bug #7 per-item bounded rewind | Phase A1 audit | Tricks make placement harder; placement-quality fix lands here |

## Dependencies

- **Phase A archived first.** Deltas multiple specs post-archive.
- **No dependency on other Phase B slices.** Could ship alongside #4a Inverted (trick predicates in Inverted dungeons are a #5 follow-on if #4a ships first).

## When work starts

1. `/openspec-apply add-rando-trick-logic-and-axes` — walk through `tasks.md` task-by-task.
2. Critical: prototype per-item rewind (Section 8) early — if N=10 is too aggressive, tune before authoring the trick predicates.
3. Fresh-eyes audit post-trick-authoring per memory `[[cluster-audit-cadence]]`.
4. `/openspec-archive` when done.
