## MODIFIED Requirements

### Requirement: Share-string format

The system SHALL accept and emit share strings as single base32 tokens (RFC 4648 uppercase alphabet, no padding) in two wire formats, both with a 4-byte magic prefix unique to this port, and SHALL dispatch decoding on the decoded **magic bytes** (not on input length):

**v1 (legacy — decoded forever, no longer the exchange emission):**
`magic "ZRSS"[4] | generator_version[1] | settings_hash[16] | seed_u64[8] (LE) | crc16[2] (LE)` = 31 bytes → exactly 50 base32 chars. The CRC is CRC-16-CCITT-FALSE over bytes [0..28]. `settings_hash` is one-way; a v1 string can restore only the seed.

**v2 (the exchange format — emitted by all copy/distribution surfaces):**
`magic "ZRS2"[4] | generator_version[1] | settings_len[1] | settings_canonical[settings_len] | seed_u64[8] (LE) | crc16[2] (LE)`, where `settings_len = kSettingsCanonicalLen` (28) at encode time and `settings_canonical` is the verbatim `Settings_CanonicalSerialize` output. Total `16 + settings_len` bytes → `ceil((16 + settings_len) * 8 / 5)` base32 chars = exactly **71** for the current 28-byte canonical layout. The CRC is CRC-16-CCITT-FALSE over all bytes before it. `settings_hash` is NOT embedded — decoders recompute it from the canonical bytes. A v2 string SHALL fully restore `(settings, seed)`.

Decode rules: after base32 decode, magic `ZRSS` SHALL require exactly 31 bytes and parse as v1; magic `ZRS2` SHALL require exactly `16 + settings_len` bytes and parse as v2. A v2 string whose `settings_len` exceeds the binary's `kSettingsCanonicalLen` SHALL be refused with a distinct "newer version" decode status (no partial application). A v2 string whose `settings_len` is smaller (an older binary's string after a future canonical growth) SHALL zero-extend the canonical tail (zero is the append-only default for later-added axes). Explicit rejects (alttpr.com format, corrupted base32, wrong length, wrong magic, checksum mismatch) SHALL apply to both formats.

The v1 31-byte raw blob SHALL remain the internal **seed identity**: the sidecar slot header `share_string` field, the suppressed-spoiler (ZRSR) share-string field, the spoiler filename and the spoiler JSON `meta.share_string`, the 5-icon visual-hash input, and the race-reveal share-string comparisons SHALL continue to use the v1 form unchanged. Emitting v2 SHALL NOT change placement, `settings_hash`, the race-mode stamp, the sidecar or ZRSR layouts, the regression corpus, or `kGeneratorVersion`.

The headless CLI SHALL emit the v2 string as the distribution artifact: `--out-share-string=<path>` writes the v2 string (single line, no trailing newline) and the `--generate-seed` summary prints both forms; the spoiler JSON's `meta.share_string` stays the v1 identity string. When the settings carry `customizer_active`, all emission surfaces (native-window copy and the CLI) SHALL fall back to the v1 string — customizer placements depend on a local manifest file that no share string can carry until the deferred `customizer_seed` encoding lands (design D5).

#### Scenario: Round-trip encoding (v2)
- **WHEN** a v2 share string is generated from `(settings, seed_u64)` and then parsed
- **THEN** the decoded canonical settings bytes and `seed_u64` exactly match the originals, the recomputed `settings_hash` matches `Settings_HashShort` of the original settings, and the checksum validates

#### Scenario: v1 strings still decode
- **WHEN** a 50-char v1 share string (including one minted by an earlier release) is pasted
- **THEN** it decodes as v1 (seed + settings_hash); the seed is adopted and the settings-mismatch warning path applies — v1 decoding is never removed

#### Scenario: v2 length is exactly 71 chars for the 28-byte canonical layout
- **WHEN** a v2 share string is encoded while `kSettingsCanonicalLen == 28`
- **THEN** the encoded token is exactly 71 base32 chars (44 bytes = 352 bits → 71 chars), and the encoder buffer constant (`kShareStringBase32MaxLen`) accommodates it with a compile-time assert coupling it to `kSettingsCanonicalLen`

#### Scenario: Magic-based dispatch, not length-based
- **WHEN** a token base32-decodes to bytes whose magic is `ZRSS` but whose length is not 31, or whose magic is `ZRS2` but whose length is not `16 + settings_len`
- **THEN** the decoder rejects it (no partial parse); v1 vs v2 is never inferred from string length alone

#### Scenario: Newer-version v2 string is refused, not truncated
- **WHEN** a v2 string carries `settings_len` greater than the binary's `kSettingsCanonicalLen`
- **THEN** the decoder returns the distinct "newer version" reject status and no settings or seed are applied (silently dropping unknown settings axes would reproduce the silent-different-seed failure this format exists to prevent)

#### Scenario: External ALTTPR hash is rejected
- **WHEN** the user enters a hash in the alttpr.com format
- **THEN** the parser detects the absent magic prefix, rejects the input, and displays an error explicitly naming the format mismatch

#### Scenario: Corrupted share string
- **WHEN** the user enters a v1 or v2 share string with an altered character
- **THEN** the parser rejects it with a checksum-failure (or base32/magic) error and does not begin generation

#### Scenario: Identity surfaces are byte-identical
- **WHEN** a seed is generated by a binary with v2 support
- **THEN** the sidecar slot's stored raw blob, the ZRSR file bytes, the spoiler filename, `meta.share_string`, the 5-icon hash, and all race stamps are byte-identical to the pre-v2 binary's output for the same `(settings, seed)` — verified by a corpus run with zero digest changes and no `kGeneratorVersion` bump
