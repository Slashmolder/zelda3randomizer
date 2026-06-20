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
#include "rando_settings.h"  // kSettingsCanonicalLen (persisted settings blob)

// Magic prefix for both file header and per-slot header. "ZRSC" (Zelda Rando
// SideCar) in ASCII; little-endian read produces 0x4353525A. Distinct from the
// share-string magic "ZRSS" (rando_share.h).
#define kRandoSidecar_FileMagic  0x4353525A  // 'Z' 'R' 'S' 'C' on disk
#define kRandoSidecar_SlotMagic  0x53435253  // 'S' 'R' 'C' 'S' on disk

// Sidecar file constants.
//
// format_version history:
//   1 — original layout (header + flat placement table + checked bitmap).
//   2 — appends a per-slot canonical RandoSettings blob (kSettingsCanonicalLen
//       bytes) AFTER the checked bitmap, and a `settings_present` byte at slot
//       header @70. A v1 file has neither; the loader keys the blob's physical
//       presence on this file version (RandoSave_ReadFile), and old binaries
//       reading a v2 file would mis-size slots — but format_version gating in
//       both directions is the contract. Needed so a reloaded slot can
//       reproduce the seed's settings + shuffle assignments for the runtime
//       reachability (tracker) engine.
//   3 — appends a fixed kRandoSidecar_SlotExtV3Size-byte per-slot EXTENSION
//       block AFTER the settings blob (the 80-byte slot header is fully
//       claimed since door shuffle took @76-79, so additive fields now land
//       here). Carries entrance_digest24 (FIX #4 — the entrance-shuffle
//       analogue of door_digest24). v1/v2 files have no block; the loader
//       keys its presence on the file version and forces the fields to 0
//       (= legacy warn-only behavior). New writes are always v3.
#define kRandoSidecar_FileFormatVersion 3
#define kRandoSidecar_SlotCount         3       // mirrors sram.dat's 3-slot layout
#define kRandoSidecar_FileHeaderSize    16
#define kRandoSidecar_SlotHeaderSize    80
#define kRandoSidecar_ShareStringLength 32      // raw binary (rando_share writes 31 bytes + pad)

