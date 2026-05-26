// rando_settings.c — RandoSettings struct + canonical serialization
// (tasks.md §2.5).
//
// The canonical layout is pinned below. Reordering, widening, or renumbering
// any field is a kGeneratorVersion bump trigger (tasks.md §13.6).
//
// Layout (kSettingsCanonicalLen = 20 bytes):
//   offset 0  settings_version            (Phase A: 1)
//   offset 1  world_state                 WorldState enum
//   offset 2  goal                        Goal enum
//   offset 3  crystals_ganon              0..7
//   offset 4  crystals_tower              0..7
//   offset 5  item_pool_difficulty        ItemPoolDifficulty enum
//   offset 6  dungeon_small_keys_mode     DungeonItemMode enum
//   offset 7  dungeon_big_keys_mode       DungeonItemMode enum
//   offset 8  dungeon_maps_mode           DungeonItemMode enum
//   offset 9  dungeon_compasses_mode      DungeonItemMode enum
//   offset 10 prize_shuffle               bool (0/1)
//   offset 11 medallion_shuffle           bool (0/1)
//   offset 12 mode_weapons                ModeWeapons enum
//   offset 13 accessibility               Accessibility enum
//   offset 14 pyramid_bow_upgrade         PyramidBowUpgrade enum
//   offset 15 pieces_required             uint8
//   offset 16 pieces_placed               uint8
//   offset 17 reserved                    = 0 (forward-compat)
//   offset 18 reserved                    = 0
//   offset 19 reserved                    = 0

#include "rando_settings.h"
#include "../types.h"
#include "third_party/sha256/sha256.h"

void Settings_SetDefaults(RandoSettings *s) {
  s->settings_version = 1;
  s->world_state = kWorldState_Open;
  s->goal = kGoal_FastGanon;
  s->crystals_ganon = 7;
  s->crystals_tower = 7;
  s->tricks = 0;                            // Phase A pinned to none
  s->item_pool_difficulty = kItemPoolDifficulty_Normal;
  s->logic = 0;                             // Phase A pinned to NoGlitches
  s->mode_weapons = kModeWeapons_Randomized;
  s->accessibility = kAccessibility_Items;
  s->pyramid_bow_upgrade = kPyramidBowUpgrade_Silvers;
  s->region_boss_hearts_in_pool = 1;        // Phase A pinned to identity placement
  s->dungeon_small_keys_mode = kDungeonItemMode_Vanilla;
  s->dungeon_big_keys_mode = kDungeonItemMode_Vanilla;
  s->dungeon_maps_mode = kDungeonItemMode_Vanilla;
  s->dungeon_compasses_mode = kDungeonItemMode_Vanilla;
  s->prize_shuffle = 1;
  s->medallion_shuffle = 1;
  s->race_mode = 0;
  s->pieces_required = 20;
  s->pieces_placed = 30;
}

// Apply derived-from-other-fields normalization rules. Audit Bug #5:
// `goal=completionist` forces `accessibility=locations` per spec — otherwise
// the canonical hash differs between (SetDefaults; s->goal=Completionist)
// callers and (Settings_ParseCsv("goal=completionist")) callers.
//
// Applied on a private copy in Settings_CanonicalSerialize so any external
// caller's struct is left untouched and the hash is consistent regardless of
// how the struct was populated.
static void apply_derived_rules(RandoSettings *s) {
  if (s->goal == kGoal_Completionist) {
    s->accessibility = kAccessibility_Locations;
  }
}

