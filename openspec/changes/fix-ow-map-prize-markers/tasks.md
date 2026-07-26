# Tasks: fix-ow-map-prize-markers

## 1. Grounding (done at proposal time — re-verify at implementation start)

- [x] 1.1 Re-read `WorldMap_HandleSprites` / `WorldMap_AddSprite` /
      `OverworldMap_CheckForPendant` / `OverworldMap_CheckForCrystal` against
      current `src/messaging.c`; confirm the per-`k` marker inventory in
      `design.md` still holds
- [x] 1.2 Re-confirm slot→dungeon from `kPendantBitMask`/`kCrystalBitMask` ×
      `kDungeonCrystalPendantBit`, and the prize-id→`tab` derivation from
      `PrizeShuffle_Run`'s identity assignment

## 2. Rando module

- [x] 2.1 Promote the `kPrize_*` enum from `rando_shuffles.c` to
      `rando_shuffles.h` (second consumer)
- [x] 2.2 Export the vanilla marker data `messaging.c` owns — the seven
      `kOwMapCrystal*_tab` arrays (via one pointer table) and
      `kOverworldMapData` — non-static, in the `kBirdTravel_*` style, so the
      rando path and its oracle share one source
- [x] 2.3 New `src/rando/ow_map_prizes.{c,h}`: slot→dungeon table for
      `k ∈ {3, 6, 7}`, prize-id→(`tab`, number tile) resolution read out of the
      vanilla tables, `prize_shuffle`/NULL-settings gating (fail closed), and
      the checked-location knowledge gate
- [x] 2.4 Identity-assignment oracle `RandoOwMap_SelfCheck`: for every prize
      marker slot in every affected `k`, the vanilla assignment must resolve to
      the vanilla `tab` word and number tile; register in the `ui` group

## 3. Engine seam

- [x] 3.1 Collapse the seven copy-pasted marker blocks in
      `WorldMap_HandleSprites` into one loop over slots 0..6 (sprite `14 - i`),
      preserving vanilla behavior exactly
- [x] 3.2 Add the rando hook: substitute the resolved `tab` BEFORE the
      `t = tab >> 8` branch so the unknown form reuses vanilla's `kOwMap_tab2`
      blink path
- [x] 3.3 Thread the crystal-number override through `WorldMap_AddSprite`
      (`0` = vanilla `kOverworldMapData[spr - 8]`)

## 4. Guards, build wiring, docs

- [x] 4.1 Allowlist `src/rando/ow_map_prizes.c` for
      `Rando_GetDungeonPrizeAssignment` in
      `assets/scripts/check_knowledge_consumers.py`, with the written
      justification the spec requires
- [x] 4.2 Register `src/rando/ow_map_prizes.c` in `zelda3.vcxproj` (the
      Makefile globs `src/rando/*.c`; MSBuild does not)
- [x] 4.3 `docs/randomizer.md`: player-facing behavior, including the
      deliberate entrance-shuffle position choice

## 5. Validation

- [x] 5.1 MSVC build clean; WSL `gcc -Werror` build clean
- [x] 5.2 `--rando-selftest` all groups OK, including the new `ui` oracle
- [x] 5.3 `check_knowledge_consumers.py` and the rest of
      `run_rando_validation.py full` pass (`full` rather than `ci`: `ci`
      requires a genuinely assetless checkout, and this worktree needed the
      mirrored `assets/rando/*.gen.*` inputs for the corpus to run every entry)
- [x] 5.4 Corpus regen shows **0** digest changes vs. the pre-change baseline
      (display-only claim verified, not asserted) — no `kGeneratorVersion` bump
- [ ] 5.5 Owner playtest: vanilla seed pause map unchanged; `prize_shuffle` seed
      shows the blinking red X at every prize dungeon, flips to the correct icon + crystal
      number after collecting that dungeon's prize; Inverted pause map sane

## 6. Close-out

- [x] 6.1 Independent fresh-eyes review of the diff before declaring done
      (3 reviewers, distinct lenses; findings fixed in fcb6aa94 + d762eb2e:
      the oracle re-derived instead of driving the resolver and was circular,
      docs/spec contradicted decision D3, and the unknown marker is a red X
      rather than the "?" every artifact claimed)
- [ ] 6.2 Reconcile these deltas against as-built source, then
      `openspec archive fix-ow-map-prize-markers --yes` on the branch
