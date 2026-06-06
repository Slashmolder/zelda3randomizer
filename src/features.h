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
//   0x662-0x66f reserved                 (14 bytes forward-compat headroom)
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
  // chest). Default ON (see ParseConfigFile). Behavior-affecting, so it diverges
  // from the original-ROM RAM compare - toggle off (EasyMinigames = 0) for
  // side-by-side vanilla verification. Read at point-of-use in
  // DiggingGameGuy_AttemptPrizeSpawn (player.c) + OpenMiniGameChest (dungeon.c).
  kFeatures0_EasyMinigames = 131072,
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
