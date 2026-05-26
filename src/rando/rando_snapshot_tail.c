// rando_snapshot_tail.c — TLV-chain encoder/decoder for snapshot tail
// (tasks.md §8.8, §8.8a; randomizer-save spec § "Snapshot interoperability";
// design.md §D11).
//
// On-disk byte layout (per spec, all multi-byte LE):
//
//   magic[8]                 = "ZRSNAP01"  raw ASCII, no NUL
//   type[4]   LE             = 1 (TAIL_RANDO_STATE)
//   length[4] LE             = payload size in bytes
//   payload:
//     generator_version[2]   LE
//     settings_hash[16]
//     share_string[32]
//     placement_table_size[2] LE       (bytes; = 2 × #locations)
//     placement_table[placement_table_size]  flat uint16 LE by location_id
//                              (0xFFFF = no placement sentinel)
//
// Multi-byte fields are emitted/consumed via explicit LE byte helpers (no
// host-endianness reliance) — per the same discipline as rando_save.c.

#include "rando_snapshot_tail.h"
#include "rando_placement.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// LE byte helpers — local copies to keep this module self-contained
// (rando_save.c keeps its own as `static`, so we can't share without
// linkage churn).
// ---------------------------------------------------------------------------
static void put_u16le_bytes(uint8 out[2], uint16 v) {
  out[0] = (uint8)v;
  out[1] = (uint8)(v >> 8);
}
static void put_u32le_bytes(uint8 out[4], uint32 v) {
  out[0] = (uint8)v;
  out[1] = (uint8)(v >> 8);
  out[2] = (uint8)(v >> 16);
  out[3] = (uint8)(v >> 24);
}
static uint16 get_u16le_bytes(const uint8 in[2]) {
  return (uint16)in[0] | ((uint16)in[1] << 8);
}
static uint32 get_u32le_bytes(const uint8 in[4]) {
  return (uint32)in[0] | ((uint32)in[1] << 8)
       | ((uint32)in[2] << 16) | ((uint32)in[3] << 24);
}

// ---------------------------------------------------------------------------
// Snapshot context — the metadata the TLV payload needs. Module-static.
// `g_has_ctx` discriminates "context installed" from "all-zeros happens to
// match" (since settings_hash could be all-zero in principle).
// ---------------------------------------------------------------------------
static struct {
  uint16 generator_version;
  uint8 settings_hash[16];
  uint8 share_string[32];
} g_ctx;
static bool g_has_ctx = false;

void Rando_SetSnapshotContext(uint16 generator_version,
                              const uint8 settings_hash[16],
                              const uint8 share_string[32]) {
  if (settings_hash == NULL || share_string == NULL) {
    g_has_ctx = false;
    memset(&g_ctx, 0, sizeof(g_ctx));
    return;
  }
  g_ctx.generator_version = generator_version;
  memcpy(g_ctx.settings_hash, settings_hash, 16);
  memcpy(g_ctx.share_string, share_string, 32);
  g_has_ctx = true;
}

void Rando_ClearSnapshotContext(void) {
  g_has_ctx = false;
  memset(&g_ctx, 0, sizeof(g_ctx));
}

bool Rando_HasSnapshotContext(void) {
  return g_has_ctx;
}

uint16 Rando_GetSnapshotGeneratorVersion(void) {
  return g_ctx.generator_version;
}

const uint8 *Rando_GetSnapshotSettingsHash(void) {
  return g_ctx.settings_hash;
}

const uint8 *Rando_GetSnapshotShareString(void) {
  return g_ctx.share_string;
}

// ---------------------------------------------------------------------------
// Ordering-invariant tripwire (tasks.md §8.8a).
// ---------------------------------------------------------------------------
uint64 g_rando_oncheck_call_count = 0;

