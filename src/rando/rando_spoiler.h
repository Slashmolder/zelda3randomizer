// rando_spoiler.h — JSON + text spoiler writer (tasks.md §5.1-§5.2). Stub.
//
// Stable field names per the spec: share_string, generator_version, settings,
// placements[], sphere_data[], goal_completable, generation_wall_clock_ms,
// fallback_warnings[].

#ifndef ZELDA3_RANDO_SPOILER_H_
#define ZELDA3_RANDO_SPOILER_H_

#include "../types.h"
#include "rando_placement.h"
#include "rando_settings.h"  // kSettingsCanonicalLen for the _Static_assert below

typedef struct RandoSpoiler {
  const char *share_string;
  uint64 seed_u64;                   // per spec — emitted in meta block
  uint32 generator_version;
  const RandoSettings *settings;
  const RandoPlacementTable *placements;
  const RandoSpheres *spheres;       // optional; NULL omits sphere_data
  // Placer fallback counters — populated from Place_AssumedFill outputs.
  // Each non-zero counter produces one fallback_warnings[] entry in JSON
  // and a banner in the text spoiler. Per audit Bug #8.
  uint32 forward_fill_fallback_count;  // # of placements that fell back to forward-fill
  uint32 retry_attempts;               // # of assumed-fill attempts taken before success
  uint32 generation_wall_clock_ms;
  bool goal_completable;
} RandoSpoiler;

// Write JSON spoiler to `out_path`. Returns true on success, false on I/O
// failure. The JSON shape is byte-identical given the same `RandoSpoiler`
// input (sorted keys, no whitespace variation) so the file's SHA-256 is a
// stable identity for race-mode stamping.
bool Spoiler_WriteJson(const RandoSpoiler *s, const char *out_path);

// Write text spoiler grouped by region for human readability.
bool Spoiler_WriteText(const RandoSpoiler *s, const char *out_path);

// ---------------------------------------------------------------------------
// Phase B Slice 6 — race-mode spoiler suppression.
//
// Top-level entry that writes the JSON + .txt spoiler when `s->settings->race_mode == 0`,
// or the suppressed binary form alone (no .txt) when `race_mode == 1`. The
// suppressed form on disk is the file at `json_path` — the magic header
// (`ZRSR`) is what callers use to distinguish; the filename is unchanged.
//
// Returns true on success.
//
// `txt_path` may be NULL to skip the text companion in both branches; race
// mode always skips the text companion regardless of `txt_path`.
// ---------------------------------------------------------------------------
bool Spoiler_Write(const RandoSpoiler *s,
                   const char *json_path,
                   const char *txt_path);

// ---------------------------------------------------------------------------
// Suppressed-spoiler on-disk format (Phase B Slice 6 — randomizer-save spec).
//
// The file at `<spoiler_dir>/<share_string>.json` contains, in race mode, the
// bytes described by RandoSuppressedSpoiler (little-endian multibyte ints):
//
//   offset    size   field
//   ------    ----   --------------------------------
//   +0        4      magic[4] = "ZRSR"
//   +4        2      generator_version (u16 LE)
//   +6        32     spoiler_stamp[32] = SHA-256 of the full canonical
//                    JSON spoiler with race_mode cleared to 0 and
//                    generation_wall_clock_ms cleared to 0 (so the stamp
//                    is reproducible across runs)
//   +38       4      share_string_len (u32 LE) — actual length without NUL
//   +42       64     share_string[64] — base32 textual share string,
//                    zero-padded
//   +106      24     settings_canonical[24] = Settings_CanonicalSerialize
//                    output with race_mode cleared to 0 (race-mode flag
//                    itself is recorded in the file's existence, not in
//                    the serialized settings). Needed at reveal time to
//                    regenerate the placement deterministically.
//   +130      4      crc32 (u32 LE) — IEEE 802.3 over offsets 0..129
//
// Total: 134 bytes. The struct below is for in-memory parsing; the on-disk
// layout is the byte sequence above (we serialize explicitly to avoid
// platform-dependent padding).
//
// Why settings are in the file: the per-slot sidecar (RandoSidecarSlot) does
// NOT preserve the original RandoSettings — only `settings_hash` (a truncated
// SHA-256, not invertible). To regenerate the placement and verify the
// stamp, the reveal pipeline needs the original settings. Including them
// here doesn't leak the placement (the secret); settings are already
// publicly visible on any tournament settings sheet.
// ---------------------------------------------------------------------------
#define kRandoSuppressedSpoilerMagic "ZRSR"
#define kRandoSuppressedSpoilerShareStringMax 64
#define kRandoSuppressedSpoilerSettingsLen 28  // = kSettingsCanonicalLen (last changed at kGenVer 14 §66; stable through current)
#define kRandoSuppressedSpoilerSize 138  // on-disk byte length (last changed at kGenVer 14: 134→138; stable through current)

