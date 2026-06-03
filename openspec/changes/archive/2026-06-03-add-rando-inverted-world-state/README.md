# add-rando-inverted-world-state

Phase B Slice 2. Translates ALTTPR's Inverted region logic (2977 PHP lines, recursive) into YAML, activates `RegionRemap`, populates `world_state_filter`, and wires Phase A1 audit Bug #12 (starting-inventory call site).

## Status

**Fully authored.** Authored: 2026-05-26. Promoted from stub to full content after capturing translation discipline + RegionRemap overlay shape + Bug #12 call-site decision (`Module05_LoadFile` end) in design.md.

## Read these in order

| File | Purpose | Status |
|---|---|---|
| [proposal.md](proposal.md) | Why, what changes, capabilities, impact | ✅ authored |
| [design.md](design.md) | Translation discipline; RegionRemap overlay shape; Bug #12 call-site decision; world_state_filter encoding | ✅ authored |
| [specs/randomizer-logic/spec.md](specs/randomizer-logic/spec.md) | Inverted region graph + RegionRemap + filter contract | ✅ authored |
| [specs/randomizer-placement/spec.md](specs/randomizer-placement/spec.md) | Starting-inventory call-site (Bug #12) | ✅ authored |
| [specs/randomizer-ui/spec.md](specs/randomizer-ui/spec.md) | Picker un-gate | ✅ authored |
| [tasks.md](tasks.md) | Implementation checklist (15 sections, ~70 tasks) | ✅ authored |

Priming directory at [`assets/rando/logic_parts/inverted/`](../../../assets/rando/logic_parts/inverted/) holds 24 stub YAML files (one per upstream PHP file) ready for translation.

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

1. `/openspec-apply add-rando-inverted-world-state` to walk through `tasks.md` task-by-task. Translation work (Section 4) is the long pole.
2. Per memory `[[cluster-audit-cadence]]` — schedule a fresh-eyes audit IMMEDIATELY after the translation completes (Section 12). Every audit finds 5-10 new bugs.
3. `/openspec-archive` when done; specs merge into `openspec/specs/randomizer-{logic,placement,ui}/spec.md`.
