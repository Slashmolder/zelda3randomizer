## ADDED Requirements

### Requirement: Inverted Dark World → Light World under-rock world-warps

In Inverted mode the Magic Mirror only carries Light World → Dark World, so the way OUT of the Dark World SHALL be a fixed set of "world-warp" tiles hidden under liftable rocks on specific Dark-World overworld screens. Each warp is realized by three layers that MUST agree per screen: (a) a type-`0x82` overworld-secret record added by `InvertedSecrets_Install` (`src/rando/inverted_entrances.c`); (b) a liftable rock map16 tile placed by the inverted overlay (`src/rando/inverted_maps.c`, applied by `Overworld_ApplyInvertedTiles`); and (c) the walk-in placer `Overworld_EnsureInvertedWarpRock` (`src/overworld.c`).

The warp screens, `dung_bg2` tile positions, and rock map16 tiles SHALL be: `0x73`/`0x02A8`/`0x020F`, `0x50`/`0x0B2E`/`0x020F`, `0x6F`/`0x0BB2`/`0x020F`, `0x4E`/`0x1D4A`/`0x0239`, `0x70`/`0x1D94`/`0x0239`, `0x78`/`0x1D94`/`0x0239`, `0x75`/`0x0F50`/`0x0239`, and `0x47`/`0x069E`+`0x06A4`/`0x0239`. These mirror the vanilla Light-World hardcoded rocks relocated to the Dark World (ALTTPR `Rom.php` `setInvertedMode()` + z3randomizer `inverted.asm` `HardcodedRocks`); no new rock art is introduced.

Lifting a warp rock SHALL reveal map16 tile `0x0212` (overworld collision `TileBehavior_Warp` / `0x4b`); stepping onto the revealed warp SHALL flip `savegame_is_darkworld` (Dark↔Light). The warp SHALL be reusable (a repeatable world flip) and therefore SHALL NOT be gated on a location-checked or consumed flag. The warp rocks require the Power Glove to lift; this is intended — the inverted logic graph accounts for Dark-World escape being gated on the Power Glove (no glove-less escape is expected). All behavior SHALL be inert unless `enhanced_features1 & kFeatures1_RandomizerActive` and the active `world_state` is Inverted; vanilla / Open / Standard / Retro overworld tiles SHALL be byte-identical to the pre-change build.

#### Scenario: Walking onto a Dark-World warp screen exposes the liftable rock and warps to the Light World
- **WHEN** an Inverted player who has the Power Glove walks (overworld scroll, not a full screen rebuild) onto a warp screen — e.g. screen `0x73` — and reaches the warp position `0x02A8`
- **THEN** a liftable rock (map16 `0x020F`) is present at that tile, asserted by `Overworld_EnsureInvertedWarpRock` the frame the screen settles
- **AND** lifting the rock reveals warp tile `0x0212`, and stepping onto it flips `savegame_is_darkworld` from `0x40` (Dark) to `0x00` (Light)

#### Scenario: The warp rock appears regardless of entry method
- **WHEN** a warp screen is entered by ANY method — walking-scroll, Magic Mirror, flute, cave/building exit, or save-and-quit
- **THEN** the liftable rock is present at the warp position: the inverted overlay places it on full screen rebuilds, and `Overworld_EnsureInvertedWarpRock` covers the walking-scroll path that the overlay skips

#### Scenario: A revealed warp is not re-covered before the player can use it
- **WHEN** the player lifts a warp rock so the tile becomes the revealed warp `0x0212`, then remains on the screen
- **THEN** the per-frame placer leaves the tile as `0x0212` — it places the rock only when the current tile is neither the rock nor the revealed warp — so the player can still step onto the warp

#### Scenario: The Light-World counterpart rocks are absent in Inverted
- **WHEN** an Inverted player is on Light-World screen `0x33` or `0x2F` (the vanilla hardcoded-rock screens), reached by walk-in OR full rebuild
- **THEN** the vanilla hardcoded rock is NOT drawn there (removed in Inverted by `Overworld_HandleOverlaysAndBombDoors`) — the rock exists only on the Dark-World side under the warp

#### Scenario: Inert outside Inverted mode
- **WHEN** the active world-state is not Inverted (vanilla / Open / Standard / Retro), or no randomizer slot is active
- **THEN** `Overworld_EnsureInvertedWarpRock` early-returns and writes no tiles, so overworld map16 tiles are byte-identical to the pre-change build
