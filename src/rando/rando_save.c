// rando_save.c — sidecar save serialization + file IO (tasks.md §8.1, §8.2).
//
// Byte-layout discipline: every multi-byte field is written/read with explicit
// little-endian helpers. Compiler padding / struct layout is NOT relied upon
// for the on-disk format.
//
// Phase A0 status: serialization + round-trip read/write tested via
// RandoSave_SelfCheck. The atomic-commit protocol (fsync, dir-fsync) is
// stubbed — Phase A1 adds the per-OS calls.

// Windows headers must come BEFORE "../types.h" because types.h defines
// BYTE / WORD / DWORD as function-style macros that conflict with the same
// names as typedefs in winbase.h. Including windows.h first lets it lay
// down the typedefs; types.h's macros then shadow them — the macros are
// fine for project use, but the windows.h declarations of MoveFileExA etc.
// need the typedef to be the typedef, not a macro.
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>  // MoveFileExA, BOOL
#include <io.h>       // _commit, _fileno
#endif

#include "rando_save.h"
#include "rando.h"
#include "../types.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The persisted settings blob must be exactly kSettingsCanonicalLen bytes (the
// canonical serializer's output size) — couples the slot field to the codec so
// a canonical-length change can never silently truncate/overrun it. Mirrors the
// ZRSR _Static_assert (rando_spoiler.h). See [[canonical-size-coupling]].
_Static_assert(sizeof(((RandoSidecarSlot *)0)->settings_canonical) == kSettingsCanonicalLen,
               "RandoSidecarSlot.settings_canonical size must equal kSettingsCanonicalLen");

// Atomic-commit protocol (tasks.md §8.2 / design.md D12).
// POSIX: fsync the file descriptor, then rename, then fsync the containing dir.
// Windows: _commit on the file descriptor, then MoveFileEx with REPLACE_EXISTING.
#if !defined(_WIN32)
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <libgen.h>
#endif

// ---------------------------------------------------------------------------
// LE byte helpers
// ---------------------------------------------------------------------------
static void put_u16le(uint8 *p, uint16 v) {
  p[0] = (uint8)v;
  p[1] = (uint8)(v >> 8);
}

static void put_u32le(uint8 *p, uint32 v) {
  p[0] = (uint8)v;
  p[1] = (uint8)(v >> 8);
  p[2] = (uint8)(v >> 16);
  p[3] = (uint8)(v >> 24);
}

static uint16 get_u16le(const uint8 *p) {
  return (uint16)p[0] | ((uint16)p[1] << 8);
}

static uint32 get_u32le(const uint8 *p) {
  return (uint32)p[0] | ((uint32)p[1] << 8) | ((uint32)p[2] << 16) | ((uint32)p[3] << 24);
}

// ---------------------------------------------------------------------------
// On-disk sizing
//
// Per spec (randomizer-save § Slot header): `placement_table_size` is the
// byte size of the embedded placement table (= 2 × location_count, since
// each location takes a uint16 LE). The bitmap covers `location_count` bits.
// ---------------------------------------------------------------------------
// Base (version-1) on-disk size: header + flat placement table + bitmap, with
// no trailing settings blob. Internal — the version-aware paths add the blob.
static uint32 slot_on_disk_size_base(uint16 placement_table_size) {
  uint32 placements_bytes = (uint32)placement_table_size;
  uint32 location_count = (uint32)placement_table_size / 2;
  uint32 bitmap_bytes = (location_count + 7) >> 3;
  return kRandoSidecar_SlotHeaderSize + placements_bytes + bitmap_bytes;
}

// Public size is the CURRENT format (version 2): base + the settings blob.
uint32 RandoSave_SlotOnDiskSize(uint16 placement_table_size) {
  return slot_on_disk_size_base(placement_table_size) + kSettingsCanonicalLen;
}

// ---------------------------------------------------------------------------
// File header
// ---------------------------------------------------------------------------
uint32 RandoSave_SerializeFileHeader(const RandoSidecarFileHeader *h,
                                     uint8 *buf, uint32 buf_size) {
  if (h == NULL || buf == NULL || buf_size < kRandoSidecar_FileHeaderSize) return 0;
  put_u32le(buf + 0, kRandoSidecar_FileMagic);
  put_u16le(buf + 4, h->format_version);
  put_u16le(buf + 6, h->slot_count);
  put_u32le(buf + 8, h->file_crc);
  // reserved[4] @12 — zero on write.
  buf[12] = buf[13] = buf[14] = buf[15] = 0;
  return kRandoSidecar_FileHeaderSize;
}

uint32 RandoSave_DeserializeFileHeader(const uint8 *buf, uint32 buf_size,
                                       RandoSidecarFileHeader *out) {
  if (buf == NULL || out == NULL || buf_size < kRandoSidecar_FileHeaderSize) return 0;
  uint32 magic = get_u32le(buf + 0);
  if (magic != kRandoSidecar_FileMagic) return 0;
  out->format_version = get_u16le(buf + 4);
  out->slot_count = get_u16le(buf + 6);
  out->file_crc = get_u32le(buf + 8);
  // reserved bytes ignored
  return kRandoSidecar_FileHeaderSize;
}

// ---------------------------------------------------------------------------
// Slot header
// ---------------------------------------------------------------------------
static uint32 serialize_slot_header(const RandoSlotHeader *h, uint8 *buf) {
  // @0 magic[4]
  put_u32le(buf + 0, kRandoSidecar_SlotMagic);
  // @4 slot_kind
  buf[4] = h->slot_kind;
  // @5 generator_version (u16 LE)
  put_u16le(buf + 5, h->generator_version);
  // @7 settings_hash[16]
  memcpy(buf + 7, h->settings_hash, 16);
  // @23 share_string[32]
  memcpy(buf + 23, h->share_string, kRandoSidecar_ShareStringLength);
  // @55 last_vanilla_write_version (u16 LE)
  put_u16le(buf + 55, h->last_vanilla_write_version);
  // @57 sram_slot_checksum_at_last_write (u32 LE)
  put_u32le(buf + 57, h->sram_slot_checksum_at_last_write);
  // @61 placement_table_size (u16 LE)
  put_u16le(buf + 61, h->placement_table_size);
  // @63 flags
  buf[63] = h->flags;
  // @64 mushroom_held (rando Mushroom-possession).
  buf[64] = h->mushroom_held;
  // @65-67 Phase B hints settings extension (additive; previously zero). The
  // generator only reads `hints` and `goal` from RandoSettings, so those two
  // axes are all the reserved tail needs to carry to regenerate hints at slot
  // load. @68-79 still zero on write.
  buf[65] = h->settings_ext_present;
  buf[66] = h->hints_setting;
  buf[67] = h->goal;
  // @68 world_state (Phase B Inverted runtime). Additive; previously zero, so
  // old binaries reading a new file see @68 as "reserved" and ignore it, and
  // new binaries reading an old file see 0 == kWorldState_Open (safe no-op).
  buf[68] = h->world_state;
  // @69 flute_shovel_owned (rando flute/shovel decouple). Additive; previously
  // zero, so old binaries reading a new file ignore it and new binaries reading
  // an old file see 0 (own neither — the safe default).
  buf[69] = h->flute_shovel_owned;
  // @70 settings_present (format_version >= 2). Whether the slot body carries a
  // valid canonical RandoSettings blob. v1 readers see this as a reserved zero.
  buf[70] = h->settings_present;
  // @71-72 Phase C entrance shuffle (additive; previously zero). The packed
  // entrance-axis byte + accepted goal-retry attempt let slot-load regenerate
  // the cave permutation from the seed. 0/0 for non-shuffle slots. (Relocated
  // from @70/@71 on the main merge after main took @70 for settings_present.)
  buf[71] = h->entrance_axes;
  buf[72] = h->entrance_attempt;
  // @73-74 boomerang/bow ownership (additive; previously zero). Lets a
  // swapped-down byte keep the higher tier across save/reload. Pre-field readers
  // see these in the reserved tail and ignore them; new readers of an old file
  // see 0 (own neither — the safe default). See Rando_GrantBoomerang/Bow.
  buf[73] = h->boomerang_owned;
  buf[74] = h->bow_owned;
  // @75 prize_attempt (FIX #6; additive, previously zero). The accepted assumed-
  // fill attempt index used to re-derive the prize/medallion shuffle at slot
  // load. Pre-field readers see it in the reserved tail and ignore it; new
  // readers of an old file see 0 (== attempt 0 == legacy behavior).
  buf[75] = h->prize_attempt;
  // @76-79 door shuffle (claims the reserved tail): accepted door_attempt +
  // 24-bit layout digest for the activation drift hard-fail. Zero on
  // vanilla-door slots (== the old reserved zeros).
  buf[76] = h->door_attempt;
  buf[77] = (uint8)(h->door_digest24 & 0xff);
  buf[78] = (uint8)((h->door_digest24 >> 8) & 0xff);
  buf[79] = (uint8)((h->door_digest24 >> 16) & 0xff);
  return kRandoSidecar_SlotHeaderSize;
}

