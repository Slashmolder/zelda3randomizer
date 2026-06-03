## 1. Ground the spawn-select exit mechanism (do FIRST — no guessing)

- [x] 1.1 ALTTPR upstream: the three Inverted spawn names ("@'s House" /
      "Dark Chapel" / "Dark Mountain") and the `StartingArea*` block are recorded
      in `design.md`. NOTE: the as-built (§2c) DOES use vanilla room `0x112` for the
      Dark Chapel (loaded via door `0x5A`/`kEntranceData`, special-cased in
      `Dungeon_LoadEntrance`) — it just doesn't port ALTTPR's full `StartingArea*`
      asset block. The earlier "fork does NOT port room 0x112" note is superseded.
- [x] 1.2/1.3/1.4 **Grounded statically** instead of by F12 dump: read the exit
      mechanism directly from `src/overworld.c LoadOverworldFromDungeon` (the
      spawn-select spawns indoors at `kStartingPoint_rooms[idx]`; on walking out,
      the room-keyed search reads `kExitData_ScreenIndex[exit-id]` — NOT a
      `kStartingPoint_*`-embedded screen, so 1.3 resolves to the `kExitData`
      knob) and traced the exit-ids/screens from the vanilla ROM `zelda3.smc`:
      Sanctuary room `0x012` → exit-id `0x01` → screen `0x13` (→ DW `0x53`);
      Mountain Cave room `0x0E4` → exit-id `0x31` → screen `0x03` (→ DW `0x43`).
      The runtime `screen |= 0x40` (3.1) still needs a playtest F12 to confirm
      `0x53` / `0x43` render + connect in the DW.

## 2. Redirect the spawn-select exit world (Inverted-gated, RUNTIME not asset)

> Mechanism changed from tasks.md's original plan. The reverted first attempt
> added two `kExitData_ScreenIndex` rows; that override world-warps the Sanctuary
> *check* too (exit-id `0x01` is shared), which the Non-Goals forbid. As built it
> is a runtime redirect that fires only for a menu respawn (the spec stub permits
> this alternative). See `design.md`.

