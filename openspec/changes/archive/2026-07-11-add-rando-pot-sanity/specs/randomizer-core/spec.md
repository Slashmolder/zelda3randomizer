## ADDED Requirements

### Requirement: Pot-shuffle canonical settings axis

The `RandoSettings` struct SHALL gain an enum axis `pot_shuffle` with values
`Off` (0), `Keys` (1), `Contents` (2), `All` (3), defaulting to `Off`. In the
canonical serialization (the input to `SHA-256()` for `settings_hash` and to the
v2 share-string encoder) it SHALL be packed as a **3-bit field split
non-contiguously** — canonical byte `[26]` bits 6-7 (the low 2 bits) plus `[27]`
bit 7 (the high bit), the LAST free bits at length 28. (Three bits, not two,
because a 4th tier `Subset` (4) is reserved for a later phase; `[27]` bits 2-3 are
NOT free — `trap_categories` owns them.) `kSettingsCanonicalLen` SHALL stay
**28**; no existing field's offset, width, or value changes and no size-coupling
cascade is triggered (`canonical-size-coupling`).

Because `pot_shuffle` defaults to `Off` (all three bits 0) and the off path draws
no fill RNG and adds no active locations:
- **Default-settings seeds keep a byte-identical `settings_hash`** (the canonical
  bytes are unchanged for the default tuple), and
- **all existing seeds keep a byte-identical `placement_digest_hex`** (pot
  locations are excluded from the active graph and the digest when off; see
  `randomizer-pot-sanity / Pot-shuffle Off is placement-byte-identical`).

Adding the axis SHALL advance `kGeneratorVersion`: it version-locks a new
live-runtime axis (an older binary surfaces the upgrade warning rather than
silently mis-handling a pot-shuffle slot), and a seed with `pot_shuffle != Off`
serializes non-zero bits, changing that seed's `settings_hash`. The corpus
regenerates (manifest `generator_version` advances); because no existing corpus
seed enables `pot_shuffle`, every regenerated digest SHALL be byte-identical to
the pre-change baseline, validated by a 3-way diff against `main`.

The `All` tier introduces a **Literally Nothing** filler item (`ITEM_Nothing`,
appended to `item_registry.yaml`) placed at empty-pot checks via a **dedicated
pre-pass** (NOT the junk rotation) so it can never land on a real location; its
behavior is specified normatively in `randomizer-pot-sanity / Literally Nothing
filler for empty pots`.

#### Scenario: Default hash and all placement identical with pot-shuffle off
- **WHEN** `pot_shuffle` is added and a default-settings seed is generated on the
  new binary
- **THEN** the seed's `settings_hash` AND `placement_digest_hex` are byte-identical
  to the pre-change baseline (canonical length stays 28, the default bits are 0);
  the corpus regenerates only its manifest version

#### Scenario: kGeneratorVersion bump + backward load
- **WHEN** the axis is added
- **THEN** `kGeneratorVersion` advances and a slot written by the prior version
  loads on the new binary with the one-time informational warning (per
  `randomizer-save / Embedded placement table — upgrade safety`)

#### Scenario: Selecting a tier changes only that seed's settings hash
- **WHEN** `pot_shuffle` is set to `Contents` for a seed
- **THEN** that seed's `settings_hash` changes (the packed bits become non-zero)
  while a pot-shuffle-`Off` seed with the same other axes keeps the baseline hash
