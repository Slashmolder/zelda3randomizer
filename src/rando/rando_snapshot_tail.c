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
#include "door_runtime.h"   // DoorRt_Installed (door-shuffle restore reconcile)
#include "../features.h"    // enhanced_features1 / kFeatures1_DoorShuffleActive
#include "../variables.h"   // g_ram (the features macro)

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

    // `length` is read from an untrusted file. Cap it before any `(long)`-cast
    // fseek below: on LLP64 (Windows x64) `long` is 32-bit, so a length >=
    // 0x80000000 casts to a NEGATIVE seek and the loop could rewind and re-read
    // the same header forever (hang on a corrupt-but-parseable snapshot). The
    // largest legal payload is the RandoState body (52 + 512*2 = 1076); cap well
    // above that and bail on anything larger — a real tail never exceeds it.
    if (length > kRandoSnapshotTail_MaxPayloadBytes) return recognized;

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
      // Reject an ODD or oversized placement_table_bytes BEFORE the fread.
      // The `location_count > 512` check above bounds location_count (=
      // placement_table_bytes/2, integer-truncated), but the fread below uses
      // the raw placement_table_bytes: an odd value like 1025 truncates to
      // location_count==512 (passes the >512 reject) yet would fread 1025
      // bytes into `raw[1024]` — a 1-byte stack overflow. Require an exact
      // even round-trip and a hard ≤1024 cap. Skip the body + continue,
      // mirroring the location_count>512 reject branch.
      if (placement_table_bytes != (uint16)(location_count * 2u) ||
          placement_table_bytes > 1024) {
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
      // Any restore drops an armed mid-staircase spiral redirect (process
      // state; it must not fire on a later unrelated room load).
      DoorRt_ClearSpiralPending();
      // add-rando-door-shuffle — the door redirect / kind-overlay tables are
      // PROCESS state installed by slot activation, not snapshot state. If
      // the restored RAM claims door shuffle but no layout is installed this
      // session (snapshot replayed on a fresh launch / different slot), clear
      // the bit and warn: silently-vanilla doors on a save whose placement
      // assumed the shuffled graph is the worse failure. (A snapshot from a
      // DIFFERENT door-shuffle slot than the activated one keeps the
      // activated layout — same process-state tolerance as the entrance
      // overlay / boss shuffle.)
      if ((enhanced_features1 & kFeatures1_DoorShuffleActive) && !DoorRt_Installed()) {
        fprintf(stderr, "RandoSnapshotTail: door-shuffle bit restored without an "
                        "installed layout — clearing (activate the slot first)\n");
        enhanced_features1 &= ~(uint32)kFeatures1_DoorShuffleActive;
      }
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

  // -------------------------------------------------------------------------
  // §8.9 snapshot replay test.
  // Spec scenario "Replay mode preserves rando": when StateRecorder_Load
  // restores g_ram from `base_snapshot` via LoadSnesState, the trailing TLV
  // reinstall must restore the placement table so every replayed frame's
  // dispatch fires correctly. We can't drive the full StateRecorder pipeline
  // from this self-check (it depends on game-runtime globals), but we CAN
  // exercise the equivalent TLV save → clear → load cycle and verify the
  // placement table comes back identically — which is exactly the state
  // StateRecorder_Load relies on after its LoadSnesState call.
  //
  // The §8.8 ClearKeyLog-on-rando-active forcing in StateRecorder_Save lives
  // in src/zelda_rtl.c; we sanity-check the contract here: Placement_GetActive
  // must be non-NULL when Save is asked to emit, AND HasSnapshotContext must
  // be true. The Save returns true (silently emits nothing) when either is
  // missing — graceful degradation. Verify both branches.
  // -------------------------------------------------------------------------
  {
    // Build a "mid-run" placement table and context — the state the player
    // would be in when they pressed Shift+F1 to snapshot.
    static RandoPlacement replay_entries[3];
    static RandoPlacementTable replay_table;
    replay_entries[0].location_id = 1;   replay_entries[0].item_id = 0xAAAA;
    replay_entries[1].location_id = 5;   replay_entries[1].item_id = 0xBBBB;
    replay_entries[2].location_id = 9;   replay_entries[2].item_id = 0xCCCC;
    replay_table.entries = replay_entries;
    replay_table.count = 3;

    uint8 r_settings[16];
    uint8 r_share[32];
    for (int i = 0; i < 16; i++) r_settings[i] = (uint8)(0x55 ^ i);
    for (int i = 0; i < 32; i++) r_share[i] = (uint8)(0x77 + i);

    Placement_Install(&replay_table);
    Rando_SetSnapshotContext(0xBEEF, r_settings, r_share);

    // "Snapshot save" — emit TLV to a tmpfile.
    FILE *fsnap = tmpfile();
    if (fsnap == NULL) selfcheck_die("§8.9: tmpfile() returned NULL");
    if (!RandoSnapshotTail_Save(fsnap)) selfcheck_die("§8.9: Save returned false");

    // Simulate "exit + relaunch": clear all rando state, as if process died
    // and was restarted with no placement installed.
    Placement_Install(NULL);
    Rando_ClearSnapshotContext();
    if (Placement_GetActive() != NULL) selfcheck_die("§8.9: clear placement should leave Active==NULL");
    if (Rando_HasSnapshotContext()) selfcheck_die("§8.9: clear context should leave HasContext==false");

    // "Replay-mode reload" — TLV consumer reinstalls placement + context.
    fseek(fsnap, 0, SEEK_SET);
    int recognized = RandoSnapshotTail_Load(fsnap);
    if (recognized != 1) selfcheck_die("§8.9: replay reload should recognize 1 TLV");
    if (!Rando_HasSnapshotContext()) {
      selfcheck_die("§8.9: replay reload should restore snapshot context");
    }
    if (Rando_GetSnapshotGeneratorVersion() != 0xBEEF) {
      selfcheck_die("§8.9: replay reload should restore generator_version");
    }
    if (memcmp(Rando_GetSnapshotSettingsHash(), r_settings, 16) != 0) {
      selfcheck_die("§8.9: replay reload should restore settings_hash");
    }
    if (memcmp(Rando_GetSnapshotShareString(), r_share, 32) != 0) {
      selfcheck_die("§8.9: replay reload should restore share_string");
    }
    const RandoPlacementTable *post = Placement_GetActive();
    if (post == NULL) selfcheck_die("§8.9: replay reload should restore placement");
    if (post->count != 3) selfcheck_die("§8.9: placement count after replay reload");
    for (uint16 i = 0; i < 3; i++) {
      if (post->entries[i].location_id != replay_entries[i].location_id ||
          post->entries[i].item_id != replay_entries[i].item_id) {
        selfcheck_die("§8.9: placement entry mismatch after replay reload");
      }
    }
    fclose(fsnap);

    // §8.8 ClearKeyLog contract sanity-check: Save returns true (silent
    // emit) when no placement is installed — the snapshot then has a vanilla
    // tail and older readers see no rando state. This is the path
    // StateRecorder_Save in zelda_rtl.c takes for non-rando snapshots.
    Placement_Install(NULL);
    Rando_ClearSnapshotContext();
    FILE *fsilent = tmpfile();
    if (fsilent == NULL) selfcheck_die("§8.9: tmpfile() silent-path");
    if (!RandoSnapshotTail_Save(fsilent)) selfcheck_die("§8.9: silent-emit path should return true");
    // File should be empty — no TLV header was written.
    fseek(fsilent, 0, SEEK_END);
    long silent_size = ftell(fsilent);
    if (silent_size != 0) selfcheck_die("§8.9: silent-emit path should write zero bytes");
    fclose(fsilent);

    // Same path for "placement installed but no context" — Save returns true
    // and emits nothing (cannot fabricate the payload metadata).
    Placement_Install(&replay_table);
    FILE *fno_ctx = tmpfile();
    if (fno_ctx == NULL) selfcheck_die("§8.9: tmpfile() no-ctx-path");
    if (!RandoSnapshotTail_Save(fno_ctx)) selfcheck_die("§8.9: no-context Save should return true");
    fseek(fno_ctx, 0, SEEK_END);
    long no_ctx_size = ftell(fno_ctx);
    if (no_ctx_size != 0) selfcheck_die("§8.9: no-context Save should write zero bytes");
    fclose(fno_ctx);
  }

  // -------------------------------------------------------------------------
  // §8.10 older-binary snapshot test.
  // Spec scenario "Older binary degrades gracefully": a TLV-aware writer's
  // output, when consumed by a reader that stops after the standard 4
  // chunks (older binary predates RandoSnapshotTail), leaves the older
  // binary in a clean vanilla state — the trailing tail bytes are simply
  // ignored.
  //
  // Two assertions to make:
  //
  //   1. Reading a snapshot that has NO trailing TLV (vanilla snapshot)
  //      cleanly returns 0 from RandoSnapshotTail_Load — no false positives.
  //      This is the equivalent test for the modern reader: it must not
  //      claim to recognize a TLV when none was emitted.
  //
  //   2. Reading a snapshot whose tail is a TLV with an UNKNOWN type code
  //      cleanly skips it and continues. This is the forward-compat
  //      property that lets older binaries cope with newer snapshots — the
  //      unknown-type-skip path is the same code path an older binary
  //      would use if it were extended to know about TLVs in general but
  //      not the specific new type. (An older binary that knows nothing
  //      about TLVs at all just closes the file after the 4 chunks; we
  //      can't simulate "close the file early" inside a self-check, but
  //      we CAN exercise the skip-then-continue path which is the same
  //      forward-compat machinery.)
  // -------------------------------------------------------------------------
  {
    // (1) Vanilla snapshot — no TLV bytes. Load should return 0 cleanly.
    FILE *fvanilla = tmpfile();
    if (fvanilla == NULL) selfcheck_die("§8.10: tmpfile() vanilla");
    // Write zero bytes — pure empty file, simulating a snapshot whose
    // standard chunks have already been consumed by the standard reader.
    fseek(fvanilla, 0, SEEK_SET);
    int n_vanilla = RandoSnapshotTail_Load(fvanilla);
    if (n_vanilla != 0) {
      selfcheck_die("§8.10: vanilla (empty) tail should yield zero recognized TLVs");
    }
    // Re-confirm prior state was untouched. We don't have a "snapshot prior"
    // saved here — just verify the loader was a pure no-op on its outputs.
    fclose(fvanilla);

    // (1b) "Garbage trailing bytes" (e.g., unrelated trailing payload from
    // a different format) — first 8 bytes don't match TLV magic, so loader
    // terminates cleanly. Mirrors the older-binary case where the trailing
    // bytes are simply foreign data the modern loader must reject without
    // crashing.
    FILE *fgarbage = tmpfile();
    if (fgarbage == NULL) selfcheck_die("§8.10: tmpfile() garbage");
    uint8 not_magic[8] = { 'X', 'Y', 'Z', 'Q', 'A', 'B', 'C', 'D' };
    fwrite(not_magic, 1, sizeof(not_magic), fgarbage);
    fseek(fgarbage, 0, SEEK_SET);
    int n_garbage = RandoSnapshotTail_Load(fgarbage);
    if (n_garbage != 0) {
      selfcheck_die("§8.10: non-matching magic should terminate loop cleanly (zero recognized)");
    }
    fclose(fgarbage);

    // (1c) Truncated TLV header (less than 8 bytes — magic incomplete).
    // Older-binary equivalent: snapshot file ends abruptly inside what would
    // have been a TLV. Loader returns 0 without crashing.
    FILE *ftrunc = tmpfile();
    if (ftrunc == NULL) selfcheck_die("§8.10: tmpfile() truncated");
    uint8 trunc[3] = { 'Z', 'R', 'S' };  // 3 of 8 magic bytes — short read
    fwrite(trunc, 1, sizeof(trunc), ftrunc);
    fseek(ftrunc, 0, SEEK_SET);
    int n_trunc = RandoSnapshotTail_Load(ftrunc);
    if (n_trunc != 0) {
      selfcheck_die("§8.10: short-read magic should terminate cleanly");
    }
    fclose(ftrunc);

    // (2) Unknown-type TLV — emit a valid magic + unknown type discriminator
    // + valid length, and verify the loader skips past it without claiming
    // recognition (returns 0 because the only TLV present was unknown).
    // This is the forward-compat property: future TLV types (e.g., Phase C
    // entrance-shuffle data) shipped to older binaries get cleanly skipped.
    FILE *funknown = tmpfile();
    if (funknown == NULL) selfcheck_die("§8.10: tmpfile() unknown-type");
    uint8 unk_hdr2[16];
    memcpy(unk_hdr2, kRandoSnapshotTail_Magic, 8);
    put_u32le_bytes(unk_hdr2 + 8, 0xDEADBEEFu);  // unknown type
    put_u32le_bytes(unk_hdr2 + 12, 12u);          // payload length
    fwrite(unk_hdr2, 1, 16, funknown);
    uint8 unk_payload[12] = { 0,1,2,3,4,5,6,7,8,9,10,11 };
    fwrite(unk_payload, 1, sizeof(unk_payload), funknown);
    fseek(funknown, 0, SEEK_SET);
    int n_unknown = RandoSnapshotTail_Load(funknown);
    if (n_unknown != 0) {
      selfcheck_die("§8.10: unknown TLV type should be skipped (zero recognized)");
    }
    // Confirm we've consumed past the unknown TLV — the file cursor should
    // be at the unknown payload's end (16 + 12 = 28). If the loader had
    // truncated early or seek-overshot, the cursor would be elsewhere.
    long cursor_after_unknown = ftell(funknown);
    if (cursor_after_unknown != 28L) {
      selfcheck_die("§8.10: unknown-TLV skip should advance cursor exactly past payload");
    }
    fclose(funknown);

    // (3) Mixed-tail file: unknown TLV followed by recognized TAIL_RANDO_STATE.
    // Loader returns 1 (the recognized one), having skipped the unknown.
    // This is the same path exercised earlier in this self-check but reframed
    // as a §8.10 forward-compat assertion: the recognized TLV survives the
    // presence of unknown predecessors.
    static RandoPlacement mix_entries[2];
    static RandoPlacementTable mix_table;
    mix_entries[0].location_id = 0; mix_entries[0].item_id = 0x1111;
    mix_entries[1].location_id = 3; mix_entries[1].item_id = 0x2222;
    mix_table.entries = mix_entries; mix_table.count = 2;
    uint8 mix_settings[16];
    uint8 mix_share[32];
    for (int i = 0; i < 16; i++) mix_settings[i] = (uint8)i;
    for (int i = 0; i < 32; i++) mix_share[i] = (uint8)(0x80 + i);
    Placement_Install(&mix_table);
    Rando_SetSnapshotContext(0x0042, mix_settings, mix_share);

    FILE *fmix = tmpfile();
    if (fmix == NULL) selfcheck_die("§8.10: tmpfile() mixed");
    // Unknown TLV first.
    uint8 mix_hdr[16];
    memcpy(mix_hdr, kRandoSnapshotTail_Magic, 8);
    put_u32le_bytes(mix_hdr + 8, 0xBADC0DE5u);
    put_u32le_bytes(mix_hdr + 12, 4u);
    fwrite(mix_hdr, 1, 16, fmix);
    uint8 mix_pay[4] = { 9, 8, 7, 6 };
    fwrite(mix_pay, 1, 4, fmix);
    // Then the recognized TLV.
    if (!RandoSnapshotTail_Save(fmix)) selfcheck_die("§8.10: Save (mixed) returned false");
    // Clear and reload.
    Placement_Install(NULL);
    Rando_ClearSnapshotContext();
    fseek(fmix, 0, SEEK_SET);
    int n_mix = RandoSnapshotTail_Load(fmix);
    if (n_mix != 1) selfcheck_die("§8.10: mixed-tail should recognize exactly 1 known TLV");
    if (Rando_GetSnapshotGeneratorVersion() != 0x0042) {
      selfcheck_die("§8.10: mixed-tail should restore generator_version through the skip");
    }
    fclose(fmix);
  }

  // Restore prior state to leave the world unchanged.
  Placement_Install(prior_table);
  if (prior_table == NULL) Rando_ClearSnapshotContext();

  fprintf(stderr, "[RandoSnapshotTail_SelfCheck] OK\n");
}
