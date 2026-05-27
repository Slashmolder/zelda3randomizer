# add-rando-auto-tracker

**Phase D** — auto-tracker server. Optional local TCP listener that emits state-change events for external tracker clients (EmoTracker, PopTracker, custom OBS overlays). Subscribe-only; localhost-bound by default; no game-state mutation.

## Status

**Phase D proposal-only stub.** Authored: 2026-05-26. Phase D cannot start before Phase A archives.

## Read these in order

| File | Purpose | Status |
|---|---|---|
| [proposal.md](proposal.md) | Why, what changes, capabilities, impact | ✅ authored |
| [specs/randomizer-ui/spec.md](specs/randomizer-ui/spec.md) | Server lifecycle + protocol + security | 🪵 minimal stub deltas |
| `tasks.md` | Implementation checklist | ⏳ deferred (Phase D apply-time) |

## Effort

**2-3 weeks of focused work.** Protocol design + cross-platform socket plumbing + integration with `reachability_state_counter` event signal.

## Dependencies

- Phase A archived.
- Benefits from Phase B #2 trackers (shares the `reachability_state_counter` advance signal).
