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
  kAccessibility_None = 2,  // Phase B Slice 4 — un-pinned per add-rando-trick-logic-and-axes §5.
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
  uint8 tricks;                     // Phase A: pinned to 0 (none); Phase B bitmask reserved
  uint8 item_pool_difficulty;       // ItemPoolDifficulty
  uint8 logic;                      // Phase A: 0 (NoGlitches); Phase B+ reserved
  uint8 mode_weapons;               // ModeWeapons
  uint8 accessibility;              // Accessibility
  uint8 pyramid_bow_upgrade;        // PyramidBowUpgrade
  uint8 region_boss_hearts_in_pool; // bool; Phase A pinned 1 (identity-placed)
  uint8 dungeon_small_keys_mode;    // DungeonItemMode
  uint8 dungeon_big_keys_mode;      // DungeonItemMode
  uint8 dungeon_maps_mode;          // DungeonItemMode
  uint8 dungeon_compasses_mode;     // DungeonItemMode
  uint8 prize_shuffle;              // bool: shuffle crystal/pendant→dungeon assignments
  uint8 medallion_shuffle;          // bool: shuffle Bombos/Ether/Quake → MM/TR entrance
  uint8 race_mode;                  // bool; Phase B feature, bit reserved in hash from Phase A
  uint16 pieces_required;           // for triforce-hunt / ganonhunt (was uint8; spec is uint16)
  uint16 pieces_placed;             // for triforce-hunt / ganonhunt
  // Phase B Slice 5 §61 — hints axis. Binary on/off matching ALTTPR
  // `spoil.Hints` (HintService.php:54). Default 0 (off). Canonical
  // serialization at offset [22] — landed with §66 in the kGenVer 13→14
  // bump.
  uint8 hints;
  // Phase B Slice 7 §63 / Slice 8 §64 — shuffle axes. Binary on/off.
  // Default 0 (off). Canonical serialization at offsets [23] and [24]
  // — landed with §66 in the kGenVer 13→14 bump.
  uint8 boss_shuffle;
  uint8 drop_shuffle;
  // Phase C — entrance shuffle composable axes (binary on/off). All default
  // off EXCEPT `coupled` (defaults on, the ALTTPR baseline: enter A ⇒ exit A).
  // Canonical serialization bit-PACKS all five into the previously-zero pad
  // byte [25] (see kEntranceAxis_* below), so the default settings still
  // serialize to a zero byte and kSettingsCanonicalLen stays 28 — no
  // canonical-size-coupling cascade. `coupled`/`cross_category`/`decoupled`
  // are normalized to 0 when no shuffle axis is active (apply_derived_rules),
  // so the default packs to 0x00. The four famous ALTTPR modes (Simple /
  // Restricted / Crossed / Insanity) are UI presets over these axes, not a
  // stored enum. See add-rando-entrance-shuffle/design.md §5.
  uint8 shuffle_cave_entrances;     // bool
  uint8 shuffle_dungeon_entrances;  // bool (Stage 2)
  uint8 coupled;                    // bool, default ON
  uint8 cross_category;             // bool (Stage 3) — caves↔dungeons may mix
  uint8 decoupled;                  // bool (Stage 4) — per-endpoint; implies !coupled
  // Advanced opt-in: also shuffle Ganon's Tower's entrance. Off by default
  // because GT's crystal-tower gate travels with its door, which can be circular
  // at high crystals.tower (the door needs N crystals but leads to a crystal-
  // bearing dungeon). The full-reachability gate rejects circular permutations,
  // so a seed that can't be made reachable simply fails to generate — lower
  // crystals.tower (0 always works) or reroll. Requires shuffle_dungeon_entrances.
  uint8 shuffle_ganons_tower_entrance;  // bool (advanced)
} RandoSettings;

// Phase C — bit positions for the packed entrance-axis byte (canonical [25]).
// Used by Settings_CanonicalSerialize/Deserialize. A zero byte == no entrance
// shuffle (the default), preserving the byte-identical corpus invariant.
enum {
  kEntranceAxis_ShuffleCaves    = 1u << 0,
  kEntranceAxis_ShuffleDungeons = 1u << 1,
  kEntranceAxis_Coupled         = 1u << 2,
  kEntranceAxis_CrossCategory   = 1u << 3,
  kEntranceAxis_Decoupled       = 1u << 4,
  kEntranceAxis_ShuffleGanonsTower = 1u << 5,
};

