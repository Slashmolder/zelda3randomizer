## Why

ALTTPR's Inverted world-state relocates the save / respawn point to a **"Dark
Chapel"** — a Dark-World Sanctuary — via the `StartingArea*` block in
`app/Rom.php setInvertedMode()` (lines ~1680-1730: `StartingAreaExitTable`
`0x180250`, `StartingAreaExitOffset` `0x180240`, `StartingAreaOverworldDoor`
`0x180247`, plus the Dark-Sanctuary spawn data block `0x02D8D4..0x02D9B3`).

This fork has **no `StartingArea*` tables**. The Inverted spawn-point gap was
closed for *playability* (`docs/inverted_alttpr_gaps.md` §B6 / §C1) by baking
`which_starting_point = 0` (Link's House), whose exit lands at the DW Bomb-Shop
position (screen 0x6C) via the Link's-House↔Bomb-Shop entrance swap — navigable
and in the correct world (DW). But Link's House is **not** ALTTPR's relocated
Dark Chapel. This is the last Inverted spawn-point divergence.

This is a **faithfulness refinement, not a softlock fix** — the current
Link's-House spawn is fully playable. The change relocates the Inverted respawn
to the Dark-World Sanctuary (the Dark Chapel) to match ALTTPR exactly.

## What Changes

- **Relocate the Inverted save / respawn spawn from Link's House to the Dark
  Chapel** — the Dark-World Sanctuary at overworld screen `0x53` (the DW mirror
  of the Light-World Sanctuary at `0x13`) — matching ALTTPR. Covers new-game
  spawn, save-and-quit respawn, and death respawn.
- The fork spawns via the `kStartingPoint_*` assets indexed by
  `which_starting_point` (`src/dungeon.c:8418`). The Sanctuary entry (index 1)
  spawns into Sanctuary room `0x12`, whose overworld exit is hardcoded to the
  **Light World** (screen `0x13`) — which is exactly why baking
  `which_starting_point = 1` trapped the Inverted player in the LW (§C1).
  Override the Sanctuary spawn's post-exit world/screen for Inverted so it lands
  in the DW (screen `0x53`), then bake `which_starting_point = 1` for Inverted
  (superseding the interim `= 0` Link's-House choice from §B6).
- All behavior gated on the active Inverted rando world-state; non-Inverted /
  Open / Standard / Retro spawn-select stays byte-identical (the fork
  RAM-compares against the original ROM).

## Capabilities

### Modified Capabilities
- `randomizer-inverted-runtime`: ADDS the Inverted spawn-point (Dark Chapel)
  requirement. The capability is **established in-flight** by the
  `add-rando-inverted-ganon-relocation` change; this change contributes a
  *sibling* ADDED requirement (the spawn point), not a modification of Ganon's
  requirements — so the two changes archive independently without a
  sequencing conflict.

## Impact

- **Code:** `src/rando/rando_generate.c` (`Rando_InitNewSlotSram` — bake
  `which_starting_point = 1` for Inverted; update `RandoGenerate_SelfCheck`'s
  expected value), `src/rando/inverted_entrances.c` (a new asset-shadow override
  repointing the Sanctuary spawn's exit world/screen to the DW Sanctuary under
  the active Inverted slot), `src/misc.c` (`Module05_LoadFile` outdoors-DW
  reload) + `src/messaging.c` (death respawn) redirected from Link's House to
  the Dark Chapel.
- **Supersedes** the interim Link's-House spawn (§B6): the byte that changes is
  the baked `which_starting_point` plus the new Sanctuary-exit override; §B6's
  Link's-House path becomes dead for Inverted.
- **No generation / placement / logic impact:** spawn location is runtime
  start-state, not placement. No `kGeneratorVersion` / settings-hash / corpus
  cascade expected beyond the `RandoGenerate_SelfCheck` expected-value update
  (verify at apply-time).
- **Verification:** playtest only (the playable-slot path has no automated test,
  per `CLAUDE.md`); an F12 dump must confirm the spawn lands at DW screen `0x53`
  with `savegame_is_darkworld = 0x40` and is navigable.
