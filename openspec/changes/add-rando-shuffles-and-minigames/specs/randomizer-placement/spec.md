## ADDED Requirements

### Requirement: §6.8 Minigame dispatch sites

Four minigame reward sites SHALL route their reward through the rando dispatcher
(`Rando_OnLocationCheck` / `Rando_DispatchVanillaGrant`) so a seed can place any
item at the site, and SHALL preserve byte-identical vanilla behavior when
`kFeatures1_RandomizerActive` is clear:

1. **`LOC_Digging_Game`** (location id 228) — the Digging Game. Dispatched at the
   PoH "win" outcome in `DiggingGameGuy_AttemptPrizeSpawn` (`src/player.c`); on a
   direct-grant placement the `0xeb` reward sprite is suppressed so the player
   doesn't also receive the vanilla Piece of Heart.
2. **`LOC_Hype_Cave_NPC`** (location id 227) — the gift NPC in Hype Cave (the 4
   Hype Cave chests are wired separately via the §6.3 universal chest hook).
   Dispatched in `NiceThiefWithGift` (`src/sprite_main.c`), gated on the full
   16-bit room index `0x11E` and passing the `0xFFFF` registry-id convention so a
   placed item can't mis-grant the vanilla 300-rupee code.
3. **`LOC_Hammer_Pegs`** (location id 218) — the hammer-pegs Piece of Heart.
   Dispatched at the once-per-save 22nd-peg trigger in `HandlePegPuzzles`
   (`src/overworld.c`, screen 98); the obtained-bit is set before the tile reveal
   so the vanilla standing Piece of Heart self-cancels.
4. **`LOC_Chest_Game`** — the Treasure-Chest minigame in the Village of Outcasts.
   Dispatched at the rare-prize branch of `OpenMiniGameChest` (`src/dungeon.c`),
   which fires once per save (a `dung_savegame_state_bits` gate). The fork models
   the game as this single rare-prize location, not three placement slots.

#### Scenario: Digging Game routed through dispatcher
- **WHEN** the player wins a Digging Game dig in a rando slot
- **THEN** `Rando_DispatchVanillaGrant(LOC_Digging_Game, ...)` fires; the placed
  item is granted and the vanilla Piece-of-Heart reward sprite is suppressed on a
  direct-grant placement

#### Scenario: Hammer Pegs and Hype Cave NPC route through the dispatcher
- **WHEN** the player hammers the last peg (Hammer Pegs) or receives the Hype
  Cave gift NPC's item in a rando slot
- **THEN** the placed item for `LOC_Hammer_Pegs` / `LOC_Hype_Cave_NPC` is granted
  once, and the corresponding vanilla reward does not also spawn

#### Scenario: Vanilla mode minigames unchanged
- **WHEN** the binary is in vanilla mode (`kFeatures1_RandomizerActive` clear) and
  the player wins any of the four minigames
- **THEN** the minigame grants its vanilla item; `g_ram` after the grant is
  bit-identical to pre-rando-change behavior

#### Scenario: Treasure-Chest minigame dispatches its single reward location
- **WHEN** the player wins the once-per-save rare prize in the Treasure-Chest
  minigame
- **THEN** dispatch fires once for `LOC_Chest_Game`; consolation outcomes stay
  vanilla
