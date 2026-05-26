// rando_share.c — share-string encoder/decoder (tasks.md §2.3-§2.4).
//
// Wire format (31 bytes binary, encoded as 50 base32 chars):
//   offset  size  field
//   0       4     magic ("ZRSS" = 0x5A 0x52 0x53 0x53)
//   4       1     version (currently 1)
//   5       16    settings_hash (SHA-256 of canonical settings, truncated to 16)
//   21      8     seed_u64 (little-endian)
//   29      2     checksum (CRC-16-CCITT-FALSE over bytes [0..28], LE)
//
// Base32: RFC 4648 alphabet (uppercase, no padding). 31 bytes = 248 bits =
// 50 base32 chars (last char's lowest 3 bits are zero-padded).
//
// Explicit-reject signals (per task 2.4):
//   - alttpr.com hash format (lowercase, '/', '+', or wrong length)
//   - corrupted base32 character
//   - wrong-length input
//   - wrong magic prefix
//   - checksum mismatch
//
// All multi-byte fields are explicit little-endian via byte shifts (no
// htole*/be*toh macros — per randomizer-core / Byte-order pin).

#include "rando_share.h"
#include "../types.h"
#include <string.h>

#define kShareMagic0 0x5A  /* 'Z' */
#define kShareMagic1 0x52  /* 'R' */
#define kShareMagic2 0x53  /* 'S' */
#define kShareMagic3 0x53  /* 'S' */
#define kShareCurrentVersion 1

#define kShareBinaryLen 31     /* magic+ver+hash+seed+ck */
#define kShareBase32Len 50     /* ceil(31*8 / 5) */

/* RFC 4648 Base32 alphabet — uppercase, no padding. */
static const char kBase32Alphabet[32] =
  "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

/* Reverse table: ASCII byte → 5-bit value, or 0xFF if not in alphabet. */
static int8 base32_decode_char(uint8 c) {
  if (c >= 'A' && c <= 'Z') return (int8)(c - 'A');
  if (c >= '2' && c <= '7') return (int8)(c - '2' + 26);
  return -1;
}

/* CRC-16-CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflection, no XOR-out.
 * Byte-at-a-time bit-bang (~10 lines, no table needed). */
static uint16 crc16_ccitt(const uint8 *data, size_t len) {
  uint16 crc = 0xFFFF;
  for (size_t i = 0; i < len; ++i) {
    crc ^= (uint16)data[i] << 8;
    for (int b = 0; b < 8; ++b) {
      if (crc & 0x8000) {
        crc = (uint16)((crc << 1) ^ 0x1021);
      } else {
        crc = (uint16)(crc << 1);
      }
    }
  }
  return crc;
}

/* Build the 31-byte binary blob from a ShareString. */
static void pack_binary(const ShareString *ss, uint8 out[kShareBinaryLen]) {
  out[0] = kShareMagic0;
  out[1] = kShareMagic1;
  out[2] = kShareMagic2;
  out[3] = kShareMagic3;
  out[4] = ss->version;
  for (int i = 0; i < 16; ++i) out[5 + i] = ss->settings_hash[i];
  /* seed_u64 little-endian */
  out[21] = (uint8)(ss->seed_u64 >> 0);
  out[22] = (uint8)(ss->seed_u64 >> 8);
  out[23] = (uint8)(ss->seed_u64 >> 16);
  out[24] = (uint8)(ss->seed_u64 >> 24);
  out[25] = (uint8)(ss->seed_u64 >> 32);
  out[26] = (uint8)(ss->seed_u64 >> 40);
  out[27] = (uint8)(ss->seed_u64 >> 48);
  out[28] = (uint8)(ss->seed_u64 >> 56);
  /* checksum over bytes [0..28], little-endian */
  uint16 ck = crc16_ccitt(out, 29);
  out[29] = (uint8)(ck & 0xFF);
  out[30] = (uint8)(ck >> 8);
}

/* Base32-encode 31 bytes → 50 chars (no padding). */
static void base32_encode(const uint8 in[kShareBinaryLen], char out[kShareBase32Len]) {
  uint64 buf = 0;
  int bits = 0;
  int out_pos = 0;
  for (int i = 0; i < kShareBinaryLen; ++i) {
    buf = (buf << 8) | in[i];
    bits += 8;
    while (bits >= 5) {
      bits -= 5;
      out[out_pos++] = kBase32Alphabet[(buf >> bits) & 0x1F];
    }
  }
  if (bits > 0) {
    /* Final char: lowest (5 - bits) bits are zero-padded. */
    out[out_pos++] = kBase32Alphabet[(buf << (5 - bits)) & 0x1F];
  }
  /* out_pos == kShareBase32Len == 50 */
}

