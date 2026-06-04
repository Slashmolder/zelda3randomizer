## MODIFIED Requirements

### Requirement: Item types receivable via dispatcher

The dispatcher SHALL grant any item type listed in the audit deliverable's "receivable items" enumeration. Phase A receivable items include:

- **Progressive items** (mandatory for ALTTPR-style placement): `ProgressiveSword`, `ProgressiveShield`, `ProgressiveArmor`, `ProgressiveGlove`, `ProgressiveBow`. Each grant advances the corresponding `link_item_*` by one level via a new helper; at-max grants are a generator error in well-formed seeds.
- All vanilla absolute `link_item_*` items, **`SilverArrowUpgrade`** (standalone upgrade item used in absolute-bow mode).
- **`TriforcePiece`** — required by Triforce Hunt and Ganon Hunt; grant path adds 1 to the player's triforce-piece counter and updates the HUD.
- **Magic upgrades** as items: `HalfMagic`, `QuarterMagic` — grant path writes to `link_magic_consumption` (1 = full, 2 = half, 4 = quarter per vanilla semantics; new helper).
- **Bottle-with-contents** as distinct item IDs: `BottleEmpty`, `BottleWithFairy`, `BottleWithBee`, `BottleWithGoodBee`, `BottleWithRedPotion`, `BottleWithGreenPotion`, `BottleWithBluePotion`.
- **Heart items** as two distinct IDs: `PieceOfHeart` (granted via the vanilla PoH path — 4 pieces = +1 max HP via the existing quarters mechanic) and `BossHeartContainer` (granted via a direct +1 max HP path, no quarters mechanic). The dispatcher routes each ID to the correct receive code.
- Small keys (per dungeon), big keys (per dungeon), maps, compasses.
- Multi-tier rupees: `Rupee1`, `Rupee5`, `Rupee20`, `Rupee100`, `Rupee300`.
- Junk pool: `SmallMagic`, `Arrow1`, `Arrow10`, `Bombs1`, `Bombs3`, `Bombs10`. **`Rupoor` is in the receivable enumeration but enters the pool only when `item_pool_difficulty ∈ {hard, expert}`** — the dispatcher still needs the grant path so a hard-pool seed actually works.
- Prize items (when prize shuffle is enabled): `Prize_Crystal1..7`, `Prize_GreenPendant`, `Prize_RedPendant`, `Prize_BluePendant`. Granted by clearing the dungeon to which prize shuffle assigned the prize; dispatch site is the boss-death code path in `dungeon.c`.

Item types not in the receivable enumeration SHALL NOT be placed by the generator.

#### Scenario: Small-key receive
- **WHEN** the dispatcher grants a small key from a chest in a dungeon other than that key's vanilla home
- **THEN** the small-key counter for the destination dungeon increments, a small-key receive animation plays (new code path; vanilla had no such animation), and the chest-cleared flag is set

#### Scenario: Multi-tier rupee receive — small tiers silent
- **WHEN** the dispatcher grants `Rupee1`, `Rupee5`, `Rupee20`, or `Rupee100` into any chest
- **THEN** Link's rupee counter increments by the documented value, the standard rupee-pickup SFX plays, and no item-receive cutscene fires

#### Scenario: Rupee300 plays the item-receive cutscene (ALTTPR convention)
- **WHEN** the dispatcher grants `Rupee300` (purple rupee — the canonical Misery Mire gift)
- **THEN** Link's rupee counter increments by 300, the full item-receive cutscene plays mirroring ALTTPR's behavior (player gets the "you found a purple rupee" beat), and the chest-cleared flag is set

#### Scenario: Progressive sword grant advances by one level
- **WHEN** the dispatcher grants `ProgressiveSword` while `link_item_sword == L2_MasterSword`
- **THEN** `link_item_sword` advances to L3, the appropriate receive animation plays, and HUD updates accordingly

#### Scenario: Progressive sword grant at max is a no-op or junk-fill
- **WHEN** the dispatcher grants `ProgressiveSword` while `link_item_sword == L4_GoldenSword`
- **THEN** the dispatcher records a generator error in the audit log (this case SHALL NOT occur for valid placements: the item pool contains exactly the right number of progressive items for the active mode); a debug-build asserts; release behavior is to silently grant a `Rupee5` placeholder so gameplay does not soft-lock

#### Scenario: TriforcePiece grants increment counter and HUD
- **WHEN** the dispatcher grants `TriforcePiece` and the player has `triforce_pieces < pieces_required`
- **THEN** the triforce-piece counter increments, the HUD updates, the goal predicate is re-evaluated, and the standard receive animation/SFX plays

#### Scenario: HalfMagic / QuarterMagic grant
- **WHEN** the dispatcher grants `HalfMagic`
- **THEN** `link_magic_consumption` is set to 2 (half) if currently 1 (full); the standard receive animation plays; subsequent `QuarterMagic` grant sets it to 4

#### Scenario: PieceOfHeart vs BossHeartContainer routing
- **WHEN** the dispatcher grants `PieceOfHeart`
- **THEN** the vanilla PoH path runs (4-quarter mechanic; +1 max HP every 4 pieces)
- **AND WHEN** the dispatcher grants `BossHeartContainer`
- **THEN** `+1 max HP` is applied directly without consuming any piece-of-heart quarters

#### Scenario: Boss kill dispatches TWO locations
- **WHEN** the player defeats a dungeon boss (e.g., Helmasaur King in Palace of Darkness)
- **THEN** the boss-death code path calls `Rando_OnLocationCheck(PalaceOfDarkness_BossHeart, BossHeartContainer)` AND separately `Rando_OnLocationCheck(PalaceOfDarkness_Prize, Prize_Crystal_PoD_Vanilla)`, granting whatever the placement table has at each location ID

#### Scenario: Phase A boss-heart slots are identity-placed
- **WHEN** Phase A generates a seed
- **THEN** each of the 10 `<Dungeon>_BossHeart` slots in the placement table is hardcoded to `BossHeartContainer` (the placer does not shuffle them); the dispatcher still fires uniformly via the existing code path but every boss kill grants the heart container at that dungeon. Phase B's `bossHeartsInPool=true` setting (when added) would let these slots participate in the shuffle

#### Scenario: Great-fairy ponds grant two reach-only checks on contact (chest model)
- **WHEN** the player contacts a great-fairy pond — the Pyramid Fairy in the Dark World or the Waterfall of Wishing in the Light World — AND `kFeatures1_RandomizerActive` is set
- **THEN** the pond grants the next un-collected of its TWO checks DIRECTLY on contact, with no "throw an item in" interaction, no item-picker, and no consume/upgrade: Waterfall `waterfall_fairy_left` / `waterfall_fairy_right`, Pyramid `pyramid_fairy_left` / `pyramid_fairy_right`. This mirrors ALTTPR, which replaces both ponds with two chests. A receivable item runs the standard over-head receive; a direct-write item fires the §7.6 confirmation cue. Each pond's two checks are **reach-only** (no sword/bow/throwable-item requirement), so the runtime requirement equals the placement logic ("reach the pond")
- **AND** the Pyramid's vanilla "throw your sword/bow in to upgrade" Trade slots (`pyramid_fairy_sword`, `pyramid_fairy_bow`) are NOT used under rando and are RETIRED from the placement pool; the Waterfall has no such Trade slots
- **AND WHEN** `kFeatures1_RandomizerActive` is clear
- **THEN** the vanilla throw-in upgrade shrine runs unchanged and no `Rando_OnLocationCheck` fires