int Settings_CanonicalSerialize(const RandoSettings *s_in,
                                uint8 out[kSettingsCanonicalLen]) {
  // Layout per `randomizer-core / Settings canonical serialization order`.
  // 18 single-byte fields + 2×u16 LE + zero padding to multiple of 4 = 24 bytes.
  RandoSettings sn = *s_in;
  apply_derived_rules(&sn);
  const RandoSettings *s = &sn;
  out[0]  = s->world_state;
  out[1]  = s->goal;
  out[2]  = s->crystals_ganon;
  out[3]  = s->crystals_tower;
  out[4]  = s->tricks;
  out[5]  = s->item_pool_difficulty;
  out[6]  = s->logic;
  out[7]  = s->mode_weapons;
  out[8]  = s->accessibility;
  out[9]  = s->pyramid_bow_upgrade;
  out[10] = s->region_boss_hearts_in_pool;
  out[11] = s->dungeon_small_keys_mode;
  out[12] = s->dungeon_big_keys_mode;
  out[13] = s->dungeon_maps_mode;
  out[14] = s->dungeon_compasses_mode;
  out[15] = s->prize_shuffle;
  out[16] = s->medallion_shuffle;
  out[17] = s->race_mode;
  out[18] = (uint8)(s->pieces_required & 0xff);
  out[19] = (uint8)((s->pieces_required >> 8) & 0xff);
  out[20] = (uint8)(s->pieces_placed & 0xff);
  out[21] = (uint8)((s->pieces_placed >> 8) & 0xff);
  out[22] = 0;  // pad to multiple of 4
  out[23] = 0;
  return kSettingsCanonicalLen;
}

void Settings_ComputeHash(const RandoSettings *s, uint8 out_hash[32]) {
  uint8 buf[kSettingsCanonicalLen];
  Settings_CanonicalSerialize(s, buf);
  sha256_buffer(buf, kSettingsCanonicalLen, out_hash);
}

void Settings_HashShort(const RandoSettings *s, uint8 out_hash[16]) {
  uint8 full[32];
  Settings_ComputeHash(s, full);
  for (int i = 0; i < 16; ++i) out_hash[i] = full[i];
}

// ===========================================================================
// Self-test. Defaults serialize to a known byte sequence; the SHA-256 of
// that sequence is the reference value below.
// ===========================================================================
#include <stdio.h>
#include <stdlib.h>

static int settings_byte_eq(const uint8 *a, const uint8 *b, int n) {
  for (int i = 0; i < n; ++i) if (a[i] != b[i]) return 0;
  return 1;
}

