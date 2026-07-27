# Design: fix-ow-map-prize-markers

## Grounding

Everything below is read off current source or the upstream checkouts, not
memory. Line references are anchors for review; prefer the symbol names.

### The vanilla marker machinery

`WorldMap_HandleSprites` (`src/messaging.c`) draws, per frame:

| OAM slot | content |
| --- | --- |
| 0 | Link's blip |
| 8..14 | marker slots 6..0 (slot `i` → sprite `14 - i`) |
| 15 | residual-portal / bird-travel marker |

Markers 0..6 are gated on `savegame_map_icons_indicator` (`$7EF3C7`, `k`),
which also selects the world: `if (save_ow_event_info[0x5b] & 0x20 || (((k >= 6)
^ is_in_dark_world) & 1)) goto out;` — light-world map for `k < 6`, dark-world
map for `k >= 6`. Slot data is nine-entry tables `kOwMapCrystal{0..6}_{x,y,tab}`
indexed by `k`; `x == 0xff00` (`sign16`) means "no marker in this slot".

Per-`k` content, read off those tables:

| `k` | markers | meaning |
| --- | --- | --- |
| 0,1,2 | slot 0, `tab == 0` | blinking red-X routing marker (castle / Sahasrahla) |
| **3** | **slots 0,1,2** + slot 3 | **the three pendant dungeons** + Master Sword pedestal |
| 4 | slot 0 | pedestal |
| 5 | slot 0 | Hyrule Castle |
| **6** | **slot 0** | **Palace of Darkness** ("go to the first dark-world dungeon") |
| **7** | **slots 0..6** | **the seven crystal dungeons** |
| 8 | slot 0 | Ganon's Tower |

Only the bolded rows point at prize dungeons; everything else is routing and is
untouched by this change. The non-prize rows are already inert with respect to
the ownership tests, because `OverworldMap_CheckForPendant` requires `k == 3`
and `OverworldMap_CheckForCrystal` requires `k == 7`.

`k` is reachable under rando: `Sprite_Sahasrahla` sets 3, the Palace-of-Darkness
maiden sets 7, all-seven-crystals sets 8 (`src/sprite_main.c`).

### Slot → dungeon (derived twice, independently)

`kPendantBitMask[3] = {4, 1, 2}` and `kCrystalBitMask[7] = {2, 0x40, 8, 0x20, 1,
4, 0x10}` (`src/messaging.c`) are the per-slot prize bits. Cross-referencing
against `kDungeonCrystalPendantBit[13] = {0,0,4,2,0,16,2,1,64,4,1,32,8}`
(`src/zelda_rtl.c`, indexed by **game** dungeon id) pins:

| `k` | slot | bit | dungeon |
| --- | --- | --- | --- |
| 3 | 0 | 0x04 | Eastern Palace |
| 3 | 1 | 0x01 | Tower of Hera |
| 3 | 2 | 0x02 | Desert Palace |
| 3 | 3 | — | Master Sword pedestal (not a prize) |
| 6 | 0 | — | Palace of Darkness (position match) |
| 7 | 0 | 0x02 | Palace of Darkness |
| 7 | 1 | 0x40 | Skull Woods |
| 7 | 2 | 0x08 | Turtle Rock |
| 7 | 3 | 0x20 | Thieves' Town |
| 7 | 4 | 0x01 | Misery Mire |
| 7 | 5 | 0x04 | Ice Palace |
| 7 | 6 | 0x10 | Swamp Palace |

Independent confirmation: upstream ALTTPR replaced this whole routine with a
per-dungeon table (`/c/src/z3randomizer/overworldmap.asm`, tables at
`tables.asm:2617+`), ordered EP, Hera, Desert, PoD, SW, TR, TT, MM, IP, SP. Its
`WorldMapIcon_posx/posy_vanilla` values are byte-identical to our
`kOwMapCrystal*_{x,y}` entries at `k == 3` and `k == 7` — `$0F31/$0620` EP,
`$08D0/$0080` Hera, `$0108/$0D70` Desert, `$0F40/$0620` PoD, `$0082/$00B0` SW,
`$0F11/$0103` TR, `$01D0/$0780` TT, `$0100/$0CA0` MM, `$0CA0/$0DA0` IP,
`$0759/$0ED0` SP. Two derivations, same answer.

