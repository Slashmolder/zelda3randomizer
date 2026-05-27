# add-rando-hints

Phase B Slice 5. Adds the **new `randomizer-hints` capability** plus hint NPC sprite-handler dispatch + spoiler integration. Sahasrahla telepathic, storyteller, bookshelves, Murahdahla.

## Status

**Proposal-only stub.** Authored: 2026-05-26. Specs deltas + `tasks.md` deferred to `/openspec-explore` + `/openspec-propose` at apply-time.

**Stub-only because**: the new-capability decision (peer vs. extension) needs a design.md call; hint-text format depends on ALTTPR upstream grep; dialogue-ID range carve-out needs a grep against `src/messaging.c`; hints-axis default policy depends on goal interaction.

## Read these in order

| File | Purpose | Status |
|---|---|---|
| [proposal.md](proposal.md) | Why, what changes, capabilities, impact | ✅ authored |
| [specs/randomizer-hints/spec.md](specs/randomizer-hints/spec.md) | **NEW** capability: generation pipeline + per-source-NPC contracts | 🪵 minimal stub deltas |
| [specs/randomizer-placement/spec.md](specs/randomizer-placement/spec.md) | Hint-NPC dispatch routing | 🪵 minimal stub deltas |
| [specs/randomizer-core/spec.md](specs/randomizer-core/spec.md) | `hints` settings axis + spoiler section | 🪵 minimal stub deltas |
| `design.md` | New-capability decision; dialogue-ID range; hints-axis default policy | ⏳ deferred (apply-time) |
| `tasks.md` | Implementation checklist | ⏳ deferred (apply-time) |

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

1. `/openspec-explore add-rando-hints` — settle new-capability-vs-extension decision; grep ALTTPR PHP for hint-text format; find unused dialogue-ID band in `src/messaging.c`; survey hint-NPC sprite handlers.
2. `/openspec-propose` to finalize spec deltas + design.md.
3. `/openspec-apply` to walk through tasks.
4. `/openspec-archive` when done.
