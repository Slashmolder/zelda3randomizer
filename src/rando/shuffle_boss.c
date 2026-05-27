// shuffle_boss.c — Phase B Slice 7 (add-rando-shuffles-and-minigames) scaffold.
//
// See shuffle_boss.h. STUB: identity mapping; no actual shuffle yet.

#include "shuffle_boss.h"
#include <string.h>

static uint8 g_boss_assignment[16];
static bool g_boss_assignment_active = false;

bool BossShuffle_Generate(const RandoSettings *settings,
                          uint64 seed_u64,
                          uint8 out_assignment[16]) {
  (void)settings;
  (void)seed_u64;
  // Identity: dungeon_id -> dungeon_id (each dungeon keeps its vanilla
  // boss). Real impl will permute the pool deterministically from
  // seed_u64 + settings.boss_shuffle.
  for (uint8 i = 0; i < 16; i++) out_assignment[i] = i;
  memcpy(g_boss_assignment, out_assignment, sizeof(g_boss_assignment));
  g_boss_assignment_active = true;
  return true;
}

uint8 BossShuffle_GetForDungeon(uint8 dungeon_id) {
  if (!g_boss_assignment_active) return 0xFF;
  if (dungeon_id >= 16) return 0xFF;
  return g_boss_assignment[dungeon_id];
}
