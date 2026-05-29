## Why

**Status**: IMPLEMENTED 2026-05-28 (was STUB). Promoted from `add-rando-retro-world-state` (Slice 3a) per its `design.md` §5 scope split. See `design.md` for the grounded implementation.

Slice 3a shipped the 9 regular shops + Capacity Upgrade randomization with item-registry expansion (7 new IDs) and the picker un-gate. The **31** TakeAny caves (ALTTPR declares 31 in `app/Region/Standard/**`; the original stub said "22" — corrected) were deferred because:

1. **No dispatch infrastructure in the fork**: `app/Shop/TakeAny.php` is an empty subclass of Shop; the mechanism is ALTTPR's `setActive(true)` ROM-patching the overworld-door redirect table. The fork has no take-any runtime — the 31 cave entrances load vanilla fairy ponds / fortune tellers. This change BUILT that runtime (per-seed overworld-door redirect → take-any host room + free-grant presentation).
2. **RNG-driven activation**: per `app/Randomizer.php:716-735`, which 5-of-31 TakeAny caves get activated is chosen per-seed. The set differs every seed.
3. **kGenVer churn avoided**: adding LOC ids in Slice 3a would have burned corpus regenerations on unreachable entries.

This change lands the TakeAny dispatch infrastructure + 62 LOC ids (31 caves × 2 slots) + the deterministic selection.

## What Changes

### Per-seed activation (replicate ALTTPR's RNG model)

Per `app/Randomizer.php:716-734`, in Retro:
- 4 random TakeAny caves gain `BluePotion@slot0` + `BossHeartContainer@slot1`. The 4-of-31 selection uses `randomCollection(4)` (pick-without-replacement).
- A 5th distinct TakeAny cave gains `ProgressiveSword` (default) OR `ThreeHundredRupees` (when `mode.weapons ∈ {swordless, vanilla}`), via `->random()`. (mode.weapons vanilla/swordless are reserved/unreachable today, so the 5th is currently always ProgressiveSword.)
- The player takes ONE of the two offered items; the cave then locks (asm `ShopState |= $07`).

The regular-shop `randomCollection(5)` extras described in earlier drafts are **out of scope here** — Slice 3a already shipped the 9 regular shops as identity-placed inventory (not per-seed extras); this change touches TakeAny only. See design.md §6.

### Dispatch infrastructure (BUILT)

Option B (active-only) chosen. All 62 LOC ids carry `world_state_filter: [retro]`; only the ~9 active caves' slots (4×2 + 1) emit into the placement table per seed. Runtime: `Overworld_UseEntrance` redirects an active cave's overworld door to its take-any host room (0x112/0x10F/0x11F) and captures the source door; `SpritePrep_Shopkeeper` presents the cave's offered items and suppresses the host room's regular shop; `ShopItem_TakeAny` grants free and locks the cave.

### Sprite handler

Locate (or author) a TakeAny-cave sprite handler. Apply-time grep:
- `grep -rn "TakeAny\|take_any" src/sprite_main.c`
- `grep -n "shop_indicator\|takeAnys" src/` for any existing flag-based gate.

### Item references

The 7 item-registry IDs needed by TakeAny are **already in the registry** post-3a (`56ac308`):
- `BluePotion` (id 126), `BossHeartContainer` (existing), `ProgressiveSword` (existing), `ShopArrow` (alias to Arrow1), `ShopKey` (= GenericKey id 125), `TenBombs` (= Bombs10), `ThreeHundredRupees` (existing).

Slice 3b just consumes them.

## Capabilities

### Modified Capabilities

- `randomizer-placement`: MODIFIED placer for per-seed TakeAny activation (Option B — active-only, see design.md §D4). Rewards are **role-pinned, NOT pool-shuffled** (the earlier "add §2(b) TakeAny extras to `BuildItemPool`" idea was dropped — take-any inventory is fixed in ALTTPR, design.md §D3), so `BuildItemPool` gains no take-any item additions.
- `randomizer-shuffles`: NEW Requirement for the pick-without-replacement selection RNG model (4 potion caves via randomCollection + 1 weapon cave via random()).

## Impact

- **Code**: `src/rando/rando_placement.c` (selection + active-only pin), `src/rando/rando.c` (runtime cave table + dispatch), `src/overworld.c` (entrance redirect), `src/sprite_main.c` (host-room presentation + `ShopItem_TakeAny`), `assets/rando/location_registry.yaml` (**62** new entries = 31 caves × 2 slots), `assets/rando_logic_gen.py` + `logic.schema.yaml` (LOCTYPE_TakeAny).
- **Regression risk**: kGeneratorVersion 35→36; corpus regenerated (3 Retro digests; 52 non-Retro byte-identical). Existing Retro 3a placements stay decision-stable (take-anys append as new LOCs).
- **No dependency on other slices**: 3a is the only prerequisite (landed).

## Status

**Implemented** 2026-05-28 (full subsystem: generator + runtime). Runtime entrance-redirect + presentation pending in-game playtest confirmation (R1). The original stub (2026-05-27) deferred on TakeAny dispatch infra + the Option A/B choice; both resolved here (the fork has no take-any runtime — it was built: per-seed overworld-door redirect to a take-any host room + a free-grant `ShopItem_TakeAny`; Option B active-only).
