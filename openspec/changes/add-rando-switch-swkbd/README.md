# add-rando-switch-swkbd

Phase B §9.1c. Implements the libnx software-keyboard wrapper that Phase A re-scoped to Phase B per the Switch-manual-gate precedent. Switch-only — no PC code path changes.

## Status

**Fully authored.** Authored: 2026-05-26. Promoted with design.md (sync wrapper, post-filter base32, cancel-preserves semantics).

## Read these in order

| File | Purpose | Status |
|---|---|---|
| [proposal.md](proposal.md) | Why, what changes, capabilities, impact | ✅ authored |
| [design.md](design.md) | Sync vs. async; charset restriction; cancel behavior; initial-value seeding | ✅ authored |
| [specs/randomizer-ui/spec.md](specs/randomizer-ui/spec.md) | libnx swkbd wrapper contract | ✅ authored |
| [tasks.md](tasks.md) | Implementation checklist (9 sections, ~25 tasks) | ✅ authored |

## Effort

**3-5 days of focused work.** Small wrapper; manual Switch-dev-unit smoke confirms.

## Switch-manual-gate disclosure

Per `add-randomizer-support / tasks.md §12.3b`, Switch builds are NOT part of per-PR CI. This change can only be archived after a Switch dev-unit smoke confirms the swkbd flow works end-to-end. The release-cut Switch verification gate also applies.

## Dependencies

- **Phase A archived first.** Deltas `randomizer-ui` post-archive.
- **No upstream slice dependency.** Standalone Phase B change.
- **No PC code impact.** PC builds are unaffected; CI continues to run for PC platforms only.

## When work starts

1. `/openspec-apply add-rando-switch-swkbd` — Section 1 (pre-flight DevKitPro/libnx version verification) settles SDK-shape questions first.
2. Manual Switch dev-unit smoke is the release-cut gate (per `tasks.md §12.3b`); no PC CI step.
3. `/openspec-archive` when done.
