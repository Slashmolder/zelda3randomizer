## Why

The post-Agahnim **spawn-select menu** (`Module1B_SpawnSelect`, `src/messaging.c`)
offers three respawn points, indexed by `which_starting_point` via
`kLocationMenuStartPos = {0, 1, 6}`. In ALTTPR Inverted these three are renamed
"@'s House" / **"Dark Chapel"** / **"Dark Mountain"** (`app/Rom.php` `menu_start_2`
/ `menu_start_3`) — i.e. Link's House / Sanctuary / Mountain Cave — and ALTTPR
places **all three in the Dark World** (the "Dark Chapel" is the relocated DW
Sanctuary; ALTTPR spawns it from a dedicated `StartingArea*` block at
`Rom.php:1680-1730`, room `0x112`).

The fork's Sanctuary spawn (`which_starting_point = 1`) and Mountain Cave spawn
(`= 6`) exit to the **Light World** (the Sanctuary at LW screen `0x13`; the DM
cave in the LW). An F12 dump confirmed the exit screen is hardcoded LW even with
`savegame_is_darkworld = 0x40` set. So an Inverted player who picks "Dark Chapel"
or "Dark Mountain" from the respawn menu lands in the Light World and soft-locks
— the **same LW-trap class** as the initial-spawn bug (`docs/inverted_alttpr_gaps.md`
§B6 / §C1 / §D1), but reached via the manual respawn menu rather than the baked
start.

This is a **reachability concern, not cosmetic**: the Dark Chapel is a Dark-World
respawn anchor the player is expected to be able to use, and placements may count
on it. A broken Dark-Chapel respawn can leave a seed effectively unwinnable from
that anchor (or, at minimum, soft-lock a player who selects it).

## What Changes

- For an active Inverted slot, the spawn-select **"Dark Chapel" (Sanctuary, idx 1)**
  and **"Dark Mountain" (Mountain Cave, idx 6)** options SHALL respawn the player
  in the **Dark World** — the Dark Chapel at the DW Sanctuary-mirror (screen
  `0x53`) and Dark Mountain at the DW Death Mountain — matching ALTTPR.
- The **initial spawn (Link's House, `which_starting_point = 0`) is unchanged** —
  §B6 already lands it in the DW; this change does NOT touch the baked start.
- Gated on the active Inverted world-state; non-Inverted / Open / Standard / Retro
  spawn-select is byte-identical (the fork RAM-compares against the original ROM).

## Capabilities

### Modified Capabilities
- `randomizer-inverted-runtime`: ADDS the Inverted spawn-select respawn-world
  requirement. Sibling ADDED requirement to the in-flight
  `add-rando-inverted-ganon-relocation` change (no archive-sequencing conflict).

## Impact

- **Code:** an Inverted asset-shadow override (the `src/rando/inverted_entrances.c`
  pattern) repointing the Sanctuary + Mountain-Cave exit *screen* to their DW
  equivalents (`kExitData_ScreenIndex`) — the Link's-House exit is already
  overridden the same way (idx 0 → `0x6C`). The Sanctuary's overworld position is
  identical in both worlds (the world bit `0x40` does not change the exit's
  X/Y/camera), so only the screen byte changes. Exact exit-ids confirmed at
  apply-time (F12 dump of the fork's spawn-select, or the extracted exit data).
- **No initial-spawn change** (§B6's `which_starting_point = 0` stays).
- **No generation / placement / logic impact** beyond making an
  already-modeled DW anchor actually reachable. No `kGeneratorVersion` /
  settings-hash / corpus cascade expected (verify at apply-time).
- **Verification:** playtest only — pick "Dark Chapel" then "Dark Mountain" from
  the respawn menu and F12-confirm the spawn lands in the DW (`savegame_is_darkworld
  = 0x40`, DW screen) and is navigable.
