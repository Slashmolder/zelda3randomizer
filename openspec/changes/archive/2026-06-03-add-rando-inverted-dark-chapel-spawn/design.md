## Status: IMPLEMENTED & PLAYTEST-CONFIRMED

Implemented on branch `inverted-dark-chapel-spawn`. Confirmed by playtest: "Dark
Chapel" spawns INSIDE vanilla room `0x112` at the altar, walking out lands on DW
screen `0x53`, walking back UP into the door re-enters room `0x112`, and Dark
Mountain is mirror-gated. The long-tail bug (re-entry → Link's House) turned out to
be a `death_var4` carryover, NOT the exit-screen / door-tile issues chased first —
see item 2c and "Playtest history".

**As built (current):**

1. **Route the Inverted respawn through the menu.** `Module05_LoadFile`
   (`src/misc.c`) previously hard-routed every Inverted DW reload to Link's House,
   so `Module1B_SpawnSelect` (`main_module = 27`) never fired for Inverted. It now
   mirrors the LW branch's menu gate: a post-Agahnim Inverted load opens the
   spawn-select menu; a death-revival / pre-Agahnim respawn keeps the §B6 direct
   Link's-House spawn.
2. **The Dark Chapel is the vanilla room `0x112`, NOT the LW Sanctuary.** An F12
   dump (non-Inverted seed, standing in the chapel) proved the Dark Chapel already
   exists in vanilla: room `0x112`, reached via overworld door `0x5A`, exiting to DW
   screen `0x53`. Earlier revisions wrongly reused the LW Sanctuary room (`0x012`,
   slot 1's `kStartingPoint` row) for the spawn — which the player correctly read as
   "the actual Sanctuary." Room `0x112` has no `kStartingPoint` row, so
   `Dungeon_LoadEntrance` (`src/dungeon.c`) special-cases the Inverted slot-1 spawn:
   load room `0x112` through its real entrance (`which_entrance = 0x5A`, via
   `kEntranceData`) instead of `kStartingPoint[1]`. The player spawns INSIDE the
   chapel and walks OUT the door to the DW. "@'s House" (slot 0, Link's House room
   `0x104`) and "Dark Mountain" (slot 6, Mountain Cave `0x0E4`) keep the
   `kStartingPoint` path; Dark Mountain still uses the runtime redirect (item 3) to
   reach DW `0x43`. ALTTPR does the same (`app/Rom.php` writes room `0x112` + door
   `0x5A` into the spawn tables, `StartingAreaExitTable` → screen `0x53`).
2b. **The chapel exits via its NATURAL cached-exit branch fed a GENUINE screen-0x53
   cache.** Room `0x112` (>= `0x100`, != `0x104`) takes the cached-exit branch in
   `LoadOverworldFromDungeon` → `LoadCachedEntranceProperties`, which replays the
   `*_exit` block (`g_ram` `0xC140`). A menu spawn has no real overworld state to
   cache, so the chapel spawn (`Dungeon_LoadEntrance`, `inv_dark_chapel` block)
   PRE-LOADS `*_exit` with the chapel's genuine DW screen-`0x53` arrival — values
   captured by an F12 dump taken *inside* room `0x112` after a real walk-in entry
   (`overworld_screen_index_exit=0x53`, `map16_load_src_off_exit=0x9C`, link
   `0x758/0x456`, scroll/camera/themes, `overworld_unk3=0x0006`). On the way out the
   cached branch replays the real chapel screen — GFX, tilemap, and the chapel's own
   door — so re-entry works. A small **world-flag sync** added to the cached branch
   (mirroring the search branch) flips `savegame_is_darkworld`/`is_in_dark_world`/
   bunny from the restored screen's `0x40` bit; it's a no-op when the world doesn't
   change, so other (non-chapel) cached exits stay byte-identical.
   - *Abandoned approaches (kept for context):* (i) a Sanctuary-room borrow
     (`g_rando_entrance_exit_room = 0x12` + forcing the search branch + a `0x40`
     screen redirect) loaded the *Light-World Sanctuary* screen tagged as `0x53` — a
     counterfeit whose door tile sits two rows off the chapel's; (ii) painting an
     entrance marker (`ow_entrance_value = 0x2AA` → `0xDA4/0xDA6`) at the door
     CLOBBERED the genuine static door tile (col 22 = map16 `0xE0`, whose subtiles
     are in `kOverworld_Entrance_Tab0/1`). The static screen-`0x53` map already
     contains the enterable door, so the chapel exit just CLEARS `ow_entrance_value`
     (no marker) and lands Link at column 21, row 6 (`link_y = 0x466`) facing DOWN —
     just south of the row-5 trigger. The door is a walk-UP door (the facing-up check
     in `Overworld_UseEntrance` reads the cell one to the RIGHT of `pos`, col 22 =
     `0xE0`); facing him up instead auto-walked him into the door in a loop.
2c. **Re-entry fix — clear `death_var4` (the actual root cause).** The spawn-select
   signal is `death_var4 = 1` (not `follower_indicator` — F12: follower=0, death4=1).
   The chapel takes the ENTRANCE branch of `Dungeon_LoadEntrance`, which — unlike the
   `kStartingPoint` branch (clears `death_var4` at `dungeon.c:~8555`) — never cleared
   it. So a later WALK-IN through the chapel door (which correctly sets
   `which_entrance = 0x5A`) re-entered `Dungeon_LoadEntrance` with `death_var4` STILL
   1 but `which_starting_point` no longer 1 → `inv_dark_chapel` false → the top gate
   `(follower==4 || death_var4) && !inv_dark_chapel` fired → the `kStartingPoint`
   branch loaded `kStartingPoint_rooms[which_starting_point]` (= Link's House
   `0x104`), **discarding `which_entrance = 0x5A`**. (Smoking gun: a room-load logger
   showed the re-entry fired `which_entrance=0x5A` but produced NO entrance-path load
   line — it took the *other* branch.) Fix: `WORD(death_var4) = 0` in the
   `inv_dark_chapel` block. This is the project's dominant "vanilla state reused as a
   progress proxy" class — `death_var4` is the spawn proxy; the entrance-path spawn
   must consume it just like the `kStartingPoint` branch does.
3. **Runtime DW redirect (Dark Mountain only now), NOT an asset override.**
   `Module1B_SpawnSelect` arms a runtime flag `g_rando_inverted_spawn_redirect`;
   `LoadOverworldFromDungeon` (`src/overworld.c`) consumes it when the player walks
   out and ORs `0x40` into the anchor's exit screen in the room-keyed SEARCH branch.
   This now matters only for **Dark Mountain** (Mountain Cave `0x03 -> 0x43`): the
   Dark Chapel exits via the cached branch with screen `0x53` already baked into its
   `*_exit` cache (item 2b), and Link's House is already `0x6C` via its entrance-swap
   override. The search-branch cross-world sync block then flips
   `savegame_is_darkworld` / bunny. This deliberately does **not** add
   `kExitData_ScreenIndex` asset rows — exit-id `0x01` is shared by the Sanctuary
   *check*, and an asset override would world-warp the check too. The runtime flag
   fires only for a menu respawn, so the check still exits to the LW.
4. **Dark Mountain is unlocked, not free.** The vanilla spawn-select gates its third
   option ("Dark Mountain") on owning the Magic Mirror (`link_item_mirror == 2`,
   message `0x184` 2-option vs `0x185` 3-option). The fork auto-granted the Magic
   Mirror at Inverted start, so Dark Mountain was always offered. The Magic Mirror is
   **no longer a starting item** (removed from `Rando_InitNewSlotSram` and
   `Rando_TryGrantStartingInventory`; Moon Pearl is still granted for no-bunny). Now
   Dark Mountain appears only after the mirror is found — the vanilla unlock. This is
   logic-safe: `Place_AssumedFill` only pre-collects `RescuedZelda` for non-Standard
   (never the mirror), so placement/reachability and the corpus digests are unchanged
   (verified: 67/67 corpus byte-identical).

## Playtest history

- **Playtest 1** (indoor spawn + redirect): the player saw the Sanctuary interior
  and read it as "the actual sanctuary, wrong spot," plus Dark Mountain was always
  offered (the starting-mirror bug, fixed in item 4).
- **Playtest 2** (switched to direct OUTDOOR spawn at DW `0x53`): an F12 dump
  confirmed the spawn landed correctly outdoors at DW `0x53` (`savegame_is_darkworld
  = 0x40`, not a bunny, ALTTPR position X=`0x758`) — but the player could not LEAVE
  the screen. Root cause: the direct `main_module = 8` placement skipped the normal
  door-exit screen setup. User then clarified the intended behavior (matching
  ALTTPR): spawn INSIDE the chapel and walk out a door into the DW. Reverted to the
  indoor path (item 2).
- **Playtests 3+** (room `0x112` + re-entry hunt): spawn-in-chapel and exit-to-`0x53`
  worked, but re-entering the chapel kept landing in Link's House across ~10
  iterations. Many false leads were chased and ruled out with F12 dumps / asset
  decode / a runtime entry-decision logger: the exit screen (genuine `0x53`, not a
  counterfeit), the door tile (static `0xE0` is enterable; a painted `0xDA4/0xDA6`
  marker clobbered it), Link's exit position/facing (a facing-up exit auto-walked
  into the door in a loop), and a wrong-folder dump trap that poisoned the baked
  `*_exit` (`0x9A` vs `0x9C`). The ACTUAL cause was `death_var4` carryover (item 2c),
  found when a room-load logger showed re-entry firing `which_entrance=0x5A` yet
  taking the kStartingPoint branch. **Final playtest: spawn → exit → walk back in
  all confirmed working.** Lesson: when the door triggers correctly but the wrong
  room loads, stop tuning position — trace what the room-load *branch* reads.

**Findings from the first (pre-menu) attempt, retained for context:**

1. The spawn-select menu does NOT fire for Inverted out of the box — see #1 above.
2. **Not a beatability fix — parity only.** The fork's Inverted logic graph uses a
   SINGLE start anchor (`LinksHouse_Inverted`); there is no Dark Chapel region in
   `logic_parts/inverted/`. The placer never assumes Dark-Chapel access, so seeds
   are beatable from Link's House without it. This change is ALTTPR parity (a
   usable second respawn anchor), not a reachability fix.

