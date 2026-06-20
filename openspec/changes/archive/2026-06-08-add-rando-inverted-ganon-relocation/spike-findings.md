# Spike findings — screen-0x1B inverted Ganon overlay (FAITHFUL facade PAUSED; HOLE-ONLY shipped)

> **RESOLUTION (2026-06-07): a NO-ART hole-only relocation shipped instead.** This
> document records the spike for the *faithful* facade overlay, which stays paused
> (it needs unlicensed custom art — see below). Rather than the facade, a no-art
> Ganon pit at screen 0x1B shipped (a 2-tone dark diamond from a solid castle tile;
> fall→Ganon via a per-char pit-attr override + an `Overworld_GetPitDestination`
> special-case; flute slot 8 repointed; DW-pyramid hole REMOVED — Ganon only at the
> LW castle). See
> `tasks.md` (as-built header) and `docs/inverted_alttpr_gaps.md` §A1 for the
> shipped behavior. The "no changes made" note at the bottom of THIS file describes
> the spike-only state and is superseded by the hole-only implementation.

**Date:** 2026-06-06 (offline spike) · **Live-confirmed:** 2026-06-07 (playtest).
**Outcome (faithful facade):** PAUSED-AT-SPIKE (per `tasks.md` §1.3 STOP rule).
**Upstreams pinned:** ALTTPR `219fcafd029dab597b8db400efafd8f56f8b4edb`,
z3randomizer `dcb0a2b42d14445f7994a0e9e4d63cbecf4b98d3`.

This change relocates Ganon from the DW pyramid (area 0x5B) to under the LW Hyrule
Castle (area 0x1B), matching ALTTPR Inverted. The load-bearing piece is rendering
the screen-0x1B "pyramid" overlay (currently suppressed in
`src/rando/inverted_maps_apply.c`) without graphics corruption. **It cannot be done
with vanilla assets.** Everything downstream (fall-hole, exits, flute, death-respawn)
is dead without it, so the whole change is paused.

## LIVE CONFIRMATION (playtest, 2026-06-07)

An exploratory probe branch (off `main`: just
comments out the `if (scr == 0x1B) return;` suppression) was built and played in an
Inverted seed. Reaching LW Hyrule Castle (warp to entrance `0x04` → walk out → screen
`0x1B`) and F12-dumping confirmed the verdict **on real hardware-path rendering**:

- **Pre-Agahnim (facade only):** the overlay paints **wrong but coherent tiles** — a
  brown fence-post cross, green bulb-pillars on the moat walls, and scattered green
  gems — i.e., the DW-pyramid facade blocks reinterpreted through the castle
  gfx+palette. Not a usable castle facade.
- **Post-Agahnim (hole added):** "different random tiles" (the hole blocks, worse).

**Calibration vs. the offline render:** the offline pass predicted *mint-green*; the
live manifestation is more "wrong **tiles**" than "wrong **color**" (the green
pillars/gems match the predicted green; the fence-post cross is the wrong-tile part).
Same root cause and same conclusion (the overlay does NOT render correctly with
vanilla assets → PAUSE), but the live screen is the authority — the offline renderer
drew the overlay tiles in isolation on black, not composited over the live base
screen, so the exact gestalt differs. The renderer was a strong, correctly-concluding
hypothesis, not a pixel-exact oracle.

Also confirmed in the same session: the **DW pyramid hole is intact and beatable**
(the probe only un-suppressed the visual; it did not relocate the hole), so the paused
state leaves Inverted fully playable — the relocation is ALTTPR-visual-fidelity-only,
not a functional requirement.

## How the spike was run (no playtest available)

The visual result is playtest-only, and playtest was unavailable in this work mode.
Instead an **offline renderer** reconstructed exactly what the game would draw:

- Parsed `zelda3_assets.dat` directly; decompressed (LZ2, ported from
  `Decompress()`) the real BG gfx sheets screen 0x1B loads.
- Reproduced the BG VRAM/char layout from `InitializeTilesets`
  (8 sheets at VRAM 0x2000-0x3FFF, char base 0x2000, region = char>>6).
- Reproduced the **4bpp `Do3To4High`/`Do3To4Low` split** from
  `LoadBackgroundGraphics` (for overworld `main_theme>=0x20`, char-regions
  {0,3,4,5} use `Do3To4High` → palette colors 9-15; {1,2,6,7} use `Do3To4Low` →
  colors 1-7) and the overworld palette assembly from `Palette_Load_OWBG*`
  (main → 1-7 of rows 2-6; aux3 → 1-7 of row 7; aux1 → 9-15 of rows 2-4; aux2 →
  9-15 of rows 5-7).
- Ran the `.map1B` overlay through a port of the `Overworld_ApplyInvertedTiles`
  interpreter, forcing the post-Agahnim hole.

