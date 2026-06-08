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
#include "rando_hints.h"
#include "shuffle_entrance.h"  // Entrance_WriteSpoilerJson (Phase C entrance_mapping)
#include "shuffle_boss.h"      // BossShuffle_BossName / _DungeonName (Slice 7 spoiler)
#include "../config.h"
#include "../types.h"

// Single-source accessor over the vanilla prize-drop table (kPrizeItems[56]) —
// defined in src/sprite.c (declared in sprite.h). Forward-declared here (rather
// than including sprite.h, which would drag in variables.h) so the drop_tables
// section can print the resolved drop item for each shuffled slot without
// embedding a copy of the ROM-derived table. Keep in sync with sprite.h.
extern uint8 Sprite_VanillaPrizeItem(uint8 source_index);

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define RANDO_MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define RANDO_MKDIR(p) mkdir((p), 0755)
#endif

// Generated tables — typedefs + extern declarations live in rando_logic.h.
#include "rando_logic.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
// Ensure `dir_path` exists on disk. Best-effort: on success or if the dir
// already exists, returns 0; otherwise returns nonzero. Spoiler writes
// guard against missing dirs by calling this before fopen.
static int ensure_directory(const char *dir_path) {
  if (dir_path == NULL || *dir_path == '\0') return 0;
  if (RANDO_MKDIR(dir_path) == 0) return 0;
  // errno == EEXIST means it already exists — treat as success.
  // We don't have <errno.h> included; rely on a probe: try fopen test isn't
  // robust either. Just always return 0 — if the dir truly can't be created,
  // the subsequent fopen will fail and the caller's error path will fire.
  return 0;
}

// Extract the directory portion of `path` (everything before the last '/'
// or '\\') into `out`. Returns true if there was a directory portion.
static bool extract_dir(const char *path, char *out, size_t out_cap) {
  if (path == NULL || out == NULL || out_cap == 0) return false;
  const char *last_sep = NULL;
  for (const char *p = path; *p; ++p) {
    if (*p == '/' || *p == '\\') last_sep = p;
  }
  if (last_sep == NULL) return false;
  size_t len = (size_t)(last_sep - path);
  if (len >= out_cap) return false;
  memcpy(out, path, len);
  out[len] = '\0';
  return true;
}

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

// Look up a location def by id (replaces hand-inlined kRandoLocations scans).
// Returns NULL if the id is not in the registry.
static const RandoLocationDef *find_location(uint16 id) {
  for (uint32 j = 0; j < kRandoLocationsCount; j++)
    if (kRandoLocations[j].id == id) return &kRandoLocations[j];
  return NULL;
}

// One shop-class placement row, gathered once for both spoiler emitters (the
// JSON `shops[]` array and the text `Shops:` section).
typedef struct { uint16 loc; uint16 item; uint8 type; uint16 region_id; } SpoilerShopRow;

static int spoiler_shoprow_cmp(const void *a, const void *b) {
  uint16 la = ((const SpoilerShopRow *)a)->loc;
  uint16 lb = ((const SpoilerShopRow *)b)->loc;
  return (la > lb) - (la < lb);
}

// Collect the shop-class placements (LOCTYPE_Shop / ShopUpgrade / TakeAny — the
// Retro shop / capacity-upgrade / take-any slots) into `out`, sorted by location
// id so each shop's contiguous slots stay grouped. Returns the count (0 ⇒ no
// shop-class locations placed, i.e. a non-Retro seed). `cap` bounds `out`; 160
// exceeds the max possible shop-class count (27 Shop + 2 ShopUpgrade + 62 TakeAny
// = 91), so it never truncates — it is a hard backstop, not a real cap.
static uint16 collect_shop_rows(const RandoSpoiler *s, SpoilerShopRow *out, uint16 cap) {
  uint16 n = 0;
  if (s == NULL || s->placements == NULL) return 0;
  for (uint16 i = 0; i < s->placements->count && n < cap; i++) {
    uint16 loc = s->placements->entries[i].location_id;
    const RandoLocationDef *d = find_location(loc);
    if (d == NULL) continue;
    if (d->type != LOCTYPE_Shop && d->type != LOCTYPE_ShopUpgrade &&
        d->type != LOCTYPE_TakeAny) continue;
    out[n].loc = loc;
    out[n].item = s->placements->entries[i].item_id;
    out[n].type = d->type;
    out[n].region_id = d->region_id;
    n++;
  }
  qsort(out, n, sizeof(out[0]), spoiler_shoprow_cmp);
  return n;
}