static uint32 deserialize_slot_header(const uint8 *buf, uint32 buf_size, RandoSlotHeader *out) {
  if (buf_size < kRandoSidecar_SlotHeaderSize) return 0;
  uint32 magic = get_u32le(buf + 0);
  if (magic != kRandoSidecar_SlotMagic) return 0;
  out->slot_kind = buf[4];
  out->generator_version = get_u16le(buf + 5);
  memcpy(out->settings_hash, buf + 7, 16);
  memcpy(out->share_string, buf + 23, kRandoSidecar_ShareStringLength);
  out->last_vanilla_write_version = get_u16le(buf + 55);
  out->sram_slot_checksum_at_last_write = get_u32le(buf + 57);
  out->placement_table_size = get_u16le(buf + 61);
  out->flags = buf[63];
  out->mushroom_held = buf[64];
  // Phase B hints settings extension (@65-67). A file written before this
  // field existed has zero here -> settings_ext_present == 0 -> the loader
  // (Rando_ActivateSidecarSlot) applies the hints-on default.
  out->settings_ext_present = buf[65];
  out->hints_setting = buf[66];
  out->goal = buf[67];
  // @68 world_state (Phase B Inverted runtime). Pre-field files read 0 here
  // (== kWorldState_Open), the safe no-op default.
  out->world_state = buf[68];
  // @69 flute_shovel_owned (rando flute/shovel decouple). Pre-field files read
  // 0 here (own neither), the safe default.
  out->flute_shovel_owned = buf[69];
  // @70 settings_present (format_version >= 2). v1 files read 0 (absent). The
  // version-aware body deserializer additionally forces this to 0 when the file
  // has no trailing blob, so a stray nonzero byte in a v1 file can't mislead.
  out->settings_present = buf[70];
  // @71-72 Phase C entrance shuffle. Pre-field files read 0/0 (no shuffle), the
  // safe no-op default.
  out->entrance_axes = buf[71];
  out->entrance_attempt = buf[72];
  // @73-74 boomerang/bow ownership (rando decouple). Pre-field files read 0/0
  // (own neither), the safe default.
  out->boomerang_owned = buf[73];
  out->bow_owned = buf[74];
  // @75 prize_attempt (FIX #6). Pre-field files read 0 here (== attempt 0 ==
  // legacy behavior), the safe no-op default.
  out->prize_attempt = buf[75];
  // @76-79 door shuffle. Pre-field files read zeros (vanilla doors, digest 0).
  out->door_attempt = buf[76];
  out->door_digest24 = (uint32)buf[77] | ((uint32)buf[78] << 8) | ((uint32)buf[79] << 16);
  // remaining reserved bytes ignored — forward-compat
  return kRandoSidecar_SlotHeaderSize;
}

// ---------------------------------------------------------------------------
// Slot body (placements + bitmap)
//
// Per spec: the on-disk embedded placement table is a FLAT uint16[] indexed
// by location_id. Each slot at index k holds the item_id placed at
// location_id k, or 0xFFFF as the "no placement" sentinel. The in-memory
// `placements[]` array is a sparse (location, item) list — we translate
// between the two representations here.
// ---------------------------------------------------------------------------
uint32 RandoSave_SerializeSlot(const RandoSidecarSlot *slot, uint8 *buf, uint32 buf_size) {
  if (slot == NULL || buf == NULL) return 0;
  uint32 size = RandoSave_SlotOnDiskSize(slot->header.placement_table_size);
  if (buf_size < size) return 0;
  uint32 location_count = (uint32)slot->header.placement_table_size / 2;
  serialize_slot_header(&slot->header, buf);
  uint8 *p = buf + kRandoSidecar_SlotHeaderSize;
  // Initialize the flat table to the no-placement sentinel.
  for (uint32 i = 0; i < location_count; i++) {
    put_u16le(p + i * 2, kRandoSidecar_NoPlacementSentinel);
  }
  // Scatter the sparse placements[] entries into the flat array. Entries with
  // location_id >= location_count are ignored (they would lose data on
  // round-trip — caller should size placement_table_size for the max id in
  // use, which is what we do at write time).
  uint16 sparse_count = slot->placement_count;
  if (sparse_count > (sizeof(slot->placements) / sizeof(slot->placements[0]))) {
    sparse_count = (uint16)(sizeof(slot->placements) / sizeof(slot->placements[0]));
  }
  for (uint16 i = 0; i < sparse_count; i++) {
    uint16 loc = slot->placements[i].location_id;
    if ((uint32)loc < location_count) {
      put_u16le(p + (uint32)loc * 2, slot->placements[i].item_id);
    }
  }
  p += location_count * 2;
  uint32 bitmap_bytes = (location_count + 7) >> 3;
  memcpy(p, slot->checked_bitmap, bitmap_bytes);
  p += bitmap_bytes;
  // format_version 2: canonical settings blob trails the bitmap. Always written
  // (zeroed when settings_present == 0); RandoSave_SlotOnDiskSize accounts for it.
  memcpy(p, slot->settings_canonical, kSettingsCanonicalLen);
  return size;
}

// Version-aware slot deserialize. `with_settings` is true for the current
// (version-2) layout, which has a trailing canonical settings blob; false for
// the older version-1 layout (no blob). RandoSave_ReadFile passes the value
// derived from the file's format_version.
static uint32 deserialize_slot_versioned(const uint8 *buf, uint32 buf_size,
                                         RandoSidecarSlot *out, bool with_settings) {
  if (buf == NULL || out == NULL) return 0;
  memset(out, 0, sizeof(*out));
  uint32 hdr_used = deserialize_slot_header(buf, buf_size, &out->header);
  if (hdr_used == 0) return 0;
  uint32 location_count = (uint32)out->header.placement_table_size / 2;
  // Sanity-check against the in-memory bound — we cannot store more sparse
  // pairs than the static array can hold.
  if (location_count > (sizeof(out->placements) / sizeof(out->placements[0]))) {
    return 0;
  }
  uint32 total = with_settings
                     ? RandoSave_SlotOnDiskSize(out->header.placement_table_size)
                     : slot_on_disk_size_base(out->header.placement_table_size);
  if (buf_size < total) return 0;
  const uint8 *p = buf + kRandoSidecar_SlotHeaderSize;
  // Gather the flat array back into the sparse in-memory list. Sentinel
  // entries (item_id == 0xFFFF) are skipped — the dispatcher's fall-back
  // path handles those.
  uint16 sparse_count = 0;
  for (uint32 i = 0; i < location_count; i++) {
    uint16 item_id = get_u16le(p + i * 2);
    if (item_id == kRandoSidecar_NoPlacementSentinel) continue;
    out->placements[sparse_count].location_id = (uint16)i;
    out->placements[sparse_count].item_id = item_id;
    sparse_count++;
  }
  out->placement_count = sparse_count;
  p += location_count * 2;
  uint32 bitmap_bytes = (location_count + 7) >> 3;
  // The location_count cap above (== placements[] length) is sized so the
  // checked-bitmap always covers it; assert that coupling so the two can't
  // drift apart silently. Fail CLOSED if a slot ever claims more bits than the
  // bitmap holds — better to reject the slot than load it with checks cleared.
  _Static_assert((sizeof(out->placements) / sizeof(out->placements[0]))
                     <= sizeof(out->checked_bitmap) * 8,
                 "checked_bitmap must cover every placement slot's location bit");
  if (bitmap_bytes > sizeof(out->checked_bitmap)) return 0;
  memcpy(out->checked_bitmap, p, bitmap_bytes);
  p += bitmap_bytes;
  if (with_settings) {
    memcpy(out->settings_canonical, p, kSettingsCanonicalLen);
  } else {
    // A v1 file physically has no blob — force settings_present off so a stray
    // @70 byte can't be mistaken for a (nonexistent) valid blob.
    out->header.settings_present = 0;
  }
  return total;
}

