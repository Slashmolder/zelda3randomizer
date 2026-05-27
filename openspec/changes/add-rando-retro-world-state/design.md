# Slice 3 Retro — translation research & design proposal

**Status**: APPROVED. Risks 1 + 3 (TakeAny enumeration + missing item IDs) resolved 2026-05-27 per user-approved deep-dive recommendation; see §4 Resolutions. Slice now split 3a/3b per §5 — this proposal covers 3a only.

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

## 4. Risks / unknowns — resolutions

**Risk 1 — TakeAny set is RNG-driven, not enumerable.** **RESOLVED 2026-05-27**: defer
TakeAny entirely to Slice 3b (matches Risk 6 recommendation). The 22 TakeAny shops
require new sprite-handler infrastructure this fork does not yet have (`app/Shop/TakeAny.php`
is an empty subclass — the activation mechanism is ALTTPR's `setActive()` ROM
patching, not present here). Adding 44 (Option A) or 22 (Option B) registry IDs
now would burn 2 corpus regenerations on entries that can never be reached.
Slice 3a ships the 9 regular shops + Capacity Upgrade — ~80% of the Retro
player experience. The Option A/B choice happens in 3b, informed by whatever
dispatch shape lands then.

**Risk 2 — Shop+slot identity is not tracked on the shop-item sprite.** `Sprite_BB_Shopkeeper` (`sprite_main.c:25236`) uses `sprite_subtype2[k]` to distinguish item *kind*, not shop+slot.

**Apply-time deep-dive finding (2026-05-27)**: the disambiguation is harder than originally scoped. Findings:

1. **`ShopKeeper_SpawnShopItem(int k, int pos, int what)`** at `sprite_main.c:25426`: the `pos` parameter (0/1/2 slot index) is consumed only by `kShopKeeper_ItemX[pos]` for the spawn X coordinate. It is **NOT stored on the spawned sprite**. Both `sprite_subtype2[j]` and `sprite_ignore_projectile[j]` are set to `what` (item kind), not `pos`. The slot index is lost by receipt time.

2. **`kShopKeeperWhere[13]`** at `sprite_main.c:7800` lists 13 dungeon-room indices, but only **5 of those rooms** are actual shop-dispatch sites (cases 0/1/5/7/8). The other 8 cases route to ChestGameGuy / NiceThief / minigame handlers.

3. **Multiple ALTTPR shops share the same `dungeon_room_index`**: ALTTPR enumerates 9 regular shops; the C fork's dispatch covers only 5 unique rooms. The missing 4 are distinguished via `is_in_dark_world` (savegame_is_darkworld) — the same indoor room ID is reused for LW vs DW shop variants. Verify: room 0x12 in LW = LW Lake Hylia Shop, room 0x12 in DW = DW Lake Hylia Shop (or similar; needs apply-time grep to confirm exact pairing).

**Implementation requirements** for #53:

- Plumb `pos` into `ShopKeeper_SpawnShopItem` → spawned sprite's fields. `sprite_ignore_projectile` is unsafe (`pos=0` would disable projectile-ignore for slot-0). Recommend storing in `sprite_C[k]` (verified unused by `Sprite_BB`'s ShopItem_* paths at `sprite_main.c:25245-25251`).
- Build a `kShopLocationLookup[5][2][3]` table — `[room_index][is_dark_world][pos] → LOC_id`. 5 rooms × 2 worlds × 3 slots = 30 entries. For unmapped entries return `LOC_NONE` (vanilla passthrough).
- Wire each of the 7 `ShopItem_*` handlers (sprite_main.c:25418-25559) through a new `ShopItem_DispatchVanillaGrant(int k, uint16 vanilla_item, uint8 lttp_code)` helper that reads `sprite_C[k]` (pos) + `dungeon_room_index` + `savegame_is_darkworld`.

Tracked as **task #53**; deferred from 3a's main bundle (multi-hour focused work). The placement-table is correct and player-visible randomization in shops will activate when #53 lands.

**Risk 3 — ShopArrow / ShopKey / BluePotion (unbottled) / RedPotion (unbottled) / Heart-refill / Bee (unbottled) / BlueShield / RedShield / BombUpgrade5 / ArrowUpgrade5 / GenericKey items don't exist in `assets/rando/item_registry.yaml`.** **RESOLVED 2026-05-27**: match ALTTPR's own alias/distinct mix. Verified against `app/Item.php:107, 138-141, 168-170, 253` and `:270-271`. **Net 7 new distinct item-registry IDs** (the other 4 alias to existing entries):

