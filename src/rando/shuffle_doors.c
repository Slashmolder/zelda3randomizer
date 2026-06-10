// shuffle_doors.c — door-shuffle generation (stitcher + explorer). STUB:
// the full implementation lands with Stage 2 (see shuffle_doors.h contract).
#include "shuffle_doors.h"

#include <stdio.h>
#include <string.h>

bool DoorShuffle_Generate(uint64 seed, uint32 attempt, uint16 dungeon_mask,
                          DoorShuffleLayout *out) {
  (void)seed; (void)attempt; (void)dungeon_mask;
  memset(out, 0xFF, sizeof(out->pairing));
  memset(out->key_door_count, 0, sizeof(out->key_door_count));
  out->bk_restricted_count = 0;
  out->shuffled_mask = 0;
  return false;
}

uint32 DoorShuffle_LayoutDigest(const DoorShuffleLayout *l) {
  (void)l;
  return 0;
}

void DoorExplore_Run(const DoorShuffleLayout *layout, uint8 dungeon,
                     const uint16 *portal_regions, int portal_count,
                     const DoorExploreGates *gates, DoorExploreResult *out) {
  (void)layout; (void)dungeon; (void)portal_regions; (void)portal_count; (void)gates;
  memset(out, 0, sizeof(*out));
}

bool DoorExplore_Reached(const DoorExploreResult *r, uint16 region) {
  return ((r->visited_blue[region >> 3] | r->visited_orange[region >> 3]) >> (region & 7)) & 1;
}

bool DoorExplore_EvalRule(uint16 rule_off, const DoorExploreResult *r,
                          const DoorExploreGates *gates) {
  (void)rule_off; (void)r; (void)gates;
  return false;  // stub: no layout can be installed yet, never queried
}

int DoorShuffle_SelfTest(void) {
  fprintf(stderr, "door-selftest: generation not implemented yet (Stage 2 stub)\n");
  return 1;
}
