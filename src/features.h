// This file declares extensions to the base game
#ifndef ZELDA3_FEATURES_H_
#define ZELDA3_FEATURES_H_

#include "types.h"

// Special RAM locations that are unused but I use for compat things.
//
// Layout:
//   0x648       kRam_APUI00              (1 byte)
//   0x649       kRam_CrystalRotateCounter (1 byte)
//   0x64a       kRam_BugsFixed           (1 byte) — 0x64b unused (padding)
//   0x64c-0x64f kRam_Features0           (uint32)
//   0x650-0x655 msu_curr_sample/volume/track (6 bytes, see below)
//   0x656-0x658 hud_cur_item_x/l/r       (3 bytes)
//   0x659-0x65c kRam_Features1           (uint32) — randomizer feature flags
//   0x65d       kRam_RandoSlotActive     (1 byte) — 0 vanilla, 1 randomizer
//   0x65e       kRam_RandoStartingInventoryGranted (1 byte) — once-per-slot gate
//   0x65f       kRam_RandoTriforcePieceCount (1 byte) — Phase A TriforceHunt counter
//   0x660       kRam_PreTemperSword      (1 byte) — rando: stash of Link's sword
//                                         tier during smithy tempering, so the
//                                         original is restored if the placed
//                                         Blacksmith reward isn't a sword
//   0x661       kRam_RandoSwordless      (1 byte) — rando: 1 when the active
//                                         slot is swordless. Persisted in g_ram
//                                         so a snapshot replay-restore (which
//                                         restores g_ram but not the C-static
//                                         RandoSettings) keeps the swordless
//                                         runtime patches firing.
//   0x662-0x665 kRam_EnemyShuffleVanPos2 (4 bytes) — rando: the vanilla-resolved
//                                         sprite GFX subgroup sheet for slots
//                                         0,1,2,3 of the current room/area,
//                                         tracked by the enemy-shuffle SHEET
//                                         reshuffle so an inherited (kSpriteTilesets
//                                         slot == 0) room is restored to vanilla
//                                         instead of a leaked reshuffle.
//                                         Snapshot-safe. (The enum name keeps its
//                                         original ...VanPos2 spelling; it is the
//                                         base of the 4-byte per-slot shadow array.)
//   0x666-0x66f reserved                 (10 bytes forward-compat headroom; when
//                                         ES_RESHUFFLE_DIAG is built =1, the
//                                         enemy-shuffle reshuffle writes PLAYTEST
//                                         diagnostics to 0x666-0x668 — hook-calls /
//                                         cumulative-slots-changed / last-room-
//                                         blocked-mask. Default-off, so reserved.)
//   0x670+      spotlight_* (DO NOT USE — see the `spotlight_*` declarations in variables.h)
//
// Verified clean in audit.md §0.7 (Phase 0 deliverable).
enum {
  kRam_APUI00 = 0x648,
  kRam_CrystalRotateCounter = 0x649,
  kRam_BugsFixed = 0x64a,
  kRam_Features0 = 0x64c,
  kRam_Features1 = 0x659,
  kRam_RandoSlotActive = 0x65d,
  kRam_RandoStartingInventoryGranted = 0x65e,
  kRam_RandoTriforcePieceCount = 0x65f,
  kRam_PreTemperSword = 0x660,
  kRam_RandoSwordless = 0x661,
  kRam_EnemyShuffleVanPos2 = 0x662,
};

enum {
  // Poly rendered uses correct speed
  kBugFix_PolyRenderer = 1,
  kBugFix_AncillaOverwrites = 1,
  kBugFix_Latest = 1,
};

// Enum values for kRam_Features0
enum {
  kFeatures0_ExtendScreen64 = 1,
  kFeatures0_SwitchLR = 2,
  kFeatures0_TurnWhileDashing = 4,
  kFeatures0_MirrorToDarkworld = 8,
  kFeatures0_CollectItemsWithSword = 16,
  kFeatures0_BreakPotsWithSword = 32,
  kFeatures0_DisableLowHealthBeep = 64,
  kFeatures0_SkipIntroOnKeypress = 128,
  kFeatures0_ShowMaxItemsInYellow = 256,
  kFeatures0_MoreActiveBombs = 512,

  // This is set for visual fixes that don't affect game behavior but will affect ram compare.
  kFeatures0_WidescreenVisualFixes = 1024,

  kFeatures0_CarryMoreRupees = 2048,

