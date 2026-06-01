## Status: DEFERRED

Deferred to a future change (user decision: keep the single Link's-House inverted
spawn for now). Two findings from the first implementation attempt that the next
implementer needs:

1. **The spawn-select menu does NOT fire for Inverted.** `Module05_LoadFile`
   (`src/misc.c`) takes the `savegame_is_darkworld` branch for an Inverted slot,
   which routes to Link's House (§B6); the spawn-select menu (`main_module = 27`,
   `Module1B_SpawnSelect`) lives only in the `else` / Light-World-save branch
   (misc.c:691), which Inverted never reaches. So this change is **not** just the
   `kExitData_ScreenIndex` override below — it must ALSO route the Inverted respawn
   through the spawn-select (or otherwise expose the Dark Chapel as a choice). The
   exit-screen override alone is inert for the menu (and has a side effect: it
   makes the Sanctuary *check* exit to the DW). That override was implemented then
   reverted; do not just re-add it.

2. **Not a beatability fix — parity only.** The fork's Inverted logic graph uses a
   SINGLE start anchor (`LinksHouse_Inverted`); there is no Dark Chapel region in
   `logic_parts/inverted/`. The placer never assumes Dark-Chapel access, so seeds
   are beatable from Link's House without it. This change is ALTTPR parity (a
   usable second respawn anchor), not a reachability fix.

Grounded exit data (read from the vanilla ROM, so the next attempt need not
re-derive it):
- Dark Chapel  = Sanctuary    (`which_starting_point` 1): exit-id `0x01`, `0x13 -> 0x53`
- Dark Mountain = Mountain Cave (`which_starting_point` 6): exit-id `0x31`, `0x03 -> 0x43`
- path: `kStartingPoint_rooms` @`0x82DB6E` -> `kExitDataRooms` @`0x82DD8A` -> `kExitData_ScreenIndex` @`0x82DE28`

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

**Goals:**
- Inverted spawn-select "Dark Chapel" lands in the DW (screen `0x53`); "Dark
  Mountain" lands on the DW Death Mountain. Byte-identical for non-Inverted.
- Reuse the `inverted_entrances.c` `kExitData_ScreenIndex` shadow.

**Non-Goals:**
- Porting ALTTPR's `StartingArea*` / room-`0x112` system.
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
