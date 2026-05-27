## Why

**Status**: STUB. Promoted from `add-rando-retro-world-state` (Slice 3a) per its `design.md` §5 scope split (2026-05-27).

Slice 3a shipped the 9 regular shops + Capacity Upgrade randomization with item-registry expansion (7 new IDs) and the picker un-gate. The 22 TakeAny shops were deferred because:

1. **No dispatch infrastructure in the fork**: `app/Shop/TakeAny.php` is literally an empty subclass of Shop. The mechanism that makes a TakeAny "exist in the world" is ALTTPR's `setActive(true)` writing ROM patches — there is no corresponding sprite handler in `src/sprite_main.c`.
2. **RNG-driven activation**: per `app/Randomizer.php:716-735` + `:737-750`, which 4-of-22 TakeAny shops + which 5-of-9 regular shops get extras is chosen per-seed by `randomCollection(4)` / `randomCollection(5)`. The set differs every seed.
3. **kGenVer churn avoided**: adding 22 (Option B) or 44 (Option A) LOC ids in Slice 3a would burn corpus regenerations on unreachable entries.

This change picks up where 3a stopped: lands the TakeAny dispatch infrastructure + 22 LOC ids + RNG-driven `randomCollection` replication.

## What Changes

### Per-seed activation (replicate ALTTPR's RNG model)

Per `app/Randomizer.php:716-734`:
- 4 random TakeAny shops gain `BluePotion@slot0` + `BossHeartContainer@slot1`. The 4-of-22 selection uses `randomCollection(4)`.
- A 5th random TakeAny is set active with `ProgressiveSword` (default) OR `ThreeHundredRupees` (when `mode.weapons == swordless` OR `vanilla`).

Per `app/Randomizer.php:737-750`:
- 5 random non-TakeAny / non-Upgrade shops gain `ShopArrow@slot0` + `ShopKey@slot1` + `TenBombs@slot2`. The 5-of-9 selection uses `randomCollection(5)`.

### Dispatch infrastructure

The fork has no sprite handler for TakeAny entry. **Option A vs B decision (deferred to this change)**:

- **Option A** (static enumeration): add 22 TakeAny × 2 slots = 44 LOC ids; placer pin all 44; runtime sets fixed-slot vanilla items for the selected 4 + 5th.
- **Option B** (active-only): all 22 LOC ids registered with `world_state_filter: [retro]` + a runtime "active" gate; only the 4 + 5th selected emit into the placement pool.

**Recommendation**: pick A or B once dispatch shape is clearer.

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

- `randomizer-core`: MODIFIED `BuildItemPool` Retro branch (add §2(b) TakeAny extras).
- `randomizer-placement`: MODIFIED placer for per-seed TakeAny activation (Option A or B).
- `randomizer-shuffles`: NEW Requirement for the `randomCollection(4) + randomCollection(5)` RNG model.

## Impact

- **Code**: `src/rando/rando_placement.c` (Retro pool §2(b)), `src/sprite_main.c` (TakeAny handler), `assets/rando/location_registry.yaml` (22 or 44 new entries).
- **Effort**: **1 week of focused work** — gated on finding/authoring TakeAny dispatch.
- **Regression risk**: kGeneratorVersion bump; corpus regens. Existing Retro 3a digests will drift (the 4+5 RNG extras change the pool).
- **No dependency on other slices**: 3a is the only prerequisite (landed).

## Status

**Authored as stub** 2026-05-27. Promotion to active-development requires:
1. Sprite-handler discovery for TakeAny entry (the missing infra).
2. Option A vs B decision once dispatch shape is clearer.
