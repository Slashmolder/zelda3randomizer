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
| 4 | 2 | `format_version` (LE) | Phase A = 1; current writes = 3 (version 2 added the per-slot settings blob, version 3 the slot extension block) |
| 6 | 2 | `slot_count` (LE) | Phase A = 3 |
| 8 | 4 | `file_crc` (LE) | CRC-32 over the slot region (every byte after this 16-byte header), computed on write and verified on read; `0` = legacy file (pre-CRC writers), accepted without verification |
| 12 | 4 | `reserved[4]` | zero-initialized; forward-compat |

**Slot region** = slot header (80 bytes) + placement table + checked-location bitmap + (`format_version` ≥ 2) a `kSettingsCanonicalLen`-byte canonical `RandoSettings` blob + (`format_version` ≥ 3) an 8-byte slot extension block (`@0-2` `entrance_digest24` LE, `@3-7` reserved zero). The loader keys each slot's body layout on the file's declared `format_version`, so v1/v2 files load with their shorter bodies.

**Slot header (80 bytes total, byte-counted)**:

| Offset | Width | Field | Notes |
|---:|---:|---|---|
| 0 | 4 | `magic[4]` | distinct from file magic |
| 4 | 1 | `slot_kind` | Empty=0 / Vanilla=1 / Randomizer=2 |
| 5 | 2 | `generator_version` (LE) | |
| 7 | 16 | `settings_hash[16]` | first 16 bytes of SHA-256 of canonical settings serialization (per `randomizer-core / Settings canonical serialization order`) |
| 23 | 32 | `share_string[32]` | stored seed identity: the 31-byte v1 raw share blob (`magic[4] \| generator_version[1] \| settings_hash[16] \| seed_u64[8] \| crc16[2]`) followed by one zero pad byte |
| 55 | 2 | `last_vanilla_write_version` (LE) | the `generator_version` under which the paired `sram.dat` slot was last consistently written; SHALL be set to the current `generator_version` on every sidecar write |
| 57 | 4 | `sram_slot_checksum_at_last_write` (LE) | checksum of the paired `sram.dat` slot snapshotted at the moment of sidecar write. Algorithm mirrors `src/messaging.c::SaveGameFile` for drift-detection compatibility with the vanilla writer: `t = 0x5a5a; for (i = 0; i < 0x4fe; i += 2) t -= u16le_at(slot_bytes + i); return (uint32)t;` (high 16 bits of the stored u32 are always zero). NOT CRC32 — the spec field is named for forward compatibility with a wider digest, but Phase A must match the vanilla per-slot routine. |
| 61 | 2 | `placement_table_size` (LE) | bytes; REQUIRED for cross-version forward compatibility |
| 63 | 1 | `flags` | bit 0 = forward-fill fallback was used; bits 1..7 reserved |
| 64 | 1 | `mushroom_held` | rando Mushroom/Powder ownership bitfield: bit 0 = undelivered Mushroom, bit 1 = Powder obtained; 0 for older slots |
| 65 | 1 | `settings_ext_present` | 1 when `hints_setting`/`goal`/`world_state` are meaningful |
| 66 | 1 | `hints_setting` | `RandoHintsMode` value |
| 67 | 1 | `goal` | `Goal` enum value |
| 68 | 1 | `world_state` | `WorldState` enum value |
| 69 | 1 | `flute_shovel_owned` | bitfield: shovel/flute/flute-activated ownership |
| 70 | 1 | `settings_present` | format_version >= 2: body carries a valid canonical settings blob |
| 71 | 1 | `entrance_axes` | packed `kEntranceAxis_*` byte, matching canonical byte 25 |
| 72 | 1 | `entrance_attempt` | accepted entrance-shuffle retry attempt |
| 73 | 1 | `boomerang_owned` | bitfield: blue/red boomerang ownership |
| 74 | 1 | `bow_owned` | bitfield: wood/silver bow ownership |
| 75 | 1 | `prize_attempt` | accepted assumed-fill attempt for prize/medallion shuffle re-derivation |
| 76 | 1 | `door_attempt` | accepted door-shuffle retry attempt |
| 77 | 3 | `door_digest24` (LE) | 24-bit door-layout digest used for activation drift hard-fail |

