// chains_runtime.h - dungeon-chain runtime bring-up hooks.
//
// This module is intentionally small while task 5.1 is still a spike. The
// debug arm is removed before merge; the entrance-hop primitive remains the
// shape the real chain seam hooks will consume.

#ifndef ZELDA3_RANDO_CHAINS_RUNTIME_H_
#define ZELDA3_RANDO_CHAINS_RUNTIME_H_

#include "../types.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
  kChainsRtReason_None = 0,
  kChainsRtReason_Armed,
  kChainsRtReason_WrongSeam,
  kChainsRtReason_HopRequested,
  kChainsRtReason_HopConsumed,
};

typedef struct ChainsRuntimeDebug {
  uint8 spike_armed;
  uint8 hop_pending;
  uint16 seam_checks;
  uint16 request_count;
  uint16 consume_count;
  uint16 last_source_room;
  uint16 last_destination_room;
  uint8 last_dir;
  uint8 last_entrance;
  uint8 last_reason;
  uint8 last_main_module;
  uint8 last_submodule;
  uint8 last_subsubmodule;
} ChainsRuntimeDebug;

void Chains_DebugArmEpBossToDesert(void);
void Chains_DebugClearEpBossToDesert(void);
const ChainsRuntimeDebug *Chains_DebugState(void);

// One-shot debug seam hook for task 5.1. Returns true when it consumed the
// transition and handed off to Module_PreDungeon.
bool Chains_TryDebugEpBossToDesertHop(uint8 dir,
                                      uint16 source_room,
                                      uint16 vanilla_destination_room);

// Consumed at the top of Dungeon_LoadEntrance. True means this entrance load is
// chain-owned and must not recache the overworld *_exit state.
bool Chains_ConsumeHopPending(void);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // ZELDA3_RANDO_CHAINS_RUNTIME_H_
