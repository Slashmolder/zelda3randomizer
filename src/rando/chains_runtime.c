// chains_runtime.c - dungeon-chain runtime hooks.

#include "chains_runtime.h"

#include "chain_boss_entrances.gen.h"
#include "chain_seams.gen.h"
#include "door_tables.gen.h"

#include "../assets.h"
#include "../dungeon.h"
#include "../hud.h"
#include "../variables.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define kChainsEntranceOverlayMax 4096

static bool g_chains_hop_pending;
static bool g_chains_runtime_active;
static bool g_chains_origin_active;
static bool g_chains_terminal_active;
static uint16 g_chains_origin_exit_room;
static uint8 g_chains_terminal_dungeon = kRandoDungeon_None;
static DungeonChainsLayout g_chains_runtime_layout;
static uint8 g_chains_entrance_overlay[kChainsEntranceOverlayMax];
static const uint8 *g_chains_entrance_overlay_orig;

_Static_assert(kChainBossEntranceBase == 133,
               "synthetic chain boss entrances must append after vanilla rows");
_Static_assert(kChainBossEntranceCount == 9,
               "dungeon-chain boss entrance count drift");

uint8 Chains_BossEntranceForRandoDungeon(uint8 rando_dungeon) {
  for (uint8 i = 0; i < kChainBossEntranceCount; i++) {
    if (kChainBossEntranceChecks[i].rando_dungeon == rando_dungeon)
      return kChainBossEntranceChecks[i].entrance_id;
  }
  return 0xFF;
}

static const ChainBossEntranceCheck *Chains_CheckForRandoDungeon(uint8 rando_dungeon) {
  for (uint8 i = 0; i < kChainBossEntranceCount; i++) {
    if (kChainBossEntranceChecks[i].rando_dungeon == rando_dungeon)
      return &kChainBossEntranceChecks[i];
  }
  return NULL;
}

static uint8 Chains_MainEntranceForRandoDungeon(uint8 rando_dungeon) {
  const ChainBossEntranceCheck *row = Chains_CheckForRandoDungeon(rando_dungeon);
  if (row != NULL)
    return row->main_entrance_id;
  return 0xFF;
}

