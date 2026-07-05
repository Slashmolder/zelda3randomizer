// chains_runtime.c - dungeon-chain runtime bring-up hooks.

#include "chains_runtime.h"

#include "chain_boss_entrances.gen.h"
#include "chain_seams.gen.h"
#include "door_tables.gen.h"

#include "../assets.h"
#include "../dungeon.h"
#include "../hud.h"
#include "../variables.h"

#include <stdio.h>
#include <string.h>

enum {
  kChainsDebugEntrance_DesertMain = 0x09,
  kChainsDebugRoom_EasternDuoEyegores = 0x00D8,
  kChainsDebugRoom_EasternBoss = 0x00C8,
};

static bool g_chains_hop_pending;
static bool g_chains_ep_spike_armed;
static bool g_chains_runtime_active;
static bool g_chains_origin_active;
static bool g_chains_terminal_active;
static uint16 g_chains_origin_exit_room;
static uint8 g_chains_terminal_dungeon = kRandoDungeon_None;
static DungeonChainsLayout g_chains_runtime_layout;
static ChainsRuntimeDebug g_chains_debug;

_Static_assert(kChainBossEntranceBase == 133,
               "synthetic chain boss entrances must append after vanilla rows");
_Static_assert(kChainBossEntranceCount == 9,
               "dungeon-chain boss entrance count drift");

static void Chains_DebugSetReason(uint8 reason) {
  g_chains_debug.last_reason = reason;
  g_chains_debug.spike_armed = g_chains_ep_spike_armed ? 1 : 0;
  g_chains_debug.hop_pending = g_chains_hop_pending ? 1 : 0;
  g_chains_debug.origin_active = g_chains_origin_active ? 1 : 0;
  g_chains_debug.terminal_active = g_chains_terminal_active ? 1 : 0;
}

const ChainsRuntimeDebug *Chains_DebugState(void) {
  g_chains_debug.spike_armed = g_chains_ep_spike_armed ? 1 : 0;
  g_chains_debug.hop_pending = g_chains_hop_pending ? 1 : 0;
  g_chains_debug.origin_active = g_chains_origin_active ? 1 : 0;
  g_chains_debug.terminal_active = g_chains_terminal_active ? 1 : 0;
  g_chains_debug.origin_exit_room = g_chains_origin_exit_room;
  return &g_chains_debug;
}

uint8 Chains_BossEntranceForRandoDungeon(uint8 rando_dungeon) {
  for (uint8 i = 0; i < kChainBossEntranceCount; i++) {
    if (kChainBossEntranceChecks[i].rando_dungeon == rando_dungeon)
      return kChainBossEntranceChecks[i].entrance_id;
  }
  return 0xFF;
}

static uint8 Chains_MainEntranceForRandoDungeon(uint8 rando_dungeon) {
  for (uint8 i = 0; i < kChainBossEntranceCount; i++) {
    if (kChainBossEntranceChecks[i].rando_dungeon == rando_dungeon)
      return kChainBossEntranceChecks[i].main_entrance_id;
  }
  return 0xFF;
}

static uint8 Chains_EntranceForElement(uint8 elem) {
  uint8 rando_dungeon = Chains_ElementDungeon(elem);
  if (elem == kChainsElement_None || Chains_PoolIndexForDungeon(rando_dungeon) < 0)
    return 0xFF;
  return Chains_ElementIsBoss(elem)
             ? Chains_BossEntranceForRandoDungeon(rando_dungeon)
             : Chains_MainEntranceForRandoDungeon(rando_dungeon);
}

static bool Chains_EntranceAssetCountOk(uint32 size, uint32 bytes_per_entry) {
  return size >= (uint32)kChainBossEntranceLimit * bytes_per_entry;
}