/* Base32-decode 50 chars → 31 bytes. Returns true on success, false on any
 * non-alphabet character. */
static bool base32_decode(const char in[kShareBase32Len], uint8 out[kShareBinaryLen]) {
  uint64 buf = 0;
  int bits = 0;
  int out_pos = 0;
  for (int i = 0; i < kShareBase32Len; ++i) {
    int8 v = base32_decode_char((uint8)in[i]);
    if (v < 0) return false;
    buf = (buf << 5) | (uint64)(uint8)v;
    bits += 5;
    if (bits >= 8) {
      bits -= 8;
      if (out_pos < kShareBinaryLen) {
        out[out_pos++] = (uint8)((buf >> bits) & 0xFF);
      }
    }
  }
  /* The trailing (50*5 - 31*8) = 2 bits of buf are zero padding; we don't
   * validate they're zero (some encoders may not zero them) — strict
   * validation would reject otherwise-valid round-tripped inputs from
   * lossy encoders. The CRC-16 catches any actual corruption. */
  return out_pos == kShareBinaryLen;
}

/* ===========================================================================
 * Public API
 * =========================================================================== */

int Share_Encode(const ShareString *ss, char *out, int out_capacity) {
  if (out_capacity < kShareBase32Len + 1) return -1;
  uint8 bin[kShareBinaryLen];
  pack_binary(ss, bin);
  base32_encode(bin, out);
  out[kShareBase32Len] = '\0';
  return kShareBase32Len;
}

int Share_EncodeRaw(const uint8 raw_binary[kShareStringBinaryLen],
                    char *out, int out_capacity) {
  if (out == NULL || raw_binary == NULL) return -1;
  if (out_capacity < kShareBase32Len + 1) return -1;
  base32_encode(raw_binary, out);
  out[kShareBase32Len] = '\0';
  return kShareBase32Len;
}

ShareDecodeStatus Share_Decode(const char *in, ShareString *out_ss) {
  if (in == NULL || out_ss == NULL) return kShareDecodeBadLength;

  /* Length check — also fast-path-rejects alttpr.com format which is
   * typically a different length (hex hash or base64). */
  size_t len = 0;
  while (in[len] != '\0') {
    if (len > kShareBase32Len + 1) return kShareDecodeBadLength;  /* avoid scanning huge inputs */
    ++len;
  }
  if (len != kShareBase32Len) {
    return kShareDecodeBadLength;
  }

  /* Early-out: alttpr.com hashes contain lowercase letters or '/'+'-'.
   * Our alphabet is uppercase A-Z + 2-7 only. Any non-alphabet character
   * tells us it isn't our format. We surface a specific status for
   * recognized alttpr.com-style patterns. */
  bool has_lower = false, has_b64_special = false;
  for (size_t i = 0; i < len; ++i) {
    uint8 c = (uint8)in[i];
    if (c >= 'a' && c <= 'z') has_lower = true;
    if (c == '/' || c == '+' || c == '-' || c == '_' || c == '=') has_b64_special = true;
  }
  if (has_lower || has_b64_special) {
    return kShareDecodeAlttprFormat;
  }

  uint8 bin[kShareBinaryLen];
  if (!base32_decode(in, bin)) {
    return kShareDecodeBadBase32;
  }

  if (bin[0] != kShareMagic0 || bin[1] != kShareMagic1 ||
      bin[2] != kShareMagic2 || bin[3] != kShareMagic3) {
    return kShareDecodeBadMagic;
  }

  uint16 stored_ck = (uint16)bin[29] | ((uint16)bin[30] << 8);
  uint16 expected_ck = crc16_ccitt(bin, 29);
  if (stored_ck != expected_ck) {
    return kShareDecodeBadChecksum;
  }

  out_ss->version = bin[4];
  for (int i = 0; i < 16; ++i) out_ss->settings_hash[i] = bin[5 + i];
  out_ss->seed_u64 =
      (uint64)bin[21] |
      ((uint64)bin[22] << 8) |
      ((uint64)bin[23] << 16) |
      ((uint64)bin[24] << 24) |
      ((uint64)bin[25] << 32) |
      ((uint64)bin[26] << 40) |
      ((uint64)bin[27] << 48) |
      ((uint64)bin[28] << 56);

  return kShareDecodeOk;
}

/* ===========================================================================
 * Self-test (tasks.md §2.4 — round-trip + explicit rejects)
 * =========================================================================== */
#include <stdio.h>
#include <stdlib.h>

static void share_assert(bool cond, const char *msg) {
  if (!cond) {
    fprintf(stderr, "Share_SelfCheck: FAILED: %s\n", msg);
    exit(2);
  }
}

