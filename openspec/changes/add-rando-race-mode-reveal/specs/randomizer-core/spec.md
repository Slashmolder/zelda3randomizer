## MODIFIED Requirements

### Requirement: Spoiler-log emission

The generator SHALL emit a spoiler log in two forms: a human-readable text file grouped by region, and a machine-readable JSON file with stable field names suitable for parsing by external tools. Spoiler files SHALL be written to a configurable directory keyed by share string. The JSON schema SHALL define `fallback_warnings` as an array of objects each with `code` (string enum, e.g., `"forward_fill_fallback"`, `"rewind_budget_exceeded_recovered"`) and `detail` (human-readable string).

**Race-mode suppression**: when `race_mode == 1` in the settings serialization, the generator SHALL NOT emit the full JSON or text spoilers at generation time. Instead, the generator SHALL emit a single suppressed-spoiler file at `<spoiler_dir>/<share_string>.json` containing exactly:
- 4-byte magic header `ZRSR` (Zelda Rando Spoiler Race — distinct from the full-spoiler form which is parseable JSON without magic).
- 2-byte `generator_version` (LE).
- 32-byte SHA-256 `spoiler_stamp` of the full-spoiler JSON the generator *would have emitted* with `race_mode` cleared in the canonical settings object (the stamp is over placement, not over the race-mode flag).
- 4-byte length-prefix + UTF-8 `share_string` for round-trip convenience.
- 4-byte CRC32 over the previous bytes for tamper detection.

The total file size SHALL be 4 + 2 + 32 + 4 + N + 4 bytes (where N is the share-string length, ≤ 32 bytes). No `.txt` text-spoiler companion is emitted.

#### Scenario: Both JSON and text spoilers are emitted (non-race seed)
- **WHEN** a seed generates successfully with `race_mode == 0`
- **THEN** both `<spoiler_dir>/<share_string>.json` and `<spoiler_dir>/<share_string>.txt` are written to the configured spoiler directory

#### Scenario: Race-mode suppression writes only the stamp file
- **WHEN** a seed generates successfully with `race_mode == 1`
- **THEN** only the suppressed-spoiler file at `<spoiler_dir>/<share_string>.json` is written; no `.txt` companion is emitted; the file is at most 78 bytes (4-byte magic + 2-byte version + 32-byte stamp + 4-byte share-string-length + 32-byte share-string + 4-byte CRC32)

#### Scenario: Stamp algorithm is canonical
- **WHEN** the same `race_mode == 1` seed is generated twice on different platforms (Linux, macOS, Windows, Switch)
- **THEN** the resulting `spoiler_stamp` bytes are byte-identical — the canonical JSON serialization SHALL be deterministic (sorted keys, no trailing whitespace, normalized number representation per `randomizer-core / Settings canonical serialization order`)

#### Scenario: fallback_warnings records forward-fill fallback
- **WHEN** the forward-fill fallback fires (per `Forward-fill fallback after timeout` in the assumed-fill requirement above)
- **THEN** the JSON spoiler's `fallback_warnings` array contains an object whose `code` field equals `"forward_fill_fallback"` and whose `detail` field is a human-readable string explaining the fallback

#### Scenario: Text spoiler is grouped by region
- **WHEN** the text spoiler file is read
- **THEN** placements are organised into one section per region (one region heading followed by that region's location/item lines), making the file scannable without external tooling

#### Scenario: Race-mode fallback_warnings still stamped
- **WHEN** a race-mode seed generates and a forward-fill fallback fires
- **THEN** the stamp covers a full-spoiler form that includes the `fallback_warnings` entry; reveal will surface the warning to the player

## ADDED Requirements

### Requirement: Race-mode reveal action

The randomizer SHALL expose `Rando_RevealSpoiler(slot_index)` that:

1. Locates the suppressed-spoiler file at `<spoiler_dir>/<share_string>.json` using the slot's share string.
2. Parses the file's magic + version + stamp + share-string + CRC32; rejects on CRC mismatch.
3. Verifies the parsed share-string matches the slot's share-string.
4. Regenerates the full spoiler in-memory using the same placement pipeline the CLI generator uses (`--generate-seed --settings=... --seed=...`), with `race_mode == 0` substituted in the in-memory settings copy for stamp recomputation.
5. Computes SHA-256 of the regenerated full-spoiler JSON canonical form.
6. **If the computed stamp matches the stored stamp**: overwrites the suppressed file with the full JSON; also writes the `.txt` companion; returns success.
7. **If the computed stamp does NOT match**: leaves the suppressed file unmodified; returns a `kRandoReveal_StampMismatch` error.
8. **If the slot's `generator_version` differs from the runtime's**: returns `kRandoReveal_VersionMismatch` and does NOT attempt regeneration (cross-version reveal is not supported in Phase B; a future change can refine this).

#### Scenario: Reveal of a freshly-generated seed succeeds
- **WHEN** a race-mode seed is generated and `Rando_RevealSpoiler` is invoked immediately afterward on the same binary
- **THEN** the suppressed file at `<spoiler_dir>/<share_string>.json` is overwritten with the full JSON spoiler; the `.txt` companion is created; the action returns success

#### Scenario: Reveal of a tampered suppressed file fails closed
- **WHEN** an attacker modifies the on-disk suppressed file's stored stamp byte and `Rando_RevealSpoiler` is invoked
- **THEN** the CRC32 check fails before regeneration begins; the action returns `kRandoReveal_CrcMismatch` and the file is unchanged

#### Scenario: Reveal with mismatched share-string fails
- **WHEN** the slot's share-string differs from the suppressed file's stored share-string (e.g., user copy-renamed the file)
- **THEN** the action returns `kRandoReveal_ShareStringMismatch` without regeneration

#### Scenario: Reveal across binary versions refuses
- **WHEN** the suppressed file's `generator_version` differs from the runtime's
- **THEN** the action returns `kRandoReveal_VersionMismatch`; the player is advised to use a binary matching the stored version

#### Scenario: CLI counterpart `--reveal-spoiler`
- **WHEN** the CLI is invoked as `./zelda3 --reveal-spoiler=<path-to-suppressed-file>`
- **THEN** the process runs the reveal action against the supplied path and exits zero on success / non-zero on any failure (with the specific failure code printed to stderr)
