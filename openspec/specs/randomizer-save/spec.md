# randomizer-save Specification

## Purpose
TBD - created by archiving change add-randomizer-support. Update Purpose after archive.
## Requirements
### Requirement: Sidecar file model

Randomizer state SHALL be stored in `saves/sram_rando.dat` parallel to `saves/sram.dat`. The existing 8 KB `sram.dat` (3 slots at offsets {0, 0x500, 0xa00} with backups at {0xf00, 0x1400, 0x1900}) SHALL NOT be modified. All multi-byte fields in the sidecar SHALL be little-endian on disk (per `randomizer-core / Byte-order pin`).

#### Scenario: Vanilla file untouched
- **WHEN** a vanilla save is read or written
- **THEN** `sram.dat` bytes are identical to the pre-change build

#### Scenario: Absent sidecar is normal vanilla
- **WHEN** `sram_rando.dat` does not exist
- **THEN** all three slots load as vanilla and the file-select screen shows no rando metadata

### Requirement: Sidecar slot contents

The sidecar SHALL contain a **16-byte file header** followed by three slot regions (one per `sram.dat` slot, in the same order).

**File header (16 bytes, byte-counted)**:

| Offset | Width | Field | Notes |
|---:|---:|---|---|
| 0 | 4 | `magic[4]` | distinct from slot magic |
| 4 | 2 | `format_version` (LE) | Phase A = 1 |
| 6 | 2 | `slot_count` (LE) | Phase A = 3 |
| 8 | 4 | `file_crc` (LE) | CRC32 over the rest of the file |
| 12 | 4 | `reserved[4]` | zero-initialized; forward-compat |

**Slot region** = slot header (80 bytes) + placement table + checked-location bitmap.

**Slot header (80 bytes total, byte-counted)**:

| Offset | Width | Field | Notes |
|---:|---:|---|---|
| 0 | 4 | `magic[4]` | distinct from file magic |
| 4 | 1 | `slot_kind` | Empty=0 / Vanilla=1 / Randomizer=2 |
| 5 | 2 | `generator_version` (LE) | |
| 7 | 16 | `settings_hash[16]` | first 16 bytes of SHA-256 of canonical settings serialization (per `randomizer-core / Settings canonical serialization order`) |
| 23 | 32 | `share_string[32]` | raw binary form: `magic[4] \| version[2] \| settings_hash[16] \| seed_u64[8] \| checksum[2]` |
| 55 | 2 | `last_vanilla_write_version` (LE) | the `generator_version` under which the paired `sram.dat` slot was last consistently written; SHALL be set to the current `generator_version` on every sidecar write |
| 57 | 4 | `sram_slot_checksum_at_last_write` (LE) | checksum of the paired `sram.dat` slot snapshotted at the moment of sidecar write. Algorithm mirrors `src/messaging.c::SaveGameFile` for drift-detection compatibility with the vanilla writer: `t = 0x5a5a; for (i = 0; i < 0x4fe; i += 2) t -= u16le_at(slot_bytes + i); return (uint32)t;` (high 16 bits of the stored u32 are always zero). NOT CRC32 — the spec field is named for forward compatibility with a wider digest, but Phase A must match the vanilla per-slot routine. |
| 61 | 2 | `placement_table_size` (LE) | bytes; REQUIRED for cross-version forward compatibility |
| 63 | 1 | `flags` | bit 0 = forward-fill fallback was used; bits 1..7 reserved |
| 64 | 16 | `reserved[16]` | zero-initialized; forward-compat |

**Embedded placement table**: `placement_table_size` bytes — exact value stored per slot. The Phase A baseline location count is ~212 (Open / Standard / Inverted; counted from `app/Region/Standard/` location definitions). `Retro` world-state adds shop locations to the pool, inflating the table for those slots. The slot's `placement_table_size` field is therefore authoritative — the loader reads exactly that many bytes per slot, not a hardcoded constant. **Item-ID `0xFFFF` is reserved as the "no placement / deprecated location" sentinel** — the dispatcher treats sentinel entries as "no rando substitute" and falls back to `vanilla_item_id` (per `randomizer-placement / Dispatcher signature and fall-back behavior`).