bool Chains_SyntheticEntrancesAvailable(void) {
  if (kEntranceData_rooms == NULL || kEntranceData_palace == NULL ||
      kEntranceData_musicTrack == NULL) {
    return false;
  }
  if (!Chains_EntranceAssetCountOk(kEntranceData_rooms_SIZE, 2) ||
      !Chains_EntranceAssetCountOk(kEntranceData_relativeCoords_SIZE, 8) ||
      !Chains_EntranceAssetCountOk(kEntranceData_scrollX_SIZE, 2) ||
      !Chains_EntranceAssetCountOk(kEntranceData_scrollY_SIZE, 2) ||
      !Chains_EntranceAssetCountOk(kEntranceData_playerX_SIZE, 2) ||
      !Chains_EntranceAssetCountOk(kEntranceData_playerY_SIZE, 2) ||
      !Chains_EntranceAssetCountOk(kEntranceData_cameraX_SIZE, 2) ||
      !Chains_EntranceAssetCountOk(kEntranceData_cameraY_SIZE, 2) ||
      !Chains_EntranceAssetCountOk(kEntranceData_blockset_SIZE, 1) ||
      !Chains_EntranceAssetCountOk(kEntranceData_floor_SIZE, 1) ||
      !Chains_EntranceAssetCountOk(kEntranceData_palace_SIZE, 1) ||
      !Chains_EntranceAssetCountOk(kEntranceData_doorwayOrientation_SIZE, 1) ||
      !Chains_EntranceAssetCountOk(kEntranceData_startingBg_SIZE, 1) ||
      !Chains_EntranceAssetCountOk(kEntranceData_quadrant1_SIZE, 1) ||
      !Chains_EntranceAssetCountOk(kEntranceData_quadrant2_SIZE, 1) ||
      !Chains_EntranceAssetCountOk(kEntranceData_doorSettings_SIZE, 2) ||
      !Chains_EntranceAssetCountOk(kEntranceData_musicTrack_SIZE, 1)) {
    return false;
  }
  for (uint8 i = 0; i < kChainBossEntranceCount; i++) {
    const ChainBossEntranceCheck *row = &kChainBossEntranceChecks[i];
    uint8 entrance = row->entrance_id;
    if (kEntranceData_rooms[entrance] != row->room ||
        kEntranceData_palace[entrance] != row->palace ||
        kEntranceData_musicTrack[entrance] != row->music) {
      return false;
    }
  }
  return true;
}

void Chains_RuntimeTeardown(void) {
  g_chains_runtime_active = false;
  g_chains_origin_active = false;
  g_chains_terminal_active = false;
  g_chains_origin_exit_room = 0;
  g_chains_terminal_dungeon = kRandoDungeon_None;
  g_chains_hop_pending = false;
  memset(&g_chains_runtime_layout, 0xFF, sizeof(g_chains_runtime_layout));
  Chains_DebugSetReason(kChainsRtReason_None);
}

void Chains_RuntimeInstallLayout(const DungeonChainsLayout *layout) {
  if (layout == NULL) {
    Chains_RuntimeTeardown();
    return;
  }
  g_chains_runtime_layout = *layout;
  g_chains_runtime_active = true;
}

void Chains_RuntimeArmOrigin(uint16 origin_exit_room) {
  g_chains_origin_active = origin_exit_room != 0;
  g_chains_origin_exit_room = origin_exit_room;
  g_chains_terminal_active = false;
  g_chains_terminal_dungeon = kRandoDungeon_None;
  Chains_DebugSetReason(kChainsRtReason_None);
}

void Chains_RuntimeClearOrigin(void) {
  g_chains_origin_active = false;
  g_chains_terminal_active = false;
  g_chains_origin_exit_room = 0;
  g_chains_terminal_dungeon = kRandoDungeon_None;
  Chains_DebugSetReason(kChainsRtReason_None);
}

void Chains_DebugArmEpBossToDesert(void) {
  g_chains_ep_spike_armed = true;
  Chains_DebugSetReason(kChainsRtReason_Armed);
  fprintf(stderr, "dungeon-chains spike: armed EP boss seam -> Desert main entrance\n");
}

void Chains_DebugClearEpBossToDesert(void) {
  g_chains_ep_spike_armed = false;
  Chains_DebugSetReason(kChainsRtReason_None);
  fprintf(stderr, "dungeon-chains spike: disarmed\n");
}

static void Chains_ClearFollowerForHop(void) {
  bool had_super_bomb = follower_indicator == 13 ||
                        super_bomb_indicator_unk1 != 0 ||
                        super_bomb_indicator_unk2 != 0;
  WORD(follower_indicator) = 0;
  follower_dropped = 0;
  saved_tagalong_y = 0;
  saved_tagalong_x = 0;
  saved_tagalong_indoors = 0;
  saved_tagalong_floor = 0;
  tagalong_var1 = 0;
  tagalong_var2 = 0;
  tagalong_var3 = 0;
  tagalong_var4 = 0;
  tagalong_var5 = 0;
  tagalong_var7 = 0;
  tagalong_event_flags = 0;
  timer_tagalong_reacquire = 0;
  if (had_super_bomb) {
    super_bomb_indicator_unk1 = 0;
    super_bomb_indicator_unk2 = 0;
    Hud_RemoveSuperBombIndicator();
  }
}

