# Proposal: fix-ow-map-prize-markers

## Why

The overworld pause map's pendant/crystal markers (`WorldMap_HandleSprites`,
`src/messaging.c`) take zero randomizer input. Each vanilla marker slot is a
hard-wired triple — *(dungeon position, prize icon, prize-type ownership test)* —
that is only coherent when the vanilla dungeon→prize assignment holds. Under
`prize_shuffle`, which **defaults to on** (`rando_settings.c` `Settings_SetDefaults`),
three of those three legs are wrong:

1. **The icon is a false claim.** The tile/palette is baked per slot
   (`kOwMapCrystal*_tab`): `ch 0x60` + flags `0x38/0x32/0x34` for the three
   pendants, `ch 0x64` for crystals. A crystal dungeon assigned a pendant still
   draws a crystal, and vice versa.
2. **The crystal *number* is a false claim.** `WorldMap_AddSprite` blinks the
   crystal tile against `kOverworldMapData[spr - 8]` — indexed by *OAM slot*,
   i.e. by vanilla dungeon. Turtle Rock always says "7".
3. **The "already obtained" test is prize-TYPE-keyed, not location-keyed.**
   `OverworldMap_CheckForPendant/CheckForCrystal` test the global
   `link_which_pendants` / `link_has_crystals` bits, so owning *a* green pendant
   hides Eastern Palace's marker regardless of whether EP's prize was collected
   — and leaves the marker up on a dungeon the player has already cleared. This
   is the identical defect the tracker fixed in `tracker-player-knowledge`
   (`PrizeIcon`'s `type_owned` vs `prize_obtained`, `tracker_windows.cpp`).

The markers are reachable in ordinary rando play: `Sprite_Sahasrahla` sets
`savegame_map_icons_indicator = 3` and the Palace-of-Darkness maiden sets 7
(`src/sprite_main.c`), so a default seed shows a misdirecting map.

This is misdirection, not a spoiler leak — but it is the *same* correctness
family the `randomizer-player-knowledge` capability governs, and the fix has to
land on the safe side of that invariant rather than simply "make the map tell
the truth".

## What Changes

- **Marker identity is re-keyed to the DUNGEON.** A small rando module owns the
  slot→dungeon correspondence for the three map-icon states that point at prize
  dungeons (`k == 3` → EP/TH/DP, `k == 6` → PoD, `k == 7` → the seven dark-world
  dungeons), derived from `kPendantBitMask`/`kCrystalBitMask` cross-referenced
  against `kDungeonCrystalPendantBit` and independently confirmed against the
  upstream ALTTPR marker tables.

- **The icon is knowledge-gated.** A prize marker renders the *true* placed
  prize only once the player has observed it — that dungeon's prize location is
  checked (`Rando_IsLocationChecked(Rando_GetDungeonPrizeLocation(d))`).
  Until then it renders vanilla's existing blinking "?" marker, which the code
  already draws for the pre-pendant map-icon states (the `tab >> 8 == 0` path
  through `kOwMap_tab2`). No new art, no new render path.

- **Visibility is location-keyed and the marker persists.** Under
  `prize_shuffle` a prize dungeon always carries a marker; collecting its prize
  turns "?" into the real icon instead of erasing it, so the pause map doubles
  as the in-game prize tracker (the only one that exists on Switch). Vanilla's
  type-keyed hide test is left untouched on the vanilla path.

- **The revealed crystal number follows the placed crystal**, via a
  number-tile table re-keyed by crystal number rather than OAM slot.

- **An identity-assignment oracle self-check** proves the re-keyed tables
  reproduce vanilla byte-for-byte: for every prize marker slot in every affected
  map-icon state, resolving the *vanilla* assignment through the new path must
  yield exactly the vanilla `tab` word and number tile. Registered in the `ui`
  self-check group.

- **The knowledge-guard allowlist gains one purpose-named file**
  (`src/rando/ow_map_prizes.c`) with a written justification, per
  `randomizer-player-knowledge / Knowledge-guard CI check`. The resolution lives
  in its own module rather than in `messaging.c` specifically so the allowlist
  entry stays narrow.

Explicitly **not** in scope (see `design.md` for the reasoning):

- Porting upstream's compass/map reveal axes (`CompassMode`/`MapMode`). Owning a
  dungeon's compass does not make its prize *known*, so a compass-reveal is a
  second intentional-disclosure path, which the landed player-knowledge
  requirement forbids without an owner decision.
- Moving markers under entrance shuffle (upstream's `WorldMapIcon_pos*_located`).
  Relocating a marker to the shuffled entrance would *reveal* where the dungeon
  actually is — a genuine leak, the opposite of this change's goal.
- The overworld dungeon-area music, which encodes pendant-vs-crystal the same
  way (upstream randomizes it for exactly this reason). Same bug family,
  different subsystem; tracked as a follow-up.

## Impact

- Affected specs: `randomizer-ui` (new requirement), `randomizer-player-knowledge`
  (new requirement covering in-world diegetic surfaces).
- Affected code: `src/rando/ow_map_prizes.{c,h}` (new), `src/messaging.c`
  (marker loop + two vanilla-table exports), `src/rando/rando.c` (self-check
  registration), `src/rando/rando_shuffles.h` (prize-id enum promoted to the
  header), `assets/scripts/check_knowledge_consumers.py`, `zelda3.vcxproj`,
  `Makefile` glob (automatic), `docs/randomizer.md`.
- **No placement effect**: nothing on the generation path changes, so
  `kGeneratorVersion` is NOT bumped and the corpus stays byte-identical
  (verified by regen, not by assertion).
- Vanilla and `prize_shuffle = 0` rando paths are byte-identical to today.
