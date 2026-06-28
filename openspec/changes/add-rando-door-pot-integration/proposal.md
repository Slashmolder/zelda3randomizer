# Door shuffle x Pot shuffle integration

## Why

Pot shuffle currently normalizes to `off` whenever door shuffle is effective. That
kept the first pot-shuffle release safe because the door oracle and key-door prover
did not model pot locations. It also blocks the natural long-term composition:
door-shuffled dungeons should be able to contain active pot checks, including pot
keys, without certifying a seed against the wrong key-door graph.

## What Changes

- Keep cave-entrance shuffle as a pot-forced-off axis, but stop forcing pots off
  under effective door shuffle.
- Generate a pot-door bridge from local pot and key-depth artifacts. Static committed
  door tables stay asset-independent; the local generated logic data owns the
  additional pot-door rows.
- Teach the door key prover that active pot locations are key-source locations and
  that itemized pot-key drops are no longer free drop keys.
- Teach `OP_DOORS_LOC_REACHABLE` to resolve generated pot locations through the same
  door oracle as existing door-controlled locations.
- Update placement, spoiler, UI, docs, self-checks, and corpus entries so
  door+pot seeds are real pot-active seeds instead of door-only-equivalent seeds.

## Out of Scope

- Cave-entrance shuffle x pot shuffle remains forced off.
- Cross-dungeon door shuffle, entrance shuffle x door shuffle, and non-basic door
  shuffle intensities remain unchanged.
- The static committed `door_tables.gen.*` tables do not start depending on
  gitignored ROM-derived pot artifacts.

## Risks

- Pot-door row generation must fail closed when local pot artifacts exist but cannot
  map dungeon pots into door-table regions.
- Key accounting must stay synchronized across the door prover, logic oracle, and
  placement pool.
- Conservative region mapping for non-key pots can over-gate some loot or empty
  pots; over-gating is acceptable for v1, under-gating is not.