// format_version >= 3: per-slot extension block trailing the settings blob.
//   @0-2  entrance_digest24 (u24 LE)  (FIX #4; 0 = absent/no entrance shuffle)
//   @3-7  reserved (zero on write)
#define kRandoSidecar_SlotExtV3Size     8

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
//   @64 mushroom_held (u8)                   (rando Mushroom/Powder bitfield:
//                                             bit 0 = undelivered Mushroom,
//                                             bit 1 = Powder obtained)
//   @65 settings_ext_present (u8)            (Phase B hints; 1 = @66/@67 meaningful, 0 = unset)
//   @66 hints_setting (u8)                   (Phase B hints; RandoHintsMode: 0=off 1=on)
//   @67 goal (u8)                            (Phase B hints; Goal enum, rando_settings.h)
//   @68 world_state (u8)                      (Phase B Inverted runtime; WorldState enum,
//                                              rando_settings.h. Only meaningful when
//                                              settings_ext_present == 1. Carried additively so
//                                              slot-load knows it is an Inverted seed and can
//                                              grant Moon Pearl + Magic Mirror / start in the
//                                              Dark World. Older slots read 0 == Open == no-op.)
//   @69 flute_shovel_owned (u8)              (rando flute/shovel decouple bitfield:
//                                              0x01 shovel, 0x02 flute, 0x04 flute
//                                              activated; additive)
//   @70 settings_present (u8)                (format_version >= 2: 1 = the slot
//                                              body carries a valid canonical
//                                              RandoSettings blob; 0 = absent.
//                                              v1 files read 0 here = absent.)
//   @71 entrance_axes (u8)                    (Phase C entrance shuffle; packed
//                                              kEntranceAxis_* byte == canonical
//                                              [25]. 0 = no entrance shuffle.)
//   @72 entrance_attempt (u8)                  (Phase C; accepted goal-retry
//                                              attempt index used to regenerate
//                                              the cave permutation at slot load)
//   @73 boomerang_owned (u8)                   (rando boomerang decouple bitfield:
//                                              0x01 blue, 0x02 red; additive)
//   @74 bow_owned (u8)                         (rando bow decouple bitfield:
//                                              0x01 wood, 0x02 silver; additive)
//   @75 prize_attempt (u8)                     (FIX #6; accepted assumed-fill
//                                              attempt index whose per-attempt
//                                              seed produced the prize/medallion
//                                              shuffle baked into the placement
//                                              table. Re-applied at slot load so
//                                              the runtime falling-prize sprite /
//                                              OP_HAS_PRIZE tracker match the
//                                              stored table. 0 = attempt 0 =
//                                              legacy behavior; older slots read 0.)
//   @76 reserved[4]                            (forward-compat; zero on write)
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
  // Rando Mushroom/Powder ownership bitfield, stored at on-disk offset @64 (the first
  // byte of the former reserved[16] block; older binaries wrote it as zero).
  // Bit 0 is set while the player holds the Mushroom item but has not yet handed
  // it to the Witch. Bit 1 records Magic Powder ownership even when the shared
  // link_item_mushroom byte currently shows Mushroom. See Rando_MushroomHeld /
  // Rando_PowderOwned / Witch_AcceptShroom.
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
  // format_version >= 2: 1 when the slot body carries a valid canonical
  // RandoSettings blob (RandoSidecarSlot.settings_canonical). Lets slot-load
  // reproduce the seed's settings + prize/medallion shuffle assignments for the
  // runtime reachability engine. 0 (older slots, vanilla/empty slots, or a
  // writer that didn't populate it) means the loader must SUPPRESS reachability
  // rather than guess (a wrong prize_shuffle flag yields confidently-wrong
  // assignments). See Rando_RecoverActiveSettings / Rando_ActivateSidecarSlot.
  uint8 settings_present;       // @70
  // Phase C entrance shuffle, carried additively in the reserved tail (same
  // pattern as the hints ext): the packed entrance-axis byte (== canonical
  // settings byte [25]) and the accepted goal-retry attempt index. Together
  // with the seed (in share_string) they let slot-load REGENERATE the cave
  // permutation deterministically (no full-permutation storage / TLV needed).
  // 0 / 0 for non-entrance-shuffle slots, so older slots are a safe no-op.
  // (Relocated from @70/@71 to @71/@72 on the main merge, after main claimed @70
  // for settings_present.)
  uint8 entrance_axes;          // @71 (kEntranceAxis_* bits; 0 = no shuffle)
  uint8 entrance_attempt;       // @72 (cave-permutation goal-retry attempt index)
  // Boomerang/bow ownership bitfields, carried additively in the reserved tail
  // (older binaries wrote these as zero). The vanilla link_item_boomerang /
  // link_item_bow bytes are single slots that can't represent owning both tiers,
  // which rando shuffles as independent items. We persist true ownership here
  // and treat the byte as the currently-SELECTED tier (toggled in the item
  // menu). boomerang: 0x01 blue, 0x02 red. bow: 0x01 wood, 0x02 silver. See
  // kRandoBoomerang_* / kRandoBow_* / Rando_GrantBoomerang / Rando_GrantBow.
  uint8 boomerang_owned;        // @73
  uint8 bow_owned;              // @74
  // FIX #6 — accepted assumed-fill attempt index (@75), carried additively in
  // the reserved tail (same pattern as the entrance/boomerang/bow bytes). The
  // placer bakes a per-attempt prize/medallion shuffle into the table seeded
  // from base_seed ^ (attempt * 0x9E3779B97F4A7C15); persisting the accepted
  // attempt lets Rando_ActivateSidecarSlot re-derive the SAME assignments at
  // load instead of re-deriving from the base seed (= attempt 0), which would
  // desync the runtime falling-prize sprite + OP_HAS_PRIZE tracker from the
  // stored table whenever attempt != 0. Older slots / pre-field writers read 0
  // (== attempt 0 == legacy behavior, the XOR-with-0 identity).
  uint8 prize_attempt;          // @75
  // add-rando-door-shuffle (@76-79, claims the reserved[4] tail). The door
  // layout is NOT serialized — it regenerates from (base_seed, settings,
  // door_attempt) at activation. door_digest24 is a 24-bit fold of
  // DoorShuffle_LayoutDigest over the ACCEPTED layout; activation recomputes
  // it from the regenerated layout and HARD-FAILS on mismatch (a drifted
  // interior layout can make the certified-beatable placement unbeatable —
  // unlike entrance shuffle's non-blocking drift warning). Both zero on
  // vanilla-door slots / pre-field writers.
  uint8 door_attempt;           // @76
  uint32 door_digest24;         // @77-79 (3 bytes LE on disk)
  // FIX #4 — entrance-shuffle analogue of door_digest24. The entrance
  // permutation regenerates from (seed, entrance_axes, entrance_attempt) at
  // slot load; a generator change to the algorithm/pool silently installs a
  // DIFFERENT layout than the placement was certified against. This is the
  // 24-bit Rando_EntranceLayoutDigest24 fold over everything
  // Entrance_RuntimeInstall installs; Rando_ActivateSidecarSlot recomputes it
  // and refuses the slot on mismatch (same pathway as the door gate). 0 =
  // absent: non-entrance-shuffle slots, and v1/v2 sidecars (which physically
  // lack the field) — those keep the legacy warn-only version-drift behavior.
  // On disk it lives in the format_version-3 slot EXTENSION block (the 80-byte
  // header is full), bytes @0-2 LE; the in-memory struct carries it here.
  uint32 entrance_digest24;     // v3 ext block @0-2 (3 bytes LE on disk)
} RandoSlotHeader;

