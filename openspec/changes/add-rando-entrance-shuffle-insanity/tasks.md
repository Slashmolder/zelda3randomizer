# Tasks — add-rando-entrance-shuffle-insanity

Follow-up carve-out from `add-rando-entrance-shuffle` (archived). Insanity's
deterministic permutation + logic shipped with the parent (generation-only); this
change is the runtime that makes it playable.

## 1. Cave-arrival asset fork
- [ ] 1.1 Source/derive the per-cave arrival positional data a decoupled cave
  entrance needs (caves use a cached entry-snapshot branch; arrival is not
  derivable from the existing exit tables).
- [ ] 1.2 Wire the runtime arrival replay into the decoupled cave path in
  `shuffle_entrance.c` / `rando.c` (productionize the `claude/insanity-arrival-spike`
  capture/replay).

## 2. Preset activation
- [ ] 2.1 Activate the **Insanity** preset (the `decoupled` axis) in the settings
  UI / preset buttons as a shipped mode.

## 3. Verify
- [ ] 3.1 Corpus: re-run after the runtime lands; confirm non-Insanity digests are
  byte-identical and decide whether `kGeneratorVersion` needs a bump.
- [ ] 3.2 Playtest: an Insanity seed is beatable for at least one goal — every
  decoupled cave/dungeon arrival deposits the player correctly (no softlock).
- [ ] 3.3 Fresh-eyes audit before archive (model↔runtime match is the risk).
