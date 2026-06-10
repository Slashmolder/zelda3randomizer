// door_runtime.c — door-shuffle runtime redirect layer. See door_runtime.h.
//
// Geometry facts (derived from dungeon.c's kDoorPositionToTilemapOffs_* with
// XY(x,y)=y*64+x over 8px tiles; cross-checked against the RoomDraw_Door_*
// consumers):
//   * A supertile is 512x512 px. Outer-edge doors sit at three slots per
//     edge, 128px apart: North/South door columns start at x = {112,240,368}
//     (3 tiles wide, centers {124,252,380}); West/East door rows start at
//     y = {120,248,376} (4 tiles tall, centers {136,264,392}).
//   * The door-list position byte's upper nibble identifies the slot:
//     North/West outer slots are nibbles 0..2; South/East outer slots are
//     nibbles 6..8. Higher nibbles are interior-wall variants, which never
//     produce an inter-room transition.
#include "door_runtime.h"
#include "rando.h"

#include "../variables.h"
#include "../features.h"
#include "../dungeon.h"  // Dungeon_AdjustQuadrant

#include <string.h>

static uint16 g_door_link[kDoorTbl_DoorCount];
static bool g_door_rt_active;
static bool g_door_rt_index_built;
static bool g_door_staircase_ctx;       // inside Dungeon_DetectStaircase's spiral path
static bool g_door_toggles_overridden;  // latch: last Trans override consumed the toggles
static uint16 g_door_spiral_pending;    // dest door id for the pending spiral fixup
static uint16 g_door_spiral_source;     // source door id of the pending spiral

// Per-room catalog index: doors sorted by room; g_room_first[room] = first
// slot in g_door_by_room, -1 = no doors. Built once (the catalog is const).
static int16 g_room_first[256];
static uint16 g_door_by_room[kDoorTbl_DoorCount];

void DoorRt_Reset(void) {
  memset(g_door_link, 0xFF, sizeof(g_door_link));
  g_door_rt_active = false;
  g_door_staircase_ctx = false;
  g_door_toggles_overridden = false;
  g_door_spiral_pending = 0xFFFF;
  g_door_spiral_source = 0xFFFF;
}

void DoorRt_SetLink(uint16 door_a, uint16 door_b) {
  if (door_a < kDoorTbl_DoorCount && door_b < kDoorTbl_DoorCount)
    g_door_link[door_a] = door_b;
}

uint16 DoorRt_GetLink(uint16 door_id) {
  return door_id < kDoorTbl_DoorCount ? g_door_link[door_id] : kDoorRt_NoOverride;
}

static void DoorRt_BuildRoomIndex(void) {
  // counting sort by room byte
  int counts[257] = { 0 };
  for (int i = 0; i < kDoorTbl_DoorCount; i++) {
    uint8 room = kDoorTblDoors[i].room;
    counts[room + 1]++;
  }
  for (int i = 0; i < 256; i++)
    counts[i + 1] += counts[i];
  for (int i = 0; i < 256; i++)
    g_room_first[i] = (int16)counts[i];
  int cursor[256];
  for (int i = 0; i < 256; i++)
    cursor[i] = counts[i];
  for (int i = 0; i < kDoorTbl_DoorCount; i++)
    g_door_by_room[cursor[kDoorTblDoors[i].room]++] = (uint16)i;
  g_door_rt_index_built = true;
}

void DoorRt_Activate(void) {
  if (!g_door_rt_index_built)
    DoorRt_BuildRoomIndex();
  g_door_rt_active = true;
}

bool DoorRt_Active(void) {
  return g_door_rt_active && (enhanced_features1 & kFeatures1_DoorShuffleActive) != 0;
}

// ---------------------------------------------------------------------------
// Exit-door resolution
// ---------------------------------------------------------------------------

// Outer-edge slot index from the door-list position byte, or -1 when the
// entry is an interior-wall variant / wrong edge for `dir`.
static int DoorRt_OuterSlot(uint8 dir, uint8 pos_byte) {
  int nib = pos_byte >> 4;
  switch (dir) {
  case kDoorTblDir_North:
  case kDoorTblDir_West:
    return (nib <= 2) ? nib : -1;
  case kDoorTblDir_South:
  case kDoorTblDir_East:
    return (nib >= 6 && nib <= 8) ? nib - 6 : -1;
  }
  return -1;
}