**Known limitation (vanilla parity, not a regression):** `Module1B_SpawnSelect`
restores `which_starting_point` after the spawn (the menu choice is transient and
never persisted to SRAM 0xF3C8), and the runtime redirect flag is process-local.
So if the player Saves-and-Quits *while still standing indoors* at a menu-chosen
Dark Chapel / Dark Mountain (before walking out), a reload respawns at the last
*saved* anchor and the flag is gone — that one exit would land in the LW. This is
identical to vanilla's always-transient spawn-menu choice, is not a softlock
(Inverted has DW↔LW routes), and is not worth new persisted state. The spec's
absolute "SHALL NOT land in the Light World" holds for the direct menu→walk-out
flow; this S&Q-in-between corner is the documented exception.

Grounded exit data (re-confirmed this session by a direct read of the vanilla ROM
`zelda3.smc`, `kStartingPoint_rooms` @`0x82DB6E` -> `kExitDataRooms` @`0x82DD8A`
-> `kExitData_ScreenIndex` @`0x82DE28`):
- Dark Chapel  = Sanctuary    (`which_starting_point` 1, room `0x012`): exit-id `0x01`, `0x13 -> 0x53`
- Dark Mountain = Mountain Cave (`which_starting_point` 6, room `0x0E4`): exit-id `0x31`, `0x03 -> 0x43`

