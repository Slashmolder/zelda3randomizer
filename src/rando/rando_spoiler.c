// rando_spoiler.c — JSON + text spoiler writer (tasks.md §5.1, §5.2).
//
// Phase A0 emits a minimal-but-valid spoiler that exercises the file-write
// path. Phase A1 fills in sphere_data, fallback_warnings, and the full
// placements[] body grouped by region.
//
// Determinism: JSON output is byte-identical for byte-identical input
// (sorted keys; no whitespace variation; entries iterated in location_id
// order). The file's SHA-256 is a stable identity for race-mode stamping
// (task 5.3, Phase B).

#include "rando_spoiler.h"
#include "rando_placement.h"
#include "rando_settings.h"
#include "rando.h"
#include "../config.h"
#include "../types.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Generated tables — typedefs + extern declarations live in rando_logic.h.
#include "rando_logic.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static void write_hex(FILE *f, const uint8 *bytes, size_t n) {
  static const char hex[] = "0123456789abcdef";
  for (size_t i = 0; i < n; i++) {
    fputc(hex[bytes[i] >> 4], f);
    fputc(hex[bytes[i] & 0xf], f);
  }
}

static int placement_cmp(const void *a, const void *b) {
  const RandoPlacement *pa = a;
  const RandoPlacement *pb = b;
  if (pa->location_id < pb->location_id) return -1;
  if (pa->location_id > pb->location_id) return 1;
  return 0;
}