void Settings_SelfCheck(void) {
  RandoSettings s;
  Settings_SetDefaults(&s);

  uint8 canonical[kSettingsCanonicalLen];
  int n = Settings_CanonicalSerialize(&s, canonical);
  if (n != kSettingsCanonicalLen) {
    fprintf(stderr, "Settings_SelfCheck: serialize length mismatch\n");
    exit(2);
  }

  // Default-settings canonical bytes, layout per Settings_CanonicalSerialize:
  //   ws=0 goal=1 cg=7 ct=7 tricks=0 pool=1 logic=0 weapons=0 access=0
  //   bow=0 bossH=1 sk=0 bk=0 mp=0 cmp=0 prize=1 med=1 race=0
  //   pieces_req=20 (0x0014 LE) pieces_pl=30 (0x001e LE) pad pad
  static const uint8 kExpectedCanonical[kSettingsCanonicalLen] = {
    0x00, 0x01, 0x07, 0x07, 0x00, 0x01, 0x00, 0x00,
    0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01,
    0x01, 0x00, 0x14, 0x00, 0x1e, 0x00, 0x00, 0x00,
  };
  if (!settings_byte_eq(canonical, kExpectedCanonical, kSettingsCanonicalLen)) {
    fprintf(stderr,
            "Settings_SelfCheck: default canonical bytes mismatch.\n"
            "  Expected: ");
    for (int i = 0; i < kSettingsCanonicalLen; ++i)
      fprintf(stderr, "%02x", kExpectedCanonical[i]);
    fprintf(stderr, "\n  Got:      ");
    for (int i = 0; i < kSettingsCanonicalLen; ++i)
      fprintf(stderr, "%02x", canonical[i]);
    fprintf(stderr, "\n  Either the defaults changed (bump kGeneratorVersion!) or\n"
                    "  the serializer drifted from the pinned layout.\n");
    exit(2);
  }

  uint8 hash[32];
  Settings_ComputeHash(&s, hash);
  // SHA-256 of the kExpectedCanonical bytes.
  static const uint8 kExpectedHash[32] = {
    0xcb, 0x9b, 0xc5, 0xab, 0x8f, 0xe4, 0xbb, 0x34,
    0x29, 0x19, 0x01, 0x5b, 0x0f, 0xfa, 0x6f, 0xcc,
    0xdc, 0xd3, 0xd6, 0xaf, 0x8b, 0x4c, 0x5c, 0x2f,
    0x43, 0x57, 0x34, 0xfe, 0x0e, 0xac, 0xb9, 0x0f,
  };
  if (!settings_byte_eq(hash, kExpectedHash, 32)) {
    fprintf(stderr,
            "Settings_SelfCheck: default settings_hash mismatch (SHA-256 broken?).\n");
    exit(2);
  }

  // CSV parser round-trip: defaults → CSV → defaults.
  {
    RandoSettings s2;
    Settings_SetDefaults(&s2);
    int rc = Settings_ParseCsv("mode.state=open,goal=fast_ganon,crystals.ganon=7,"
                               "crystals.tower=7,prize_shuffle=true", &s2);
    if (rc != 0) {
      fprintf(stderr, "Settings_SelfCheck: CSV parser failed on valid input rc=%d\n", rc);
      exit(2);
    }
    uint8 c2[kSettingsCanonicalLen];
    Settings_CanonicalSerialize(&s2, c2);
    if (!settings_byte_eq(canonical, c2, kSettingsCanonicalLen)) {
      fprintf(stderr, "Settings_SelfCheck: CSV-parsed defaults serialize differently\n");
      exit(2);
    }
  }
  // CSV parser rejects unknown key.
  {
    RandoSettings s3;
    Settings_SetDefaults(&s3);
    int rc = Settings_ParseCsv("not_a_key=42", &s3);
    if (rc == 0) {
      fprintf(stderr, "Settings_SelfCheck: CSV parser should reject unknown key\n");
      exit(2);
    }
  }
  // CSV parser rejects bad enum value.
  {
    RandoSettings s4;
    Settings_SetDefaults(&s4);
    int rc = Settings_ParseCsv("goal=not_a_goal", &s4);
    if (rc == 0) {
      fprintf(stderr, "Settings_SelfCheck: CSV parser should reject bad enum\n");
      exit(2);
    }
  }
  // CSV parser rejects duplicate keys.
  {
    RandoSettings s4d;
    Settings_SetDefaults(&s4d);
    int rc = Settings_ParseCsv("goal=fast_ganon,goal=dungeons", &s4d);
    if (rc == 0) {
      fprintf(stderr, "Settings_SelfCheck: CSV parser should reject duplicate keys\n");
      exit(2);
    }
  }
  // CSV parser rejects out-of-range numeric values.
  {
    RandoSettings s4r;
    Settings_SetDefaults(&s4r);
    int rc = Settings_ParseCsv("crystals.ganon=8", &s4r);  // valid range is 0..7
    if (rc == 0) {
      fprintf(stderr, "Settings_SelfCheck: CSV parser should reject crystals.ganon=8\n");
      exit(2);
    }
  }
  // CSV parser rejects pieces_required > pieces_placed for Triforce-Hunt.
  {
    RandoSettings s4t;
    Settings_SetDefaults(&s4t);
    int rc = Settings_ParseCsv("goal=triforce-hunt,pieces_required=30,pieces_placed=20", &s4t);
    if (rc == 0) {
      fprintf(stderr, "Settings_SelfCheck: CSV parser should reject pieces_required > pieces_placed\n");
      exit(2);
    }
  }
  // Preset round-trip: each preset applies cleanly, and the Standard preset
  // changes world_state from the default.
  for (int p = 0; p < kPreset__Count; p++) {
    RandoSettings sp;
    int rc = Settings_ApplyPreset((SettingsPreset)p, &sp);
    if (rc != 0) {
      fprintf(stderr, "Settings_SelfCheck: preset %d failed\n", p);
      exit(2);
    }
  }
  {
    RandoSettings sp;
    Settings_ApplyPreset(kPreset_StandardGanon, &sp);
    if (sp.world_state != kWorldState_Standard) {
      fprintf(stderr, "Settings_SelfCheck: StandardGanon preset failed to set world_state\n");
      exit(2);
    }
  }
  {
    RandoSettings sp;
    Settings_ApplyPreset(kPreset_TriforceHuntDefault, &sp);
    if (sp.goal != kGoal_TriforceHunt || sp.pieces_required != 20 || sp.pieces_placed != 30) {
      fprintf(stderr, "Settings_SelfCheck: TriforceHuntDefault preset wrong\n");
      exit(2);
    }
  }
}

