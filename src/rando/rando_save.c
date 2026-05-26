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
// ---------------------------------------------------------------------------
uint32 RandoSave_SlotOnDiskSize(uint16 placement_table_size) {
  uint32 placements_bytes = (uint32)placement_table_size * 4;
  uint32 bitmap_bytes = ((uint32)placement_table_size + 7) >> 3;
  return kRandoSidecar_SlotHeaderSize + placements_bytes + bitmap_bytes;
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
  // @64-79 reserved[16] — zero on write
  memset(buf + 64, 0, 16);
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
  // reserved bytes ignored — forward-compat
  return kRandoSidecar_SlotHeaderSize;
}

// ---------------------------------------------------------------------------
// Slot body (placements + bitmap)
// ---------------------------------------------------------------------------
uint32 RandoSave_SerializeSlot(const RandoSidecarSlot *slot, uint8 *buf, uint32 buf_size) {
  if (slot == NULL || buf == NULL) return 0;
  uint32 size = RandoSave_SlotOnDiskSize(slot->header.placement_table_size);
  if (buf_size < size) return 0;
  serialize_slot_header(&slot->header, buf);
  uint8 *p = buf + kRandoSidecar_SlotHeaderSize;
  for (uint16 i = 0; i < slot->header.placement_table_size; i++) {
    put_u16le(p + i * 4, slot->placements[i].location_id);
    put_u16le(p + i * 4 + 2, slot->placements[i].item_id);
  }
  p += (uint32)slot->header.placement_table_size * 4;
  uint32 bitmap_bytes = ((uint32)slot->header.placement_table_size + 7) >> 3;
  memcpy(p, slot->checked_bitmap, bitmap_bytes);
  return size;
}

uint32 RandoSave_DeserializeSlot(const uint8 *buf, uint32 buf_size, RandoSidecarSlot *out) {
  if (buf == NULL || out == NULL) return 0;
  memset(out, 0, sizeof(*out));
  uint32 hdr_used = deserialize_slot_header(buf, buf_size, &out->header);
  if (hdr_used == 0) return 0;
  // Sanity-check placement_table_size against the array bound.
  if (out->header.placement_table_size > (sizeof(out->placements) / sizeof(out->placements[0]))) {
    return 0;
  }
  uint32 total = RandoSave_SlotOnDiskSize(out->header.placement_table_size);
  if (buf_size < total) return 0;
  const uint8 *p = buf + kRandoSidecar_SlotHeaderSize;
  for (uint16 i = 0; i < out->header.placement_table_size; i++) {
    out->placements[i].location_id = get_u16le(p + i * 4);
    out->placements[i].item_id = get_u16le(p + i * 4 + 2);
  }
  p += (uint32)out->header.placement_table_size * 4;
  uint32 bitmap_bytes = ((uint32)out->header.placement_table_size + 7) >> 3;
  if (bitmap_bytes <= sizeof(out->checked_bitmap)) {
    memcpy(out->checked_bitmap, p, bitmap_bytes);
  }
  return total;
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

  uint32 off = hdr_used;
  for (uint8 i = 0; i < kRandoSidecar_SlotCount; i++) {
    uint32 used = RandoSave_DeserializeSlot(buf + off, (uint32)fsize - off, &out_slots[i]);
    if (used == 0) { free(buf); return false; }
    off += used;
  }
  free(buf);
  return true;
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
  src.header.placement_table_size = 3;
  src.header.flags = 0x42;
  src.placements[0].location_id = 100; src.placements[0].item_id = 50;
  src.placements[1].location_id = 200; src.placements[1].item_id = 75;
  src.placements[2].location_id = 300; src.placements[2].item_id = 99;
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
  if (get_u16le(buf + 61) != 3) selfcheck_die("placement_table_size at @61 wrong");
  if (buf[63] != 0x42) selfcheck_die("flags at @63 wrong");

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
  for (uint16 i = 0; i < src.header.placement_table_size; i++) {
    if (dst.placements[i].location_id != src.placements[i].location_id) selfcheck_die("placements round-trip loc");
    if (dst.placements[i].item_id != src.placements[i].item_id) selfcheck_die("placements round-trip item");
  }
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

  fprintf(stderr, "[RandoSave_SelfCheck] OK\n");
}
