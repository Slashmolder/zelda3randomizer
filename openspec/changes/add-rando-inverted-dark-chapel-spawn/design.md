## Context

The Inverted spawn-point was made *playable* (`docs/inverted_alttpr_gaps.md`
§B6) by baking `which_starting_point = 0` (Link's House) in
`Rando_InitNewSlotSram`. That lands the player at the DW Bomb-Shop position
(screen 0x6C) via the Link's-House↔Bomb-Shop entrance swap — a navigable DW
home. ALTTPR instead relocates the respawn to a **Dark Chapel** (a Dark-World
Sanctuary) via a `StartingArea*` table block the fork does not have.

The fork's spawn path: `which_starting_point` indexes the `kStartingPoint_*`
assets, consumed in `src/dungeon.c:8418`. Index 1 (Sanctuary) spawns into
Sanctuary room `0x12`; its overworld exit is hardcoded to the **Light World**
(screen `0x13`). That hardcoded LW exit is the original §C1 trap — baking
`which_starting_point = 1` on an Inverted slot dropped the player in the LW even
though `savegame_is_darkworld = 0x40` was set.

Authoritative source: ALTTPR `../alttp_vt_randomizer/app/Rom.php
setInvertedMode()` (`StartingArea*` block 1680-1730 + Dark-Sanctuary spawn
`0x02D8D4..0x02D9B3`); upstream asm `../z3randomizer/darkworldspawn.asm`.

## Goals / Non-Goals

**Goals:**
- Inverted new-game / S&Q / death respawn at the Dark Chapel (DW Sanctuary,
  screen `0x53`), byte-identical for non-Inverted.
- Reuse the existing `inverted_entrances.c` asset-shadow override subsystem.

**Non-Goals:**
- Porting ALTTPR's full `StartingArea*` table system (the fork's
  `kStartingPoint_*` + per-slot override is sufficient).
- Any logic/placement change (spawn is runtime start-state only).

## Approach

The Dark Chapel **is** the DW side of the Sanctuary (same building). So rather
than add a new spawn point, override the existing Sanctuary spawn's post-exit
world/screen for Inverted:

1. Bake `which_starting_point = 1` (Sanctuary) for Inverted in
   `Rando_InitNewSlotSram` (replacing the §B6 interim `= 0`).
2. Add an inverted asset-shadow override (the `inverted_entrances.c` pattern)
   that repoints whichever `kStartingPoint_*` / exit field hardcodes the
   Sanctuary's LW exit (`0x13`) to the DW Sanctuary screen (`0x53`), gated on the
   active Inverted slot.
3. Redirect the `misc.c Module05` outdoors-DW reload and the `messaging.c` death
   respawn from Link's House to the same Dark-Chapel spawn.

**Key apply-time uncertainty** (resolve with an F12 dump before coding): which
`kStartingPoint_*` field actually drives the Sanctuary's post-exit overworld
world/screen, and whether `savegame_is_darkworld` (already `0x40`) participates.
If the LW exit is purely in an exit-data field already shadowed by
`inverted_entrances.c`, this is a small data add; if it is in a
`kStartingPoint_*` asset not yet registered, that asset joins the shadow set.

## Risks

- **Re-trap risk:** reverting `which_starting_point` 0→1 must NOT reinstate the
  §C1 LW trap — the DW-exit override is the load-bearing piece and must be live
  before the bake flips. Order the apply so the override ships with the bake.
- **Dark Chapel renders / is navigable:** DW screen `0x53` is the Sanctuary-mirror
  area; confirm its overlay renders and it connects to the broader DW (playtest +
  F12 BG dump).
- **Low value / high specificity:** this is cosmetic-tier faithfulness; if the
  apply-time spike shows the Sanctuary-exit field is not cleanly overridable, the
  acceptable fallback is to KEEP the Link's-House spawn and close this change as
  "won't-port" (Link's House remains a valid DW home).
