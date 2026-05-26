// rando_settings.h — RandoSettings struct + canonical serialization
// (tasks.md §2.5).
//
// The serialization order pinned below is part of the determinism contract:
// changing it (or any field width / enum value) is a kGeneratorVersion bump
// trigger (tasks.md §13.6) and invalidates the regression corpus.
//
// The canonical-byte sequence feeds SHA-256 to produce settings_hash; the
// first 16 bytes of that hash go into the share-string payload
// (rando_share.h).

#ifndef ZELDA3_RANDO_SETTINGS_H_
#define ZELDA3_RANDO_SETTINGS_H_

#include "../types.h"

// ===========================================================================
// Pinned enums. Values are part of the determinism contract — additions
// MUST go at the end of each enum (so existing values stay stable).
// ===========================================================================

typedef enum {
  kWorldState_Open = 0,
  kWorldState_Standard = 1,
  kWorldState_Inverted = 2,
  kWorldState_Retro = 3,
} WorldState;

typedef enum {
  kGoal_Ganon = 0,
  kGoal_FastGanon = 1,
  kGoal_Dungeons = 2,
  kGoal_Pedestal = 3,
  kGoal_TriforceHunt = 4,
  kGoal_GanonHunt = 5,
  kGoal_Completionist = 6,
} Goal;

typedef enum {
  kItemPoolDifficulty_Easy = 0,
  kItemPoolDifficulty_Normal = 1,
  kItemPoolDifficulty_Hard = 2,
  kItemPoolDifficulty_Expert = 3,
} ItemPoolDifficulty;

typedef enum {
  kDungeonItemMode_Vanilla = 0,
  kDungeonItemMode_Dungeon = 1,
  kDungeonItemMode_Wild = 2,
} DungeonItemMode;

typedef enum {
  kModeWeapons_Randomized = 0,
  kModeWeapons_Assured = 1,
  // Phase B reservations:
  // kModeWeapons_Vanilla = 2,
  // kModeWeapons_Swordless = 3,
} ModeWeapons;

typedef enum {
  kAccessibility_Items = 0,
  kAccessibility_Locations = 1,
  // Phase B reservation:
  // kAccessibility_None = 2,
} Accessibility;

typedef enum {
  kPyramidBowUpgrade_Silvers = 0,
  // Phase B reservation:
  // kPyramidBowUpgrade_Arrows = 1,
} PyramidBowUpgrade;

// ===========================================================================
// RandoSettings struct. Holds the user's choice for every Phase A axis.
// Field order in C does NOT need to match canonical serialization order —
// the serializer reads each field by name (see Settings_CanonicalSerialize
// in rando_settings.c).
// ===========================================================================

typedef struct RandoSettings {
  uint8 settings_version;           // = 1 for Phase A
  uint8 world_state;                // WorldState
  uint8 goal;                       // Goal
  uint8 crystals_ganon;             // 0..7 — required to make Ganon vulnerable
  uint8 crystals_tower;             // 0..7 — required to enter Ganon's Tower
  uint8 item_pool_difficulty;       // ItemPoolDifficulty
  uint8 dungeon_small_keys_mode;    // DungeonItemMode
  uint8 dungeon_big_keys_mode;      // DungeonItemMode
  uint8 dungeon_maps_mode;          // DungeonItemMode
  uint8 dungeon_compasses_mode;     // DungeonItemMode
  uint8 prize_shuffle;              // bool: shuffle crystal/pendant→dungeon assignments
  uint8 medallion_shuffle;          // bool: shuffle Bombos/Ether/Quake → MM/TR entrance
  uint8 mode_weapons;               // ModeWeapons
  uint8 accessibility;              // Accessibility
  uint8 pyramid_bow_upgrade;        // PyramidBowUpgrade
  uint8 pieces_required;            // for triforce-hunt / ganonhunt
  uint8 pieces_placed;              // for triforce-hunt / ganonhunt
} RandoSettings;

// ===========================================================================
// Canonical byte length — every Phase A settings struct serializes to
// exactly this many bytes. Adding a field requires bumping this constant
// AND kGeneratorVersion (tasks.md §13.6).
// ===========================================================================
#define kSettingsCanonicalLen 20

// Populate the struct with Phase A defaults (Open / Fast Ganon / Normal
// pool / 7 crystals each / dungeon items Vanilla / prize+medallion shuffle
// on / Randomized weapons / Items-accessibility / Silvers / 20-of-30 pieces).
void Settings_SetDefaults(RandoSettings *s);

// Serialize to a fixed-layout little-endian byte sequence. Always writes
// kSettingsCanonicalLen bytes. Returns kSettingsCanonicalLen.
int Settings_CanonicalSerialize(const RandoSettings *s,
                                uint8 out[kSettingsCanonicalLen]);

// Compute SHA-256 of the canonical-serialized bytes. Writes 32 bytes.
void Settings_ComputeHash(const RandoSettings *s, uint8 out_hash[32]);

// Convenience: compute the first 16 bytes of the full hash (the truncation
// the share-string payload uses).
void Settings_HashShort(const RandoSettings *s, uint8 out_hash[16]);

// Self-test (tasks.md §2.5): round-trip the default settings, validate the
// reference SHA-256 of the canonical bytes for known input. Exits with
// code 2 on failure.
void Settings_SelfCheck(void);

#endif  // ZELDA3_RANDO_SETTINGS_H_
