# Slice 3 Retro — translation research & design proposal

**Status**: PROPOSAL. Authored 2026-05-27 by background research agent. Requires user decisions before implementation.

## 0. Summary of upstream model

Per `C:\src\alttp_vt_randomizer\app\World\Retro.php:18-23`, Retro forces 4 flags on top of Open: `rom.rupeeBow`, `rom.genericKeys`, `region.takeAnys`, `region.wildKeys`.

Per `app/World.php:520`, shop "locations" enter the placement pool via `$this->shops->getLocations()` (merged into `getCollectableLocations()`).

Per `app/Shop.php:144-163`, `Shop::getLocations()` returns one `Location` per populated inventory slot, named `"$shopName - $slot"`.

**Critical finding (Risk 1, below):** Per `app/Randomizer.php:716-735` and `:737-750`, which TakeAny and which regular shops receive extra ShopArrow/ShopKey/TenBombs/BluePotion items is chosen **per-seed by `randomCollection(4)` / `randomCollection(5)`**. ALTTPR therefore does *not* have a fixed enumeration of "Retro shop placement slots". The set differs every seed. Two viable translation strategies are presented in §1b.

## 1. Shop location inventory

### 1a. Static-vanilla slots (always present in Retro, identity-placed)

These are the default inventories declared at region construction, present in every Retro seed.

Proposed IDs continue from current max (236). Region tokens match existing entries.

| Proposed ID | Name | Region | Type | Vanilla item | PHP source |
|---|---|---|---|---|---|
| 237 | "Dark World Potion Shop - 0" | DarkWorld_NorthEast | ShopPurchase | RedPotion | `app/Region/Standard/DarkWorld/NorthEast.php:53` |
| 238 | "Dark World Potion Shop - 1" | DarkWorld_NorthEast | ShopPurchase | BlueShield | `app/Region/Standard/DarkWorld/NorthEast.php:54` |
| 239 | "Dark World Potion Shop - 2" | DarkWorld_NorthEast | ShopPurchase | Bombs10 (TenBombs) | `app/Region/Standard/DarkWorld/NorthEast.php:55` |
| 240 | "Dark World Forest Shop - 0" | DarkWorld_NorthWest | ShopPurchase | RedShield | `app/Region/Standard/DarkWorld/NorthWest.php:51` |
| 241 | "Dark World Forest Shop - 1" | DarkWorld_NorthWest | ShopPurchase | Bee | `app/Region/Standard/DarkWorld/NorthWest.php:52` |
| 242 | "Dark World Forest Shop - 2" | DarkWorld_NorthWest | ShopPurchase | Arrow10 | `app/Region/Standard/DarkWorld/NorthWest.php:53` |
| 243-251 | Dark World Lumberjack Hut / Outcasts / Lake Hylia Shops (3 slots each) | DarkWorld_NorthWest/South | ShopPurchase | RedPotion / BlueShield / Bombs10 | `app/Region/Standard/DarkWorld/NorthWest.php:55-61, South.php:52-54` |
| 252-254 | Dark World Death Mountain Shop (3 slots) | DarkWorld_DeathMountain_East | ShopPurchase | RedPotion / Heart / Bombs10 | `app/Region/Standard/DarkWorld/DeathMountain/East.php:45-47` |
| 255-257 | Light World Death Mountain Shop (3 slots) | LightWorld_DeathMountain_East | ShopPurchase | RedPotion / Heart / Bombs10 | `app/Region/Standard/LightWorld/DeathMountain/East.php:51-53` |
| 258-260 | Light World Kakariko Shop (3 slots) | LightWorld_NorthWest | ShopPurchase | RedPotion / Heart / Bombs10 | `app/Region/Standard/LightWorld/NorthWest.php:69-71` |
| 261-263 | Light World Lake Hylia Shop (3 slots) | LightWorld_South | ShopPurchase | RedPotion / Heart / Bombs10 | `app/Region/Standard/LightWorld/South.php:73-75` |
| 264-265 | Capacity Upgrade (2 slots, identity-placed) | LightWorld_South | ShopUpgrade | BombUpgrade5 / ArrowUpgrade5 | `app/Region/Standard/LightWorld/South.php:69-70` |

