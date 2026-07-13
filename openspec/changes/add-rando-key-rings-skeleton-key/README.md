# add-rando-key-rings-skeleton-key

Adds OoTR-inspired per-dungeon key rings and an optional logic-neutral Skeleton
Key to the native randomizer.

## Status

**Implementation complete; ready for owner playtest (48/55).** The remaining
six gameplay checks and archive/merge closeout are intentionally owner-gated.
The implementation was reconciled against the item pool, predicate VM,
dungeon-key RAM, door-shuffle oracle, sidecar, snapshot, spoiler, tracker, and
native-window paths.

## Read these in order

| File | Purpose |
|---|---|
| [proposal.md](proposal.md) | Motivation, scope, capability impact, and non-goals |
| [design.md](design.md) | Grounded implementation decisions and compatibility matrix |
| [specs/randomizer-key-items/spec.md](specs/randomizer-key-items/spec.md) | New feature's primary behavioral contract |
| [specs/](specs/) | Integration deltas for settings, placement, logic, runtime, save, shuffles, and UI |
| [tasks.md](tasks.md) | Implementation and validation checklist |

## Headline decisions

- `key_rings=off|random|all`, default `off`.
- A selected dungeon family's complete shuffled small-key multiset becomes one
  dungeon-specific Key Ring plus junk padding; itemized pot and enemy keys are
  included in the collapse.
- `random` uses a salted deterministic selection and guarantees a non-empty,
  non-total subset of eligible families.
- Rings normalize off when small keys are effectively Vanilla or Retro generic
  keys are active.
- `skeleton_key=true` adds exactly one bonus item. It opens every small-key door
  without spending a key, never opens a big-key door, and is deliberately absent
  from generation logic.
- Ownership caches are reconstructed from the placement table plus checked
  bitmap; no separate authoritative ownership save field is introduced.
- The canonical settings blob grows from 30 to 31 bytes, requiring the normal
  generator/share/sidecar/corpus version cascade.

The implementation checklist contains 55 tasks: 48 implementation/automated
validation tasks complete, with six owner playtests and archive closeout pending.