// ---------------------------------------------------------------------------
// Save
// ---------------------------------------------------------------------------
bool RandoSnapshotTail_Save(FILE *f) {
  if (f == NULL) return false;

  // No placement installed → nothing to emit. Older readers see a vanilla
  // tail; newer readers reading this file will see EOF at the TLV loop and
  // terminate cleanly. This is intentional graceful degradation.
  const RandoPlacementTable *t = Placement_GetActive();
  if (t == NULL || t->count == 0) return true;

  // No context → cannot emit the payload (we'd be lying about gen_version /
  // settings_hash / share_string). Suppress emission; the tail-load
  // graceful-degrade path takes over on the read side.
  if (!g_has_ctx) return true;

  // Translate sparse placements[] back into a flat uint16-by-location_id
  // array. Same convention as rando_save.c: max(location_id)+1 entries,
  // sentinel-fill any unset entry. Cap at 512 to match RandoSidecarSlot's
  // static `placements[512]` size — the spec's location_count fits well
  // under this (Phase A baseline is ~237).
  uint16 max_loc = 0;
  for (uint16 i = 0; i < t->count; i++) {
    if (t->entries[i].location_id >= max_loc) {
      max_loc = (uint16)(t->entries[i].location_id + 1);
    }
  }
  if (max_loc > 512) return false;  // refuse to emit truncated payload
  uint16 location_count = max_loc;
  uint32 placement_table_bytes = (uint32)location_count * 2u;

  // Build payload in a heap buffer so we can compute the total length up
  // front (TLV's length field must be exact).
  uint32 payload_len = 2u + 16u + 32u + 2u + placement_table_bytes;
  uint8 *payload = (uint8 *)malloc(payload_len);
  if (payload == NULL) return false;
  uint8 *p = payload;
  put_u16le_bytes(p, g_ctx.generator_version); p += 2;
  memcpy(p, g_ctx.settings_hash, 16); p += 16;
  memcpy(p, g_ctx.share_string, 32); p += 32;
  put_u16le_bytes(p, (uint16)placement_table_bytes); p += 2;
  // Sentinel-fill, then scatter sparse entries.
  for (uint16 i = 0; i < location_count; i++) {
    p[i * 2 + 0] = 0xFF;
    p[i * 2 + 1] = 0xFF;
  }
  for (uint16 i = 0; i < t->count; i++) {
    uint16 loc = t->entries[i].location_id;
    if (loc < location_count) {
      p[loc * 2 + 0] = (uint8)(t->entries[i].item_id & 0xff);
      p[loc * 2 + 1] = (uint8)(t->entries[i].item_id >> 8);
    }
  }

  // TLV header (magic + type + length).
  uint8 hdr[16];
  memcpy(hdr, kRandoSnapshotTail_Magic, kRandoSnapshotTail_MagicLen);
  put_u32le_bytes(hdr + 8, kRandoSnapshotTail_Type_RandoState);
  put_u32le_bytes(hdr + 12, payload_len);

  size_t w1 = fwrite(hdr, 1, sizeof(hdr), f);
  size_t w2 = (w1 == sizeof(hdr)) ? fwrite(payload, 1, payload_len, f) : 0;
  free(payload);
  return (w1 == sizeof(hdr)) && (w2 == payload_len);
}

// ---------------------------------------------------------------------------
// Load
//
// Reads as many trailing TLV entries as the file holds. For each entry:
//   - Read 8-byte magic. EOF / short read / non-matching magic → terminate
//     loop cleanly (vanilla / older / unrelated trailing data).
//   - Read type[4] + length[4] (LE). On short read, terminate (malformed).
//   - If type is known, parse payload exactly `length` bytes.
//   - If type is unknown, fseek past `length` bytes; loop continues.
//
// Returns the number of recognized TLVs consumed.
// ---------------------------------------------------------------------------
static RandoPlacement g_tail_entries[512];
static RandoPlacementTable g_tail_table;