- [x] 2.1 Route the Inverted post-Agahnim respawn through the spawn-select menu
      (`src/misc.c Module05_LoadFile`, mirroring the LW branch's menu gate);
      death / pre-Agahnim respawns keep the §B6 direct Link's-House spawn.
- [x] 2.2 `Module1B_SpawnSelect` (`src/messaging.c`) arms
      `g_rando_inverted_spawn_redirect` for an Inverted slot; declared in
      `src/rando/rando.{c,h}`, cleared in `Entrance_RuntimeTeardown`.
- [x] 2.3 `LoadOverworldFromDungeon` (`src/overworld.c`) consumes the flag at the
      top (clear-into-local, leak-safe like the sibling coupling globals) and ORs
      `0x40` into the anchor's exit screen in the room-keyed search branch, before
      the existing cross-world sync block (which flips `savegame_is_darkworld` /
      bunny). Link's House (`0x6C`) is already DW via its asset override, so the
      OR is a no-op there; the Sanctuary / Mountain-Cave *checks* (flag not armed)
      are unaffected.
- [x] 2.4 Non-Inverted untouched: build (MSBuild, sandboxed `bin/verify`) +
      `--rando-selftest` (all subsystems OK) + placement corpus (all 67 entries
      byte-identical — no `kGeneratorVersion` / settings-hash cascade) +
      audit-guard / determinism / codegen-wiring guards all green.

## 2b. Playtest revisions (2026-06-01) — indoor spawn + Dark Mountain unlock

Two playtests refined the spawn mechanism (see `design.md` "Playtest history"):

- [x] 2b.1 **Dark Mountain was always offered** because the fork granted a
      starting Magic Mirror (the vanilla menu gate is `link_item_mirror == 2`).
      Removed the Magic-Mirror start grant (`Rando_InitNewSlotSram` +
      `Rando_TryGrantStartingInventory`; Moon Pearl kept). Dark Mountain now
      unlocks when the mirror is found — the vanilla unlock. Logic-safe:
      `Place_AssumedFill` never pre-collected the mirror → corpus 67/67 unchanged.
- [x] 2b.2 **Spawn mechanism = indoor + walk-out-the-door (per user).** An interim
      revision spawned the Dark Chapel / Dark Mountain DIRECTLY outdoors at DW
      `0x53` / `0x43` (`main_module = 8`); an F12 dump confirmed the spot was correct
      but the player could not move — the direct placement skipped the normal
      door-exit screen setup. Reverted: `Module1B_SpawnSelect` keeps the vanilla
      indoor `LoadDungeonRoomRebuildHUD` for every anchor (the "Dark Chapel" IS the
      Sanctuary interior, "Dark Mountain" the Mountain Cave) and arms the redirect so
      walking OUT the door lands in the DW. Matches ALTTPR (spawn in the chapel, walk
      out a door to the dark world).
- [x] 2b.3 Re-verify: build + `--rando-selftest` (StartingInventory + RandoGenerate
      selfchecks updated for mirror=0) — all green.
- [x] 2b.4 **PLAYTEST 3 (user):** "Dark Chapel" spawns inside room `0x112` and walks
      out to DW `0x53` — confirmed. (The re-entry sub-saga that followed is §2c.)

## 2c. Room 0x112 spawn + cached-exit + re-entry fix (2026-06-02)

Switched "Dark Chapel" from the (wrong) LW Sanctuary reuse to vanilla room `0x112`
(door `0x5A`) and rebuilt the exit/re-entry. See `design.md` items 2 / 2b / 2c.

- [x] 2c.1 Spawn into vanilla room `0x112` via `which_entrance = 0x5A`
      (`Dungeon_LoadEntrance` `inv_dark_chapel` block), at the altar (F12-captured
      coords).
- [x] 2c.2 Exit via the room's NATURAL cached-exit branch fed a genuine screen-`0x53`
      `*_exit` cache (F12-captured inside room `0x112` after a real walk-in). Replaced
      the abandoned Sanctuary-room borrow AND the abandoned entrance-marker paint
      (`0xDA4/0xDA6` clobbered the static door tile `0xE0`). Added a cached-branch
      world-flag sync (no-op for same-world cached exits → RAM-compare safe).
- [x] 2c.3 **Re-entry root cause = `death_var4` carryover.** The entrance-path chapel
      spawn never cleared the spawn signal, so a later door re-entry took the
      `kStartingPoint` branch → Link's House, discarding `which_entrance=0x5A`. Fix:
      `WORD(death_var4) = 0` in the `inv_dark_chapel` block. Found via a room-load
      logger (re-entry fired `which_entrance=0x5A` but logged no entrance-path load).
- [x] 2c.4 Exit position: column 21, row 6, facing down (just south of the door's
      row-5 walk-up trigger). Debug logging stripped; build + `--rando-selftest` +
      `check_no_embedded_data` + `check_audit_guard` all green.
- [x] 2c.5 **PLAYTEST CONFIRMED (user):** spawn at the chapel altar → exit to DW
      `0x53` → walk back UP into the door → re-enter room `0x112`. All working.

## 3. Verify (playtest is the only net)

- [x] 3.1 **PLAYTEST (user) — CONFIRMED:** "Dark Chapel" spawns in room `0x112`,
      exits to DW `0x53`, and re-enters via the door (§2c.5). "@'s House" → Link's
      House; "Dark Mountain" mirror-gated. (Per-anchor F12 of `0x43` Dark Mountain
      navigability not separately re-confirmed this session, but it uses the same
      search-branch redirect as before.)
- [x] 3.2 Non-Inverted spawn-select byte-identical: `--rando-selftest` + placement
      corpus run (digests unchanged). LW-path runtime byte-identity is
      structurally guaranteed (the redirect flag is only armed for Inverted).
- [x] 3.3 `docs/inverted_alttpr_gaps.md` updated: D1 (Mountain-Cave spawn-select)
      marked ADDRESSED; the Dark-Chapel half of §B6 marked BUILT/PLAYTEST-PENDING;
      resolved exit-ids recorded.
