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

  // sphere_digest: SHA-256 over the canonical sphere assignment (per spec
  // "sphere_digest in meta block"). Lets corpus tooling detect
  // sphere-computation regressions independently of the placement table.
  // Canonical bytes: for each placement in location_id-sorted order,
  // emit (location_id u16 LE | sphere_index u8). Unreachable placements
  // contribute sphere_index 0xFF.
  uint8 sphere_digest[32];
  {
    extern void sha256_buffer(const uint8 *data, size_t len, uint8 out[32]);
    if (s->spheres != NULL && s->placements != NULL && s->placements->count > 0) {
      static struct { uint16 loc; uint8 sph; } rows[512];
      uint16 n = s->placements->count;
      if (n > 512) n = 512;
      for (uint16 i = 0; i < n; i++) {
        rows[i].loc = s->placements->entries[i].location_id;
        rows[i].sph = s->spheres->sphere_index_by_placement[i];
      }
      // Insertion sort by location_id (canonical).
      for (uint16 i = 1; i < n; i++) {
        uint16 j = i;
        while (j > 0 && rows[j - 1].loc > rows[j].loc) {
          uint16 tl = rows[j - 1].loc; uint8 ts = rows[j - 1].sph;
          rows[j - 1].loc = rows[j].loc; rows[j - 1].sph = rows[j].sph;
          rows[j].loc = tl; rows[j].sph = ts;
          j--;
        }
      }
      static uint8 buf[512 * 3];
      for (uint16 i = 0; i < n; i++) {
        buf[i * 3 + 0] = (uint8)(rows[i].loc & 0xff);
        buf[i * 3 + 1] = (uint8)(rows[i].loc >> 8);
        buf[i * 3 + 2] = rows[i].sph;
      }
      sha256_buffer(buf, (size_t)n * 3, sphere_digest);
    } else {
      sha256_buffer((const uint8 *)"", 0, sphere_digest);
    }
  }

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
  fprintf(f, "    \"sphere_digest\": \"");
  write_hex(f, sphere_digest, 32);
  fprintf(f, "\",\n");
  fprintf(f, "    \"share_string\": \"%s\",\n",
          s->share_string != NULL ? s->share_string : "");
  fprintf(f, "    \"seed_u64\": \"0x%016llx\",\n", (unsigned long long)s->seed_u64);
  fprintf(f, "    \"world_state\": %u,\n", (unsigned)s->settings->world_state);
  fprintf(f, "    \"goal\": %u,\n", (unsigned)s->settings->goal);
  fprintf(f, "    \"generation_wall_clock_ms\": %u,\n",
          (unsigned)s->generation_wall_clock_ms);
  fprintf(f, "    \"goal_completable\": %s,\n",
          s->goal_completable ? "true" : "false");
  // fallback_warnings: each non-zero counter from the placer surfaces here
  // (audit Bug #8). Plus an "unreachable_placements" rollup when the placer
  // could not produce a fully-reachable seed.
  fprintf(f, "    \"fallback_warnings\": [");
  {
    bool first = true;
    if (s->forward_fill_fallback_count > 0) {
      fprintf(f, "%s\n      {\"kind\": \"forward_fill_fallback\", \"count\": %u}",
              first ? "" : ",",
              (unsigned)s->forward_fill_fallback_count);
      first = false;
    }
    if (s->retry_attempts > 1) {
      fprintf(f, "%s\n      {\"kind\": \"retry_attempts\", \"count\": %u}",
              first ? "" : ",",
              (unsigned)s->retry_attempts);
      first = false;
    }
    if (s->spheres != NULL && s->spheres->unreachable_count > 0) {
      fprintf(f, "%s\n      {\"kind\": \"unreachable_placements\", \"count\": %u}",
              first ? "" : ",",
              (unsigned)s->spheres->unreachable_count);
      first = false;
    }
    if (!first) fprintf(f, "\n    ");
  }
  fprintf(f, "]\n");
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

  // Fallback warnings — surface forward-fill / retry / unreachable counts
  // prominently per audit Bug #8.
  if (s->forward_fill_fallback_count > 0 || s->retry_attempts > 1 ||
      (s->spheres != NULL && s->spheres->unreachable_count > 0)) {
    fprintf(f, "WARNINGS\n--------\n");
    if (s->forward_fill_fallback_count > 0)
      fprintf(f, "  ! Forward-fill fallback hit %u time(s) during placement.\n",
              (unsigned)s->forward_fill_fallback_count);
    if (s->retry_attempts > 1)
      fprintf(f, "  ! Placer needed %u attempts before producing a seed.\n",
              (unsigned)s->retry_attempts);
    if (s->spheres != NULL && s->spheres->unreachable_count > 0)
      fprintf(f, "  ! %u placement(s) are unreachable in this seed.\n",
              (unsigned)s->spheres->unreachable_count);
    fprintf(f, "\n");
  }

  fprintf(f, "Placements (grouped by region):\n");
  fprintf(f, "-------------------------------\n");
  if (s->placements != NULL && s->placements->count > 0) {
    uint16 n = s->placements->count;
    if (n > 512) n = 512;
    // Look up region_id for each placement via kRandoLocations.
    // Then sort by (region_id, location_id) and emit grouped sections.
    static struct {
      uint16 region_id;
      uint16 location_id;
      uint16 item_id;
    } rows[512];
    for (uint16 i = 0; i < n; i++) {
      rows[i].location_id = s->placements->entries[i].location_id;
      rows[i].item_id = s->placements->entries[i].item_id;
      rows[i].region_id = 0xFFFF;
      for (uint32 j = 0; j < kRandoLocationsCount; j++) {
        if (kRandoLocations[j].id == rows[i].location_id) {
          rows[i].region_id = kRandoLocations[j].region_id;
          break;
        }
      }
    }
    // Insertion sort by (region_id, location_id) — n is small (~237).
    for (uint16 i = 1; i < n; i++) {
      uint16 j = i;
      while (j > 0 &&
             (rows[j - 1].region_id > rows[j].region_id ||
              (rows[j - 1].region_id == rows[j].region_id &&
               rows[j - 1].location_id > rows[j].location_id))) {
        // swap
        uint16 tr = rows[j - 1].region_id, tl = rows[j - 1].location_id, ti = rows[j - 1].item_id;
        rows[j - 1].region_id = rows[j].region_id;
        rows[j - 1].location_id = rows[j].location_id;
        rows[j - 1].item_id = rows[j].item_id;
        rows[j].region_id = tr; rows[j].location_id = tl; rows[j].item_id = ti;
        j--;
      }
    }
    uint16 cur_region = 0;
    bool first_section = true;
    for (uint16 i = 0; i < n; i++) {
      if (first_section || rows[i].region_id != cur_region) {
        if (!first_section) fprintf(f, "\n");
        first_section = false;
        cur_region = rows[i].region_id;
        const char *rname = Rando_GetRegionName(cur_region);
        fprintf(f, "[%s]\n", rname);
      }
      const char *lname = Rando_GetLocationName(rows[i].location_id);
      fprintf(f, "  %-44s -> ITEM %3u\n", lname, rows[i].item_id);
    }
  }
  fprintf(f, "\n");

  fclose(f);
  return true;
}