int RandoSnapshotTail_Load(FILE *f) {
  if (f == NULL) return 0;
  int recognized = 0;

  for (;;) {
    uint8 magic[kRandoSnapshotTail_MagicLen];
    size_t mr = fread(magic, 1, kRandoSnapshotTail_MagicLen, f);
    if (mr != kRandoSnapshotTail_MagicLen) return recognized;  // EOF / short read
    if (memcmp(magic, kRandoSnapshotTail_Magic, kRandoSnapshotTail_MagicLen) != 0) {
      // Unknown / non-matching magic — terminate cleanly.
      return recognized;
    }

    uint8 hdr[8];  // type[4] + length[4]
    if (fread(hdr, 1, 8, f) != 8) return recognized;
    uint32 type = get_u32le_bytes(hdr + 0);
    uint32 length = get_u32le_bytes(hdr + 4);

    if (type == kRandoSnapshotTail_Type_RandoState) {
      // Payload schema: gen_version[2] + settings_hash[16] + share_string[32]
      //               + placement_table_size[2] + placement_table[...]
      // Minimum length = 2 + 16 + 32 + 2 = 52 (empty table is legal).
      if (length < 52u) {
        // Malformed — seek past whatever payload claims and continue.
        if (fseek(f, (long)length, SEEK_CUR) != 0) return recognized;
        continue;
      }
      // Read fixed-size prefix.
      uint8 head[52];
      if (fread(head, 1, 52, f) != 52) return recognized;
      uint16 gen_version = get_u16le_bytes(head + 0);
      const uint8 *settings_hash = head + 2;     // 16 bytes
      const uint8 *share_string  = head + 18;    // 32 bytes
      uint16 placement_table_bytes = get_u16le_bytes(head + 50);

      uint32 expected_body = (uint32)placement_table_bytes;
      uint32 expected_total = 52u + expected_body;
      if (length != expected_total) {
        // Inner-size mismatch — skip the rest and continue.
        long remaining = (long)length - 52L;
        if (remaining > 0 && fseek(f, remaining, SEEK_CUR) != 0) return recognized;
        continue;
      }
      // Read the placement table.
      uint16 location_count = (uint16)(placement_table_bytes / 2u);
      if (location_count > 512) {
        // Reject — exceeds our static buffer; skip body, continue.
        if (placement_table_bytes > 0 && fseek(f, (long)placement_table_bytes, SEEK_CUR) != 0) {
          return recognized;
        }
        continue;
      }
      if (placement_table_bytes > 0) {
        uint8 raw[1024];  // max 512 locations × 2 bytes
        if (fread(raw, 1, placement_table_bytes, f) != placement_table_bytes) {
          return recognized;
        }
        uint16 sparse_count = 0;
        for (uint16 i = 0; i < location_count; i++) {
          uint16 item_id = get_u16le_bytes(raw + i * 2);
          if (item_id == 0xFFFFu) continue;  // sentinel — no placement
          g_tail_entries[sparse_count].location_id = i;
          g_tail_entries[sparse_count].item_id = item_id;
          sparse_count++;
        }
        g_tail_table.entries = g_tail_entries;
        g_tail_table.count = sparse_count;
        Placement_Install(&g_tail_table);
      } else {
        // Empty table — install an empty placement (effectively clears).
        g_tail_table.entries = g_tail_entries;
        g_tail_table.count = 0;
        Placement_Install(&g_tail_table);
      }

      // Reinstall context so future re-saves of this snapshot carry the
      // same metadata.
      Rando_SetSnapshotContext(gen_version, settings_hash, share_string);
      recognized++;
      continue;
    }

    // Unknown type — seek past payload and continue.
    if (length > 0) {
      if (fseek(f, (long)length, SEEK_CUR) != 0) return recognized;
    }
  }
}

// ---------------------------------------------------------------------------
// Self-check — round-trip a synthetic TLV via tmpfile() and assert byte-
// equality + a known-good unknown-TLV-skip path.
// ---------------------------------------------------------------------------
static void selfcheck_die(const char *msg) {
  fprintf(stderr, "[RandoSnapshotTail_SelfCheck] FAIL: %s\n", msg);
  exit(2);
}

