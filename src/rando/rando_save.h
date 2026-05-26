// rando_save.h — sidecar save + snapshot tail (tasks.md §8). Stub.
//
// Sidecar file: saves/sram_rando.dat — parallel to saves/sram.dat. 3 slots
// (NOT 4 — see audit.md §0.6). Per-slot 80-byte header + embedded placement
// table + checked-location bitmap.
//
// Atomic-commit protocol (per design.md D12): write .tmp, fflush, fsync,
// rename, fsync containing dir. Save order: sidecar first, then sram.dat.

#ifndef ZELDA3_RANDO_SAVE_H_
#define ZELDA3_RANDO_SAVE_H_

#include "../types.h"
#include "rando_placement.h"

#define kRandoSidecarMagic 0x52414e4f  // "RANO" in LE

typedef enum {
  kSlotKind_Vanilla = 0,
  kSlotKind_Randomizer = 1,
} RandoSlotKind;

// 80 bytes total per randomizer-save spec.
typedef struct RandoSlotHeader {
  uint32 magic;                          // 0x00: kRandoSidecarMagic
  uint8 slot_kind;                       // 0x04
  uint8 reserved_flags;                  // 0x05
  uint16 placement_table_size;           // 0x06: number of (location, item) pairs
  uint32 generator_version;              // 0x08
  uint32 last_vanilla_write_version;     // 0x0c
  uint8 settings_hash[16];               // 0x10
  uint8 share_string_binary[32];         // 0x20: raw 31-byte binary + 1 pad
  uint32 sram_slot_checksum_at_last_write; // 0x40
  uint8 reserved[12];                    // 0x44-0x4f: forward-compat
} RandoSlotHeader;
_Static_assert(sizeof(RandoSlotHeader) == 80, "RandoSlotHeader must be exactly 80 bytes");

// Load the sidecar slot (slot_index ∈ [0, 3)). Returns true on success.
// On rando-kind slots, populates kRam_RandoSlotActive and installs the
// embedded placement table.
bool RandoSave_LoadSlot(int slot_index);

// Atomic write of all three slots to saves/sram_rando.dat. Per §8.2.
bool RandoSave_WriteAll(void);

#endif  // ZELDA3_RANDO_SAVE_H_
