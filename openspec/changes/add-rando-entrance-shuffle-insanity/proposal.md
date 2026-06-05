## Why

Follow-up to `add-rando-entrance-shuffle`. That change shipped the composable
entrance-shuffle axes (cave + dungeon entrance shuffle, `coupled` default,
`cross_category`) and the **Simple / Restricted / Crossed** presets, all owner-
playtested.

It deferred the **Insanity** preset — the `decoupled` axis, per-endpoint
cave/dungeon cross-mapping where any overworld entrance may map to any interior
(including cave↔dungeon). Insanity's **generation + logic landed** (the
deterministic per-endpoint permutation plus the cave-arrival capture/replay
design validated on the `claude/insanity-arrival-spike` branch), but the
**runtime cave-arrival replay path is blocked on an asset fork**: the per-cave
arrival positional data is not derivable from the existing exit tables (caves use
a cached entry-snapshot branch), so a decoupled cave entrance has no place to
deposit the player on arrival. It was carved out of the parent's
`randomizer-shuffles` delta so the archived baseline reflects only playable modes.

## What Changes

- Land the **cave-arrival asset fork** (the positional/arrival data a decoupled
  cave entrance needs) and wire the runtime replay, so Insanity seeds are
  playable end-to-end rather than generation-only.
- Activate the **Insanity** preset (the `decoupled` axis) in the settings UI /
  presets as a shipped mode.

## Capabilities

### New Capabilities

- `randomizer-shuffles`: ADDED "Insanity (decoupled) entrance mode" — per-endpoint
  cave/dungeon cross-mapping, playable once the cave-arrival fork lands.

## Impact

- **Code**: the cave-arrival asset fork (positional arrival data) + the runtime
  replay (`shuffle_entrance.c` decoupled path, the arrival capture/replay in
  `rando.c`), and Insanity-preset activation in `rando_window`.
- **Determinism**: the `decoupled` axis already serializes into the canonical
  settings, so generation is in place; re-run the corpus when the runtime lands to
  confirm whether `kGeneratorVersion` needs a bump (placement should be unchanged
  for non-Insanity seeds).
- **Dependency**: `add-rando-entrance-shuffle` (archived) provides the axes,
  permutation, and the two-mechanism reachability this preset rides on.
