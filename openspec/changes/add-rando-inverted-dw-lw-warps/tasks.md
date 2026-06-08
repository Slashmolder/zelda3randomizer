# Tasks

Most of this change was already built; checked boxes are as-built (verified this session).

## 1. Warp data layers (pre-existing)

- [x] 1.1 Type-`0x82` warp records added per warp screen by `InvertedSecrets_Install` (`src/rando/inverted_entrances.c`), positioned under the rocks.
- [x] 1.2 Inverted overlay rows place the liftable rocks (`0x020F`/`0x0239`) at the warp positions and remove the Light-World counterparts (`src/rando/inverted_maps.c`, applied by `Overworld_ApplyInvertedTiles`).

## 2. Walk-in rendering fix (this session)

- [x] 2.1 Add `Overworld_EnsureInvertedWarpRock()` + `kInvertedWarpRocks` table (`src/overworld.c`); re-assert the current warp screen's rock tile, skipping when the tile is already the rock or the revealed warp `0x0212`.
- [x] 2.2 Call it from `Module09_00_PlayerControl` (runs every frame in the overworld walking state).
- [x] 2.3 Confirm the three layers (warp record pos / overlay rock pos / placer table) agree for all 9 entries.
- [x] 2.4 Confirm Light-World hardcoded-rock removal (`0x33`/`0x2F`) fires on both walk-in and full-rebuild entry (`Overworld_HandleOverlaysAndBombDoors`).

## 3. Verification

- [x] 3.1 Build clean: gcc `-Wall -Werror` and MSVC Release x64.
- [x] 3.2 `--rando-selftest` green; `chest_lookup.h` populated (fail-open guard).
- [x] 3.3 Fresh-eyes review (no new correctness findings; lift/re-cover, large-area sub-screens, VRAM, LW-side, inertness all cleared).
- [x] 3.4 Playtest: walk onto screen `0x73`, lift the rock, step on the warp → reach the Light World (user-confirmed; glove required, as intended).
- [ ] 3.5 Playtest the remaining warp screens (Village of Outcasts `0x50`, DW Death Mountain `0x4E`, Misery Mire `0x70`/`0x78`, `0x6F`, `0x75`, Turtle Rock `0x47`) and a full DW→LW→DW loop (confirm the return trip and Light-World rock absence).

## 4. Land

- [ ] 4.1 Squash the branch to one clean commit (the bush-retype + its revert cancel out — net change is `src/overworld.c` only) and merge to `main`.
- [ ] 4.2 Rebuild the main `zelda3.exe` from `main` source (the fix was built to the worktree bin).
- [ ] 4.3 Archive this change (`openspec archive add-rando-inverted-dw-lw-warps`) and sweep any doc/path references per the archive side-effects note.
