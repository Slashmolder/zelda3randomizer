#include "ow_map_prizes.h"

#include <stdio.h>
#include <stdlib.h>

#include "dungeon_ids.h"
#include "rando.h"
#include "rando_placement.h"
#include "rando_shuffles.h"
#include "../features.h"
#include "../variables.h"

// Vanilla pause-map marker data, owned by messaging.c. Shared rather than
// copied so the re-keyed path and its oracle cannot drift from what the engine
// actually draws (the kBirdTravel_* pattern).
//   kOwMapCrystal_tab[slot][k] — icon word `ch << 8 | flags` for marker slot
//     `slot` in map-icon state `k`; 0 means vanilla's blinking unknown marker.
//   kOverworldMapData[spr - 8] — the crystal-NUMBER glyph baked into each OAM
//     slot (marker slot `i` draws as sprite `14 - i`).
extern const uint16 *const kOwMapCrystal_tab[7];
extern const uint8 kOverworldMapData[7];

enum { kOwMapMarkerSlots = 7, kPrizeCount = 10 };

// Map-icon states whose markers point at prize dungeons. Everything else the
// pause map draws (Hyrule Castle, Sahasrahla, the Master Sword pedestal,
// Ganon's Tower) is routing, carries no prize claim, and stays vanilla.
enum {
  kOwMapState_Pendants = 3,   // the three light-world pendant dungeons
  kOwMapState_FirstDark = 6,  // "go to the first dark-world dungeon" (PoD)
  kOwMapState_Crystals = 7,   // the seven dark-world crystal dungeons
};

// Slot -> dungeon. Derived from kPendantBitMask / kCrystalBitMask (messaging.c)
// cross-referenced against kDungeonCrystalPendantBit (zelda_rtl.c), and
// independently confirmed against the upstream ALTTPR per-dungeon marker tables
// (z3randomizer tables.asm WorldMapIcon_pos*), whose coordinates are
// byte-identical to ours in this order. See the change's design.md.
static const uint8 kPendantSlotDungeon[3] = {
  kRandoDungeon_EasternPalace,   // slot 0, pendant bit 0x04
  kRandoDungeon_TowerOfHera,     // slot 1, pendant bit 0x01
  kRandoDungeon_DesertPalace,    // slot 2, pendant bit 0x02
};
static const uint8 kCrystalSlotDungeon[7] = {
  kRandoDungeon_PalaceOfDarkness,  // slot 0, crystal bit 0x02
  kRandoDungeon_SkullWoods,        // slot 1, crystal bit 0x40
  kRandoDungeon_TurtleRock,        // slot 2, crystal bit 0x08
  kRandoDungeon_ThievesTown,       // slot 3, crystal bit 0x20
  kRandoDungeon_MiseryMire,        // slot 4, crystal bit 0x01
  kRandoDungeon_IcePalace,         // slot 5, crystal bit 0x04
  kRandoDungeon_SwampPalace,       // slot 6, crystal bit 0x10
};

// Prize id -> marker icon word. Read off the vanilla tables under the identity
// assignment: Green is Eastern Palace (slot 0 @ state 3), the registry's "Red"
// is Desert (slot 2 @ state 3), the registry's "Blue" is Tower of Hera (slot 1
// @ state 3) — the registry names are swapped vs the drawn colour, so this is
// keyed by ID, never by colour word (see kPrize_* in rando_shuffles.h). Every
// crystal shares one icon and differs only in the number glyph below.
// RandoOwMap_SelfCheck re-proves all of this against the vanilla arrays.
static const uint16 kPrizeTab[kPrizeCount] = {
  0x6038,  // Green pendant
  0x6034,  // Red pendant (draws blue)
  0x6032,  // Blue pendant (draws red)
  0x6434, 0x6434, 0x6434, 0x6434, 0x6434, 0x6434, 0x6434,  // Crystal 1..7
};

// Crystal 1..7 -> number glyph. Vanilla blinks these out of kOverworldMapData
// indexed by OAM slot, which prize_shuffle invalidates. Cross-checked against
// kBirdTravel_tab1[0..6] (the flute map's independent 1..8 digit glyphs).
static const uint8 kCrystalNumChar[7] = {0x7f, 0x79, 0x6c, 0x6d, 0x6e, 0x6f, 0x7c};

// Which dungeon marker slot `slot` points at in map-icon state `k`, or
// kRandoDungeon_None when the slot carries no prize claim.
static uint8 PrizeDungeonForSlot(uint8 k, uint8 slot) {
  if (slot >= kOwMapMarkerSlots)
    return kRandoDungeon_None;
  switch (k) {
    // Slot 3 in this state is the Master Sword pedestal, not a prize.
    case kOwMapState_Pendants:
      return slot < 3 ? kPendantSlotDungeon[slot] : (uint8)kRandoDungeon_None;
    case kOwMapState_FirstDark:
      return slot == 0 ? (uint8)kRandoDungeon_PalaceOfDarkness
                       : (uint8)kRandoDungeon_None;
    case kOwMapState_Crystals:
      return kCrystalSlotDungeon[slot];
    default:
      return kRandoDungeon_None;
  }
}

