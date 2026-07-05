// chains_runtime.c - dungeon-chain runtime bring-up hooks.

#include "chains_runtime.h"

#include "door_tables.gen.h"

#include "../hud.h"
#include "../variables.h"

#include <stdio.h>

enum {
  kChainsDebugEntrance_DesertMain = 0x09,
  kChainsDebugRoom_EasternDuoEyegores = 0x00D8,
  kChainsDebugRoom_EasternBoss = 0x00C8,
};

static bool g_chains_hop_pending;
static bool g_chains_ep_spike_armed;
static ChainsRuntimeDebug g_chains_debug;

static void Chains_DebugSetReason(uint8 reason) {
  g_chains_debug.last_reason = reason;
  g_chains_debug.spike_armed = g_chains_ep_spike_armed ? 1 : 0;
  g_chains_debug.hop_pending = g_chains_hop_pending ? 1 : 0;
}

const ChainsRuntimeDebug *Chains_DebugState(void) {
  g_chains_debug.spike_armed = g_chains_ep_spike_armed ? 1 : 0;
  g_chains_debug.hop_pending = g_chains_hop_pending ? 1 : 0;
  return &g_chains_debug;
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

static void Chains_RequestEntranceHop(uint8 entrance,
                                      uint16 source_room,
                                      uint16 destination_room) {
  g_chains_hop_pending = true;
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
  Chains_RequestEntranceHop(kChainsDebugEntrance_DesertMain,
                            source_room, vanilla_destination_room);
  return true;
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
