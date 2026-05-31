## 1. Pre-flight + gfx/palette spike (load-bearing — do this FIRST)

- [ ] 1.1 Pin the ALTTPR upstream commit: `git -C ../alttp_vt_randomizer rev-parse HEAD`; re-read `app/Rom.php setInvertedMode()` and transcribe the exact pyramid/HC writes (ExtraHole 1842-1849; exits 0x06/0x37/0x3D; door 0x35) to confirm the prior-research values against current source.
- [ ] 1.2 Read `src/rando/inverted_maps_apply.c:43-57` + the original `invertedmaps.asm` screen-0x1B `.map1B` data (via `assets/scripts/gen_inverted_maps.py` source) and enumerate every map16 block the overlay paints and the gfx-set/palette it assumes.
- [ ] 1.3 **SPIKE:** prototype a gfx/palette approach that renders the screen-0x1B pyramid + Ganon-hole blocks without corruption alongside the castle gfx. Evaluate (a) Inverted-specific gfx-set load on 0x1B, (b) map16-block/palette-slot remap to castle-valid tiles, (c) extract castle-compatible variant blocks. Acceptance: clean render on a full screen rebuild AND walking-scroll (F12 BG dump). If none is tractable in a bounded spike, STOP and record the finding — the change pauses with Ganon in the DW pyramid.
- [ ] 1.4 Confirm the existing Inverted logic graph (`logic_parts/inverted/`) + `Goal_IsCompletable` don't encode the DW-pyramid screen id (expected: they gate on crystals + Agahnim 2, not a screen) — record in this change's notes.

## 2. Screen-0x1B inverted pyramid overlay

- [ ] 2.1 Implement the spike's chosen gfx/palette mechanism (gated on `Rando_GetActiveWorldState() == Inverted`).
- [ ] 2.2 Un-suppress the screen-0x1B overlay in `src/rando/inverted_maps_apply.c` (remove the `if (scr == 0x1B) return;` early-out) and regenerate/adjust the `kInvertedMapData` 0x1B block via `gen_inverted_maps.py` so it carries the corrected blocks.
- [ ] 2.3 Verify the overlay renders correctly via F12 BG dump on every entry method (mirror, cave exit, flute, S&Q) and walking-scroll; no garbage/mint-green tiles.

## 3. Fall-hole relocation to area 0x1B

- [ ] 3.1 Add the ExtraHole entry (Area=0x001B, Map16=0x0140, Entrance=0x7B) to the fall-hole extraction in `assets/compile_resources.py` (sorted-by-entrance-id build) so `kFallHole_*` (assets 127/128/129) gains the 20th entry placed by sort.
- [ ] 3.2 Bump the hardcoded scan bound (`i = 36/2`) in `Overworld_GetPitDestination` (`src/overworld.c:3299-3321`) to cover 20 entries; gate the new entry's consumption on the Inverted world-state so non-Inverted fall-through is byte-identical.
- [ ] 3.3 Regenerate `zelda3_assets.dat` + `src/rando/vanilla_assets_hash.h`; run `assets/scripts/run_rando_corpus.py` to confirm placement digests are unchanged (Ganon's drop is game data, not placement); bump `kGeneratorVersion` ONLY if a digest moves.

## 4. Carved hole + exit-data + flute slot

- [ ] 4.1 Add an Inverted variant of `CreatePyramidHole` (`src/overworld.c`) to carve the area-0x1B hole (mirror the DW-pyramid carve footprint at the 0x1B position), gated on Inverted.
- [ ] 4.2 Add the pyramid/HC exit-data override rows (exits 0x06/0x37/0x3D + door 0x35 area/pos/id) to `kInvertedOverrides[]` in `src/rando/inverted_entrances.c`, with any new `g_shadows[]` registrations; values verified against `Rom.php` in task 1.1.
- [ ] 4.3 Add the flute slot-8 override row (`kAsset_BirdTravel_ScreenIndex, 2, 8, 0x001B`) to `kInvertedOverrides[]` (replacing the current "intentionally left vanilla" comment).

## 5. Death-to-Ganon respawn

- [ ] 5.1 Port `darkworldspawn.asm SetDeathWorldChecked_Inverted` `.castle` into `src/messaging.c Death_Func15`: when the Ganon-fight respawn condition holds (indoors + DungeonID==0xFF + room 0x000 + respawn flag) under Inverted, force the LIGHT world; otherwise keep the existing `savegame_is_darkworld = 0x40` Inverted death path.
- [ ] 5.2 Verify the normal Inverted DW death respawn (the existing `0x40` path) is untouched, and that death-to-Ganon now respawns at the LW castle.

## 6. Verification + record-keeping

- [ ] 6.1 End-to-end Inverted playtest: fall into the 0x1B hole → land in the Ganon room; defeat/attempt Ganon; die → respawn at the castle (LW); flute slot 8 → arrive at 0x1B; overlay clean on all entry methods.
- [ ] 6.2 Confirm non-Inverted byte-identity: build + `--rando-selftest` + `check_audit_guard.py --strict`; spot-check Open/Standard Ganon at the DW pyramid is unchanged.
- [ ] 6.3 Fresh-eyes audit pass (per `CLAUDE.md` cadence) on the full diff before declaring done.
- [ ] 6.4 Update `docs/inverted_alttpr_gaps.md` (move §A1 to done; drop the §C2 non-gap entries) and the memory note `inverted-entrance-topology-source` to reflect the relocation.