// ---------------------------------------------------------------------------
// CSV parser
// ---------------------------------------------------------------------------
#include <string.h>
#include <ctype.h>

static int csv_str_eq(const char *a, int alen, const char *b) {
  int blen = (int)strlen(b);
  if (alen != blen) return 0;
  for (int i = 0; i < alen; i++) if (a[i] != b[i]) return 0;
  return 1;
}

static int parse_bool(const char *v, int vlen, uint8 *out) {
  if (csv_str_eq(v, vlen, "true") || csv_str_eq(v, vlen, "1")) { *out = 1; return 0; }
  if (csv_str_eq(v, vlen, "false") || csv_str_eq(v, vlen, "0")) { *out = 0; return 0; }
  return -1;
}

static int parse_uint(const char *v, int vlen, uint32 *out) {
  if (vlen <= 0) return -1;
  uint32 r = 0;
  for (int i = 0; i < vlen; i++) {
    if (v[i] < '0' || v[i] > '9') return -1;
    r = r * 10 + (uint32)(v[i] - '0');
  }
  *out = r;
  return 0;
}

static int parse_world_state(const char *v, int vlen, uint8 *out) {
  if (csv_str_eq(v, vlen, "open"))      { *out = kWorldState_Open;     return 0; }
  if (csv_str_eq(v, vlen, "standard"))  { *out = kWorldState_Standard; return 0; }
  if (csv_str_eq(v, vlen, "inverted"))  { *out = kWorldState_Inverted; return 0; }
  if (csv_str_eq(v, vlen, "retro"))     { *out = kWorldState_Retro;    return 0; }
  return -1;
}

static int parse_goal(const char *v, int vlen, uint8 *out) {
  if (csv_str_eq(v, vlen, "ganon"))         { *out = kGoal_Ganon;        return 0; }
  if (csv_str_eq(v, vlen, "fast_ganon"))    { *out = kGoal_FastGanon;    return 0; }
  if (csv_str_eq(v, vlen, "dungeons"))      { *out = kGoal_Dungeons;     return 0; }
  if (csv_str_eq(v, vlen, "pedestal"))      { *out = kGoal_Pedestal;     return 0; }
  if (csv_str_eq(v, vlen, "triforce-hunt")) { *out = kGoal_TriforceHunt; return 0; }
  if (csv_str_eq(v, vlen, "ganonhunt"))     { *out = kGoal_GanonHunt;    return 0; }
  if (csv_str_eq(v, vlen, "completionist")) { *out = kGoal_Completionist; return 0; }
  return -1;
}

static int parse_pool_diff(const char *v, int vlen, uint8 *out) {
  if (csv_str_eq(v, vlen, "easy"))   { *out = kItemPoolDifficulty_Easy;   return 0; }
  if (csv_str_eq(v, vlen, "normal")) { *out = kItemPoolDifficulty_Normal; return 0; }
  if (csv_str_eq(v, vlen, "hard"))   { *out = kItemPoolDifficulty_Hard;   return 0; }
  if (csv_str_eq(v, vlen, "expert")) { *out = kItemPoolDifficulty_Expert; return 0; }
  return -1;
}

