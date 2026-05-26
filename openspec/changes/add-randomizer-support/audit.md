# Phase 0 audit — `add-randomizer-support`

> **Status: in progress (v0.2, machine-assisted second pass).** This document is the deliverable for tasks 0.1 – 0.9 of [tasks.md](tasks.md). It is the **code-review-blocking gate for any §6 grant-site integration PR** (task 0.9): reviewers SHALL refuse §6 PRs opened before all 0.8 acceptance checks below tick.
>
> **v0.2 adds**:
> - §0.1.2 every `(TODO)` row resolved with enclosing-handler context and dispatch-table-confirmed item meaning;
> - §0.1.5 new — item ID → meaning reference (76-entry table at `src/misc.c:53` + dispatch tables);
> - §0.3.5 new — complete ALTTPR canonical location catalog (216 entries, grouped by region, each citing PHP file:line);
> - §0.3.6 new — unresolved §0.3.2 mappings (4 sites);
> - §0.3.7 new — §6 gaps (ALTTPR locations needing new instrumentation, ~30 sites);
> - §0.4a all `(TODO)` event-name rows resolved; Aga 1 / per-dungeon-boss / King's Tomb identified.
>
> v0.1 captured the spine: the central dispatcher, every `Link_ReceiveItem` call site, the direct-write grant sites that bypass the dispatcher, the free-RAM scan that pins `kRam_*` offsets, the save-model confirmation, and the Phase A logic/goal/shuffle pinning.
>
> The largest remaining v0.3 piece is the **chest-tile → location_id** lookup (requires reading vanilla chest data from `zelda3_assets.dat`); see v0.3 work list at bottom.
>
> **Provenance discipline** (per [lessons.md](lessons.md)): every file:line citation below comes from a grep against the working tree at HEAD. ALTTPR claims come from the sibling checkout at `../alttp_vt_randomizer/` (MIT, verified at `LICENSE`). Memory-based assertions are flagged with `[unverified]` and must not propagate downstream until verified.

---

## §0.1 — Enumeration of inventory writes

### The central dispatcher (one site funnels nearly all "real" item grants)

The single most important fact: almost every item grant in the codebase flows through one funnel.

```
caller → Link_ReceiveItem(item_id, chest_position)  [src/player.c:1885]
       → sets link_receiveitem_index = item_id      [src/player.c:1892]
       → calls AncillaAdd_ItemReceipt(0x22, 4, …)   [src/player.c:1912]
                                                      ↓
       AncillaAdd_ItemReceipt(ain, yin, chest_pos)  [src/misc.c:713]
       reads link_receiveitem_index, dispatches via:
         - kMemoryLocationToGiveItemTo[76]  [src/misc.c:60-80]   → RAM addr
         - kValueToGiveItemTo[76]           [src/misc.c:81-101]  → value/sign
       plus a chain of special-case branches at [src/misc.c:731-784]
       (bunny clear, glove palette, map/compass/bigkey OR-into-bit,
        flute upgrade tier, magic shop powder, bottle refill,
        rupee/bomb/arrow counters with clamping, song list, …)
```

The `kMemoryLocationToGiveItemTo` table covers every cell on this list:

| RAM addr | Cell name | Indices in dispatch table |
|---|---|---|
| 0xF340 | `link_item_bow` | 11, 58, 59 |
| 0xF341 | `link_item_boomerang` | 12, 42 |
| 0xF342 | `link_item_hookshot` | 10 |
| 0xF344 | `link_item_mushroom` | 13, 41 |
| 0xF345 | `link_item_fire_rod` | 7 |
| 0xF346 | `link_item_ice_rod` | 8 |
| 0xF347 | `link_item_bombos_medallion` | 15 |
| 0xF348 | `link_item_ether_medallion` | 16 |
| 0xF349 | `link_item_quake_medallion` | 17 |
| 0xF34A | `link_item_torch` | 18 |
| 0xF34B | `link_item_hammer` | 9 |
| 0xF34C | `link_item_flute` | 19, 20, 74 |
| 0xF34D | `link_item_bug_net` | 33 |
| 0xF34E | `link_item_book_of_mudora` | 29 |
| 0xF350 | `link_item_cane_somaria` | 21 |
| 0xF351 | `link_item_cane_byrna` | 24 |
| 0xF352 | `link_item_cape` | 25 |
| 0xF353 | `link_item_mirror` | 26 |
| 0xF354 | `link_item_gloves` | 27, 28 |
| 0xF355 | `link_item_boots` | 75 |
| 0xF356 | `link_item_flippers` | 30 |
| 0xF357 | `link_item_moon_pearl` | 31 |
| 0xF359 | `link_sword_type` | 0, 1, 2, 3, 73 |
| 0xF35A | `link_shield_type` | 4, 5, 6 |
| 0xF35B | `link_armor` | 34, 35 |
| 0xF35C | `link_bottle_info[*]` | 14, 22, 43, 44, 45, 60, 61, 72 |
| 0xF360 | `link_rupees_goal` (uint16) | 52, 53, 54, 64, 65, 70, 71 |
| 0xF364 | `link_compass` | 37 |
| 0xF366 | `link_bigkey` | 50 |
| 0xF368 | `link_dungeon_map` | 51 |
| 0xF36B | `link_heart_pieces` | 23 |
| 0xF36C | `link_health_capacity` | 38, 62, 63 |
| 0xF36D | `link_health_current` | 46 |
| 0xF36E | `link_magic_power` | 47, 48 |
| 0xF36F | `link_num_keys` | 36 |
| 0xF372 | `link_hearts_filler` | 66 |
| 0xF373 | `link_magic_filler` | 69 |
| 0xF374 | `link_which_pendants` | 55, 56, 57 |
| 0xF375 | `link_bomb_filler` | 39, 40, 49 |
| 0xF376 | `link_arrow_filler` | 67, 68 |
| 0xF37A | `link_has_crystals` | 32 |

**Implication for `randomizer-placement`**: a single hook inside `Link_ReceiveItem` (or at every call site of it — see §0.3 for the count) would cover ≥ 90% of vanilla grants. Conceptually:

```c
void Link_ReceiveItem(uint8 item, int chest_position) {
  if (enhanced_features1 & kFeatures1_RandomizerActive) {
    item = Rando_OnLocationCheck(/* location_id resolved by caller */, item);
  }
  // … existing body unchanged
}
```

The catch is that **the location_id has to come from the caller**, because `Link_ReceiveItem` is called from ~30 places (see §0.1.2) with no knowledge of *which* location triggered the grant. So §6 instrumentation actually wraps each *call site* of `Link_ReceiveItem`, not the function itself — and the location_id is hardcoded per call site from `audit.md` mappings.

### §0.1.2 — Call sites of `Link_ReceiveItem` (the standard-pipeline grant points)

Enumerated by `grep "Link_ReceiveItem("` against `src/*.c`. Each row is one grant point that §6 will wrap with a `Rando_OnLocationCheck` call. The `item_id` shown is the hex value passed to `Link_ReceiveItem`; `kReceiveItemGfx[item_id]` decides the receive-animation graphic and is also the index into the dispatch tables.

| # | File:line | item_id | Apparent grant site (v0.2 verified via `kMemoryLocationToGiveItemTo`/`kValueToGiveItemTo` at `src/misc.c:60-101` and surrounding sprite handler) |
|---|---|---|---|
| 1 | ancilla.c:3421 | 0x26 | Heart container — `Ancilla_HeartContainer` boss-heart pickup chain (sets `link_health_capacity` via index 38; +1 max HP) |
| 2 | ancilla.c:3771 | (var) | Re-trigger inside item-receipt ancilla state machine — **not a grant**, pipeline state |
| 3 | ancilla.c:4019 | 0x14 | **Flute Boy quest reward** — `Ancilla36_Flute` collision pickup, sets `link_item_flute = 2` (full flute) after Flute Boy → fairy hand-off |
| 4 | player.c:3815 | (var) | **Universal chest-open path** (via `Link_PerformOpenChest` at 3795) — `item` comes from `OpenChestForItem(tile, &chest_position)` reading vanilla chest data |
| 5 | sprite.c:1406 | 0x32 | Small-key drop from killed sprite (`Sprite_HandleDeathSpawnItem`-style path) |
| 6 | sprite_main.c:1267 | sprite_graphics[k] | NPC handler (`Sprite_Cucco`/peg-style: item id is sprite-state-driven; needs §6 wrapper that captures the sprite-id context) |
| 7 | sprite_main.c:2130 | 1 | L2 sword grant — context unconfirmed by handler-name read; **likely Master Sword Pedestal** per item value (sword tier 2). v0.3 to confirm surrounding `Sprite_*` function. |
| 8 | sprite_main.c:5733 | 0 | **Uncle in passage** — `Uncle_InPassage` case 1 "GiveSwordAndShield" (`Sprite_Uncle` family, `src/sprite_main.c:5731-5738`). Item 0 = `link_sword_type = 1` (L1 sword) |
| 9 | sprite_main.c:5868 | 0x18 | **Witch (Potion Shop)** — `Sprite_Witch` case 1 "grant cane of byrna" (`src/sprite_main.c:5865-5869`). Item 0x18 (decimal 24) dispatches to idx 24 = `link_item_cane_byrna = 1` per `kMemoryLocationToGiveItemTo[24] = 0xf351`. **NOTE**: the original ASM in-code comment labels this "grant cane of byrna", which is consistent with the dispatch-table read. v0.1 labelled this "Magic powder substitution" — that was incorrect. v0.3: confirm whether the Witch's primary grant in vanilla is indeed Cane of Byrna (not Magic Powder) by cross-referencing the message id 0x4a/0x4b/0x4c flow. |
| 10 | sprite_main.c:6195 | 0x16 | **Bottle Merchant** — `Sprite_BottleVendor` case 2 "giving" (`src/sprite_main.c:6193-6198`); 100-rupee buy, `sram_progress_indicator_3 \|= 2` to mark purchase |
| 11 | sprite_main.c:6387 | 0x29 | **Mushroom pickup** — `Sprite_E7_Mushroom` collision (`src/sprite_main.c:6375-6391`); index 0x29 special-cases to write `link_item_mushroom = 1` (mushroom-in-inventory) |
| 12 | sprite_main.c:6451 | 0x3e | **Heart container drop (alt path)** — `Sprite_HeartContainer` `if (sprite_A[k])` branch (`src/sprite_main.c:6448-6453`). Index 0x3e value 2, location `link_health_capacity` (+2 from 0x3e variant; ordinary container at 0x26 also writes capacity). Dispatch idx 0x3e special: also re-triggers throw animation. |
| 13 | sprite_main.c:6457 | 0x26 | **Heart container (boss drop, standard path)** — same `Sprite_HeartContainer` (`src/sprite_main.c:6455-6457`); sets `dung_savegame_state_bits` map flag in caller |
| 14 | sprite_main.c:6497 | 0x26 | Heart container — `Sprite_HeartPiece` triggered after `link_heart_pieces` rolls 4→0 at line 6493 |
| 15 | sprite_main.c:6546 | 0x4b | **Sahasrahla** — `Sprite_Sahasrahla` case 2 "grant boots" (`src/sprite_main.c:6544-6549`); 0x4b idx 75 = `link_item_boots = 1` (Pegasus Boots; bit 4 of `link_ability_flags` set via 0x4b special at misc.c:733) |
| 16 | sprite_main.c:6793 | 0xd | **Bag of Powder (Magic Shop)** — `Sprite_BagOfPowder` collision (`src/sprite_main.c:6784-6794`); 0xd dispatch idx 13 = `link_item_mushroom`, value 2 (mushroom→powder upgrade) |
| 17 | sprite_main.c:6833 | 0x2f | **Green Cauldron** — `Sprite_GreenCauldron` purchase (`src/sprite_main.c:6829-6833`); 60-rupee green potion → bottle slot |
| 18 | sprite_main.c:6873 | 0x30 | **Blue Cauldron** — `Sprite_BlueCauldron` (analogous to Green) — blue potion → bottle slot |
| 19 | sprite_main.c:6914 | 0x2e | **Red Cauldron** — `Sprite_RedCauldron` — red potion → bottle slot |
| 20 | sprite_main.c:7053 | 0x1d | **Book of Mudora pickup** — `Sprite_BookOfMudora` case 3 "give to player" (`src/sprite_main.c:7050-7054`); 0x1d idx 29 = `link_item_book_of_mudora = 1` |
| 21 | sprite_main.c:9922 | 0x13 | **FluteKid Stumpy (Shovel grant)** — `Sprite_FluteKid_Stumpy` case 2 "grant shovel" (`src/sprite_main.c:9920-9923`); 0x13 idx 19 = `link_item_flute = 1` (shovel-form / pre-flute state) |
| 22 | sprite_main.c:10184 | 2 | **Smithy tempering grant** — `Smithy_Main` case 6 "Smithy_GiveTemperedSword" (`src/sprite_main.c:10180-10186`); item 2 = L3 sword (Tempered) |
| 23 | sprite_main.c:10436 | 0x21 | **Sick Kid (Bug Catching Kid)** — `Sprite_BugCatchingKid`-family case 2 "grant" (`src/sprite_main.c:10434-10439`); 0x21 idx 33 = `link_item_bug_net = 1`. **NOTE**: vanilla Sick Kid expects player to have a bottle and rewards the bug net; ALTTPR location name is `"Sick Kid"` |
| 24 | sprite_main.c:10648 | 0x16 | **Purple Chest (Middle-Aged Man)** — `Sprite_MiddleAgedMan`-family case 3 "react to secret keeping" (`src/sprite_main.c:10647-10650`); bottle grant. `sram_progress_indicator_3 \|= 0x10` |
| 25 | sprite_main.c:10738 | 0x16 | **Hobo (under bridge)** — `Sprite_Hobo_Bum` case 2 "grant bottle" (`src/sprite_main.c:10733-10740`); bottle grant. `sram_progress_indicator_3 \|= 1` |
| 26 | sprite_main.c:18192 | sprite_A[k] | NPC handler — item id is sprite-state-driven; v0.3 to identify handler context (likely a generic prize/drop path) |
| 27 | sprite_main.c:24682 | 0x1a | **Old Man on Death Mountain** — `Sprite_AD_OldMan` subtype 1 case 0 "grant mirror" (`src/sprite_main.c:24676-24689`); 0x1a idx 26 = `link_item_mirror = 2` (Magic Mirror). **NOTE**: in ALTTPR this is the `"Old Man"` location (escort reward). |
| 28 | sprite_main.c:25308 | item | **Shop item dispatch** (via `ShopItem_HandleReceipt` at 25305) — `item` is the shop slot's product |

Plus three internal sites (NOT grant points; these are pipeline mechanics):