// Compile-time guard — when kSettingsCanonicalLen bumps, this assert
// forces a coupled update to kRandoSuppressedSpoilerSettingsLen +
// kRandoSuppressedSpoilerSize + the CRC32 offsets in rando_spoiler.c +
// the corpus runner constants. Caught at build time rather than at
// the run_rando_corpus race-mode regression (which is how the
// kGenVer 14 cycle first surfaced the gap).
_Static_assert(kRandoSuppressedSpoilerSettingsLen == kSettingsCanonicalLen,
               "ZRSR settings_canonical span must match kSettingsCanonicalLen; "
               "bump kRandoSuppressedSpoilerSettingsLen AND kRandoSuppressedSpoilerSize "
               "AND the CRC32 offsets in rando_spoiler.c serialize/read AND the "
               "126/130/134/138 constants in assets/scripts/{bump,run}_rando_corpus.py.");

// TODO(durability sweep 2026-05-27): the 3 hard-coded `134` literals in
// rando_spoiler.c (serialize/write/read at lines ~348/376/484) should
// become `#define kRandoSuppressedSpoilerCrcOffset (kRandoSuppressedSpoilerSize - 4)`
// + an additional `_Static_assert` that the offset equals the expected
// position. Cheap defensive follow-up — would catch 3 more coupled sites
// at compile time instead of the current "find via corpus regression"
// path. See memory `canonical_size_coupling.md` for the lesson.

typedef struct RandoSuppressedSpoiler {
  uint8 magic[4];                // 'ZRSR'
  uint16 generator_version;      // LE on disk
  uint8 spoiler_stamp[32];       // SHA-256
  uint32 share_string_len;       // actual length (<= 64)
  uint8 share_string[kRandoSuppressedSpoilerShareStringMax]; // zero-padded
  uint8 settings_canonical[kRandoSuppressedSpoilerSettingsLen];
  uint32 crc32;                  // IEEE 802.3 over prior bytes (LE on disk)
} RandoSuppressedSpoiler;

// Read the suppressed spoiler at `path` into `out`, verifying magic + CRC.
// Returns 0 on success.
// Returns -1 on file-not-found, -2 on bad magic / parse error, -3 on CRC mismatch.
int Spoiler_ReadSuppressed(const char *path, RandoSuppressedSpoiler *out);

// Resolve the spoiler path for a slot's share string. The path is derived
// at runtime from `[Randomizer] SpoilerDir` (or `<exe-dir>/spoilers` when
// the INI key is unset) + the slot's share string + the extension.
//
// Per `randomizer-core / Spoiler-log emission`: the path is NOT stored in
// the slot — it's recomputed whenever the spoiler needs to be written or
// surfaced. This keeps slots invariant under config changes.
//
// Writes into `out_path` (max `out_capacity` bytes including NUL). Returns
// the length written (excluding NUL) or 0 on failure (buffer too small).
//
// Caller responsibility: the directory `<spoiler_dir>` must exist when the
// spoiler is actually written (Spoiler_WriteJson does not create it).
int Spoiler_ResolvePath(const char *share_string,
                        const char *extension_with_dot,  // ".json" or ".txt"
                        char *out_path, int out_capacity);

#endif  // ZELDA3_RANDO_SPOILER_H_
