# add-rando-major-glitch

**Phase D** — major-glitch logic level. Extends Phase B #5's `OP_GLITCH_LEVEL_AT_LEAST` handler to support `HybridMajorGlitches` (threshold 3) and `NoLogic` (threshold 4). Phase A's enum space already includes these values; Phase D un-pins user input for them.

## Status

**Phase D proposal-only stub.** Authored: 2026-05-26. Phase D cannot start before Phase B #5 (`add-rando-trick-logic-and-axes`) archives.

## Read these in order

| File | Purpose | Status |
|---|---|---|
| [proposal.md](proposal.md) | Why, what changes, capabilities, impact | ✅ authored |
| [specs/randomizer-core/spec.md](specs/randomizer-core/spec.md) | Settings axis un-pin (values 3, 4) | 🪵 minimal stub deltas |
| [specs/randomizer-logic/spec.md](specs/randomizer-logic/spec.md) | OP_GLITCH_LEVEL_AT_LEAST extension + NoLogic short-circuit | 🪵 minimal stub deltas |
| `tasks.md` | Implementation checklist | ⏳ deferred (Phase D apply-time) |

## Effort

**2-3 weeks of focused work.** HybridMG predicate authoring is the bulk.

## Dependencies

- Phase A archived.
- **Phase B #5 trick-logic-and-axes archived first** — Phase D extends Phase B's `OP_GLITCH_LEVEL_AT_LEAST` handler.