### Prize id → icon (derived from the vanilla tables, not transcribed)

`PrizeShuffle_Run(NULL, ...)` yields the identity assignment
(`src/rando/rando_shuffles.c`): EP=Green, DP=Red, TH=Blue, PoD=Crystal1,
SP=Crystal2, SW=Crystal3, TT=Crystal4, IP=Crystal5, MM=Crystal6, TR=Crystal7.
Combining with the slot table above and the vanilla `tab` words:

| prize id | vanilla source | `tab` |
| --- | --- | --- |
| 0 Green | EP = slot 0 @ `k=3` | `kOwMapCrystal0_tab[3]` = `0x6038` |
| 1 Red | DP = slot 2 @ `k=3` | `kOwMapCrystal2_tab[3]` = `0x6034` |
| 2 Blue | TH = slot 1 @ `k=3` | `kOwMapCrystal1_tab[3]` = `0x6032` |
| 3..9 Crystal1..7 | any slot @ `k=7` | `0x6434` (uniform) |

**Trap:** the registry's Red/Blue Pendant *names* are swapped relative to the
in-game colour (`PrizeIcon`, `tracker_windows.cpp`: `RedPendant` grants
DP/Power and displays blue). Keying off the prize **id** — as the table above
does — sidesteps the naming entirely; keying off the word "red" would swap DP
and TH.

The implementation reads those three `tab` words straight out of the vanilla
arrays rather than restating the literals, so drift is impossible by
construction.

### Crystal number tile

`WorldMap_AddSprite`'s `ch == 100` (`0x64`) branch alternates the crystal tile
with `kOverworldMapData[spr - 8]` — the *number* glyph, indexed by OAM slot.
Re-keying by crystal number via the slot table gives
`{C1..C7} = {0x7f, 0x79, 0x6c, 0x6d, 0x6e, 0x6f, 0x7c}`.

Independent confirmation: `kBirdTravel_tab1[8] = {0x7f, 0x79, 0x6c, 0x6d, 0x6e,
0x6f, 0x7c, 0x7d}` — the flute-map digit glyphs 1..8. The first seven are
exactly the sequence above, from an unrelated table.

## Decisions

**D1 — Re-key by dungeon; do not suppress.** Suppressing markers under
`prize_shuffle` is ~10 lines and removes the misdirection, but it blanks the map
at `k = 3/7` (reads as a bug), discards an information channel vanilla and
upstream both keep populated, and leaves the type-vs-location visibility defect
in place. Rejected.

**D2 — Reveal on collection only; no compass axis.** Upstream's default *does*
reveal each dungeon's true prize on the map (the generator rewrites
`WorldMapIcon_tile` per seed — see `app/Region/Standard/EasternPalace.php`
writing `0x53E76/0x53E77`), with `CompassMode`/`MapMode` as optional gates. Our
landed `randomizer-player-knowledge` requirement says the gating is
unconditional and "the spoiler reveal flow remains the only intentional
disclosure path", so an unconditional upstream-style reveal is out, and a
compass-based reveal would be a *second* disclosure path — an owner decision,
not a bug fix. This change therefore reveals exactly what the player has
observed: the prize at a dungeon whose prize location is checked.

**D3 — Persist the marker after collection; show the real icon.** Vanilla
erases a marker once its prize type is owned. Under D2 that would make the
revealed-icon branch unreachable, leaving the map permanently red-X-only. Keeping
the marker and flipping red X → real icon costs nothing (7 slots, 7 markers max),
is knowledge-safe by construction (the player collected it), keeps the map and
the ImGui tracker in agreement, and gives the Switch build — which has no
tracker window — a prize tracker. The count of visible markers is unchanged
either way, so nothing is inferable from marker *presence*.

**D4 — Gate on `prize_shuffle`, not merely on rando-active.** With
`prize_shuffle == 0` the assignment is the identity, so vanilla's art and its
type-keyed visibility test are both correct; staying on the vanilla path keeps
that case byte-identical. When `Rando_GetActiveSettings()` returns NULL
(snapshot replay / v1 slot) the code **fails closed** — treats the seed as
shuffled, so the marker degrades to the red X rather than asserting a possibly-wrong
prize. Same NULL-fail-closed shape as `tracker_windows.cpp`'s `shuffle_on`.