bool RandoOwMap_PrizeMarker(uint8 k, uint8 slot, uint16 *tab, uint8 *num_char) {
  if (!(enhanced_features1 & kFeatures1_RandomizerActive))
    return false;
  const RandoSettings *rs = Rando_GetActiveSettings();
  // Fail CLOSED when the active slot's settings can't be recovered (snapshot
  // replay / pre-settings slot): treat the seed as shuffled so the marker
  // degrades to the unknown form rather than asserting the vanilla prize.
  // `rs && rs->prize_shuffle` would render the vanilla claim when unknown.
  if (rs != NULL && rs->prize_shuffle == 0)
    return false;  // identity assignment: the vanilla art is already true
  uint8 dungeon = PrizeDungeonForSlot(k, slot);
  if (dungeon == kRandoDungeon_None)
    return false;

  // Unknown until observed. Note this holds even when the roll happens to
  // reproduce the vanilla assignment — the player cannot know that.
  *tab = 0;
  *num_char = 0;

  uint16 prize_loc = Rando_GetDungeonPrizeLocation(dungeon);
  if (prize_loc == 0xFFFF || !Rando_IsLocationChecked(prize_loc))
    return true;

  // Observed: the player collected THIS dungeon's prize, so naming it reveals
  // nothing they don't already hold. Allowlisted read (this file only) — see
  // assets/scripts/check_knowledge_consumers.py.
  const uint8 *assign = Rando_GetDungeonPrizeAssignment();
  if (assign == NULL)
    return true;
  uint8 prize = assign[dungeon];
  if (prize >= kPrizeCount)
    return true;  // unknown prize id: stay unknown rather than draw garbage
  *tab = kPrizeTab[prize];
  if (prize >= kPrize_Crystal1)
    *num_char = kCrystalNumChar[prize - kPrize_Crystal1];
  return true;
}

// ---------------------------------------------------------------------------
// Self-check
// ---------------------------------------------------------------------------
static void selfcheck_die(const char *msg) {
  fprintf(stderr, "[RandoOwMap_SelfCheck] FAIL: %s\n", msg);
  exit(2);
}

void RandoOwMap_SelfCheck(void) {
  // The identity assignment is what vanilla's baked marker art depicts, so
  // resolving it through the re-keyed path must reproduce that art exactly.
  // This pins the slot->dungeon table AND the prize->icon tables at once: get
  // either wrong and some slot's icon or crystal number stops matching.
  // PrizeShuffle_Run(NULL, ...) takes the identity branch without drawing RNG.
  uint8 identity[kRandoDungeonCount];
  PrizeShuffle_Run(NULL, NULL, identity);

  static const uint8 kPrizeStates[3] = {
    kOwMapState_Pendants, kOwMapState_FirstDark, kOwMapState_Crystals,
  };
  int prize_slots = 0;
  for (int si = 0; si < 3; si++) {
    uint8 k = kPrizeStates[si];
    for (uint8 slot = 0; slot < kOwMapMarkerSlots; slot++) {
      uint8 dungeon = PrizeDungeonForSlot(k, slot);
      if (dungeon == kRandoDungeon_None)
        continue;
      prize_slots++;
      uint8 prize = identity[dungeon];
      if (prize >= kPrizeCount)
        selfcheck_die("prize marker slot maps to a dungeon with no vanilla prize");
      if (kPrizeTab[prize] != kOwMapCrystal_tab[slot][k])
        selfcheck_die("prize icon word disagrees with the vanilla marker table");
      if (prize >= kPrize_Crystal1) {
        // Marker slot `slot` draws as sprite `14 - slot`, and the vanilla glyph
        // lives at kOverworldMapData[sprite - 8].
        if (kCrystalNumChar[prize - kPrize_Crystal1] != kOverworldMapData[6 - slot])
          selfcheck_die("crystal number glyph disagrees with the vanilla table");
      } else if (kPrizeTab[prize] >> 8 == 100) {
        selfcheck_die("pendant resolved to the crystal icon");
      }
    }
  }
  // 3 pendant dungeons + the first-dark-world marker + 7 crystal dungeons.
  if (prize_slots != 11)
    selfcheck_die("unexpected prize marker slot count");

  // Routing markers must stay vanilla: the pedestal shares state 3 with the
  // pendant dungeons, and states 0/1/2/4/5/8 carry no prize claim at all.
  if (PrizeDungeonForSlot(kOwMapState_Pendants, 3) != kRandoDungeon_None)
    selfcheck_die("Master Sword pedestal slot must not be a prize marker");
  static const uint8 kRoutingStates[6] = {0, 1, 2, 4, 5, 8};
  for (int si = 0; si < 6; si++) {
    for (uint8 slot = 0; slot < kOwMapMarkerSlots; slot++) {
      if (PrizeDungeonForSlot(kRoutingStates[si], slot) != kRandoDungeon_None)
        selfcheck_die("routing map-icon state must not carry a prize marker");
    }
  }

  // Every prize dungeon owns exactly one marker slot across the prize states,
  // so no dungeon can be silently dropped or double-drawn. PoD legitimately
  // appears twice (states 6 and 7) — one marker each, never both at once,
  // because the state IS the selector.
  for (uint8 d = 0; d < kRandoDungeonCount; d++) {
    uint16 loc = Rando_GetDungeonPrizeLocation(d);
    if (loc == 0xFFFF)
      continue;  // no prize location: HCE / HCT / GT
    int seen = 0;
    for (uint8 slot = 0; slot < kOwMapMarkerSlots; slot++) {
      if (PrizeDungeonForSlot(kOwMapState_Pendants, slot) == d) seen++;
      if (PrizeDungeonForSlot(kOwMapState_Crystals, slot) == d) seen++;
    }
    if (seen != 1)
      selfcheck_die("each prize dungeon needs exactly one pause-map marker slot");
  }

  fprintf(stderr, "[RandoOwMap_SelfCheck] OK\n");
}