**Embedded placement table**: `placement_table_size` bytes — exact value stored per slot. The Phase A baseline location count is ~212 (Open / Standard / Inverted; counted from `app/Region/Standard/` location definitions). `Retro` world-state adds shop locations to the pool, inflating the table for those slots. The slot's `placement_table_size` field is therefore authoritative — the loader reads exactly that many bytes per slot, not a hardcoded constant. `placement_table_size` is always even (the table is a flat `uint16[]`); an odd value SHALL be rejected on write and treated as corruption (a failed slot parse) on load. **Item-ID `0xFFFF` is reserved as the "no placement / deprecated location" sentinel** — the dispatcher treats sentinel entries as "no rando substitute" and falls back to `vanilla_item_id` (per `randomizer-placement / Dispatcher signature and fall-back behavior`).

**Checked-location bitmap**: `(placement_table_size / 2 + 7) >> 3` bytes; iterated only over `[0, placement_table_size / 2)` bits.

**Forward-compat model (as built)**: the slot body has NO TLV chain (only the snapshot tail uses that shape; the entrance-permutation requirement below records why the original TLV plan was superseded). Additive slot state lands either in single bytes of the slot header's reserved tail or in fixed-size sections appended after the bitmap and keyed on the file `format_version`: version 2 appended the canonical settings blob, version 3 the 8-byte extension block. Cross-version reads are gated on `format_version` in both directions (older binaries do not skip newer sections); new writes always use the current version.

The spoiler file path SHALL NOT be stored in the slot. The runtime derives it from `[randomizer] spoiler_dir` config + the slot's share string — this is a deliberate decision so slots are invariant under config changes.

#### Scenario: Round-trip
- **WHEN** a randomizer slot is written and immediately read back
- **THEN** every header field, the full placement table, and the checked-location bitmap round-trip without change

#### Scenario: Slot header byte layout is exact
- **WHEN** a slot header is serialized
- **THEN** the byte offsets and widths above are observed exactly in the serialized 80-byte header; fields that are inactive for a slot are written as zero

#### Scenario: Sentinel placement entry is recognized
- **WHEN** the placement table contains the value `0xFFFF` at index k
- **THEN** the dispatcher treats location k as having no rando substitute and falls back to `vanilla_item_id`

#### Scenario: last_vanilla_write_version advances on every write
- **WHEN** a sidecar slot is written
- **THEN** `last_vanilla_write_version` in the new on-disk slot equals the current binary's `generator_version`, regardless of its previous value

#### Scenario: Slot-kind discriminator
- **WHEN** the sidecar exists but a given slot has `slot_kind = Vanilla` or `Empty`
- **THEN** the loader ignores any subsequent bytes of that slot and treats the corresponding `sram.dat` slot as a plain vanilla save

#### Scenario: Corrupt file is refused
- **WHEN** the file's `file_crc` is non-zero and does not match the CRC-32 recomputed over the slot region, or any slot declares an odd `placement_table_size`
- **THEN** the read fails (the file is treated as corrupt) and no slot state is installed; a legacy file with `file_crc == 0` skips CRC verification and loads normally

### Requirement: Settings-hash truncation policy

The `settings_hash` field stored in the sidecar slot header and in the share string SHALL be the **first 16 bytes** of `SHA-256(canonical_serialize(RandoSettings))`. Canonical serialization order is defined normatively in `randomizer-core / Settings canonical serialization order`.

#### Scenario: Truncation is first-16-bytes
- **WHEN** the same settings serialize twice
- **THEN** the resulting `settings_hash[16]` values are byte-identical and equal to `SHA-256(serialization)[0..16]`

### Requirement: Embedded placement table — upgrade safety

The load path SHALL use the embedded placement table directly and SHALL NOT regenerate from the share string. Loading SHALL succeed even when the binary's `generator_version` differs from the slot's, surfacing an informational warning rather than refusing.

Three activation-time validation gates are carve-outs from the warn-only rule, each refusing the slot **non-destructively** (deactivate; the file is untouched): a door-shuffle slot whose regenerated layout digest mismatches `door_digest24` (see "Door-layout regeneration with a digest hard-fail"); an entrance-shuffle slot with a non-zero `entrance_digest24` (format_version ≥ 3) whose regenerated entrance layout digest mismatches (see "Entrance-permutation persistence"); and a slot whose canonical settings blob is present but fails range validation (`Settings_CanonicalDeserialize` → `Settings_Validate` rejects out-of-range enum bytes; undefined flag bits stay permissive) — a corrupt enum would otherwise flow into shift/index consumers or silently skip the digest gates.