## Context

The fork's spawn-select (`Module1B_SpawnSelect`, `src/messaging.c`) maps the menu
choice to `which_starting_point` via `kLocationMenuStartPos = {0, 1, 6}`, then
calls `LoadDungeonRoomRebuildHUD`, spawning the player **indoors** at the
`kStartingPoint_*` room (Link's House `0x104`, Sanctuary `0x12`, Mountain Cave).
The player then exits to the overworld via the exit data
(`LoadOverworldFromDungeon` → search `kExitDataRooms` for the room → load
`kExitData_ScreenIndex[exit-id]`).

The exit screen byte **includes the world bit** and is hardcoded: the Sanctuary
exit is LW `0x13`, so an Inverted player who picks "Dark Chapel" exits to the LW
even though `savegame_is_darkworld = 0x40` (confirmed by the initial-spawn F12
dump — the world flag does not flip the exit). ALTTPR sidesteps this with a
dedicated `StartingArea*` block (room `0x112`); the fork has no such system.

The §B6 Link's-House fix already proves the fork-native approach: the
Link's-House exit (exit-id 0) screen is overridden `0x2C → 0x6C` in
`src/rando/inverted_entrances.c` so it lands in the DW. The Sanctuary +
Mountain-Cave exits need the same treatment.

## Goals / Non-Goals

> NOTE: the "Context / Goals / Non-Goals / Approach / Risks" sections below are the
> ORIGINAL plan (a `kExitData_ScreenIndex` shadow that would have left the chapel as
> the LW Sanctuary). The implementation diverged — see "As built" above. Kept for
> historical context; "As built" is authoritative. Two items changed materially:
> the as-built DOES load vanilla room `0x112` (the real Dark Chapel) directly, and it
> does NOT touch `kExitData_ScreenIndex` (it uses the cached `*_exit` branch).

**Goals:**
- Inverted spawn-select "Dark Chapel" lands in the DW (screen `0x53`); "Dark
  Mountain" lands on the DW Death Mountain. Byte-identical for non-Inverted.
- ~~Reuse the `inverted_entrances.c` `kExitData_ScreenIndex` shadow.~~ (Superseded:
  the as-built loads room `0x112` and uses the cached-exit `*_exit` branch instead.)

**Non-Goals:**
- ~~Porting ALTTPR's `StartingArea*` / room-`0x112` system.~~ (Partially superseded:
  the as-built DOES spawn into vanilla room `0x112` via its real entrance (door
  `0x5A`, `kEntranceData`), special-cased in `Dungeon_LoadEntrance` — but does NOT
  port ALTTPR's full `StartingArea*` asset block.)
- Touching the initial spawn (§B6 `which_starting_point = 0` Link's House).
- Relocating the Sanctuary *check* (BossHeartContainer) — it stays a LW location
  reached via LW North-West, matching the logic predicate
  (`logic_parts/inverted/HyruleCastleEscape.yaml:185`). Only the *respawn-anchor*
  world moves. (The check and the spawn anchor are distinct; conflating them is
  the error this re-scope corrects.)

## Approach

1. Identify the Sanctuary exit-id (the `kExitDataRooms` index whose room is the
   Sanctuary room) and the Mountain-Cave exit-id, plus their vanilla screens
   (expected `0x13` and the LW DM cave area). **Grounding:** F12-dump the fork's
   spawn-select for each option, or read the extracted exit data — do NOT guess
   the exit-ids.
2. Add two rows to the Inverted `kExitData_ScreenIndex` override in
   `inverted_entrances.c`: Sanctuary exit → `0x53`; Mountain-Cave exit → the DW
   DM screen. Only the screen byte changes — the DW area `0x53` shares the LW
   `0x13` map position (the `0x40` bit does not alter `&7`/`&56`), so X/Y/camera
   need no change.
3. Verify non-Inverted is untouched (the override is gated on the Inverted slot,
   like the existing entries).

**Key apply-time uncertainty (resolve before coding):** confirm the spawn-select
path actually consumes `kExitData_ScreenIndex` for these rooms (vs. a
`kStartingPoint_*`-embedded screen). If the screen is baked into a
`kStartingPoint_*` asset instead of `kExitData`, that asset joins the shadow set.
An F12 dump of "Dark Chapel" selection (read `overworld_screen_index` + the
exit-id path) settles it.

## Risks

- **Wrong exit-id → wrong-screen warp.** Mitigated by grounding the exit-id from a
  dump/asset, not memory.
- **DW `0x53` navigability.** Confirm the Dark-Chapel screen renders and connects
  to the DW (F12 BG dump) — same risk class as the B7 warps.
- **Mountain-Cave DW target.** The DW Death Mountain screen for the Dark-Mountain
  spawn must be a real, navigable DW DM screen; confirm the exact id at
  apply-time.