**D5 — An identity assignment stays hidden.** If `prize_shuffle` is on but the
roll happens to be the identity, markers still show the red X until collected. The
player cannot know the roll was identity; this mirrors the landed
"Identity-mapped assignment stays hidden" scenario for dungeon shuffle.

**D6 — Own module, narrow allowlist.** `Rando_GetDungeonPrizeAssignment` is a
guarded symbol (`check_knowledge_consumers.py`) and the guard is *file*-granular.
Allowlisting `src/messaging.c` would open a ~3 kLOC vanilla file to every future
assignment read. The resolution therefore lives in `src/rando/ow_map_prizes.c`
— the shape `src/rando/medallion_icons.c` already uses for the other deliberate
in-world assignment surface — and only that file is allowlisted.

**D7 — Reuse vanilla's red-X render path.** Substituting `tab = 0` *before* the
existing `t = tab >> 8` branch routes the marker through vanilla's own
`kOwMap_tab2` blink path (flags `0x32`, `ext 0`, no `-4` pre-adjust, no
odd-frame skip). That path is live in vanilla at `k = 0/1/2`, so it needs no new
art, no new OAM budget, and no new blink timing.

**D8 — Convert the seven copy-pasted marker blocks into one loop.** They are
byte-identical modulo slot index and table (sprite index `14 - i`; the
`CheckForPendant` leg is vacuous for `i >= 3` because it requires `k == 3`,
where slots 3..6 are `0xff00` except the pedestal). One loop means one rando
hook instead of seven — the alternative is seven chances to fix six sites.

**D9 — No `kGeneratorVersion` bump.** Nothing on the generation path is touched;
this is display-only. Per the project's inert-change rule the bump is skipped,
and the claim is verified with a corpus regen rather than asserted.

## Risks

- **Render/OAM regression is playtest-only.** The corpus never executes this
  code. The identity-oracle self-check proves the *data* mapping, not the draw.
  Pause-map playtest is a required gate: vanilla seed unchanged, shuffled seed
  shows the red X then the right icon and crystal number after a boss.
- **Inverted.** The light/dark marker split is `(k >= 6) ^ is_in_dark_world`.
  Under Inverted the dungeon-to-world correspondence is unchanged (a dungeon
  stays where it is; the *player's* world flips), so the split still selects the
  right marker set — but this is the same neighbourhood as the Inverted
  residual-portal fix immediately above it in `WorldMap_HandleSprites`, and
  wants an Inverted pause-map playtest.
- **Entrance shuffle.** Markers keep vanilla positions, which remain a true
  statement ("the vanilla entrance is here") but are no longer where the dungeon
  is. Deliberate: moving them would leak the shuffled topology. Called out in
  `docs/randomizer.md`.

## Follow-ups (not this change)

- ~~**Overworld dungeon-area music leaks prize type.**~~ **RETIRED — the
  premise was wrong; investigated 2026-07-26, do not re-open.** Two
  independent reasons. (1) `PrizeShuffle_Run` permutes pendants only among
  the three pendant dungeons and crystals only among the seven crystal
  dungeons, so a dungeon's prize TYPE is invariant and a type-encoding
  surface cannot leak or mislead. (2) The upstream evidence never applied:
  ALTTPR targets JP 1.0 and this fork is US 1.0, so its `music_addresses`
  are JP file offsets — reading them in the US ROM yields 0x00/0x01, not
  the 0x11/0x16 the claim assumed. Upstream needs the rewrite because ITS
  crystal shuffle can swap pendants and crystals between dungeons; ours
  cannot. Original (incorrect) note follows for the record.
- ~~**Overworld dungeon-area music leaks prize type.**~~ Upstream's
  `Prize::writeItem` (`app/Location/Prize.php`) rewrites each dungeon region's
  overworld music to `0x11` (pendant) or `0x16` (crystal) to match the placed
  prize, and randomizes it when the map-on-pickup option is on precisely so it
  does not leak. This fork has no rando handling of `overworld_music` at all —
  it is loaded static from `kOwMusicSets` (`src/overworld.c`) — so the music
  near each dungeon still announces the vanilla prize type. Same bug family,
  different subsystem.
- **Compass-reveal axis**, if the owner wants upstream's `CompassMode`
  behaviour as an opt-in disclosure path.
