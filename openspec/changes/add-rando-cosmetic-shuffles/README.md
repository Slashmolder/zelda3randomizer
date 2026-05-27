# add-rando-cosmetic-shuffles

**Phase D** — palette / sprite / music shuffles. Cosmetic only; SHALL NOT affect placement, `settings_hash`, or determinism. Drives off a separate `cosmetic_seed` per-slot field.

## Status

**Phase D proposal-only stub.** Authored: 2026-05-26. Phase D cannot start before Phase A archives.

## Read these in order

| File | Purpose | Status |
|---|---|---|
| [proposal.md](proposal.md) | Why, what changes, capabilities, impact | ✅ authored |
| [specs/randomizer-shuffles/spec.md](specs/randomizer-shuffles/spec.md) | Cosmetic-shuffle contract (extends Phase A drafted requirement) | 🪵 minimal stub deltas |
| [specs/randomizer-save/spec.md](specs/randomizer-save/spec.md) | `cosmetic_seed` slot-header field | 🪵 minimal stub deltas |
| `tasks.md` | Implementation checklist | ⏳ deferred (Phase D apply-time) |

## Effort

**3-4 weeks of focused work.** Each axis (palette / sprite / music) is independent.

## Dependencies

- Phase A archived. No Phase B / C dependency.
