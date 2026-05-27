## ADDED Requirements

### Requirement: Shop-handler dispatch routing

Shop-purchase sites SHALL route through `Rando_OnLocationCheck(LOC_<Shop_Slot>, vanilla_item_id)` instead of granting the shop's vanilla item inline. The primary dispatch site is `src/sprite_main.c:25308` (`ShopItem_HandleReceipt`, per `audit.md §0.1.4` shop subsystem enumeration). The dispatcher resolves the slot to the placement-table entry; the rupee cost SHALL remain under vanilla shop-pricing semantics (Phase B does not randomize shop prices).

When `kFeatures1_RandomizerActive` is clear (vanilla mode), shop handlers SHALL preserve byte-identical vanilla behavior — the dispatcher fall-back path returns the vanilla item id and the inline grant proceeds.

For shops outside the Retro placement pool (Open / Standard / Inverted seeds — where shops grant vanilla items), the dispatcher SHALL fall back to vanilla via the absent-from-table path; the audit-guard remains green because the dispatch fires uniformly regardless of placement-pool membership.

#### Scenario: Rupee cost preserved
- **WHEN** the player purchases at a shop in a Retro seed
- **THEN** rupees are deducted at the vanilla shop-pricing rate; the granted item is the placement-table substitute; the shop-purchase animation runs as in vanilla

#### Scenario: Vanilla mode shop unchanged
- **WHEN** the binary is in vanilla mode (`kFeatures1_RandomizerActive` clear) and the player purchases at any shop
- **THEN** the shop grants its vanilla item via the standard inline path; `g_ram` after the purchase is bit-identical to pre-rando-change behavior

#### Scenario: Non-Retro rando mode shops unchanged
- **WHEN** a non-Retro rando seed (Open / Standard / Inverted) is loaded and the player purchases at a shop
- **THEN** the dispatcher fires but the shop slot is not in the placement table; fall-back returns the vanilla item id; the standard inline grant proceeds

#### Scenario: Capacity upgrades dispatch identity
- **WHEN** the player buys a bomb-capacity upgrade in a Retro seed (`sprite_main.c:11483-11484`)
- **THEN** `Rando_OnLocationCheck(LOC_<CapacityUpgradeSlot>, ITEM_BombCapacityUpgrade)` fires; the placement table returns the identity substitute (`ITEM_BombCapacityUpgrade`); the vanilla capacity-upgrade write to `link_bomb_upgrades` proceeds

### Requirement: Take-Any cave shop dispatch

Take-Any caves (`20 Rupee Cave`, `50 Rupee Cave`, `Bonk Fairy (Light)`, `Bonk Fairy (Dark)`, `Desert Fairy`, `Good Bee Cave`, `Light Hype Fairy`, `Long Fairy Cave`, and any other `Shop\TakeAny` instance enumerated in ALTTPR) SHALL route through the same dispatcher entry point as standard shops, OR a peer entry if the Take-Any sprite handler is a different code path.

The decision (single entry vs. peer entry) is recorded in design.md after apply-time `src/sprite_main.c` grep against the Take-Any sprite-handler code. The spec's normative requirement is "Take-Any purchases are dispatched"; the implementation choice is open.

#### Scenario: Take-Any cave in Retro grants placement-table substitute
- **WHEN** the player enters `20 Rupee Cave` in a Retro seed and accepts the offer
- **THEN** dispatch fires with `LOC_<TakeAny_20RupeeCave>` (or similar id); the placement-table substitute is granted; the 20-rupee cost is deducted at vanilla rate

#### Scenario: Take-Any cave in Open is not enterable
- **WHEN** the player walks up to the entrance of `20 Rupee Cave` in an Open seed
- **THEN** the entrance does not accept the player; vanilla behavior holds (Take-Any caves are gated by `region.takeAnys = false` in Open)