#### Scenario: Same generator version loads cleanly
- **WHEN** the slot's `generator_version` matches the binary's
- **THEN** the slot loads with no warning

#### Scenario: Version drift loads with warning
- **WHEN** the slot's `generator_version` differs from the binary's and no activation refusal gate fires (door/entrance layout digests verify or are absent; the settings blob, when present, validates)
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

When race mode is enabled, the on-disk spoiler at slot-creation time SHALL contain only the share string, a `generator_version`, and a SHA-256 stamp of the full spoiler that would have been generated with `race_mode` cleared. A reveal action SHALL regenerate the spoiler from the share string and verify the stamp matches.

The on-disk suppressed-file format SHALL be the file format defined in `randomizer-core / Spoiler-log emission` Race-mode suppression section. The format includes a CRC32 to detect file-system corruption or tampering.

The reveal action SHALL be the `Rando_RevealSpoiler(slot_index)` entry point defined in `randomizer-core / Race-mode reveal action`, callable from the file-select UI (see `randomizer-ui / Race-mode reveal UI`) or from the CLI's `--reveal-spoiler=<path>` flag.

#### Scenario: Race-mode file contains only stamp
- **WHEN** a race-mode slot is created
- **THEN** the on-disk file contains the magic header `ZRSR`, the generator_version (u16 LE), the SHA-256 stamp, the length-prefixed share-string (64-byte buffer with leading length), the canonical-serialized settings (`kSettingsCanonicalLen` = 28 bytes, with `race_mode` cleared), and the CRC32 — and nothing else; the total size is exactly 138 bytes (`kRandoSuppressedSpoilerSize`). The settings field is included because the sidecar slot does not preserve `RandoSettings` and reveal needs it to regenerate placement deterministically.

#### Scenario: Reveal verifies stamp
- **WHEN** the player triggers reveal for a race-mode slot
- **THEN** the full spoiler is regenerated, written to disk, and its SHA-256 matches the stamp; mismatch is reported as a verification failure and the suppressed file is preserved unchanged

#### Scenario: Sidecar slot survives unchanged across reveal
- **WHEN** the reveal action completes (success or failure)
- **THEN** the sidecar's slot bytes, placement table, and checked-location bitmap are unchanged; reveal touches only files in `<spoiler_dir>`, not in `<saves_dir>`

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

### Requirement: Entrance-permutation persistence via the header reserved tail

When an entrance shuffle is active for a generated slot, the sidecar slot SHALL
persist enough state to restore the entrance permutation π on slot load **without
storing the full permutation**, by carrying it additively in the previously-zero
header reserved tail and **regenerating π deterministically** at load. This
supersedes the original stubbed `TAIL_ENTRANCE_MAP` TLV design: the slot file
format has no TLV-skip infrastructure after the bitmap (unlike the snapshot-tail
format), and three multi-slot packers sum the fixed `RandoSave_SlotOnDiskSize`, so
a variable-length tail is not viable without a format break. Because π for a given
attempt is a pure function of `(seed, settings axes, attempt)`, regeneration is
cheaper and equally robust — the same precedent the Phase B hints settings
extension established.

The slot header (80 bytes, unchanged size) SHALL carry, in the reserved tail:
- `@70 entrance_axes` (uint8) — the packed entrance-axis byte (identical to the
  canonical settings byte `[25]`; `kEntranceAxis_*` bits). `0` ⇒ no entrance
  shuffle.
- `@71 entrance_attempt` (uint8) — the accepted goal-retry attempt index whose
  permutation π was used.

On slot load, when `entrance_axes` has the cave-shuffle bit set AND the slot's
`world_state` is in the supported set (Open/Standard), the runtime SHALL
regenerate π from the seed (recovered from the stored `share_string`),
`entrance_axes`, and `entrance_attempt`, then install the door overlay + per-seed
region overrides. When `entrance_axes == 0` the bytes are zero and the slot is
byte-identical (sans these two additive bytes, which older binaries already treat
as reserved) to a non-entrance-shuffle slot.

