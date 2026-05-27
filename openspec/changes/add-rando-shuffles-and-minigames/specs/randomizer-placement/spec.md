## ADDED Requirements

### Requirement: §6.8 Minigame dispatch sites

Four minigame sites SHALL route through `Rando_OnLocationCheck(LOC_<...>, vanilla_item_id)`:

1. **`LOC_Digging_Game`** (location id 228) — Digging Game in the Light World. Dispatch fires when the player wins a dig and the digging-game-guy sprite hands over the item. Sprite handlers in `src/sprite_main.c` (`SpritePrep_DiggingGameGuy_bounce` at line 7772, draw at 19407+). Exact patch point deferred to apply-time grep.
2. **`LOC_Hype_Cave_NPC`** (location id 227) — the soldier NPC in Hype Cave (the 4 chests in Hype Cave are already wired via the §6.3 universal chest hook per `src/rando/chest_lookup.h:203-206`).
3. **Peg Cave** — hammer-pegs reward chest opening. Location ID may need to be added to `assets/rando/location_registry.yaml`; verify at apply-time. Sprite handlers in `src/sprite_main.c` (hammer-pegs sprite + reward-chest open).
4. **Treasure-Chest minigame** — the "pick one of three chests" game in Village of Outcasts. Special handling: the game has 3 candidate chests but only 1 reward; dispatch fires for the picked chest only.

When `kFeatures1_RandomizerActive` is clear, the minigame handlers SHALL preserve byte-identical vanilla behavior — the dispatcher fall-back returns the vanilla item id and the inline grant proceeds.

> **Stub status**: per-site patch points deferred to apply-time. Peg Cave location-id presence verification deferred.

#### Scenario: Digging Game routed through dispatcher
- **WHEN** the player wins a Digging Game dig in a rando slot
- **THEN** `Rando_OnLocationCheck(LOC_Digging_Game, <vanilla>)` fires; the placement-table substitute is granted; the spoiler reflects the assignment

#### Scenario: Vanilla mode minigames unchanged
- **WHEN** the binary is in vanilla mode (`kFeatures1_RandomizerActive` clear) and the player wins a Digging Game dig
- **THEN** the Digging Game grants its vanilla item; `g_ram` after the grant is bit-identical to pre-rando-change behavior

#### Scenario: Treasure-Chest minigame dispatches only the picked chest
- **WHEN** the player picks chest #2 of 3 in the Treasure-Chest minigame
- **THEN** dispatch fires once for the picked chest's `LOC_<id>`; the other two chests do NOT dispatch (they were never reachable in this play)
