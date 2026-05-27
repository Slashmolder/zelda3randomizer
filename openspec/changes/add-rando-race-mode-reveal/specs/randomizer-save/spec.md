## MODIFIED Requirements

### Requirement: Race-mode spoiler suppression (Phase B)

When race mode is enabled, the on-disk spoiler at slot-creation time SHALL contain only the share string, a `generator_version`, and a SHA-256 stamp of the full spoiler that would have been generated with `race_mode` cleared. A reveal action SHALL regenerate the spoiler from the share string and verify the stamp matches.

The on-disk suppressed-file format SHALL be the file format defined in `randomizer-core / Spoiler-log emission` Race-mode suppression section. The format includes a CRC32 to detect file-system corruption or tampering.

The reveal action SHALL be the `Rando_RevealSpoiler(slot_index)` entry point defined in `randomizer-core / Race-mode reveal action`, callable from the file-select UI (see `randomizer-ui / Race-mode reveal UI`) or from the CLI's `--reveal-spoiler=<path>` flag.

#### Scenario: Race-mode file contains only stamp
- **WHEN** a race-mode slot is created
- **THEN** the on-disk file contains the magic header, the generator_version, the SHA-256 stamp, the length-prefixed share-string, and the CRC32 — and nothing else; the total size is bounded to ≤ 78 bytes

#### Scenario: Reveal verifies stamp
- **WHEN** the player triggers reveal for a race-mode slot
- **THEN** the full spoiler is regenerated, written to disk, and its SHA-256 matches the stamp; mismatch is reported as a verification failure and the suppressed file is preserved unchanged

#### Scenario: Sidecar slot survives unchanged across reveal
- **WHEN** the reveal action completes (success or failure)
- **THEN** the sidecar's slot bytes, placement table, and checked-location bitmap are unchanged; reveal touches only files in `<spoiler_dir>`, not in `<saves_dir>`
