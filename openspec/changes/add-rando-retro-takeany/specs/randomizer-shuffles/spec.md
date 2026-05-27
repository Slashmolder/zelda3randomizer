## ADDED Requirements

### Requirement: Retro TakeAny randomCollection RNG model

The TakeAny + regular-shop random selection SHALL be driven by a Fisher-Yates shuffle over a static shop-id list, seeded deterministically from the Retro-mode seed RNG. The shuffle SHALL be invoked exactly twice per generation:

1. Once over the 22 TakeAny shop ids; the first 5 picks become the activated set (first 4 → BluePotion+BossHeart pair; 5th → ProgressiveSword/ThreeHundredRupees).
2. Once over the 9 regular non-Upgrade shop ids; the first 5 picks become the randomCollection(5) extras recipients.

The two shuffles SHALL consume RNG state in a fixed order (TakeAny first, then regular shops) so the placement is reproducible.

> **Stub status**: exact RNG sub-state mixing (whether TakeAny selection uses a sub-RNG forked from the seed or the main RNG inline) deferred to apply-time. ALTTPR uses `randomCollection` against `mt_rand`-driven shuffles; this fork's reimplementation pins to `xoshiro256**` per `randomizer-core / RNG family` and must keep cross-platform determinism.

#### Scenario: TakeAny shuffle precedes regular-shop shuffle
- **WHEN** a Retro seed is generated
- **THEN** the RNG calls for TakeAny selection complete before the first RNG call for regular-shop selection
- **AND** the placement table is byte-identical to another seed with the same `(settings, seed_u64)` regardless of build configuration or platform

#### Scenario: Cross-platform RNG determinism
- **WHEN** the same `(settings, seed_u64)` is generated on Linux, macOS, Windows, and Switch
- **THEN** the activated TakeAny set + regular-shop extras set are identical across all four platforms
