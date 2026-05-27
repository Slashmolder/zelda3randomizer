# add-rando-trick-logic-and-axes

Phase B Slice 4 + folded misc items. Un-pins 5 Phase A settings axes (`tricks`, `logic`, `swordless`, `accessibility=none`, `pyramid_bow_upgrade=arrows`), activates 3 reserved op-codes (`OP_TRICK`, `OP_DIFFICULTY_AT_LEAST`, `OP_GLITCH_LEVEL_AT_LEAST`), and implements Phase A1 audit Bug #7 (per-item bounded rewind in `Place_AssumedFill`).

## Status

**Proposal-only stub.** Authored: 2026-05-26. Specs deltas + `tasks.md` deferred to `/openspec-explore` + `/openspec-propose` at apply-time.

**Stub-only because**: trick predicate authoring depends on ALTTPR PHP grep findings (specific trick names + per-location applicability); per-item rewind needs a prototype to settle the rewind-budget tuning (N=10 is a guess); the interaction matrix between settings and predicates is best surveyed during /openspec-explore.

## Read these in order

| File | Purpose | Status |
|---|---|---|
| [proposal.md](proposal.md) | Why, what changes, capabilities, impact | ✅ authored |
| [specs/randomizer-logic/spec.md](specs/randomizer-logic/spec.md) | 3 op-code handlers + trick predicate convention | 🪵 minimal stub deltas |
| [specs/randomizer-core/spec.md](specs/randomizer-core/spec.md) | Settings axes + per-item rewind | 🪵 minimal stub deltas |
| `design.md` | Per-item rewind algorithm + tuning + escalation strategy | ⏳ deferred (apply-time) |
| `tasks.md` | Implementation checklist | ⏳ deferred (apply-time) |

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

1. `/openspec-explore add-rando-trick-logic-and-axes` — grep ALTTPR PHP for trick names + per-location applicability; survey settings-axis CSV grammar; prototype per-item rewind on a known-hard seed.
2. `/openspec-propose` to finalize spec deltas + design.md.
3. `/openspec-apply` to walk through tasks.
4. `/openspec-archive` when done.