**Checked-location bitmap**: `(placement_table_size / 2 + 7) >> 3` bytes; iterated only over `[0, placement_table_size / 2)` bits.

**Forward-compat reserve (Phase C foresight)**: Phase C (entrance shuffle) WILL need to persist a per-seed entrance-map. Phase C SHALL append a TLV chain after the bitmap with the same shape as the snapshot tail TLV (`magic[8] + type[4] + length[4] + payload`). Older binaries reading a Phase C slot SHALL ignore unknown TLVs (read length, seek past). This is forward-looking; Phase A's slot body has no TLV chain.

The spoiler file path SHALL NOT be stored in the slot. The runtime derives it from `[randomizer] spoiler_dir` config + the slot's share string — this is a deliberate decision so slots are invariant under config changes.

#### Scenario: Round-trip
- **WHEN** a randomizer slot is written and immediately read back
- **THEN** every header field, the full placement table, and the checked-location bitmap round-trip without change

#### Scenario: Slot header byte layout is exact
- **WHEN** a slot header is serialized
- **THEN** the byte offsets and widths above are observed exactly; `sizeof(RandoSlotHeader) == 80`; unused `reserved[16]` bytes are zero-initialized

#### Scenario: Sentinel placement entry is recognized
- **WHEN** the placement table contains the value `0xFFFF` at index k
- **THEN** the dispatcher treats location k as having no rando substitute and falls back to `vanilla_item_id`

#### Scenario: last_vanilla_write_version advances on every write
- **WHEN** a sidecar slot is written
- **THEN** `last_vanilla_write_version` in the new on-disk slot equals the current binary's `generator_version`, regardless of its previous value

#### Scenario: Slot-kind discriminator
- **WHEN** the sidecar exists but a given slot has `slot_kind = Vanilla` or `Empty`
- **THEN** the loader ignores any subsequent bytes of that slot and treats the corresponding `sram.dat` slot as a plain vanilla save

### Requirement: Settings-hash truncation policy

The `settings_hash` field stored in the sidecar slot header and in the share string SHALL be the **first 16 bytes** of `SHA-256(canonical_serialize(RandoSettings))`. Canonical serialization order is defined normatively in `randomizer-core / Settings canonical serialization order`.

#### Scenario: Truncation is first-16-bytes
- **WHEN** the same settings serialize twice
- **THEN** the resulting `settings_hash[16]` values are byte-identical and equal to `SHA-256(serialization)[0..16]`

### Requirement: Embedded placement table — upgrade safety

The load path SHALL use the embedded placement table directly and SHALL NOT regenerate from the share string. Loading SHALL succeed even when the binary's `generator_version` differs from the slot's, surfacing an informational warning rather than refusing.

#### Scenario: Same generator version loads cleanly
- **WHEN** the slot's `generator_version` matches the binary's
- **THEN** the slot loads with no warning

#### Scenario: Version drift loads with warning
- **WHEN** the slot's `generator_version` differs from the binary's
- **THEN** the slot loads using the embedded placement table, the loader reads exactly `placement_table_size` bytes (honoring the slot's size, not the binary's current registry count), and a one-time warning is shown

#### Scenario: Location registry is append-only
- **WHEN** a new generator version adds locations
- **THEN** the new locations receive new IDs at the end of the registry, no previously assigned location ID is reused or removed, and any older slot's `placement_table_size` remains a valid prefix of the new registry's location space

### Requirement: Checked-location bitmap read invariant

When reading a slot, the loader SHALL iterate only the bitmap bits in the range `[0, placement_table_size / 2)` — corresponding to the locations the writing binary actually placed. Bits beyond that range SHALL NOT be interpreted as "newer-registry locations not checked yet"; they SHALL be ignored entirely.

For a newer binary reading an older slot, the new locations beyond `placement_table_size / 2` SHALL be treated as **unchecked** (default state for never-visited locations), not as "zero bit means unchecked." The distinction matters: if the bitmap was stored slightly larger than the writing binary's `placement_table_size / 2` for forward-compat, those extra zero bits would be indistinguishable from "checked" if the reader assumed the bitmap covered the new registry too.

