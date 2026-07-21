# randomizer-core — delta for add-rando-ow-warp-shuffle

## ADDED Requirements

### Requirement: Warp axes occupy canonical byte [30] bits 3-5 append-only

The canonical settings encoding SHALL carry `whirlpool_shuffle` in byte [30]
bit 3 and `flute_shuffle` in byte [30] bits 4-5 (0=off, 1=balanced, 2=random;
3 refused), with `kSettingsCanonicalLen` unchanged at 31 and both defaults 0
so default blobs, settings hashes, and share strings are byte-stable across
the change. Deserialization SHALL refuse blobs with any still-undefined [30]
bit set, and the `Settings_SelfCheck` undefined-bit probe — which currently
uses bit 3 — SHALL relocate to a still-undefined bit so the refusal property
remains under test. `apply_derived_rules` normalization (both axes 0 under
Inverted) SHALL apply before hashing, so a share string never encodes an
axis the world state cannot honor.

#### Scenario: Default blob unchanged
- **WHEN** the default settings are canonicalized on this build
- **THEN** the 31-byte blob is byte-identical to the previous build's,
  including byte [30]

#### Scenario: Undefined bits still refused
- **WHEN** a canonical blob arrives with [30] bit 6 set
- **THEN** deserialization refuses it, and `Settings_SelfCheck` proves the
  refusal via the relocated probe

#### Scenario: Flute enum value 3 is refused
- **WHEN** a canonical blob arrives with [30] bits 4-5 both set
- **THEN** deserialization refuses the blob rather than aliasing it to a
  defined mode