uint32 RandoSave_DeserializeSlot(const uint8 *buf, uint32 buf_size, RandoSidecarSlot *out) {
  // Public entry assumes the current (version-2) layout. RandoSave_ReadFile
  // uses the version-aware static directly for older files.
  return deserialize_slot_versioned(buf, buf_size, out, true);
}

// ---------------------------------------------------------------------------
// Atomic-commit file write (tasks.md §8.2 / design.md D12).
//
// Protocol:
//   1. fopen `<path>.tmp` for writing, fwrite the payload, fflush.
//   2. fsync (POSIX) / _commit (Windows) the file descriptor — ensures the
//      bytes hit the storage device before the rename.
//   3. fclose.
//   4. rename `<path>.tmp` → `<path>` atomically. On POSIX `rename(2)` is
//      atomic over an existing target. On Windows we use MoveFileExA with
//      MOVEFILE_REPLACE_EXISTING for the same atomicity property (best-effort
//      — Windows atomicity guarantees are weaker than POSIX, but the
//      MoveFileEx-replace semantics are the strongest tool available).
//   5. fsync the containing directory (POSIX) — ensures the directory entry's
//      new name is durable. Skipped on Windows (no equivalent; the
//      MoveFileEx already guarantees the rename is visible).
//
// On any I/O error in steps 1-3, the .tmp file is removed and we return false
// without touching the final path — the prior `<path>` (if any) is untouched.
// ---------------------------------------------------------------------------