**Cross-validation:** rendering the same overlay with the DW-pyramid theme reproduces
the *known in-game DW pyramid* appearance pixel-for-pixel (brown brick, teal stone
hole, sand) — so the renderer is trustworthy. The PNGs (kept outside the repo, in
a local spike scratch dir) are: `pal_castle*.png` (castle screen — shows the bug),
`pal_pyramid*.png` (DW reference — validates the renderer).

## The blocker (precise)

- The overlay **gfx content is coherent** with castle gfx — the shapes are right
  (it *is* a castle facade + a round hole). The first-pass index-color render even
  looked clean. The bug is **palette**, exactly as the prior playtest note recorded
  ("mint-green, wrong palette slots").
- Screen 0x1B loads the **castle palette**: main theme 0x20, aux theme 36, `bgpal=2`,
  `overworld_palette_mode=0`. The `.map1B` tiles are authored for the **DW pyramid
  palette** (mode 1, bgpal 18). The mismatch lands on the HIGH halves:
  - castle row 6 HIGH = white/gray/teal (castle windows), pyramid = brown/purple stone
  - castle row 7 HIGH = tan + **green** (`#317331`,`#4a9c4a`), pyramid = brown stone
  - castle row 5 HIGH = orange/pink, pyramid = red/dark
  → the hole + facade render mint-green/white/garish instead of brown.
- **Why it is irreducible without custom art:** the overlay tiles share palette rows
  with the *base* Hyrule-Castle tiles, **including the LOW halves the base stonework
  uses** (pal2-L, pal5-L, pal6-L). So:
  - loading the pyramid palette wholesale → turns the whole castle screen
    pyramid-colored (wrong — ALTTPR keeps the LW castle look); and
  - a per-row palette override of just the overlay rows → also recolors the base
    castle tiles in those rows.
  There is no free palette row to retarget the overlay into, and no single palette
  that satisfies both the overlay and the base castle.
- The corruption is in the **always-painted facade** (live: fence-post cross + green
  pillars/gems; offline: the central mint-green pyramid-eye blob), not only the
  post-Agahnim hole — so it can't be dodged by skipping the hole.

## What ALTTPR does (and why it doesn't port cheaply)

`Rom.php setInvertedMode()` solves this with **custom non-vanilla artwork** plus tweaks:

- `0x00D009=0x31, 0x00D0e8=0xE0, 0x00D1c7=0x00` — repoint a gfx sheet to `$31E000`,
  which is `incbin "data/sheet73.gfx"` in z3randomizer
  (`LTTP_RND_GeneralBugfixes.asm:238 InvertedCastleHole`, 1192 bytes LZ2 → 0x600).
  This is **custom castle-hole graphics drawn to look correct with the castle
  palette** — it is not in the vanilla US ROM, so it is not in the fork's extracted
  `zelda3_assets.dat`.
- `0x0FF1C8` (36 words) + `0x0FA480` (8 words) — redefine the hole map16 blocks
  (~0xE39-0xE41, 0x490-0x491) to point at the custom gfx + palettes 6/7.
- `0x1BE8DA=0x39AD` — one added palette color "for shading for castle hole".
- `0x00DB9D=0x1A, 0x00DC09=0x1A` — make retreat-bat sprite gfx available in HC.

Porting this faithfully into the fork means a **new gitignored non-vanilla gfx
asset + asset-pipeline plumbing + a gated VRAM-load hook on screen 0x1B + map16
redefinition data + a palette tweak** — a large gfx-pipeline change that runs
against the fork's vanilla-extraction + no-committed-data model, and whose result is
**playtest-only** to validate. That is precisely the "balloons into a large
gfx-pipeline change" the spike STOP rule names. → **Paused.**

## Most-promising future approach (when someone returns to this)

1. Add `z3randomizer/data/sheet73.gfx` as a **new gitignored fork asset** (decompress
   to 0x600; treat like other extracted BG sheets — needs an `assets/` extraction +
   `compile_resources.py` packing entry + an `assets.h` reader + `kNumberOfAssets`
   bump). Note: it is *custom*, not vanilla-ROM-derived — confirm this is acceptable
   under the no-committed-data policy (it is data, but injected, gitignored).
2. On screen 0x1B under Inverted only, after the normal gfx load, **overlay
   sheet73.gfx into the slot-73 char range** (`Do3To4High`, the same conversion the
   castle aux2 sheet uses) — a small gated hook in `InitializeTilesets`/the OW gfx
   path.
3. Port the **map16 redefinitions** (`0x0FF1C8`, `0x0FA480`) into the fork's
   `kMap16ToMap8` data for the hole blocks, gated/applied only under Inverted (or as
   an Inverted-specific override), and the **`0x1BE8DA=0x39AD` shading color** into
   the castle screen-0x1B palette under Inverted.