void RandoSnapshotTail_SelfCheck(void) {
  // Build a synthetic placement table.
  static RandoPlacement entries[4];
  static RandoPlacementTable t;
  entries[0].location_id = 0;   entries[0].item_id = 0x0100;
  entries[1].location_id = 3;   entries[1].item_id = 0x0205;
  entries[2].location_id = 7;   entries[2].item_id = 0x0303;
  entries[3].location_id = 12;  entries[3].item_id = 0x0BEE;
  t.entries = entries;
  t.count = 4;

  // Capture any prior installed table so we can restore it after the test.
  const RandoPlacementTable *prior_table = Placement_GetActive();

  Placement_Install(&t);

  uint8 settings_hash[16];
  uint8 share_string[32];
  for (int i = 0; i < 16; i++) settings_hash[i] = (uint8)(0xA0 + i);
  for (int i = 0; i < 32; i++) share_string[i] = (uint8)(0x10 + i);
  Rando_SetSnapshotContext(0xCAFE, settings_hash, share_string);

  // Round-trip via tmpfile.
  FILE *f = tmpfile();
  if (f == NULL) selfcheck_die("tmpfile() returned NULL");
  if (!RandoSnapshotTail_Save(f)) selfcheck_die("Save returned false");

  // Clear state so the reader is what reinstalls it.
  Placement_Install(NULL);
  Rando_ClearSnapshotContext();

  fseek(f, 0, SEEK_SET);
  int n = RandoSnapshotTail_Load(f);
  if (n != 1) selfcheck_die("Load consumed != 1 recognized TLV");
  if (!Rando_HasSnapshotContext()) selfcheck_die("context not restored");
  if (Rando_GetSnapshotGeneratorVersion() != 0xCAFE) selfcheck_die("gen_version mismatch");
  if (memcmp(Rando_GetSnapshotSettingsHash(), settings_hash, 16) != 0) {
    selfcheck_die("settings_hash mismatch");
  }
  if (memcmp(Rando_GetSnapshotShareString(), share_string, 32) != 0) {
    selfcheck_die("share_string mismatch");
  }
  const RandoPlacementTable *r = Placement_GetActive();
  if (r == NULL) selfcheck_die("placement not installed after load");
  if (r->count != 4) selfcheck_die("placement count mismatch");
  for (uint16 i = 0; i < 4; i++) {
    if (r->entries[i].location_id != entries[i].location_id ||
        r->entries[i].item_id != entries[i].item_id) {
      selfcheck_die("placement entry round-trip mismatch");
    }
  }
  fclose(f);

  // Unknown-TLV-skip path: write magic + (type=0xFEEDFACE, length=8, junk[8])
  // then a known TLV; assert the unknown one is skipped and the known one
  // is still parsed.
  Placement_Install(NULL);
  Rando_ClearSnapshotContext();
  // Re-install the synthetic table so Save emits something.
  Placement_Install(&t);
  Rando_SetSnapshotContext(0x0001, settings_hash, share_string);

  FILE *f2 = tmpfile();
  if (f2 == NULL) selfcheck_die("tmpfile() for unknown-TLV test returned NULL");
  // Emit an unknown TLV first.
  uint8 unk_hdr[16];
  memcpy(unk_hdr, kRandoSnapshotTail_Magic, 8);
  put_u32le_bytes(unk_hdr + 8, 0xFEEDFACEu);
  put_u32le_bytes(unk_hdr + 12, 8u);
  fwrite(unk_hdr, 1, 16, f2);
  uint8 junk[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
  fwrite(junk, 1, 8, f2);
  // Then the recognized TLV.
  if (!RandoSnapshotTail_Save(f2)) selfcheck_die("Save (unknown-TLV test) returned false");

  Placement_Install(NULL);
  Rando_ClearSnapshotContext();

  fseek(f2, 0, SEEK_SET);
  int n2 = RandoSnapshotTail_Load(f2);
  if (n2 != 1) selfcheck_die("Load (unknown-TLV test) didn't recognize 1 TLV");
  if (!Rando_HasSnapshotContext()) selfcheck_die("context not restored (unknown-TLV test)");
  fclose(f2);

  // Restore prior state to leave the world unchanged.
  Placement_Install(prior_table);
  if (prior_table == NULL) Rando_ClearSnapshotContext();

  fprintf(stderr, "[RandoSnapshotTail_SelfCheck] OK\n");
}
