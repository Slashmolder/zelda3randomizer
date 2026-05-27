## MODIFIED Requirements

### Requirement: Cosmetic shuffles do not affect logic (Phase D)

Palette, sprite, and music shuffles SHALL be cosmetic-only and SHALL NOT alter the placement table, predicate evaluation, or the settings hash. A separate `cosmetic_seed` setting SHALL drive cosmetic outputs.

**Phase D activation**: three per-axis toggles in the settings struct (`cosmetic_palette`, `cosmetic_sprite`, `cosmetic_music`), each enum-valued per ALTTPR convention. Cosmetic state derives from `cosmetic_seed` (separate uint64 RNG seed, NOT participating in `settings_hash`).

> **Stub status**: per-axis sub-mode enumeration (palette: vanilla/shuffled/negative/blackout; sprite: vanilla / community-pack-id; music: vanilla / shuffled / MSU-pack-driven) deferred to Phase D apply-time.

#### Scenario: Cosmetic shuffle leaves placement untouched
- **WHEN** sprite shuffle is enabled with a fixed cosmetic seed
- **THEN** the placement table is byte-identical to a non-sprite-shuffled seed with the same `share_string`; only the rendered Link sprite differs

#### Scenario: cosmetic_seed is independent of settings_hash
- **WHEN** two seeds have identical `settings` but different `cosmetic_seed`
- **THEN** their `settings_hash` values are identical; their `placement_digest_hex` values are identical; their on-screen presentation differs (palette / sprite / music)

#### Scenario: Tournament cosmetic decoupling
- **WHEN** a tournament distributes one `share_string` to multiple players, each with their own `cosmetic_seed`
- **THEN** every player plays the same placement but with personal cosmetic state; screenshots are visually distinct

#### Scenario: Cosmetic shuffle interacts cleanly with MSU-1
- **WHEN** music shuffle is enabled AND an MSU-1 pack is loaded
- **THEN** music shuffle draws tracks from the MSU-1 pack rather than the SPC engine; no MSU-1 incompatibility
