## ADDED Requirements

### Requirement: Retro TakeAny per-seed activation

In Retro world-state, the generator SHALL replicate ALTTPR's TakeAny activation model (per `app/Randomizer.php:716-735`):

1. 4 of the 22 TakeAny shops are selected via Fisher-Yates (deterministic from the seed RNG) and gain fixed inventory `BluePotion @ slot 0` + `BossHeartContainer @ slot 1`.
2. A 5th distinct TakeAny shop is selected and gains a single-slot inventory: `ProgressiveSword` by default, OR `ThreeHundredRupees` when `mode.weapons ∈ {swordless, vanilla}`.
3. The remaining 17 TakeAny shops are NOT active for this seed (player cannot enter / no inventory).

The 5 active TakeAny shops contribute to the placement table per the chosen dispatch model (Option A or Option B — see § "Dispatch model" below). The 17 inactive shops produce no placement entries.

> **Stub status**: dispatch model (Option A static enumeration with 44 LOC ids vs Option B active-only gate with 22 LOC ids) deferred to apply-time once sprite-handler infrastructure is authored. Per the parent change folder's proposal.md §"Dispatch infrastructure".

#### Scenario: Same seed produces same 4 + 5th TakeAny selection
- **WHEN** two `--generate-seed` invocations run with identical `(settings, seed_u64)` against Retro world-state
- **THEN** the 4 BluePotion+BossHeart shops AND the 5th weapon/rupee shop are byte-identical between the two runs

#### Scenario: mode.weapons toggles the 5th TakeAny inventory
- **WHEN** a Retro seed with `mode.weapons=randomized` is generated
- **THEN** the 5th active TakeAny carries `ProgressiveSword`
- **AND WHEN** the same seed is regenerated with `mode.weapons=swordless`
- **THEN** the 5th active TakeAny carries `ThreeHundredRupees`

#### Scenario: Inactive TakeAny shops are not in the placement table
- **WHEN** a Retro seed is generated
- **THEN** the placement table contains exactly 5 TakeAny entries (the 4 + 5th), not 22

### Requirement: Retro regular-shop randomCollection(5) extras

Independently of TakeAny activation, the generator SHALL select 5 of the 9 regular Retro shops (excluding Shop\Upgrade) via the same RNG to gain extra inventory slots (per `app/Randomizer.php:737-750`):

- `ShopArrow @ slot 0` (priced 80 rupees)
- `ShopKey @ slot 1` (priced 100 rupees)
- `TenBombs @ slot 2` (priced 50 rupees)

The remaining 4 regular shops keep their vanilla inventory.

#### Scenario: 5 of 9 regular shops gain randomCollection extras
- **WHEN** a Retro seed is generated
- **THEN** exactly 5 of the 9 non-Upgrade, non-TakeAny shops carry the `ShopArrow + ShopKey + TenBombs` slot triple
- **AND** the placement table emits 15 entries for those slots (5 × 3 slots)