// Bitmap covers placement_table_size / 2 locations.
#define kRandoSlotFlag_ForwardFillUsed 0x01

// File header (16 bytes total):
//   @0  magic[4]                            (= kRandoSidecar_FileMagic, LE)
//   @4  format_version (u16 LE)             (= kRandoSidecar_FileFormatVersion)
//   @6  slot_count (u16 LE)                 (= 3)
//   @8  file_crc (u32 LE)                   (FIX #13: CRC-32 — poly 0xEDB88320,
//                                            init/xorout 0xFFFFFFFF, same
//                                            algorithm as util.c's BPS crc32 —
//                                            over ALL bytes after this 16-byte
//                                            header. 0 = legacy file (written
//                                            as 0 before the FIX): loader
//                                            accepts without verification.)
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
  // In-memory only — sized by the module-wide ceiling (rando_logic.h). The
  // ON-DISK format is sized per slot by header.placement_table_size, so growing
  // these buffers does NOT change the wire format. The slot-load bounds check in
  // rando_save.c (location_count > sizeof(placements)/sizeof(placements[0]))
  // refuses a slot too large for the buffer — which makes compatibility
  // one-directional for free: a pot-capable binary accepts ≤ capacity, while an
  // older (512-buffer) binary refuses a pot-expanded slot non-destructively.
  RandoPlacement placements[kRandoLocationCapacity];
  uint16 placement_count;          // valid entries in placements[]
  uint8 checked_bitmap[(kRandoLocationCapacity + 7) >> 3];
  // Canonical RandoSettings blob (format_version >= 2). Valid only when
  // header.settings_present == 1. On disk it trails the checked bitmap; v1
  // files have no such bytes (the loader keys presence on the file version).
  uint8 settings_canonical[kSettingsCanonicalLen];
} RandoSidecarSlot;

// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------

// Compute the on-disk byte size of one slot given its placement_table_size,
// for the CURRENT format (version 3). placement_table_size is in BYTES (= 2 ×
// location count). Total = 80 (header) + placement_table_size +
// ((placement_table_size/2 + 7) >> 3) bitmap + kSettingsCanonicalLen (settings
// blob) + kRandoSidecar_SlotExtV3Size (extension block). v1 files omit the
// blob and the ext block, v2 files omit the ext block; RandoSave_ReadFile
// handles those older layouts internally based on the file's format_version.
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

// ---------------------------------------------------------------------------
// Rando_EntranceLayoutDigest24 (FIX #4) — 24-bit digest of the FULL entrance
// layout that hdr's (entrance_axes, world_state, share-string seed,
// entrance_attempt) regenerate: every active permutation (cave / dungeon /
// cross / all decoupled exit nets) plus the built door overlay. Implemented in
// rando.c beside Entrance_RuntimeInstall — installer and digest share ONE
// layout computation, so any algorithm/pool drift that changes what installs
// changes the digest. Returns 0 when no entrance mode is active (or the door
// table is unavailable); a nonzero fold of 0 is remapped to 1 so 0 stays
// unambiguous as "absent". Generation stores the result in the slot header
// (sidecar v3 ext block); activation recomputes and refuses on mismatch.
// ---------------------------------------------------------------------------
uint32 Rando_EntranceLayoutDigest24(const RandoSlotHeader *hdr);

#endif  // ZELDA3_RANDO_SAVE_H_
