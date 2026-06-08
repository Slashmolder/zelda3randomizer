# randomizer-inverted-runtime Specification

## Purpose
TBD - created by archiving change add-rando-inverted-dark-chapel-spawn. Update Purpose after archive.
## Requirements
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

### Requirement: Inverted Dark World → Light World under-rock world-warps

In Inverted mode the Magic Mirror only carries Light World → Dark World, so the way OUT of the Dark World SHALL be a fixed set of "world-warp" tiles hidden under liftable rocks on specific Dark-World overworld screens. Each warp is realized by three layers that MUST agree per screen: (a) a type-`0x82` overworld-secret record added by `InvertedSecrets_Install` (`src/rando/inverted_entrances.c`); (b) a liftable rock map16 tile placed by the inverted overlay (`src/rando/inverted_maps.c`, applied by `Overworld_ApplyInvertedTiles`); and (c) the walk-in placer `Overworld_EnsureInvertedWarpRock` (`src/overworld.c`).

The warp screens, `dung_bg2` tile positions, and rock map16 tiles SHALL be: `0x73`/`0x02A8`/`0x020F`, `0x50`/`0x0B2E`/`0x020F`, `0x6F`/`0x0BB2`/`0x020F`, `0x4E`/`0x1D4A`/`0x0239`, `0x70`/`0x1D94`/`0x0239`, `0x78`/`0x1D94`/`0x0239`, `0x75`/`0x0F50`/`0x0239`, and `0x47`/`0x069E`+`0x06A4`/`0x0239`. These mirror the vanilla Light-World hardcoded rocks relocated to the Dark World (ALTTPR `Rom.php` `setInvertedMode()` + z3randomizer `inverted.asm` `HardcodedRocks`); no new rock art is introduced.

Lifting a warp rock SHALL reveal map16 tile `0x0212` (overworld collision `TileBehavior_Warp` / `0x4b`); stepping onto the revealed warp SHALL flip `savegame_is_darkworld` (Dark↔Light). The warp SHALL be reusable (a repeatable world flip) and therefore SHALL NOT be gated on a location-checked or consumed flag. The warp rocks require the Power Glove to lift; this is intended — the inverted logic graph accounts for Dark-World escape being gated on the Power Glove (no glove-less escape is expected). All behavior SHALL be inert unless `enhanced_features1 & kFeatures1_RandomizerActive` and the active `world_state` is Inverted; vanilla / Open / Standard / Retro overworld tiles SHALL be byte-identical to the pre-change build.

#### Scenario: Walking onto a Dark-World warp screen exposes the liftable rock and warps to the Light World
- **WHEN** an Inverted player who has the Power Glove walks (overworld scroll, not a full screen rebuild) onto a warp screen — e.g. screen `0x73` — and reaches the warp position `0x02A8`
- **THEN** a liftable rock (map16 `0x020F`) is present at that tile, asserted by `Overworld_EnsureInvertedWarpRock` the frame the screen settles
- **AND** lifting the rock reveals warp tile `0x0212`, and stepping onto it flips `savegame_is_darkworld` from `0x40` (Dark) to `0x00` (Light)

#### Scenario: The warp rock appears regardless of entry method
- **WHEN** a warp screen is entered by ANY method — walking-scroll, Magic Mirror, flute, cave/building exit, or save-and-quit
- **THEN** the liftable rock is present at the warp position: the inverted overlay places it on full screen rebuilds, and `Overworld_EnsureInvertedWarpRock` covers the walking-scroll path that the overlay skips

#### Scenario: A revealed warp is not re-covered before the player can use it
- **WHEN** the player lifts a warp rock so the tile becomes the revealed warp `0x0212`, then remains on the screen
- **THEN** the per-frame placer leaves the tile as `0x0212` — it places the rock only when the current tile is neither the rock nor the revealed warp — so the player can still step onto the warp

#### Scenario: The Light-World counterpart rocks are absent in Inverted
- **WHEN** an Inverted player is on Light-World screen `0x33` or `0x2F` (the vanilla hardcoded-rock screens), reached by walk-in OR full rebuild
- **THEN** the vanilla hardcoded rock is NOT drawn there (removed in Inverted by `Overworld_HandleOverlaysAndBombDoors`) — the rock exists only on the Dark-World side under the warp

