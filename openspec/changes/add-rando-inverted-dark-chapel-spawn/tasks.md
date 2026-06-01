## 1. Source + spawn-mechanism grounding (do FIRST)

- [ ] 1.1 Pin the ALTTPR upstream commit (`git -C ../alttp_vt_randomizer rev-parse HEAD`); re-read `app/Rom.php setInvertedMode()` `StartingArea*` block (~1680-1730) + the Dark-Sanctuary spawn data block (`0x02D8D4..0x02D9B3`); transcribe the exact DW Sanctuary screen + Link-landing coords.
- [ ] 1.2 **F12 dump the fork's Sanctuary spawn** (`which_starting_point = 1`, via a scratch non-Inverted slot or a temporary bake): record `kStartingPoint_rooms[1]`, the entrance/door fields, and the post-exit `overworld_screen_index` + `savegame_is_darkworld` interaction. Determine WHICH field hardcodes the LW exit (screen `0x13`) — i.e. whether it is an already-shadowed exit-data field (`inverted_entrances.c`) or a `kStartingPoint_*` asset not yet registered.
- [ ] 1.3 Confirm DW screen `0x53` (the Sanctuary-mirror Dark Chapel) renders correctly and connects to the broader Dark World (F12 BG dump from a walk-in); record the navigability finding.

## 2. Dark Chapel spawn override

- [ ] 2.1 Add an inverted asset-shadow override (the `inverted_entrances.c` pattern) repointing the field identified in 1.2 so the Sanctuary spawn's post-exit world/screen is the DW Sanctuary (screen `0x53`), gated on `Rando_GetActiveWorldState() == Inverted`. If a new asset joins the shadow set, register it.
- [ ] 2.2 Bake `which_starting_point = 1` (Sanctuary) for Inverted in `Rando_InitNewSlotSram` (`src/rando/rando_generate.c`), replacing the §B6 interim `= 0`; **ship 2.1 in the same change** so the DW-exit override is live before the bake flips (avoid re-introducing the §C1 LW trap). Update `RandoGenerate_SelfCheck`'s expected `sram[0x3C8]` value (0x00 → 0x01).
- [ ] 2.3 Redirect the `src/misc.c` `Module05_LoadFile` outdoors-DW reload path and the `src/messaging.c` death respawn from Link's House to the Dark-Chapel spawn (gated on Inverted).

## 3. Verify (playtest is the only net)

- [ ] 3.1 Playtest: a fresh Inverted seed spawns at the Dark Chapel (DW screen `0x53`); S&Q and death respawn there; F12 dump confirms `savegame_is_darkworld = 0x40` + screen `0x53`, navigable.
- [ ] 3.2 Confirm the non-Inverted Sanctuary spawn (Open / Standard / Retro) is byte-identical (LW screen `0x13`); run `--rando-selftest` (`RandoGenerate_SelfCheck` green) and the placement corpus (digests unchanged).
- [ ] 3.3 Update `docs/inverted_alttpr_gaps.md` §B6 — mark the Dark-Chapel exactness resolved (or, on the won't-port fallback, record the finding and keep the Link's-House spawn).