// Perpendicular pixel center of an outer-edge door slot, within the supertile.
static int DoorRt_SlotPerpCenter(uint8 dir, int slot) {
  if (dir == kDoorTblDir_North || dir == kDoorTblDir_South)
    return 124 + slot * 128;  // door columns 112/240/368, 3 tiles wide
  return 136 + slot * 128;    // door rows 120/248/376, 4 tiles tall
}

// Find the catalog door Link is exiting through: same room, direction `dir`,
// Normal type (positional transitions only — vanilla-teleport-flagged doors
// exit through the 0x89 branch), matching layer, nearest outer slot to Link's
// perpendicular coordinate.
static int DoorRt_ResolveExit(uint8 dir) {
  uint16 room = dungeon_room_index;
  if (room >= 256 || g_room_first[room] < 0)
    return -1;
  int first = g_room_first[room];
  int end = (room < 255) ? g_room_first[room + 1] : kDoorTbl_DoorCount;
  // Link's perpendicular coordinate within the supertile.
  int perp;
  if (dir == kDoorTblDir_North || dir == kDoorTblDir_South)
    perp = (link_x_coord + 8) & 0x1FF;  // sprite center
  else
    perp = (link_y_coord + 12) & 0x1FF;
  int best = -1, best_d = 0x7FFFFFFF;
  for (int i = first; i < end; i++) {
    const DoorTblDoor *d = &kDoorTblDoors[g_door_by_room[i]];
    if (d->type != kDoorTblType_Normal || d->direction != dir)
      continue;
    if (d->flags & kDoorTblFlag_VanillaTeleport)
      continue;
    if (d->layer != (link_is_on_lower_level ? 1 : 0))
      continue;
    int slot = DoorRt_OuterSlot(dir, d->pos_byte);
    if (slot < 0)
      continue;
    int dd = perp - DoorRt_SlotPerpCenter(dir, slot);
    if (dd < 0)
      dd = -dd;
    if (dd < best_d) {
      best_d = dd;
      best = g_door_by_room[i];
    }
  }
  // A door slot is 128px from its neighbor; if Link is further than ~80px
  // from the nearest cataloged slot he is not exiting through a cataloged
  // door (open edge, blown-up wall, ...) — take the vanilla path.
  return (best >= 0 && best_d <= 80) ? best : -1;
}

// ---------------------------------------------------------------------------
// Arrival — generalized Dungeon_AdjustForTeleportDoors (both axes + slot
// correction + layer + quadrant). The transition machinery (camera targets +
// bounds Add/Sub at the Trans_* entry) is re-based by absolute-target
// arithmetic, exactly like the vanilla teleport-door routine.
// ---------------------------------------------------------------------------