#### Scenario: Inert outside Inverted mode
- **WHEN** the active world-state is not Inverted (vanilla / Open / Standard / Retro), or no randomizer slot is active
- **THEN** `Overworld_EnsureInvertedWarpRock` early-returns and writes no tiles, so overworld map16 tiles are byte-identical to the pre-change build

### Requirement: Ganon located under Hyrule Castle in Inverted

When `Rando_GetActiveWorldState() == Inverted` (under `kFeatures1_RandomizerActive`), the runtime SHALL relocate the Ganon fight from the Dark-World pyramid (overworld area 0x5B) to under the Light-World Hyrule Castle (area 0x1B). The relocation SHALL cover the Ganon fall-pit at area 0x1B (drop destination → the Ganon room), the removal of the Dark-World pyramid as a Ganon access, and the flute travel-slot destination. All behavior SHALL be gated so that non-Inverted / Open / Standard / Retro play is byte-identical (the fork RAM-compares against the original ROM).

#### Scenario: Non-Inverted play is unchanged
- **WHEN** a seed is played in Open, Standard, or Retro, or with no active rando slot
- **THEN** Ganon remains in the Dark-World pyramid (area 0x5B); the screen-0x1B overlay, fall-hole routing, flute slot 8, and death respawn are byte-identical to the pre-change build

#### Scenario: Inverted Ganon drop lands under Hyrule Castle
- **WHEN** an Inverted player, post-Agahnim, falls into the Ganon pit on the Light-World Hyrule-Castle screen (area 0x1B)
- **THEN** they arrive in the Ganon fight room (entrance 0x7B), not the Hyrule-Castle interior or the Chris-Houlihan fallback

#### Scenario: The Dark-World pyramid is no longer a Ganon access
- **WHEN** an Inverted player falls at the Dark-World pyramid (area 0x5B)
- **THEN** they do NOT reach Ganon — the pyramid's Ganon fall-hole entries no longer match, so the fall drops to the Chris-Houlihan fallback (kept in the Dark World)

#### Scenario: Death at Ganon respawns via the Inverted spawn-select menu
- **WHEN** an Inverted player dies during the Ganon fight under Hyrule Castle (post-Agahnim)
- **THEN** they respawn via the existing Inverted spawn-select menu (Dark-World options) and can re-reach Ganon (NOTE: ALTTPR's Light-World-castle respawn is intentionally NOT ported — it conflicts with the fork's Dark-World-home spawn system and risks the Light-World-spawn trap)

#### Scenario: Flute slot 8 destination points at the relocated Ganon spot
- **WHEN** the Inverted flute slot-8 destination is consulted
- **THEN** it resolves to area 0x1B (the relocated Hyrule-Castle Ganon location), not the Dark-World pyramid (0x5B) — though slot 8 is not yet selectable in the current 8-spot flute menu (a deferred cosmetic gap)

### Requirement: The Inverted Ganon pit on screen 0x1B renders without corruption

When the Inverted world-state is active and Agahnim is defeated, the screen-0x1B Ganon pit SHALL render as a clean dark pit using graphics already loaded on the Hyrule-Castle screen (no new/custom art), with no garbage / mint-green tiles, leaving the surrounding castle unchanged. The full ALTTPR "pyramid facade" overlay is intentionally NOT rendered — it requires unlicensed custom graphics (`z3randomizer/data/sheet73.gfx`) and is deferred.

#### Scenario: The pit renders cleanly on a full screen rebuild
- **WHEN** an Inverted player arrives at screen 0x1B post-Agahnim via a full screen rebuild (mirror / cave exit / flute / save-and-quit)
- **THEN** the Ganon pit renders as a coherent dark pit (a 2-tone dark diamond) with no garbage / mint-green tiles, and the surrounding Hyrule Castle is unchanged

#### Scenario: Pre-Agahnim the castle is unchanged
- **WHEN** an Inverted player is on screen 0x1B before Agahnim is defeated
- **THEN** no pit is drawn and the Hyrule-Castle screen is byte-identical to the non-relocated build (the pit is gated on the pyramid-hole bit `save_ow_event_info[0x5B] & 0x20`)

