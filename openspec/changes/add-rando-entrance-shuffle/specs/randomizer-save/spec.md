## ADDED Requirements

### Requirement: TAIL_ENTRANCE_MAP TLV chain entry

When `settings.entrance_shuffle != none`, the sidecar slot SHALL persist the entrance-permutation overlay so the runtime can restore it on slot load without re-running the shuffle algorithm. Persistence uses the TLV chain reserved at Phase A `randomizer-save / Slot region layout` Phase C foresight, appended after the checked-location bitmap.

The TLV entry SHALL be:
- `magic[8]` — `"ENTMAP\0\0"` (8 bytes, padded with NUL).
- `type[4]` — `TAIL_ENTRANCE_MAP` (numeric value pinned in `audit.md §"TLV type registry"`).
- `length[4]` — `NUM_ENTRANCES * 2` (each entrance id is uint16 LE).
- `payload` — `uint16 overlay[NUM_ENTRANCES]` LE.

When `settings.entrance_shuffle == none`, the TLV SHALL be omitted (older binaries reading the slot will see no entrance-map TLV and behave correctly).

The TLV entry SHALL coexist with any future TLV chain entries; older binaries that don't recognize `TAIL_ENTRANCE_MAP` SHALL seek past `length` bytes and continue parsing per the Phase A "unknown TLVs are skipped" contract.

> **Stub status**: exact magic byte sequence + TLV numeric type value deferred to Phase C apply-time `audit.md` update.

#### Scenario: Entrance-shuffle slot round-trip
- **WHEN** an entrance-shuffle Insanity slot is written and read back
- **THEN** the `TAIL_ENTRANCE_MAP` TLV round-trips byte-identical; the runtime restores the same overlay on load

#### Scenario: Older binary ignores entrance TLV
- **WHEN** a Phase A or Phase B binary (no Phase C support) reads a Phase C entrance-shuffle slot
- **THEN** the binary parses the bitmap successfully, encounters the `TAIL_ENTRANCE_MAP` TLV, reads its length, seeks past, and continues; the slot loads as if entrance shuffle were not active (which means it plays as a vanilla-entrance seed, NOT crashes — graceful degradation per Phase A's TLV-skip contract)

#### Scenario: No TLV when entrance_shuffle is none
- **WHEN** a Phase C binary writes a slot with `settings.entrance_shuffle == none`
- **THEN** no `TAIL_ENTRANCE_MAP` TLV is appended; the slot is byte-identical (sans TLV chain) to a Phase B slot for the same seed
