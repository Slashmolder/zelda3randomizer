> **AS-BUILT PIVOT — HOLE-ONLY RELOCATION SHIPPED (2026-06-07, branch
> `claude/inverted-ganon-holeonly`).** The faithful-facade approach below stayed
> paused (it needs unlicensed custom art — see the spike note). Instead a **no-art
> hole-only** relocation shipped: render ONLY a Ganon pit at screen 0x1B (a 2-tone
> dark diamond from solid castle tile 0x037, via a `kMap16ToMap8`/asset-70 runtime
> shadow), make it fall to Ganon (per-char pit-attr override in `tile_detect.c` +
> an `Overworld_GetPitDestination` special-case → entrance 0x7B), repoint flute slot
> 8 `0x5B→0x1B` (faithful, inert in the 8-spot menu), and REMOVE the DW-pyramid hole
> (a pyramid fall no longer matches its Ganon entries → Houlihan fallback; the
> animated carve is gated to screen 0x1B) so Ganon lives ONLY under the LW castle.
> Death-respawn unchanged (existing spawn menu). All Inverted-gated; non-Inverted
> byte-identical; build/-Werror, `--rando-selftest`, corpus 110/110, and the
> audit/determinism/no-embedded-data guards all green. Playtest-confirmed: pit
> renders, fall→Ganon, death recoverable, LW reachable (DW→LW warps are in `main`).
> Deferred: faithful facade, 9th flute-menu spot, walking-scroll verification. The
> detailed faithful tasks below are the (superseded) faithful-facade path.

## 1. Pre-flight + gfx/palette spike (load-bearing — do this FIRST)

> **SPIKE OUTCOME: PAUSED-AT-SPIKE (2026-06-06).** The load-bearing screen-0x1B
> overlay **cannot render cleanly using vanilla assets** — confirmed by an offline
> renderer (decompressed real gfx + the exact 4bpp `Do3To4High/Low` palette model
> from `LoadBackgroundGraphics`), cross-validated against the known DW-pyramid look.
> The overlay (facade **and** hole) is authored for the DW-pyramid palette and shares
> palette rows — *including LOW halves used by the base castle stonework* — with the
> base Hyrule-Castle tiles, so the conflict is irreducible without ALTTPR's custom
> non-vanilla graphics (`z3randomizer/data/sheet73.gfx` + map16 block redefinitions
> + 1 added palette color, `Rom.php:1798-1879`). That is a large gfx-pipeline change
> against the fork's vanilla-extraction + no-committed-data model, so per task 1.3's
> STOP rule the change is paused: the `if (scr == 0x1B) return;` suppression stays,
> Ganon stays in the DW pyramid. Phases 2–6 are **blocked** on this. Full findings +
> the most-promising future approach are in `spike-findings.md` (this folder).

- [x] 1.1 Pin the ALTTPR upstream commit + transcribe the exact pyramid/HC writes. <!-- done: ALTTPR 219fcafd029dab597b8db400efafd8f56f8b4edb, z3rando dcb0a2b; setInvertedMode read; ExtraHole/exits/door/flute values captured in spike-findings.md -->
- [x] 1.2 Enumerate every map16 block `.map1B` paints + the gfx-set/palette it assumes. <!-- done: 27 facade + 12 hole blocks enumerated; screen 0x1B loads main theme 0x20 + aux theme 36, bgpal 2 (mode 0); see spike-findings.md -->
- [x] 1.3 **SPIKE — PAUSED.** No vanilla-asset approach renders cleanly; faithful fix needs ALTTPR custom gfx (large change). STOP per the spike rule. <!-- done: offline render proves mint-green facade+hole; suppression left in place; see spike-findings.md + the PNG evidence -->
- [x] 1.4 Confirm Inverted logic does not encode the DW-pyramid screen id. <!-- done: logic_parts/inverted gates Ganon on crystals+Agahnim and removeItem("Ganon") from DW NorthEast (NorthEast.yaml:16,94-99); no screen id anywhere — no logic change needed regardless of Ganon's physical location -->

## 2. Screen-0x1B inverted pyramid overlay  — BLOCKED on the §1.3 spike (no clean vanilla render)