static int parse_dungeon_mode(const char *v, int vlen, uint8 *out) {
  if (csv_str_eq(v, vlen, "vanilla")) { *out = kDungeonItemMode_Vanilla; return 0; }
  if (csv_str_eq(v, vlen, "dungeon")) { *out = kDungeonItemMode_Dungeon; return 0; }
  if (csv_str_eq(v, vlen, "wild"))    { *out = kDungeonItemMode_Wild;    return 0; }
  return -1;
}

static int parse_weapons(const char *v, int vlen, uint8 *out) {
  if (csv_str_eq(v, vlen, "randomized")) { *out = kModeWeapons_Randomized; return 0; }
  if (csv_str_eq(v, vlen, "assured"))    { *out = kModeWeapons_Assured;    return 0; }
  return -1;
}

static int parse_accessibility(const char *v, int vlen, uint8 *out) {
  if (csv_str_eq(v, vlen, "items"))     { *out = kAccessibility_Items;     return 0; }
  if (csv_str_eq(v, vlen, "locations")) { *out = kAccessibility_Locations; return 0; }
  return -1;
}

// Bitmap of keys seen — used to reject duplicate keys per spec.
typedef struct {
  uint32 seen;
} SeenKeys;

#define KEY_BIT(name) (1u << (name))
enum {
  KEY_world_state = 0,
  KEY_goal,
  KEY_crystals_ganon,
  KEY_crystals_tower,
  KEY_item_pool,
  KEY_dungeon_small_keys,
  KEY_dungeon_big_keys,
  KEY_dungeon_maps,
  KEY_dungeon_compasses,
  KEY_prize_shuffle,
  KEY_medallion_shuffle,
  KEY_mode_weapons,
  KEY_accessibility,
  KEY_pieces_required,
  KEY_pieces_placed,
  // New keys per audit finding N7 — were in canonical hash but not parseable.
  KEY_tricks,
  KEY_logic,
  KEY_pyramid_bow_upgrade,
  KEY_region_boss_hearts_in_pool,
  KEY_race_mode,
};