static void DoorRt_Arrive(const DoorTblDoor *dst) {
  uint16 D = dst->room;
  // The virtual neighbor Link "comes from": just outside dst's arrival edge.
  // Mirrors Dungeon_AdjustForTeleportDoors's (room∓1, flag) convention; the
  // routine sets dungeon_room_index2/_prev to that virtual room so the next
  // room load resyncs them (Dungeon_LoadRoom does BYTE(index2)=BYTE(index)).
  int slot = DoorRt_OuterSlot(dst->direction, dst->pos_byte);
  if (slot < 0)
    slot = 1;  // defensive: arrival door should always be an outer slot

  dungeon_room_index = D;

  int dx_hi, dy_hi;        // grid-granular target (256px units), vanilla-style
  uint16 virtual_room = D;
  switch (dst->direction) {
  case kDoorTblDir_West:   // arriving through dst's west door, moving east
    dx_hi = ((D & 0xf) << 1) - 1;
    dy_hi = -1;            // perpendicular: computed from slot below
    virtual_room = D - 1;
    break;
  case kDoorTblDir_East:
    dx_hi = ((D & 0xf) << 1) + 2;
    dy_hi = -1;
    virtual_room = D + 1;
    break;
  case kDoorTblDir_North:  // arriving through dst's north door, moving south
    dy_hi = ((D & 0xf0) >> 3) - 1;
    dx_hi = -1;
    virtual_room = D - 0x10;
    break;
  case kDoorTblDir_South:
    dy_hi = ((D & 0xf0) >> 3) + 2;
    dx_hi = -1;
    virtual_room = D + 0x10;
    break;
  default:
    return;
  }
  dungeon_room_index2 = virtual_room;
  dungeon_room_index_prev = virtual_room;

  bool horizontal = (dst->direction == kDoorTblDir_West || dst->direction == kDoorTblDir_East);

  // Scroll axis: move to the just-outside-the-edge 256px unit, keeping the
  // low byte (the door-mouth offset along the walk direction) — vanilla
  // teleport behavior.
  if (horizontal) {
    int xx = dx_hi - (link_x_coord >> 8);
    link_x_coord += xx << 8;
    BG2HOFS_copy2 += xx << 8;
    room_bounds_x.a1 += xx << 8;
    room_bounds_x.b1 += xx << 8;
    room_bounds_x.a0 += xx << 8;
    room_bounds_x.b0 += xx << 8;
  } else {
    int yy = dy_hi - (link_y_coord >> 8);
    link_y_coord += yy << 8;
    BG2VOFS_copy2 += yy << 8;
    room_bounds_y.a1 += yy << 8;
    room_bounds_y.b1 += yy << 8;
    room_bounds_y.a0 += yy << 8;
    room_bounds_y.b0 += yy << 8;
  }

  // Perpendicular axis: grid-granular re-base to D's row/column (vanilla
  // style — bounds shift with the grid delta), then a fine slot delta on the
  // coordinate + camera only (bounds are room-granular and stay put).
  if (horizontal) {
    int target_hi = (D & 0xf0) >> 3;  // D's top row, 256px units
    int yy = target_hi - (link_y_coord >> 8);
    link_y_coord += yy << 8;
    BG2VOFS_copy2 += yy << 8;
    room_bounds_y.a1 += yy << 8;
    room_bounds_y.b1 += yy << 8;
    room_bounds_y.a0 += yy << 8;
    room_bounds_y.b0 += yy << 8;
    // fine: Link's y within the supertile -> the dst door row
    int target_in_room = 128 + slot * 128;  // door rows 120..152; Link y anchor
    int cur_in_room = link_y_coord & 0x1FF;
    int fine = target_in_room - cur_in_room;
    link_y_coord += fine;
    BG2VOFS_copy2 += fine;
    link_quadrant_y = (target_in_room >= 256) ? 2 : 0;
  } else {
    int target_hi = (D & 0xf) << 1;
    int xx = target_hi - (link_x_coord >> 8);
    link_x_coord += xx << 8;
    BG2HOFS_copy2 += xx << 8;
    room_bounds_x.a1 += xx << 8;
    room_bounds_x.b1 += xx << 8;
    room_bounds_x.a0 += xx << 8;
    room_bounds_x.b0 += xx << 8;
    int target_in_room = 116 + slot * 128;  // door columns 112..136; Link x anchor
    int cur_in_room = link_x_coord & 0x1FF;
    int fine = target_in_room - cur_in_room;
    link_x_coord += fine;
    BG2HOFS_copy2 += fine;
    link_quadrant_x = (target_in_room >= 256) ? 1 : 0;
  }

  // Layer authority: the destination door record (the vanilla layer/palace
  // toggles encode the POSITIONAL partner's relationship and are skipped by
  // the caller when an override fires). Basic shuffle never crosses a palace
  // boundary, so cur_palace_index_x2 stays untouched.
  link_is_on_lower_level = dst->layer;
  link_is_on_lower_level_mirror = dst->layer;

  Dungeon_AdjustQuadrant();

  for (int i = 0; i < 20; i++)
    tagalong_y_hi[i] = link_y_coord >> 8;
}

// ---------------------------------------------------------------------------
// dungeon.c hooks
// ---------------------------------------------------------------------------