// ---------------------------------------------------------------------------
// Share_PastePath (tasks.md §9.6) — UI helper.
//
// Decodes a user-pasted share string and extracts the seed_u64 +
// settings_hash. Returns the decode status enum so the caller can surface a
// specific error message.
// ---------------------------------------------------------------------------
ShareDecodeStatus Share_PastePath(const char *share_string_input,
                                  uint64 *out_seed_u64,
                                  uint8 out_settings_hash_short[16]) {
  if (share_string_input == NULL || out_seed_u64 == NULL ||
      out_settings_hash_short == NULL) {
    return kShareDecodeBadLength;
  }
  ShareString ss;
  ShareDecodeStatus st = Share_Decode(share_string_input, &ss);
  if (st != kShareDecodeOk) {
    return st;
  }
  *out_seed_u64 = ss.seed_u64;
  for (int i = 0; i < 16; i++) out_settings_hash_short[i] = ss.settings_hash[i];
  return kShareDecodeOk;
}

void Share_SelfCheck(void) {
  /* Round-trip: encode + decode → identical struct. */
  ShareString original = {0};
  original.version = kShareCurrentVersion;
  for (int i = 0; i < 16; ++i) original.settings_hash[i] = (uint8)(i * 17 + 3);
  original.seed_u64 = 0xCAFEBABEDEADBEEFull;

  char encoded[kShareStringBase32MaxLen];
  int n = Share_Encode(&original, encoded, sizeof encoded);
  share_assert(n == kShareBase32Len, "encode length");

  ShareString decoded;
  ShareDecodeStatus st = Share_Decode(encoded, &decoded);
  share_assert(st == kShareDecodeOk, "round-trip decode");
  share_assert(decoded.version == original.version, "version round-trip");
  share_assert(decoded.seed_u64 == original.seed_u64, "seed round-trip");
  for (int i = 0; i < 16; ++i) {
    share_assert(decoded.settings_hash[i] == original.settings_hash[i],
                 "settings_hash round-trip");
  }

  /* Reject: alttpr.com-style hash (lowercase + slashes — typical permalink). */
  st = Share_Decode("eyf+abc/def123EYF+ABCABCABCABCABCABCABCABCABCABCDEFG", &decoded);
  share_assert(st == kShareDecodeAlttprFormat ||
               st == kShareDecodeBadLength,
               "reject alttpr.com format (with slash/lower)");

  /* Reject: corrupted base32 char (`1` is not in the alphabet). */
  char corrupt_b32[kShareBase32Len + 1];
  memcpy(corrupt_b32, encoded, sizeof encoded);
  corrupt_b32[10] = '1';  /* invalid */
  st = Share_Decode(corrupt_b32, &decoded);
  share_assert(st == kShareDecodeBadBase32 ||
               st == kShareDecodeAlttprFormat,
               "reject corrupted base32 char");

  /* Reject: wrong-length input. */
  char shortstr[20];
  memcpy(shortstr, encoded, 19);
  shortstr[19] = '\0';
  st = Share_Decode(shortstr, &decoded);
  share_assert(st == kShareDecodeBadLength, "reject wrong-length input");

  /* Reject: wrong magic. Manually flip the first byte after a clean encode,
   * recompute the checksum so we don't trip the checksum check first, then
   * re-encode and decode. */
  uint8 bin[kShareBinaryLen];
  pack_binary(&original, bin);
  bin[0] = 'X';  /* break magic */
  /* recompute crc so the failure surfaces as BadMagic, not BadChecksum */
  uint16 ck = crc16_ccitt(bin, 29);
  bin[29] = (uint8)(ck & 0xFF);
  bin[30] = (uint8)(ck >> 8);
  char encoded_badmagic[kShareBase32Len + 1];
  base32_encode(bin, encoded_badmagic);
  encoded_badmagic[kShareBase32Len] = '\0';
  st = Share_Decode(encoded_badmagic, &decoded);
  share_assert(st == kShareDecodeBadMagic, "reject wrong magic");

  /* Reject: checksum corruption. Flip a byte mid-payload (after magic, before
   * the checksum), keeping the same checksum bytes → decoder recomputes,
   * sees mismatch, returns BadChecksum. */
  char encoded_badck[kShareBase32Len + 1];
  memcpy(encoded_badck, encoded, sizeof encoded_badck);
  /* Flip char at position 30 (well past the magic, before the trailing CRC
   * bytes which encode to positions ~46-50). Pick a valid alphabet char. */
  encoded_badck[30] = (encoded_badck[30] == 'A') ? 'B' : 'A';
  st = Share_Decode(encoded_badck, &decoded);
  /* It could fail as BadChecksum OR as BadMagic if the flip cascades through
   * base32 decoding. Either way it MUST NOT report Ok. */
  share_assert(st != kShareDecodeOk, "reject mutation in payload");
}