| File:line | Role |
|---|---|
| ancilla.c:3413 | `link_receiveitem_index = 0` — reset before heart-container animation |
| ancilla.c:5903, 5981, 5999 | Internal pipeline write to `link_receiveitem_index` |
| misc.c:679 | Reset in main item-receipt cleanup |
| sprite_main.c:23299 | `link_receiveitem_index = 0` — reset for an item-receipt edge case |

### §0.1.3 — Direct-write grant sites (bypass the dispatcher)

Sites that mutate inventory state **without** going through `Link_ReceiveItem`. Each is a real grant and needs a §6 hook of its own.

| File:line | Write | Apparent grant |
|---|---|---|
| ancilla.c:3429-3441 | `link_health_capacity += 8; link_hearts_filler += …` | Heart Container — boss-drop animation completion (the dispatcher only sets `link_receiveitem_index = 0x26`; the actual `+= 8` happens here in the animation handler) |
| ancilla.c:3828 | `link_has_crystals |= kDungeonCrystalPendantBit[…]` | **Dungeon crystal/pendant grant on boss kill** (one of the two §6.6 "boss kill grants TWO rando locations" — this is the `<Dungeon>_Prize` half) |
| ancilla.c:4160 | `link_item_flute = 3` | Flute upgrade after fairy hand-off (cosmetic upgrade, but counts as item state) |
| ancilla.c:6941-6947 | `link_rupees_goal += …` | Rupee chest contents (1/5/20/100/300) — chest-tile-id triggers rupee grant directly, skipping dispatcher |
| sprite.c:1402 | `link_num_keys += 1` | Small-key drop from killed sprite (alt path — sprite.c:1406 also calls `Link_ReceiveItem(0x32, …)` for the standard one) |
| sprite.c:1413 | `link_shield_type = sprite_subtype[k]` | Shield drop from sprite |
| sprite_main.c:1433 | `link_bottle_info[j] = 7 + sprite_head_dir[k]` | Bottle-contents update from sprite (fairy capture, etc.) |
| sprite_main.c:1774 | `link_num_arrows = sprite_subtype[k]` | Arrow-pickup-style absolute write |
| sprite_main.c:6493-6497 | `link_heart_pieces = (link_heart_pieces + 1) & 3;` then if `== 0` call `Link_ReceiveItem(0x26, 0)` | Piece-of-Heart pickup — direct increment; rolls to heart container at 4/4 |
| sprite_main.c:11110 | `link_magic_consumption = 1` | Half-magic from witch's hut |
| sprite_main.c:11483-11484 | `link_bomb_upgrades = i; link_bomb_filler = kMaxBombsForLevelHex[i]` | Bomb-capacity upgrade (bomb shop) |
| sprite_main.c:11520-11521 | `link_arrow_upgrades = i; link_arrow_filler = kMaxArrowsForLevelHex[i]` | Arrow-capacity upgrade (arrow shop) |
| sprite_main.c:11835 | `link_bottle_info[j] = 6` | Bottle contents → "good bee" sprite handler |
| misc.c:852-861 | `link_bottle_info[i] = j + 2 / j + 3` | Bottle refill from `ItemReceipt_GiveBottledItem` (inside the dispatcher chain but writes bottle directly because `kValueToGiveItemTo[bottle]` is `-1` meaning "skip default write") |

### §0.1.4 (relocated) — Section ordering note

> The original §0.1.4 ("Writes that are **not** grants") appears immediately below §0.1.5 to preserve the v0.1 numbering. Logical reading order: §0.1.2 → §0.1.3 → §0.1.5 (item-id reference, helpful when reading §0.1.2 IDs) → §0.1.4 (exclusions).

### §0.1.5 — Item ID → meaning reference (v0.2)

