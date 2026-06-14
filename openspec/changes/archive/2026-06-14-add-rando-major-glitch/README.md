# add-rando-major-glitch

**Phase D** — major-glitch logic level. Extends Phase B #5's `OP_GLITCH_LEVEL_AT_LEAST` handler to support `HybridMajorGlitches` (threshold 3) and `NoLogic` (threshold 4). Phase A's enum space already includes these values; Phase D un-pins user input for them.

## Status

**Archived 2026-06-14.** HybridMajorGlitches and NoLogic are shipped, including
runtime JP-glitch flag coupling for glitch seeds, technique-macro
reclassification, the NoLogic `can_place` short-circuit, and the documented
follow-up carve-outs for remaining nested OWG disjuncts.

## Read these in order

| File | Purpose | Status |
|---|---|---|
| [proposal.md](proposal.md) | Why, what changes, capabilities, impact | ✅ authored |
| [specs/randomizer-core/spec.md](specs/randomizer-core/spec.md) | Settings axis un-pin (values 3, 4) + runtime JP-glitch coupling | ✅ archived |
| [specs/randomizer-logic/spec.md](specs/randomizer-logic/spec.md) | OP_GLITCH_LEVEL_AT_LEAST extension, technique macros, NoLogic short-circuit | ✅ archived |
| [tasks.md](tasks.md) | Implementation checklist | ✅ complete / archived |

## Effort

Complete; see `tasks.md` for the shipped scope and deferred follow-ups.

## Dependencies

- Phase A and Phase B #5 are archived; this change is now archived on top of
  those baselines.