static int handle_kv(const char *key, int klen, const char *val, int vlen,
                     RandoSettings *s, SeenKeys *seen) {
  #define MARK_SEEN(bit) do { \
    if (seen->seen & KEY_BIT(bit)) { \
      fprintf(stderr, "Settings_ParseCsv: duplicate key '%.*s'\n", klen, key); \
      return -1; \
    } \
    seen->seen |= KEY_BIT(bit); \
  } while (0)

  if (csv_str_eq(key, klen, "mode.state")) {
    MARK_SEEN(KEY_world_state);
    if (parse_world_state(val, vlen, &s->world_state) != 0) goto bad_value;
  } else if (csv_str_eq(key, klen, "goal")) {
    MARK_SEEN(KEY_goal);
    if (parse_goal(val, vlen, &s->goal) != 0) goto bad_value;
    // Per spec: completionist auto-sets accessibility=locations
    if (s->goal == kGoal_Completionist) s->accessibility = kAccessibility_Locations;
  } else if (csv_str_eq(key, klen, "crystals.ganon")) {
    MARK_SEEN(KEY_crystals_ganon);
    uint32 v;
    if (parse_uint(val, vlen, &v) != 0 || v > 7) goto bad_value;
    s->crystals_ganon = (uint8)v;
  } else if (csv_str_eq(key, klen, "crystals.tower")) {
    MARK_SEEN(KEY_crystals_tower);
    uint32 v;
    if (parse_uint(val, vlen, &v) != 0 || v > 7) goto bad_value;
    s->crystals_tower = (uint8)v;
  } else if (csv_str_eq(key, klen, "item_pool") || csv_str_eq(key, klen, "item.pool")) {
    MARK_SEEN(KEY_item_pool);
    if (parse_pool_diff(val, vlen, &s->item_pool_difficulty) != 0) goto bad_value;
  } else if (csv_str_eq(key, klen, "dungeon_items.small_keys")) {
    MARK_SEEN(KEY_dungeon_small_keys);
    if (parse_dungeon_mode(val, vlen, &s->dungeon_small_keys_mode) != 0) goto bad_value;
  } else if (csv_str_eq(key, klen, "dungeon_items.big_keys")) {
    MARK_SEEN(KEY_dungeon_big_keys);
    if (parse_dungeon_mode(val, vlen, &s->dungeon_big_keys_mode) != 0) goto bad_value;
  } else if (csv_str_eq(key, klen, "dungeon_items.maps")) {
    MARK_SEEN(KEY_dungeon_maps);
    if (parse_dungeon_mode(val, vlen, &s->dungeon_maps_mode) != 0) goto bad_value;
  } else if (csv_str_eq(key, klen, "dungeon_items.compasses")) {
    MARK_SEEN(KEY_dungeon_compasses);
    if (parse_dungeon_mode(val, vlen, &s->dungeon_compasses_mode) != 0) goto bad_value;
  } else if (csv_str_eq(key, klen, "prize_shuffle")) {
    MARK_SEEN(KEY_prize_shuffle);
    if (parse_bool(val, vlen, &s->prize_shuffle) != 0) goto bad_value;
  } else if (csv_str_eq(key, klen, "medallion_shuffle")) {
    MARK_SEEN(KEY_medallion_shuffle);
    if (parse_bool(val, vlen, &s->medallion_shuffle) != 0) goto bad_value;
  } else if (csv_str_eq(key, klen, "mode.weapons")) {
    MARK_SEEN(KEY_mode_weapons);
    if (parse_weapons(val, vlen, &s->mode_weapons) != 0) goto bad_value;
  } else if (csv_str_eq(key, klen, "accessibility")) {
    MARK_SEEN(KEY_accessibility);
    if (parse_accessibility(val, vlen, &s->accessibility) != 0) goto bad_value;
  } else if (csv_str_eq(key, klen, "pieces_required")) {
    MARK_SEEN(KEY_pieces_required);
    uint32 v;
    if (parse_uint(val, vlen, &v) != 0 || v > 65535) goto bad_value;
    s->pieces_required = (uint16)v;
  } else if (csv_str_eq(key, klen, "pieces_placed")) {
    MARK_SEEN(KEY_pieces_placed);
    uint32 v;
    if (parse_uint(val, vlen, &v) != 0 || v > 65535) goto bad_value;
    s->pieces_placed = (uint16)v;
  } else if (csv_str_eq(key, klen, "tricks")) {
    // Phase A: only `tricks=none` (the canonical zero bitmask) is accepted.
    // Phase B will extend this to parse comma-separated trick names; the
    // value column then becomes a quoted list per spec.
    MARK_SEEN(KEY_tricks);
    if (!csv_str_eq(val, vlen, "none") && !csv_str_eq(val, vlen, "0")) goto bad_value;
    s->tricks = 0;
  } else if (csv_str_eq(key, klen, "logic")) {
    // Phase A: `logic=NoGlitches` (level 0). Phase B+ adds OWG/MajorGlitches.
    MARK_SEEN(KEY_logic);
    if (csv_str_eq(val, vlen, "NoGlitches") || csv_str_eq(val, vlen, "none") || csv_str_eq(val, vlen, "0")) {
      s->logic = 0;
    } else {
      goto bad_value;
    }
  } else if (csv_str_eq(key, klen, "pyramid_bow_upgrade") ||
             csv_str_eq(key, klen, "region.pyramidBowUpgrade")) {
    MARK_SEEN(KEY_pyramid_bow_upgrade);
    if (csv_str_eq(val, vlen, "silvers")) {
      s->pyramid_bow_upgrade = kPyramidBowUpgrade_Silvers;
    } else {
      // Phase A pins to silvers; arrows is Phase B reserved.
      goto bad_value;
    }
  } else if (csv_str_eq(key, klen, "region.bossHeartsInPool") ||
             csv_str_eq(key, klen, "region_boss_hearts_in_pool")) {
    MARK_SEEN(KEY_region_boss_hearts_in_pool);
    if (parse_bool(val, vlen, &s->region_boss_hearts_in_pool) != 0) goto bad_value;
  } else if (csv_str_eq(key, klen, "race_mode") ||
             csv_str_eq(key, klen, "race")) {
    MARK_SEEN(KEY_race_mode);
    if (parse_bool(val, vlen, &s->race_mode) != 0) goto bad_value;
  } else {
    fprintf(stderr, "Settings_ParseCsv: unknown key '%.*s'\n", klen, key);
    return -1;
  }
  return 0;
