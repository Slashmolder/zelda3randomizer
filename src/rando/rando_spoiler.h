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
  // sphere_data, fallback_warnings populated by the spoiler generator
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

#endif  // ZELDA3_RANDO_SPOILER_H_
