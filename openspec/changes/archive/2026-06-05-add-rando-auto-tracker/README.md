# add-rando-auto-tracker

**Phase D** — auto-tracker server. Optional local TCP listener that emits state-change events for external tracker clients (EmoTracker, PopTracker, custom OBS overlays). Subscribe-only; localhost-bound by default; no game-state mutation.

## Status

**Implemented; pending owner real-client test.** Proposal authored 2026-05-26;
implemented 2026-06-04. Headless verification (build, guards, selftest gating,
default-off, loopback client catalog+state, multi-client, disconnect-safety) is
green. Remaining: the owner's real external-client test (EmoTracker / PopTracker /
OBS overlay with a loaded slot) before `openspec archive`.

## Read these in order

| File | Purpose | Status |
|---|---|---|
| [proposal.md](proposal.md) | Why, what changes, capabilities, impact | ✅ authored |
| [specs/randomizer-ui/spec.md](specs/randomizer-ui/spec.md) | Server lifecycle + protocol + security | ✅ authored |
| [design.md](design.md) | Transport / trigger / spoiler-safety decisions | ✅ authored |
| [tasks.md](tasks.md) | Implementation checklist | ✅ implemented (7.4 owner test pending) |

## Effort

Protocol design + cross-platform socket plumbing + integration with the
`reachability_state_counter` event signal. Implemented behind a default-off,
observation-only, localhost-only server — zero regression risk.

## Dependencies

- Phase A archived.
- Benefits from Phase B #2 trackers (shares the `reachability_state_counter` advance signal).
