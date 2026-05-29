## ADDED Requirements

### Requirement: Retro TakeAny per-seed activation

In Retro world-state, the generator SHALL replicate ALTTPR's TakeAny activation model (per `app/Randomizer.php:716-735`) over the **31** `Shop\TakeAny` caves (ALTTPR declares 31 in `app/Region/Standard/**`; Inverted declares 0):

1. 4 caves are selected via deterministic pick-without-replacement (see `randomizer-shuffles`) and gain fixed inventory `BluePotion @ slot 0` + `BossHeartContainer @ slot 1`.
2. A 5th distinct cave is selected and gains a single-slot inventory: `ProgressiveSword` by default, OR `Rupee300` when `mode.weapons ∈ {swordless, vanilla}`.
3. The remaining 26 caves are NOT active for this seed (no redirect, no inventory).

Encoding is **Option B (active-only)**: each cave has 2 reserved LOC ids (266..327 = 31 × 2). Only active caves' slots emit into the placement table — 9 per seed (4 potion caves × 2 + 1 weapon cave × 1). Rewards are **role-pinned, not pool-shuffled** (take-any inventory is fixed). The player takes ONE offered item and the cave locks (runtime, see `randomizer-core`).

#### Scenario: Same seed produces same 4 + 5th TakeAny selection
- **WHEN** two `--generate-seed` invocations run with identical `(settings, seed_u64)` against Retro world-state
- **THEN** the 4 BluePotion+BossHeart caves AND the 5th weapon cave are byte-identical between the two runs

#### Scenario: mode.weapons toggles the 5th TakeAny inventory
- **WHEN** a Retro seed with `mode.weapons=randomized` is generated
- **THEN** the 5th active TakeAny carries `ProgressiveSword`
- **AND WHEN** the same seed is regenerated with `mode.weapons=swordless`
- **THEN** the 5th active TakeAny carries `Rupee300` (NOTE: vanilla/swordless weapon modes are reserved/unreachable in current Phase B; this scenario activates once those modes land)

#### Scenario: Inactive TakeAny caves are not in the placement table
- **WHEN** a Retro seed is generated
- **THEN** the placement table contains exactly 9 TakeAny slot entries (4 caves × 2 + the 5th cave × 1), not 62

### Requirement: Retro regular-shop inventory (superseded — identity-placed in Slice 3a)

This change SHALL NOT modify the regular-shop inventory: the 9 regular Retro shops MUST remain identity-placed with their vanilla inventory as shipped by Slice 3a (`add-rando-retro-world-state`), and SHALL NOT be given per-seed `randomCollection(5)` extras. This supersedes the earlier draft of this requirement.

Rationale: ALTTPR's `randomCollection(5)` regular-shop extras (`app/Randomizer.php:737-750`) add `ShopArrow` only under `rom.rupeeBow`, `ShopKey` only under `rom.genericKeys` (both out of scope — see `design.md` §8), and `TenBombs` unconditionally. Slice 3a chose to model the 9 shops as identity-placed vanilla inventory ("the randomization is that the player must find shops + pay rupees, not that shop inventory is shuffled"), which already shipped in the corpus. This change does NOT re-open that decision; it touches TakeAny only. See `design.md` §6.

#### Scenario: Regular Retro shops keep vanilla inventory
- **WHEN** a Retro seed is generated
- **THEN** each of the 9 regular shop slots holds its vanilla item (identity-placed by Slice 3a), and this change adds no regular-shop extras