All entries should carry `world_state_filter: [retro]` so they only enter the pool when `settings.world_state == Retro` (mechanism: `kRandoLocations[i].world_state_filter` per `rando_placement.c:381-382`).

Per proposal.md:41 design decision, the Capacity Upgrade slots (264, 265) are listed but should be **identity-placed** in Phase B (not in the randomized pool). They exist as registry slots so the dispatcher can route grants through the standard codepath for uniformity.

That gives **27 non-Upgrade shop-purchase slots** for the placement pool, plus 2 identity-placed Upgrade slots.

### 1b. Retro-extra slots (added per-seed via `randomCollection`)

Per `app/Randomizer.php:716-724`, in Retro 4 TakeAny shops gain `BluePotion@slot0` and `BossHeartContainer@slot1`. Per `:737-750`, 5 random non-TakeAny / non-Upgrade shops gain `ShopArrow@0`, `ShopKey@1`, `TenBombs@2`. Per `:726-734`, a 5th TakeAny is set active with one item (`ProgressiveSword` or `ThreeHundredRupees`).

Because the *set* is RNG-chosen per seed, there are two acceptable encodings (decision deferred):

- **Option A** ("static enumeration"): Add registry entries for **all 22 TakeAny shops × 2 slots = 44 slots**, plus the 5 ShopArrow / 5 ShopKey extra slots on every regular shop. Mark each as `world_state_filter: [retro]`. The Retro placer picks 4-of-22 TakeAny by setting fixed-slot vanilla items at runtime; the unselected ones get junk identity-placed. Total new IDs: ~75.
- **Option B** ("active-only enumeration"): Replicate ALTTPR's RNG-driven `randomCollection(4)` selection in `BuildItemPool`, then only emit the chosen slots' location IDs into the placement pool. Registry entries for all 22 TakeAny + extra-regular-shop slots still exist (so IDs are stable across seeds), but they all carry `world_state_filter: [retro]` *and* a runtime "active" gate.

22 TakeAny shop names + PHP cites are in the full agent output (see git log for the original).

## 2. BuildItemPool branch outline

Insert immediately before the junk-pad loop at `src/rando/rando_placement.c:375`. Itemcounts trace ALTTPR's pool additions in `Randomizer.php:716-750`:

```c
  // ----- Retro world-state additions (per ALTTPR app/Randomizer.php:716-750) -----
  if (settings->world_state == kWorldState_Retro) {
    // (a) Static vanilla shop inventories (29 slots):
    n = pool_add(out_items, n, capacity, ID_RedPotion,    9);
    n = pool_add(out_items, n, capacity, ID_BlueShield,   4);
    n = pool_add(out_items, n, capacity, ID_Bombs10,      6);
    n = pool_add(out_items, n, capacity, ID_Heart,        3);
    n = pool_add(out_items, n, capacity, ID_RedShield,    1);
    n = pool_add(out_items, n, capacity, ID_Bee,          1);
    n = pool_add(out_items, n, capacity, ID_Arrow10,      1);

    // (b) Retro extras (Randomizer.php:716-734 TakeAny):
    n = pool_add(out_items, n, capacity, ID_BottleWithBluePotion, 4);
    n = pool_add(out_items, n, capacity, ID_BossHeartContainer,   4);
    if (settings->mode_weapons == kModeWeapons_Swordless ||
        settings->mode_weapons == kModeWeapons_Vanilla) {
      n = pool_add(out_items, n, capacity, ID_Rupee300, 1);
    } else {
      n = pool_add(out_items, n, capacity, ID_ProgressiveSword, 1);
    }

    // (c) Retro extras (Randomizer.php:737-750 regular-shop):
    n = pool_add(out_items, n, capacity, ID_ShopArrow,    5);  // NEW
    n = pool_add(out_items, n, capacity, ID_ShopKey,      5);  // NEW (generic key)
    n = pool_add(out_items, n, capacity, ID_Bombs10,      5);
  }
```