4. Then un-suppress (`remove if (scr == 0x1B) return;`) and **F12-verify** on every
   entry method AND walking-scroll (the historical `inverted-overlay-mirror-glitch`
   note: the overlay only ran on full rebuilds — confirm/extend to walking-scroll).
5. Re-validate the offline renderer first (cheap), then playtest (the only real net).

A non-faithful fallback (carve a functional hole + relocate the drop without the
visual pyramid overlay, leaving screen 0x1B looking like vanilla Hyrule Castle) was
considered and rejected: it leaves an unmarked hole and does not satisfy the spec's
"overlay renders" scenarios — and it is still playtest-gated.

## Transcribed ALTTPR relocation writes (verified vs `Rom.php` HEAD — for the future port)

All from `app/Rom.php setInvertedMode()`. Exit-table column bases:
`0x15AEE`=load-coords/scroll, `0x15B8C`=area, `0x15BDB`=cameraY, `0x15C79`=cameraX,
`0x15D17`=linkY, `0x15DB5`=linkX, `0x15E53`=cameraYbound, `0x15EF1`=…,
`0x15F8F`=…, `0x1602D`/`0x1607C`=scroll bytes, `0x160CB`/`0x16169`=0.

- **ExtraHole** (1846-1849): `0x308300=0x0140`(map16) `0x308320=0x001B`(area)
  `0x308340=0x7B`(entrance).
- **pyramid hole entrances** (1842-1844): `0x1bb810 = 0x00BE,0x00C0,0x013E`;
  `0x1bb836 = 0x001B,0x001B,0x001B`.
- **exit 0x06** (HC ledge spawn, 1732-1745): `15AEE+2*06=0x0020`, `15B8C+06=0x1B`,
  `15BDB+2*06=0x00AE`, `15C79+2*06=0x0610`, `15D17+2*06=0x077E`, `15DB5+2*06=0x0672`,
  `15E53+2*06=0x07F8`, `15EF1+2*06=0x067D`, `15F8F+2*06=0x0803`, `1602D+06=0x00`,
  `1607C+06=0xF2`, `160CB+2*06=0`, `16169+2*06=0`.
- **exit 0x37** (pyramid → new HC area, 1905-1917): `15AEE+2*37=0x0010`,
  `15B8C+37=0x1B`, `15BDB+2*37=0x0418`, `15C79+2*37=0x0679`, `15D17+2*37=0x06B4`,
  `15DB5+2*37=0x06C6`, `15E53+2*37=0x0728`, `15EF1+2*37=0x06E6`, `15F8F+2*37=0x0733`,
  `1602D+37=0x07`, `1607C+37=0xF9`, `160CB+2*37=0`, `16169+2*37=0`.
- **exit 0x3D** (Pyramid Exit ⇐ Houlihan, 1665-1678): `15AEE+2*3D=0x0003`,
  `15B8C+3D=0x5B` (NB: stays 0x5B), `15BDB+2*3D=0x0B0E`, `15C79+2*3D=0x075A`,
  `15D17+2*3D=0x0674`, `15DB5+2*3D=0x07A8`, `15E53+2*3D=0x06E8`, `15EF1+2*3D=0x07C7`,
  `15F8F+2*3D=0x06F3`, `1602D+3D=0x06`, `1607C+3D=0xFA`, `160CB+2*3D=0`, `16169+2*3D=0`.
- **door 0x35** (move pyramid exit overworld door, 1896-1899): `0xDB96F+2*35=0x001B`,
  `0xDBA71+2*35=0x06A4`, `0xDBB73+0x35=0x36`.
- **flute** (1600): `0x02E849 = 0x0043,0x0056,0x0058,0x006C,0x006F,0x0070,0x007B,
  0x007F,0x001B` — slot 8 (index 8) = `0x001B`. Flute spot-9 coords at
  `0x02E87B..0x02E98B` (1747-1756) mirror the exit-0x06 spawn coords.
- **death-respawn** (`darkworldspawn.asm SetDeathWorldChecked_Inverted .castle`):
  NOT transcribed in detail — phase 5 is blocked; revisit when the overlay unblocks.
  Gate intent: Ganon-fight respawn (indoors + DungeonID==0xFF + room 0x000 + the
  respawn flag) → force LIGHT world (0); keep the existing `0x40` Inverted DW-death
  path for all other deaths.

## Validation of the pause (headless)

No source/asset changes were made, so non-Inverted byte-identity is trivially intact
(branch == `main` for all code/data). Baseline on `main` HEAD `030218c`, built
fresh: clean `-Werror` build, `--rando-selftest` → all subsystems OK, corpus 110/110
OK. The only branch content is documentation (`tasks.md`, `design.md`, this file,
`docs/inverted_alttpr_gaps.md`).
