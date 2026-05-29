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
//   @64 mushroom_held (u8)                   (rando Mushroom-possession; bit 0 = held, not yet delivered)
//   @65 settings_ext_present (u8)            (Phase B hints; 1 = @66/@67 meaningful, 0 = unset)
//   @66 hints_setting (u8)                   (Phase B hints; RandoHintsMode: 0=off 1=on)
//   @67 goal (u8)                            (Phase B hints; Goal enum, rando_settings.h)
//   @68 world_state (u8)                      (Phase B Inverted runtime; WorldState enum,
//                                              rando_settings.h. Only meaningful when
//                                              settings_ext_present == 1. Carried additively so
//                                              slot-load knows it is an Inverted seed and can
//                                              grant Moon Pearl + Magic Mirror / start in the
//                                              Dark World. Older slots read 0 == Open == no-op.)
//   @69 reserved[11]                         (forward-compat; zero on write)
//   Total = 80 bytes.
//
// === Phase B hints (Slice 5): settings extension in the reserved tail ===
// The hint generator (rando_hints.c::Rando_GenerateHints) reads exactly two
// axes from RandoSettings: `hints` (off/on) and `goal`. To regenerate hints
// at slot-load WITHOUT enlarging the slot (the on-disk size is coupled to the
// ZRSR file size + corpus-runner constants via kSettingsCanonicalLen — see
// [[canonical-size-coupling]]), those two bytes are carried ADDITIVELY in the
// previously-zero reserved tail. @64 (mushroom_held) is unchanged; the ext
// starts at @65. Existing field offsets and the 80-byte slot size are
// unchanged; old binaries reading a new file see @65-67 as "reserved" and
// ignore them. settings_ext_present == 0 (older slot, or a writer that does
// not populate it) makes the loader fall back to "hints on".
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
  // Rando Mushroom-possession flag, stored at on-disk offset @64 (the first
  // byte of the former reserved[16] block; older binaries wrote it as zero).
  // Set while the player holds the Mushroom item but has not yet handed it to
  // the Witch. Tracked here rather than via link_item_mushroom — which
  // doubles as the Powder slot — so obtaining Powder first cannot lock out
  // the Potion Shop check. See Rando_MushroomHeld / Witch_AcceptShroom.
  uint8 mushroom_held;
  // Phase B hints settings extension (serialized into reserved bytes @65-67;
  // see the layout note above). settings_ext_present == 0 means "not written"
  // and the loader applies the hints-on default.
  uint8 settings_ext_present;   // @65
  uint8 hints_setting;          // @66 (RandoHintsMode: 0=off 1=on)
  uint8 goal;                   // @67 (Goal enum, rando_settings.h)
  // Phase B Inverted runtime: the seed's world_state, carried additively at
  // @69... no — at @68 (first reserved byte after the hints ext). Only
  // meaningful when settings_ext_present == 1. Lets slot-load regrant the
  // Inverted starting inventory (Moon Pearl + Magic Mirror) and recognize an
  // Inverted seed for runtime world-state setup. Older slots / non-populated
  // writers read 0 (== kWorldState_Open), which is the safe no-op default.
  uint8 world_state;            // @68 (WorldState enum, rando_settings.h)
  // Flute/shovel ownership bitfield, stored additively at @69 (older binaries
  // wrote it as zero). The vanilla link_item_flute byte (0xF34C) is a single
  // slot — 1=shovel, 2=flute, 3=active flute — so it can't represent owning
  // BOTH a flute and the shovel, which rando shuffles as independent items.
  // We persist true ownership here and treat link_item_flute as the currently
  // SELECTED function (toggled in the item menu). Bits: 0x01 shovel, 0x02
  // flute, 0x04 flute activated. See kRandoFluteShovel_* / Rando_GrantFluteShovel.
  uint8 flute_shovel_owned;     // @69
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

// ---------------------------------------------------------------------------
// Per-slot SRAM checksum (tasks.md §8.4).
//
// Sources the algorithm from `src/messaging.c::SaveGameFile` — the project's
// existing per-slot checksum routine. NOT a freshly-invented one (per
// CLAUDE.md / spec-vs-impl discipline).
//
// Routine (per `SaveGameFile`):
//   t = 0x5a5a;
//   for (i = 0; i < 0x4fe; i += 2) t -= *(uint16 *)(slot_bytes + i);
// Returns t as a uint32 (zero-extended). The on-disk field is u32 LE per the
// randomizer-save spec; the spec's wording calls this "CRC32 of the paired
// sram.dat slot" but the practical contract is "any deterministic checksum
// of the paired slot bytes." We use the existing 16-bit algorithm to keep
// the cost of computing this in lockstep with vanilla SaveGameFile.
//
// `slot_bytes` must point at the start of a vanilla SRAM slot (0x500 bytes
// total, but the checksum only covers bytes 0..0x4fe). `size` MUST be >=
// 0x4fe; smaller buffers return 0.
// ---------------------------------------------------------------------------
uint32 RandoSave_ComputeSramSlotChecksum(const uint8 *slot_bytes, uint32 size);