static bool Chains_RequestEntranceHop(uint8 entrance,
                                      uint16 source_room,
                                      uint16 destination_room,
                                      bool terminal,
                                      uint8 terminal_dungeon) {
  if (entrance >= kChainBossEntranceBase && entrance < kChainBossEntranceLimit &&
      !Chains_SyntheticEntrancesAvailable()) {
    g_chains_debug.last_source_room = source_room;
    g_chains_debug.last_destination_room = destination_room;
    g_chains_debug.last_entrance = entrance;
    Chains_DebugSetReason(kChainsRtReason_MissingSyntheticEntrances);
    fprintf(stderr,
            "dungeon-chains: synthetic entrance %02x unavailable; regenerate zelda3_assets.dat\n",
            entrance);
    return false;
  }

  g_chains_hop_pending = true;
  g_chains_terminal_active = terminal;
  g_chains_terminal_dungeon = terminal ? terminal_dungeon : (uint8)kRandoDungeon_None;
  g_chains_debug.request_count++;
  g_chains_debug.last_source_room = source_room;
  g_chains_debug.last_destination_room = destination_room;
  g_chains_debug.last_entrance = entrance;
  g_chains_debug.last_main_module = main_module_index;
  g_chains_debug.last_submodule = submodule_index;
  g_chains_debug.last_subsubmodule = subsubmodule_index;
  Chains_DebugSetReason(kChainsRtReason_HopRequested);

  Chains_ClearFollowerForHop();
  WORD(death_var4) = 0;
  WORD(death_var5) = 0;
  room_transitioning_flags = 0;
  link_this_controls_sprite_oam = 0;
  player_near_pit_state = 0;
  link_visibility_status = 0;
  is_standing_in_doorway = 0;
  WORD(dung_cur_door_pos) = 0;
  WORD(door_animation_step_indicator) = 0;
  which_entrance = entrance;
  player_is_indoors = 1;
  subsubmodule_index = 0;
  submodule_index = 0;
  main_module_index = 6;  // Module_PreDungeon runs next frame.

  fprintf(stderr,
          "dungeon-chains spike: request entrance hop src=%04x dst=%04x entrance=%02x\n",
          source_room, destination_room, entrance);
  return true;
}

static const ChainSeamRow *Chains_FindBossSeam(uint8 kind,
                                               uint8 direction,
                                               uint16 source_room,
                                               uint16 destination_room,
                                               uint8 slot) {
  for (uint8 i = 0; i < kChainBossSeamCount; i++) {
    const ChainSeamRow *row = &kChainBossSeams[i];
    if (row->kind != kind ||
        row->source_room != source_room ||
        row->dest_room != destination_room) {
      continue;
    }
    if (direction != kChainSeamDir_None && row->direction != direction)
      continue;
    if (slot != 0xFF && row->slot != slot)
      continue;
    return row;
  }
  return NULL;
}

static const ChainSeamRow *Chains_FindOutboundSeam(uint8 kind,
                                                   uint8 direction,
                                                   uint16 source_room,
                                                   uint16 destination_room,
                                                   uint8 slot) {
  for (uint8 i = 0; i < kChainBossOutboundSeamCount; i++) {
    const ChainSeamRow *row = &kChainBossOutboundSeams[i];
    if (row->kind != kind ||
        row->source_room != source_room ||
        row->dest_room != destination_room) {
      continue;
    }
    if (direction != kChainSeamDir_None && row->direction != direction)
      continue;
    if (slot != 0xFF && row->slot != slot)
      continue;
    return row;
  }
  return NULL;
}

