## MODIFIED Requirements

### Requirement: Item types receivable via dispatcher

The dispatcher SHALL grant any item type listed in the audit deliverable's "receivable items" enumeration. Phase A receivable items include:

- **Progressive items** (mandatory for ALTTPR-style placement): `ProgressiveSword`, `ProgressiveShield`, `ProgressiveArmor`, `ProgressiveGlove`, `ProgressiveBow`. Each grant advances the corresponding `link_item_*` by one level via a new helper; at-max grants are a generator error in well-formed seeds.
- All vanilla absolute `link_item_*` items, **`SilverArrowUpgrade`** (standalone upgrade item used in absolute-bow mode).
- **`TriforcePiece`** — required by Triforce Hunt and Ganon Hunt; grant path adds 1 to the player's triforce-piece counter and updates the HUD.
- **Magic upgrades** as items: `HalfMagic`, `QuarterMagic` — the grant is **strictly progressive**: each magic upgrade advances `link_magic_consumption` by exactly one tier (capped at the maximum), regardless of which of the two items it is. **This port uses `0 = full, 1 = half, 2 = quarter`** (the cost table is indexed `item*3 + consumption`), NOT the vanilla-SNES `1/2/4` convention. Grant path: `magic_upgrade_direct_grant` plus the Magic Bat identity write. This deliberately diverges from ALTTPR (whose `QuarterMagic` jumps straight to quarter); capping at +1 tier wastes no pickup and cannot skip the half tier, and is placement-neutral because no logic predicate requires quarter specifically (the magic macro is satisfied at ≥ half).
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

#### Scenario: HalfMagic / QuarterMagic grant is strictly progressive
- **WHEN** the dispatcher grants a magic upgrade (`HalfMagic` OR `QuarterMagic`) while `link_magic_consumption == 0` (full)
- **THEN** `link_magic_consumption` advances to 1 (half) — the 1st magic upgrade collected is always half, regardless of which of the two items it is
- **AND WHEN** a second magic upgrade (`HalfMagic` OR `QuarterMagic`) is granted while `link_magic_consumption == 1`
- **THEN** it advances to 2 (quarter); any further magic-upgrade grant is capped at 2 (never exceeds quarter, never downgrades)

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

## ADDED Requirements

### Requirement: Shared-byte item grants — never-downgrade, progressive collapse, and item-menu swap

Several items that vanilla packs two-or-more tiers into a single Y-slot byte are shuffled by the randomizer as independent items. Under `kFeatures1_RandomizerActive`, the grant path SHALL NOT let acquiring a lower tier after a higher one downgrade the slot, and SHALL track true ownership independently of the shared byte so a player who owns multiple tiers never loses one. Specifically:

- **Never-downgrade (all shared-byte upgrade items).** An absolute byte-write SHALL only raise the byte (write iff `v > current`), never lower it — protecting sword, shield, gloves, mail, bow, and boomerang from out-of-order downgrades. This clamp SHALL EXEMPT non-tier counter bytes: `link_arrow_filler` (receive codes `0x43`/`0x44`) is a per-frame drain countdown, not a tier, and SHALL keep the vanilla absolute write (the clamp would otherwise silently drop a paid arrow grant while a prior fill is still draining).
- **Progressive collapse (boomerang, magic).** Where the two shuffled tiers are **logically interchangeable** (no logic predicate distinguishes them), the grant SHALL collapse them into a single progressive ladder: the 1st collected gives tier 1, the 2nd gives tier 2, regardless of which item is placed. Boomerang (blue = 1, red = 2) and magic (half = 1, quarter = 2) are collapsed this way.
- **Item identity preserved when a tier is a logic gate (bow).** Where a logic predicate DOES distinguish the tiers — silver vs wood arrows for the bow — the grant SHALL keep item identity (a wood-bow item grants wood, a silver-arrow item grants silver) under never-downgrade, so the placer cannot desync from runtime.
- **Item-menu swap (Press A).** When the player owns BOTH tiers of a shared-slot item, pressing A on that highlighted slot in the inventory menu SHALL swap which tier the slot performs (the shared byte tracks the *selected* tier): flute↔shovel, blue↔red boomerang, wood↔silver arrows. Ownership SHALL persist across save/reload via per-item bitfields carried additively in the slot-header reserved tail (`boomerang_owned` @73, `bow_owned` @74 — no `format_version` bump), so a swapped-down byte never loses the higher tier.

#### Scenario: Progressive boomerang ignores item identity
- **WHEN** the dispatcher grants either a `BlueBoomerang` or a `RedBoomerang` item while the player owns no boomerang
- **THEN** the player receives the blue boomerang (tier 1), regardless of which item was placed
- **AND WHEN** a second boomerang item (either color) is granted
- **THEN** the player receives the red boomerang (tier 2) and owns both; pressing A on the boomerang slot swaps the selected color

#### Scenario: Bow keeps identity and never downgrades
- **WHEN** the player owns the silver bow and the dispatcher grants a wood-bow item
- **THEN** the bow strength is unchanged (stays silver) — never downgraded
- **AND WHEN** the player owns both wood and silver
- **THEN** pressing A on the bow slot swaps between wood and silver arrows, preserving the arrow-present state

#### Scenario: Never-downgrade exempts the arrow-fill counter
- **WHEN** the dispatcher grants an arrow refill (receive code `0x43`/`0x44` → `link_arrow_filler`) while a prior fill is still draining
- **THEN** the new fill value is written absolutely (vanilla behavior), NOT clamped — the never-downgrade rule does not apply to drain/counter bytes

### Requirement: Trigger-based location re-collect safety

A location whose grant fires from a re-triggerable in-world action (an NPC summon, a dig, a tablet read) SHALL, under `kFeatures1_RandomizerActive`, gate its grant on `Rando_IsLocationChecked(LOC_*)` so the shuffled item is granted exactly once. Vanilla often relied on the grant's own side effect to disable re-triggering; the randomizer breaks that equivalence in two directions the gate fixes: (a) a vanilla precondition that, once satisfied from elsewhere, makes the location uncollectable (MISSABLE); (b) a re-enabled one-shot action that re-runs the non-idempotent grant (RE-GRANT / duplicate). The non-rando path SHALL remain byte-identical (RAM-compare preserved).

#### Scenario: Magic Bat is collectable regardless of magic level
- **WHEN** the player reaches the Magic Bat having already obtained quarter magic (`link_magic_consumption == 2`) from another location
- **THEN** the bat still appears and grants its shuffled item — the vanilla `link_magic_consumption >= 2` summon guard applies only off-rando; under rando `Rando_IsLocationChecked(LOC_Magic_Bat)` is the sole re-grant gate

#### Scenario: Flute Spot grants exactly once
- **WHEN** the player digs the Flute Spot, collects its shuffled item, then (via the flute/shovel decouple) toggles back to the shovel and re-digs the same tile
- **THEN** the re-dig grants nothing — the grant is gated on `!Rando_IsLocationChecked(LOC_Flute_Spot)`, preventing the duplicate that vanilla avoided only by flipping `link_item_flute` out of a shovel-selectable state