// ---------------------------------------------------------------------------
// Rando_LoadSidecarSlot (tasks.md §8.3).
//
// Reads `saves/sram_rando.dat`, validates the file header, then deserializes
// slot `slot_index` (0 ≤ slot_index < kRandoSidecar_SlotCount). On success
// the caller receives the full slot in `*out` (header + sparse placements +
// checked-location bitmap).
//
// Integration contract for the file-select UI (deferred to UI sprint):
//
//   1. UI calls Rando_LoadSidecarSlot(slot_index, &slot).
//   2. If returns false → no sidecar (or unreadable) → treat slot as vanilla
//      per spec scenario "Absent sidecar is normal vanilla".
//   3. If slot.header.slot_kind != kSlotKind_Randomizer → spec scenario
//      "Slot-kind discriminator": treat as vanilla, ignore the rest of the
//      slot body.
//   4. Drift check (spec § "Downgrade-then-re-upgrade safety"): compute
//      RandoSave_ComputeSramSlotChecksum(<paired sram.dat slot bytes>, 0x500);
//      compare to slot.header.sram_slot_checksum_at_last_write. If different,
//      surface the drift warning + recovery prompt.
//   5. UI installs the placement table (translate slot.placements[] →
//      RandoPlacementTable + call Placement_Install) and sets
//      g_rando_slot_active = 1 + enhanced_features1 |= kFeatures1_RandomizerActive.
//   6. UI also calls Rando_SetSnapshotContext(slot.header.generator_version,
//      slot.header.settings_hash, slot.header.share_string) so the snapshot
//      tail-TLV emitter has the metadata it needs.
//
// This function does NOT mutate any rando-runtime state; it is a pure read.
// Returns false when the sidecar file is missing, malformed, slot_index is
// out of range, or the requested slot has bad magic.
// ---------------------------------------------------------------------------
bool Rando_LoadSidecarSlot(int slot_index, RandoSidecarSlot *out);

// ---------------------------------------------------------------------------
// Rando_WriteSidecarSlot (tasks.md §8.4).
//
// Writes slot `slot_index` of `saves/sram_rando.dat` with the supplied
// `in` slot data. Atomically commits via RandoSave_WriteFile (which already
// implements the §8.2 protocol: write .tmp → fflush → fsync/_commit →
// rename → dir-fsync).
//
// The function OVERRIDES two fields of the on-disk header before writing,
// regardless of what `in->header` carries for them:
//
//   - last_vanilla_write_version  ← kGeneratorVersion (current binary's)
//   - sram_slot_checksum_at_last_write ← RandoSave_ComputeSramSlotChecksum(
//       paired_sram_slot, paired_sram_slot_size)
//
// Per spec scenario "last_vanilla_write_version advances on every write" and
// the downgrade-drift-detection clause.
//
// Other slots in the sidecar are preserved: if the file already exists, we
// load it first and only the targeted slot is replaced. If the file does
// not exist, the untouched slots are initialized empty (slot_kind=0).
//
// `paired_sram_slot` MUST point at the caller's snapshot of the current
// in-memory paired sram.dat slot (typically `g_zenv.sram + slot_index * 0x500`,
// size 0x500). The caller is responsible for snapshotting these bytes at the
// same logical moment as the rando write — this is why the function takes
// them as a parameter rather than reading sram.dat itself.
//
// Save-order coordination (per spec § "Atomic-commit protocol" — sidecar
// first, then sram.dat): the UI sprint's call site issues
// Rando_WriteSidecarSlot BEFORE ZeldaWriteSram (or the equivalent sram.dat
// commit). A crash between the two writes leaves sram.dat matching the
// pre-write state; the sidecar's `sram_slot_checksum_at_last_write` will
// then differ from the actual sram.dat slot checksum on next load, and the
// drift warning fires per spec.
//
// Returns true on a successful atomic commit, false on any I/O or
// validation error (slot_index out of range, NULL buffers, RandoSave_WriteFile
// failure). On false, the on-disk file is in its prior state.
// ---------------------------------------------------------------------------
bool Rando_WriteSidecarSlot(int slot_index, const RandoSidecarSlot *in,
                            const uint8 *paired_sram_slot,
                            uint32 paired_sram_slot_size);

// ---------------------------------------------------------------------------
// Drift-detection helpers (tasks.md §8.5, §8.6; randomizer-save spec §
// "Embedded placement table — upgrade safety" + § "Downgrade-then-re-upgrade
// safety"). Pure functions; no I/O.
//
// Rando_DetectVersionDrift: true when the slot was written by a different
//   generator_version than the binary currently in use. Spec semantics: load
//   still succeeds (the embedded placement table is authoritative); the
//   loader surfaces an informational warning. The UI layer is responsible
//   for the actual warning surface; this helper is the detection primitive
//   the UI calls.
//
// Rando_DetectChecksumDrift: true when the paired sram.dat slot's current
//   checksum no longer matches the value the sidecar recorded at last write.
//   Indicates the paired sram.dat was edited outside the rando code path —
//   typically a downgraded binary writing the slot as vanilla. The UI uses
//   this to fire the "rando state may be stale" recovery prompt. NOTE: this
//   helper expects the same byte-size buffer that the writer hashed
//   (typically 0x500). A buffer smaller than 0x4fe returns false (no signal
//   — the checksum routine can't compute, so we don't claim drift).
// ---------------------------------------------------------------------------
bool Rando_DetectVersionDrift(const RandoSlotHeader *hdr,
                              uint16 current_generator_version);
bool Rando_DetectChecksumDrift(const RandoSlotHeader *hdr,
                               const uint8 *paired_sram_slot,
                               uint32 paired_sram_slot_size);

#endif  // ZELDA3_RANDO_SAVE_H_