Regeneration drift SHALL be guarded by a digest (sidecar `format_version` 3): at
generation the slot stores `entrance_digest24` — the low 24 bits of
`Rando_EntranceLayoutDigest24` over everything the runtime entrance install
regenerates from `(seed, axes, attempt)` — in the v3 slot extension block (the
80-byte header is fully claimed). At activation, BEFORE any slot state installs,
a slot with a non-zero `entrance_digest24` SHALL recompute the digest and on
mismatch SHALL **refuse the slot** non-destructively (the same pathway as the
door-shuffle gate — a drifted entrance layout can make the certified-beatable
placement unbeatable). `entrance_digest24 == 0` — no entrance shuffle, or a
pre-v3 slot that physically lacks the field — SHALL keep the legacy warn-only
behavior: the slot loads, and `Entrance_RuntimeInstall` surfaces a loud
version-drift warning when the slot's `generator_version` differs from the
binary's (the regenerated π may not match the baked placement).

#### Scenario: Entrance-shuffle slot round-trip
- **WHEN** a coupled cave-shuffle slot is written and read back
- **THEN** `entrance_axes` + `entrance_attempt` (and the v3 `entrance_digest24`)
  round-trip byte-identical, and the runtime regenerates the SAME permutation π
  (so the same door→interior mapping and region overrides are restored)

#### Scenario: Entrance-layout drift blocks activation (v3 slots)
- **WHEN** a format_version-3 entrance-shuffle slot's recomputed entrance-layout
  digest differs from the stored `entrance_digest24` (e.g. the entrance algorithm
  or pool changed across builds)
- **THEN** activation refuses the slot (deactivates, with a diagnostic) instead of
  installing a permutation the placement was not certified against; the slot file
  is left intact

#### Scenario: Legacy digest-0 slot keeps the warn-only behavior
- **WHEN** a pre-v3 entrance-shuffle slot (no extension block, so
  `entrance_digest24` reads 0) loads under a different `generator_version`
- **THEN** the slot loads with its embedded placement and a loud warning that the
  regenerated entrance layout may not match — the legacy non-blocking path

#### Scenario: Older binary ignores the entrance bytes
- **WHEN** a Phase A or Phase B binary (no Phase C support) reads a Phase C
  entrance-shuffle slot
- **THEN** it reads the header through `@69` and treats `@70`/`@71` as reserved
  (ignored); the slot loads as a vanilla-entrance seed (graceful degradation — the
  door overlay is simply not installed)

#### Scenario: No entrance bytes when shuffle is off
- **WHEN** a Phase C binary writes a slot with no entrance shuffle active
- **THEN** `entrance_axes == 0` and `entrance_attempt == 0`; the slot is
  byte-identical to a Phase B slot for the same seed (the two additive bytes were
  already zero)

### Requirement: Checked-location bitmap write path

The sidecar SHALL maintain a per-slot `checked_locations_bitmap` of size `(placement_table_size / 2 + 7) >> 3` bytes — the same size the Phase A read invariant (`randomizer-save / Checked-location bitmap read invariant`) already specs. **The write side** is added here.

The bit at position `k` (location_id k) SHALL be set to 1 when ANY of:

1. `Rando_OnLocationCheck(LOC_<...>, vanilla_item)` fires for that location and the dispatcher grants a substitute (the standard hot path).
2. An audit-exempt direct-write site enumerated in `audit.md` §"Reachability-affecting events" — Aga 1 defeat, every dungeon-boss-cleared flag, NPC-satisfied flags (sahasrahla, sick kid, magic-bat / mushroom→powder), pyramid-opened, master-sword-pulled, king's tomb item taken — fires and the site is the canonical pickup for a tracked location.
3. The §6.3 universal chest hook resolves a chest to a `LOC_<...>` and grants the placement-table substitute (covers every chest in the world, including ones not separately enumerated).

The bit SHALL persist to disk on the next sidecar write (per the existing `randomizer-save / Atomic-commit protocol`). Mid-session bit-set is in-memory only until the next save fires.

The bitmap SHALL NOT be cleared by the dispatcher under any circumstance during a session — once a location is checked, it stays checked until the slot is erased or the player explicitly invokes file-erase from the file-select screen.

When the in-session bitmap differs from the on-disk bitmap and the sidecar write fires, the writer SHALL include the updated bitmap; the trailing slot sections (the `format_version` ≥ 2 settings blob and the `format_version` ≥ 3 extension block) SHALL still be appended after the bitmap regardless of whether the bitmap content changed.

#### Scenario: Dispatcher fire sets the bit
- **WHEN** the dispatcher fires for `LOC_HyruleCastle_BoomerangChest` and grants a substitute
- **THEN** bit `LOC_HyruleCastle_BoomerangChest` in the in-memory bitmap is set to 1; on the next sidecar write, the on-disk bitmap reflects the change

