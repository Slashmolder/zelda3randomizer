# add-rando-retro-world-state — audit

Provenance + grounding notes. Every shop / item / line citation below is grounded
against the sibling `../alttp_vt_randomizer/` checkout (license MIT) and this
repo's `assets/rando/{location,item}_registry.yaml`. Per `CLAUDE.md`
claim-grounding discipline: source over memory.

---

## Retro shop provenance

**Tasks 1.1–1.4 deliverable.** Enumerate the ALTTPR shop entities, classify them,
record each purchasable slot's item + price with PHP source-line citation, and
cross-check against the location/item registries for collisions.

### 1.1 — Shop entity census + classification

All shop entities are constructed in `app/Region/Standard/**/*.php` via
`new Shop(...)`, `new Shop\Upgrade(...)`, or `new Shop\TakeAny(...)`. Constructor
signature (`app/Shop.php:38`):
`__construct(string $name, int $config, int $shopkeeper, int $room_id, int $door_id, Region $region, array $writes = [])`.

**Census (grounded count):**

| Class | Count | Slice |
|---|---|---|
| `Shop` (regular) | 9 | 3a |
| `Shop\Upgrade` | 1 (Capacity Upgrade) | 3a |
| `Shop\TakeAny` | **31** | 3b (`add-rando-retro-takeany`) |
| **Total** | **41** | |