static bool atomic_write_and_commit(const char *final_path,
                                    const uint8 *buf, uint32 total) {
  size_t plen = strlen(final_path);
  char *tmp_path = (char *)malloc(plen + 5);
  if (tmp_path == NULL) return false;
  memcpy(tmp_path, final_path, plen);
  strcpy(tmp_path + plen, ".tmp");

  FILE *f = fopen(tmp_path, "wb");
  if (f == NULL) { free(tmp_path); return false; }
  size_t wrote = fwrite(buf, 1, total, f);
  if (wrote != total) {
    fclose(f);
    remove(tmp_path);
    free(tmp_path);
    return false;
  }
  if (fflush(f) != 0) {
    fclose(f);
    remove(tmp_path);
    free(tmp_path);
    return false;
  }
#if defined(_WIN32)
  // _commit equivalent of fsync on Windows.
  if (_commit(_fileno(f)) != 0) {
    fclose(f);
    remove(tmp_path);
    free(tmp_path);
    return false;
  }
#else
  if (fsync(fileno(f)) != 0) {
    fclose(f);
    remove(tmp_path);
    free(tmp_path);
    return false;
  }
#endif
  if (fclose(f) != 0) {
    remove(tmp_path);
    free(tmp_path);
    return false;
  }

  // Atomic rename.
#if defined(_WIN32)
  // MoveFileExA with MOVEFILE_REPLACE_EXISTING is the closest Windows
  // analogue to POSIX rename(2) atomicity. Fails if the source doesn't exist
  // or the target is locked.
  BOOL ok = MoveFileExA(tmp_path, final_path,
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
  if (!ok) {
    remove(tmp_path);
    free(tmp_path);
    return false;
  }
#else
  if (rename(tmp_path, final_path) != 0) {
    remove(tmp_path);
    free(tmp_path);
    return false;
  }
  // fsync the containing directory so the rename's directory entry is
  // durable. POSIX-only; Windows MoveFileEx handles this internally.
  {
    char *path_copy = strdup(final_path);
    if (path_copy != NULL) {
      const char *dir = dirname(path_copy);
      int dirfd = open(dir, O_RDONLY);
      if (dirfd >= 0) {
        fsync(dirfd);
        close(dirfd);
      }
      free(path_copy);
    }
  }
#endif
  free(tmp_path);
  return true;
}

bool RandoSave_WriteFile(const char *path,
                         const RandoSidecarSlot slots[kRandoSidecar_SlotCount]) {
  if (path == NULL) return false;
  // Compute total size.
  uint32 total = kRandoSidecar_FileHeaderSize;
  for (uint8 i = 0; i < kRandoSidecar_SlotCount; i++) {
    total += RandoSave_SlotOnDiskSize(slots[i].header.placement_table_size);
  }
  uint8 *buf = (uint8 *)malloc(total);
  if (buf == NULL) return false;

  RandoSidecarFileHeader fh = { kRandoSidecar_FileFormatVersion, kRandoSidecar_SlotCount, 0 };
  uint32 off = RandoSave_SerializeFileHeader(&fh, buf, total);
  for (uint8 i = 0; i < kRandoSidecar_SlotCount; i++) {
    off += RandoSave_SerializeSlot(&slots[i], buf + off, total - off);
  }

  bool ok = atomic_write_and_commit(path, buf, total);
  free(buf);
  return ok;
}

bool RandoSave_ReadFile(const char *path,
                        RandoSidecarSlot out_slots[kRandoSidecar_SlotCount]) {
  if (path == NULL) return false;
  FILE *f = fopen(path, "rb");
  if (f == NULL) return false;
  fseek(f, 0, SEEK_END);
  long fsize = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (fsize < (long)kRandoSidecar_FileHeaderSize) { fclose(f); return false; }
  uint8 *buf = (uint8 *)malloc((size_t)fsize);
  if (buf == NULL) { fclose(f); return false; }
  size_t read_bytes = fread(buf, 1, (size_t)fsize, f);
  fclose(f);
  if (read_bytes != (size_t)fsize) { free(buf); return false; }

  RandoSidecarFileHeader fh;
  uint32 hdr_used = RandoSave_DeserializeFileHeader(buf, (uint32)fsize, &fh);
  if (hdr_used == 0 || fh.slot_count != kRandoSidecar_SlotCount) { free(buf); return false; }

  // Slots in a version-2 file carry a trailing settings blob; version-1 files
  // do not. Key the body layout on the file's declared format_version so an old
  // (v1) sidecar still loads correctly (its slots are 28 bytes shorter).
  bool with_settings = (fh.format_version >= 2);
  uint32 off = hdr_used;
  for (uint8 i = 0; i < kRandoSidecar_SlotCount; i++) {
    uint32 used = deserialize_slot_versioned(buf + off, (uint32)fsize - off,
                                             &out_slots[i], with_settings);
    if (used == 0) { free(buf); return false; }
    off += used;
  }
  free(buf);
  return true;
}

// ---------------------------------------------------------------------------
// Per-slot SRAM checksum (tasks.md §8.4).
//
// Mirrors `SaveGameFile`'s per-slot checksum:
//   t = 0x5a5a;
//   for (i = 0; i < 0x4fe; i += 2) t -= *(uint16 *)(slot_bytes + i);
//
// We re-implement byte-by-byte from `slot_bytes` (rather than #include
// messaging.c's table) because messaging.c reads a `save_dung_info`-typed
// pointer that's tied to game state. The arithmetic is identical: read u16
// LE pairs from offset 0..0x4fe in 2-byte stride, subtract from 0x5a5a.
// ---------------------------------------------------------------------------
uint32 RandoSave_ComputeSramSlotChecksum(const uint8 *slot_bytes, uint32 size) {
  if (slot_bytes == NULL || size < 0x4fe) return 0;
  uint16 t = 0x5a5a;
  for (uint32 i = 0; i < 0x4fe; i += 2) {
    // Vanilla game writes save_dung_info as a uint16-aligned buffer; we read
    // bytes in LE order to be host-endianness-independent.
    uint16 w = (uint16)slot_bytes[i] | ((uint16)slot_bytes[i + 1] << 8);
    t = (uint16)(t - w);
  }
  return (uint32)t;
}

// ---------------------------------------------------------------------------
// Rando_LoadSidecarSlot — pure read of saves/sram_rando.dat into `*out`.
// ---------------------------------------------------------------------------
bool Rando_LoadSidecarSlot(int slot_index, RandoSidecarSlot *out) {
  if (out == NULL) return false;
  if (slot_index < 0 || slot_index >= (int)kRandoSidecar_SlotCount) return false;

  RandoSidecarSlot all_slots[kRandoSidecar_SlotCount];
  if (!RandoSave_ReadFile("saves/sram_rando.dat", all_slots)) return false;

  // Sanity-check the requested slot's bounds before copying out — the
  // deserializer already validates buf sizes & magic, but defense-in-depth
  // catches a corrupted file that somehow parsed (deserialize returns
  // non-zero on header.placement_table_size that fits the array bound).
  const RandoSidecarSlot *src = &all_slots[slot_index];
  uint32 loc_count = (uint32)src->header.placement_table_size / 2u;
  if (loc_count > (sizeof(src->placements) / sizeof(src->placements[0]))) {
    return false;
  }
  *out = *src;
  return true;
}

// ---------------------------------------------------------------------------
// Rando_WriteSidecarSlot — atomic write of saves/sram_rando.dat.
// ---------------------------------------------------------------------------
bool Rando_WriteSidecarSlot(int slot_index, const RandoSidecarSlot *in,
                            const uint8 *paired_sram_slot,
                            uint32 paired_sram_slot_size) {
  if (in == NULL || paired_sram_slot == NULL) return false;
  if (slot_index < 0 || slot_index >= (int)kRandoSidecar_SlotCount) return false;

  // Read existing file (if any) so we don't clobber the other slots.
  // CRITICAL: zero-init is safe ONLY if the file genuinely does not exist
  // yet (first-write case). If the file exists but parsing failed (truncation,
  // corruption, partial write from a crash mid-commit), zero-initializing
  // would silently destroy the other two slots. Distinguish ENOENT from
  // parse-error by probing the file's existence directly before deciding.
  RandoSidecarSlot all_slots[kRandoSidecar_SlotCount];
  bool file_exists = false;
  {
    FILE *probe = fopen("saves/sram_rando.dat", "rb");
    if (probe != NULL) { file_exists = true; fclose(probe); }
  }
  if (file_exists) {
    if (!RandoSave_ReadFile("saves/sram_rando.dat", all_slots)) {
      // File exists but failed to parse. Refuse the write — overwriting
      // would destroy the corrupted-but-recoverable bytes. Back up the
      // bad file under a .bad suffix (best-effort; rename failure is
      // non-fatal — the original stays on disk) so the user / a recovery
      // tool can inspect it.
      char bak_path[] = "saves/sram_rando.dat.bad";
      remove(bak_path);
      rename("saves/sram_rando.dat", bak_path);
      return false;
    }
  } else {
    // Genuine first-write — initialize all slots empty. The serializer
    // still writes a valid header per slot.
    memset(all_slots, 0, sizeof(all_slots));
  }

  // Compose the slot to write: caller-supplied data with two fields
  // overridden per spec.
  RandoSidecarSlot s = *in;
  s.header.last_vanilla_write_version = (uint16)kGeneratorVersion;
  s.header.sram_slot_checksum_at_last_write =
      RandoSave_ComputeSramSlotChecksum(paired_sram_slot, paired_sram_slot_size);

  // Defensive: ensure placement_count and placement_table_size are
  // consistent with the in-memory sparse list bounds. If
  // placement_table_size implies more locations than the static array can
  // hold, refuse — caller-side bug.
  uint32 loc_count = (uint32)s.header.placement_table_size / 2u;
  if (loc_count > (sizeof(s.placements) / sizeof(s.placements[0]))) {
    return false;
  }

  all_slots[slot_index] = s;

  return RandoSave_WriteFile("saves/sram_rando.dat", all_slots);
}

// ---------------------------------------------------------------------------
// Drift-detection helpers (tasks.md §8.5, §8.6). Pure; no I/O.
//
// NULL-input policy: both helpers return false ("no drift signal") on NULL
// inputs. This is intentional — the load path that consults these only does
// so AFTER `Rando_LoadSidecarSlot` returned true (which guarantees the
// header is populated and a paired sram slot is available). A NULL on
// either input therefore means the caller already detected a load failure
// upstream and is calling these helpers in a defensive path that doesn't
// need a drift warning on top — the load failure itself is the signal.
// Callers MUST NOT rely on these helpers to detect missing data; they only
// compare two pieces of already-loaded state.
// ---------------------------------------------------------------------------
bool Rando_DetectVersionDrift(const RandoSlotHeader *hdr,
                              uint16 current_generator_version) {
  if (hdr == NULL) return false;
  return hdr->generator_version != current_generator_version;
}

bool Rando_DetectChecksumDrift(const RandoSlotHeader *hdr,
                               const uint8 *paired_sram_slot,
                               uint32 paired_sram_slot_size) {
  if (hdr == NULL || paired_sram_slot == NULL) return false;
  // Smaller-than-0x4fe buffers can't be checksummed; treat as "no signal".
  // (A truncated sram.dat is a load-path failure detected upstream; this
  // helper isn't the place to surface it as drift.)
  if (paired_sram_slot_size < 0x4fe) return false;
  uint32 current = RandoSave_ComputeSramSlotChecksum(paired_sram_slot,
                                                     paired_sram_slot_size);
  return current != hdr->sram_slot_checksum_at_last_write;
}

// ---------------------------------------------------------------------------
// Self-check
// ---------------------------------------------------------------------------
static void selfcheck_die(const char *msg) {
  fprintf(stderr, "[RandoSave_SelfCheck] FAIL: %s\n", msg);
  exit(2);
}

void RandoSave_SelfCheck(void) {
  // Round-trip one slot via serialize → deserialize.
  RandoSidecarSlot src, dst;
  memset(&src, 0, sizeof(src));
  memset(&dst, 0, sizeof(dst));

  src.header.slot_kind = kSlotKind_Randomizer;
  src.header.generator_version = 0x1234;
  for (int i = 0; i < 16; i++) src.header.settings_hash[i] = (uint8)(i * 17);
  for (int i = 0; i < 32; i++) src.header.share_string[i] = (uint8)('A' + (i % 26));
  src.header.last_vanilla_write_version = 0xABCD;
  src.header.sram_slot_checksum_at_last_write = 0xDEADBEEF;
  // placement_table_size is in BYTES per spec. We want 3 placements at
  // location_ids 5, 10, 20 — so we need a flat table covering loc_ids 0..20
  // (21 entries × 2 bytes = 42 bytes).
  src.header.placement_table_size = 42;
  src.header.flags = 0x42;
  src.header.mushroom_held = 0x01;
  // Phase B hints settings extension round-trip coverage.
  src.header.settings_ext_present = 1;
  src.header.hints_setting = 1;   // kHintsMode_On
  src.header.goal = 4;            // kGoal_TriforceHunt
  src.header.world_state = 2;     // kWorldState_Inverted
  src.header.flute_shovel_owned = 0x05;  // shovel + flute-active (distinct from mushroom 0x01)
  // format_version 2: canonical settings blob round-trip coverage.
  src.header.settings_present = 1;
  for (int i = 0; i < kSettingsCanonicalLen; i++) src.settings_canonical[i] = (uint8)(0xC0 + i);
  // Phase C entrance shuffle round-trip coverage (@71/@72).
  src.header.entrance_axes = 0x05;       // cave + coupled bits (distinct value)
  src.header.entrance_attempt = 0x03;
  // Boomerang/bow ownership round-trip coverage (@73/@74).
  src.header.boomerang_owned = 0x03;     // blue + red
  src.header.bow_owned = 0x03;           // wood + silver
  // FIX #6 prize/medallion accepted-attempt round-trip coverage (@75).
  src.header.prize_attempt = 0x07;       // distinct non-zero attempt index
  src.placements[0].location_id = 5;  src.placements[0].item_id = 50;
  src.placements[1].location_id = 10; src.placements[1].item_id = 75;
  src.placements[2].location_id = 20; src.placements[2].item_id = 99;
  src.placement_count = 3;
  src.checked_bitmap[0] = 0x05;  // bits 0 and 2 set

  uint8 buf[256];
  uint32 wrote = RandoSave_SerializeSlot(&src, buf, sizeof(buf));
  if (wrote == 0) selfcheck_die("serialize returned 0");
  if (wrote != RandoSave_SlotOnDiskSize(src.header.placement_table_size)) {
    selfcheck_die("serialize size mismatch");
  }

  // Spot-check byte layout per spec offsets.
  if (get_u32le(buf + 0) != kRandoSidecar_SlotMagic) selfcheck_die("slot magic wrong");
  if (buf[4] != kSlotKind_Randomizer) selfcheck_die("slot_kind at @4 wrong");
  if (get_u16le(buf + 5) != 0x1234) selfcheck_die("generator_version at @5 wrong");
  if (get_u16le(buf + 55) != 0xABCD) selfcheck_die("last_vanilla_write_version at @55 wrong");
  if (get_u32le(buf + 57) != 0xDEADBEEF) selfcheck_die("sram_checksum at @57 wrong");
  if (get_u16le(buf + 61) != 42) selfcheck_die("placement_table_size at @61 wrong");
  if (buf[63] != 0x42) selfcheck_die("flags at @63 wrong");
  if (buf[64] != 0x01) selfcheck_die("mushroom_held at @64 wrong");
  // Phase B hints settings extension byte layout @65-67.
  if (buf[65] != 1) selfcheck_die("settings_ext_present at @65 wrong");
  if (buf[66] != 1) selfcheck_die("hints_setting at @66 wrong");
  if (buf[67] != 4) selfcheck_die("goal at @67 wrong");
  // Phase B Inverted runtime: world_state byte layout @68.
  if (buf[68] != 2) selfcheck_die("world_state at @68 wrong");
  // Rando flute/shovel decouple: flute_shovel_owned byte layout @69.
  if (buf[69] != 0x05) selfcheck_die("flute_shovel_owned at @69 wrong");
  // format_version 2: settings_present @70, and the canonical blob trails the
  // bitmap at offset (base size = header + placements + bitmap).
  if (buf[70] != 1) selfcheck_die("settings_present at @70 wrong");
  {
    uint32 base = RandoSave_SlotOnDiskSize(src.header.placement_table_size) - kSettingsCanonicalLen;
    if (buf[base] != 0xC0) selfcheck_die("settings_canonical blob not at expected offset");
    if (buf[base + kSettingsCanonicalLen - 1] != (uint8)(0xC0 + kSettingsCanonicalLen - 1))
      selfcheck_die("settings_canonical blob tail wrong");
  }
  // Phase C entrance shuffle: entrance_axes @71, entrance_attempt @72.
  if (buf[71] != 0x05) selfcheck_die("entrance_axes at @71 wrong");
  if (buf[72] != 0x03) selfcheck_die("entrance_attempt at @72 wrong");
  if (buf[75] != 0x07) selfcheck_die("prize_attempt at @75 wrong");
  // Flat table layout check: location 5 should hold item 50.
  if (get_u16le(buf + kRandoSidecar_SlotHeaderSize + 5 * 2) != 50)
    selfcheck_die("flat table: loc 5 item slot wrong");
  if (get_u16le(buf + kRandoSidecar_SlotHeaderSize + 10 * 2) != 75)
    selfcheck_die("flat table: loc 10 item slot wrong");
  if (get_u16le(buf + kRandoSidecar_SlotHeaderSize + 20 * 2) != 99)
    selfcheck_die("flat table: loc 20 item slot wrong");
  if (get_u16le(buf + kRandoSidecar_SlotHeaderSize + 0 * 2) != kRandoSidecar_NoPlacementSentinel)
    selfcheck_die("flat table: loc 0 should be sentinel");

  uint32 used = RandoSave_DeserializeSlot(buf, wrote, &dst);
  if (used != wrote) selfcheck_die("deserialize used != serialize wrote");

  // Compare round-trip fields.
  if (dst.header.slot_kind != src.header.slot_kind) selfcheck_die("slot_kind round-trip");
  if (dst.header.generator_version != src.header.generator_version) selfcheck_die("gen_version round-trip");
  if (memcmp(dst.header.settings_hash, src.header.settings_hash, 16) != 0) selfcheck_die("settings_hash round-trip");
  if (memcmp(dst.header.share_string, src.header.share_string, kRandoSidecar_ShareStringLength) != 0) selfcheck_die("share_string round-trip");
  if (dst.header.last_vanilla_write_version != src.header.last_vanilla_write_version) selfcheck_die("last_vanilla_write_version round-trip");
  if (dst.header.sram_slot_checksum_at_last_write != src.header.sram_slot_checksum_at_last_write) selfcheck_die("sram_checksum round-trip");
  if (dst.header.placement_table_size != src.header.placement_table_size) selfcheck_die("placement_table_size round-trip");
  if (dst.header.flags != src.header.flags) selfcheck_die("flags round-trip");
  if (dst.header.mushroom_held != src.header.mushroom_held) selfcheck_die("mushroom_held round-trip");
  if (dst.header.settings_ext_present != src.header.settings_ext_present) selfcheck_die("settings_ext_present round-trip");
  if (dst.header.hints_setting != src.header.hints_setting) selfcheck_die("hints_setting round-trip");
  if (dst.header.goal != src.header.goal) selfcheck_die("goal round-trip");
  if (dst.header.world_state != src.header.world_state) selfcheck_die("world_state round-trip");
  if (dst.header.flute_shovel_owned != src.header.flute_shovel_owned) selfcheck_die("flute_shovel_owned round-trip");
  if (dst.header.settings_present != src.header.settings_present) selfcheck_die("settings_present round-trip");
  if (memcmp(dst.settings_canonical, src.settings_canonical, kSettingsCanonicalLen) != 0) selfcheck_die("settings_canonical round-trip");
  if (dst.header.entrance_axes != src.header.entrance_axes) selfcheck_die("entrance_axes round-trip");
  if (dst.header.entrance_attempt != src.header.entrance_attempt) selfcheck_die("entrance_attempt round-trip");
  if (dst.header.boomerang_owned != src.header.boomerang_owned) selfcheck_die("boomerang_owned round-trip");
  if (dst.header.bow_owned != src.header.bow_owned) selfcheck_die("bow_owned round-trip");
  if (dst.header.prize_attempt != src.header.prize_attempt) selfcheck_die("prize_attempt round-trip");
  if (dst.placement_count != src.placement_count) selfcheck_die("placement_count round-trip");
  // After deserialization the sparse list is sorted by location_id (because
  // we scatter+gather over the dense array).
  if (dst.placements[0].location_id != 5  || dst.placements[0].item_id != 50) selfcheck_die("placements[0] round-trip");
  if (dst.placements[1].location_id != 10 || dst.placements[1].item_id != 75) selfcheck_die("placements[1] round-trip");
  if (dst.placements[2].location_id != 20 || dst.placements[2].item_id != 99) selfcheck_die("placements[2] round-trip");
  if (dst.checked_bitmap[0] != src.checked_bitmap[0]) selfcheck_die("checked_bitmap round-trip");

  // Bad-magic case → deserialize returns 0.
  buf[0] = 0;
  RandoSidecarSlot trash;
  if (RandoSave_DeserializeSlot(buf, wrote, &trash) != 0) {
    selfcheck_die("deserialize should reject bad magic");
  }

  // File header round-trip.
  RandoSidecarFileHeader fh = { kRandoSidecar_FileFormatVersion, kRandoSidecar_SlotCount, 0xCAFEBABEu };
  uint8 fhbuf[16];
  if (RandoSave_SerializeFileHeader(&fh, fhbuf, sizeof(fhbuf)) != 16) selfcheck_die("file header serialize");
  RandoSidecarFileHeader fh2;
  if (RandoSave_DeserializeFileHeader(fhbuf, sizeof(fhbuf), &fh2) != 16) selfcheck_die("file header deserialize");
  if (fh2.format_version != fh.format_version) selfcheck_die("file format_version round-trip");
  if (fh2.slot_count != fh.slot_count) selfcheck_die("file slot_count round-trip");
  if (fh2.file_crc != fh.file_crc) selfcheck_die("file_crc round-trip");

  // SRAM-slot checksum sanity: mirror the messaging.c routine on a known
  // pattern. For a zeroed slot, t = 0x5a5a - 0 - 0 - ... = 0x5a5a.
  {
    uint8 zeroed[0x500] = { 0 };
    if (RandoSave_ComputeSramSlotChecksum(zeroed, sizeof(zeroed)) != 0x5a5au) {
      selfcheck_die("checksum on zeroed slot != 0x5a5a");
    }
    // A slot with just byte 0 = 0x01 (so u16 LE at offset 0 = 0x0001) →
    // t = 0x5a5a - 0x0001 = 0x5a59.
    uint8 patterned[0x500] = { 0 };
    patterned[0] = 0x01;
    if (RandoSave_ComputeSramSlotChecksum(patterned, sizeof(patterned)) != 0x5a59u) {
      selfcheck_die("checksum on patterned slot wrong");
    }
    // Too-small buffer rejects with 0.
    if (RandoSave_ComputeSramSlotChecksum(zeroed, 100) != 0) {
      selfcheck_die("checksum on tiny buffer should be 0");
    }
  }

  // Rando_WriteSidecarSlot / Rando_LoadSidecarSlot round-trip via a temp
  // file path. Build a synthetic slot, write to a temp dir, load back,
  // compare byte-by-byte (against the spec's promise that round-trip is
  // exact). We bypass the production "saves/sram_rando.dat" path by
  // exercising RandoSave_WriteFile / RandoSave_ReadFile directly with a
  // tmpfile-derived path — keeps the self-check hermetic.
  //
  // (We can't reliably write to "saves/" in CI from --rando-selftest, so
  // exercise the serialize→deserialize→compare path which is the same data
  // flow as Rando_LoadSidecarSlot uses internally.)
  {
    RandoSidecarSlot slots[kRandoSidecar_SlotCount];
    memset(slots, 0, sizeof(slots));
    slots[0].header.slot_kind = kSlotKind_Empty;
    slots[1] = src;  // populated above
    slots[2].header.slot_kind = kSlotKind_Vanilla;

    // Compute total size.
    uint32 total = kRandoSidecar_FileHeaderSize;
    for (uint8 i = 0; i < kRandoSidecar_SlotCount; i++) {
      total += RandoSave_SlotOnDiskSize(slots[i].header.placement_table_size);
    }
    uint8 *bigbuf = (uint8 *)malloc(total);
    if (bigbuf == NULL) selfcheck_die("malloc for round-trip buffer");
    RandoSidecarFileHeader hdr = { kRandoSidecar_FileFormatVersion,
                                   kRandoSidecar_SlotCount, 0 };
    uint32 off = RandoSave_SerializeFileHeader(&hdr, bigbuf, total);
    for (uint8 i = 0; i < kRandoSidecar_SlotCount; i++) {
      uint32 used = RandoSave_SerializeSlot(&slots[i], bigbuf + off, total - off);
      if (used == 0) { free(bigbuf); selfcheck_die("serialize slot for round-trip"); }
      off += used;
    }
    if (off != total) { free(bigbuf); selfcheck_die("round-trip size mismatch"); }

    // Read back via the same path the deserializer uses.
    RandoSidecarFileHeader hdr2;
    uint32 used = RandoSave_DeserializeFileHeader(bigbuf, total, &hdr2);
    if (used != kRandoSidecar_FileHeaderSize) {
      free(bigbuf); selfcheck_die("file header deserialize for round-trip");
    }
    uint32 cur = used;
    RandoSidecarSlot back[kRandoSidecar_SlotCount];
    for (uint8 i = 0; i < kRandoSidecar_SlotCount; i++) {
      uint32 u = RandoSave_DeserializeSlot(bigbuf + cur, total - cur, &back[i]);
      if (u == 0) { free(bigbuf); selfcheck_die("slot deserialize for round-trip"); }
      cur += u;
    }
    free(bigbuf);

    // Byte-equality on the populated slot.
    if (back[1].header.slot_kind != slots[1].header.slot_kind ||
        back[1].header.generator_version != slots[1].header.generator_version ||
        memcmp(back[1].header.settings_hash, slots[1].header.settings_hash, 16) != 0 ||
        memcmp(back[1].header.share_string, slots[1].header.share_string, 32) != 0 ||
        back[1].header.last_vanilla_write_version != slots[1].header.last_vanilla_write_version ||
        back[1].header.sram_slot_checksum_at_last_write != slots[1].header.sram_slot_checksum_at_last_write ||
        back[1].header.placement_table_size != slots[1].header.placement_table_size ||
        back[1].header.flags != slots[1].header.flags ||
        back[1].placement_count != slots[1].placement_count) {
      selfcheck_die("full-file round-trip slot header mismatch");
    }
    for (uint16 i = 0; i < slots[1].placement_count; i++) {
      if (back[1].placements[i].location_id != slots[1].placements[i].location_id ||
          back[1].placements[i].item_id != slots[1].placements[i].item_id) {
        selfcheck_die("full-file round-trip placement mismatch");
      }
    }
    if (memcmp(back[1].checked_bitmap, slots[1].checked_bitmap,
               sizeof(slots[1].checked_bitmap)) != 0) {
      selfcheck_die("full-file round-trip bitmap mismatch");
    }
    if (back[1].header.settings_present != slots[1].header.settings_present ||
        memcmp(back[1].settings_canonical, slots[1].settings_canonical,
               kSettingsCanonicalLen) != 0) {
      selfcheck_die("full-file round-trip settings blob mismatch");
    }
  }

  // -------------------------------------------------------------------------
  // format_version 1 backward-compat: a v1 slot has no trailing settings blob.
  // Serialize the body WITHOUT the blob (base size), then deserialize with
  // with_settings=false (the path RandoSave_ReadFile takes for a v1 file) and
  // confirm it loads with settings_present forced off and the placement/bitmap
  // intact. Guards the older-file load path the version gate protects.
  // -------------------------------------------------------------------------
  {
    // Build a v1-style buffer by hand: header (with @70 deliberately nonzero to
    // prove the loader forces it off), flat table, bitmap — and NO blob.
    uint8 v1buf[256];
    memset(v1buf, 0, sizeof(v1buf));
    serialize_slot_header(&src.header, v1buf);  // writes settings_present=1 @70
    uint32 loc_count = (uint32)src.header.placement_table_size / 2;
    uint8 *p = v1buf + kRandoSidecar_SlotHeaderSize;
    for (uint32 i = 0; i < loc_count; i++) put_u16le(p + i * 2, kRandoSidecar_NoPlacementSentinel);
    put_u16le(p + 5 * 2, 50);
    p += loc_count * 2;
    uint32 v1_bitmap = (loc_count + 7) >> 3;
    p[0] = 0x05;  // matches src.checked_bitmap[0]
    uint32 v1_total = kRandoSidecar_SlotHeaderSize + loc_count * 2 + v1_bitmap;  // NO blob

    RandoSidecarSlot v1dst;
    uint32 v1_used = deserialize_slot_versioned(v1buf, v1_total, &v1dst, false);
    if (v1_used != v1_total) selfcheck_die("v1 compat: used != base total (blob must be absent)");
    if (v1dst.header.settings_present != 0) selfcheck_die("v1 compat: settings_present must be forced 0");
    if (v1dst.placement_count != 1 || v1dst.placements[0].location_id != 5 ||
        v1dst.placements[0].item_id != 50) selfcheck_die("v1 compat: placement round-trip");
    if (v1dst.checked_bitmap[0] != 0x05) selfcheck_die("v1 compat: bitmap round-trip");
  }

  // -------------------------------------------------------------------------
  // §8.5 cross-version load test.
  // Spec scenario "Version drift loads with warning": a slot whose
  // `generator_version` differs from the binary's current value still loads
  // (the embedded placement table is authoritative); the drift is signalled
  // by Rando_DetectVersionDrift so the UI can warn.
  // -------------------------------------------------------------------------
  {
    RandoSlotHeader hdr_old;
    memset(&hdr_old, 0, sizeof(hdr_old));
    hdr_old.slot_kind = kSlotKind_Randomizer;
    hdr_old.generator_version = (uint16)(kGeneratorVersion - 1);  // N
    hdr_old.placement_table_size = 0;

    // Newer binary (N+1) loading an older slot — drift detected.
    if (!Rando_DetectVersionDrift(&hdr_old, (uint16)kGeneratorVersion)) {
      selfcheck_die("§8.5: newer binary should detect version drift on older slot");
    }
    // Same-version load: no drift.
    RandoSlotHeader hdr_cur;
    memset(&hdr_cur, 0, sizeof(hdr_cur));
    hdr_cur.slot_kind = kSlotKind_Randomizer;
    hdr_cur.generator_version = (uint16)kGeneratorVersion;
    if (Rando_DetectVersionDrift(&hdr_cur, (uint16)kGeneratorVersion)) {
      selfcheck_die("§8.5: same-version load should report no drift");
    }
    // Older binary reading a newer slot — drift symmetric.
    RandoSlotHeader hdr_new = hdr_old;
    hdr_new.generator_version = (uint16)(kGeneratorVersion + 1);
    if (!Rando_DetectVersionDrift(&hdr_new, (uint16)kGeneratorVersion)) {
      selfcheck_die("§8.5: drift detection should be symmetric (older binary vs newer slot)");
    }
    // NULL header is defensively false.
    if (Rando_DetectVersionDrift(NULL, (uint16)kGeneratorVersion)) {
      selfcheck_die("§8.5: NULL header should report no drift");
    }
    // Spec invariant: even with drift, the on-disk slot's
    // `placement_table_size` is honored — we don't re-derive from the
    // binary's current registry. Build a serialized slot with size 10 (5
    // locations), drift the version, deserialize on the "newer binary",
    // and confirm `placement_table_size` round-trips exactly.
    //
    // Vacuous-by-construction note (audit follow-up): the cross-registry
    // failure mode this is meant to guard against (a loader that re-derives
    // from current-binary registry count instead of reading the slot's own
    // size) is structurally impossible in `RandoSave_DeserializeSlot` —
    // there's no `current_registry_count` parameter or global it could
    // consult. The assertion below documents the invariant as a regression
    // guard against a future loader rewrite that adds such a parameter.
    RandoSidecarSlot drift_src;
    memset(&drift_src, 0, sizeof(drift_src));
    drift_src.header = hdr_old;
    drift_src.header.placement_table_size = 10;  // 5 locations
    drift_src.placements[0].location_id = 2;
    drift_src.placements[0].item_id = 0x0042;
    drift_src.placement_count = 1;
    uint8 drift_buf[256];
    uint32 drift_wrote = RandoSave_SerializeSlot(&drift_src, drift_buf, sizeof(drift_buf));
    if (drift_wrote == 0) selfcheck_die("§8.5: serialize for drift round-trip");
    RandoSidecarSlot drift_dst;
    if (RandoSave_DeserializeSlot(drift_buf, drift_wrote, &drift_dst) == 0) {
      selfcheck_die("§8.5: deserialize for drift round-trip");
    }
    if (drift_dst.header.generator_version != (uint16)(kGeneratorVersion - 1)) {
      selfcheck_die("§8.5: drifted generator_version should round-trip as-written");
    }
    if (drift_dst.header.placement_table_size != 10) {
      selfcheck_die("§8.5: placement_table_size should round-trip exactly (spec: read N bytes, not current-binary's count)");
    }
    if (drift_dst.placement_count != 1 ||
        drift_dst.placements[0].location_id != 2 ||
        drift_dst.placements[0].item_id != 0x0042) {
      selfcheck_die("§8.5: drifted slot placements should round-trip identically");
    }
  }

  // -------------------------------------------------------------------------
  // §8.6 downgrade-then-re-upgrade drift detection.
  // Spec scenario "Drift detected after downgrade-then-re-upgrade": after a
  // vanilla-only write to the paired sram.dat slot, the stored
  // sram_slot_checksum_at_last_write no longer matches the current paired
  // bytes — the helper returns true so the UI can fire the recovery prompt.
  // -------------------------------------------------------------------------
  {
    // Synthesize "v1.1 state" — populate sram_v11 with a deterministic
    // pattern and capture its checksum into the slot header.
    uint8 sram_v11[0x500];
    for (uint32 i = 0; i < sizeof(sram_v11); i++) {
      sram_v11[i] = (uint8)((i * 13u + 7u) & 0xffu);
    }
    RandoSlotHeader hdr_at_v11;
    memset(&hdr_at_v11, 0, sizeof(hdr_at_v11));
    hdr_at_v11.slot_kind = kSlotKind_Randomizer;
    hdr_at_v11.generator_version = (uint16)kGeneratorVersion;
    hdr_at_v11.sram_slot_checksum_at_last_write =
        RandoSave_ComputeSramSlotChecksum(sram_v11, sizeof(sram_v11));

    // Same paired slot bytes → no drift.
    if (Rando_DetectChecksumDrift(&hdr_at_v11, sram_v11, sizeof(sram_v11))) {
      selfcheck_die("§8.6: no-edit paired sram should report no drift");
    }

    // Simulate v1.0 writing the paired sram slot — different bytes.
    uint8 sram_v10[0x500];
    for (uint32 i = 0; i < sizeof(sram_v10); i++) {
      sram_v10[i] = (uint8)((i * 17u + 3u) & 0xffu);
    }
    if (!Rando_DetectChecksumDrift(&hdr_at_v11, sram_v10, sizeof(sram_v10))) {
      selfcheck_die("§8.6: edited paired sram should fire drift signal");
    }

    // Single-byte change is still drift (the checksum routine is u16
    // subtract-over-pairs; a single flipped bit shifts the result).
    uint8 sram_one_byte_off[0x500];
    memcpy(sram_one_byte_off, sram_v11, sizeof(sram_one_byte_off));
    sram_one_byte_off[0x100] ^= 0x01;
    if (!Rando_DetectChecksumDrift(&hdr_at_v11, sram_one_byte_off,
                                   sizeof(sram_one_byte_off))) {
      selfcheck_die("§8.6: single-byte sram change should fire drift signal");
    }

    // NULL/short-buffer cases are defensively false (no signal).
    if (Rando_DetectChecksumDrift(&hdr_at_v11, NULL, sizeof(sram_v11))) {
      selfcheck_die("§8.6: NULL paired slot should report no drift");
    }
    if (Rando_DetectChecksumDrift(NULL, sram_v11, sizeof(sram_v11))) {
      selfcheck_die("§8.6: NULL header should report no drift");
    }
    if (Rando_DetectChecksumDrift(&hdr_at_v11, sram_v11, 100)) {
      selfcheck_die("§8.6: too-small buffer should report no drift");
    }

    // Spec scenario "Crash between sidecar and sram.dat" produces
    // checksum drift WITHOUT version drift — single binary version, but
    // the sram.dat write was interrupted so its bytes don't match the
    // sidecar's recorded checksum. Verify checksum-drift fires alone on
    // this isolated scenario.
    {
      RandoSlotHeader hdr_crash = hdr_at_v11;  // same gen_version
      // sram_v10 represents the partially-written sram.dat after the crash.
      if (!Rando_DetectChecksumDrift(&hdr_crash, sram_v10, sizeof(sram_v10))) {
        selfcheck_die("§8.6: crash-mid-save should fire checksum drift");
      }
      if (Rando_DetectVersionDrift(&hdr_crash, (uint16)kGeneratorVersion)) {
        selfcheck_die("§8.6: crash-mid-save scenario should NOT have version drift (single binary)");
      }
    }

    // Cross-product: both drifts can co-occur — this is the
    // downgrade-then-re-upgrade pattern (player wrote on v1.1, ran an older
    // v1.0 binary that re-saved the paired sram.dat as vanilla without
    // touching the sidecar, then re-upgraded to a v1.2 binary that loads
    // the slot). The sidecar's gen_version still says v1.1; the binary is
    // v1.2 (version drift); and the paired sram.dat checksum no longer
    // matches the sidecar's recorded value (checksum drift). Both helpers
    // fire independently — the UI surfaces both conditions.
    RandoSlotHeader hdr_both = hdr_at_v11;
    hdr_both.generator_version = (uint16)(kGeneratorVersion - 1);
    if (!Rando_DetectVersionDrift(&hdr_both, (uint16)kGeneratorVersion)) {
      selfcheck_die("§8.6: downgrade-re-upgrade case should fire version drift");
    }
    if (!Rando_DetectChecksumDrift(&hdr_both, sram_v10, sizeof(sram_v10))) {
      selfcheck_die("§8.6: downgrade-re-upgrade case should fire checksum drift");
    }
  }

  // -------------------------------------------------------------------------
  // §8.7 coexistence test.
  // Spec scenarios "Slot-kind discriminator" and "sram.dat present but
  // sidecar absent": a sidecar with mixed slot_kinds (Randomizer / Vanilla /
  // Empty) loads correctly per-slot; a missing sidecar file produces no
  // rando metadata. We exercise the in-memory serialize → deserialize path
  // (the same data flow as RandoSave_ReadFile uses internally) so the test
  // stays hermetic — CI environments may not have a writable saves/ dir.
  // -------------------------------------------------------------------------
  {
    RandoSidecarSlot mixed[kRandoSidecar_SlotCount];
    memset(mixed, 0, sizeof(mixed));
    // Slot 0: Randomizer with a populated placement table.
    mixed[0].header.slot_kind = kSlotKind_Randomizer;
    mixed[0].header.generator_version = (uint16)kGeneratorVersion;
    mixed[0].header.placement_table_size = 8;  // 4 locations
    mixed[0].placements[0].location_id = 1;
    mixed[0].placements[0].item_id = 0x0080;
    mixed[0].placement_count = 1;
    // Slot 1: Vanilla — explicit opt-out. Build with a NON-zero placement
    // table body to exercise the spec scenario "Slot-kind discriminator"
    // properly: "the loader ignores any subsequent bytes of that slot and
    // treats the corresponding sram.dat slot as a plain vanilla save."
    // A Vanilla slot can in principle have leftover placement bytes from a
    // prior write (e.g., the slot was a Randomizer that the user converted
    // to Vanilla). The discriminator contract says the caller MUST gate on
    // slot_kind, not on placement contents. Test below asserts slot_kind
    // round-trips so the UI's slot-kind dispatch is reliable.
    mixed[1].header.slot_kind = kSlotKind_Vanilla;
    mixed[1].header.placement_table_size = 6;  // 3 location slots
    mixed[1].placements[0].location_id = 0;
    mixed[1].placements[0].item_id = 0xBEEF;   // junk that the UI must ignore
    mixed[1].placement_count = 1;
    // Slot 2: Empty — never touched.
    mixed[2].header.slot_kind = kSlotKind_Empty;
    mixed[2].header.placement_table_size = 0;

    // Serialize full file (file header + 3 slot regions).
    uint32 total = kRandoSidecar_FileHeaderSize;
    for (uint8 i = 0; i < kRandoSidecar_SlotCount; i++) {
      total += RandoSave_SlotOnDiskSize(mixed[i].header.placement_table_size);
    }
    uint8 *bigbuf = (uint8 *)malloc(total);
    if (bigbuf == NULL) selfcheck_die("§8.7: malloc coexistence buffer");
    RandoSidecarFileHeader fh3 = { kRandoSidecar_FileFormatVersion,
                                   kRandoSidecar_SlotCount, 0 };
    uint32 off = RandoSave_SerializeFileHeader(&fh3, bigbuf, total);
    for (uint8 i = 0; i < kRandoSidecar_SlotCount; i++) {
      uint32 u = RandoSave_SerializeSlot(&mixed[i], bigbuf + off, total - off);
      if (u == 0) { free(bigbuf); selfcheck_die("§8.7: serialize mixed slot"); }
      off += u;
    }
    if (off != total) { free(bigbuf); selfcheck_die("§8.7: coexistence size mismatch"); }

    // Read back and verify per-slot classification round-trips.
    RandoSidecarFileHeader fh4;
    uint32 fhu = RandoSave_DeserializeFileHeader(bigbuf, total, &fh4);
    if (fhu != kRandoSidecar_FileHeaderSize) {
      free(bigbuf); selfcheck_die("§8.7: file header deserialize");
    }
    uint32 cur = fhu;
    RandoSidecarSlot back[kRandoSidecar_SlotCount];
    for (uint8 i = 0; i < kRandoSidecar_SlotCount; i++) {
      uint32 u = RandoSave_DeserializeSlot(bigbuf + cur, total - cur, &back[i]);
      if (u == 0) { free(bigbuf); selfcheck_die("§8.7: slot deserialize"); }
      cur += u;
    }
    free(bigbuf);

    if (back[0].header.slot_kind != kSlotKind_Randomizer) {
      selfcheck_die("§8.7: slot 0 should round-trip as Randomizer");
    }
    if (back[0].placement_count != 1 ||
        back[0].placements[0].location_id != 1 ||
        back[0].placements[0].item_id != 0x0080) {
      selfcheck_die("§8.7: slot 0 placement should round-trip");
    }
    if (back[1].header.slot_kind != kSlotKind_Vanilla) {
      selfcheck_die("§8.7: slot 1 should round-trip as Vanilla (spec: discriminator preserved)");
    }
    // The Vanilla slot's body (junk placement bytes) is preserved through
    // serialize/deserialize — that's expected. The contract the spec pins
    // is that the UI MUST gate on slot_kind, not parse the body. Assert
    // the body bytes did make the round-trip so we know the discriminator
    // worked under realistic conditions (a Vanilla slot with non-zero
    // body), AND that the slot_kind discriminator is intact so the UI
    // can rely on it.
    if (back[1].placement_count != 1 ||
        back[1].placements[0].item_id != 0xBEEF) {
      selfcheck_die("§8.7: Vanilla slot body should round-trip (UI must gate on slot_kind, not contents)");
    }
    if (back[2].header.slot_kind != kSlotKind_Empty) {
      selfcheck_die("§8.7: slot 2 should round-trip as Empty");
    }

    // Spec scenario "sram.dat present but sidecar absent": loading a path
    // that doesn't exist returns false; caller treats all slots as vanilla.
    // Use a path with embedded entropy (PID-equivalent + literal cookie) so
    // collision with a real CI file is implausible — protects against the
    // pathological cwd hygiene failure of a literal short filename.
    {
      RandoSidecarSlot dummy_slots[kRandoSidecar_SlotCount];
      char unique_path[128];
      // Compose: literal cookie + a deterministic-but-unlikely-to-collide
      // suffix derived from the function address (changes per build) and
      // the stack address (changes per invocation). Neither is a "secure"
      // random source, but the combination is essentially never the name
      // of a real file in any sane checkout.
      uintptr_t entropy = (uintptr_t)&dummy_slots ^ (uintptr_t)&RandoSave_ReadFile;
      snprintf(unique_path, sizeof(unique_path),
               "selfcheck_nonexistent_%016llx.dat",
               (unsigned long long)entropy);
      bool ok = RandoSave_ReadFile(unique_path, dummy_slots);
      if (ok) {
        selfcheck_die("§8.7: missing sidecar file should return false (vanilla appearance)");
      }
    }
  }

  fprintf(stderr, "[RandoSave_SelfCheck] OK\n");
}
