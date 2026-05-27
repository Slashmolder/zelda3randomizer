## ADDED Requirements

### Requirement: cosmetic_seed slot-header field

The sidecar slot header SHALL include a `cosmetic_seed` uint64 LE field, distinct from `share_string`'s embedded `seed_u64`. The Phase A header layout's `reserved[16]` field MAY be repurposed to hold this (decision deferred to Phase D apply-time; the existing reserved bytes are forward-compat-safe).

`cosmetic_seed` SHALL be settable per-slot at slot-creation time and SHALL NOT be re-derived from `share_string`. Two slots with identical `share_string` and different `cosmetic_seed` SHALL produce gameplay-identical seeds with visually-distinct presentation.

The `cosmetic_seed` SHALL NOT participate in `settings_hash` canonical-serialization.

> **Stub status**: exact byte position in the slot header (carve from `reserved[16]` vs. extend the header) deferred to Phase D apply-time after a Phase B + C audit of any TLV chain growth.

#### Scenario: cosmetic_seed round-trips
- **WHEN** a slot is written with `cosmetic_seed = 0xCAFEBABE` and read back
- **THEN** the read-back slot's `cosmetic_seed` is `0xCAFEBABE`

#### Scenario: Sidecar load preserves cosmetic
- **WHEN** the runtime loads a slot
- **THEN** the cosmetic-shuffle module derives palette / sprite / music outputs from the loaded `cosmetic_seed` deterministically

#### Scenario: Older binary ignores cosmetic_seed
- **WHEN** a Phase A or Phase B binary reads a Phase D slot
- **THEN** the binary parses the slot header successfully; the bytes occupied by `cosmetic_seed` are ignored (they live in what Phase A reserved); gameplay proceeds without cosmetic shuffle