static bool Chains_RequestTerminalExit(uint16 source_room,
                                       uint16 destination_room) {
  g_chains_debug.last_source_room = source_room;
  g_chains_debug.last_destination_room = destination_room;
  if (!g_chains_origin_active || g_chains_origin_exit_room == 0) {
    Chains_DebugSetReason(kChainsRtReason_MissingOrigin);
    return false;
  }

  g_chains_debug.request_count++;
  uint16 origin_exit_room = g_chains_origin_exit_room;
  SaveDungeonKeys();
  SaveQuadrantsToSram();
  dungeon_room_index = origin_exit_room;
  room_transitioning_flags = 0;
  is_standing_in_doorway = 0;
  saved_module_for_menu = 8;
  main_module_index = 15;
  submodule_index = 0;
  subsubmodule_index = 0;
  Dungeon_ResetTorchBackgroundAndPlayerInner();

  g_chains_origin_active = false;
  g_chains_terminal_active = false;
  g_chains_origin_exit_room = 0;
  g_chains_terminal_dungeon = kRandoDungeon_None;
  Chains_DebugSetReason(kChainsRtReason_TerminalExit);
  fprintf(stderr,
          "dungeon-chains: terminal exit src=%04x dst=%04x origin_room=%04x\n",
          source_room, destination_room, origin_exit_room);
  return true;
}

bool Chains_TryTerminalOutboundSeam(uint8 kind,
                                    uint8 direction,
                                    uint16 source_room,
                                    uint16 vanilla_destination_room,
                                    uint8 slot) {
  if (!g_chains_runtime_active || !g_chains_terminal_active)
    return false;

  const ChainSeamRow *seam = Chains_FindOutboundSeam(kind, direction, source_room,
                                                     vanilla_destination_room, slot);
  if (seam == NULL || seam->rando_dungeon != g_chains_terminal_dungeon)
    return false;

  g_chains_debug.seam_checks++;
  g_chains_debug.last_dir = direction;
  return Chains_RequestTerminalExit(source_room, vanilla_destination_room);
}

bool Chains_TryBossSeamHop(uint8 kind,
                           uint8 direction,
                           uint16 source_room,
                           uint16 vanilla_destination_room,
                           uint8 slot) {
  if (!g_chains_runtime_active)
    return false;

  const ChainSeamRow *seam = Chains_FindBossSeam(kind, direction, source_room,
                                                 vanilla_destination_room, slot);
  if (seam == NULL)
    return false;

  g_chains_debug.seam_checks++;
  g_chains_debug.last_dir = direction;
  g_chains_debug.last_source_room = source_room;
  g_chains_debug.last_destination_room = vanilla_destination_room;

  int pool_idx = Chains_PoolIndexForDungeon(seam->rando_dungeon);
  if (pool_idx < 0)
    return false;

  uint8 successor = g_chains_runtime_layout.chain_successor[pool_idx];
  if (successor == Chains_BossElement(seam->rando_dungeon)) {
    if (g_chains_origin_active) {
      g_chains_terminal_active = true;
      g_chains_terminal_dungeon = seam->rando_dungeon;
    }
    Chains_DebugSetReason(kChainsRtReason_PinnedIdentity);
    return false;
  }

  uint8 entrance = Chains_EntranceForElement(successor);
  if (entrance == 0xFF) {
    Chains_DebugSetReason(kChainsRtReason_BadSuccessor);
    return false;
  }

  uint8 terminal_dungeon = Chains_ElementDungeon(successor);
  return Chains_RequestEntranceHop(entrance, source_room, vanilla_destination_room,
                                   Chains_ElementIsBoss(successor),
                                   terminal_dungeon);
}

bool Chains_TryDebugEpBossToDesertHop(uint8 dir,
                                      uint16 source_room,
                                      uint16 vanilla_destination_room) {
  if (!g_chains_ep_spike_armed)
    return false;

  g_chains_debug.seam_checks++;
  g_chains_debug.last_dir = dir;
  g_chains_debug.last_source_room = source_room;
  g_chains_debug.last_destination_room = vanilla_destination_room;

  bool match = dir == kDoorTblDir_North &&
               source_room == kChainsDebugRoom_EasternDuoEyegores &&
               vanilla_destination_room == kChainsDebugRoom_EasternBoss;
  if (!match) {
    Chains_DebugSetReason(kChainsRtReason_WrongSeam);
    return false;
  }

  g_chains_ep_spike_armed = false;
  return Chains_RequestEntranceHop(kChainsDebugEntrance_DesertMain,
                                   source_room, vanilla_destination_room,
                                   false, (uint8)kRandoDungeon_None);
}

bool Chains_ConsumeHopPending(void) {
  if (!g_chains_hop_pending)
    return false;
  g_chains_hop_pending = false;
  g_chains_debug.consume_count++;
  Chains_DebugSetReason(kChainsRtReason_HopConsumed);
  fprintf(stderr, "dungeon-chains spike: consumed entrance-hop pending flag\n");
  return true;
}