static uint16 Chains_MainExitRoomForRandoDungeon(uint8 rando_dungeon) {
  const ChainBossEntranceCheck *row = Chains_CheckForRandoDungeon(rando_dungeon);
  if (row != NULL)
    return row->main_exit_room;
  return 0;
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

static bool Chains_InstallEntranceOverlay(const DungeonChainsLayout *layout) {
  if (layout == NULL || kOverworld_Entrance_Id == NULL)
    return false;
  uint32 len = kOverworld_Entrance_Id_SIZE;
  if (len == 0 || len > kChainsEntranceOverlayMax)
    return false;

  const uint8 *vanilla = (const uint8 *)kOverworld_Entrance_Id;
  memcpy(g_chains_entrance_overlay, vanilla, len);
  for (uint8 i = 0; i < kChainsPoolCount; i++) {
    uint8 source_dungeon = Chains_PoolDungeonAt(i);
    uint8 source_entrance = Chains_MainEntranceForRandoDungeon(source_dungeon);
    uint8 target_entrance = Chains_EntranceForElement(layout->chain_door_first[i]);
    if (source_entrance == 0xFF || target_entrance == 0xFF)
      return false;

    bool found = false;
    for (uint32 lx = 0; lx < len; lx++) {
      if (vanilla[lx] == source_entrance) {
        g_chains_entrance_overlay[lx] = target_entrance;
        found = true;
      }
    }
    if (!found)
      return false;
  }

  g_chains_entrance_overlay_orig = vanilla;
  g_asset_ptrs[126] = g_chains_entrance_overlay;
  return true;
}

void Chains_RuntimeTeardown(void) {
  if (g_chains_entrance_overlay_orig != NULL) {
    g_asset_ptrs[126] = (void *)g_chains_entrance_overlay_orig;
    g_chains_entrance_overlay_orig = NULL;
  }
  g_chains_runtime_active = false;
  g_chains_origin_active = false;
  g_chains_terminal_active = false;
  g_chains_origin_exit_room = 0;
  g_chains_terminal_dungeon = kRandoDungeon_None;
  g_chains_hop_pending = false;
  memset(&g_chains_runtime_layout, 0xFF, sizeof(g_chains_runtime_layout));
}

bool Chains_RuntimeInstallLayout(const DungeonChainsLayout *layout) {
  if (layout == NULL) {
    Chains_RuntimeTeardown();
    return false;
  }
  DungeonChainsLayout copy = *layout;
  Chains_RuntimeTeardown();
  if (!Chains_SyntheticEntrancesAvailable())
    return false;
  if (!Chains_InstallEntranceOverlay(&copy))
    return false;
  g_chains_runtime_layout = copy;
  g_chains_runtime_active = true;
  return true;
}

void Chains_RuntimeArmOrigin(uint16 origin_exit_room) {
  g_chains_origin_active = origin_exit_room != 0;
  g_chains_origin_exit_room = origin_exit_room;
  g_chains_terminal_active = false;
  g_chains_terminal_dungeon = kRandoDungeon_None;
}

void Chains_RuntimeClearOrigin(void) {
  g_chains_origin_active = false;
  g_chains_terminal_active = false;
  g_chains_origin_exit_room = 0;
  g_chains_terminal_dungeon = kRandoDungeon_None;
}

static bool Chains_IsMainExitRoom(uint16 room) {
  for (uint8 i = 0; i < kChainsPoolCount; i++) {
    if (Chains_MainExitRoomForRandoDungeon(Chains_PoolDungeonAt(i)) == room)
      return true;
  }
  return false;
}

bool Chains_RuntimeRecordDoorEntry(uint16 lx) {
  if (!g_chains_runtime_active || g_chains_entrance_overlay_orig == NULL)
    return false;
  uint32 len = kOverworld_Entrance_Id_SIZE;
  if (lx >= len)
    return false;

  uint8 source_entrance = g_chains_entrance_overlay_orig[lx];
  for (uint8 i = 0; i < kChainsPoolCount; i++) {
    uint8 source_dungeon = Chains_PoolDungeonAt(i);
    const ChainBossEntranceCheck *row = Chains_CheckForRandoDungeon(source_dungeon);
    if (row == NULL || row->main_entrance_id != source_entrance)
      continue;

    uint8 first = g_chains_runtime_layout.chain_door_first[i];
    Chains_RuntimeArmOrigin(row->main_exit_room);
    if (Chains_ElementIsBoss(first)) {
      g_chains_terminal_active = true;
      g_chains_terminal_dungeon = Chains_ElementDungeon(first);
    }
    return true;
  }
  return false;
}

uint16 Chains_RuntimeConsumeMainExitOrigin(uint16 resolved_exit_room) {
  if (!g_chains_runtime_active || !g_chains_origin_active ||
      g_chains_origin_exit_room == 0 || !Chains_IsMainExitRoom(resolved_exit_room)) {
    return 0;
  }

  uint16 origin = g_chains_origin_exit_room;
  g_chains_origin_active = false;
  g_chains_terminal_active = false;
  g_chains_origin_exit_room = 0;
  g_chains_terminal_dungeon = kRandoDungeon_None;
  return origin;
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
  (void)source_room;
  (void)destination_room;
  if (entrance >= kChainBossEntranceBase && entrance < kChainBossEntranceLimit &&
      !Chains_SyntheticEntrancesAvailable()) {
    return false;
  }

  g_chains_hop_pending = true;
  g_chains_terminal_active = terminal;
  g_chains_terminal_dungeon = terminal ? terminal_dungeon : (uint8)kRandoDungeon_None;

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
  (void)source_room;
  (void)destination_room;
  if (!g_chains_origin_active || g_chains_origin_exit_room == 0)
    return false;

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

  int pool_idx = Chains_PoolIndexForDungeon(seam->rando_dungeon);
  if (pool_idx < 0)
    return false;

  uint8 successor = g_chains_runtime_layout.chain_successor[pool_idx];
  if (successor == Chains_BossElement(seam->rando_dungeon)) {
    if (g_chains_origin_active) {
      g_chains_terminal_active = true;
      g_chains_terminal_dungeon = seam->rando_dungeon;
    }
    return false;
  }

  uint8 entrance = Chains_EntranceForElement(successor);
  if (entrance == 0xFF)
    return false;

  uint8 terminal_dungeon = Chains_ElementDungeon(successor);
  return Chains_RequestEntranceHop(entrance, source_room, vanilla_destination_room,
                                   Chains_ElementIsBoss(successor),
                                   terminal_dungeon);
}

bool Chains_ConsumeHopPending(void) {
  if (!g_chains_hop_pending)
    return false;
  g_chains_hop_pending = false;
  return true;
}

static void Chains_RuntimeSelfCheckDie(const char *msg) {
  Chains_RuntimeTeardown();
  fprintf(stderr, "Chains_RuntimeSelfCheck: %s\n", msg);
  exit(2);
}

static uint16 Chains_RuntimeSelfCheckFindDoor(const uint8 *ids, uint32 len,
                                              uint8 entrance_id) {
  if (ids == NULL)
    return 0xFFFF;
  for (uint32 lx = 0; lx < len && lx <= 0xFFFFu; lx++) {
    if (ids[lx] == entrance_id)
      return (uint16)lx;
  }
  return 0xFFFF;
}

void Chains_RuntimeSelfCheck(void) {
  static uint8 synth_door_ids[256];
  static uint16 synth_u16[kChainBossEntranceLimit];
  static uint16 synth_rooms[kChainBossEntranceLimit];
  static uint8 synth_u8[kChainBossEntranceLimit];
  static uint8 synth_relative[kChainBossEntranceLimit * 8];
  static int8 synth_i8[kChainBossEntranceLimit];
  static int8 synth_palace[kChainBossEntranceLimit];
  static uint8 synth_music[kChainBossEntranceLimit];
  static const uint8 kSynthAssetIds[] = {
    11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 126,
  };
  const uint8 *saved_ptrs[sizeof(kSynthAssetIds)];
  uint32 saved_sizes[sizeof(kSynthAssetIds)];
  bool synth_assets = kOverworld_Entrance_Id == NULL ||
                      kOverworld_Entrance_Id_SIZE == 0 ||
                      !Chains_SyntheticEntrancesAvailable();
  if (synth_assets) {
    for (uint32 i = 0; i < (uint32)sizeof(kSynthAssetIds); i++) {
      uint8 id = kSynthAssetIds[i];
      saved_ptrs[i] = g_asset_ptrs[id];
      saved_sizes[i] = g_asset_sizes[id];
    }
    memset(synth_door_ids, 0, sizeof(synth_door_ids));
    memset(synth_u16, 0, sizeof(synth_u16));
    memset(synth_rooms, 0, sizeof(synth_rooms));
    memset(synth_u8, 0, sizeof(synth_u8));
    memset(synth_relative, 0, sizeof(synth_relative));
    memset(synth_i8, 0, sizeof(synth_i8));
    memset(synth_palace, 0, sizeof(synth_palace));
    memset(synth_music, 0, sizeof(synth_music));
    for (uint8 i = 0; i < kChainsPoolCount; i++) {
      const ChainBossEntranceCheck *row =
          Chains_CheckForRandoDungeon(Chains_PoolDungeonAt(i));
      synth_door_ids[16 + i] = row->main_entrance_id;
    }
    for (uint8 i = 0; i < kChainBossEntranceCount; i++) {
      const ChainBossEntranceCheck *row = &kChainBossEntranceChecks[i];
      synth_rooms[row->entrance_id] = row->room;
      synth_palace[row->entrance_id] = row->palace;
      synth_music[row->entrance_id] = row->music;
    }
    g_asset_ptrs[11] = (const uint8 *)synth_rooms;
    g_asset_sizes[11] = sizeof(synth_rooms);
    g_asset_ptrs[12] = synth_relative;
    g_asset_sizes[12] = sizeof(synth_relative);
    g_asset_ptrs[13] = (const uint8 *)synth_u16;
    g_asset_sizes[13] = sizeof(synth_u16);
    g_asset_ptrs[14] = (const uint8 *)synth_u16;
    g_asset_sizes[14] = sizeof(synth_u16);
    g_asset_ptrs[15] = (const uint8 *)synth_u16;
    g_asset_sizes[15] = sizeof(synth_u16);
    g_asset_ptrs[16] = (const uint8 *)synth_u16;
    g_asset_sizes[16] = sizeof(synth_u16);
    g_asset_ptrs[17] = (const uint8 *)synth_u16;
    g_asset_sizes[17] = sizeof(synth_u16);
    g_asset_ptrs[18] = (const uint8 *)synth_u16;
    g_asset_sizes[18] = sizeof(synth_u16);
    g_asset_ptrs[19] = synth_u8;
    g_asset_sizes[19] = sizeof(synth_u8);
    g_asset_ptrs[20] = (const uint8 *)synth_i8;
    g_asset_sizes[20] = sizeof(synth_i8);
    g_asset_ptrs[21] = (const uint8 *)synth_palace;
    g_asset_sizes[21] = sizeof(synth_palace);
    g_asset_ptrs[22] = synth_u8;
    g_asset_sizes[22] = sizeof(synth_u8);
    g_asset_ptrs[23] = synth_u8;
    g_asset_sizes[23] = sizeof(synth_u8);
    g_asset_ptrs[24] = synth_u8;
    g_asset_sizes[24] = sizeof(synth_u8);
    g_asset_ptrs[25] = synth_u8;
    g_asset_sizes[25] = sizeof(synth_u8);
    g_asset_ptrs[26] = (const uint8 *)synth_u16;
    g_asset_sizes[26] = sizeof(synth_u16);
    g_asset_ptrs[27] = synth_music;
    g_asset_sizes[27] = sizeof(synth_music);
    g_asset_ptrs[126] = synth_door_ids;
    g_asset_sizes[126] = sizeof(synth_door_ids);
  }

  const uint8 *vanilla = (const uint8 *)kOverworld_Entrance_Id;
  uint32 len = kOverworld_Entrance_Id_SIZE;
  if (vanilla == NULL || len == 0 || !Chains_SyntheticEntrancesAvailable())
    Chains_RuntimeSelfCheckDie("asset fixture unavailable");

  DungeonChainsLayout layout;
  memset(&layout, kChainsElement_None, sizeof(layout));
  for (uint8 i = 0; i < kChainsPoolCount; i++) {
    uint8 source = Chains_PoolDungeonAt(i);
    uint8 target = Chains_PoolDungeonAt((uint8)((i + 1) % kChainsPoolCount));
    layout.chain_door_first[i] = Chains_DungeonElement(target);
    layout.chain_successor[i] = Chains_BossElement(source);
    layout.terminal_boss[i] = source;
  }

  if (!Chains_RuntimeInstallLayout(&layout))
    Chains_RuntimeSelfCheckDie("rotated layout install failed");
  if ((const uint8 *)kOverworld_Entrance_Id == vanilla)
    Chains_RuntimeSelfCheckDie("asset 126 was not repointed");

  for (uint8 i = 0; i < kChainsPoolCount; i++) {
    uint8 source = Chains_PoolDungeonAt(i);
    uint8 target = Chains_PoolDungeonAt((uint8)((i + 1) % kChainsPoolCount));
    uint16 lx = Chains_RuntimeSelfCheckFindDoor(
        vanilla, len, Chains_MainEntranceForRandoDungeon(source));
    if (lx == 0xFFFF)
      Chains_RuntimeSelfCheckDie("source door missing from vanilla table");
    if (kOverworld_Entrance_Id[lx] != Chains_MainEntranceForRandoDungeon(target))
      Chains_RuntimeSelfCheckDie("chain-start overlay target mismatch");
  }

  uint8 ep = kRandoDungeon_EasternPalace;
  uint16 ep_lx = Chains_RuntimeSelfCheckFindDoor(
      vanilla, len, Chains_MainEntranceForRandoDungeon(ep));
  if (ep_lx == 0xFFFF || !Chains_RuntimeRecordDoorEntry(ep_lx))
    Chains_RuntimeSelfCheckDie("origin entry arm failed");
  if (!g_chains_origin_active ||
      g_chains_origin_exit_room != Chains_MainExitRoomForRandoDungeon(ep))
    Chains_RuntimeSelfCheckDie("origin room was not armed");
  if (Chains_RuntimeConsumeMainExitOrigin(0x0123) != 0 ||
      !g_chains_origin_active)
    Chains_RuntimeSelfCheckDie("non-main exit consumed chain origin");
  if (Chains_RuntimeConsumeMainExitOrigin(Chains_MainExitRoomForRandoDungeon(ep)) !=
      Chains_MainExitRoomForRandoDungeon(ep))
    Chains_RuntimeSelfCheckDie("main exit did not consume origin");
  if (g_chains_origin_active)
    Chains_RuntimeSelfCheckDie("origin remained armed after main exit");

  layout.chain_door_first[0] = Chains_BossElement(kRandoDungeon_DesertPalace);
  if (!Chains_RuntimeInstallLayout(&layout))
    Chains_RuntimeSelfCheckDie("terminal layout install failed");
  ep_lx = Chains_RuntimeSelfCheckFindDoor(
      vanilla, len, Chains_MainEntranceForRandoDungeon(ep));
  if (ep_lx == 0xFFFF ||
      kOverworld_Entrance_Id[ep_lx] != Chains_BossEntranceForRandoDungeon(kRandoDungeon_DesertPalace))
    Chains_RuntimeSelfCheckDie("terminal boss overlay target mismatch");
  if (!Chains_RuntimeRecordDoorEntry(ep_lx) ||
      !g_chains_origin_active ||
      !g_chains_terminal_active)
    Chains_RuntimeSelfCheckDie("terminal entry did not arm origin and terminal state");

  Chains_RuntimeTeardown();
  if ((const uint8 *)kOverworld_Entrance_Id != vanilla)
    Chains_RuntimeSelfCheckDie("asset 126 was not restored");
  if (synth_assets) {
    for (uint32 i = 0; i < (uint32)sizeof(kSynthAssetIds); i++) {
      uint8 id = kSynthAssetIds[i];
      g_asset_ptrs[id] = saved_ptrs[i];
      g_asset_sizes[id] = saved_sizes[i];
    }
  }
  fprintf(stderr, "[Chains_RuntimeSelfCheck] OK\n");
}
