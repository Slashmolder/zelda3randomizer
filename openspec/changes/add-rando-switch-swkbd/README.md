# add-rando-switch-swkbd

Phase B §9.1c. Implements the libnx software-keyboard wrapper that Phase A re-scoped to Phase B per the Switch-manual-gate precedent. Switch-only — no PC code path changes.

## Status

**Proposal-only stub.** Authored: 2026-05-26. Specs deltas + `tasks.md` deferred to `/openspec-explore` + `/openspec-propose` at apply-time.

**Stub-only because**: libnx swkbd API has changed across DevKitPro versions; the exact `swkbdCreate` call sequence + character-set-restriction mechanism need verification against the current Switch SDK at apply-time.

## Read these in order

| File | Purpose | Status |
|---|---|---|
| [proposal.md](proposal.md) | Why, what changes, capabilities, impact | ✅ authored |
| [specs/randomizer-ui/spec.md](specs/randomizer-ui/spec.md) | libnx swkbd wrapper contract | 🪵 minimal stub deltas |
| `design.md` | swkbd API sequence; character-set restriction | ⏳ deferred (apply-time) |
| `tasks.md` | Implementation checklist | ⏳ deferred (apply-time) |

## Effort

**3-5 days of focused work.** Small wrapper; manual Switch-dev-unit smoke confirms.

## Switch-manual-gate disclosure

Per `add-randomizer-support / tasks.md §12.3b`, Switch builds are NOT part of per-PR CI. This change can only be archived after a Switch dev-unit smoke confirms the swkbd flow works end-to-end. The release-cut Switch verification gate also applies.

## Dependencies

- **Phase A archived first.** Deltas `randomizer-ui` post-archive.
- **No upstream slice dependency.** Standalone Phase B change.
- **No PC code impact.** PC builds are unaffected; CI continues to run for PC platforms only.

## When work starts

1. `/openspec-explore add-rando-switch-swkbd` — verify libnx swkbd API against current DevKitPro; survey character-set restriction mechanisms; survey existing `RandoTextField` consumer API.
2. `/openspec-propose` to finalize spec deltas + design.md.
3. `/openspec-apply` to walk through tasks.
4. `/openspec-archive` when done — requires Switch dev-unit smoke pass.
