## ADDED Requirements

### Requirement: Enemy-shuffle canonical settings axis

The `RandoSettings` struct SHALL gain a boolean axis `enemy_shuffle`. In the canonical serialization (the input to `SHA-256()` for `settings_hash` and to the v2 share-string encoder) it SHALL be packed as canonical byte `[26]` bit0. Byte `[26]` also carries `customizer_active` at bit1 and `traps` in bits2-3; door shuffle owns byte `[27]` bits0-1. `kSettingsCanonicalLen` SHALL stay **28**. No existing field's offset, width, or value changes; no `kSettingsCanonicalLen` size-coupling cascade is triggered.

Because `enemy_shuffle` defaults off (the bit is 0) and draws no fill RNG / adds no predicate:
- **Default-settings seeds keep a byte-identical `settings_hash`** (the canonical bytes are unchanged for the default tuple), and
- **all seeds keep a byte-identical `placement_digest_hex`** (enemy shuffle is orthogonal to item placement).

Adding the axis SHALL still advance `kGeneratorVersion`: it version-locks a new *live runtime* axis (so an older binary surfaces the upgrade warning rather than silently ignoring an enemy-shuffle slot), and a seed with `enemy_shuffle` on serializes a non-zero pad bit, changing *that* seed's `settings_hash`. The corpus regenerates (manifest `generator_version` advances); because no corpus seed enables `enemy_shuffle`, every regenerated digest SHALL be byte-identical to the pre-change baseline.

#### Scenario: Pad-bit packing keeps default hash and all placement identical
- **WHEN** `enemy_shuffle` is added and a default-settings seed is generated on the new binary
- **THEN** the seed's `settings_hash` AND `placement_digest_hex` are byte-identical to the pre-change baseline (the canonical length stays 28 and the default pad bit is 0); the corpus regenerates only its manifest version

#### Scenario: kGeneratorVersion bump + backward load
- **WHEN** the axis is added
- **THEN** `kGeneratorVersion` advances by one and a slot written by the prior version loads on the new binary with the one-time informational warning (per `randomizer-save / upgrade safety`), no regeneration required

#### Scenario: Toggling enemy_shuffle changes only that seed's settings hash
- **WHEN** `enemy_shuffle` is toggled on for a seed
- **THEN** that seed's `settings_hash` changes (the packed pad bit flips to 1) while its `placement_digest_hex` is unchanged from the enemy-shuffle-off seed with the same other axes