// ===========================================================================
// Canonical byte length — every Phase A settings struct serializes to
// exactly this many bytes. Adding a field requires bumping this constant
// AND kGeneratorVersion (tasks.md §13.6).
// ===========================================================================
// Canonical serialization is 25 bytes of content (21 single-byte fields +
// 2×u16 LE = 25) padded to 28 (the next multiple of 4 = 3 pad bytes).
// Layout per spec — see Settings_CanonicalSerialize.
// Phase B Slice 7+8 §66: bumped from 24→28 to absorb `hints`, `boss_shuffle`,
// `drop_shuffle` at offsets [22..24]. kGeneratorVersion bumped 13→14 in lockstep.
#define kSettingsCanonicalLen 28

// Populate the struct with Phase A defaults (Open / Fast Ganon / Normal
// pool / 7 crystals each / dungeon items Vanilla / prize+medallion shuffle
// on / Randomized weapons / Items-accessibility / Silvers / 20-of-30 pieces).
void Settings_SetDefaults(RandoSettings *s);

// Serialize to a fixed-layout little-endian byte sequence. Always writes
// kSettingsCanonicalLen bytes. Returns kSettingsCanonicalLen.
int Settings_CanonicalSerialize(const RandoSettings *s,
                                uint8 out[kSettingsCanonicalLen]);

// Inverse of Settings_CanonicalSerialize. Reads `kSettingsCanonicalLen`
// bytes and populates `out`. Returns 0 on success, non-zero on input error
// (NULL pointer or non-zero pad bytes). Phase B Slice 6 — needed by the
// race-mode reveal pipeline to reconstruct settings from the suppressed
// spoiler file.
int Settings_CanonicalDeserialize(const uint8 in[kSettingsCanonicalLen],
                                  RandoSettings *out);

// Compute SHA-256 of the canonical-serialized bytes. Writes 32 bytes.
void Settings_ComputeHash(const RandoSettings *s, uint8 out_hash[32]);

// Convenience: compute the first 16 bytes of the full hash (the truncation
// the share-string payload uses).
void Settings_HashShort(const RandoSettings *s, uint8 out_hash[16]);

// Self-test (tasks.md §2.5): round-trip the default settings, validate the
// reference SHA-256 of the canonical bytes for known input. Exits with
// code 2 on failure.
void Settings_SelfCheck(void);

// ---------------------------------------------------------------------------
// CLI `--settings=k=v,...` parser (tasks.md §1.6a, randomizer-core spec).
//
// `csv` is a comma-separated list of `<key>=<value>` pairs using the
// canonical CLI key grammar (dot-separated keys: `mode.state`, `crystals.ganon`,
// `dungeon_items.small_keys`, etc.). Enum values are case-sensitive and match
// the spec exactly (e.g., `fast_ganon`, `triforce-hunt`, `NoGlitches`).
//
// On success: writes parsed values into `*out` over the defaults and returns
// 0. On any error (unknown key, bad value, duplicate key): writes a one-line
// error to stderr and returns non-zero. Callers SHOULD have populated `out`
// with defaults before calling.
//
// Examples accepted:
//   "mode.state=open,goal=fast_ganon,crystals.ganon=7"
//   "goal=triforce-hunt,pieces_required=25,pieces_placed=30"
//   "dungeon_items.small_keys=dungeon,prize_shuffle=true"
int Settings_ParseCsv(const char *csv, RandoSettings *out);

// ---------------------------------------------------------------------------
// Settings presets (tasks.md §9.5). Named bundles of (key, value) pairs that
// populate RandoSettings. The settings screen offers these as one-tap
// defaults; the user can fine-tune afterward.
//
// Phase A presets: Open Ganon, Standard Ganon, Inverted Ganon, Retro,
// Triforce Hunt Default. Each preset starts from Settings_SetDefaults and
// overrides specific fields.
// ---------------------------------------------------------------------------
typedef enum {
  kPreset_OpenGanon = 0,        // Open / Fast Ganon (the defaults)
  kPreset_StandardGanon = 1,    // Standard world-state, Fast Ganon
  kPreset_InvertedGanon = 2,    // Inverted world-state, Fast Ganon
  kPreset_Retro = 3,            // Retro world-state, Defeat Ganon
  kPreset_TriforceHuntDefault = 4,  // Open / Triforce Hunt 20-of-30
  kPreset__Count = 5,
} SettingsPreset;

// Apply a preset to `out`. Calls Settings_SetDefaults first, then overrides
// the preset-specific fields. Returns 0 on success, non-zero on unknown preset.
int Settings_ApplyPreset(SettingsPreset preset, RandoSettings *out);

// Human-readable preset name for UI display.
const char *Settings_PresetName(SettingsPreset preset);

#endif  // ZELDA3_RANDO_SETTINGS_H_
