## ADDED Requirements

### Requirement: Retro TakeAny selection RNG model

The TakeAny activation SHALL be driven by **pick-without-replacement** (ALTTPR `randomCollection` / `array_splice` semantics — NOT take-N-of-a-Fisher-Yates-shuffle) over the static 31-cave candidate list, seeded deterministically from a dedicated RNG forked off the seed (`seed_u64 ^ salt`) so the main fill RNG stream is unperturbed. Selection picks:

1. 4 "potion" caves (each gains `BluePotion@slot0` + `BossHeartContainer@slot1`).
2. A 5th distinct "weapon" cave from the remaining inactive caves (gains `ProgressiveSword`, or `Rupee300` when `mode.weapons ∈ {swordless, vanilla}`).

The fork does NOT reproduce ALTTPR's `mt_rand` byte-for-byte (it pins `xoshiro256**` per `randomizer-core / RNG family`); the determinism guarantee is **self-consistency** — identical `(settings, seed_u64)` yields an identical activated set on every platform. The regular-shop inventory is NOT part of this selection (Slice 3a shipped the 9 regular shops as identity-placed inventory; see `randomizer-placement` §6).

#### Scenario: Same seed produces same activated set
- **WHEN** two generations run with identical `(settings, seed_u64)` against Retro world-state
- **THEN** the 4 potion caves AND the 5th weapon cave are identical between the two runs
- **AND** the placement table is byte-identical regardless of build configuration or platform

#### Scenario: Cross-platform RNG determinism
- **WHEN** the same `(settings, seed_u64)` is generated on Linux, macOS, Windows, and Switch
- **THEN** the activated TakeAny set is identical across all four platforms
