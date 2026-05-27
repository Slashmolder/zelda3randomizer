# add-rando-entrance-shuffle

**Phase C** — entrance shuffle. The four-mode (Simple / Restricted / Crossed / Insanity) entrance permutation that maps each overworld door to a different interior. Uses the `RegionRemap` overlay scaffolded in Phase A (`src/rando/rando_logic.c`) and first activated in Phase B #4a Inverted (Light↔Dark swap).

## Status

**Phase C proposal-only stub.** Authored: 2026-05-26.

**Stub-only because**: Phase C cannot start before Phase B's #4a Inverted ships (RegionRemap must be production-grade); ALTTPR's entrance-shuffle algorithm needs careful study; per-mode complexity (especially Insanity) needs a prototype.

## Read these in order

| File | Purpose | Status |
|---|---|---|
| [proposal.md](proposal.md) | Why, what changes, capabilities, impact | ✅ authored |
| [specs/randomizer-shuffles/spec.md](specs/randomizer-shuffles/spec.md) | 4-mode shuffle contract (extends Phase A's drafted requirement) | 🪵 minimal stub deltas |
| [specs/randomizer-logic/spec.md](specs/randomizer-logic/spec.md) | RegionRemap entrance-overlay shape | 🪵 minimal stub deltas |
| [specs/randomizer-save/spec.md](specs/randomizer-save/spec.md) | `TAIL_ENTRANCE_MAP` TLV (first realized Phase A "Forward-compat reserve" TLV) | 🪵 minimal stub deltas |
| `design.md` | Per-mode algorithm; goal-preservation strategy; overlay shape | ⏳ deferred (Phase C apply-time) |
| `tasks.md` | Implementation checklist | ⏳ deferred (Phase C apply-time) |

## Effort

**4-8 weeks of focused work.** Insanity mode is the long pole (the algorithm may retry many permutations before goal-reachability holds).

## Dependencies

- **Phase A archived first**: deltas multiple specs post-archive.
- **Phase B #4a Inverted archived first**: RegionRemap must be production-grade before entrance shuffle layers on top.
- **Benefits from Phase B #2 trackers + #6 hints**: entrance-shuffle play is much more enjoyable with a per-door tracker and hints.

## Key upstream references

- `../alttp_vt_randomizer/app/EntranceRandomizer.php` — the PHP entrypoint. **Note**: per `CLAUDE.md` claim-grounding section, this file's docstring incorrectly says "we use mt_rand"; the actual code shells out to a Python script. Phase C translation needs to study the actual entrance-randomizer Python tool referenced by `EntranceRandomizer.php`.

## When work starts

1. Wait for Phase A archive + Phase B #4a Inverted archive.
2. `/openspec-explore add-rando-entrance-shuffle` — verify RegionRemap shape post-#4a; survey ALTTPR entrance-randomizer tool; prototype Simple mode end-to-end before specing the harder modes.
3. `/openspec-propose` to finalize.
4. `/openspec-apply` to walk through tasks.
5. `/openspec-archive` when done.

## Future hooks

After entrance shuffle ships, the RegionRemap overlay machinery is mature enough to support arbitrary topology changes. Future phases could extend it to:
- Multi-world / co-op (cross-player entrance pairing).
- Single-screen-shuffle (sub-overlay level — every screen transition independently shuffled, even more chaotic than Insanity).

These are NOT scoped here; documented as forward-looking notes.