// ---------------------------------------------------------------------------
// JSON writer
// ---------------------------------------------------------------------------
static bool write_spoiler_json_stream(const RandoSpoiler *s, FILE *f);

bool Spoiler_WriteJson(const RandoSpoiler *s, const char *out_path) {
  if (s == NULL || out_path == NULL) return false;

  // Auto-create the spoiler directory if it doesn't exist. Avoids forcing
  // users to mkdir spoilers/ before the first run.
  char dir_buf[512];
  if (extract_dir(out_path, dir_buf, sizeof(dir_buf))) {
    ensure_directory(dir_buf);
  }

  FILE *f = fopen(out_path, "wb");
  if (f == NULL) return false;
  bool ok = write_spoiler_json_stream(s, f);
  fclose(f);
  return ok;
}

static bool write_spoiler_json_stream(const RandoSpoiler *s, FILE *f) {
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
  // hints_count: number of populated hint NPCs (0 when hints==Off or no
  // hints were generated). Mirrors the length of the hints[] array below;
  // a tooling convenience so consumers can branch without parsing hints[].
  {
    uint16 hints_count = 0;
    for (uint16 npc = 1; npc < (uint16)kRandoHintNpc__Count; npc++) {
      if (Rando_GetHintString((RandoHintNpc)npc) != NULL) hints_count++;
    }
    fprintf(f, "    \"hints_count\": %u,\n", (unsigned)hints_count);
  }
  // fallback_warnings: each non-zero counter from the placer surfaces
  // here. Plus an "unreachable_placements" rollup when the placer could
  // not produce a fully-reachable seed.
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
    // Slice 8 §3.5 — drop heart-floor exhausted its retry budget and fell back
    // to the vanilla identity drop table (essentially never; surfaced so the
    // reader knows the seed's drop shuffle is a no-op).
    if (s->drop_used_fallback) {
      fprintf(f, "%s\n      {\"kind\": \"drop_heart_floor_fallback\"}",
              first ? "" : ",");
      first = false;
    }
    if (s->spheres != NULL && s->spheres->unreachable_count > 0) {
      fprintf(f, "%s\n      {\"kind\": \"unreachable_placements\", \"count\": %u}",
              first ? "" : ",",
              (unsigned)s->spheres->unreachable_count);
      first = false;
    }
    // Phase D (add-rando-major-glitch) — NoLogic seeds enforce NO reachability:
    // the predicate VM short-circuits the reachability eval (rando_logic.c), so
    // placement ignores progression gating and the seed may be un-completable.
    // Emitted by reading settings directly (the unverified_tricks_enabled
    // pattern below). The spec pins this entry's shape to {"code","detail"},
    // distinct from the {"kind"} shape of the placer-counter warnings above.
    if (s->settings->logic >= 4 /* NoLogic */) {
      fprintf(f, "%s\n      {\"code\": \"no_logic_seed\", \"detail\": \"Seed generated with logic=NoLogic; reachability is not enforced. The seed may be un-completable.\"}",
              first ? "" : ",");
      first = false;
    }
    // NOTE: there is no longer an `accessibility_none_seed` warning. Under the
    // ALTTPR three-way accessibility axis, `none` means "beatable only" — the
    // seed is still guaranteed completable, so the old "may be unwinnable"
    // warning no longer applies. When `items`/`beatable` intentionally leave
    // some locations unreachable, the `unreachable_placements` warning above
    // already surfaces that to the reader for every tier.
    //
    // §12.6.4 — surface tricks / glitch levels the user enabled that are NOT
    // yet confirmed to behave on the US 1.0 ROM (ALTTPR targets JP 1.0). This is
    // informational (the seed still generates); race admins / seed validators
    // decide whether to accept. cross-version + verified-us10 do NOT warn.
    {
      bool any_unverified = false;
      for (uint32 i = 0; i < kRandoTrickStatusCount; i++) {
        const RandoTrickStatus *ts = &kRandoTrickStatus[i];
        if ((s->settings->tricks & (uint8)(1u << ts->bit)) == 0) continue;
        if (ts->rom_version_status == kRomVerStatus_CrossVersion ||
            ts->rom_version_status == kRomVerStatus_VerifiedUs10) continue;
        if (!any_unverified) {
          fprintf(f, "%s\n      {\"kind\": \"unverified_tricks_enabled\", \"names\": [",
                  first ? "" : ",");
          any_unverified = true;
        } else {
          fprintf(f, ", ");
        }
        fprintf(f, "\"%s\"", ts->id);
      }
      // Glitch levels: logic >= level enables that tier's gates, so warn for
      // every non-zero level the seed reaches whose status is unverified.
      // Suppressed entirely under NoLogic (logic>=4): the reachability eval is
      // short-circuited (rando_logic.c), so NO glitch-tier gate actually
      // evaluates, and the dedicated no_logic_seed warning above already covers
      // the seed — listing every tier (including no_logic itself) here would be
      // misleading "in-use" noise. Phase D (add-rando-major-glitch) audit fix.
      for (uint32 i = 0; i < kRandoGlitchLevelStatusCount && s->settings->logic < 4; i++) {
        const RandoGlitchLevelStatus *gs = &kRandoGlitchLevelStatus[i];
        if (gs->level == 0) continue;                 // no_glitches never warns
        if (s->settings->logic < gs->level) continue; // tier not reached
        if (gs->rom_version_status == kRomVerStatus_CrossVersion ||
            gs->rom_version_status == kRomVerStatus_VerifiedUs10) continue;
        if (!any_unverified) {
          fprintf(f, "%s\n      {\"kind\": \"unverified_tricks_enabled\", \"names\": [",
                  first ? "" : ",");
          any_unverified = true;
        } else {
          fprintf(f, ", ");
        }
        fprintf(f, "\"%s\"", gs->id);
      }
      if (any_unverified) {
        fprintf(f, "]}");
        first = false;
      }
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
  // Emit the EFFECTIVE small-keys mode (Retro pins Wild) so the spoiler matches
  // the canonical hash + actual placement, not the user's raw (overridden) field.
  fprintf(f, "    \"dungeon_small_keys_mode\": %u,\n", Settings_EffectiveSmallKeysMode(s->settings));
  fprintf(f, "    \"dungeon_big_keys_mode\": %u,\n", s->settings->dungeon_big_keys_mode);
  fprintf(f, "    \"dungeon_maps_mode\": %u,\n", s->settings->dungeon_maps_mode);
  fprintf(f, "    \"dungeon_compasses_mode\": %u,\n", s->settings->dungeon_compasses_mode);
  fprintf(f, "    \"prize_shuffle\": %s,\n", s->settings->prize_shuffle ? "true" : "false");
  fprintf(f, "    \"medallion_shuffle\": %s,\n", s->settings->medallion_shuffle ? "true" : "false");
  fprintf(f, "    \"mode_weapons\": %u,\n", s->settings->mode_weapons);
  fprintf(f, "    \"accessibility\": %u,\n", s->settings->accessibility);
  fprintf(f, "    \"pyramid_bow_upgrade\": %u,\n", s->settings->pyramid_bow_upgrade);
  fprintf(f, "    \"pieces_required\": %u,\n", s->settings->pieces_required);
  fprintf(f, "    \"pieces_placed\": %u,\n", s->settings->pieces_placed);
  fprintf(f, "    \"hints\": %u,\n", s->settings->hints);
  fprintf(f, "    \"boss_shuffle\": %u,\n", s->settings->boss_shuffle);
  fprintf(f, "    \"drop_shuffle\": %u\n", s->settings->drop_shuffle);
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
  // entrance_mapping (Phase C) — door interior → loaded interior, when an
  // entrance shuffle was applied. Omitted otherwise.
  // -----------------------------------------------------------------------
  Entrance_WriteSpoilerJson(f, s->entrance_assign, s->entrance_count);
  Entrance_WriteDungeonSpoilerJson(f, s->dungeon_assign, s->dungeon_count);
  Entrance_WriteCrossSpoilerJson(f, s->cross_assign, s->cross_count);
  Entrance_WriteDecoupledSpoilerJson(f, s->decoupled_assign, s->decoupled_count);
  Entrance_WriteDungeonDecoupledSpoilerJson(f, s->dun_decoupled_assign, s->dun_decoupled_count);

  // -----------------------------------------------------------------------
  // boss_assignments (Slice 7 §6.1) — dungeon → boss now in its boss room.
  // Emitted only when boss_shuffle is on (boss_assignment != NULL — §6.4); the
  // dungeon→prize binding is unchanged (EP's prize stays at EP regardless).
  // -----------------------------------------------------------------------
  if (s->boss_assignment != NULL) {
    fprintf(f, "  \"boss_assignments\": [\n");
    bool first = true;
    for (uint8 d = 1; d <= 12; d++) {  // dungeons EP..GT; HCE (0) has no boss
      uint8 idx = s->boss_assignment[d];
      if (idx == 0xFF) continue;
      fprintf(f, "%s    {\"dungeon\": %u, \"dungeon_name\": \"%s\", "
                 "\"boss\": %u, \"boss_name\": \"%s\"}",
              first ? "" : ",\n", (unsigned)d, BossShuffle_DungeonName(d),
              (unsigned)idx, BossShuffle_BossName(idx));
      first = false;
    }
    fprintf(f, "%s  ],\n", first ? "" : "\n");
  }

  // -----------------------------------------------------------------------
  // drop_tables (Slice 8 §6.2) — the 7 enemy-drop packs after the shuffle,
  // each as 8 resolved drop item ids (kPrizeItems[drop_map[flat]]). Pack 0 is
  // the heart-floor pack (guaranteed >=1 heart, id 216 = 0xD8). Emitted only
  // when drop_shuffle is on (drop_map != NULL — §6.4).
  // -----------------------------------------------------------------------
  if (s->drop_map != NULL) {
    fprintf(f, "  \"drop_tables\": [\n");
    for (uint8 pack = 0; pack < 7; pack++) {
      fprintf(f, "    [");
      for (uint8 slot = 0; slot < 8; slot++) {
        uint8 flat = (uint8)(pack * 8 + slot);
        uint8 item = Sprite_VanillaPrizeItem(s->drop_map[flat]);
        fprintf(f, "%s%u", slot ? ", " : "", (unsigned)item);
      }
      fprintf(f, "]%s\n", (pack < 6) ? "," : "");
    }
    fprintf(f, "  ],\n");
  }

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
  // -----------------------------------------------------------------------
  // hints[] — Phase B Slice 5 §3. Emitted by `Rando_GenerateHints`,
  // populated only when `settings.hints == kHintsMode_On`. Each entry
  // mirrors ALTTPR's `(npc_string_id, text)` shape from HintService
  // output; the `dialogue_id` is the runtime carve from `kRandoHint*`.
  // When the runtime intercept (#85) lands these texts will surface
  // in-game; today they're spoiler-only.
  // -----------------------------------------------------------------------
  fprintf(f, "  \"hints\": [");
  {
    bool first = true;
    for (uint16 npc = 1; npc < (uint16)kRandoHintNpc__Count; npc++) {
      const char *text = Rando_GetHintString((RandoHintNpc)npc);
      if (text == NULL) continue;
      const char *npc_str = Rando_GetHintNpcStringId((RandoHintNpc)npc);
      uint16 dlg = Rando_GetHintDialogueId((RandoHintNpc)npc);
      if (first) fprintf(f, "\n");
      fprintf(f, "    %s{\"npc\": \"%s\", \"dialogue_id\": %u, \"text\": \"",
              first ? "" : ",\n    ", npc_str ? npc_str : "?", (unsigned)dlg);
      // Escape minimal JSON chars in the text (quotes + backslash).
      for (const char *p = text; *p; p++) {
        char c = *p;
        if (c == '"' || c == '\\') fputc('\\', f);
        fputc(c, f);
      }
      fprintf(f, "\"}");
      first = false;
    }
    if (!first) fprintf(f, "\n  ");
  }
  fprintf(f, "],\n");

  // shops[] (tasks §8.2-8.3) — the Retro shop / capacity-upgrade / take-any
  // placements, carrying their region (so consumers can group by region) and an
  // identity_placed flag for the Capacity Upgrade slots. Emitted ONLY when
  // shop-class locations are placed (Retro), so non-Retro JSON is byte-identical
  // (the key is omitted entirely, not emitted empty). This changes the JSON
  // bytes for Retro seeds, which feed the race-mode stamp — version-locked by
  // the kGeneratorVersion bump that ships with Retro wild-keys.
  {
    SpoilerShopRow shops[160];
    uint16 sn = collect_shop_rows(s, shops, 160);
    if (sn > 0) {
      fprintf(f, "  \"shops\": [\n");
      for (uint16 i = 0; i < sn; i++) {
        const char *type_str = (shops[i].type == LOCTYPE_ShopUpgrade) ? "ShopUpgrade"
                             : (shops[i].type == LOCTYPE_TakeAny)      ? "TakeAny"
                                                                      : "Shop";
        // Shop-class locations are identity-pinned and carry no logic-region
        // binding (region_id == 0xFFFF), so the location NAME is the grouping key
        // (e.g. "Light World Kakariko Shop - 1"); a bound region is emitted only
        // when one actually exists.
        const char *lname = Rando_GetLocationName(shops[i].loc);
        fprintf(f, "%s    {\"location\": %u, \"name\": \"%s\", \"item\": %u, "
                   "\"type\": \"%s\"",
                i ? ",\n" : "", shops[i].loc, lname, shops[i].item, type_str);
        if (shops[i].region_id != 0xFFFF)
          fprintf(f, ", \"region\": \"%s\"", Rando_GetRegionName(shops[i].region_id));
        if (shops[i].type == LOCTYPE_ShopUpgrade)
          fprintf(f, ", \"identity_placed\": true");
        fprintf(f, "}");
      }
      fprintf(f, "\n  ],\n");
    }
  }

  fprintf(f, "  \"playthrough\": [],\n");
  fprintf(f, "  \"regions\": []\n");

  fprintf(f, "}\n");
  return true;
}

// ---------------------------------------------------------------------------
// Phase B Slice 6 — race-mode suppressed-spoiler writer + reader.
// ---------------------------------------------------------------------------

// CRC-32 IEEE 802.3, reflected, polynomial 0xEDB88320. Same algorithm as
// zlib's crc32. Small inline implementation — no LUT to keep .data
// footprint minimal; the suppressed spoiler is < 200 bytes so perf doesn't
// matter.
static uint32 crc32_ieee(const uint8 *data, size_t len) {
  uint32 c = 0xFFFFFFFFu;
  for (size_t i = 0; i < len; i++) {
    c ^= (uint32)data[i];
    for (int b = 0; b < 8; b++) {
      c = (c >> 1) ^ (0xEDB88320u & (uint32)(-(int32)(c & 1u)));
    }
  }
  return c ^ 0xFFFFFFFFu;
}

static void put_u16_le(uint8 *p, uint16 v) {
  p[0] = (uint8)(v & 0xff);
  p[1] = (uint8)((v >> 8) & 0xff);
}

static void put_u32_le(uint8 *p, uint32 v) {
  p[0] = (uint8)(v & 0xff);
  p[1] = (uint8)((v >> 8) & 0xff);
  p[2] = (uint8)((v >> 16) & 0xff);
  p[3] = (uint8)((v >> 24) & 0xff);
}

static uint16 read_u16_le(const uint8 *p) {
  return (uint16)((uint16)p[0] | ((uint16)p[1] << 8));
}

static uint32 read_u32_le(const uint8 *p) {
  return (uint32)p[0] | ((uint32)p[1] << 8) | ((uint32)p[2] << 16) | ((uint32)p[3] << 24);
}

// Serialize the suppressed-spoiler header into the on-disk layout.
// Returns the number of bytes written (always kRandoSuppressedSpoilerSize).
static size_t serialize_suppressed(const RandoSuppressedSpoiler *h,
                                   uint8 out[kRandoSuppressedSpoilerSize]) {
  memcpy(out + 0, h->magic, 4);
  put_u16_le(out + 4, h->generator_version);
  memcpy(out + 6, h->spoiler_stamp, 32);
  put_u32_le(out + 38, h->share_string_len);
  memcpy(out + 42, h->share_string, kRandoSuppressedSpoilerShareStringMax);
  memcpy(out + 106, h->settings_canonical, kRandoSuppressedSpoilerSettingsLen);
  put_u32_le(out + 134, h->crc32);
  return kRandoSuppressedSpoilerSize;
}

// Build + write the suppressed-spoiler file at `out_path`. Caller supplies
// the SHA-256 stamp of the full canonical JSON (computed with race_mode
// cleared to 0 and generation_wall_clock_ms cleared to 0) AND the canonical
// settings bytes (also with race_mode cleared to 0).
static bool write_suppressed_file(const char *share_string,
                                  uint16 generator_version,
                                  const uint8 stamp[32],
                                  const uint8 settings_canonical[kRandoSuppressedSpoilerSettingsLen],
                                  const char *out_path) {
  RandoSuppressedSpoiler h;
  memset(&h, 0, sizeof(h));
  memcpy(h.magic, kRandoSuppressedSpoilerMagic, 4);
  h.generator_version = generator_version;
  memcpy(h.spoiler_stamp, stamp, 32);
  size_t len = share_string ? strlen(share_string) : 0;
  if (len > kRandoSuppressedSpoilerShareStringMax) len = kRandoSuppressedSpoilerShareStringMax;
  h.share_string_len = (uint32)len;
  if (len > 0) memcpy(h.share_string, share_string, len);
  memcpy(h.settings_canonical, settings_canonical, kRandoSuppressedSpoilerSettingsLen);

  uint8 buf[kRandoSuppressedSpoilerSize];
  serialize_suppressed(&h, buf);
  // CRC over everything except the crc32 trailer itself.
  h.crc32 = crc32_ieee(buf, kRandoSuppressedSpoilerSize - 4);
  put_u32_le(buf + 134, h.crc32);

  // Auto-create the spoiler directory.
  char dir_buf[512];
  if (extract_dir(out_path, dir_buf, sizeof(dir_buf))) {
    ensure_directory(dir_buf);
  }

  FILE *f = fopen(out_path, "wb");
  if (f == NULL) return false;
  size_t wrote = fwrite(buf, 1, sizeof(buf), f);
  fclose(f);
  return wrote == sizeof(buf);
}

// Stamp computation: write the spoiler JSON to a temporary file with
// non-deterministic fields cleared, read it back into memory, and SHA-256
// the bytes. Returns true and fills `out_stamp` on success.
//
// **Stamp normalization** — fields normalized to stable values so the
// stamp is reproducible across runs and machines:
//   - settings.race_mode → 0       (race_mode is recorded in file existence,
//                                    not in the stamped settings)
//   - generation_wall_clock_ms → 0 (varies with machine speed)
//   - forward_fill_fallback_count → 0  (varies with placer's wall-clock
//   - retry_attempts → 1               budget cutoff; reveal at a different
//                                       budget would otherwise mismatch)
// The `Place_AssumedFill` retry-attempts count is also a function of the
// per-call `budget_seconds`. Reveal MUST pass `budget_seconds = 0` (no
// wall-clock cutoff) so the placer runs to its hard 8-attempt cap; that
// makes attempts_used deterministic and lets us normalize to 1 in the
// stamp without lying about what happened on a particular machine.
static bool compute_stamp(const RandoSpoiler *s, uint8 out_stamp[32]) {
  // Build the normalized spoiler view for stamping.
  RandoSettings norm_settings = *s->settings;
  norm_settings.race_mode = 0;
  RandoSpoiler norm = *s;
  norm.settings = &norm_settings;
  norm.generation_wall_clock_ms = 0;
  // Slice 6 audit H1 — these fields depend on placer wall-clock; clear so
  // the stamp is reproducible regardless of budget_seconds or machine speed.
  norm.forward_fill_fallback_count = 0;
  norm.retry_attempts = 1;

  FILE *tmp = tmpfile();
  if (tmp == NULL) return false;
  if (!write_spoiler_json_stream(&norm, tmp)) {
    fclose(tmp);
    return false;
  }
  fflush(tmp);
  if (fseek(tmp, 0, SEEK_END) != 0) { fclose(tmp); return false; }
  long len = ftell(tmp);
  if (len <= 0) { fclose(tmp); return false; }
  rewind(tmp);
  uint8 *bytes = (uint8 *)malloc((size_t)len);
  if (bytes == NULL) { fclose(tmp); return false; }
  size_t got = fread(bytes, 1, (size_t)len, tmp);
  fclose(tmp);
  if (got != (size_t)len) { free(bytes); return false; }

  extern void sha256_buffer(const uint8 *data, size_t len, uint8 out[32]);
  sha256_buffer(bytes, (size_t)len, out_stamp);
  free(bytes);
  return true;
}

bool Spoiler_Write(const RandoSpoiler *s,
                   const char *json_path,
                   const char *txt_path) {
  if (s == NULL || s->settings == NULL || json_path == NULL) return false;

  bool race_mode = s->settings->race_mode != 0;
  if (!race_mode) {
    if (!Spoiler_WriteJson(s, json_path)) return false;
    if (txt_path != NULL) (void)Spoiler_WriteText(s, txt_path);
    return true;
  }

  // Race mode: write the suppressed binary in lieu of the full JSON.
  uint8 stamp[32];
  if (!compute_stamp(s, stamp)) return false;

  // Canonicalize settings with race_mode = 0 (per spec — stamp is over
  // the placement, not the race-mode flag).
  RandoSettings norm_settings = *s->settings;
  norm_settings.race_mode = 0;
  uint8 settings_canon[kRandoSuppressedSpoilerSettingsLen];
  Settings_CanonicalSerialize(&norm_settings, settings_canon);

  return write_suppressed_file(s->share_string,
                               (uint16)s->generator_version,
                               stamp,
                               settings_canon,
                               json_path);
  // No .txt sibling in race mode.
}

int Spoiler_ReadSuppressed(const char *path, RandoSuppressedSpoiler *out) {
  if (path == NULL || out == NULL) return -2;
  FILE *f = fopen(path, "rb");
  if (f == NULL) return -1;
  uint8 buf[kRandoSuppressedSpoilerSize];
  size_t got = fread(buf, 1, sizeof(buf), f);
  fclose(f);
  if (got != sizeof(buf)) return -2;
  if (memcmp(buf, kRandoSuppressedSpoilerMagic, 4) != 0) return -2;

  uint32 disk_crc = read_u32_le(buf + 134);
  uint32 calc_crc = crc32_ieee(buf, kRandoSuppressedSpoilerSize - 4);
  if (disk_crc != calc_crc) return -3;

  memcpy(out->magic, buf + 0, 4);
  out->generator_version = read_u16_le(buf + 4);
  memcpy(out->spoiler_stamp, buf + 6, 32);
  out->share_string_len = read_u32_le(buf + 38);
  if (out->share_string_len > kRandoSuppressedSpoilerShareStringMax) return -2;
  memcpy(out->share_string, buf + 42, kRandoSuppressedSpoilerShareStringMax);
  memcpy(out->settings_canonical, buf + 106, kRandoSuppressedSpoilerSettingsLen);
  out->crc32 = disk_crc;
  return 0;
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

  // Auto-create the spoiler directory if it doesn't exist. Avoids forcing
  // users to mkdir spoilers/ before the first run.
  char dir_buf[512];
  if (extract_dir(out_path, dir_buf, sizeof(dir_buf))) {
    ensure_directory(dir_buf);
  }

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

  // Fallback warnings — surface forward-fill / retry / unreachable
  // counts prominently in the text spoiler.
  if (s->forward_fill_fallback_count > 0 || s->retry_attempts > 1 ||
      s->drop_used_fallback ||
      (s->spheres != NULL && s->spheres->unreachable_count > 0)) {
    fprintf(f, "WARNINGS\n--------\n");
    if (s->forward_fill_fallback_count > 0)
      fprintf(f, "  ! Forward-fill fallback hit %u time(s) during placement.\n",
              (unsigned)s->forward_fill_fallback_count);
    if (s->retry_attempts > 1)
      fprintf(f, "  ! Placer needed %u attempts before producing a seed.\n",
              (unsigned)s->retry_attempts);
    if (s->drop_used_fallback)
      fprintf(f, "  ! Drop-pool heart floor exhausted its retries; drop shuffle "
                 "fell back to the vanilla drop table.\n");
    if (s->spheres != NULL && s->spheres->unreachable_count > 0)
      fprintf(f, "  ! %u placement(s) are unreachable in this seed.\n",
              (unsigned)s->spheres->unreachable_count);
    fprintf(f, "\n");
  }

  // Phase C — entrance shuffle mappings (omitted when not shuffled). Mirrors the
  // JSON entrance_mapping / dungeon_entrance_mapping sections.
  if ((s->entrance_assign != NULL && s->entrance_count > 0) ||
      (s->dungeon_assign != NULL && s->dungeon_count > 0) ||
      (s->cross_assign != NULL && s->cross_count > 0)) {
    fprintf(f, "ENTRANCE SHUFFLE\n----------------\n");
    Entrance_WriteSpoilerText(f, s->entrance_assign, s->entrance_count);
    Entrance_WriteDungeonSpoilerText(f, s->dungeon_assign, s->dungeon_count);
    Entrance_WriteCrossSpoilerText(f, s->cross_assign, s->cross_count);
    Entrance_WriteDecoupledSpoilerText(f, s->decoupled_assign, s->decoupled_count);
    Entrance_WriteDungeonDecoupledSpoilerText(f, s->dun_decoupled_assign, s->dun_decoupled_count);
    Entrance_WriteCrossDecoupledSpoilerText(f, s->cross_decoupled_assign, s->cross_decoupled_count);
    fprintf(f, "\n");
  }

  // Slice 7 — boss assignments (omitted unless boss_shuffle is on).
  if (s->boss_assignment != NULL) {
    fprintf(f, "BOSS ASSIGNMENTS\n----------------\n");
    for (uint8 d = 1; d <= 12; d++) {
      uint8 idx = s->boss_assignment[d];
      if (idx == 0xFF) continue;
      fprintf(f, "  %-20s : %s\n", BossShuffle_DungeonName(d),
              BossShuffle_BossName(idx));
    }
    fprintf(f, "\n");
  }

  // Slice 8 — drop tables (omitted unless drop_shuffle is on). Each pack is the
  // 8 resolved drop item ids; pack 0 is the heart-floor pack.
  if (s->drop_map != NULL) {
    fprintf(f, "DROP TABLES (item ids, pack 0 = heart floor)\n");
    fprintf(f, "--------------------------------------------\n");
    for (uint8 pack = 0; pack < 7; pack++) {
      fprintf(f, "  pack %u:", (unsigned)pack);
      for (uint8 slot = 0; slot < 8; slot++) {
        uint8 flat = (uint8)(pack * 8 + slot);
        fprintf(f, " %u", (unsigned)Sprite_VanillaPrizeItem(s->drop_map[flat]));
      }
      fprintf(f, "\n");
    }
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
      const RandoLocationDef *d = find_location(rows[i].location_id);
      if (d != NULL) {
        rows[i].region_id = d->region_id;
        // Audit H1 — under Inverted, a location may be assigned to a different
        // region via Rando_FindPredicateOverride. Honor that override here so the
        // grouped spoiler text matches the runtime's reachability view (e.g.,
        // Ether Tablet shows under LightWorld_DeathMountain_East, not the base
        // West region).
        if (s->settings != NULL) {
          const RandoLocationPredOverride *ov =
              Rando_FindPredicateOverride(rows[i].location_id,
                                          s->settings->world_state);
          if (ov != NULL && ov->region_override != 0xFFFF) {
            rows[i].region_id = ov->region_override;
          }
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
      const char *iname = Rando_GetItemName(rows[i].item_id);
      fprintf(f, "  %-44s -> %s\n", lname, iname);
    }
  }
  fprintf(f, "\n");

  // Shops (tasks §8.1) — a dedicated grouping of the Retro shop / capacity-
  // upgrade / take-any slots. These also appear under their region headings
  // above; this section re-lists them grouped by shop for at-a-glance reading
  // and flags the identity-placed Capacity Upgrade slots. Emitted only when
  // shop-class locations are actually placed (i.e. Retro seeds), so non-Retro
  // spoilers are byte-unchanged. This is TEXT-spoiler only: the .txt is never
  // written in race mode and is not part of the race-mode stamp (computed over
  // the JSON), so this section cannot move any placement / sphere digest or stamp.
  {
    SpoilerShopRow shops[160];
    uint16 sn = collect_shop_rows(s, shops, 160);
    if (sn > 0) {
      fprintf(f, "Shops:\n");
      fprintf(f, "------\n");
      char cur_shop[64];
      cur_shop[0] = '\0';
      for (uint16 i = 0; i < sn; i++) {
        const char *lname = Rando_GetLocationName(shops[i].loc);
        // Group heading = the location name with the trailing " - <slot>" suffix
        // stripped (e.g. "Light World Kakariko Shop - 1" -> "Light World Kakariko
        // Shop"). Capacity-upgrade / take-any names follow the same shape.
        char shop_name[64];
        size_t k = 0;
        for (const char *p = lname; *p && k < sizeof(shop_name) - 1; p++) {
          if (p[0] == ' ' && p[1] == '-' && p[2] == ' ') break;  // slot separator
          shop_name[k++] = *p;
        }
        shop_name[k] = '\0';
        if (strcmp(shop_name, cur_shop) != 0) {
          if (cur_shop[0] != '\0') fprintf(f, "\n");
          fprintf(f, "[%s]\n", shop_name);
          strncpy(cur_shop, shop_name, sizeof(cur_shop) - 1);
          cur_shop[sizeof(cur_shop) - 1] = '\0';
        }
        const char *iname = Rando_GetItemName(shops[i].item);
        const char *tag =
            (shops[i].type == LOCTYPE_ShopUpgrade) ? "  [identity-placed]" : "";
        fprintf(f, "  %-44s -> %s%s\n", lname, iname, tag);
      }
      fprintf(f, "\n");
    }
  }

  // Hints — Phase B Slice 5 §3. Mirrors the JSON `hints[]` array. The
  // section is omitted entirely when no hints are populated (settings.hints
  // == kHintsMode_Off, or non-rando spoiler context). Runtime telepathic-
  // tile dispatch (#85) is deferred — these hints are spoiler-only today.
  {
    bool any_hint = false;
    for (uint16 npc = 1; npc < (uint16)kRandoHintNpc__Count; npc++) {
      if (Rando_GetHintString((RandoHintNpc)npc) != NULL) { any_hint = true; break; }
    }
    if (any_hint) {
      fprintf(f, "Hints:\n");
      fprintf(f, "------\n");
      for (uint16 npc = 1; npc < (uint16)kRandoHintNpc__Count; npc++) {
        const char *text = Rando_GetHintString((RandoHintNpc)npc);
        if (text == NULL) continue;
        const char *npc_str = Rando_GetHintNpcStringId((RandoHintNpc)npc);
        fprintf(f, "  %-42s : %s\n", npc_str ? npc_str : "?", text);
      }
      fprintf(f, "\n");
    }
  }

  fclose(f);
  return true;
}