// ---------------------------------------------------------------------------
// JSON writer
// ---------------------------------------------------------------------------
bool Spoiler_WriteJson(const RandoSpoiler *s, const char *out_path) {
  if (s == NULL || out_path == NULL) return false;

  FILE *f = fopen(out_path, "wb");
  if (f == NULL) return false;

  // Compute hashes/digests we'll cite in the meta block.
  uint8 settings_hash[32];
  Settings_ComputeHash(s->settings, settings_hash);
  uint8 placement_digest[32];
  PlacementTable_ComputeDigest(s->placements, placement_digest);

  // -----------------------------------------------------------------------
  // Meta block — names match ALTTPR's "meta" object where applicable (per
  // randomizer-core / "Spoiler JSON schema mirrors ALTTPR field names").
  // -----------------------------------------------------------------------
  fprintf(f, "{\n");
  fprintf(f, "  \"meta\": {\n");
  fprintf(f, "    \"spoiler_format_version\": 1,\n");
  fprintf(f, "    \"generator_version\": %u,\n", (unsigned)s->generator_version);
  fprintf(f, "    \"settings_hash_hex\": \"");
  write_hex(f, settings_hash, 16);  // first 16 bytes, matching share-string truncation
  fprintf(f, "\",\n");
  fprintf(f, "    \"settings_hash_full_hex\": \"");
  write_hex(f, settings_hash, 32);
  fprintf(f, "\",\n");
  fprintf(f, "    \"placement_digest_hex\": \"");
  write_hex(f, placement_digest, 32);
  fprintf(f, "\",\n");
  fprintf(f, "    \"share_string\": \"%s\",\n",
          s->share_string != NULL ? s->share_string : "");
  fprintf(f, "    \"world_state\": %u,\n", (unsigned)s->settings->world_state);
  fprintf(f, "    \"goal\": %u,\n", (unsigned)s->settings->goal);
  fprintf(f, "    \"generation_wall_clock_ms\": %u,\n",
          (unsigned)s->generation_wall_clock_ms);
  fprintf(f, "    \"goal_completable\": %s,\n",
          s->goal_completable ? "true" : "false");
  fprintf(f, "    \"fallback_warnings\": []\n");
  fprintf(f, "  },\n");

  // -----------------------------------------------------------------------
  // Settings — full struct as a flat object. Field order matches the C
  // struct (NOT the canonical-serialization order; that's a determinism
  // contract for the hash, not a human-readable surface).
  // -----------------------------------------------------------------------
  fprintf(f, "  \"settings\": {\n");
  fprintf(f, "    \"settings_version\": %u,\n", s->settings->settings_version);
  fprintf(f, "    \"world_state\": %u,\n", s->settings->world_state);
  fprintf(f, "    \"goal\": %u,\n", s->settings->goal);
  fprintf(f, "    \"crystals_ganon\": %u,\n", s->settings->crystals_ganon);
  fprintf(f, "    \"crystals_tower\": %u,\n", s->settings->crystals_tower);
  fprintf(f, "    \"item_pool_difficulty\": %u,\n", s->settings->item_pool_difficulty);
  fprintf(f, "    \"dungeon_small_keys_mode\": %u,\n", s->settings->dungeon_small_keys_mode);
  fprintf(f, "    \"dungeon_big_keys_mode\": %u,\n", s->settings->dungeon_big_keys_mode);
  fprintf(f, "    \"dungeon_maps_mode\": %u,\n", s->settings->dungeon_maps_mode);
  fprintf(f, "    \"dungeon_compasses_mode\": %u,\n", s->settings->dungeon_compasses_mode);
  fprintf(f, "    \"prize_shuffle\": %s,\n", s->settings->prize_shuffle ? "true" : "false");
  fprintf(f, "    \"medallion_shuffle\": %s,\n", s->settings->medallion_shuffle ? "true" : "false");
  fprintf(f, "    \"mode_weapons\": %u,\n", s->settings->mode_weapons);
  fprintf(f, "    \"accessibility\": %u,\n", s->settings->accessibility);
  fprintf(f, "    \"pyramid_bow_upgrade\": %u,\n", s->settings->pyramid_bow_upgrade);
  fprintf(f, "    \"pieces_required\": %u,\n", s->settings->pieces_required);
  fprintf(f, "    \"pieces_placed\": %u\n", s->settings->pieces_placed);
  fprintf(f, "  },\n");

  // -----------------------------------------------------------------------
  // Placements — sorted by location_id (determinism).
  // -----------------------------------------------------------------------
  fprintf(f, "  \"placements\": [\n");
  if (s->placements != NULL && s->placements->count > 0) {
    // Sort a copy by location_id.
    uint16 n = s->placements->count;
    if (n > 512) n = 512;
    RandoPlacement local[512];
    memcpy(local, s->placements->entries, n * sizeof(RandoPlacement));
    qsort(local, n, sizeof(RandoPlacement), placement_cmp);
    for (uint16 i = 0; i < n; i++) {
      fprintf(f, "    {\"location\": %u, \"item\": %u}%s\n",
              local[i].location_id, local[i].item_id,
              (i + 1 < n) ? "," : "");
    }
  }
  fprintf(f, "  ],\n");

  // -----------------------------------------------------------------------
  // sphere_data — emitted per `randomizer-core / Sphere semantics` when a
  // sphere table is provided. Format: array of arrays; each inner array
  // holds {location, item} for that sphere.
  // -----------------------------------------------------------------------
  fprintf(f, "  \"sphere_data\": [");
  if (s->spheres != NULL && s->placements != NULL && s->placements->count > 0) {
    fprintf(f, "\n");
    uint8 max_sphere = s->spheres->max_sphere;
    for (uint8 sp = 0; sp <= max_sphere; sp++) {
      fprintf(f, "    [");
      bool first = true;
      for (uint16 i = 0; i < s->placements->count; i++) {
        if (s->spheres->sphere_index_by_placement[i] != sp) continue;
        if (!first) fprintf(f, ", ");
        fprintf(f, "{\"location\": %u, \"item\": %u}",
                s->placements->entries[i].location_id,
                s->placements->entries[i].item_id);
        first = false;
      }
      fprintf(f, "]%s\n", (sp < max_sphere) ? "," : "");
    }
    fprintf(f, "  ");
  }
  fprintf(f, "],\n");
  // unreachable_placements: placements whose location never became reachable
  // — emitted alongside sphere_data so tooling can detect broken seeds.
  fprintf(f, "  \"unreachable_placements\": [");
  if (s->spheres != NULL && s->placements != NULL) {
    bool first = true;
    for (uint16 i = 0; i < s->placements->count; i++) {
      if (s->spheres->sphere_index_by_placement[i] != 0xFF) continue;
      if (!first) fprintf(f, ", ");
      fprintf(f, "{\"location\": %u, \"item\": %u}",
              s->placements->entries[i].location_id,
              s->placements->entries[i].item_id);
      first = false;
    }
  }
  fprintf(f, "],\n");
  fprintf(f, "  \"playthrough\": [],\n");
  fprintf(f, "  \"regions\": []\n");

  fprintf(f, "}\n");

  fclose(f);
  return true;
}

