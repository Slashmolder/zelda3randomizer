## ADDED Requirements

### Requirement: Inverted spawn-select respawn points are in the Dark World

When `Rando_GetActiveWorldState() == Inverted` (under `kFeatures1_RandomizerActive`), the post-Agahnim spawn-select menu's **"Dark Chapel"** option (`which_starting_point = 1`) and **"Dark Mountain"** option (Mountain Cave slot, `which_starting_point = 6`) SHALL spawn the player **inside the anchor's interior** and, when the player walks OUT the door, land them in the **Dark World**. The **"Dark Chapel" SHALL load vanilla room `0x112`** (the real Dark World chapel reached in vanilla via overworld door `0x5A`, NOT the Light-World Sanctuary room `0x012`), spawn the player at its altar, and exit to DW screen `0x53`; **walking back INTO the chapel door SHALL re-enter room `0x112`** (not Link's House). The Dark Mountain exit lands at the DW Death Mountain (screen `0x43`). The player SHALL NOT walk out into the Light World from either option. The **"Dark Mountain" option SHALL be gated on the player owning the Magic Mirror** (the vanilla `link_item_mirror == 2` menu gate); the Magic Mirror is NOT an Inverted starting item, so Dark Mountain is unlocked through play, exactly as in vanilla. The "@'s House" spawn (Link's House, `which_starting_point = 0`) keeps the vanilla indoor spawn, walking out to the DW. All behavior SHALL be gated so that non-Inverted / Open / Standard / Retro spawn-select is byte-identical (the fork RAM-compares against the original ROM), and SHALL NOT change placement/reachability (the corpus digests are unchanged).

> **Mechanism (as built)**: "Dark Chapel" loads vanilla room `0x112` via its real entrance (`which_entrance = 0x5A`, `kEntranceData`), special-cased in `Dungeon_LoadEntrance` (`inv_dark_chapel`), and positions Link at the altar. It exits via the room's NATURAL cached-exit branch fed a genuine screen-`0x53` `*_exit` cache pre-loaded at spawn (so `LoadCachedEntranceProperties` replays the real chapel screen — including its own door); a cached-branch world-flag sync flips `savegame_is_darkworld` for the DW arrival (no-op for same-world cached exits). The spawn also clears `death_var4` — without that, a later walk-in through the chapel door (which correctly sets `which_entrance=0x5A`) is misrouted by the `Dungeon_LoadEntrance` gate into the `kStartingPoint` branch and loads Link's House, discarding `which_entrance`. "Dark Mountain" keeps the vanilla indoor spawn + a runtime `g_rando_inverted_spawn_redirect` (`Module1B_SpawnSelect` arms it; `LoadOverworldFromDungeon`'s search branch ORs `0x40` into its exit screen → `0x43`) — NOT a `kExitData_ScreenIndex` asset override (exit-id `0x01` is shared by the Sanctuary *check*, which must keep exiting to the LW). Dark Mountain's gate is the unchanged vanilla menu message selection (`link_item_mirror == 2`), now meaningful because the Magic Mirror is no longer auto-granted. This supersedes `docs/inverted_alttpr_gaps.md` §D1 and closes the Dark-Chapel half of §B6.

#### Scenario: Non-Inverted spawn-select is unchanged
- **WHEN** a seed is played in Open, Standard, or Retro, or with no active rando slot
- **THEN** the spawn-select Sanctuary (idx 1) and Mountain Cave (idx 6) options behave byte-identically to the pre-change build (indoor Sanctuary / Mountain-Cave interiors exiting to the Light World)

#### Scenario: Inverted "Dark Chapel" spawns in room 0x112 and walks out to the Dark World
- **WHEN** an Inverted player opens the respawn menu and selects "Dark Chapel"
- **THEN** they spawn at the altar inside **vanilla room `0x112`** (the real Dark World chapel, not the Light-World Sanctuary room `0x012`) and, on walking out the door, land in the Dark World at screen `0x53` (`savegame_is_darkworld = 0x40`) — not in the Light World — and can navigate the Dark World from there

#### Scenario: Re-entering the Dark Chapel returns to room 0x112
- **WHEN** the player (after the "Dark Chapel" spawn) is standing on DW screen `0x53` and walks back UP into the chapel door
- **THEN** they re-enter vanilla room `0x112` (the chapel) — NOT Link's House (room `0x104`); i.e. the door entry routes through `which_entrance = 0x5A`, not the `kStartingPoint` respawn branch

#### Scenario: Dark Mountain is gated on the Magic Mirror (vanilla unlock)
- **WHEN** an Inverted seed is freshly generated (the player has not yet found the Magic Mirror)
- **THEN** the respawn menu offers only "@'s House" and "Dark Chapel" (the 2-option `0x184` menu); "Dark Mountain" does NOT appear
- **AND WHEN** the player has obtained the Magic Mirror (`link_item_mirror == 2`)
- **THEN** the respawn menu also offers "Dark Mountain", which spawns the player in the Mountain Cave interior and, on walking out, lands on the DW Death Mountain (screen `0x43`) — not in the Light-World DM cave

#### Scenario: The Link's House spawn is untouched
- **WHEN** an Inverted slot is freshly generated (the baked `which_starting_point = 0`) or the player selects "@'s House" from the menu
- **THEN** the spawn is the indoor Link's House landing in the Dark World (screen `0x6C`), exactly as before this change (§B6 behavior preserved)