## 3. Shop-handler instrumentation

The pattern is the same as the Phase A potion-shop dispatch at `sprite_main.c:6934-6940`. Each `ShopItem_HandleReceipt(k, <code>)` call must wrap to route through the dispatcher when `kFeatures1_RandomizerActive` is set.

Call sites:

| Line | Handler | Vanilla LttP grant | Vanilla item |
|---|---|---|---|
| 25418 | `ShopItem_RedPotion150` | `0x2e` | RedPotion (unbottled) |
| 25455 | `ShopItem_FighterShield` | `0x04` | FighterShield (= BlueShield in ALTTPR) |
| 25478 | `ShopItem_FireShield` | `0x05` | MirrorShield (= RedShield in ALTTPR) |
| 25502 | `ShopItem_Heart` | `0x42` | Heart-refill |
| 25521 | `ShopItem_Arrows` | `0x44` | TenArrows |
| 25540 | `ShopItem_Bombs` | `0x31` | TenBombs |
| 25559 | `ShopItem_Bee` | `0x0e` | Bee (in bottle) |

There is **no Take-Any-specific sprite handler** in `sprite_main.c`. Take-Any "entry" appears to be an ALTTPR-side ROM-table mechanism — out of scope per proposal.md:37.

## 4. Risks / unknowns requiring user clarification

**Risk 1 — TakeAny set is RNG-driven, not enumerable.** Pick Option A or Option B (see §1b) before implementation; this determines whether `kGeneratorVersion` bump rules apply to all 44 IDs or only the 5 active ones.

**Risk 2 — Shop+slot identity is not tracked on the shop-item sprite.** `Sprite_BB_Shopkeeper` (`sprite_main.c:25236`) uses `sprite_subtype2[k]` to distinguish item *kind*, not shop+slot. To dispatch to the correct LOC, the dispatcher needs to derive shop identity from `dungeon_room_index` via a parallel `kShopkeeper_LocId[13][3]` table.

**Risk 3 — ShopArrow / ShopKey / BluePotion (unbottled) / RedPotion (unbottled) / Heart-refill / Bee (unbottled) / BlueShield / RedShield / BombUpgrade5 / ArrowUpgrade5 / GenericKey items don't exist in `assets/rando/item_registry.yaml`.** Item-pool work is blocked on a Phase B item_registry expansion. Decide whether to add these IDs or to alias existing IDs (e.g. `ShopArrow → ID_Arrow1`, `ShopKey → ID_GenericSmallKey`).

**Risk 4 — proposal.md:11 claims "42 shop entities"; actual count ~32.** Recommend updating proposal.md once the user confirms the count from the agent's grep.

**Risk 5 — Inverted-only "Dark World Lake Hylia Shop" exclusion** (per `Randomizer.php:740`) is irrelevant in Retro+Open since Retro extends Open. Document only.

**Risk 6 — Take-Any dispatch mechanism unspecified.** Recommend deferring TakeAny placements to Phase B Slice 3b so the bulk of Retro (9 regular shops + Capacity Upgrade) ships in 3a.

**Risk 7 — TakeAny activation under hard-mode flag interaction with `setShops()` / `clearInventory()` is unverified.** Worth a focused upstream read before lock-in.

## 5. Recommended scope split

Split Slice 3 into 3a (this proposal's §1a regular shops) and 3b (TakeAny + advanced extras). 3a unblocks the Retro flag for ~80% of the player experience; 3b lands the per-seed RNG-driven TakeAny activation after the supporting item_registry IDs land.
