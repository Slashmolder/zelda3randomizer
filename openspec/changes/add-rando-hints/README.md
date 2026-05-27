# add-rando-hints

Phase B Slice 5. Adds the **new `randomizer-hints` capability** plus hint NPC sprite-handler dispatch + spoiler integration. Sahasrahla telepathic, storyteller, bookshelves, Murahdahla.

## Status

**Fully authored.** Authored: 2026-05-26. Promoted from stub with design.md recording the new-capability decision (peer to `randomizer-core`), dialogue-ID range carve strategy, and hints-axis goal-aware default.

## Read these in order

| File | Purpose | Status |
|---|---|---|
| [proposal.md](proposal.md) | Why, what changes, capabilities, impact | ✅ authored |
| [design.md](design.md) | New-capability decision; dialogue-ID range; hints-axis default policy; per-source generation algorithm | ✅ authored |
| [specs/randomizer-hints/spec.md](specs/randomizer-hints/spec.md) | **NEW** capability: generation pipeline + per-source-NPC contracts | ✅ authored |
| [specs/randomizer-placement/spec.md](specs/randomizer-placement/spec.md) | Hint-NPC dispatch routing | ✅ authored |
| [specs/randomizer-core/spec.md](specs/randomizer-core/spec.md) | `hints` settings axis + spoiler section | ✅ authored |
| [tasks.md](tasks.md) | Implementation checklist (12 sections, ~50 tasks) | ✅ authored |

## Effort

**2-3 weeks of focused work.** Text-engine integration is the long pole; the per-source-NPC text generation is mostly straight PHP-to-C porting.

## Key upstream references

- `../alttp_vt_randomizer/app/Services/HintService.php` (177 lines — verified)
- `../alttp_vt_randomizer/app/Text.php` (1110 lines — verified; hint text body)
- Combined subsystem: ≈ 1287 lines (source doc undercounted as "~1000")

## Dependencies

- **Phase A archived first.** Deltas multiple specs post-archive.
- **No upstream slice dependency.** Could ship any time. Particularly useful for Triforce Hunt seeds (which are "almost unplayable without hints" per `docs/randomizer_phase_b.md` Slice 5 rationale).

## When work starts

1. `/openspec-apply add-rando-hints` — walk through `tasks.md` task-by-task. Start with the dialogue-ID range carve (Section 1.2) before authoring hint text — it's the dependency for everything downstream.
2. Per-source PR strategy: ship Sahasrahla first, then storyteller, then bookshelves, then Murahdahla. Each is a separate translation pass with its own audit.
3. `/openspec-archive` when done.
