// rando_share.h — share-string encoder/decoder (tasks.md §2.3-§2.4). Stub.
//
// Format (per randomizer-core spec):
//   magic[4] (LE) | version[1] | settings_hash[16] | seed_u64[8] | checksum[2]
// = 31 bytes binary, base32-encoded.
//
// Explicit rejects: alttpr.com format hashes, corrupted base32, wrong magic,
// wrong length.

#ifndef ZELDA3_RANDO_SHARE_H_
#define ZELDA3_RANDO_SHARE_H_

#include "../types.h"

#define kShareStringBinaryLen 31  // before base32
#define kShareStringBase32MaxLen 64  // generous upper bound; actual ~50

typedef enum {
  kShareDecodeOk = 0,
  kShareDecodeBadLength,
  kShareDecodeBadBase32,
  kShareDecodeBadMagic,
  kShareDecodeBadChecksum,
  kShareDecodeAlttprFormat,  // explicit reject of alttpr.com-style hashes
} ShareDecodeStatus;

typedef struct ShareString {
  uint8 version;
  uint8 settings_hash[16];
  uint64 seed_u64;
} ShareString;

// Encode `ss` into `out` (base32). Returns the number of bytes written
// (excluding the trailing NUL); caller passes `out_capacity` >= kShareStringBase32MaxLen.
int Share_Encode(const ShareString *ss, char *out, int out_capacity);

// Decode `in` (base32, NUL-terminated) into `out_ss`. Returns the
// decode-status enum; success is `kShareDecodeOk`.
ShareDecodeStatus Share_Decode(const char *in, ShareString *out_ss);

// Self-test (tasks.md §2.4). Round-trip + 5 explicit-reject tests. Exits
// the process with code 2 on any failure.
void Share_SelfCheck(void);

// ---------------------------------------------------------------------------
// Share-string paste path (tasks.md §9.6). UI helper that combines
// Share_Decode with a settings-population callback.
//
// `share_string_input` is the user-entered text (typically from RandoTextField).
// On success: decodes to ShareString, populates `out_seed_u64` and writes the
// settings_hash to `out_settings_hash_short[16]`. Returns kShareDecodeOk.
//
// On failure: returns the specific reject status (alttpr format, bad base32,
// wrong magic, bad checksum, bad length). The UI can surface a specific
// error message per status.
//
// Note: this does NOT regenerate settings — settings_hash is one-way, so the
// user must still pick settings independently. The settings_hash is recorded
// so the UI can warn when the user's settings don't match the pasted seed's
// originating settings.
// ---------------------------------------------------------------------------
ShareDecodeStatus Share_PastePath(const char *share_string_input,
                                  uint64 *out_seed_u64,
                                  uint8 out_settings_hash_short[16]);

#endif  // ZELDA3_RANDO_SHARE_H_

