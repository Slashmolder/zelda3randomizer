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
  s->item_pool_difficulty = kItemPoolDifficulty_Normal;
  s->dungeon_small_keys_mode = kDungeonItemMode_Vanilla;
  s->dungeon_big_keys_mode = kDungeonItemMode_Vanilla;
  s->dungeon_maps_mode = kDungeonItemMode_Vanilla;
  s->dungeon_compasses_mode = kDungeonItemMode_Vanilla;
  s->prize_shuffle = 1;
  s->medallion_shuffle = 1;
  s->mode_weapons = kModeWeapons_Randomized;
  s->accessibility = kAccessibility_Items;
  s->pyramid_bow_upgrade = kPyramidBowUpgrade_Silvers;
  s->pieces_required = 20;
  s->pieces_placed = 30;
}

int Settings_CanonicalSerialize(const RandoSettings *s,
                                uint8 out[kSettingsCanonicalLen]) {
  out[0]  = s->settings_version;
  out[1]  = s->world_state;
  out[2]  = s->goal;
  out[3]  = s->crystals_ganon;
  out[4]  = s->crystals_tower;
  out[5]  = s->item_pool_difficulty;
  out[6]  = s->dungeon_small_keys_mode;
  out[7]  = s->dungeon_big_keys_mode;
  out[8]  = s->dungeon_maps_mode;
  out[9]  = s->dungeon_compasses_mode;
  out[10] = s->prize_shuffle;
  out[11] = s->medallion_shuffle;
  out[12] = s->mode_weapons;
  out[13] = s->accessibility;
  out[14] = s->pyramid_bow_upgrade;
  out[15] = s->pieces_required;
  out[16] = s->pieces_placed;
  out[17] = 0;
  out[18] = 0;
  out[19] = 0;
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

  // Default-settings canonical bytes (computed in Python — see comment in
  // rando_rng.c for the discipline. Bake-in matches the layout above.)
  static const uint8 kExpectedCanonical[kSettingsCanonicalLen] = {
    0x01, 0x00, 0x01, 0x07, 0x07, 0x01, 0x00, 0x00,
    0x00, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x14,
    0x1e, 0x00, 0x00, 0x00,
  };
  if (!settings_byte_eq(canonical, kExpectedCanonical, kSettingsCanonicalLen)) {
    fprintf(stderr,
            "Settings_SelfCheck: default canonical bytes mismatch.\n"
            "  Expected: 010001070701000000000101000000141e000000\n"
            "  Got:      ");
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
    0xb0, 0x1d, 0x22, 0xb2, 0xb5, 0x03, 0xa1, 0xc8,
    0x32, 0xfe, 0x7d, 0xaf, 0xc0, 0x92, 0x06, 0xb0,
    0x1f, 0xfd, 0x5c, 0xf0, 0x6c, 0x27, 0xaa, 0x37,
    0xed, 0x4d, 0x11, 0x01, 0x4d, 0x5c, 0x00, 0x09,
  };
  if (!settings_byte_eq(hash, kExpectedHash, 32)) {
    fprintf(stderr,
            "Settings_SelfCheck: default settings_hash mismatch (SHA-256 broken?).\n");
    exit(2);
  }
}