  kFeatures0_MiscBugFixes = 4096,

  kFeatures0_CancelBirdTravel = 8192,

  kFeatures0_GameChangingBugFixes = 16384,

  kFeatures0_SwitchLRLimit = 32768,

  kFeatures0_DimFlashes = 65536,

  // Make the Digging Game and Chest Game winnable for a half-competent player
  // (digging: guaranteed win within ~8 digs; chest game: win on the first
  // chest). Behavior-affecting (diverges from the original-ROM RAM compare), so
  // it is RANDO-ONLY by default: forced on under the randomizer, OFF in vanilla
  // and side-by-side. Vanilla players opt in with `EasyMinigames = 1`. Applied
  // at point-of-use (gated on kFeatures1_RandomizerActive) in
  // DiggingGameGuy_AttemptPrizeSpawn (player.c) + OpenMiniGameChest (dungeon.c).
  kFeatures0_EasyMinigames = 131072,

  // Re-introduce glitches present in the Japanese 1.0 ALTTP ROM but patched out
  // of the US 1.0 ROM this project reimplements (Fake Flippers et al.).
  // BEHAVIOR-AFFECTING: every restored glitch diverges from the original
  // US-1.0 RAM every frame, so this is DEFAULT-OFF and ALSO suppressed under
  // side-by-side comparison. Each glitch's point-of-use gate is
  //   (enhanced_features0 & kFeatures0_RestoreJpGlitches) && !ZeldaIsEmulatorAttached()
  // so attaching the original ROM keeps the per-frame RAM/SRAM/VRAM comparator
  // clean even if a user manually enabled the flag. The flag means "enable
  // every JP-1.0 glitch this build has verified-and-implemented"; adding a
  // verified glitch later needs no new flag/save-format/migration.
  kFeatures0_RestoreJpGlitches = 262144,  // bit 18

  // Select the destination overworld music/ambient the way the Japanese 1.0
  // ROM does (instead of US 1.0) in the two routines that run after a mirror
  // warp / long screen transition (MirrorWarp_FinalizeAndLoadDestination and
  // Overworld_FinalizeEntryOntoScreen). JP keys the post-warp track off world
  // state + screen index + progress rather than re-reading the overworld_music
  // scratch table, so the chosen song/ambient diverges from the US-1.0
  // reference RAM every frame. DEFAULT-OFF and suppressed under side-by-side
  // (point-of-use gate is
  //   (enhanced_features0 & kFeatures0_JpOverworldMusic) && !ZeldaIsEmulatorAttached()
  // ) so attaching the original US ROM keeps the RAM comparator clean. Only the
  // $12C/$12D (music_control/sound_effect_ambient) writes change; the
  // destination, Link position, screen index and camera math are untouched.
  kFeatures0_JpOverworldMusic = 524288,  // bit 19
};

// Enum values for kRam_Features1 (randomizer feature flags).
// Bits land here as Phase A features are implemented. The enum exists from
// A0 so the CI guards have something to scan; the values are populated
// incrementally.
enum {
  kFeatures1_RandomizerActive = 1,
  // Future bits reserved (per add-randomizer-support proposal):
  //   2  kFeatures1_ReservedTracker
  //   4  kFeatures1_ReservedSpoilerSuppress
  //   ...
};

#define enhanced_features0 (*(uint32*)(g_ram+0x64c))
#define enhanced_features1 (*(uint32*)(g_ram+0x659))
#define g_rando_slot_active (*(uint8*)(g_ram+0x65d))
#define g_rando_starting_inventory_granted (*(uint8*)(g_ram+0x65e))
#define g_rando_triforce_piece_count (*(uint8*)(g_ram+0x65f))
#define g_rando_swordless (*(uint8*)(g_ram+0x661))
#define msu_curr_sample (*(uint32*)(g_ram+0x650))
#define msu_volume (*(uint8*)(g_ram+0x654))
#define msu_track (*(uint8*)(g_ram+0x655))
#define hud_inventory_order ((uint8*)(g_ram + 0x225)) // 4x6 bytes
#define hud_cur_item_x (*(uint8*)(g_ram+0x656))
#define hud_cur_item_l (*(uint8*)(g_ram+0x657))
#define hud_cur_item_r (*(uint8*)(g_ram+0x658))



extern uint32 g_wanted_zelda_features;
extern uint32 g_wanted_zelda_features1;


#endif  // ZELDA3_FEATURES_H_
