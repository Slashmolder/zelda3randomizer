# add-rando-customizer-mode

**Phase D** — customizer mode: manual per-location placement override + custom item pool composition. The dispatcher API is unchanged from Phase A; customizer just writes the placement table differently (skips assumed-fill, reads manifest).

## Status

**Phase D proposal-only stub.** Authored: 2026-05-26. Phase D cannot start before Phase A archives.

## Read these in order

| File | Purpose | Status |
|---|---|---|
| [proposal.md](proposal.md) | Why, what changes, capabilities, impact | ✅ authored |
| [specs/randomizer-core/spec.md](specs/randomizer-core/spec.md) | Customizer pipeline + canonical-serialization extension | 🪵 minimal stub deltas |
| [specs/randomizer-ui/spec.md](specs/randomizer-ui/spec.md) | Settings-screen toggle + manifest picker | 🪵 minimal stub deltas |
| `tasks.md` | Implementation checklist | ⏳ deferred (Phase D apply-time) |

## Effort

**2-3 weeks of focused work.** Validation + share-string-compatibility are the bulk; manifest reading is straightforward.

## Dependencies

- Phase A archived. Benefits from #2 trackers (manual placement easier to verify with an in-game tracker overlay). No strict dependency.