#### Scenario: Newer binary reading older slot ignores beyond-prefix bitmap bits
- **WHEN** a binary with `current_registry_locations = 216` reads a slot with `placement_table_size = 424` (212 locations) and the bitmap is sized for the slot's registry
- **THEN** the loader iterates bits `[0, 212)` of the bitmap; bits `[212, 216)` are unchecked-default (no incoming bitmap data; the 4 new locations cannot have been checked by the older binary because they didn't exist)

#### Scenario: Bitmap sizing formula
- **WHEN** a slot is written with `placement_table_size = S` bytes (S/2 locations)
- **THEN** the bitmap occupies `(S / 2 + 7) >> 3` bytes; reading the bitmap iterates `S / 2` bits

### Requirement: Downgrade-then-re-upgrade safety

The slot header SHALL carry `last_vanilla_write_version` and `sram_slot_checksum_at_last_write`. On load, the loader SHALL compare the current paired `sram.dat` slot's checksum to the stored value. A drift indicates that `sram.dat` was edited (e.g., by a downgraded binary writing the slot as vanilla).

#### Scenario: Drift detected after downgrade-then-re-upgrade
- **WHEN** the player wrote the slot on v1.1 (sidecar + sram.dat both updated), downgraded to v1.0 (sram.dat updated, sidecar untouched), then re-upgraded to v1.1 and loads the slot
- **THEN** the v1.1 loader observes a checksum mismatch between the sidecar's recorded `sram_slot_checksum_at_last_write` and the actual current paired `sram.dat` slot's checksum, surfaces a "rando state may be stale" warning, and offers two recovery actions: continue with embedded placement (default) or convert the slot to vanilla (removing the sidecar entry).

#### Scenario: No drift on clean upgrade
- **WHEN** the player saved on v1.1 and reloads on v1.2 without intervening downgrade
- **THEN** the checksum matches and no drift warning appears (only the standard `generator_version` informational warning, if any)

### Requirement: Orphan sidecar handling

The system SHALL handle inconsistent on-disk state between `sram.dat` and `sram_rando.dat` deterministically:

- **Orphan sidecar slot** (sidecar has `slot_kind = Randomizer` but the paired `sram.dat` slot is empty or zeroed): the loader treats the rando slot as **recoverable** — the embedded placement table is still valid; the paired `sram.dat` slot is initialized to a fresh-new-game state (zeroed plus the standard new-game header), and the rando slot loads with starting-inventory injection re-applied. The file-select screen displays the slot with a "recovered" badge.
- **Orphan vanilla slot** (sidecar slot is absent or `slot_kind = Empty` but `sram.dat` has data): the loader treats it as plain vanilla — sidecar absence means vanilla, by design.

#### Scenario: Sidecar present but sram.dat slot empty
- **WHEN** the user copies `sram_rando.dat` from another machine without the matching `sram.dat`, then starts the game
- **THEN** the file-select screen displays the rando slot with a "recovered" badge, loading triggers a fresh-new-game initialization paired with the embedded placement table, and starting-inventory injection re-applies

#### Scenario: sram.dat present but sidecar absent
- **WHEN** `sram_rando.dat` is missing while `sram.dat` has data
- **THEN** all slots load as vanilla (the sidecar's absence is the canonical "no rando state" signal)

### Requirement: Atomic-commit protocol

Each save SHALL use the write-temp-then-rename protocol per file, with explicit `fsync` (POSIX) or `_commit`/`FlushFileBuffers` (Win32). Save order SHALL be sidecar first, then `sram.dat`. On POSIX, the containing directory SHALL be fsynced after each rename.

#### Scenario: Crash between sidecar and sram.dat
- **WHEN** the process is killed after `sram_rando.dat` is renamed into place but before `sram.dat` is written
- **THEN** on next load, the sidecar's `sram_slot_checksum_at_last_write` mismatches the old `sram.dat` slot's actual checksum, the drift warning fires, and the player can choose to continue with embedded placement or convert to vanilla

#### Scenario: Crash mid-write of either file
- **WHEN** the process is killed during `fwrite` of either temp file
- **THEN** the corresponding live file remains in its previous state (the temp file is unlinked or simply abandoned), and the previously-committed state for that file is preserved

#### Scenario: fsync ordering
- **WHEN** a save is performed
- **THEN** the implementation calls `fwrite → fflush → fsync/_commit → close → rename → directory-fsync` for each file in that order, sidecar first then sram.dat

### Requirement: Spoiler-log persistence per slot

The spoiler log JSON SHALL be written to disk at slot-creation time at `<spoiler_dir>/<share_string_b32>.json`. The runtime derives the path from config + the slot's share string; the path is not stored in the slot.

#### Scenario: Spoiler written at slot creation
- **WHEN** a randomizer slot is created
- **THEN** the spoiler JSON exists at the configured path and can be located by a future load using only the slot's share string

### Requirement: Race-mode spoiler suppression (Phase B)

When race mode is enabled, the on-disk spoiler at slot-creation time SHALL contain only the share string and a SHA-256 stamp of the full spoiler. A reveal action SHALL regenerate the spoiler from the share string and verify the stamp matches.

#### Scenario: Race-mode file contains only stamp
- **WHEN** a race-mode slot is created
- **THEN** the on-disk file contains only the share string and the SHA-256 stamp

#### Scenario: Reveal verifies stamp
- **WHEN** the player triggers reveal for a race-mode slot
- **THEN** the full spoiler is regenerated, written to disk, and its SHA-256 matches the stamp; mismatch is reported as a verification failure

### Requirement: Snapshot interoperability

Save snapshots taken in a rando slot SHALL force `StateRecorder_ClearKeyLog` immediately before save so the snapshot file contains a non-empty `base_snapshot`. After the standard `StateRecorder_Save` write sequence (header + log + base_snapshot + SnesState dump), the runtime SHALL append a tail-TLV chain. Each TLV entry SHALL be `magic[8] + type[4] + length[4] + payload`. Phase A defines `TAIL_RANDO_STATE` with payload `(generator_version + settings_hash + share_string + placement_table_size + placement_table)`.

On load, after `StateRecorder_Load`'s standard chunks complete (and the existing `assert(state.p == state.pend)` validates the SnesState), the runtime SHALL iterate TLV entries: read `magic[8] + type[4] + length[4]`; if `type == TAIL_RANDO_STATE` deserialize and reinstall the placement table; if `type` is unknown, seek past `length` bytes; on EOF or non-matching magic, terminate the loop cleanly.

#### Scenario: Vanilla snapshot unchanged
- **WHEN** a snapshot is taken outside a rando slot
- **THEN** no tail is appended and the snapshot loads exactly as today

#### Scenario: Rando snapshot reload restores placement
- **WHEN** a rando snapshot is reloaded
- **THEN** the SnesState dump restores `g_ram` (including `kRam_Rando*` cells), the tail's placement table is reinstalled, and gameplay resumes with rando state intact

#### Scenario: Replay mode preserves rando
- **WHEN** a rando snapshot is replayed via `Ctrl+F1..F10`
- **THEN** the forced `base_snapshot` is loaded via `LoadSnesState` before input replay, the tail is reinstalled, and every replayed frame's dispatch fires correctly

#### Scenario: Older binary degrades gracefully
- **WHEN** a rando snapshot is loaded by an older binary that predates `RandoSnapshotTail`
- **THEN** the older loader reads the standard chunks, closes the file, and the trailing tail bytes are silently ignored; rando state never applies; the snapshot resumes as vanilla

#### Scenario: Newer binary reads older rando snapshot (cross-version TLV)
- **WHEN** a v1.1 binary loads a rando snapshot whose `TAIL_RANDO_STATE` payload was written by v1.0
- **THEN** the v1.1 TLV consumer reads the `generator_version` field at the start of the payload, dispatches on that version to select the appropriate payload-schema parser, and processes the rest of the payload accordingly. The TLV-payload-schema-version detection is the consumer's responsibility; the `generator_version` field is the discriminator. Payload format MAY evolve across generator versions (e.g., adding entrance-shuffle data in Phase C); the embedded `generator_version` lets newer consumers parse older payloads correctly

