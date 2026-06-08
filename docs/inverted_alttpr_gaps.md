# Inverted world-state — known gaps vs. ALTTPR

Status: rebased onto `main`. Covers the full inverted runtime stack plus the
spawn-point fix (Inverted now spawns at Link's House in the DW — see B6/C1) and
the DW→LW under-rock warps (see B7). Last updated after the warp port.

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

- **A1 — Ganon relocated to Hyrule Castle via a NO-ART pit (hole-only; SHIPPED on
  branch `claude/inverted-ganon-holeonly`, alpha, playtest-confirmed).** ALTTPR
  moves Ganon under the LW Hyrule Castle; this fork now does too, via a hole-only
  approach (see below). The history:
  - **Spike (2026-06-06): the FAITHFUL facade is paused.** An offline renderer
    (real decompressed gfx + the exact 4bpp `Do3To4High/Low`→OW-palette pipeline,
    cross-validated against the known DW-pyramid look) proved the full `.map1B`
    overlay (facade **and** hole) renders **mint-green** on the castle screen: it
    is authored for the DW-pyramid palette and shares palette rows — including the
    LOW halves the base castle stonework uses — with the base Hyrule-Castle tiles,
    so it is irreducible without ALTTPR's custom non-vanilla art
    (`z3randomizer/data/sheet73.gfx` + map16 redefs + 1 shading color,
    `Rom.php:1798-1879`), which is unlicensed and not in this fork's
    ROM-extracted asset set. The faithful facade stays paused. Full evidence:
    `openspec/changes/add-rando-inverted-ganon-relocation/spike-findings.md`.
  - **Hole-only as-built (SHIPPED).** Rather than the facade, render ONLY a Ganon
    pit at screen 0x1B with **no new art**: a 2-tone dark diamond built from a
    solid castle tile (char 0x037) already loaded on 0x1B — #292929 interior
    (pal 4) + #393129 rim (pal 7), via two map16 blocks appended to a
    `kMap16ToMap8` (asset 70) runtime shadow (`InvertedHoleBlocks_Install`). The
    fall works via a per-char pit-attr override (char 0x037 → `0x20` on Inverted
    0x1B post-Agahnim, `tile_detect.c`) + an `Overworld_GetPitDestination`
    special-case (Inverted 0x1B post-Agahnim → entrance `0x7B`, the Ganon room).
    Flute slot 8 is repointed `0x5B→0x1B` (faithful, but inert in the current
    8-spot flute menu). Death-at-Ganon uses the existing Inverted spawn-select
    menu (recoverable). **The DW-pyramid hole is REMOVED** — under Inverted a fall
    at the pyramid (area 0x5B) no longer matches its Ganon entries and drops to the
    Chris-Houlihan fallback instead (`Overworld_GetPitDestination`), and the
    animated carve is gated to screen 0x1B so no spurious hole forms on the pyramid;
    Ganon lives ONLY under the LW castle (faithful to ALTTPR's repointed pyramid
    hole entrances). All Inverted-gated; non-Inverted byte-identical.
    Playtest-confirmed: pit renders, fall→Ganon, death recoverable, LW reachable.
  - **Reachability:** normal-play DW→LW access (to reach the LW castle) is provided
    by the under-rock world-warps (B7), now in `main`.
  - **Deferred (optional):** the faithful facade (needs custom art); a 9th
    flute-menu spot for the castle; walking-scroll verification of the pit overlay.
    The C2 non-gaps stay non-gaps (the fall-hole pos was handled at runtime, not via
    the sorted `kFallHole_*` table).

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

- **B3 — Cosmetic / unmappable portal operands (3).** ALTTPR flips three ROM
  operand bytes our portal-reversal pass could not confidently map to a fork C
  branch: `$8283E0` (residual portal world), `$86DB78` (residual portal style
  byte), `$8DB3C5` (Blue→Red portal sprite graphic). All visual-only (which
  colored portal renders / a leftover-portal cosmetic), no traversal impact.
  Left alone rather than guessed. (`$87A96D` was originally mis-listed here as
  cosmetic — it is NOT: it is the mirror residual-portal return-coord side, now
  implemented as the second half of the mirror-direction fix in `player.c`
  `DoSwordInteractionWithTiles_Mirror`.)

- **B4 — Flute spot "gargoyle statue" Link-landing nudge (deferred, ambiguous
  slot).** ALTTPR nudges one Inverted flute destination's Link-landing coord out
  of a gargoyle statue (`Rom.php:1601-1602`: `kBirdTravel_LinkXCoord`/`LinkYCoord`
  = 0x07C8/0x01F8). The whole-stack audit flagged this; we left it because the
  affected flute **slot index is ambiguous** — the audit guessed slot 2
  (Kakariko), but the coord 0x07C8/0x01F8 decodes to screen col 3 / row 0 = area
  0x43 (slot 0), so the two disagree. Cosmetic (Link lands near a statue, can
  still move/mirror). Resolve by playtest: flute to each Inverted spot, find the
  one that drops Link on/inside a statue, then nudge that specific slot's
  117/118 coords. (The DM-west exit-0x18 camera nudge from the same audit — Unk1/
  Unk3 assets 141/142 — WAS resolved and shipped; only this flute-slot one is
  deferred.)

- **B5 — Flute-menu map icons render at vanilla (Light-World) positions.** The
  flute-menu destination icons use hardcoded arrays in `messaging.c` (`kBirdTravel_tab1`
  / `x_lo`/`x_hi`/`y_lo`/`y_hi`), not asset 113, so in Inverted the on-map spot
  icons display at Light-World coordinates over the (Dark-World) map. Travel still
  warps correctly (asset 113 is overridden); only the icon position is off.
  Visual-only; a gated C change to those arrays would close it.

- **B6 — Inverted spawn point (FIXED & PLAYTEST-CONFIRMED).** A fresh
  Inverted slot used to bake `which_starting_point = 1` (Sanctuary, a LW
  building) → the player spawned in the Light World (hard trap, C1). **Fixed**
  (`src/rando/rando_generate.c Rando_InitNewSlotSram`): Inverted now bakes
  `which_starting_point = 0` (Link's House), whose exit lands at the DW Bomb-Shop
  position (screen 0x6C) via the Link's-House↔Bomb-Shop entrance swap — the
  Inverted home, in the DW, navigable. The outdoors-DW reload path
  (`src/misc.c Module05_LoadFile`) is likewise redirected from the trapped pyramid
  ledge to Link's House. **Dark Chapel / Dark Mountain (CONFIRMED).**
  ALTTPR exposes a post-Agahnim respawn menu whose three options ("@'s House" /
  "Dark Chapel" / "Dark Mountain") all land in the DW. The fork's `Module05_LoadFile`
  hard-routed every Inverted DW reload to Link's House, so the menu (`main_module=27`)
  never fired for Inverted. **Added** (`add-rando-inverted-dark-chapel-spawn`):
  `Module05_LoadFile` routes a post-Agahnim Inverted load through the spawn-select
  menu (death / pre-Agahnim respawns keep the direct Link's-House spawn).
  - **Dark Chapel = vanilla room `0x112`** (the REAL DW chapel, NOT the LW Sanctuary):
    `Dungeon_LoadEntrance` special-cases the Inverted slot-1 spawn to load room `0x112`
    via its own door (`which_entrance = 0x5A`) at the altar, exits via the room's
    cached-exit branch fed a genuine screen-`0x53` `*_exit` cache, and **clears
    `death_var4`** so walking back IN through the chapel door re-enters room `0x112`
    (without the clear, the carryover routed the re-entry into the `kStartingPoint`
    branch → Link's House). Spawn + exit + re-entry all PLAYTEST-CONFIRMED.
  - **Dark Mountain** keeps the indoor Mountain-Cave spawn + a runtime
    `g_rando_inverted_spawn_redirect` (`LoadOverworldFromDungeon`'s search branch ORs
    `0x40` into its exit screen → DW `0x43`). NOT a `kExitData` asset override, so the
    Mountain-Cave *check* (entered from the overworld) still exits to the LW.
  - **Dark Mountain is mirror-gated** (vanilla `link_item_mirror == 2`): the fork no
    longer grants a starting Magic Mirror (only Moon Pearl), so it unlocks when the
    mirror is found. Logic-safe (placer never pre-collected the mirror; corpus
    digests unchanged).

- **B7 — DW→LW under-rock warps (BUILT; PLAYTEST-PENDING).** In Inverted the
  Magic Mirror only carries LW→DW; the way *out* of the DW to the LW is a set of
  fixed under-rock world-warp tiles (overworld-secret type 0x82). The fork never
  ported them, so an Inverted player had no legitimate DW→LW route. **Added**
  (`src/rando/inverted_entrances.c InvertedSecrets_Install`): one type-0x82 warp
  per DW screen that carries one in ALTTPR (`Rom.php:1887-1919`) — screens
  0x4D/0x4E (DM), 0x50, 0x6F, 0x70/0x78 (Mire), 0x73, 0x75, 0x47. **Playtest
  risk:** a 0x82 warp only fires if a *liftable rock exists at that tile* in the
  fork's vanilla tilemap; if ALTTPR added a rock there, that screen also needs a
  tilemap edit. Confirm by lifting rocks at each screen and F12-dumping a miss.

## C. Resolved as NON-GAPS (researched, no change needed — recorded so they're not re-investigated)

- **C1 — Spawn / Sanctuary relocation. *(CORRECTED — this was wrong; see B6.)***
  This was originally recorded as a non-gap on the theory that the persisted
  `savegame_is_darkworld` flag made any spawn land in the DW. **Playtest proved
  otherwise:** a fresh Inverted slot baked `which_starting_point = 1` (the
  Sanctuary), a *Light-World* building whose exit dropped the player in the LW —
  the "spawning in the LW" trap. The spawn *world* is now fixed (B6), AND the
  Dark-Chapel *exactness* is now resolved too: the `add-rando-inverted-dark-chapel-spawn`
  change spawns "Dark Chapel" into vanilla room `0x112` (the real DW chapel) directly,
  rather than ALTTPR's `StartingArea*` asset block (B6, playtest-confirmed).

- **C2 — Pyramid ExtraHole + the pyramid/HC exit rows.** ALTTPR adds a fall-hole
  at area 0x1B (LW Hyrule Castle) and relocates pyramid/HC exits (`Rom.php`
  exits 0x06/0x37/0x3D, door 0x35). All dead under A1 (Ganon stays at DW 0x5B); a
  0x1B fall-hole would route to an HC/Houlihan dead-end. The fork's sorted-by-id
  `kFallHole_*` tables also break the PHP offset→index mapping. Not ported.

## D. Latent / unverified edges (need a playtest to confirm before fixing)

- **D1 — Mountain-Cave spawn-select world. *(ADDRESSED — see B6; BUILT,
  PLAYTEST-PENDING.)*** Selecting "Mountain Cave" (index 6) from the spawn-select
  menu in Inverted would land in the Light-World DM cave. The earlier-hypothesised
  one-liner (force `savegame_is_darkworld = 0x40` on the index-6 spawn) was wrong
  on two counts: (a) the spawn-select menu never fired for Inverted at all — the
  reverted first attempt proved it (`Module05_LoadFile` hard-routed Inverted DW
  loads to Link's House); and (b) the world bit alone wouldn't move the overworld
  *exit* screen, which is keyed off `kExitData_ScreenIndex`. Fixed under
  `add-rando-inverted-dark-chapel-spawn` together with the Dark Chapel (B6): the
  menu is now reachable for Inverted and the index-6 anchor spawns in the Mountain
  Cave interior, walking out to the DW Death Mountain (screen `0x43`). Dark Mountain
  is also **mirror-gated**
  like vanilla — the fork no longer grants a starting Magic Mirror, so the option
  only appears once the mirror is found. Awaiting playtest.

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