bool Rando_DoorTransOverride(uint8 dir) {
  g_door_toggles_overridden = false;
  if (!DoorRt_Active() || g_door_staircase_ctx)
    return false;
  int exit_id = DoorRt_ResolveExit(dir);
  if (exit_id < 0)
    return false;
  uint16 dest_id = g_door_link[exit_id];
  if (dest_id == kDoorRt_NoOverride || dest_id >= kDoorTbl_DoorCount)
    return false;
  DoorRt_Arrive(&kDoorTblDoors[dest_id]);
  g_door_toggles_overridden = true;
  return true;
}

bool Rando_DoorTransConsumedToggles(void) {
  bool v = g_door_toggles_overridden;
  g_door_toggles_overridden = false;
  return v;
}

void Rando_DoorStaircaseContext(bool entering) {
  g_door_staircase_ctx = entering;
}

uint8 Rando_DoorSpiralDest(uint16 room, uint8 slot, uint8 attr, uint8 vanilla_byte) {
  g_door_spiral_pending = 0xFFFF;
  g_door_spiral_source = 0xFFFF;
  if (!DoorRt_Active())
    return vanilla_byte;
  if (attr != 0x38 && attr != 0x39)
    return vanilla_byte;  // straight/water stairs are not shuffled (intensity 1)
  if (room >= 256 || g_room_first[room] < 0)
    return vanilla_byte;
  int first = g_room_first[room];
  int end = (room < 255) ? g_room_first[room + 1] : kDoorTbl_DoorCount;
  for (int i = first; i < end; i++) {
    const DoorTblDoor *d = &kDoorTblDoors[g_door_by_room[i]];
    if (d->type != kDoorTblType_SpiralStairs || d->door_index != slot)
      continue;
    uint16 dest_id = g_door_link[g_door_by_room[i]];
    if (dest_id == kDoorRt_NoOverride || dest_id >= kDoorTbl_DoorCount)
      return vanilla_byte;
    g_door_spiral_source = g_door_by_room[i];
    g_door_spiral_pending = dest_id;
    return kDoorTblDoors[dest_id].room;
  }
  return vanilla_byte;
}

void Rando_DoorSpiralFixup(void) {
  if (g_door_spiral_pending == 0xFFFF)
    return;
  const DoorTblDoor *dst = &kDoorTblDoors[g_door_spiral_pending];
  g_door_spiral_pending = 0xFFFF;
  g_door_spiral_source = 0xFFFF;

  // The vanilla flow already applied the grid-granular move
  // (Dungeon_AdjustAfterSpiralStairs with prev = the SOURCE room), keeping
  // Link's intra-room offset. Vanilla spiral pairs share that offset;
  // shuffled ones need not — locate the destination staircase tile in the
  // freshly loaded room (attr2 marker 0x30|slot under the staircase head
  // attr) and apply the intra-room delta to Link + camera.
  int want = 0x30 | (dst->door_index & 3);
  int found_pos = -1;
  for (int pos = 0; pos < 0x1000; pos++) {
    uint8 at = dung_bg2_attr_table[pos];
    if (at != 0x38 && at != 0x39)
      continue;
    if (dung_bg2_attr_table[pos + 0x40] == want) {  // attr2 row below the head
      found_pos = pos;
      break;
    }
  }
  if (found_pos < 0)
    return;  // defensive: leave vanilla offset (playtest-visible, not fatal)
  int tx = (found_pos & 0x3f) << 3;        // x within supertile, px
  int ty = ((found_pos >> 6) & 0x3f) << 3; // y within supertile, px
  int cur_x = link_x_coord & 0x1FF, cur_y = link_y_coord & 0x1FF;
  int dxp = tx - cur_x, dyp = ty - cur_y;
  link_x_coord += dxp;
  link_y_coord += dyp;
  BG2HOFS_copy2 += dxp;
  BG2VOFS_copy2 += dyp;
  link_quadrant_x = ((link_x_coord & 0x1FF) >= 256) ? 1 : 0;
  link_quadrant_y = ((link_y_coord & 0x1FF) >= 256) ? 2 : 0;
  link_is_on_lower_level = dst->layer;
  link_is_on_lower_level_mirror = dst->layer;
  Dungeon_AdjustQuadrant();
  for (int i = 0; i < 20; i++)
    tagalong_y_hi[i] = link_y_coord >> 8;
}
