// rando_spoiler.h — JSON + text spoiler writer (tasks.md §5.1-§5.2). Stub.
//
// Stable field names per the spec: share_string, generator_version, settings,
// placements[], sphere_data[], goal_completable, generation_wall_clock_ms,
// fallback_warnings[].

#ifndef ZELDA3_RANDO_SPOILER_H_
#define ZELDA3_RANDO_SPOILER_H_

#include "../types.h"
#include "rando_placement.h"

typedef struct RandoSpoiler {
  const char *share_string;
  uint32 generator_version;
  const RandoSettings *settings;
  const RandoPlacementTable *placements;
  const RandoSpheres *spheres;       // optional; NULL omits sphere_data
  // fallback_warnings populated by the spoiler generator
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
