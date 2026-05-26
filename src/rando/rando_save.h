// rando_save.h — sidecar save + snapshot tail (tasks.md §8).
//
// Sidecar file: saves/sram_rando.dat — parallel to saves/sram.dat. 3 slots
// (NOT 4 — per audit.md §0.6). Per-slot 80-byte header + embedded placement
// table + checked-location bitmap.
//
// On-disk byte layout is the determinism contract — multi-byte fields are
// little-endian (per randomizer-core / Byte-order pin). The C structs below
// describe the IN-MEMORY shape; serialization in rando_save.c uses explicit
// per-field LE writes (no compiler padding reliance).
//
// Atomic-commit protocol (per design.md D12): write .tmp, fflush,
// fsync/_commit, rename, fsync containing dir. Save order: sidecar first,
// then sram.dat (a crash between writes leaves sram.dat matching the prior
// sidecar — the safer of two recovery branches).

#ifndef ZELDA3_RANDO_SAVE_H_
#define ZELDA3_RANDO_SAVE_H_

#include "../types.h"
#include "rando_placement.h"

// Magic prefix for both file header and per-slot header. "ZRSC" (Zelda Rando
// SideCar) in ASCII; little-endian read produces 0x4353525A. Distinct from the
// share-string magic "ZRSS" (rando_share.h).
#define kRandoSidecar_FileMagic  0x4353525A  // 'Z' 'R' 'S' 'C' on disk
#define kRandoSidecar_SlotMagic  0x53435253  // 'S' 'R' 'C' 'S' on disk

// Sidecar file constants.
#define kRandoSidecar_FileFormatVersion 1
#define kRandoSidecar_SlotCount         3       // mirrors sram.dat's 3-slot layout
#define kRandoSidecar_FileHeaderSize    16
#define kRandoSidecar_SlotHeaderSize    80
#define kRandoSidecar_ShareStringLength 32      // raw binary (rando_share writes 31 bytes + pad)

// Per randomizer-save spec § Slot header: 3-value discriminator.
// Empty=0 is the all-zeroes default, distinguishable from an explicit
// Vanilla=1 slot (i.e., the user has affirmatively claimed this slot as
// non-randomized). The orphan/recovery logic relies on this distinction.
typedef enum {
  kSlotKind_Empty = 0,
  kSlotKind_Vanilla = 1,
  kSlotKind_Randomizer = 2,
} RandoSlotKind;

// In-memory representation of one slot header. On-disk byte layout follows
// the field-by-field LE serialization in rando_save.c; do NOT rely on this
// struct's compiler-chosen padding.
//
// Authoritative on-disk byte offsets (per randomizer-save spec):
//   @0  magic[4]                            (= kRandoSidecar_SlotMagic, LE)
//   @4  slot_kind (u8)                      (0 = vanilla, 1 = randomizer)
//   @5  generator_version (u16 LE)          (= kGeneratorVersion at write time)
//   @7  settings_hash[16]                   (first 16 bytes of full SHA-256)
//   @23 share_string[32]                    (raw binary; rando_share writes 31 + pad)
//   @55 last_vanilla_write_version (u16 LE) (kGeneratorVersion at last write)
//   @57 sram_slot_checksum_at_last_write (u32 LE)
//   @61 placement_table_size (u16 LE)       (**bytes**; placement_table_size / 2 = location count)
//   @63 flags (u8)                          (bit 0 = forward-fill fallback was used)
//   @64 reserved[16]                        (forward-compat; zero on write)
//   Total = 80 bytes.
//
// Per spec: the on-disk embedded placement table is a FLAT uint16[] indexed
// by location_id (length = placement_table_size / 2). Each slot holds the
// item_id placed at that location; 0xFFFF is the "no placement / deprecated
// location" sentinel that the dispatcher treats as fall-back-to-vanilla.
// The in-memory `placements[]` array remains a sparse (location, item) list
// for convenience; serialize/deserialize translate between the two forms.
typedef struct RandoSlotHeader {
  uint8 slot_kind;
  uint16 generator_version;
  uint8 settings_hash[16];
  uint8 share_string[kRandoSidecar_ShareStringLength];
  uint16 last_vanilla_write_version;
  uint32 sram_slot_checksum_at_last_write;
  uint16 placement_table_size;  // **bytes**; placement_table_size / 2 = #locations stored
  uint8 flags;                  // bit 0 = forward-fill fallback was used
} RandoSlotHeader;

