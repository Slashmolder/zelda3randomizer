## 1. Ground the spawn-select exit mechanism (do FIRST — no guessing)

- [ ] 1.1 Pin the ALTTPR upstream commit; re-read `app/Rom.php` `menu_start_2/3`
      (confirm the three Inverted spawn names) + the `StartingArea*` block
      (~1680-1730) for the Dark-Chapel DW screen/coords reference.
- [ ] 1.2 **F12-dump the fork's spawn-select** for each option under an Inverted
      slot: select "Dark Chapel" (idx 1) and "Dark Mountain" (idx 6); record the
      resulting `overworld_screen_index`, `savegame_is_darkworld`, and the exit-id
      taken (`LoadOverworldFromDungeon` search on the Sanctuary / Mountain-Cave
      room). Confirm both currently land in the LW.
- [ ] 1.3 Confirm the screen comes from `kExitData_ScreenIndex[exit-id]` (not a
      `kStartingPoint_*`-embedded screen). If it's a `kStartingPoint_*` asset,
      register that asset in the shadow set instead.
- [ ] 1.4 Determine the DW targets: Dark Chapel = `0x53` (DW mirror of LW `0x13`);
      Dark Mountain = the navigable DW Death-Mountain screen for the Mountain-Cave
      exit. F12 BG-dump `0x53` to confirm it renders + connects to the DW.

## 2. Override the spawn-select exit world (Inverted-gated)

- [ ] 2.1 Add two rows to the Inverted `kExitData_ScreenIndex` override in
      `src/rando/inverted_entrances.c` (the existing shadow set): Sanctuary
      exit-id → `0x53`; Mountain-Cave exit-id → the DW DM screen. Mirror the
      Link's-House idx-0 → `0x6C` precedent. Only the screen byte changes.
- [ ] 2.2 Confirm the initial spawn (Link's House) and the §B7 warps are
      unaffected (different exit-ids / assets); the change is additive to the
      shadow table.

## 3. Verify (playtest is the only net)

- [ ] 3.1 Playtest: open the respawn menu in an Inverted seed, pick "Dark Chapel"
      → F12-confirm DW screen `0x53`, `savegame_is_darkworld = 0x40`, navigable;
      pick "Dark Mountain" → DW Death Mountain, navigable. Neither lands in the LW.
- [ ] 3.2 Confirm non-Inverted spawn-select (Open / Standard / Retro) is
      byte-identical (Sanctuary → LW `0x13`, Mountain Cave → LW DM); run
      `--rando-selftest` + the placement corpus (digests unchanged).
- [ ] 3.3 Update `docs/inverted_alttpr_gaps.md`: close §D1 (Mountain-Cave
      spawn-select) and the Dark-Chapel half of §B6; record the resolved exit-ids.
