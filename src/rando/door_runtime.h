// door_runtime.h — door-shuffle runtime redirect layer (randomizer-door-runtime).
//
// A per-seed sparse override of dungeon door->door connections, consulted at
// the four Dungeon_StartInterRoomTrans_* supertile-boundary branches and the
// spiral-staircase destination read in Dungeon_DetectStaircase. Identity
// (every link kDoorRt_NoOverride, or the kFeatures1_DoorShuffleActive flag
// clear) reproduces vanilla behavior; the resolver still runs under the flag
// so the flag-ON all-NO_OVERRIDE side-by-side RAM-compare exercises the hook
// machinery (the override arm itself diverges from the ROM by definition and
// is playtest-verified).
#pragma once
#include "../types.h"
#include "door_tables.gen.h"

#define kDoorRt_NoOverride 0xFFFF

// Layout install/teardown (called from rando.c slot activate/deactivate and
// from generation). Reset clears every link and deactivates.
void DoorRt_Reset(void);
void DoorRt_SetLink(uint16 door_a, uint16 door_b);  // one directed half-link
void DoorRt_Activate(void);
bool DoorRt_Active(void);
uint16 DoorRt_GetLink(uint16 door_id);  // kDoorRt_NoOverride when unshuffled

// dungeon.c hook: normal edge doors. Returns true when an override fired —
// the destination room index, Link/camera/bounds arrival, quadrant and layer
// have been fully applied, and the caller MUST skip both the vanilla
// positional room-index line and the room_transitioning_flags layer/palace
// toggles (the dest door record is the layer authority; basic shuffle never
// crosses a palace boundary). Returns false to take the vanilla path
// (feature off, room not cataloged, exiting door unshuffled).
bool Rando_DoorTransOverride(uint8 dir /* kDoorTblDir_* */);

// Companion to Rando_DoorTransOverride: true (and self-clears) iff the
// immediately preceding override fired — the caller skips the vanilla
// room_transitioning_flags layer/palace toggles in that case.
bool Rando_DoorTransConsumedToggles(void);

// Spiral-context gate (design §2d): Dungeon_DetectStaircase calls
// Dungeon_StartInterRoomTrans_Up/Down for the slide animation; the normal
// edge-door hook must be a no-op there (the header-dest override is the sole
// spiral authority).
void Rando_DoorStaircaseContext(bool entering);

// dungeon.c hook: spiral staircases. Returns the destination room byte
// (vanilla_byte when unshuffled/off). `attr` is the staircase tile attribute
// (0x38/0x39 spirals only — straight/water stair attrs must not redirect).
// When a redirect is chosen, a pending intra-room fixup is armed and applied
// by Rando_DoorSpiralFixup() during Dungeon_InitializeRoomFromSpecial.
uint8 Rando_DoorSpiralDest(uint16 room, uint8 slot, uint8 attr, uint8 vanilla_byte);

// Called from Dungeon_InitializeRoomFromSpecial after the vanilla
// grid-granular Dungeon_AdjustAfterSpiralStairs ran: applies the pending
// intra-room slot delta between the source and destination spiral doors
// (vanilla pairs share their intra-room position; shuffled ones need not).
void Rando_DoorSpiralFixup(void);
