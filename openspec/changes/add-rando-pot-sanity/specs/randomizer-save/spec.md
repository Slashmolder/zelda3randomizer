## MODIFIED Requirements

### Requirement: Checked-location bitmap read invariant

When reading a slot, the loader SHALL iterate only the bitmap bits in the range `[0, placement_table_size / 2)` — corresponding to the locations the writing binary actually placed. Bits beyond that range SHALL NOT be interpreted as "newer-registry locations not checked yet"; they SHALL be ignored entirely.

For a newer binary reading an older slot, the new locations beyond `placement_table_size / 2` SHALL be treated as **unchecked** (default state for never-visited locations). A reader MUST treat beyond-prefix locations as unchecked because they did not exist when the slot was written, and MUST NOT infer their state from bitmap bytes that may extend past `placement_table_size / 2` for byte alignment. (Bit polarity: a set bit means checked and a clear bit means unchecked, per `src/rando/rando.c`; the point is that beyond-prefix bits carry no authoritative information, not that "clear == checked.")

The pot-sanity expansion (location registry growing to ~1127 IDs) rides this existing per-slot, `placement_table_size`-keyed sizing with **no on-disk format change**: a pot-shuffle slot simply has a larger `placement_table_size` and a correspondingly larger bitmap. Only the in-memory working buffers (`kRandoCheckedBitmapBytes`, the placement arrays) grow to the 2048 ceiling; the wire format is unchanged.

**Compatibility is one-directional.** The append-only ID rule (`Embedded placement table — upgrade safety`) lets a **newer binary read an older slot** (the older slot is a valid prefix of the larger registry; the new IDs default unchecked). The reverse is **NOT supported**: a pre-pot-shuffle (512-era) binary reading a pot-expanded slot sees a `placement_table_size` larger than its fixed buffers and **rejects the slot non-destructively** (the size/bounds validation refuses it; that 512-era binary's snapshot tail likewise rejects a `placement_table_bytes` past its `512 × 2 = 1024` cap — the current pot-capable tail's cap is `kRandoLocationCapacity × 2`). There is no forward-compat for old binaries without an added compat layer (out of scope) — the slot is refused and the paired `sram.dat` save is treated as plain vanilla, file untouched.

#### Scenario: Newer binary reading older slot ignores beyond-prefix bitmap bits
- **WHEN** a binary with `current_registry_locations = 216` reads a slot with `placement_table_size = 424` (212 locations) and the bitmap is sized for the slot's registry
- **THEN** the loader iterates bits `[0, 212)` of the bitmap; bits `[212, 216)` are unchecked-default (no incoming bitmap data; the 4 new locations cannot have been checked by the older binary because they didn't exist)

#### Scenario: Bitmap sizing formula
- **WHEN** a slot is written with `placement_table_size = S` bytes (S/2 locations)
- **THEN** the bitmap occupies `(S / 2 + 7) >> 3` bytes; reading the bitmap iterates `S / 2` bits

#### Scenario: Pot-shuffle slot round-trips with the larger registry
- **WHEN** a `pot_shuffle = All` slot with ~1127 placed locations is written and read back by a pot-capable binary
- **THEN** `placement_table_size` reflects all placed locations, the bitmap is `(placement_table_size / 2 + 7) >> 3` bytes, and the round-trip is lossless

#### Scenario: Old binary refuses a pot-expanded slot (one-directional compat)
- **WHEN** a pre-pot-shuffle (512-era) binary loads a sidecar whose slot has a `placement_table_size` implying ~1127 locations
- **THEN** the slot fails size/bounds validation and is refused non-destructively (the file is untouched and the paired `sram.dat` slot is treated as plain vanilla); the old binary does NOT read pot IDs beyond its registry

## ADDED Requirements

### Requirement: Snapshot persists the rando checked-location bitmap

The F-key developer snapshot (`StateRecorder`, which does a full `g_ram` dump-restore — NOT input replay) SHALL also persist and restore `g_rando_checked_bitmap`, a C global that lives OUTSIDE `g_ram` and is therefore invisible to the snapshot's RAM dump. Without it, saving and reloading a snapshot loses which locations are checked. Pots uniquely require this: a checked chest/NPC sets a persistent vanilla SNES flag (captured by the `g_ram` dump), but a checked **pot** has NO vanilla flag — its only record is the rando bitmap, so a snapshot reload would un-check every broken pot (re-granting it / dropping its recolor-as-checked).

This SHALL be a new TLV in the snapshot tail (`rando_snapshot_tail.{c,h}`, type 3 `CheckedBitmap`): a flat `kRandoCheckedBitmapBytes` copy written after the existing TLVs and restored by `memset`-then-`fread min(length, kRandoCheckedBitmapBytes)` — backward-compatible (an older snapshot without the TLV restores an all-clear bitmap; a larger payload is truncated). `--rando-selftest` SHALL round-trip the bitmap through the tail.

#### Scenario: A broken pot stays checked across a snapshot reload
- **WHEN** the player breaks an in-scope pot, takes an F-key snapshot, then replays it
- **THEN** the pot is still marked checked — the type-3 CheckedBitmap TLV round-trips `g_rando_checked_bitmap` — so the pot does not re-grant and stays recolored as checked

#### Scenario: Older snapshot without the TLV restores cleanly
- **WHEN** a snapshot written before the CheckedBitmap TLV existed is replayed
- **THEN** the loader recognizes the absent TLV and restores an all-clear bitmap (no crash, no stale check state)