| Item | ALTTPR ROM byte | Recommendation |
|---|---|---|
| ShopArrow | (alias) | Use existing `ITEM_Arrow1`; ALTTPR's `ItemAlias('ShopArrow', 'Arrow')` |
| ShopKey | 0xAF (KeyGK) | **NEW**: `ITEM_GenericKey` (alias collapses to it; ALTTPR's `ItemAlias('ShopKey', 'KeyGK')`) |
| BluePotion (unbottled) | 0x30 | **NEW**: `ITEM_BluePotion` (distinct from `BottleWithBluePotion` 0x2D) |
| RedPotion (unbottled) | 0x2E | **NEW**: `ITEM_RedPotion` (distinct from `BottleWithRedPotion` 0x2C) |
| Bee (unbottled) | 0x0E | **NEW**: `ITEM_BeeContents` (distinct from `BottleWithBee` 0x3C) |
| BlueShield | 0x04 | Use existing `ITEM_FighterShield` (same byte; ALTTPR-name only) |
| RedShield | 0x06 | Use existing `ITEM_RedShield` |
| BombUpgrade5 | 0x51 | **NEW**: `ITEM_BombUpgrade5` |
| ArrowUpgrade5 | 0x52 | **NEW**: `ITEM_ArrowUpgrade5` |
| Heart-refill | 0x42 | **NEW**: `ITEM_HeartRefill` |

Rationale: matches ALTTPR's architecture at the byte level (cross-tool diff-ability), keeps the registry honest about distinct vs alias forms, and the aliases live as comments at dispatch sites rather than registry pollution. One kGenVer bump (14→15) lands all 7 in 3a.

**Risk 4 — proposal.md:11 claims "42 shop entities"; actual count ~32.** **RESOLVED**: proposal.md already corrected to "32 = 9 regular + 1 Capacity-Upgrade + 22 TakeAny" per task #55. No further action.

**Risk 5 — Inverted-only "Dark World Lake Hylia Shop" exclusion** (per `Randomizer.php:740`) is irrelevant in Retro+Open since Retro extends Open. Document only.

**Risk 6 — Take-Any dispatch mechanism unspecified.** **RESOLVED**: defer to Slice 3b (see Risk 1 resolution).

**Risk 7 — TakeAny activation under hard-mode flag interaction with `setShops()` / `clearInventory()` is unverified.** Moved to Slice 3b scope — not blocking 3a.

**Risk 8 — Retro runtime gameplay flags are NOT pinned at the runtime layer** (task #84, verified 2026-05-27). Slice 3a wired the LOGIC + PLACEMENT side: `world_state == Retro` filters in the right shop locations, placer pins ShopUpgrade slots, picker accepts Retro. But the 4 gameplay flags from `app/World/Retro.php:18-23` are NOT actually pinned in the game runtime:

- **`rom.rupeeBow`** (bow consumes rupees instead of arrows) — not wired. In Retro, shooting an arrow should deduct rupees from `link_rupees_goal`, not from `link_num_arrows`. Find the bow-fire handler in `src/player.c` / `src/ancilla.c` and add a Retro branch.
- **`rom.genericKeys`** (small keys are inter-dungeon transferable) — not wired. In Retro, all small keys should consume from a single `link_num_keys`-style pool, not per-dungeon counters. Find the `link_num_keys` increment + small-key-door consume sites and re-route under Retro.
- **`rom.wildKeys`** — partially logic-aware (keys can spawn anywhere in the placement pool via dungeon_items.small_keys=any settings combo), but the runtime side doesn't change. Verify this matches ALTTPR's intent.
- **`region.takeAnys`** — covered by Slice 3b.

Grep confirms: `rupeeBow`, `genericKeys`, `wildKeys` appear nowhere in `src/` outside the settings-parse layer.

Without runtime wiring, Retro mode is effectively "Open + extra shop placements in the logic" — the actual Retro GAMEPLAY differences (the entire reason ALTTPR has a Retro mode) don't fire in the in-game experience. Tracked as task #84; should land in a follow-up after #53 (shop dispatch).

## 5. Scope split (locked in)

**Slice 3a** (this proposal post-resolution):
- 7 new item-registry IDs (Risk 3 resolution).
- 29 shop-purchase location IDs from §1a (27 regular + 2 Capacity-Upgrade identity-placed).
- BuildItemPool Retro branch (only the §2 sections (a) static and (c) regular-shop extras; section (b) TakeAny deferred to 3b).
- Shop-handler instrumentation at the 7 sprite_main.c sites in §3 (regular shops only).
- Picker un-gate.
- kGeneratorVersion bump + corpus regen.

**Slice 3b** (deferred, separate change folder):
- 22 TakeAny location IDs (Option A vs Option B decided then).
- TakeAny dispatch infrastructure (the missing ROM-table mechanism).
- RNG-driven `randomCollection(4)` + `randomCollection(5)` replication.
- ProgressiveSword / ThreeHundredRupees "5th TakeAny" activation logic.