// ---------------------------------------------------------------------------
// Text writer — grouped by region (per randomizer-core / "Spoiler-log emission"
// scenario: Text spoiler is grouped by region).
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// Spoiler_ResolvePath — derive `<spoiler_dir>/<share_string><ext>` at runtime.
//
// Source of truth: `[Randomizer] SpoilerDir` in zelda3.ini (parsed in
// config.c). When unset (NULL), defaults to "spoilers/" (relative to CWD).
// Per the spec: the path is NOT stored in the slot — it's recomputed on
// every write/read so config changes propagate cleanly.
// ---------------------------------------------------------------------------
int Spoiler_ResolvePath(const char *share_string,
                        const char *extension_with_dot,
                        char *out_path, int out_capacity) {
  if (share_string == NULL || extension_with_dot == NULL ||
      out_path == NULL || out_capacity <= 0) {
    return 0;
  }
  const char *dir = g_config.rando_spoiler_dir;
  if (dir == NULL || *dir == '\0') dir = "spoilers";

  // Build `<dir>/<share_string><ext>` — single-path-separator-aware. We use
  // '/' uniformly (Windows fopen accepts both).
  int written = snprintf(out_path, (size_t)out_capacity, "%s/%s%s",
                         dir, share_string, extension_with_dot);
  if (written < 0 || written >= out_capacity) return 0;
  return written;
}

bool Spoiler_WriteText(const RandoSpoiler *s, const char *out_path) {
  if (s == NULL || out_path == NULL) return false;

  FILE *f = fopen(out_path, "wb");
  if (f == NULL) return false;

  uint8 settings_hash[32];
  Settings_ComputeHash(s->settings, settings_hash);

  fprintf(f, "Zelda3 Randomizer Spoiler\n");
  fprintf(f, "=========================\n\n");
  fprintf(f, "Share string: %s\n", s->share_string != NULL ? s->share_string : "");
  fprintf(f, "Generator version: %u\n", (unsigned)s->generator_version);
  fprintf(f, "Settings hash: ");
  write_hex(f, settings_hash, 16);
  fprintf(f, "\n");
  fprintf(f, "World state: %u, Goal: %u\n",
          s->settings->world_state, s->settings->goal);
  fprintf(f, "Generation wall-clock: %u ms\n", (unsigned)s->generation_wall_clock_ms);
  fprintf(f, "Goal completable: %s\n\n", s->goal_completable ? "yes" : "no");

  fprintf(f, "Placements (by location_id):\n");
  fprintf(f, "----------------------------\n");
  if (s->placements != NULL && s->placements->count > 0) {
    uint16 n = s->placements->count;
    if (n > 512) n = 512;
    RandoPlacement local[512];
    memcpy(local, s->placements->entries, n * sizeof(RandoPlacement));
    qsort(local, n, sizeof(RandoPlacement), placement_cmp);
    for (uint16 i = 0; i < n; i++) {
      fprintf(f, "  LOC %3u -> ITEM %3u\n",
              local[i].location_id, local[i].item_id);
    }
  }
  fprintf(f, "\n");

  fclose(f);
  return true;
}
