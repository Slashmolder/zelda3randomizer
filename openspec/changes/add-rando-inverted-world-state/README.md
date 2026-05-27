# add-rando-inverted-world-state

Phase B Slice 2. Translates ALTTPR's Inverted region logic (2977 PHP lines, recursive) into YAML, activates `RegionRemap`, populates `world_state_filter`, and wires Phase A1 audit Bug #12 (starting-inventory call site).

## Status

**Proposal-only stub.** Authored: 2026-05-26. Specs deltas + `tasks.md` deferred to `/openspec-explore` + `/openspec-propose` at apply-time.

**Stub-only because**: ALTTPR translation surprises invalidate task detail written months in advance; RegionRemap overlay shape depends on findings during translation; Bug #12 call-site decision belongs in this change's design.md after a quick prototype.

## Read these in order

| File | Purpose | Status |
|---|---|---|
| [proposal.md](proposal.md) | Why, what changes, capabilities, impact | ✅ authored |
| `specs/randomizer-logic/spec.md` | Inverted region graph + RegionRemap + filter contract | ⏳ deferred (apply-time) |
| `specs/randomizer-placement/spec.md` | Starting-inventory call-site (Bug #12) | ⏳ deferred (apply-time) |
| `specs/randomizer-core/spec.md` | BuildItemPool Inverted branch | ⏳ deferred (apply-time) |
| `specs/randomizer-ui/spec.md` | Picker un-gate | ⏳ deferred (apply-time) |
| `design.md` | RegionRemap design + Bug #12 call-site decision | ⏳ deferred (apply-time) |
| `tasks.md` | Implementation checklist | ⏳ deferred (apply-time) |

## Effort

**4-6 weeks of focused work.** Source doc estimated 3-4 weeks but undercounted the upstream PHP source by ~2x: verified 2977 lines across 24 files (Inverted top-level + DarkWorld + LightWorld subdirs).

## Key upstream references

- `../alttp_vt_randomizer/app/Region/Inverted/` (2977 lines, 24 files)
- `../alttp_vt_randomizer/app/World/Inverted.php` (if exists; verify at apply-time)
- `../alttp_vt_randomizer/app/Support/ItemCollection.php` (43 macros — Phase A already translated; verify Inverted-specific additions during translation)

## Dependencies

- **Phase A archived first.** Deltas multiple specs post-archive.
- **No dependency on other Phase B slices.** Recommended ordering: ship after #4b Retro (Retro is much smaller and exercises the Phase B authoring pattern); helpful if #2 trackers has shipped (trackers surface reachability bugs during translation).

## When work starts

1. `/openspec-explore add-rando-inverted-world-state` to flesh out specs + tasks against fresh PHP-grep findings.
2. Iterate `/openspec-propose` to finalize spec deltas + design.md.
3. `/openspec-apply` to walk through `tasks.md` task-by-task.
4. `/openspec-archive` when done; specs merge into `openspec/specs/randomizer-{logic,placement,core,ui}/spec.md`.