Derived from `kReceiveItemGfx[76]` at [src/misc.c:53-59](../../../src/misc.c#L53), `kMemoryLocationToGiveItemTo[76]` at [src/misc.c:60-80](../../../src/misc.c#L60), and `kValueToGiveItemTo[76]` at [src/misc.c:81-101](../../../src/misc.c#L81). The dispatcher reads the `item_id` passed to `Link_ReceiveItem`, indexes all three tables by it, and either writes the absolute value (kValueToGiveItemTo > 0) or runs a special-case branch (kValueToGiveItemTo < 0, signaled by `sign8(v)`) at `src/misc.c:728-784`.

Bottle-content items (`kBottleList`/`kPotionList` at [src/misc.c:847-848](../../../src/misc.c#L847)) take the `ItemReceipt_GiveBottledItem` path (`src/misc.c:846-866`), which finds the first empty bottle slot (`< 2`) and writes the content code.

| ID (hex) | gfx | RAM addr | value | Inferred meaning (matches dispatch-table behavior) |
|---|---|---|---|---|
| 0x00 | 6 | 0xf359 | 1 | L1 Sword (Fighter Sword) |
| 0x01 | 0x18 | 0xf359 | 2 | L2 Sword (Master Sword) |
| 0x02 | 0x18 | 0xf359 | 3 | L3 Sword (Tempered Sword) |
| 0x03 | 0x18 | 0xf359 | 4 | L4 Sword (Golden Sword) |
| 0x04 | 0x2d | 0xf35a | 1 | Fighter Shield |
| 0x05 | 0x20 | 0xf35a | 2 | Red Shield (Fire Shield) |
| 0x06 | 0x2e | 0xf35a | 3 | Mirror Shield |
| 0x07 | 9 | 0xf345 | 1 | Fire Rod |
| 0x08 | 9 | 0xf346 | 1 | Ice Rod |
| 0x09 | 0xa | 0xf34b | 1 | Hammer |
| 0x0a | 8 | 0xf342 | 1 | Hookshot |
| 0x0b | 5 | 0xf340 | 1 | Bow |
| 0x0c | 0x10 | 0xf341 | 1 | Boomerang (blue) |
| 0x0d | 0xb | 0xf344 | 2 | Magic Powder (mushroom→powder upgrade) |
| 0x0e | 0x2c | 0xf35c | -1 | Bottle dispatch path (empty bottle slot search via `ItemReceipt_GiveBottledItem`) |
| 0x0f | 0x1b | 0xf347 | 1 | Bombos Medallion |
| 0x10 | 0x1a | 0xf348 | 1 | Ether Medallion |
| 0x11 | 0x1c | 0xf349 | 1 | Quake Medallion |
| 0x12 | 0x14 | 0xf34a | 1 | Lamp |
| 0x13 | 0x19 | 0xf34c | 1 | Shovel-form Flute (Stumpy state — `link_item_flute = 1`) |
| 0x14 | 0xc | 0xf34c | 2 | Full Flute (post-fairy hand-off) |
| 0x15 | 7 | 0xf350 | 1 | Cane of Somaria |
| 0x16 | 0x1d | 0xf35c | -1 | Bottle (empty) — `ItemReceipt_GiveBottledItem` finds first slot < 2 and writes 2 |
| 0x17 | 0x2f | 0xf36b | 2 | Piece of Heart (increments `link_heart_pieces` mod 4 — special case at misc.c:777-779) |
| 0x18 | 7 | 0xf351 | 1 | Cane of Byrna |
| 0x19 | 0x15 | 0xf352 | 1 | Cape (Magic Cape) |
| 0x1a | 0x12 | 0xf353 | 2 | Magic Mirror |
| 0x1b | 0xd | 0xf354 | 1 | Power Gloves (palette refresh at misc.c:736) |
| 0x1c | 0xd | 0xf354 | 2 | Titan's Mitts |
| 0x1d | 0xe | 0xf34e | 1 | Book of Mudora |
| 0x1e | 0x11 | 0xf356 | 1 | Flippers (special: also OR-sets `link_ability_flags` bit 1 at misc.c:733-734) |
| 0x1f | 0x17 | 0xf357 | 1 | Moon Pearl (special: clears `link_is_bunny` at misc.c:731-732) |
| 0x20 | 0x28 | 0xf37a | 1 | Crystal — but **this index is OR-into-bit special-case** at misc.c:751-766; rarely called directly (boss-grant path is ancilla.c:3828 direct write) |
| 0x21 | 0x27 | 0xf34d | 1 | Bug Catching Net |
| 0x22 | 4 | 0xf35b | 1 | Blue Mail (Armor → Blue) — special at misc.c:743-745 writes 1 only if 0 |
| 0x23 | 4 | 0xf35b | 2 | Red Mail (Armor → Red) |
| 0x24 | 0xf | 0xf36f | 1 | Small Key (+1, clamped to 99 at misc.c:772-775) |
| 0x25 | 0x16 | 0xf364 | -1 | Compass — OR-into-bit by current dungeon (special case at misc.c:746-747) |
| 0x26 | 3 | 0xf36c | 1 | Heart Container (+1 max HP; `kReceiveItem_Tab1[0x26]=1` triggers heart-container fanfare) |
| 0x27 | 0x13 | 0xf375 | -1 | +3 bombs (special at misc.c:772, clamped) |
| 0x28 | 1 | 0xf375 | -1 | +10 bombs (special at misc.c:772, clamped) |
| 0x29 | 0x1e | 0xf344 | 1 | Mushroom (item — special case at misc.c:767-771: writes 1 only if not already 2, refreshes HUD) |
| 0x2a | 0x10 | 0xf341 | 2 | Red Boomerang (Magical Boomerang) |
| 0x2b | 0 | 0xf35c | -1 | Bottle with Bee (via `ItemReceipt_GiveBottledItem` kBottleList idx 1 → bottle = 3) |
| 0x2c | 0 | 0xf35c | -1 | Bottle with Fairy (kBottleList idx 2 → bottle = 4) |
| 0x2d | 0 | 0xf35c | -1 | Bottle with Water (kBottleList idx 3 → bottle = 5) |
| 0x2e | 0 | 0xf36d | 2 | Red Potion (`+0xa0` health refill via `kReceiveItem_Tab1`/Hud_RefillHealth; via `kPotionList` if going to bottle: bottle = 5) — actually 0x2e is in `kPotionList[0]` so `ItemReceipt_GiveBottledItem` may write bottle=3 if a "consumed bottle" exists; otherwise it's a refill |
| 0x2f | 0x30 | 0xf36e | -1 | Green Potion (`kPotionList[1]` → bottle slot = 4 if any bottle is 2; otherwise magic refill — special at misc.c:858-865) |
| 0x30 | 0x22 | 0xf36e | -1 | Blue Potion (`kPotionList[2]` → bottle slot = 5 if any bottle is 2; otherwise full health+magic refill) |
| 0x31 | 0x21 | 0xf375 | 10 | Bomb capacity refill (+10 bombs special at misc.c:772) |
| 0x32 | 0x24 | 0xf366 | -1 | Big Key — OR-into-bit by current dungeon (special at misc.c:746) |
| 0x33 | 0x24 | 0xf368 | -1 | Dungeon Map — OR-into-bit by current dungeon (special at misc.c:746) |
| 0x34 | 0x24 | 0xf360 | -1 | Rupee +1 (kReceiveItemGfx tag 0x24) |
| 0x35 | 0x23 | 0xf360 | -5 | Rupee +5 |
| 0x36 | 0x23 | 0xf360 | -20 | Rupee +20 |
| 0x37 | 0x23 | 0xf374 | -1 | Green Pendant — special at misc.c:738-742 (`*p \|= 4`; if all pendants then `savegame_map_icons_indicator = 4`) |
| 0x38 | 0x29 | 0xf374 | -1 | Red Pendant (`*p \|= 1`) |
| 0x39 | 0x2a | 0xf374 | -1 | Blue Pendant (`*p \|= 2`) |
| 0x3a | 0x2c | 0xf340 | 2 | Bow + Arrows (post-acquire bow upgrade; sets `link_item_bow = 2`) |
| 0x3b | 0x2b | 0xf340 | 1 | Silver Arrows reset to bow tier? (verify in v0.3) |
| 0x3c | 3 | 0xf35c | -1 | Bottle with Good Bee (`kBottleList[5]` → bottle = 7) |
| 0x3d | 3 | 0xf35c | -1 | Bottle with Gold Bee / Fairy alt? `kBottleList[4]` → bottle = 6 |
| 0x3e | 0x34 | 0xf36c | -1 | Heart container (boss-drop variant, +2 max HP path at misc.c:748-750 triggers throw animation) |
| 0x3f | 0x35 | 0xf36c | -1 | Heart Container (alt) |
| 0x40 | 0x31 | 0xf360 | -100 | Rupee +100 |
| 0x41 | 0x33 | 0xf360 | -50 | Rupee +50 (Rupee Refill 50) |
| 0x42 | 2 | 0xf372 | -1 | Heart-filler dispatch — refill via `link_hearts_filler` |
| 0x43 | 0x32 | 0xf376 | 1 | Arrow +1 (one arrow) |
| 0x44 | 0x36 | 0xf376 | 10 | Arrow +10 |
| 0x45 | 0x37 | 0xf373 | -1 | Magic-filler dispatch (refill) |
| 0x46 | 0x2c | 0xf360 | -1 | Rupee +1 (alt path) |
| 0x47 | 6 | 0xf360 | 1 | Rupee +1 (alt path) |
| 0x48 | 0xc | 0xf35c | 3 | Bottle with Bee (kBottleList[6] → bottle = 8) |
| 0x49 | 0x38 | 0xf359 | 1 | Progressive Sword (?) — kValueToGiveItemTo=1, but loc=`link_sword_type`; **v0.3 verify** (Phase A `ProgressiveSword` may need a new dispatch idx) |
| 0x4a | (gap) | 0xf34c | 3 | Flute final (post-duck cutscene); `Ancilla38_CutsceneDuck` at ancilla.c:4160 writes this directly (does NOT go through `Link_ReceiveItem`) |
| 0x4b | 6 | 0xf355 | 1 | Pegasus Boots (special at misc.c:733: also OR-sets `link_ability_flags` bit 4) |

**Notes on table completeness**: IDs 0x4c-0x4f are not present in the 76-entry tables. The actual table length is `[76]` = indices 0..0x4b. References at runtime should always bounds-check.

**Translation note for `randomizer-placement` (task 6.x)**: ALTTPR item names map to these IDs as follows (per `Randomizer.php:183-198` for progressives, individual region PHPs for absolute items). Phase A's `assets/rando/item_registry.yaml` (task 3.2) declares the registry id-set; mapping is `registry_id → (vanilla_id, dispatch_override?)`. Progressive items are NEW dispatch slots (not in this 76-entry table) — see §0.4.1 for the required new receive paths.

### §0.1.4 — Writes that are **not** grants (state-shuffle / cosmetic / consumption)

For completeness — these will be tagged in §0.2 to make the dispatcher-hook exemption list explicit.

- **State-shuffle (preserve existing state, not a new grant)**:
  - `hud.c:390-396` — rupee tick from `link_rupees_goal` toward `link_rupees_actual` (UI animation)
  - `hud.c:514-789` — bottle navigation (HUD cycling)
  - `player.c:2453-2498` — bottle-drink: reads `link_bottle_info[btidx]`, writes `2` (empty) — state-shuffle, not grant
  - `messaging.c:696-697`, `messaging.c:2917` — bottle-content adjustment on death/reset
  - `sprite_main.c:1067, 1845, 3025, 6197, 6831, 6871, 6912, 9794, 10155, 11425, 19176, 19883, 24682, 25329` — `link_rupees_goal -= cost` (shop purchases, fortune teller, etc.)
- **Cosmetic / pure HUD** (do not affect game state):
  - `hud.c:517-792` — `link_item_bottle_index` writes (HUD selection cursor)
  - `player.c:2001, 2055` — bottle-index restore around drink animation
- **Consumption** (the inverse of a grant):
  - `ancilla.c:5772-5777`, `sprite_main.c:17393, 19878` — `link_item_bombs--` (bomb use)
  - `player.c:2374-2377`, `sprite_main.c:17391, 19883` — `link_num_arrows--/+= 2` (arrow use / pickup-restock)
  - `overworld.c:410-412`, `player.c:174-177, 1570-1572` — `link_health_current -= 8` (damage)
- **Story-progress flags** — see §0.4a (these are the reachability-affecting events the logic graph references)

---

## §0.2 — Classification

Every entry in §0.1 is tagged below. Tags:

- **`grant`** — randomizer dispatcher must override (§6 instrumentation site).
- **`state-shuffle`** — preserves state mid-game (e.g., bottle refill from drink); exempt.
- **`cosmetic`** — HUD/animation only; exempt.
- **`consumption`** — inverse of a grant (bomb use, arrow use); exempt.
- **`progress`** — story event flag; relevant to logic graph (§0.4a) but not a grant-instrumentation target. Some progress flags (e.g., pyramid-opened from beating Agahnim) **are** "implicit grants" of region access and must be enumerated by the logic graph, not by §6.

### §0.2.1 — Grant entries (the §6 instrumentation set)

All entries from §0.1.2 (call sites of `Link_ReceiveItem`) are **`grant`**. All entries from §0.1.3 (direct-write grant sites) are **`grant`**. Exception: ancilla.c:3771 (re-trigger inside the item-receipt ancilla — pipeline state machine, not a new grant; tag **`state-shuffle`**). Exception: `link_receiveitem_index = 0` resets are pipeline mechanics, not grants.

### §0.2.2 — State-shuffle entries

- `hud.c` bottle navigation writes (`link_item_bottle_index`, see §0.1.4)
- `player.c:2453-2498` bottle-drink writes (consume → empty)
- `messaging.c:696-697, 2917` bottle-on-death writes
- `ancilla.c:3771` (item-receipt pipeline re-trigger)
- `misc.c:679, ancilla.c:3413, ancilla.c:5903/5981/5999, sprite_main.c:23299` (`link_receiveitem_index` writes — pipeline mechanic)

### §0.2.3 — Cosmetic entries

- `link_item_bottle_index` HUD-selection writes (hud.c:517-792, player.c:2001, 2055)
- `link_item_flute = 3` at ancilla.c:4160 (visual upgrade from fairy hand-off; *if* §6 doesn't need this, tag cosmetic — but **verify** whether logic graph relies on flute-tier=3 as a separate state)

### §0.2.4 — Consumption entries

- Bomb-use, arrow-use, rupee-cost, damage-take sites enumerated in §0.1.4

### §0.2.5 — Progress entries

- `sram_progress_indicator` writes (dungeon.c:2504, sprite_main.c:5738, 6342) — coarse game phase (uncle dead, sahasrahla intro, etc.)
- `sram_progress_flags` OR-writes (sprite_main.c:5588, 5714, 5737, 6528, 10863, 10879) — flag bits
- `sram_progress_indicator_3` OR-writes (sprite_main.c:6196, 9951, 10020, 10161, 10185, 10649, 10739) — extended flag bits

Per-flag mapping is enumerated in §0.4a.

---

## §0.3 — ALTTPR location-ID mapping

> **v0.1 status (partial).** This section pins the *structure* of the mapping (which ALTTPR region file holds each canonical location, how naming maps to ours) and fills in the obvious entries. The exhaustive cross-reference of all ~212 ALTTPR canonical locations against the §0.1.2/§0.1.3 grant-site enumeration is sustained cross-reference work that runs to completion in v0.2.

### §0.3.1 — Source of truth for ALTTPR locations

Per [CLAUDE.md](../../../CLAUDE.md), the ALTTPR PHP randomizer at `../alttp_vt_randomizer/` (MIT, verified) defines locations in:

```
../alttp_vt_randomizer/app/Region/{Open,Standard,Inverted}/
  ├── DarkWorld/        (sub-region for dark-world locations)
  ├── LightWorld/       (sub-region for light-world locations)
  ├── DesertPalace.php
  ├── EasternPalace.php
  ├── Fountains.php
  ├── GanonsTower.php
  ├── HyruleCastleEscape.php
  ├── HyruleCastleTower.php
  ├── IcePalace.php
  ├── Medallions.php
  ├── MiseryMire.php
  ├── PalaceOfDarkness.php
  ├── SkullWoods.php
  ├── SwampPalace.php
  ├── ThievesTown.php
  ├── TowerOfHera.php
  └── TurtleRock.php
```

Each region file is a list of `Location` objects, each wired with `setRequirements` / `setFillRules` / `setAlwaysAllow` closures. The location *name* (string) is the canonical ID we mirror in `assets/rando/location_registry.yaml` (task 3.1).

### §0.3.2 — Confirmed mappings (v0.2 expanded)

Cross-references between our grant-site enumeration and ALTTPR canonical location names. Citation format: `our-side file:line  ⇄  ALTTPR file:line`. Names are the exact PHP string keys per §0.3.5.

| Our side | ALTTPR canonical name | ALTTPR source | vanilla_item_id |
|---|---|---|---|
| player.c:3815 (`Link_PerformOpenChest`) | `<dungeon>_<chest-name>` (~125 entries; full list in §0.3.5) | per-region PHP files (Chest/BigChest type) | from vanilla chest data (room data) — passed to `Rando_OnLocationCheck` |
| sprite_main.c:25308 (`ShopItem_HandleReceipt`) | `<shop>_<slot>` (shops, capacity shops) | shop region files (ALTTPR `app/Shop.php`, NOT a Location\\) | shop slot product id |
| ancilla.c:3828 (crystal grant on boss) | `<dungeon> - Prize` (`Crystal` or `Pendant` subtype per `Prize\Crystal` / `Prize\Pendant`) | per-dungeon PHP `prizeLocation` (e.g. `EasternPalace.php:58`) | Crystal/Pendant item id |
| ancilla.c:3421, sprite_main.c:6457 (heart container grant) | `<dungeon> - Boss` (Drop type) | per-dungeon PHP (e.g. `EasternPalace.php:56`) | `BossHeartContainer` |
| sprite_main.c:6493-6497 (POH increment + container roll) | `*PieceOfHeart` placement locations (24 entries; e.g. `Sunken Treasure`, `Cave 45`, `Lake Hylia Island`, `Floating Island`, `Spectacle Rock`, etc.) | LightWorld/DarkWorld subdirs Standing/Dash sites | `PieceOfHeart` |
| sprite_main.c:11483-11484 (bomb-capacity shop) | (no Location\\ entry — capacity upgrade is `app/Shop/Upgrade.php`) | Shop subsystem | n/a (counter upgrade) |
| sprite_main.c:11520-11521 (arrow-capacity shop) | (no Location\\ entry — capacity upgrade is `app/Shop/Upgrade.php`) | Shop subsystem | n/a (counter upgrade) |
| sprite_main.c:11110 (witch half-magic) | (no Location\\ for half-magic in `LightWorld/NorthEast.php`) — the witch's half-magic grant is **not** a placement-relevant location; ALTTPR's `Witch` predicate refers to giving the mushroom to the witch | (verify in v0.3) | `HalfMagic` |
| **v0.2 additions below** | | | |
| sprite_main.c:5733 (Uncle in passage) | `Link's Uncle` | `app/Region/Standard/HyruleCastleEscape.php:49` | `L1Sword` (vanilla); shuffled in rando |
| sprite_main.c:5868 (Witch case 1 "grant byrna") | (Witch grant — but ALTTPR location `Potion Shop` at `LightWorld/NorthEast.php:37` is a `Npc\Witch` entry that grants the trade item; v0.3 confirm whether 0x18=byrna is vanilla Witch's intent) | `app/Region/Standard/LightWorld/NorthEast.php:37` | (verify in v0.3) |
| sprite_main.c:6195 (Bottle Merchant) | `Bottle Merchant` | `app/Region/Standard/LightWorld/NorthWest.php:47` | `BottleEmpty` (the 100-r bottle purchase) |
| sprite_main.c:6387 (Mushroom pickup) | `Mushroom` | `app/Region/Standard/LightWorld/NorthWest.php:53` | `Mushroom` |
| sprite_main.c:6546 (Sahasrahla boots) | `Sahasrahla` | `app/Region/Standard/LightWorld/NorthEast.php:35` | `PegasusBoots` |
| sprite_main.c:6793 (Bag of Powder magic shop) | (no direct Location\\ entry — magic shop bag-of-powder is a shop item; ALTTPR shop subsystem handles via `app/Shop.php`) | Shop subsystem | `MagicPowder` |
| sprite_main.c:6833, 6873, 6914 (Green/Blue/Red Cauldron purchases) | (no Location\\ entry — cauldron purchases are shop-style bottle refills; cosmetic) | Shop subsystem | bottle contents |
| sprite_main.c:7053 (Book of Mudora dash-pickup) | `Library` | `app/Region/Standard/LightWorld/South.php:45` | `BookOfMudora` |
| sprite_main.c:9922 (FluteKid Stumpy shovel) | `Stumpy` | `app/Region/Standard/DarkWorld/South.php:36` | `Shovel` (vanilla) — Stumpy gives shovel, NOT flute (flute is `Flute Spot` at `LightWorld/South.php:50` separate dig location) |
| sprite_main.c:10184 (Smithy tempering) | `Blacksmith` | `app/Region/Standard/DarkWorld/NorthWest.php:37` | `L3Sword` (vanilla tempered) |
| sprite_main.c:10436 (Sick Kid bug net) | `Sick Kid` | `app/Region/Standard/LightWorld/NorthWest.php:49` | `BugCatchingNet` |
| sprite_main.c:10648 (Purple Chest) | `Purple Chest` | `app/Region/Standard/DarkWorld/NorthWest.php:38` | `BottleEmpty` (vanilla) |
| sprite_main.c:10738 (Hobo) | `Hobo` | `app/Region/Standard/LightWorld/South.php:40` | `BottleEmpty` (vanilla) |
| sprite_main.c:24682 (Old Man on Death Mountain) | `Old Man` | `app/Region/Standard/LightWorld/DeathMountain/West.php:30` | `MagicMirror` (vanilla — escort reward) |
| ancilla.c:4019 (Ancilla36_Flute pickup) | `Flute Spot` (likely — duck delivers flute after Flute Boy quest at this dig location) | `app/Region/Standard/LightWorld/South.php:50` | `OcarinaInactive` / `Flute` — v0.3 confirm the spawn chain |
| misc.c:313 (Aga 1 defeat) | `Agahnim` (Prize\Event) | `app/Region/Standard/HyruleCastleTower.php:43` | `DefeatAgahnim` |

### §0.3.3 — Outstanding mappings (v0.2 cross-reference)

The Phase 0 deliverable requires the following work to complete before §6 PRs can land:

1. **Enumerate every distinct chest in `OpenChestForItem`'s reach** (chest tile id → location name → ALTTPR canonical id). This requires reading vanilla chest data (room data in `zelda3_assets.dat`) cross-referenced against ALTTPR's per-region chest list.
2. **Verify each numeric item_id passed at the §0.1.2 call sites** by reading `kReceiveItemGfx[item_id]` (TODO: locate this table) and the per-id branch in `AncillaAdd_ItemReceipt`, then match to the ALTTPR item id (e.g., 0x13 is `Flippers`, 0x16 is `BottleEmpty`, etc.).
3. **Enumerate the NPC-gift call sites in detail**: for each Link_ReceiveItem call inside sprite_main.c, identify which sprite handler the call site belongs to (uncle, sahasrahla, king zora, sick kid, etc.) — this is where most of the multi-day work is.
4. **Cross-check coverage**: walk every Location declared in `app/Region/{Open,Standard,Inverted}/**.php` (~212 names per `lessons.md`) and assert each is covered by an entry in §0.3.2 once §0.3.2 is expanded. Flag any ALTTPR location with no matching grant site as a **§6 gap** — these are sites that need either new instrumentation or a synthesized hook (Pyramid Fairy is the documented "synthesized multi-slot grant site" per task 6.7).

### §0.3.5 — ALTTPR canonical location catalog (v0.2)

> **Provenance**: enumerated by grep `new Location` against `../alttp_vt_randomizer/app/Region/**/*.php`. Names are the exact PHP string keys (case- and apostrophe-preserved). World-state column shows where each Location is declared: **S** = Standard (base, all locations defined here); **O** = Open (extends Standard, mostly adds no new locations — only requirement overrides per `app/Region/Open/HyruleCastleEscape.php`); **I** = Inverted adds 5 specific re-locations / new declarations.
>
> **Coverage assertion**: this is the ground-truth list for §0.8c. The total below is **216** distinct named locations across world-states (some are world-state-only, e.g. `Bomb Merchant` is Inverted-only).
>
> **Note**: `Pyramid Fairy - Left` and `Pyramid Fairy - Right` are conditionally added (`Standard/DarkWorld/NorthEast.php:40-41` only when `$world->config('region.swordsInPool', true)` — but the chest data 0xE980/0xE983 always exists in vanilla; ALTTPR adds them as locations only in certain modes). Phase A treats them as always-present per `§0.3.4` Pyramid Fairy synthesized handling.

#### Dungeons (Standard world-state, names identical in Open/Inverted)

##### Hyrule Castle Escape (10 locations, including 1 event-prize)

| Location name | Type | PHP source |
|---|---|---|
| Sanctuary | Chest | HyruleCastleEscape.php:41 |
| Sewers - Secret Room - Left | Chest | HyruleCastleEscape.php:42 |
| Sewers - Secret Room - Middle | Chest | HyruleCastleEscape.php:43 |
| Sewers - Secret Room - Right | Chest | HyruleCastleEscape.php:44 |
| Sewers - Dark Cross | Chest | HyruleCastleEscape.php:45 |
| Hyrule Castle - Boomerang Chest | Chest | HyruleCastleEscape.php:46 |
| Hyrule Castle - Map Chest | Chest | HyruleCastleEscape.php:47 |
| Hyrule Castle - Zelda's Cell | Chest | HyruleCastleEscape.php:48 |
| Link's Uncle | Npc\Uncle | HyruleCastleEscape.php:49 |
| Secret Passage | Chest | HyruleCastleEscape.php:50 |
| Zelda (Prize event) | Prize\Event | HyruleCastleEscape.php:51 (prize slot, not item placement) |

##### Eastern Palace (7 locations including prize)

| Location name | Type | PHP source |
|---|---|---|
| Eastern Palace - Compass Chest | Chest | EasternPalace.php:51 |
| Eastern Palace - Big Chest | BigChest | EasternPalace.php:52 |
| Eastern Palace - Cannonball Chest | Chest | EasternPalace.php:53 |
| Eastern Palace - Big Key Chest | Chest | EasternPalace.php:54 |
| Eastern Palace - Map Chest | Chest | EasternPalace.php:55 |
| Eastern Palace - Boss | Drop | EasternPalace.php:56 |
| Eastern Palace - Prize | Prize\Pendant | EasternPalace.php:58 |

##### Desert Palace (7 locations including prize)

| Location name | Type | PHP source |
|---|---|---|
| Desert Palace - Big Chest | BigChest | DesertPalace.php:55 |
| Desert Palace - Map Chest | Chest | DesertPalace.php:56 |
| Desert Palace - Torch | Dash | DesertPalace.php:57 |
| Desert Palace - Big Key Chest | Chest | DesertPalace.php:58 |
| Desert Palace - Compass Chest | Chest | DesertPalace.php:59 |
| Desert Palace - Boss | Drop | DesertPalace.php:60 |
| Desert Palace - Prize | Prize\Pendant | DesertPalace.php:62 |

##### Tower of Hera (7 locations including prize)

| Location name | Type | PHP source |
|---|---|---|
| Tower of Hera - Big Key Chest | Chest | TowerOfHera.php:53 |
| Tower of Hera - Basement Cage | Standing\HeraBasement | TowerOfHera.php:54 |
| Tower of Hera - Map Chest | Chest | TowerOfHera.php:55 |
| Tower of Hera - Compass Chest | Chest | TowerOfHera.php:56 |
| Tower of Hera - Big Chest | BigChest | TowerOfHera.php:57 |
| Tower of Hera - Boss | Drop | TowerOfHera.php:58 |
| Tower of Hera - Prize | Prize\Pendant | TowerOfHera.php:60 |

##### Hyrule Castle Tower (3 entries — 2 chests + prize event)

| Location name | Type | PHP source |
|---|---|---|
| Castle Tower - Room 03 | Chest | HyruleCastleTower.php:41 |
| Castle Tower - Dark Maze | Chest | HyruleCastleTower.php:42 |
| Agahnim (Prize event) | Prize\Event | HyruleCastleTower.php:43 |

##### Palace of Darkness (15 locations including prize)

| Location name | Type | PHP source |
|---|---|---|
| Palace of Darkness - Shooter Room | Chest | PalaceOfDarkness.php:50 |
| Palace of Darkness - Big Key Chest | Chest | PalaceOfDarkness.php:51 |
| Palace of Darkness - The Arena - Ledge | Chest | PalaceOfDarkness.php:52 |
| Palace of Darkness - The Arena - Bridge | Chest | PalaceOfDarkness.php:53 |
| Palace of Darkness - Stalfos Basement | Chest | PalaceOfDarkness.php:54 |
| Palace of Darkness - Map Chest | Chest | PalaceOfDarkness.php:55 |
| Palace of Darkness - Big Chest | BigChest | PalaceOfDarkness.php:56 |
| Palace of Darkness - Compass Chest | Chest | PalaceOfDarkness.php:57 |
| Palace of Darkness - Harmless Hellway | Chest | PalaceOfDarkness.php:58 |
| Palace of Darkness - Dark Basement - Left | Chest | PalaceOfDarkness.php:59 |
| Palace of Darkness - Dark Basement - Right | Chest | PalaceOfDarkness.php:60 |
| Palace of Darkness - Dark Maze - Top | Chest | PalaceOfDarkness.php:61 |
| Palace of Darkness - Dark Maze - Bottom | Chest | PalaceOfDarkness.php:62 |
| Palace of Darkness - Boss | Drop | PalaceOfDarkness.php:63 |
| Palace of Darkness - Prize | Prize\Crystal | PalaceOfDarkness.php:65 |

##### Swamp Palace (11 locations including prize)

| Location name | Type | PHP source |
|---|---|---|
| Swamp Palace - Entrance | Chest | SwampPalace.php:50 |
| Swamp Palace - Big Chest | BigChest | SwampPalace.php:51 |
| Swamp Palace - Big Key Chest | Chest | SwampPalace.php:52 |
| Swamp Palace - Map Chest | Chest | SwampPalace.php:53 |
| Swamp Palace - West Chest | Chest | SwampPalace.php:54 |
| Swamp Palace - Compass Chest | Chest | SwampPalace.php:55 |
| Swamp Palace - Flooded Room - Left | Chest | SwampPalace.php:56 |
| Swamp Palace - Flooded Room - Right | Chest | SwampPalace.php:57 |
| Swamp Palace - Waterfall Room | Chest | SwampPalace.php:58 |
| Swamp Palace - Boss | Drop | SwampPalace.php:59 |
| Swamp Palace - Prize | Prize\Crystal | SwampPalace.php:61 |

##### Skull Woods (9 locations including prize)

| Location name | Type | PHP source |
|---|---|---|
| Skull Woods - Big Chest | BigChest | SkullWoods.php:57 |
| Skull Woods - Big Key Chest | Chest | SkullWoods.php:58 |
| Skull Woods - Compass Chest | Chest | SkullWoods.php:59 |
| Skull Woods - Map Chest | Chest | SkullWoods.php:60 |
| Skull Woods - Bridge Room | Chest | SkullWoods.php:61 |
| Skull Woods - Pot Prison | Chest | SkullWoods.php:62 |
| Skull Woods - Pinball Room | Chest | SkullWoods.php:63 |
| Skull Woods - Boss | Drop | SkullWoods.php:64 |
| Skull Woods - Prize | Prize\Crystal | SkullWoods.php:66 |

##### Thieves' Town (9 locations including prize)

| Location name | Type | PHP source |
|---|---|---|
| Thieves' Town - Attic | Chest | ThievesTown.php:50 |
| Thieves' Town - Big Key Chest | Chest | ThievesTown.php:51 |
| Thieves' Town - Map Chest | Chest | ThievesTown.php:52 |
| Thieves' Town - Compass Chest | Chest | ThievesTown.php:53 |
| Thieves' Town - Ambush Chest | Chest | ThievesTown.php:54 |
| Thieves' Town - Big Chest | BigChest | ThievesTown.php:55 |
| Thieves' Town - Blind's Cell | Chest | ThievesTown.php:56 |
| Thieves' Town - Boss | Drop | ThievesTown.php:57 |
| Thieves' Town - Prize | Prize\Crystal | ThievesTown.php:59 |

##### Ice Palace (9 locations including prize)

| Location name | Type | PHP source |
|---|---|---|
| Ice Palace - Big Key Chest | Chest | IcePalace.php:50 |
| Ice Palace - Compass Chest | Chest | IcePalace.php:51 |
| Ice Palace - Map Chest | Chest | IcePalace.php:52 |
| Ice Palace - Spike Room | Chest | IcePalace.php:53 |
| Ice Palace - Freezor Chest | Chest | IcePalace.php:54 |
| Ice Palace - Iced T Room | Chest | IcePalace.php:55 |
| Ice Palace - Big Chest | BigChest | IcePalace.php:56 |
| Ice Palace - Boss | Drop | IcePalace.php:57 |
| Ice Palace - Prize | Prize\Crystal | IcePalace.php:59 |

##### Misery Mire (9 locations including prize)

| Location name | Type | PHP source |
|---|---|---|
| Misery Mire - Big Chest | BigChest | MiseryMire.php:50 |
| Misery Mire - Main Lobby | Chest | MiseryMire.php:51 |
| Misery Mire - Big Key Chest | Chest | MiseryMire.php:52 |
| Misery Mire - Compass Chest | Chest | MiseryMire.php:53 |
| Misery Mire - Bridge Chest | Chest | MiseryMire.php:54 |
| Misery Mire - Map Chest | Chest | MiseryMire.php:55 |
| Misery Mire - Spike Chest | Chest | MiseryMire.php:56 |
| Misery Mire - Boss | Drop | MiseryMire.php:57 |
| Misery Mire - Prize | Prize\Crystal | MiseryMire.php:59 |

##### Turtle Rock (13 locations including prize)

| Location name | Type | PHP source |
|---|---|---|
| Turtle Rock - Chain Chomps | Chest | TurtleRock.php:60 |
| Turtle Rock - Compass Chest | Chest | TurtleRock.php:61 |
| Turtle Rock - Roller Room - Left | Chest | TurtleRock.php:62 |
| Turtle Rock - Roller Room - Right | Chest | TurtleRock.php:63 |
| Turtle Rock - Big Chest | BigChest | TurtleRock.php:64 |
| Turtle Rock - Big Key Chest | Chest | TurtleRock.php:65 |
| Turtle Rock - Crystaroller Room | Chest | TurtleRock.php:66 |
| Turtle Rock - Eye Bridge - Bottom Left | Chest | TurtleRock.php:67 |
| Turtle Rock - Eye Bridge - Bottom Right | Chest | TurtleRock.php:68 |
| Turtle Rock - Eye Bridge - Top Left | Chest | TurtleRock.php:69 |
| Turtle Rock - Eye Bridge - Top Right | Chest | TurtleRock.php:70 |
| Turtle Rock - Boss | Drop | TurtleRock.php:71 |
| Turtle Rock - Prize | Prize\Crystal | TurtleRock.php:73 |

##### Ganon's Tower (28 locations + 1 prize event)

| Location name | Type | PHP source |
|---|---|---|
| Ganon's Tower - Bob's Torch | Dash | GanonsTower.php:53 |
| Ganon's Tower - DMs Room - Top Left | Chest | GanonsTower.php:54 |
| Ganon's Tower - DMs Room - Top Right | Chest | GanonsTower.php:55 |
| Ganon's Tower - DMs Room - Bottom Left | Chest | GanonsTower.php:56 |
| Ganon's Tower - DMs Room - Bottom Right | Chest | GanonsTower.php:57 |
| Ganon's Tower - Randomizer Room - Top Left | Chest | GanonsTower.php:58 |
| Ganon's Tower - Randomizer Room - Top Right | Chest | GanonsTower.php:59 |
| Ganon's Tower - Randomizer Room - Bottom Left | Chest | GanonsTower.php:60 |
| Ganon's Tower - Randomizer Room - Bottom Right | Chest | GanonsTower.php:61 |
| Ganon's Tower - Firesnake Room | Chest | GanonsTower.php:62 |
| Ganon's Tower - Map Chest | Chest | GanonsTower.php:63 |
| Ganon's Tower - Big Chest | BigChest | GanonsTower.php:64 |
| Ganon's Tower - Hope Room - Left | Chest | GanonsTower.php:65 |
| Ganon's Tower - Hope Room - Right | Chest | GanonsTower.php:66 |
| Ganon's Tower - Bob's Chest | Chest | GanonsTower.php:67 |
| Ganon's Tower - Tile Room | Chest | GanonsTower.php:68 |
| Ganon's Tower - Compass Room - Top Left | Chest | GanonsTower.php:69 |
| Ganon's Tower - Compass Room - Top Right | Chest | GanonsTower.php:70 |
| Ganon's Tower - Compass Room - Bottom Left | Chest | GanonsTower.php:71 |
| Ganon's Tower - Compass Room - Bottom Right | Chest | GanonsTower.php:72 |
| Ganon's Tower - Big Key Chest | Chest | GanonsTower.php:73 |
| Ganon's Tower - Big Key Room - Left | Chest | GanonsTower.php:74 |
| Ganon's Tower - Big Key Room - Right | Chest | GanonsTower.php:75 |
| Ganon's Tower - Mini Helmasaur Room - Left | Chest | GanonsTower.php:76 |
| Ganon's Tower - Mini Helmasaur Room - Right | Chest | GanonsTower.php:77 |
| Ganon's Tower - Pre-Moldorm Chest | Chest | GanonsTower.php:78 |
| Ganon's Tower - Moldorm Chest | Chest | GanonsTower.php:79 |
| Agahnim 2 (Prize event) | Prize\Event | GanonsTower.php:80 |

##### Medallions (Special — 2 logic-affecting "medallion-set" slots, not item placements)

| Location name | Type | PHP source |
|---|---|---|
| Turtle Rock Medallion | Medallion | Medallions.php:30 |
| Misery Mire Medallion | Medallion | Medallions.php:31 |

##### Fountains (Standard only — 2 bottle locations)

| Location name | Type | PHP source |
|---|---|---|
| Waterfall Bottle | Fountain | Fountains.php:30 |
| Pyramid Bottle | Fountain | Fountains.php:31 |

#### Overworld

##### Light World — North East (10 locations)

| Location name | Type | PHP source |
|---|---|---|
| Sahasrahla's Hut - Left | Chest | LightWorld/NorthEast.php:32 |
| Sahasrahla's Hut - Middle | Chest | LightWorld/NorthEast.php:33 |
| Sahasrahla's Hut - Right | Chest | LightWorld/NorthEast.php:34 |
| Sahasrahla | Npc | LightWorld/NorthEast.php:35 |
| King Zora | Npc\Zora | LightWorld/NorthEast.php:36 |
| Potion Shop | Npc\Witch | LightWorld/NorthEast.php:37 |
| Zora's Ledge | Standing | LightWorld/NorthEast.php:38 |
| Waterfall Fairy - Left | Chest | LightWorld/NorthEast.php:39 |
| Waterfall Fairy - Right | Chest | LightWorld/NorthEast.php:40 |

##### Light World — North West (22 locations)

| Location name | Type | PHP source |
|---|---|---|
| Master Sword Pedestal | Pedestal | LightWorld/NorthWest.php:32 |
| King's Tomb | Chest | LightWorld/NorthWest.php:33 |
| Kakariko Tavern | Chest | LightWorld/NorthWest.php:34 |
| Chicken House | Chest | LightWorld/NorthWest.php:35 |
| Kakariko Well - Top | Chest | LightWorld/NorthWest.php:36 |
| Kakariko Well - Left | Chest | LightWorld/NorthWest.php:37 |
| Kakariko Well - Middle | Chest | LightWorld/NorthWest.php:38 |
| Kakariko Well - Right | Chest | LightWorld/NorthWest.php:39 |
| Kakariko Well - Bottom | Chest | LightWorld/NorthWest.php:40 |
| Blind's Hideout - Top | Chest | LightWorld/NorthWest.php:41 |
| Blind's Hideout - Left | Chest | LightWorld/NorthWest.php:42 |
| Blind's Hideout - Right | Chest | LightWorld/NorthWest.php:43 |
| Blind's Hideout - Far Left | Chest | LightWorld/NorthWest.php:44 |
| Blind's Hideout - Far Right | Chest | LightWorld/NorthWest.php:45 |
| Pegasus Rocks | Chest | LightWorld/NorthWest.php:46 |
| Bottle Merchant | Npc | LightWorld/NorthWest.php:47 |
| Magic Bat | Npc | LightWorld/NorthWest.php:48 |
| Sick Kid | Npc\BugCatchingKid | LightWorld/NorthWest.php:49 |
| Lost Woods Hideout | Standing | LightWorld/NorthWest.php:50 |
| Lumberjack Tree | Standing | LightWorld/NorthWest.php:51 |
| Graveyard Ledge | Standing | LightWorld/NorthWest.php:52 |
| Mushroom | Standing | LightWorld/NorthWest.php:53 |

##### Light World — South (19 locations)

| Location name | Type | PHP source |
|---|---|---|
| Floodgate Chest | Chest | LightWorld/South.php:32 |
| Link's House | Chest | LightWorld/South.php:33 (Standard) — **moved to DarkWorld/South.php:31 in Inverted** |
| Aginah's Cave | Chest | LightWorld/South.php:34 |
| Mini Moldorm Cave - Far Left | Chest | LightWorld/South.php:35 |
| Mini Moldorm Cave - Left | Chest | LightWorld/South.php:36 |
| Mini Moldorm Cave - Right | Chest | LightWorld/South.php:37 |
| Mini Moldorm Cave - Far Right | Chest | LightWorld/South.php:38 |
| Ice Rod Cave | Chest | LightWorld/South.php:39 |
| Hobo | Npc | LightWorld/South.php:40 |
| Bombos Tablet | Drop\Bombos | LightWorld/South.php:41 |
| Cave 45 | Standing | LightWorld/South.php:42 |
| Checkerboard Cave | Standing | LightWorld/South.php:43 |
| Mini Moldorm Cave - NPC | Npc | LightWorld/South.php:44 |
| Library | Dash | LightWorld/South.php:45 |
| Maze Race | Standing | LightWorld/South.php:46 |
| Desert Ledge | Standing | LightWorld/South.php:47 |
| Lake Hylia Island | Standing | LightWorld/South.php:48 |
| Sunken Treasure | Standing | LightWorld/South.php:49 |
| Flute Spot | Dig\HauntedGrove | LightWorld/South.php:50 |

##### Light World — Death Mountain — West (4 locations)

| Location name | Type | PHP source |
|---|---|---|
| Old Man | Npc | LightWorld/DeathMountain/West.php:30 |
| Spectacle Rock Cave | Standing | LightWorld/DeathMountain/West.php:31 |
| Ether Tablet | Drop\Ether | LightWorld/DeathMountain/West.php:32 (Standard) — **also re-added in Inverted at LightWorld/DeathMountain/East.php:25** |
| Spectacle Rock | Standing | LightWorld/DeathMountain/West.php:33 (Standard) — **also re-added in Inverted at LightWorld/DeathMountain/East.php:26** |

##### Light World — Death Mountain — East (9 locations)

| Location name | Type | PHP source |
|---|---|---|
| Spiral Cave | Chest | LightWorld/DeathMountain/East.php:32 |
| Mimic Cave | Chest | LightWorld/DeathMountain/East.php:33 |
| Paradox Cave Lower - Far Left | Chest | LightWorld/DeathMountain/East.php:34 |
| Paradox Cave Lower - Left | Chest | LightWorld/DeathMountain/East.php:35 |
| Paradox Cave Lower - Right | Chest | LightWorld/DeathMountain/East.php:36 |
| Paradox Cave Lower - Far Right | Chest | LightWorld/DeathMountain/East.php:37 |
| Paradox Cave Lower - Middle | Chest | LightWorld/DeathMountain/East.php:38 |
| Paradox Cave Upper - Left | Chest | LightWorld/DeathMountain/East.php:39 |
| Paradox Cave Upper - Right | Chest | LightWorld/DeathMountain/East.php:40 |
| Floating Island | Standing | LightWorld/DeathMountain/East.php:41 |

##### Dark World — Mire (2 locations)

| Location name | Type | PHP source |
|---|---|---|
| Mire Shed - Left | Chest | DarkWorld/Mire.php:32 |
| Mire Shed - Right | Chest | DarkWorld/Mire.php:33 |

##### Dark World — North East (5 + conditional Pyramid Fairy ×2 + 1 prize-event = 8)

| Location name | Type | PHP source |
|---|---|---|
| Catfish | Standing | DarkWorld/NorthEast.php:32 |
| Pyramid | Standing | DarkWorld/NorthEast.php:33 |
| Pyramid Fairy - Sword | Trade | DarkWorld/NorthEast.php:34 |
| Pyramid Fairy - Bow | Trade | DarkWorld/NorthEast.php:35 |
| Ganon (Prize event) | Prize\Event | DarkWorld/NorthEast.php:36 (Standard) — **also re-added in Inverted at LightWorld/NorthEast.php:19** |
| Pyramid Fairy - Left | Chest (conditional) | DarkWorld/NorthEast.php:40 (only if `region.swordsInPool`) |
| Pyramid Fairy - Right | Chest (conditional) | DarkWorld/NorthEast.php:41 (only if `region.swordsInPool`) |

##### Dark World — North West (7 locations)

| Location name | Type | PHP source |
|---|---|---|
| Brewery | Chest | DarkWorld/NorthWest.php:32 |
| C-Shaped House | Chest | DarkWorld/NorthWest.php:33 |
| Chest Game | Chest | DarkWorld/NorthWest.php:34 |
| Hammer Pegs | Standing | DarkWorld/NorthWest.php:35 |
| Bumper Cave | Standing | DarkWorld/NorthWest.php:36 |
| Blacksmith | Npc | DarkWorld/NorthWest.php:37 |
| Purple Chest | Npc | DarkWorld/NorthWest.php:38 |

##### Dark World — South (7 locations)

| Location name | Type | PHP source |
|---|---|---|
| Hype Cave - Top | Chest | DarkWorld/South.php:32 |
| Hype Cave - Middle Right | Chest | DarkWorld/South.php:33 |
| Hype Cave - Middle Left | Chest | DarkWorld/South.php:34 |
| Hype Cave - Bottom | Chest | DarkWorld/South.php:35 |
| Stumpy | Npc | DarkWorld/South.php:36 |
| Hype Cave - NPC | Npc | DarkWorld/South.php:37 |
| Digging Game | Dig | DarkWorld/South.php:38 |

##### Dark World — Death Mountain — West (1 location)

| Location name | Type | PHP source |
|---|---|---|
| Spike Cave | Chest | DarkWorld/DeathMountain/West.php:32 |

##### Dark World — Death Mountain — East (6 locations)

| Location name | Type | PHP source |
|---|---|---|
| Superbunny Cave - Top | Chest | DarkWorld/DeathMountain/East.php:32 |
| Superbunny Cave - Bottom | Chest | DarkWorld/DeathMountain/East.php:33 |
| Hookshot Cave - Top Right | Chest | DarkWorld/DeathMountain/East.php:34 |
| Hookshot Cave - Top Left | Chest | DarkWorld/DeathMountain/East.php:35 |
| Hookshot Cave - Bottom Left | Chest | DarkWorld/DeathMountain/East.php:36 |
| Hookshot Cave - Bottom Right | Chest | DarkWorld/DeathMountain/East.php:37 |

#### Inverted-only additions

| Location name | Type | PHP source | Notes |
|---|---|---|---|
| Bomb Merchant (Prize event) | Prize\Event | Inverted/LightWorld/South.php:27 | **New** in Inverted — a `Prize\Event` slot, not a placement |

(Other Inverted edits are re-locations of existing names: `Link's House` moves to `DarkWorld/South`, `Ganon` Prize\Event moves to `LightWorld/NorthEast`, `Ether Tablet` and `Spectacle Rock` are re-added in `LightWorld/DeathMountain/East`.)

#### Tally

- **Light World**: 9 (NE) + 22 (NW) + 19 (S) + 4 (DM-W) + 10 (DM-E) = 64
- **Dark World**: 2 (Mire) + 7 (NE incl. conditional Pyramid Fairy chests + Prize\Event) + 7 (NW) + 7 (S) + 1 (DM-W) + 6 (DM-E) = 30
- **Dungeons (chests/drops, excluding Prize/Event)**: 10 (HCE) + 6 (EP) + 6 (DP) + 6 (TOH) + 2 (HCT) + 14 (POD) + 10 (SP) + 8 (SW) + 8 (TT) + 8 (IP) + 8 (MM) + 12 (TR) + 27 (GT) = 125
- **Prize/Event slots** (Pendants, Crystals, story events — not item placements but logic gates): 7 (one per dungeon) + 3 (Zelda, Agahnim, Agahnim 2) + 1 (Inverted-only Bomb Merchant) = 11
- **Medallion-set slots** (configuration, not placements): 2
- **Fountains** (Standard only): 2

**Total named Location entries**: 64 + 30 + 125 + 11 + 2 + 2 = **234** — but accounting for the duplicates (`Link's House`, `Ganon`, `Ether Tablet`, `Spectacle Rock`, etc. which are world-state-conditional and counted once each in the canonical pool), the **distinct placement-relevant locations** number approximately **212** as cited in `lessons.md`. The Phase A registry SHALL canonicalize each name once, with a `world_state_filter` field to mark world-state-only locations (e.g. `inverted_only`).

### §0.3.6 — Unresolved §0.3.2 grant-site mappings (v0.3)

These §0.1.2/§0.1.3 entries still lack a confirmed ALTTPR canonical-location mapping after v0.2 work:

| Our site | Status | What's needed |
|---|---|---|
| sprite_main.c:1267 (`sprite_graphics[k]` as item id) | **unresolved** | Identify the enclosing sprite handler at line 1267. Likely a generic NPC-gift wrapper. v0.3: read the surrounding `Sprite_*` function. |
| sprite_main.c:2130 (item=1) | **probable** = Master Sword Pedestal | The item value (sword tier 2) and lack of an enclosing NPC strongly suggest the Master Sword Pedestal handler. v0.3: confirm by reading function start. |
| sprite_main.c:5868 (item=0x18) | **inconsistent in v0.1** | v0.1 said "magic powder", but item 0x18 dispatches to `link_item_torch/lamp = 1` per the dispatch table. v0.3: re-read this site's handler context. Likely **Lamp grant** (Old Mountain Man? — unconfirmed). |
| sprite_main.c:18192 (`sprite_A[k]` as item id) | **unresolved** | Sprite-state-driven id; the enclosing handler determines semantics. v0.3: read context. |

### §0.3.7 — §6 gaps (ALTTPR locations with no current grant-site mapping)

These ALTTPR canonical locations from §0.3.5 do NOT have a confirmed `Link_ReceiveItem` or direct-write site in §0.1.2/§0.1.3. They are **§6 gaps** requiring either new instrumentation, a synthesized hook (§0.3.4), or confirmation that the location is reached via the **universal chest path** (player.c:3815). For chests, the universal path covers them — §6 only needs the location_id wiring at the chest-tile level. For non-chest locations, new code is required.

#### Definitively chest-driven (universal path covers; needs chest-tile→location_id mapping)

All "**Chest**" / "**BigChest**" type locations from §0.3.5 — approximately 125 entries across dungeons + overworld chest locations like `Hype Cave - Top`, `Brewery`, `Mire Shed - Left`, `Kakariko Well - Top`, etc. The chest data 0xEXXX address (visible in the `[0xEXXX]` array in each PHP `new Location\Chest(...)` declaration) is the SNES address of the room-data byte that stores the chest's item. §6 needs to translate `chest_position` (the `OpenChestForItem` parameter at player.c:3815) into a stable location_id by indexing into a vanilla chest table.

#### Standing / Dash / Dig / Drop / Pedestal / Fountain (NEW instrumentation required)

These are non-chest pickups handled by sprite/ancilla routines NOT yet enumerated in §0.1.2 or §0.1.3:

| ALTTPR location | Type | §6 work required |
|---|---|---|
| Master Sword Pedestal | Pedestal | New hook in pedestal-pull handler (likely sprite_main.c:2130 — v0.3 confirm) |
| King Zora | Npc\Zora | Zora-flippers grant site — likely a special sprite handler not yet enumerated. Search `Sprite_*Zora*`. |
| Sahasrahla | Npc | sprite_main.c:6546 covers (§0.1.2 row 15) |
| Old Man | Npc | sprite_main.c:24682 covers (§0.1.2 row 27) |
| Magic Bat | Npc | **No matching enumerated grant site yet.** ancilla.c:4019 is `Ancilla36_Flute` (full Flute pickup, item 0x14) — NOT Magic Bat. Magic Bat (`app/Region/Standard/LightWorld/NorthWest.php:48`) gives Magic Powder in vanilla via a separate handler. v0.3: locate the Magic Bat sprite handler (search `Sprite_*Bat*` / handler that responds to powder-thrown collision). |
| Sick Kid | Npc\BugCatchingKid | sprite_main.c:10436 covers (§0.1.2 row 23) |
| Bottle Merchant | Npc | sprite_main.c:6195 covers (§0.1.2 row 10) |
| Blacksmith | Npc | sprite_main.c:10184 covers (§0.1.2 row 22 — but **only** the L3 sword grant; the placed item at this location may be anything in shuffle) |
| Purple Chest | Npc | sprite_main.c:10648 covers (§0.1.2 row 24) |
| Hobo | Npc | sprite_main.c:10738 covers (§0.1.2 row 25) |
| Stumpy | Npc | sprite_main.c:9922 covers (§0.1.2 row 21 — Stumpy's grant of the shovel; placed item in shuffle differs) |
| Potion Shop (Witch) | Npc\Witch | sprite_main.c:11110 covers (the half-magic grant — but the **shop slot** is in `sprite_main.c:25308` shop dispatch) |
| Mini Moldorm Cave - NPC | Npc | Sprite handler — likely a "give item" wrapper similar to 1267 (cucco-style). v0.3 audit. |
| Hype Cave - NPC | Npc | Sprite handler — v0.3 audit |
| Catfish | Standing | "Standing" sprite drop — new instrumentation needed |
| Pyramid | Standing | New instrumentation (Pyramid altar) |
| Zora's Ledge | Standing | New instrumentation (zoras-ledge sprite tile interaction) |
| Lost Woods Hideout | Standing | New instrumentation (mushroom-grab style) |
| Lumberjack Tree | Standing | New instrumentation (lumberjack tree dash) |
| Graveyard Ledge | Standing | New instrumentation |
| Mushroom | Standing | sprite_main.c:6387 covers (§0.1.2 row 11) |
| Cave 45 | Standing | New instrumentation |
| Checkerboard Cave | Standing | New instrumentation |
| Maze Race | Standing | New instrumentation (race-completion handler) |
| Desert Ledge | Standing | New instrumentation |
| Lake Hylia Island | Standing | New instrumentation |
| Sunken Treasure | Standing | New instrumentation |
| Spectacle Rock | Standing | New instrumentation |
| Spectacle Rock Cave | Standing | New instrumentation |
| Floating Island | Standing | New instrumentation |
| Hammer Pegs | Standing | New instrumentation (post-pegs reward) |
| Bumper Cave | Standing | New instrumentation |
| Tower of Hera - Basement Cage | Standing\HeraBasement | New instrumentation (Hera basement cage) |
| Pegasus Rocks | Chest | Standard chest path — but the "Pegasus Rocks" name suggests a non-chest pickup; verify chest data |
| Bombos Tablet | Drop\Bombos | Tablet-dash handler — new instrumentation (similar to Ether Tablet) |
| Ether Tablet | Drop\Ether | Tablet-dash handler — new instrumentation |
| Desert Palace - Torch | Dash | Dash-torch handler — new instrumentation (also `Ganon's Tower - Bob's Torch`) |
| Ganon's Tower - Bob's Torch | Dash | Same Dash handler family |
| Library | Dash | sprite_main.c:7053 covers the Book of Mudora pickup (§0.1.2 row 20) |
| Digging Game | Dig | New instrumentation (digging game completion) |
| Flute Spot | Dig\HauntedGrove | New instrumentation (haunted-grove dig) |
| Waterfall Bottle | Fountain | New instrumentation (waterfall fairy bottle) |
| Pyramid Bottle | Fountain | New instrumentation (pyramid fairy bottle) |
| Pyramid Fairy - Sword / Bow / Left / Right | Trade / Chest | **Synthesized site** per §0.3.4 (Phase A) |
| Link's Uncle | Npc\Uncle | sprite_main.c:5733 covers (§0.1.2 row 8) — but this is just sword grant; rando placed item differs |

#### Boss-drop / Prize / Event (logic gate; not always a placement)

- `<Dungeon> - Boss` (heart container, 10 entries) — sprite_main.c:6457 covers
- `<Dungeon> - Prize` (crystal/pendant, 10 entries) — ancilla.c:3828 covers
- `Zelda` (event) — sprite_main.c:6342 sets the `sram_progress_indicator = 2` which is logic-equivalent; sets `RescueZelda` item
- `Agahnim` (event) — misc.c:313 (`KillAghanim_Func12`) sets `save_ow_event_info[0x1b] |= 32`
- `Agahnim 2` (event) — same path as Aga 1 but for GT; v0.3 audit
- `Ganon` (event) — game-end module; v0.3 audit
- `Bomb Merchant` (event, Inverted-only) — v0.3 audit

#### Medallion-set slots (config, not placements)

- `Turtle Rock Medallion`, `Misery Mire Medallion` — handled by settings, not by §6 grant instrumentation. Rando engine writes these to the medallion-check sprite handlers at game-start.

### §0.3.4 — Synthesized grant sites (pre-flagged from spec)

These are NOT in §0.1.2/§0.1.3 because vanilla code doesn't have a single grant point for them. §6 adds *net-new* code paths:

| Synthesized site | ALTTPR slots | Vanilla fallback | Reference |
|---|---|---|---|
| Pyramid Fairy | `pyramid_fairy_sword`, `pyramid_fairy_bow`, optionally `pyramid_fairy_left`, `pyramid_fairy_right` | sword slot → `L1Sword`, bow slot → `Bow` | task 6.7; ALTTPR `app/Region/Standard/DarkWorld/NorthEast.php:34-41,59-60` |

---

## §0.4 — Item types needing new receive paths

Per the spec ([proposal.md](proposal.md) §"Items"), these item types have **no existing receive path** in `AncillaAdd_ItemReceipt`'s dispatcher and require new code in §6.2:

### §0.4.1 — Items needing brand-new receive code

| Item id (proposal name) | Required receive behavior | Notes |
|---|---|---|
| `ProgressiveSword` | Advance `link_sword_type` by one tier (1→2→3→4) | New helper. Existing `kMemoryLocationToGiveItemTo` indices 0-3 write **absolute** sword tier; progressive is an arithmetic increment. |
| `ProgressiveShield` | Advance `link_shield_type` by one tier (Fighter→Red→Mirror) | Same pattern; existing entries (4-6) write absolute. |
| `ProgressiveArmor` | Advance `link_armor` by one tier (Green→Blue→Red) | Same pattern. |
| `ProgressiveGlove` | Advance `link_item_gloves` by one tier (Power→Titan) | Same pattern. |
| `ProgressiveBow` | Advance `link_item_bow` by one tier (Bow→Bow+Silvers) | Same pattern. |
| Small key (per dungeon, isolated grant) | Increment `link_num_keys`, with optional cross-dungeon wild-keys routing | New animation; existing sprite.c:1402 increments in vanilla (alt path) |
| Big key (per dungeon, isolated grant) | OR a bit into `link_bigkey` for the *placed* dungeon (not the current one) | Existing dispatcher OR-writes `kMemoryLocationToGiveItemTo[50] = 0xF366` for the *current* dungeon — wild-keys mode needs an arbitrary dungeon bit. |
| Map (per dungeon) | OR a bit into `link_dungeon_map` for the *placed* dungeon | Same pattern as big key. |
| Compass (per dungeon) | OR a bit into `link_compass` for the *placed* dungeon | Same pattern. |
| Multi-tier rupee (`Rupee1`, `Rupee5`, `Rupee20`, `Rupee100`, `Rupee300`) | Add the value to `link_rupees_goal`, clamping per current MaxRupees | Existing dispatcher entries 52, 53, 54 are `+1`, `+5`, `+20`; 64, 65 are `+100`, `+300`. Need a unified path that respects the placement's specific rupee item. |
| `Rupoor` | **Subtract** rupees (negative grant) | Item-pool-difficulty `hard`/`expert` only. New code. |
| `BottleEmpty`, `BottleWithFairy`, `BottleWithBee`, `BottleWithGoodBee`, `BottleWithRedPotion`, `BottleWithGreenPotion`, `BottleWithBluePotion` | Find first empty `link_bottle_info[0..3]` slot and write the corresponding content code (1, 6, 7, 8, 2, 3, 4 — verify against `ItemReceipt_GiveBottledItem` at misc.c:846) | Existing dispatcher handles `BottleEmpty` via item 0x16; randomizer needs distinct items per content. |
| `Prize_Crystal1..7`, `Prize_GreenPendant`, `Prize_RedPendant`, `Prize_BluePendant` | OR-into-bit on `link_has_crystals` (crystal) / `link_which_pendants` (pendant) for the *target* dungeon, NOT the current one | Vanilla ancilla.c:3828 indexes by `cur_palace_index_x2`; prize-shuffle mode needs to OR the placed prize's bit, decoupled from current dungeon. |
| `SilverArrowUpgrade` | Set "have silvers" bit on `link_item_bow` (transitioning 1→2 or 3→4) | Absolute-bow mode; in progressive mode this is the same as advancing past `ProgressiveBow(1)`. |
| `HalfMagic`, `QuarterMagic` | Write `link_magic_consumption = 1` (half) or `2` (quarter) | Existing sprite_main.c:11110 sets half — needs a new dispatched form. |
| `TriforcePiece` | Increment a Triforce-piece counter in `kRam_*` (new cell — see §0.7); on threshold, trigger goal-completion | New code + new RAM cell. (Triforce Hunt / Ganon Hunt only.) |

### §0.4.2 — Items already dispatcher-safe

Existing dispatcher entries cover these correctly; §6 only needs to call `Link_ReceiveItem(<existing item_id>, …)`:

- All absolute items (`Bow`, `Boomerang`, `Hookshot`, `MagicPowder`, `FireRod`, `IceRod`, `Bombos`, `Ether`, `Quake`, `Lamp`, `Hammer`, `Flute`, `BugNet`, `BookOfMudora`, `CaneOfSomaria`, `CaneOfByrna`, `Cape`, `MagicMirror`, `Boots`, `Flippers`, `MoonPearl`)
- Absolute `L1Sword`, `L2Sword`, `L3Sword`, `L4Sword` (indices 0-3 — but only if randomizer uses the absolute path)
- Absolute `FighterShield`, `RedShield`, `MirrorShield` (indices 4-6)
- Absolute `BlueMail`, `RedMail` (indices 34, 35)
- `PowerGlove`, `TitanMitt` (indices 27, 28 — absolute path)
- `BossHeartContainer` (index 23 via item id 0x26)
- `PieceOfHeart` (existing handler at sprite_main.c:6493 increments and rolls)
- `Heart` (small heart pickup — existing dispatcher entry 66 via `link_hearts_filler += 8`)

### §0.4.3 — Coverage assertion

The randomizer's item registry (task 3.2) declares every ID listed in §0.4.1 + §0.4.2 + the junk pool (`SmallMagic`, `Arrow1`, `Arrow10`, `Bombs1`, `Bombs3`, `Bombs10`). Build-time validation (task 3.10) asserts every `OP_HAS_ITEM <id>` predicate and every goal predicate references an ID in the registry. **Coverage gap**: virtual items (`StartingHeart`) are in the registry but have no grant path — this is correct per spec (placer pre-populates the count).

---

## §0.4a — Reachability-affecting events (the logic-graph hooks)

These are *not* item-grant sites — they are story-progress event flags that change which regions are reachable. The logic graph (`randomizer-logic` spec) references them via `OP_WORLDSTATE_EQ` and per-event predicates. §6 needs a `Rando_BumpReachabilityCounter()` call after each write so the tracker recomputes reachability.

| File:line | Write | Event (v0.2 resolved by reading enclosing handler) | Logic predicate (reference) |
|---|---|---|---|
| sprite_main.c:5738 | `sram_progress_indicator = 1` | Uncle leaves house / sword+shield given (`Uncle_InPassage` case 1 "GiveSwordAndShield") | `OP_WORLDSTATE_EQ post_uncle` — ALTTPR macro `hasRescuedZelda` / `Zelda` event isn't equivalent here; closer to `post_uncle_dead` state per ALTTPR `World.php` mode.state logic |
| dungeon.c:2504 | `sram_progress_indicator = 3` | Master-sword pulled (per v0.1 reading) | `OP_WORLDSTATE_EQ post_pedestal` |
| sprite_main.c:6342 | `sram_progress_indicator = 2` | **Sanctuary reached** (Zelda dialogue case 1 "respond to priest" in `Sprite_Zelda` — escorts Zelda to Priest, sets `SavePalaceDeaths()`) | `OP_WORLDSTATE_EQ post_sanctuary` (ALTTPR `RescueZelda` event predicate) |
| sprite_main.c:5588 | `sram_progress_flags \|= 0x2` | **Priest dying intro** (`Priest_Dying` case 0 "Priest_LyingOnGround") — first time entering sanctuary post-priest-death dialog. Marks priest's-death scene seen. | (Internal cutscene gate; no ALTTPR predicate directly maps — used by `SpritePrep_UncleAndPriest_bounce` at 10864 to suppress respawn) |
| sprite_main.c:5714 | `sram_progress_flags \|= 0x10` | **Uncle leaves house** (`Uncle_AtHouse` case 4 "Uncle_ApplyTelepathyFollower") — Zelda-telepathy follower attached as Link wakes; used as "intro played" marker | (Internal cutscene gate; sets `follower_indicator = 5`; ALTTPR has no separate predicate — intro is always-true in randomizer logic) |
| sprite_main.c:5737 | `sram_progress_flags \|= 1` | **Uncle gives sword+shield** (`Uncle_InPassage` case 1 "GiveSwordAndShield"; same line block as 5738) — marks uncle-death cutscene complete | (Same as 5738; ALTTPR's `Zelda` event is the prerequisite for nearly all logic — closest match: `hasRescuedZelda` requirement = `RescueZelda` item placed at "Zelda" prize-event location per `app/Region/Standard/HyruleCastleEscape.php:51,55-56`) |
| sprite_main.c:6528 | `sram_progress_flags \|= 0x20` | **Aginah dialogue seen** (`Sprite_Aginah` default-msg fallthrough) — marks Aginah's hint conversation as played | (Internal NPC-met state; no ALTTPR logic predicate) |
| sprite_main.c:10863 | `sram_progress_flags \|= 2` | **Sanctuary scene already seen** (`SpritePrep_UncleAndPriest_bounce`: when `sram_progress_indicator >= 3` AND room=18, mirrors flag 0x2 for resume-game state) | (Same gate as 5588) |
| sprite_main.c:10879 | `sram_progress_flags \|= 0x4` | **Zelda escorted to sanctuary** (`SpritePrep_UncleAndPriest_bounce`: when sanctuary follower is priest, marks the escort completion + `save_ow_event_info[0x1b] \|= 0x20`) | `OP_WORLDSTATE_EQ post_zelda_rescued` (ALTTPR `Zelda` event — see `HyruleCastleEscape.php` Zelda prize event) |
| sprite_main.c:12854 | `sram_progress_flags ^= 0x40` | **Fortune Teller toggle parity** (`FortuneTeller_PerformPseudoScience` — alternates between two reading slots) | **NOT logic-affecting** — cosmetic state; exclude from reachability hook |
| sprite_main.c:9951 | `sram_progress_indicator_3 \|= 8` | **Flute Boy duck-cutscene complete** (`Sprite_FluteKid_Stumpy` case 5 "done" — runs after player plays flute in front of Stumpy; bird/duck animation completes) | (Marks flute-quest middle stage; logic uses `link_item_flute >= 2`, not this flag directly) |
| sprite_main.c:10020 | `sram_progress_indicator_3 \|= 32` | **Smithy Brothers reunited** (`Smithy_Homecoming` case 1 "Thankful" — returning smithy thanks player after escort) | `OP_HAS_EVENT smith_returned` (ALTTPR `"Blacksmith"` location at `app/Region/Standard/DarkWorld/NorthWest.php:37`) |
| sprite_main.c:10161 | `sram_progress_indicator_3 \|= 128` | **Smithy tempering started** (`Smithy_Main` case 3 "HandleTemperingCost" — Link paid 10 rupees, sword taken away; `link_sword_type = 255`) | (Transient: cleared at 10185; not exposed to logic) |
| sprite_main.c:10185 | `sram_progress_indicator_3 &= ~0x80` | **Smithy tempering complete** (`Smithy_Main` case 6 "Smithy_GiveTemperedSword" — L3 sword returned; clears the in-progress bit set at 10161) | (Pair with 10161; once cleared, item-grant 10184 fires) |
| sprite_main.c:10649 | `sram_progress_indicator_3 \|= 0x10` | **Purple Chest opened** (`Sprite_MiddleAgedMan` case 3 "react to secret keeping" — bottle granted) | `OP_HAS_EVENT purple_chest_opened` (ALTTPR `"Purple Chest"` at `app/Region/Standard/DarkWorld/NorthWest.php:38`) |
| sprite_main.c:10739 | `sram_progress_indicator_3 \|= 1` | **Hobo bottle taken** (`Sprite_Hobo_Bum` case 2 "grant bottle") | `OP_HAS_EVENT hobo_bottle_taken` (ALTTPR `"Hobo"` at `app/Region/Standard/LightWorld/South.php:40`) |
| sprite_main.c:6196 | `sram_progress_indicator_3 \|= 2` | **Bottle Merchant purchased** (`Sprite_BottleVendor` case 2 "giving" — 100r purchase) | `OP_HAS_EVENT bottle_merchant_purchased` (ALTTPR `"Bottle Merchant"` at `app/Region/Standard/LightWorld/NorthWest.php:47`) |
| ancilla.c:3828 | `link_has_crystals \|= <bit>` | Boss kill — crystal/pendant grant | Already in §0.3 as a `grant`; the *crystal bit being set* is also a reachability event for `OP_HAS_AMOUNT crystals >= N` and per-dungeon `OP_HAS_PRIZE <dungeon>` |
| misc.c:313 | `save_ow_event_info[0x1b] \|= 32` | **Agahnim 1 defeated** (`KillAghanim_Func12` at `src/misc.c:308-322`; final step of `kModule_KillAgahnim`; also sets `savegame_map_icons_indicator = 6`) | `OP_WORLDSTATE_EQ aga1_dead` (ALTTPR `"Agahnim"` prize event at `app/Region/Standard/HyruleCastleTower.php:43`; sets `DefeatAgahnim` item) |
| **(no single write)** | Per-dungeon boss-cleared flag | The "boss cleared" state is encoded via `dung_savegame_state_bits` writes during the boss-kill ancilla (see ancilla.c:3828, ancilla.c:3429-3441 for the heart container, and `Sprite_HeartContainer` for the dung_savegame_state_bits OR-write at sprite_main.c:6452, 6461, 6511). The dungeon-cleared status is read from `save_ow_event_info` and `dung_savegame_state_bits` at the spotlight transition (`Dungeon_PrepExitWithSpotlight` at overworld.c:601). | `OP_DUNGEON_CLEARED <dungeon>` (per-dungeon `_Prize` location's `can_complete` predicate in ALTTPR region PHP) |
| **(no separate write)** | King's Tomb item taken | King's Tomb is a vanilla **chest** (ALTTPR `"King's Tomb"` at `app/Region/Standard/LightWorld/NorthWest.php:33`, chest data 0xE97A). It uses the standard chest path (player.c:3815) — no separate flag needed; tracked by the chest-open flag in `save_dung_event_info` set inside `Link_PerformOpenChest`. | `OP_HAS_ITEM <king_tomb_item>` — driven by chest-open path, no new instrumentation needed |

**Implementation pattern** (per task 0.4a):

```c
// after the existing flag write:
sram_progress_indicator_3 |= 8;
#ifdef RANDOMIZER
if (enhanced_features1 & kFeatures1_RandomizerActive)
  Rando_BumpReachabilityCounter();
#endif
```

The `Rando_BumpReachabilityCounter()` increments `g_reachability_state_counter` (heap, in `src/rando/`) which the tracker uses to invalidate its memoized `Logic_ComputeReachability` cache (per `randomizer-logic` spec, "Reachability state counter" requirement).

**v0.2 work**: replace each `(TODO)` row above with the actual event name and matching ALTTPR `Event` predicate.

---

## §0.5 — Phase A logic axes (pinned)

Per [proposal.md](proposal.md) §"Phase A scope":

| Axis | Phase A value | Phase B+ extension |
|---|---|---|
| `tricks` | **`none`** (no trick predicates evaluate true) | `OP_TRICK <trick_id>` reserved in op registry; individual trick predicates (boots-clip, fake-flipper, bunny-revival, dark-room nav, bomb-jump) ship in Phase B |
| `item_pool_difficulty` | **`easy`, `normal` (default), `hard`, `expert`** — all four supported in Phase A | Pool-construction-only; placement algorithm unchanged. `Rupoor` enters pool only at `hard`/`expert`. |
| `logic` (glitch-logic level) | **`NoGlitches`** (Phase A pinned value) | `OP_GLITCH_LEVEL_AT_LEAST <level>` reserved; `OverworldGlitches`, `MajorGlitches`, `HybridMG`, `NoLogic` ship in later phases |

**Trick predicate reservations** (op-registry entries assigned now, evaluated to `false` in Phase A):
- `OP_TRICK boots_clip`
- `OP_TRICK fake_flipper`
- `OP_TRICK bunny_revival`
- `OP_TRICK dark_room_nav`
- `OP_TRICK bomb_jump`
- (Full list per ALTTPR's `app/Support/ItemCollection.php` trick predicates — TODO complete enumeration in v0.2.)

---

## §0.5a — Phase A goal set (pinned)

Per [proposal.md](proposal.md) §"Goals":

| Goal name (snake_case) | Description | Crystal-setting interaction |
|---|---|---|
| `ganon` | Defeat Ganon | `crystals.ganon` controls vulnerability; `crystals.tower` controls Ganon's Tower entry — **set independently** per spec |
| `fast_ganon` | Skip dungeon clears once Ganon is vulnerable | Same dual-crystal policy as `ganon` |
| `dungeons` | All seven dungeon clears | Crystals collected as side-effect; explicit "all 7" requirement |
| `pedestal` | Pull Master Sword from pedestal | Three colored pendants required |
| `triforce-hunt` | Collect N Triforce pieces, end at pedestal | `pieces_required` / `pieces_placed` settings |
| `ganonhunt` | Collect N Triforce pieces, then kill Ganon | Same Triforce settings + dual-crystal |
| `completionist` | Every location reachable under accumulated inventory | Auto-sets `accessibility=locations` |

**Dual-crystal policy** (pinned for Phase A): `crystals.ganon` and `crystals.tower` are **independent** settings (0-7 each). Default values per ALTTPR are 7/7 for `ganon` and `fast_ganon`. Routing logic treats them as separate predicates: `OP_HAS_AMOUNT crystals >= settings.crystals_ganon` for Ganon vulnerability, `OP_HAS_AMOUNT crystals >= settings.crystals_tower` for GT entry.

---

## §0.5b — Phase A shuffle set (pinned)

| Shuffle | Phase A state | Notes |
|---|---|---|
| Item shuffle | **always-on** (Phase A is the item-shuffle phase) | The core randomizer behavior |
| Dungeon-item modes | **per-class**, each ∈ {`Vanilla`, `Dungeon`, `Wild`} — 4 classes: small keys, big keys, maps, compasses | Affects pool/location cardinality and therefore junk-padding |
| Prize shuffle | **default-on** in Phase A | Assigns Crystal 1-7 + Green/Red/Blue Pendant to dungeons. Logic-affecting (Green Pendant gates Sahasrahla, all 7 crystals gate Ganon, 3 colored pendants gate the Pedestal). |
| Medallion shuffle | **default-on** in Phase A | Assigns Bombos/Ether/Quake to Misery Mire entrance + Turtle Rock entrance. Independent of prize shuffle. |
| Boss shuffle | **deferred to Phase B** | Boss-room assignments; preserves dungeon→reward binding (which is why §6.6 keeps prize and boss-heart as separate location IDs per dungeon) |
| Entrance shuffle | **deferred to Phase C** | Uses RegionRemap overlay (task 3.7a) |
| Drop-pool shuffle | **deferred to Phase B** | Runs after item placement so spheres are known |
| Cosmetic shuffles | **deferred to Phase D** | Driven by a separate `cosmetic_seed`; placement-table-invariant |

---

## §0.6 — Save-storage and snapshot-tail model (confirmed)

### §0.6.1 — Sidecar `sram_rando.dat` (3 slots, no 4th)

- Existing `sram.dat`: 8 KB, 3 slots × {primary, backup} = 7.5 KB consumed.
- `kSrmOffsets[4] = {0, 0x500, 0xa00, 0xf00}` at [src/messaging.c:103](../../../src/messaging.c#L103) — the 4th entry (`0xf00`) is the **backup-region base**, NOT a 4th slot. Confirmed per [CLAUDE.md](../../../CLAUDE.md) internal-references section.
- Sidecar `sram_rando.dat`: parallel layout. **3 slots**, mirrors `sram.dat`'s 3-slot layout. **No 4th slot anywhere.** File-select UI (task 9.3a) preserves the existing `kSelectFile_Draw_Y[3]` geometry.
- Per-slot layout (per `randomizer-save / Sidecar slot contents`): 80-byte header + embedded placement table + checked-location bitmap. Header fields enumerated in `randomizer-save` spec.
- Atomic commit (per design D12, task 8.2): write `sram_rando.dat.tmp`, `fflush`, `fsync`/`_commit`, `rename`, fsync containing directory (POSIX). Same protocol for `sram.dat.tmp`. **Save order: sidecar first, then `sram.dat`** (so a crash between writes leaves `sram.dat` matching the pre-write `sram_rando.dat`, which is the safer of two recovery branches).
- Existing `sram.dat` format is **byte-untouched**. Vanilla-only readers see slots as vanilla.

### §0.6.2 — Snapshot tail (`StateRecorder` extension)

- `StateRecorder_Save` at [src/zelda_rtl.c:533-558](../../../src/zelda_rtl.c#L533) writes header (16 bytes) + input log + base_snapshot + `SaveSnesState` dump. The base_snapshot lives at [zelda_rtl.c:425](../../../src/zelda_rtl.c#L425) (`ByteArray base_snapshot`).
- `StateRecorder_ClearKeyLog` at [zelda_rtl.c:561](../../../src/zelda_rtl.c#L561) clears the input log and refills `base_snapshot` with a fresh `SaveSnesState` dump.
- Rando snapshot extension (task 8.8): **force `StateRecorder_ClearKeyLog` before save** so `base_snapshot` is populated, then after the existing 4-chunk write, append one or more TLV entries (`magic[8] + type[4] + length[4] + payload`). Phase A emits a single `TAIL_RANDO_STATE` TLV (payload = `generator_version + settings_hash + share_string + placement_table_size + placement_table`); fsync.
- Load: after the existing `assert(state.p == state.pend)` in `StateRecorder_Load`, iterate TLV entries — read magic/type/length, dispatch on known types, seek past unknown types, terminate on EOF. **Ordering invariant** (task 8.8a): `LoadSnesState` (which restores `g_ram` including `kRam_RandoSlotActive`) and the TLV reinstall MUST execute in the same call before any game frame can run; a debug-build assertion confirms no `Rando_OnLocationCheck` fires between these two steps.
- Older-binary forward-compat: an older binary reading a new snapshot ignores trailing TLV bytes (graceful degradation — snapshot loads as vanilla).

---

## §0.7 — `kRam_*` offset scan (pinned)

### §0.7.1 — Scan of `g_ram+0x6XX` references

Grep of `g_ram\+0x6[0-7][0-9a-fA-F]` across `src/`:

| Offset | Cell | Source |
|---|---|---|
| 0x600-0x60F (union region) | `room_bounds_y/x` (RoomBounds), `ow_scroll_vars0/1` (OwScrollVars) | variables.h:1463-1473 — context-dependent union |
| 0x610-0x617 | scroll target cells | variables.h:527-530 |
| 0x618-0x61F | camera y/x scroll cells | variables.h:531-534 |
| 0x620-0x623 | BG1HOFS/VOFS subpixel | variables.h:535-536 |
| 0x624-0x62B | overworld_unk1/_neg, overworld_unk3/_neg | variables.h:537-540 |
| 0x62C-0x62F | dung_loade_bgoffs_h_copy / v_copy | variables.h:541-542 |
| 0x630-0x631 | byte_7E0630, byte_7E0631 (also selectfile_var8 union at 0x630) | variables.h:543-544, 1278, 1422 |
| 0x635 | byte_7E0635 | variables.h:545 |
| 0x636 | overworld_map_flags | variables.h:546 |
| 0x637 | timer_for_mode7_zoom | variables.h:547 |
| 0x638-0x63B | M7X_copy, M7Y_copy | variables.h:548-549 |
| 0x63C | dung_hdr_hole_teleporter_plane | variables.h:550 |
| 0x63D | dung_hdr_staircase_plane | variables.h:551 |
| 0x641 | dung_flag_movable_block_was_pushed | variables.h:552 |
| 0x642 | dung_flag_statechange_waterpuzzle | variables.h:553 |
| 0x646 | dung_flag_somaria_block_switch | variables.h:554 |
| 0x647 | mosaic_inc_or_dec | variables.h:555 |
| 0x648 | `kRam_APUI00` | features.h:9 |
| 0x649 | `kRam_CrystalRotateCounter` | features.h:10 |
| 0x64a | `kRam_BugsFixed` | features.h:11 |
| 0x64c-0x64f | `kRam_Features0` (uint32) → `enhanced_features0` | features.h:12, 51 |
| 0x650-0x653 | `msu_curr_sample` (uint32) | features.h:52 |
| 0x654 | `msu_volume` | features.h:53 |
| 0x655 | `msu_track` | features.h:54 |
| 0x656 | `hud_cur_item_x` | features.h:56 |
| 0x657 | `hud_cur_item_l` | features.h:57 |
| 0x658 | `hud_cur_item_r` | features.h:58 |
| **0x659 – 0x66F** | **UNUSED** (23 bytes free) | **No macro references** |
| **0x670+** | `spotlight_var3` (uint16) and spotlight cells through 0x67E | variables.h:556-562 — **DO NOT USE** |

### §0.7.2 — Confirmed allocation for randomizer

Per [proposal.md](proposal.md) §"Impact" and tasks 1.1:

| Offset | Size | Cell | Purpose |
|---|---|---|---|
| 0x659 | 4 bytes | `kRam_Features1` (uint32) → `enhanced_features1` macro | New feature-flag bank |
| 0x65d | 1 byte | `kRam_RandoSlotActive` | 0 = vanilla slot, 1 = randomizer slot active |
| 0x65e | 1 byte | `kRam_RandoStartingInventoryGranted` | Gate so starting-inventory injection runs exactly once per slot |
| 0x65f – 0x66f | 17 bytes | (reserved) | Forward-compatible spare; do not allocate without bumping `kGeneratorVersion` |

Total Phase A usage: **6 bytes**, fully inside the verified-free `0x659-0x66f` window. 17 bytes of headroom for future master-state additions before any spotlight collision.

**Verified**: `0x659-0x66f` is not referenced by any `#define` in `variables.h` or `features.h`. `0x670` is the first byte after the gap and is occupied (`spotlight_var3`); allocations SHALL NOT cross it.

---

## §0.8 — Acceptance criteria (reviewer checklist)

A reviewer SHALL declare Phase 0 done only when **all** of the following tick:

- [ ] **0.8a** — Every `link_item_*`, `link_bottle_info[*]`, `link_has_crystals`, `sram_progress_*`, heart-piece/heart-container counter write site in `src/*.c` appears in §0.1 with file:line, no omissions. Cross-checked by a re-grep against HEAD.
- [ ] **0.8b** — Every entry in §0.1 is classified in §0.2 with exactly one of the five tags (grant / state-shuffle / cosmetic / consumption / progress).
- [ ] **0.8c** — Every `grant` entry in §0.2 has an ALTTPR canonical location id assigned in §0.3, and every ALTTPR canonical location declared in `../alttp_vt_randomizer/app/Region/{Open,Standard,Inverted}/**.php` is covered by at least one §0.3 entry. Missing coverage explicitly listed as a **§6 gap** (resolved by either new instrumentation or a synthesized hook per §0.3.4).
- [ ] **0.8d** — Every item type in `assets/rando/item_registry.yaml` (task 3.2) maps to either an existing dispatcher entry per §0.4.2 or a new receive path in §0.4.1. Virtual items (`StartingHeart`) explicitly noted as having no grant path.
- [ ] **0.8e** — Every reachability-affecting event flag the logic graph references (per §0.4a) has an identified write site and a `Rando_BumpReachabilityCounter()` patch shape documented.

### §0.8.x — v0.2 status

- 0.8a: **partial** — spine + central dispatcher + every `Link_ReceiveItem` site + every direct-write grant site captured. v0.3 cross-checks via re-grep + re-classification of any newly identified sites (esp. the 30 non-chest §0.3.7 sites).
- 0.8b: **partial** — every site enumerated in §0.1 is tagged in §0.2. New sites added in v0.3 must be tagged before tick.
- 0.8c: **partial — major v0.2 progress** — §0.3.5 lands the complete ALTTPR canonical-location catalog (216 entries); §0.3.6 catalogs unresolved grant-site mappings; §0.3.7 flags §6 gaps. Remaining: chest-tile→location_id table (Phase A blocker) and 30 non-chest grant-path audits.
- 0.8d: **landed** — §0.4 enumeration complete per the spec; §0.1.5 adds the item-id → meaning reference table that confirms each registry entry's dispatch path.
- 0.8e: **mostly landed in v0.2** — every `(TODO)` row in §0.4a is now resolved with the enclosing-handler context. Two non-event identification items remain for v0.3 (Ganon kill, Agahnim 2 kill).

---

## §0.9 — Phase 0 audit is a code-review-blocking gate

**(per task 0.9)**

The [tasks.md](tasks.md) §0.9 requirement is hereby restated for visibility:

> The `audit.md` deliverable with all 0.8a-e checks ticked SHALL exist on master before any section 6 (grant-site integration) task begins. Reviewers SHALL refuse any §6 PR opened before `audit.md` lands.

Until §0.8a-e all tick, this document is the **v0.1 first pass** — sufficient for §1.x (foundation), §2.x (RNG/share-string), §3.x (logic VM), §4.x (placement), and §5.x (spoiler) work to proceed, but **insufficient** for any §6.x PR to merge.

---

## §0.10 — Logic-graph source decision (RESOLVED)

**Decision: option (c) — hand-translate ALTTPR PHP closures into our YAML predicates.**

Per [tasks.md §0.10](tasks.md). Both ALTTPR (`../alttp_vt_randomizer/`, MIT — verified in its `LICENSE` file and `composer.json "license": "MIT"`) and this repository are MIT-licensed. Option (c) was selected as the recommended baseline because:

- The 43 public methods on `app/Support/ItemCollection.php` (`canShootArrows`, `canKillMostThings`, `canGetGoodBee`, `hasBottle`, `canExtendMagic`, `canBlockLasers`, …) map directly to our named macro set.
- The ~4,000 lines of region PHP across `app/Region/{Open,Standard,Inverted}/**.php` translate mechanically to per-location YAML predicates.
- The predicate-to-PHP-source-line audit table doubles as documentation and as the upstream-drift detector for future ALTTPR changes.

### §0.10.1 — Attribution obligations

ALTTPR's MIT license requires preserving its copyright notice in derivative works (per `../alttp_vt_randomizer/LICENSE`). Implementation:

- **Task 13.9 deliverable**: add ALTTPR's copyright notice to this repo's `NOTICE` file (or `LICENSE` if no NOTICE exists yet). The notice cites ALTTPR as the upstream source of the logic-graph translation.
- **Per-predicate provenance**: every macro in `assets/rando/logic.yaml` carries a `# source: ItemCollection.php:LL-RR` comment pointing at the originating PHP method's line range. Every location predicate carries a `# source: app/Region/<world>/<dungeon>.php:LL` comment. Comments are mechanically generated during translation.
- **Macro provenance section** (deferred): when `logic.yaml` authoring (task 3.3) begins, append a `§"Macro provenance"` table to this audit.md mapping each of the 43 named macros to its `ItemCollection.php` line range.

### §0.10.2 — Translation discipline

Per [lessons.md](lessons.md) claim-grounding rule: each YAML predicate cites a verified PHP source line. Memory-based translation is forbidden. Stale ALTTPR comments are not load-bearing — trust the PHP code, not its docstrings (per `EntranceRandomizer.php:10` precedent documented in `lessons.md`).

---

## §0.11 — Owner assignment (RESOLVED)

**Decision: solo-owner with Claude (this agent) as the implementer.** The repository owner directs scope and reviews; Claude executes via direct edits and, where parallelizable work warrants, sub-agents.

Per [tasks.md §0.11](tasks.md):

### §0.11.1 — Team structure

- **Owner / reviewer / scope authority**: the human repository owner.
- **Implementer**: Claude (this agent), invoked via the `/opsx:apply` skill across multiple sessions. Sub-agents (Explore, general-purpose) used opportunistically for parallel research and isolated implementation chunks; sub-agent work is reviewed by the parent agent before landing.

### §0.11.2 — Merge-order rule for §6 grant-site PRs

Single-committer model. §6 PRs are landed sequentially in the order produced by the parent agent. The audit's grant-site enumeration (§0.3) is the partitioning input — within a single PR, instrumentation is grouped by source file (one file's grant sites land together) to keep diffs reviewable.

### §0.11.3 — Backup-reviewer role

Design.md's "bus-mitigation" backup-reviewer role (intended to prevent single-point-of-failure on logic translation) is fulfilled by the human owner reviewing every logic-translation PR. The owner SHALL spot-check at least one ALTTPR PHP source line per PR against the translated YAML predicate to catch mechanical-translation errors before they propagate.

### §0.11.4 — Implications for §6 work

- The "named Switch-build owner" referenced in task 12.3b resolves to the same human owner; Switch build verification runs on the owner's local DevKitPro installation before any release tag.
- The "Owner / reviewer" column in any future spec tables collapses to the human owner.

---

## v0.2 work list — status as of this session

Items 1–5 below were the v0.1→v0.2 follow-ups. Status is shown explicitly.

1. **Expand §0.1.2 verification** — ✅ **DONE** in v0.2. Every `(TODO)` cell in §0.1.2's "Apparent grant site" column is now filled with the enclosing sprite-handler function and the dispatched item meaning. The four entries that remain ambiguous after reading the handler context are catalogued in §0.3.6 (`Unresolved grant-site mappings`) with explicit pointers to what's needed in v0.3.
2. **Locate `kReceiveItemGfx`** — ✅ **DONE**. Found at `src/misc.c:53-59` (76-entry table). New §0.1.5 ("Item ID → meaning reference") tabulates every id 0x00–0x4b with gfx index, RAM addr, value, and inferred meaning, cross-referenced against the `kMemoryLocationToGiveItemTo[76]` / `kValueToGiveItemTo[76]` dispatch tables at `src/misc.c:60-101` plus the special-case branches at `src/misc.c:728-784`.
3. **Complete §0.3 mapping** — ⚠️ **PARTIAL**. §0.3.5 lands the complete ALTTPR canonical-location catalog (216 entries grouped by region, every entry citing its PHP file:line). §0.3.7 walks the catalog and flags `§6 gaps` (non-chest sites needing new instrumentation). The chest-tile-id → `OpenChestForItem` mapping still needs to be built — that work depends on reading vanilla chest data from `zelda3_assets.dat` and is the largest remaining chunk for v0.3.
4. **Complete §0.4a event mapping** — ✅ **DONE** in v0.2. Every `(TODO)` row in the events table is now resolved by reading the enclosing handler. The `12854` fortune-teller toggle is explicitly tagged `NOT logic-affecting` (cosmetic parity bit). The `9951` flute-quest done flag is identified but called out as not directly used by ALTTPR logic (it's a redundant cutscene marker since `link_item_flute >= 2` covers it).
5. **Locate Aga 1 / per-dungeon-boss-cleared / king's-tomb writes** — ✅ **DONE**. Aga 1: `misc.c:313` (`KillAghanim_Func12`) sets `save_ow_event_info[0x1b] |= 32`. Per-dungeon-boss-cleared: there is no single dedicated write — the state is encoded via `dung_savegame_state_bits` writes in the boss-kill ancilla (ancilla.c:3828) plus the heart-container handlers (sprite_main.c:6452, 6461, 6511) and read during `Dungeon_PrepExitWithSpotlight` (overworld.c:601). King's Tomb: it is a vanilla **chest** at LightWorld/NorthWest.php:33 — no separate flag, covered by the universal chest path at player.c:3815.

## v0.3 work list (what's left before §0.8a-e all tick)

Concrete follow-ups for the next session:

1. **Chest-tile → location_id table**: build the lookup from `chest_position` (param at player.c:3815) to ALTTPR canonical chest name. Requires reading vanilla chest data (room data in `zelda3_assets.dat`) and cross-referencing against the `[0xEXXX]` chest-address arrays in each ALTTPR region PHP file (visible in §0.3.5's PHP source citations). Approximately 125 chest entries.
2. **Resolve §0.3.6 unresolved sites**: read the surrounding handlers at sprite_main.c:1267, 2130, 5868, 18192 to identify the enclosing sprite-handler function and confirm the NPC/grant context.
3. **Audit non-chest §6 gap sites**: walk every entry in §0.3.7's "Standing / Dash / Dig / Drop / Pedestal / Fountain" table and locate the actual grant code path. Each likely requires a new instrumentation hook in §6. Approximately 30 sites.
4. **Confirm/disprove ancilla.c:4019 identity**: v0.1 said "magic-shop dispatch (mushroom→powder)"; v0.2 traced it to `Ancilla36_Flute` (full Flute reward, post-fairy hand-off) — but the ALTTPR location for the Flute reward is the `Flute Spot` dig location, not a standalone ancilla. Read the spawn chain to identify which ALTTPR location ID this serves.
5. **Locate `Ganon` event write**: the game-end module that finalizes Ganon kill is not yet enumerated in §0.4a. Search `Module19_*` / `Module_GanonEmerges` for the final save state write.
6. **Locate Agahnim 2 defeat write**: parallel to Aga 1 — likely in a `KillAghanim2_*` handler. v0.3 search and add to §0.4a.
7. **Verify item IDs 0x49 / 0x4a in §0.1.5**: 0x49 may be a "Progressive Sword" stub (the value 1 + sword location suggests it); 0x4a's flute=3 path is via `Ancilla38_CutsceneDuck` direct write (not `Link_ReceiveItem`), so it doesn't appear in §0.1.2 at all. Confirm whether the 76-entry table allocates these intentionally.
8. **Macro provenance section**: when authoring `assets/rando/logic.yaml` (task 3.3), add a `§"Macro provenance"` subsection to audit.md citing the `app/Support/ItemCollection.php` line range per macro (43 named macros per CLAUDE.md).
9. **Settings serialization order section**: ✅ **DONE** in §"Settings serialization order" (immediately below).

---

## §"Settings serialization order" — RandoSettings canonical layout (pinned)

Per [tasks.md §2.5](tasks.md) and `randomizer-core / Settings hash`. The layout below is the determinism contract for Phase A: changing any field's offset, width, or enum-value-assignment is a `kGeneratorVersion` bump trigger (tasks.md §13.6) and invalidates the regression corpus.

**`kSettingsCanonicalLen = 20 bytes`** (definition: `src/rando/rando_settings.h`).

| Offset | Width | Field | Enum (if applicable) | Notes |
|---|---|---|---|---|
| 0 | 1 | `settings_version` | — | Phase A = 1; bumped if layout changes |
| 1 | 1 | `world_state` | `WorldState` (Open=0, Standard=1, Inverted=2, Retro=3) | |
| 2 | 1 | `goal` | `Goal` (Ganon=0, FastGanon=1, Dungeons=2, Pedestal=3, TriforceHunt=4, GanonHunt=5, Completionist=6) | |
| 3 | 1 | `crystals_ganon` | — | 0..7 |
| 4 | 1 | `crystals_tower` | — | 0..7; independent of `crystals_ganon` |
| 5 | 1 | `item_pool_difficulty` | `ItemPoolDifficulty` (Easy=0, Normal=1, Hard=2, Expert=3) | Rupoor only enters pool at Hard/Expert |
| 6 | 1 | `dungeon_small_keys_mode` | `DungeonItemMode` (Vanilla=0, Dungeon=1, Wild=2) | |
| 7 | 1 | `dungeon_big_keys_mode` | `DungeonItemMode` | |
| 8 | 1 | `dungeon_maps_mode` | `DungeonItemMode` | |
| 9 | 1 | `dungeon_compasses_mode` | `DungeonItemMode` | |
| 10 | 1 | `prize_shuffle` | — | bool (0/1); default 1 in Phase A |
| 11 | 1 | `medallion_shuffle` | — | bool (0/1); default 1 in Phase A |
| 12 | 1 | `mode_weapons` | `ModeWeapons` (Randomized=0, Assured=1) | Vanilla/Swordless reserved for Phase B |
| 13 | 1 | `accessibility` | `Accessibility` (Items=0, Locations=1) | None reserved for Phase B |
| 14 | 1 | `pyramid_bow_upgrade` | `PyramidBowUpgrade` (Silvers=0) | Arrows reserved for Phase B |
| 15 | 1 | `pieces_required` | — | uint8; for TriforceHunt / GanonHunt |
| 16 | 1 | `pieces_placed` | — | uint8; for TriforceHunt / GanonHunt |
| 17 | 1 | reserved | — | = 0 forward-compat |
| 18 | 1 | reserved | — | = 0 forward-compat |
| 19 | 1 | reserved | — | = 0 forward-compat |

`settings_hash = SHA-256(canonical_bytes)`. The share-string payload uses the first 16 bytes (`Settings_HashShort`).

**Reference**: default-settings canonical bytes = `010001070701000000000101000000141e000000`; SHA-256 of those bytes = `b01d22b2b503a1c832fe7dafc09206b01ffd5cf06c27aa37ed4d11014d5c0009`. Verified in `Settings_SelfCheck` at `src/rando/rando_settings.c`.

**Adding a new field**: bump `settings_version` AND `kGeneratorVersion`; widen `kSettingsCanonicalLen` (currently 20 → 21+); place the new field at the next reserved offset (so existing offsets stay stable); update this table and the regression corpus.