#### Scenario: Audit-exempt event flag sets the bit when site is a tracked location
- **WHEN** the audit-exempt site for `RescuedZelda` fires (uncle's death / sanctuary escort completion) and the corresponding location is enumerated in `audit.md` §"Reachability-affecting events" with a `LOC_<...>` tag
- **THEN** that location's bit in the in-memory bitmap is set to 1

#### Scenario: No checkbacking — bit-set is monotonic during a session
- **WHEN** any logic path that already set bit k tries to set it again
- **THEN** the operation is a no-op; the bit stays at 1; no side effects

#### Scenario: Bitmap persists across save/load
- **WHEN** the player has 47 locations checked, saves, quits, and reloads
- **THEN** the bitmap on reload reflects all 47 checked bits; the location-tracker (`randomizer-ui / Optional in-game location tracker (Phase B)`) shows the same `*` glyphs that were visible before the save

#### Scenario: Trailing slot sections preserved
- **WHEN** the writer commits the slot with updated bitmap
- **THEN** the bitmap is at the same byte offset (slot header + placement table) and is `(placement_table_size / 2 + 7) >> 3` bytes long; the trailing settings blob and extension block follow it unaffected by bitmap-content changes

#### Scenario: File-erase clears the bitmap
- **WHEN** the player invokes file-erase on a slot from the file-select screen
- **THEN** the next write for that slot writes a fresh slot header with `slot_kind = Empty=0`, no placement table, and no bitmap; subsequent re-creation of the slot starts with a fresh zeroed bitmap

### Requirement: Door-layout regeneration with a digest hard-fail (sidecar tail @76-79)

The per-seed door layout SHALL NOT be serialized into the sidecar slot. Instead it
SHALL be **regenerated deterministically from `(base_seed, settings,
door_attempt)`** at slot activation, mirroring the entrance-permutation
regeneration. The sidecar header SHALL claim the `reserved[4]` tail (after the
boomerang/bow/`prize_attempt` fields at @73–75) as: `door_attempt` at `@76` (the
accepted generation attempt, so a reject-and-retry generation that accepts attempt
*k* replays to the identical layout on load) and a **24-bit layout digest** at
`@77–79` (3 bytes LE on disk — the low 24 bits of `DoorShuffle_LayoutDigest` over
the ACCEPTED layout: pairings + key doors + thresholds + `bk_restricted`). Both
fields are zero on vanilla-door slots and for pre-field writers. The settings blob
already carries the door-shuffle axis; the share string already carries the seed —
no other persisted state is required.

Drift SHALL be **blocking** — the model entrance shuffle has since adopted for its
own digest (`entrance_digest24`, sidecar format_version 3; only legacy digest-0
entrance slots still use the non-blocking informational warning). At activation
(`Rando_ActivateSidecarSlot`), BEFORE any slot state is
installed, a slot whose effective settings enable door shuffle SHALL regenerate the
layout from the share-string seed + persisted `door_attempt`, recompute the 24-bit
digest, and on generation failure or digest mismatch SHALL **refuse the slot**
(deactivate; treated as no-rando for this session) rather than silently load — a
drifted interior layout can make the certified-beatable serialized placement
unbeatable. The refusal is non-destructive: the slot file is untouched and remains
loadable by the build that wrote it.

#### Scenario: Door layout regenerates identically on load

- **WHEN** a door-shuffle slot is saved and reloaded under the same generator
  version
- **THEN** the layout regenerated from `(seed, settings, door_attempt)` digests
  equal to the persisted `@77–79` value, activation installs it (logic oracle +
  runtime redirect table + `kFeatures1_DoorShuffleActive`), and no door-layout
  bytes are stored in the slot

#### Scenario: Reject-and-retry replays to the accepted attempt

- **WHEN** generation rejected attempts 0..k-1 and accepted attempt k
- **THEN** `door_attempt == k` is persisted at `@76`, and activation regenerates
  attempt k directly to recover the identical accepted layout

#### Scenario: Layout drift blocks activation non-destructively

- **WHEN** a door-shuffle slot's regenerated layout digest differs from the
  persisted `@77–79` value (or regeneration fails outright) — e.g. the door pool or
  stitcher changed across builds
- **THEN** activation refuses the slot (deactivates, with a diagnostic) instead of
  silently loading a layout the placement was not certified against, and the slot
  file is left intact

