# Inverted world-state — known gaps vs. ALTTPR

Status as of branch `inverted-relocation` (the full inverted runtime stack:
`inverted-proper` → `inverted-entrances` → `inverted-topology` → `inverted-relocation`).

This is the register of *known* differences between this fork's Inverted mode and
ALTTPR's. The authoritative ALTTPR source is `../alttp_vt_randomizer/app/Rom.php`
`setInvertedMode()` (lines ~1587-1949) plus `app/Region/Inverted/*.php` for logic;
the upstream runtime asm is `../z3randomizer/inverted.asm` + `entrances.asm` +
`flute.asm` + `darkworldspawn.asm` + `bugfixes.asm`. Every entry below cites the
source and the fork's current handling.

Companion artifacts: the auto-memory note `inverted-entrance-topology-source`
(authoritative source → fork asset-array map) and the OpenSpec change
`openspec/changes/add-rando-inverted-ganon-relocation/` (covers item D1 below).

---

## A. Deliberate divergences (intentional — not bugs)

- **A1 — Ganon stays in the Dark-World pyramid.** ALTTPR moves Ganon under
  Hyrule Castle (LW); this fork keeps the vanilla DW-pyramid Ganon. The
  screen-0x1B inverted "pyramid" overlay is deliberately suppressed
  (`src/rando/inverted_maps_apply.c:43-57`), and the Ganon drop (`kFallHole`
  entrance 123 → room 0x000) stays hardcoded to DW pyramid area 0x5B. This single
  choice makes several ALTTPR writes *dead* for our topology (see C2, and the
  dropped pyramid/HC exit rows). **Reversing this is the subject of the
  `add-rando-inverted-ganon-relocation` OpenSpec change** — it is large
  (fall-hole + room access + overlay + exit-data + flute slot 8), hence its own
  change rather than a backlog line.

## B. Unimplemented gaps (would-be ALTTPR-faithful, deliberately not built yet)

- **B1 — Crystal → Hyrule-Castle-Tower door gate (approximate).** The GT↔AT
  *swap* is implemented (`src/rando/inverted_entrances.c`), but the physical GT
  door is still gated by the vanilla *positional* tile-0x169 `sram_progress_indicator
  >= 3` test (`src/overworld.c` `Overworld_UseEntrance`, the `is_149_or_169`
  block), NOT ALTTPR's exact crystal-count gate (`entrances.asm LockAgahnimDoors`,
  `Rom.php 0x180169=0x02`). **Not a softlock and not a logic break** — placement
  still requires 7 crystals (`logic_parts/inverted/GanonsTower.yaml`) and
  `Goal_IsCompletable` re-checks crystals + Agahnim 2. The only player-visible
  difference: GT may be physically *enterable* slightly earlier than ALTTPR, but
  not *completable* earlier. Closing it needs an F12 dump to confirm which door
  slot carries the 0x169 tile after the swap, then a crystal-count gate there.

- **B2 — Turtle Rock pre-opened bombable interior doors.** ALTTPR pre-opens two
  bombable doors on the reverse TR path (`Rom.php:1646-1647`, `0xFED31=0x0E`,
  `0xFEE41=0x0E`). Pure quality-of-life (saves 2 bombs). These bytes live in
  bank-0x1F dungeon room-object/door data, which is **not in the fork's extracted
  asset set** (assets 126-138), so they can't be ported via the
  `inverted_entrances` override table — needs a room-data archaeology pass or a
  runtime room-load hook. TR is fully completable without it (the main-entrance
  auto-open + collision table + peg-solve are already shipped).

- **B3 — Cosmetic / unmappable portal operands (4).** ALTTPR flips four ROM
  operand bytes our portal-reversal pass could not confidently map to a fork C
  branch: `$8283E0` (residual portal world), `$86DB78` (residual portal style
  byte), `$8DB3C5` (Blue→Red portal sprite graphic), `$87A96D` (residual portal
  side). All visual-only (which colored portal renders / a leftover-portal
  cosmetic), no traversal impact. Left alone rather than guessed.

## C. Resolved as NON-GAPS (researched, no change needed — recorded so they're not re-investigated)

- **C1 — Spawn / Sanctuary relocation.** ALTTPR relocates the Sanctuary spawn to
  a "Dark Chapel" via `StartingAreaExitTable`/`StartingAreaOverworldDoor`/
  `StartingAreaExitOffset` (`Rom.php:1680-1730`). The fork has **no `StartingArea*`
  tables** — it uses the `kStartingPoint_*` assets + the persisted
  `savegame_is_darkworld` flag, so an Inverted slot is born in the DW and S&Q /
  Sanctuary-respawn preserve the world correctly (`src/misc.c:648-674`,
  `src/messaging.c:797-799`). ALTTPR's relocation is a workaround for a
  single-entrance-cave spawn system the fork doesn't have.

- **C2 — Pyramid ExtraHole + the pyramid/HC exit rows.** ALTTPR adds a fall-hole
  at area 0x1B (LW Hyrule Castle) and relocates pyramid/HC exits (`Rom.php`
  exits 0x06/0x37/0x3D, door 0x35). All dead under A1 (Ganon stays at DW 0x5B); a
  0x1B fall-hole would route to an HC/Houlihan dead-end. The fork's sorted-by-id
  `kFallHole_*` tables also break the PHP offset→index mapping. Not ported.

## D. Latent / unverified edges (need a playtest to confirm before fixing)

- **D1 — Mountain-Cave spawn-select world.** Selecting "Mountain Cave" (index 6)
  from the spawn-select menu in Inverted *may* land in the Light-World DM cave
  (the menu path sets `which_starting_point` but not the world bit). Unverified.
  If confirmed, the fix is a one-liner: force `savegame_is_darkworld = 0x40` on an
  index-6 spawn-select under an active Inverted slot. S&Q and death-respawn are
  already correct, so this is the only spawn path at risk.

## E. Implemented but PLAYTEST-PENDING (built + audited, runtime-unconfirmed — not gaps, just unverified)

The playable-slot path has no automated test (`CLAUDE.md`: "playtest is the only
reliable net"). These are built and have passed build + `--rando-selftest` +
fresh-eyes audit, but await end-to-end confirmation:

- Entrance swaps (GT↔AT, Link's House↔Bomb Shop, DM-west caves) — door/exit
  values verified against `Rom.php` but not runtime-confirmed (F12-dump
  `kOverworld_Entrance_Id[0x23]/[0x36]` etc. if a door lands wrong).
- Portal flips (warp-vortex visibility, residual-portal spawn, DW-map indicator).
- Flute DW-only gate + Dark-World travel destinations (slots 0-7).
- Bunny-world inversion; Turtle Rock tail access.
