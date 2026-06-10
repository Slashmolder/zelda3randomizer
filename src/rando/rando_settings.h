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
  // kModeWeapons_Vanilla = 2,  // still reserved (out of Phase B scope)
  kModeWeapons_Swordless = 3,   // no swords in the pool; runtime swordless patches active
} ModeWeapons;

typedef enum {
  kAccessibility_Items = 0,      // "100% Inventory": every progression item reachable
  kAccessibility_Locations = 1,  // "100% Locations": every location reachable (strictest)
  // ALTTPR "Not Guaranteed" — exposed as "beatable only" in the UI. The seed is
  // still guaranteed beatable (goal completable); only full item/location
  // accessibility is relaxed. (The serialized value name stays "None" because
  // enum values are part of the determinism contract.)
  kAccessibility_None = 2,
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
  uint8 region_boss_hearts_in_pool; // bool, INVERTED vs name: 1 (default) = boss
                                    // hearts PINNED/identity-placed (NOT in pool);
                                    // 0 = shuffled into the general pool
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
  // add-rando-enemy-shuffle — enemy (sprite-type) substitution axis. Binary
  // on/off, default off. Like boss/drop shuffle this is ORTHOGONAL to item
  // placement (draws no fill RNG, adds no logic predicate), so it does NOT grow
  // the canonical layout: it bit-PACKS into the reserved pad bit [26] bit 0 (the
  // deserializer's permissive trailing pad — the intended extension surface), so
  // kSettingsCanonicalLen stays 28 and default-settings settings_hash stays
  // byte-identical. See kEnemyShuffleAxis_* below + Settings_CanonicalSerialize.
  uint8 enemy_shuffle;  // bool
  // add-rando-door-shuffle — intra-dungeon door-connection shuffle.
  // 0 = vanilla, 1 = basic (per-dungeon pool, intensity 1: Normal + spiral
  // doors, original key-door counts relocated + prover-validated). Packs into
  // canonical pad byte [27] bits 0-1; default 0 keeps the default
  // settings_hash byte-identical and kSettingsCanonicalLen at 28.
  uint8 door_shuffle;
} RandoSettings;

// add-rando-enemy-shuffle — bit positions for the packed pad byte (canonical
// [26]). A zero byte == no enemy shuffle (the default), preserving the
// byte-identical corpus invariant. [26] was previously always-zero reserved pad.
enum {
  kEnemyShuffleAxis_Enabled = 1u << 0,
};

// add-rando-door-shuffle — door_shuffle axis values (canonical [27] bits 0-1).
enum {
  kDoorShuffle_Vanilla = 0,
  kDoorShuffle_Basic = 1,
  kDoorShuffleAxis_Mask = 3,
};

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
// Phase C bit-packs the entrance axes into pad byte [25]; add-rando-enemy-shuffle
// bit-packs `enemy_shuffle` into pad byte [26] (bit0). LENGTH STAYS 28 — both
// reused previously-zero pad bytes, so no size-coupling cascade. [27] is the last
// remaining pad byte.
#define kSettingsCanonicalLen 28

// Populate the struct with Phase A defaults (Open / Fast Ganon / Normal
// pool / 7 crystals each / dungeon items Vanilla / prize+medallion shuffle
// on / Randomized weapons / Items-accessibility / Silvers / 20-of-30 pieces).
void Settings_SetDefaults(RandoSettings *s);

// Effective small-keys mode after Retro pinning. ALTTPR's Retro world-state
// forces `region.wildKeys` (small keys enter the general/wild pool, no longer
// restricted to their own dungeon — app/World/Retro.php). The fork realizes
// that by treating `dungeon_small_keys_mode` as Wild whenever
// `world_state == Retro`, reusing the existing (corpus-tested) Wild placement +
// cross-dungeon key-credit runtime (rando.c key grant). This is the SINGLE
// source of truth for the override: it is applied identically in
// apply_derived_rules (so the canonical settings hash reflects Wild) and at
// every placer read site (so placement matches the hash) — both key off
// world_state, so the hash and placement can never desync. Returns the user's
// raw mode for non-Retro seeds.
//
// NOTE: this is `wildKeys` (the placement-side override). ALTTPR Retro ALSO sets
// `rom.genericKeys` (one shared key pool, any key opens any door) — that is
// `Settings_GenericKeysActive` below, which collapses the per-dungeon key-door
// LOGIC predicates onto the shared GenericKey count. The two are complementary:
// wildKeys lets keys spawn anywhere; genericKeys makes them fungible.
uint8 Settings_EffectiveSmallKeysMode(const RandoSettings *s);

// add-rando-door-shuffle — the normalized (post-derived-rules) door_shuffle
// value; definitionally the canonical byte [27], so generation, runtime
// install, and settings_hash always agree (vanilla under Inverted/Retro,
// glitched logic, or entrance shuffle — the MVP pins).
uint8 Settings_EffectiveDoorShuffle(const RandoSettings *s);

// True iff ALTTPR's `rom.genericKeys` is in effect for these settings — i.e.
// `world_state == Retro` (Retro pins it on, per app/World/Retro.php). Like
// `Settings_EffectiveSmallKeysMode` this is *computed* from world_state, not a
// serialized bit, so no new canonical bytes enter the settings struct. Used by
// BuildItemPool (substitute each per-dungeon SmallKey with the fungible
// GenericKey) and by the predicate VM (a per-dungeon small-key requirement is
// satisfied by holding >=1 GenericKey — mirrors ALTTPR ItemCollection::has()'s
// ShopKey wildcard at app/Support/ItemCollection.php:271-273). NULL-safe.
bool Settings_GenericKeysActive(const RandoSettings *s);

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

// add-rando-major-glitch D6 — true when this seed's PLACEMENT assumed the
// player can perform a restored JP-1.0 glitch, so the runtime MUST force
// kFeatures0_RestoreJpGlitches on (else an assumed-fill-certified seed becomes
// an unreachable-item soft-softlock). Two triggers:
//   logic >= 1 (OverworldGlitches) — every glitch tier assumes the OWG
//     technique set, of which Fake Flippers (canFakeFlipper) + Superspeed
//     (canSuperSpeed) are restored on this US-1.0 build.
//   tricks bit 1 (fake-flippers) — the fake-flippers placement trick maps 1:1
//     to the restored Fake Flippers glitch and is independent of logic.
// Only fake-flippers among the 8 tricks maps to a restored glitch; the rest
// (boots-clip/pearl-bypass/...) are cross-version or unrestored and do NOT
// force the flag. Pure function of settings — no features0 dependency.
bool Rando_SettingsAssumeJpGlitches(const RandoSettings *s);

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