> **Correction vs spec estimate.** The change index and `add-rando-retro-takeany`
> proposal say "**22** TakeAny". The actual grounded count is **31** distinct
> `new Shop\TakeAny(...)` constructions (enumerated in §"TakeAny census" below).
> The "22" appears to be a stale estimate; 3b must size its dispatch table for 31,
> not 22. (Total entities = 41, also slightly under the index's "42".)

**The 9 regular `Shop` entities (slice 3a):**

| # | Shop name | file:line (constructor) | door_id |
|---|---|---|---|
| 1 | Light World Kakariko Shop | `app/Region/Standard/LightWorld/NorthWest.php:57` | 0x46 |
| 2 | Light World Death Mountain Shop | `app/Region/Standard/LightWorld/DeathMountain/East.php:45` | 0x00 |
| 3 | Light World Lake Hylia Shop | `app/Region/Standard/LightWorld/South.php:54` | 0x58 |
| 4 | Dark World Death Mountain Shop | `app/Region/Standard/DarkWorld/DeathMountain/East.php:41` | 0x6E |
| 5 | Dark World Potion Shop | `app/Region/Standard/DarkWorld/NorthEast.php:45` | 0x6F |
| 6 | Dark World Forest Shop | `app/Region/Standard/DarkWorld/NorthWest.php:42` | 0x75 |
| 7 | Dark World Lumberjack Hut Shop | `app/Region/Standard/DarkWorld/NorthWest.php:43` | 0x57 |
| 8 | Dark World Outcasts Shop | `app/Region/Standard/DarkWorld/NorthWest.php:44` | 0x60 |
| 9 | Dark World Lake Hylia Shop | `app/Region/Standard/DarkWorld/South.php:42` | 0x74 |

**The 1 `Shop\Upgrade` entity (slice 3a):**

| Shop name | file:line | door_id |
|---|---|---|
| Capacity Upgrade | `app/Region/Standard/LightWorld/South.php:55` | 0x5D |

### 1.2 — Purchasable inventory per shop (slice 3a: regular + Upgrade)

Inventory is attached via `->clearInventory()->addInventory(slot, Item::get(name), price[, max])`.
All 9 regular shops have exactly 3 slots; Capacity Upgrade has 2. **Total = 29
purchasable slots** (27 regular + 2 upgrade), within the task's "~30-40" estimate
(the remaining slots are in the 3b TakeAny caves).

| Shop | slot 0 | slot 1 | slot 2 | inventory file:line |
|---|---|---|---|---|
| Light World Kakariko | RedPotion @150 | Heart @10 | TenBombs @50 | `LightWorld/NorthWest.php:69-71` |
| Light World Death Mountain | RedPotion @150 | Heart @10 | TenBombs @50 | `LightWorld/DeathMountain/East.php:51-53` |
| Light World Lake Hylia | RedPotion @150 | Heart @10 | TenBombs @50 | `LightWorld/South.php:73-75` |
| Dark World Death Mountain | RedPotion @150 | Heart @10 | TenBombs @50 | `DarkWorld/DeathMountain/East.php:45-47` |
| Dark World Potion | RedPotion @150 | BlueShield @50 | TenBombs @50 | `DarkWorld/NorthEast.php:53-55` |
| Dark World Forest | RedShield @500 | Bee @10 | TenArrows @30 | `DarkWorld/NorthWest.php:51-53` |
| Dark World Lumberjack Hut | RedPotion @150 | BlueShield @50 | TenBombs @50 | `DarkWorld/NorthWest.php:55-57` |
| Dark World Outcasts | RedPotion @150 | BlueShield @50 | TenBombs @50 | `DarkWorld/NorthWest.php:59-61` |
| Dark World Lake Hylia | RedPotion @150 | BlueShield @50 | TenBombs @50 | `DarkWorld/South.php:52-54` |
| Capacity Upgrade | BombUpgrade5 @100 (max 7) | ArrowUpgrade5 @100 (max 7) | — | `LightWorld/South.php:69-70` |

**ALTTPR → zelda3-fork item-name aliases** (ROM byte equality; see
`item_registry.yaml` Slice-3a block, ids 125-131):

| ALTTPR `Item::get` name | zelda3-fork registry item | note |
|---|---|---|
| RedPotion | `RedPotion` (id 127, ROM 0x2E) | unbottled shop variant |
| Heart | `HeartRefill` (id 131, ROM 0x42) | transient heal |
| TenBombs | `Bombs10` (id 109, ROM 0x28) | existing Phase A id |
| TenArrows | `Arrow10` (id 106, ROM 0x44) | existing Phase A id |
| BlueShield | `FighterShield` (id 9, ROM 0x04) | same ROM byte |
| RedShield | `RedShield` (id 10) | existing Phase A id |
| Bee | `BeeContents` (id 128, ROM 0x0E) | unbottled, distinct from BottleWithBee |
| BombUpgrade5 | `BombUpgrade5` (id 129, ROM 0x51) | Slice-3a addition |
| ArrowUpgrade5 | `ArrowUpgrade5` (id 130, ROM 0x52) | Slice-3a addition |

All 9 distinct items already exist in `item_registry.yaml` (no new item ids are
needed beyond the Slice-3a 125-131 additions already landed).

### 1.4 — Registry collision cross-check

**Result: NO collisions. Already encoded and cross-validated.** The location
registry already contains the 29 Slice-3a shop slots as an append-only block,
ids **237–265** (`assets/rando/location_registry.yaml:374-443`), each carrying a
`world_state_filter: [retro]` and a PHP `source:` citation. I independently
re-derived all 29 slots from the PHP above and they match the registry
**exactly** — item mapping, source line, and price-bearing slot order all agree.
Notable confirmations:

- `238` Dark World Potion Shop - 1 → `FighterShield` with the note "BlueShield in
  ALTTPR == FighterShield (ROM 0x04)" — matches `NorthEast.php:54`.
- `253/256/259/262` use `HeartRefill` (the DM/Kakariko/Lake-Hylia `Heart` slots);
  `239..` use `Bombs10` for `TenBombs`. All consistent.
- `264/265` Capacity Upgrade are `type: ShopUpgrade`, identity-pinned by the
  placer (not by a YAML flag) — matches `LightWorld/South.php:69-70`.

Ids 237–265 are a contiguous appended block above the prior max (`Magic Bat` id
167 region); no overlap with any existing `LOC_*`. Open/Standard/Inverted digests
are unaffected because every entry is `world_state_filter: [retro]`.

### TakeAny census (31 — provenance for slice 3b `add-rando-retro-takeany`)

Recorded here so 3b sizes its dispatch table correctly. Format: name — file:line — door_id.

Dark World (13):
- Dark Death Mountain Fairy — `DarkWorld/DeathMountain/West.php:36` — 0x70
- Dark Desert Fairy — `DarkWorld/Mire.php:37` — 0x56
- Dark Desert Hint — `DarkWorld/Mire.php:38` — 0x62
- Dark Lake Hylia Fairy — `DarkWorld/NorthEast.php:47` — 0x6D
- East Dark World Hint — `DarkWorld/NorthEast.php:48` — 0x69
- Palace of Darkness Hint — `DarkWorld/NorthEast.php:49` — 0x68
- Dark Sanctuary Hint — `DarkWorld/NorthWest.php:46` — 0x5A
- Fortune Teller (Dark) — `DarkWorld/NorthWest.php:47` — 0x66
- Archery Game — `DarkWorld/South.php:44` — 0x59
- Bonk Fairy (Dark) — `DarkWorld/South.php:45` — 0x78
- Dark Lake Hylia Ledge Fairy — `DarkWorld/South.php:46` — 0x81
- Dark Lake Hylia Ledge Hint — `DarkWorld/South.php:47` — 0x6A
- Dark Lake Hylia Ledge Spike Cave — `DarkWorld/South.php:48` — 0x7C

Light World (18):
- Hookshot Fairy — `LightWorld/DeathMountain/East.php:47` — 0x50
- Long Fairy Cave — `LightWorld/NorthEast.php:44` — 0x55
- Lake Hylia Fairy — `LightWorld/NorthEast.php:45` — 0x5E
- Fortune Teller (Light) — `LightWorld/NorthWest.php:59` — 0x65
- Bush Covered House — `LightWorld/NorthWest.php:60` — 0x44
- Lost Woods Gamble — `LightWorld/NorthWest.php:61` — 0x3C
- Lumberjack House — `LightWorld/NorthWest.php:62` — 0x76
- Snitch Lady East — `LightWorld/NorthWest.php:63` — 0x3E
- Snitch Lady West — `LightWorld/NorthWest.php:64` — 0x3F
- Bomb Hut — `LightWorld/NorthWest.php:65` — 0x4A
- 20 Rupee Cave — `LightWorld/South.php:57` — 0x7B
- 50 Rupee Cave — `LightWorld/South.php:58` — 0x79
- Bonk Fairy (Light) — `LightWorld/South.php:59` — 0x77
- Desert Fairy — `LightWorld/South.php:60` — 0x72
- Good Bee Cave — `LightWorld/South.php:61` — 0x6B
- Lake Hylia Fortune Teller — `LightWorld/South.php:62` — 0x73
- Light Hype Fairy — `LightWorld/South.php:63` — 0x6C
- Kakariko Gamble Game — `LightWorld/South.php:64` — 0x67

Note: TakeAny "writes" arrays encode the ROM patch (`[addr => [byte]]`), e.g.
`Dark Death Mountain Fairy` writes `[0xDBBE2 => [0x58]]`. Several TakeAny entries
(Fortune Tellers, Gamble/Archery games, Hint caves) are NOT item-bearing shops in
the Retro sense — 3b should decide which subset becomes placement locations vs.
pure activation caves. (This is the likely origin of a "22" item-bearing subset
vs 31 total `Shop\TakeAny` — confirm at 3b authoring.)