// Bitmap covers placement_table_size / 2 locations.
#define kRandoSlotFlag_ForwardFillUsed 0x01

// File header (16 bytes total):
//   @0  magic[4]                            (= kRandoSidecar_FileMagic, LE)
//   @4  format_version (u16 LE)             (= kRandoSidecar_FileFormatVersion)
//   @6  slot_count (u16 LE)                 (= 3)
//   @8  file_crc (u32 LE)                   (CRC-32 over slot data; 0 in Phase A)
//   @12 reserved[4]                         (zero on write)
typedef struct RandoSidecarFileHeader {
  uint16 format_version;
  uint16 slot_count;
  uint32 file_crc;
} RandoSidecarFileHeader;

// One in-memory slot.
//
// `placements[]` is the sparse in-memory representation of the placement
// table; on-disk it is serialized as a flat uint16[] indexed by location_id.
// `placement_count` tracks how many entries of `placements[]` are valid.
// `header.placement_table_size` is the byte length of the on-disk flat
// array (= 2 × max_location_id_in_use + 2 for the writer; the spec lets
// later binaries that have a larger registry happily ignore zeros beyond
// this prefix).
typedef struct RandoSidecarSlot {
  RandoSlotHeader header;
  RandoPlacement placements[512];  // sized for ~237 + headroom
  uint16 placement_count;          // valid entries in placements[]
  uint8 checked_bitmap[(512 + 7) >> 3];
} RandoSidecarSlot;

// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------

// Compute the on-disk byte size of one slot given its placement_table_size.
// placement_table_size is in BYTES (= 2 × location count).
// Total = 80 (header) + placement_table_size + ((placement_table_size/2 + 7) >> 3) bitmap.
uint32 RandoSave_SlotOnDiskSize(uint16 placement_table_size);

// Sentinel item_id for "no placement / deprecated location" — written at
// indices that have no corresponding RandoPlacement entry.
#define kRandoSidecar_NoPlacementSentinel 0xFFFFu

// Serialize one slot into a fixed buffer. Returns the byte count written or 0
// on failure (buffer too small).
uint32 RandoSave_SerializeSlot(const RandoSidecarSlot *slot,
                               uint8 *buf, uint32 buf_size);

// Deserialize one slot from a buffer. Returns the byte count consumed or 0 on
// failure (bad magic, truncated, corrupt counts).
uint32 RandoSave_DeserializeSlot(const uint8 *buf, uint32 buf_size,
                                 RandoSidecarSlot *out);

// Serialize the file header into the first 16 bytes of `buf`.
uint32 RandoSave_SerializeFileHeader(const RandoSidecarFileHeader *h,
                                     uint8 *buf, uint32 buf_size);

// Deserialize the file header. Returns 0 on failure.
uint32 RandoSave_DeserializeFileHeader(const uint8 *buf, uint32 buf_size,
                                       RandoSidecarFileHeader *out);

// Atomic write of the in-memory sidecar to `path`. Per §8.2: write
// `<path>.tmp`, fflush, fsync/_commit, rename, fsync containing dir.
// Phase A0 implementation: no fsync (host-OS-dependent); uses rename only.
// Phase A1 follow-on lands the full POSIX/Windows atomic protocol.
bool RandoSave_WriteFile(const char *path,
                         const RandoSidecarSlot slots[kRandoSidecar_SlotCount]);

// Read the sidecar file from `path`. On success, populates `out_slots` and
// returns true. Missing file is NOT an error (returns false; caller treats all
// slots as vanilla).
bool RandoSave_ReadFile(const char *path,
                        RandoSidecarSlot out_slots[kRandoSidecar_SlotCount]);

void RandoSave_SelfCheck(void);

#endif  // ZELDA3_RANDO_SAVE_H_