bad_value:
  fprintf(stderr, "Settings_ParseCsv: bad value '%.*s' for key '%.*s'\n",
          vlen, val, klen, key);
  return -1;
  #undef MARK_SEEN
}

// ---------------------------------------------------------------------------
// Settings presets (tasks.md §9.5)
// ---------------------------------------------------------------------------
static const char *kPresetNames[kPreset__Count] = {
  "Open Ganon",
  "Standard Ganon",
  "Inverted Ganon",
  "Retro",
  "Triforce Hunt Default",
};

const char *Settings_PresetName(SettingsPreset preset) {
  if ((int)preset < 0 || (int)preset >= kPreset__Count) return "(unknown)";
  return kPresetNames[preset];
}

int Settings_ApplyPreset(SettingsPreset preset, RandoSettings *out) {
  if (out == NULL) return -1;
  Settings_SetDefaults(out);  // shared baseline; each preset only overrides specifics
  switch (preset) {
    case kPreset_OpenGanon:
      // Defaults are already Open / FastGanon / 7-7. No further override.
      return 0;
    case kPreset_StandardGanon:
      out->world_state = kWorldState_Standard;
      out->goal = kGoal_FastGanon;
      return 0;
    case kPreset_InvertedGanon:
      out->world_state = kWorldState_Inverted;
      out->goal = kGoal_FastGanon;
      return 0;
    case kPreset_Retro:
      out->world_state = kWorldState_Retro;
      out->goal = kGoal_Ganon;  // Retro convention: defeat Ganon, no fast-ganon shortcut
      return 0;
    case kPreset_TriforceHuntDefault:
      out->goal = kGoal_TriforceHunt;
      out->pieces_required = 20;
      out->pieces_placed = 30;
      return 0;
    default:
      return -1;
  }
}

int Settings_ParseCsv(const char *csv, RandoSettings *out) {
  if (csv == NULL || out == NULL) return -1;
  SeenKeys seen = { 0 };
  const char *p = csv;
  while (*p) {
    // Skip leading whitespace.
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '\0') break;
    // Parse key up to '='.
    const char *k_start = p;
    while (*p && *p != '=' && *p != ',') p++;
    if (*p != '=') {
      fprintf(stderr, "Settings_ParseCsv: expected '=' near '%.*s'\n",
              (int)(p - k_start), k_start);
      return -1;
    }
    int klen = (int)(p - k_start);
    p++;  // skip '='
    // Parse value up to ','.
    const char *v_start = p;
    while (*p && *p != ',') p++;
    int vlen = (int)(p - v_start);
    if (klen == 0 || vlen == 0) {
      fprintf(stderr, "Settings_ParseCsv: empty key or value\n");
      return -1;
    }
    if (handle_kv(k_start, klen, v_start, vlen, out, &seen) != 0) {
      return -1;
    }
    if (*p == ',') p++;
  }
  // Validate pieces_required <= pieces_placed for Triforce/Ganon-Hunt goals.
  if ((out->goal == kGoal_TriforceHunt || out->goal == kGoal_GanonHunt) &&
      out->pieces_required > out->pieces_placed) {
    fprintf(stderr, "Settings_ParseCsv: pieces_required (%u) > pieces_placed (%u)\n",
            out->pieces_required, out->pieces_placed);
    return -1;
  }
  return 0;
}