- [ ] 2.1 Implement the spike's chosen gfx/palette mechanism (gated on `Rando_GetActiveWorldState() == Inverted`).
- [ ] 2.2 Un-suppress the screen-0x1B overlay in `src/rando/inverted_maps_apply.c` (remove the `if (scr == 0x1B) return;` early-out) and regenerate/adjust the `kInvertedMapData` 0x1B block via `gen_inverted_maps.py` so it carries the corrected blocks.
- [ ] 2.3 Verify the overlay renders correctly via F12 BG dump on every entry method (mirror, cave exit, flute, S&Q) and walking-scroll; no garbage/mint-green tiles.

## 3. Fall-hole relocation to area 0x1B  — BLOCKED on the §1.3 spike (the drop would land in an unrendered/garbage screen)

- [ ] 3.1 Add the ExtraHole entry (Area=0x001B, Map16=0x0140, Entrance=0x7B) to the fall-hole extraction in `assets/compile_resources.py` (sorted-by-entrance-id build) so `kFallHole_*` (assets 127/128/129) gains the 20th entry placed by sort.
- [ ] 3.2 Bump the hardcoded scan bound (`i = 36/2`) in `Overworld_GetPitDestination` (`src/overworld.c:3299-3321`) to cover 20 entries; gate the new entry's consumption on the Inverted world-state so non-Inverted fall-through is byte-identical.
- [ ] 3.3 Regenerate `zelda3_assets.dat` + `src/rando/vanilla_assets_hash.h`; run `assets/scripts/run_rando_corpus.py` to confirm placement digests are unchanged (Ganon's drop is game data, not placement); bump `kGeneratorVersion` ONLY if a digest moves.

## 4. Carved hole + exit-data + flute slot  — BLOCKED on the §1.3 spike

- [ ] 4.1 Add an Inverted variant of `CreatePyramidHole` (`src/overworld.c`) to carve the area-0x1B hole (mirror the DW-pyramid carve footprint at the 0x1B position), gated on Inverted.
- [ ] 4.2 Add the pyramid/HC exit-data override rows (exits 0x06/0x37/0x3D + door 0x35 area/pos/id) to `kInvertedOverrides[]` in `src/rando/inverted_entrances.c`, with any new `g_shadows[]` registrations; values verified against `Rom.php` in task 1.1.
- [ ] 4.3 Add the flute slot-8 override row (`kAsset_BirdTravel_ScreenIndex, 2, 8, 0x001B`) to `kInvertedOverrides[]` (replacing the current "intentionally left vanilla" comment).

## 5. Death-to-Ganon respawn  — BLOCKED on the §1.3 spike (no relocated Ganon room to respawn at)

- [ ] 5.1 Port `darkworldspawn.asm SetDeathWorldChecked_Inverted` `.castle` into `src/messaging.c Death_Func15`: when the Ganon-fight respawn condition holds (indoors + DungeonID==0xFF + room 0x000 + respawn flag) under Inverted, force the LIGHT world; otherwise keep the existing `savegame_is_darkworld = 0x40` Inverted death path.
- [ ] 5.2 Verify the normal Inverted DW death respawn (the existing `0x40` path) is untouched, and that death-to-Ganon now respawns at the LW castle.

## 6. Verification + record-keeping

- [ ] 6.1 End-to-end Inverted playtest — BLOCKED on §1.3 (nothing wired to playtest; the change is paused).
- [~] 6.2 Non-Inverted byte-identity: trivially satisfied — **no source/asset changes were made**, branch == `main` for all code/data; baseline build + `--rando-selftest` + 110/110 corpus all green. <!-- done: see spike-findings.md "Validation" -->
- [ ] 6.3 Fresh-eyes audit pass — N/A for a no-code-change pause (nothing to audit); applies when the spike is later unblocked.
- [x] 6.4 Record-keeping for the pause: `docs/inverted_alttpr_gaps.md` §A1 updated with the spike outcome (A1 stays a deliberate divergence — NOT moved to done; the §C2 non-gaps stay non-gaps); `spike-findings.md` added; memory `inverted-entrance-topology-source` annotated. <!-- done -->
