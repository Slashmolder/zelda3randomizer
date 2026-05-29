// rando.c — randomizer master module (tasks.md §1.1, §1.1a). Phase A0 stub.
//
// Phase A0 deliverables landed here:
//   - g_assets_hash[32] global (computed by main.c::LoadAssets via sha256_buffer)
//   - Rando_OnLocationCheck() stub — currently pass-through (returns vanilla_item_id);
//     real placement-table dispatch lands with task 6.1
//   - Rando_BumpReachabilityCounter() stub — no-op; activates with task 3.8's
//     Logic_ComputeReachability memoization
//   - Optional SHA-256 self-test (RANDO_SELFCHECK build flag)

#include "rando.h"
#include <string.h>  // memcpy — used by Rando_ActivateSidecarSlot below
#include "rando_rng.h"
#include "rando_share.h"
#include "rando_settings.h"
#include "rando_logic.h"
#include "rando_placement.h"
#include "rando_shuffles.h"
#include "rando_save.h"
#include "rando_snapshot_tail.h"
#include "rando_textfield.h"
#include "item_ids.h"
#include "location_ids.h"
#include "chest_lookup.h"  // (room, ordinal) -> LOC_*; §6.3 codegen
#include "direct_grant_icons.h"  // kDirectGrantIcons[] (Phase B Slice 9)
#include "rando_hints.h"  // Rando_ClearHints (Phase B Slice 5)
#include "../ancilla.h"  // AncillaAdd_RandoIconReceipt (Phase B Slice 9)
#include "../types.h"
#include "../variables.h"  // §6.2 progressive-dispatch reads link_sword_type etc.
#include "../features.h"   // g_rando_triforce_piece_count
#include "../misc.h"       // §7.6 Link_CalculateSfxPan
#include "../hud.h"        // §7.6 Hud_RefreshIcon
#include "../player.h"     // §7.6 Link_ReceiveItem
#include "third_party/sha256/sha256.h"

// ---------------------------------------------------------------------------
// g_assets_hash — populated by LoadAssets() in src/main.c after the asset
// blob is read and validated. See task 1.1a.
// ---------------------------------------------------------------------------
uint8 g_assets_hash[32];

// ---------------------------------------------------------------------------
// Reachability state counter — bumped by Rando_BumpReachabilityCounter()
// when a story-progress event flag changes. The tracker overlay queries this
// to know when to invalidate its memoized Logic_ComputeReachability cache.
// (Heap-resident per design — NOT in g_ram. See proposal.md "Impact".)
// ---------------------------------------------------------------------------
static uint32 g_reachability_state_counter;

// ---------------------------------------------------------------------------
// Rando_OnLocationCheck — universal dispatcher (tasks.md §6.1).
// Phase A0 stub: pass-through. Phase A1 wires the placement_table lookup.
// ---------------------------------------------------------------------------
uint16 Rando_OnLocationCheck(uint16 location_id, uint16 vanilla_item_id) {
  // §8.8a ordering-invariant tripwire: every call increments a global
  // counter that StateRecorder_Load snapshots before LoadSnesState and
  // asserts didn't change before the TLV reinstall. If a frame ran between
  // those two steps, dispatch would fire here and bump the counter, making
  // the load-time assertion trip.
  g_rando_oncheck_call_count++;

  // Phase B Slice 1 — flag this location as checked. The bitmap is the
  // session source-of-truth; sidecar write copies it back to the slot.
  // Set BEFORE dispatch so a crash inside dispatch still records the
  // in-flight intent (tracker shows the location as checked on reload).
  Rando_MarkLocationChecked(location_id);

  // Placement_Lookup returns vanilla_item_id when no active placement table
  // is installed (rando mode inactive), or when location_id is not in the
  // active table. See rando_placement.c.
  uint16 placed = Placement_Lookup(location_id, vanilla_item_id);

  // Track Mushroom possession the moment the player obtains it, wherever it
  // is placed. link_item_mushroom can't represent "have Mushroom AND Powder"
  // (one byte, mutually exclusive values), so the Witch trade keys off this
  // flag instead — see Rando_MushroomHeld / Witch_AcceptShroom. Guarded on
  // an active slot so a vanilla Mushroom pickup never sets it.
  if (placed == ITEM_Mushroom && g_rando_slot_active)
    g_rando_mushroom_held = 1;

  return placed;
}

// §6.2 partial: progressive items don't have a vanilla LttP code (the
// game's Link_ReceiveItem dispatch table grants absolute tiers, not
// "advance to next tier"). Translate progressive items to the next-tier
// absolute LttP code at dispatch time. Each call advances by one tier;
// the placer's bounded-retry already accounts for the cumulative effect
// across multiple ProgressiveSword placements.
//
// Returns 0xFF if `registry_id` isn't a progressive item OR is at max tier
// (the placer caps pool counts via item-pool-difficulty, so we shouldn't
// hit this in practice — but if we do, fall back to junk via 0xFF).
static uint8 progressive_to_lttp(uint16 registry_id) {
  switch (registry_id) {
    case ITEM_ProgressiveSword: {
      uint8 tier = link_sword_type;
      if (tier >= 4) return 0xFF;
      return tier;  // LttP codes 0x00..0x03 = L1..L4 sword
    }
    case ITEM_ProgressiveShield: {
      uint8 tier = link_shield_type;
      if (tier >= 3) return 0xFF;
      return (uint8)(0x04 + tier);  // LttP codes 0x04..0x06 = Fighter/Red/Mirror
    }
    case ITEM_ProgressiveArmor: {
      // link_armor: 0=Green (default), 1=Blue, 2=Red
      uint8 tier = link_armor;
      if (tier >= 2) return 0xFF;
      return (uint8)(0x22 + tier);  // 0x22=BlueMail, 0x23=RedMail
    }
    case ITEM_ProgressiveGlove: {
      uint8 tier = link_item_gloves;
      if (tier >= 2) return 0xFF;
      return (uint8)(0x1b + tier);  // 0x1b=PowerGlove, 0x1c=TitanMitt
    }
    case ITEM_ProgressiveBow: {
      // link_item_bow is NOT a linear tier counter: it encodes bow strength
      // AND arrow state (1=wood/no-arrows, 2=wood/arrows, 3=silver/no-arrows,
      // 4=silver/arrows; hud.c re-derives the low bit from link_num_arrows).
      // So distinguish by strength tier, not raw value.
      uint8 bow = link_item_bow;
      if (bow == 0) return 0x0b;  // first pickup: wooden bow (code 0x0b)
      if (bow < 3)  return 0x3b;  // second: upgrade to silver bow (code 0x3b)
      return 0xFF;                // already have silver bow
    }
    // Multi-tier rupees: vanilla LttP receive codes per Ancilla_AddRupees.
    // kGiveRupeeGift_Tab[5] = {1, 5, 20, 100, 50}:
    //   0x34→1, 0x35→5, 0x36→20, 0x40→100, 0x41→50, 0x46→300
    case ITEM_Rupee1:   return 0x34;
    case ITEM_Rupee5:   return 0x35;
    case ITEM_Rupee20:  return 0x36;
    case ITEM_Rupee100: return 0x40;
    case ITEM_Rupee300: return 0x46;
    // Rupoor: ALTTPR-only item (vanilla LttP doesn't have it). No vanilla
    // LttP code grants Rupoor; §6.2 follow-on. Phase A1 falls back to
    // vanilla item (which is fine — Rupoor only appears in hard/expert
    // pools per the spec, infrequent at most slots).
    // HalfMagic / QuarterMagic: no LttP receive code in this port grants
    // them. kValueToGiveItemTo[32]=-1 means code 0x20's "magic" branch in
    // Link_ReceiveItem runs special palette/cape logic but does NOT write to
    // link_magic_consumption. The vanilla Magic Bat grant bypasses
    // Link_ReceiveItem entirely (direct write). §6.2 work would add a
    // new receive helper that writes link_magic_consumption=1 (Half) or
    // =2 (Quarter); Phase A1 falls back to vanilla item.

    // Bottle-with-contents → LttP codes per misc.c kBottleList[].
    // ItemReceipt_GiveBottledItem finds the first empty slot and writes
    // (kBottleList_index + 2). Mapping is fixed at:
    //   0x16=BottleEmpty, 0x2b=Bee, 0x2c=Fairy, 0x2d=RedPotion,
    //   0x3d=GreenPotion, 0x3c=GoodBee, 0x48=BluePotion
    case ITEM_BottleEmpty:           return 0x16;
    case ITEM_BottleWithFairy:       return 0x2c;
    case ITEM_BottleWithBee:         return 0x2b;
    case ITEM_BottleWithGoodBee:     return 0x3c;
    case ITEM_BottleWithRedPotion:   return 0x2d;
    case ITEM_BottleWithGreenPotion: return 0x3d;
    case ITEM_BottleWithBluePotion:  return 0x48;
    // SilverArrowUpgrade → LttP code 0x3b, which sets link_item_bow=3
    // (silver bow). hud.c re-derives 3→4 when arrows are present. NOTE: code
    // 0x29 (used previously) writes link_item_mushroom, NOT the bow — it
    // grants a Mushroom, so silver arrows were never actually awarded.
    case ITEM_SilverArrowUpgrade: return 0x3b;
    default:
      // Dungeon items (SmallKey 53..65, BigKey 66..76, Map 77..87 + 124,
      // Compass 88..98): for the CURRENT-dungeon vanilla fall-back this
      // returns the dispatcher code (0x24/0x32/0x33/0x25). The per-placed-
      // dungeon direct-write path in Rando_DispatchVanillaGrant supersedes
      // this — see dungeon_item_direct_grant() below. We keep these here as
      // a last-resort fall-back when the direct-write path can't apply
      // (e.g. caller passed a vanilla_lttp_code that bypasses dispatch).
      if (registry_id >= 53 && registry_id <= 65) return 0x24;  // SmallKey
      if (registry_id >= 66 && registry_id <= 76) return 0x32;  // BigKey
      if (registry_id == 124) return 0x33;                       // Map_HCE
      if (registry_id >= 77 && registry_id <= 87) return 0x33;   // Map_*
      if (registry_id >= 88 && registry_id <= 98) return 0x25;   // Compass
      return 0xFF;
  }
}

// §6.2 per-placed-dungeon counter helpers. The vanilla LttP dispatcher
// Link_ReceiveItem indexes by `cur_palace_index_x2 >> 1` (the player's
// current dungeon). For rando placements where a key/map/compass belongs to
// a DIFFERENT dungeon than the player's current one, we have to write to
// that specific dungeon's bit ourselves — and the bit MUST line up with
// the bit the door-check (dungeon.c) reads back.
//
// Returns 1 if the placed item is a dungeon item and was direct-written.
// Caller treats this as "skip Link_ReceiveItem" via kRandoLttpSkip.
//
// Two dungeon-id conventions are in play and they DO NOT agree:
//
//   ALTTPR convention   — HCE=0, EP=1, DP=2, TH=3, HCT=4, PoD=5, SP=6,
//                         SW=7, TT=8, IP=9, MM=10, TR=11, GT=12.
//     Used by ALTTPR item-id ordering (so e.g. registry_id - 66 over the
//     BigKey range gives ALTTPR dungeon - 1) and by rando_placement.c
//     internals (kDungeonPrizeLocations etc.).
//
//   Game-side convention — `cur_palace_index_x2 >> 1`. Derived by
//     cross-referencing kDungeonCrystalPendantBit[13] in zelda_rtl.c
//     against the vanilla dungeon→prize bits:
//       [0]=HCE [1]=(unused/sub-area) [2]=EP [3]=DP [4]=HCT [5]=PoD
//       [6]=SP  [7]=SW  [8]=TT  [9]=IP  [10]=TH  [11]=MM  [12]=TR  [13]=GT
//
// Per Link_ReceiveItem's special case for codes 0x25/0x32/0x33 (misc.c):
//     WORD(*p) |= 0x8000 >> (BYTE(cur_palace_index_x2) >> 1)
// So the bit for dungeon D is `0x8000 >> D` where D is the GAME-side
// index. The tables below translate ALTTPR-id → game-side index so the
// bit we set matches the bit the game's door-check reads.
static uint16 dungeon_bit_for_map_or_compass(uint8 game_dungeon_id) {
  if (game_dungeon_id >= 16) return 0;
  return (uint16)(0x8000u >> game_dungeon_id);
}

// Indexed by ALTTPR registry-id offset (66..76 → 0..10 for BigKey;
// likewise -77 for Map, -88 for Compass over the same 11 dungeons,
// in EP, DP, TH, PoD, SP, SW, TT, IP, MM, TR, GT order). The value is
// the GAME-side dungeon index for that ALTTPR dungeon.
//                                             EP  DP  TH  PoD SP  SW  TT  IP  MM  TR  GT
static const uint8 kBigKeyGameDungeon[11]  = {  2,  3, 10,  5,  6,  7,  8,  9, 11, 12, 13 };
static const uint8 kMapGameDungeon[11]     = {  2,  3, 10,  5,  6,  7,  8,  9, 11, 12, 13 };
static const uint8 kCompassGameDungeon[11] = {  2,  3, 10,  5,  6,  7,  8,  9, 11, 12, 13 };

// SmallKey spans all 13 ALTTPR dungeons (registry-id offset -53: HCE, EP, DP,
// TH, HCT, PoD, SP, SW, TT, IP, MM, TR, GT). Value = GAME-side dungeon index,
// matching the cur_palace_index_x2>>1 ordering the per-dungeon key array
// (link_keys_earned_per_dungeon) is indexed by. Note HCE=0 and HCT=4 are
// present here (BigKey/Map/Compass omit them since vanilla never has those).
//                                            HCE EP  DP  TH HCT PoD SP  SW  TT  IP  MM  TR  GT
static const uint8 kSmallKeyGameDungeon[13] = { 0,  2,  3, 10,  4,  5,  6,  7,  8,  9, 11, 12, 13 };

static uint8 dungeon_id_for_item_local(uint16 registry_id) {
  // SmallKey 53..65 → GAME-side dungeon index (same translation as
  // BigKey/Map/Compass below). Consumed by dungeon_item_direct_grant's
  // per-dungeon key-counter write into link_keys_earned_per_dungeon[].
  if (registry_id >= 53 && registry_id <= 65) return kSmallKeyGameDungeon[registry_id - 53];
  if (registry_id >= 66 && registry_id <= 76) return kBigKeyGameDungeon[registry_id - 66];
  if (registry_id == 124) return 0;  // Map_HCE (game-side index 0)
  if (registry_id >= 77 && registry_id <= 87) return kMapGameDungeon[registry_id - 77];
  if (registry_id >= 88 && registry_id <= 98) return kCompassGameDungeon[registry_id - 88];
  return 0xFF;
}

// Per-placed-dungeon counter direct-grant. Returns 1 on success.
// Handles BigKey/Map/Compass (bitfields) and SmallKey (the per-dungeon counter
// array link_keys_earned_per_dungeon[], with the live link_num_keys counter
// kept in sync when the player is standing in the destination dungeon).
static int dungeon_item_direct_grant(uint16 registry_id) {
  uint8 dungeon = dungeon_id_for_item_local(registry_id);
  // Game-side indices range 0..13 (GT). 16 is the kUpperBitmasks size — past
  // that, dungeon_bit_for_map_or_compass returns 0 and the OR would no-op.
  if (dungeon == 0xFF || dungeon >= 16) return 0;

  uint16 bit = dungeon_bit_for_map_or_compass(dungeon);
  if (registry_id >= 66 && registry_id <= 76) {
    // BigKey for `dungeon`.
    link_bigkey |= bit;
    return 1;
  }
  if (registry_id == 124 || (registry_id >= 77 && registry_id <= 87)) {
    // Map for `dungeon`.
    link_dungeon_map |= bit;
    return 1;
  }
  if (registry_id >= 88 && registry_id <= 98) {
    // Compass for `dungeon`.
    link_compass |= bit;
    return 1;
  }
  if (registry_id >= 53 && registry_id <= 65) {
    // SmallKey for game-side dungeon `dungeon`. Vanilla keeps the live count
    // for the player's CURRENT dungeon in link_num_keys and parks every
    // dungeon's saved count in link_keys_earned_per_dungeon[cur_palace_index_x2
    // >> 1] (Hyrule Castle proper, raw index 2, folds into the Escape slot 0 —
    // see SaveDungeonKeys and the dungeon-entrance restore in dungeon.c). Under
    // key-shuffle a small key can belong to a DIFFERENT dungeon than the one
    // the player is standing in (the reported bug: Tower-of-Hera keys collected
    // during the Hyrule Castle escape were credited to the escape's live
    // counter and were unusable once the player reached ToH). Credit the
    // DESTINATION dungeon's slot, not the live current-dungeon counter.
    uint8 cur = (uint8)cur_palace_index_x2;  // 0xff when not in a dungeon
    uint8 cur_slot = (cur == 0xff) ? 0xff : ((cur == 2) ? 0 : (cur >> 1));
    if (cur != 0xff && dungeon == cur_slot) {
      // Key belongs to the dungeon the player is in: bump the LIVE counter and
      // resync the saved slot (mirrors SaveDungeonKeys). Don't recompute from
      // the slot — door uses decrement link_num_keys without touching the
      // slot, so the slot can be staler than the live count.
      if (link_num_keys < 0xfe) link_num_keys += 1;
      link_keys_earned_per_dungeon[dungeon] = link_num_keys;
    } else {
      // Key belongs to a dungeon the player isn't in right now: credit only the
      // saved slot. It restores into link_num_keys on dungeon entry. (0xfe cap
      // keeps the 0xff "outside dungeon" sentinel out of a counter slot.)
      if (link_keys_earned_per_dungeon[dungeon] < 0xfe)
        link_keys_earned_per_dungeon[dungeon] += 1;
    }
    return 1;
  }
  return 0;
}

// §6.2 prize-item direct-grant. The 7 crystals + 3 pendants OR into
// `link_has_crystals` / `link_which_pendants`. Each prize has a fixed bit
// per vanilla LttP convention (kDungeonCrystalPendantBit[] indexed by
// dungeon — but since prizes can be shuffled to any dungeon, we map by
// PRIZE id, not by dungeon).
//
// Per ALTTPR Prize\Pendant / Prize\Crystal classes and the vanilla bit
// allocations in Link_ReceiveItem's pendant case and AncillaAdd_FallingPrize's
// crystal case:
//   GreenPendant (EP)  → link_which_pendants bit 2 (mask 0x04)
//   RedPendant   (DP)  → link_which_pendants bit 1 (mask 0x02)
//   BluePendant  (ToH) → link_which_pendants bit 0 (mask 0x01)
//   Crystal1 (PoD)  → link_has_crystals bit 4 (mask 0x10)
//   Crystal2 (SP)   → link_has_crystals bit 1 (mask 0x02)
//   Crystal3 (SW)   → link_has_crystals bit 0 (mask 0x01)
//   Crystal4 (TT)   → link_has_crystals bit 6 (mask 0x40)
//   Crystal5 (IP)   → link_has_crystals bit 2 (mask 0x04)
//   Crystal6 (MM)   → link_has_crystals bit 5 (mask 0x20)
//   Crystal7 (TR)   → link_has_crystals bit 3 (mask 0x08)
// Bits derived by cross-referencing kDungeonCrystalPendantBit[13] in
// src/zelda_rtl.c against the vanilla dungeon→prize assignment in
// app/Region/Standard/<Dungeon>.php.
//
// Returns 1 on success.
static int prize_item_direct_grant(uint16 registry_id) {
  switch (registry_id) {
    case ITEM_Prize_GreenPendant: link_which_pendants |= 0x04; return 1;  // EP
    case ITEM_Prize_RedPendant:   link_which_pendants |= 0x02; return 1;  // DP
    case ITEM_Prize_BluePendant:  link_which_pendants |= 0x01; return 1;  // ToH
    case ITEM_Prize_Crystal1: link_has_crystals |= 0x10; return 1;
    case ITEM_Prize_Crystal2: link_has_crystals |= 0x02; return 1;
    case ITEM_Prize_Crystal3: link_has_crystals |= 0x01; return 1;
    case ITEM_Prize_Crystal4: link_has_crystals |= 0x40; return 1;
    case ITEM_Prize_Crystal5: link_has_crystals |= 0x04; return 1;
    case ITEM_Prize_Crystal6: link_has_crystals |= 0x20; return 1;
    case ITEM_Prize_Crystal7: link_has_crystals |= 0x08; return 1;
    default: return 0;
  }
}

// §6.2 magic-upgrade direct-grant. HalfMagic / QuarterMagic bypass
// Link_ReceiveItem in vanilla — the only writer is the Magic Bat handler in
// sprite_main.c writing link_magic_consumption = 1. Rando placements
// of these items at non-Magic-Bat slots need a direct-write here.
// Returns 1 on success.
static int magic_upgrade_direct_grant(uint16 registry_id) {
  if (registry_id == ITEM_HalfMagic) {
    if (link_magic_consumption < 1) link_magic_consumption = 1;
    return 1;
  }
  if (registry_id == ITEM_QuarterMagic) {
    link_magic_consumption = 2;
    return 1;
  }
  return 0;
}

// Phase B Slice 9 — last item id resolved by Rando_DispatchVanillaGrant
// (or its callers via Rando_ChestDispatch). Read by call sites after the
// dispatch returns kRandoLttpSkip so Rando_ShowDirectGrantConfirmation can
// look up the per-item icon in kDirectGrantIcons[]. Single-threaded; the
// dispatcher is the immediately preceding rando call at every direct-grant
// site, so the value is fresh by construction.
static uint16 g_last_dispatched_item_id = 0xFFFFu;

uint16 Rando_LastDispatchedItemId(void) {
  return g_last_dispatched_item_id;
}

uint8 Rando_DispatchVanillaGrant(uint16 location_id,
                                 uint16 vanilla_registry_id,
                                 uint8 vanilla_lttp_code) {
  uint16 placed = Rando_OnLocationCheck(location_id, vanilla_registry_id);
  g_last_dispatched_item_id = placed;
  if (placed == vanilla_registry_id) return vanilla_lttp_code;

  // §6.2 TriforcePiece (no vanilla LttP code). Tick the counter and
  // return kRandoLttpSkip so the caller bypasses Link_ReceiveItem — no
  // accidental double-grant of the slot's vanilla item.
  if (placed == ITEM_TriforcePiece) {
    if (g_rando_triforce_piece_count < 255) g_rando_triforce_piece_count++;
    return kRandoLttpSkip;
  }

  // §6.2 HalfMagic / QuarterMagic direct-write (no vanilla LttP dispatcher
  // path). The Magic Bat handler writes link_magic_consumption directly;
  // rando placements of these at other slots use the same direct-write here.
  if (magic_upgrade_direct_grant(placed)) {
    return kRandoLttpSkip;
  }

  // §6.2 prize-item direct-write (crystals + pendants). The vanilla path in
  // AncillaAdd_FallingPrize ORs the current dungeon's bit into link_has_crystals;
  // for rando placements at non-boss slots we set the prize's bit directly.
  if (prize_item_direct_grant(placed)) {
    return kRandoLttpSkip;
  }

  // §6.2 per-placed-dungeon SmallKey/BigKey/Map/Compass direct-write. Vanilla
  // LttP's dispatcher credits the player's CURRENT dungeon; for rando
  // placements where the placed item belongs to a DIFFERENT dungeon we write
  // that specific dungeon's bit (BigKey/Map/Compass) or counter slot (SmallKey).
  if (dungeon_item_direct_grant(placed)) {
    return kRandoLttpSkip;
  }

  uint8 lttp = Rando_VanillaItemForRegistryId(placed);
  if (lttp != 0xFF) return lttp;

  // §6.2 partial: progressive items translate via current-tier lookup.
  uint8 prog_lttp = progressive_to_lttp(placed);
  if (prog_lttp != 0xFF) return prog_lttp;

  // Placed item has no vanilla LttP dispatch path remaining. Fall back to
  // the vanilla LttP code so the game keeps running with the vanilla grant
  // (detectable in the spoiler).
  return vanilla_lttp_code;
}

// ---------------------------------------------------------------------------
// Chest universal-dispatch lookup table.
//
// (room, ordinal) -> LOC_* via the codegen-emitted kRandoChestLookup table in
// src/rando/chest_lookup.h. Source-of-truth: vanilla chest table
// ($81e96e) cross-referenced with ALTTPR PHP location names (audit.md §0.3.5).
//
// The table is sorted by (room, ordinal) at codegen time so we can binary-
// search. Returns 0xFFFF for any (room, ordinal) pair that isn't in the
// table — at runtime this falls through to vanilla item-grant, leaving rooms
// like the 4 ALTTPR-unexposed small-key/rupee chests unaffected.
// ---------------------------------------------------------------------------
static uint16 chest_lookup(uint16 dungeon_room, uint8 chest_ordinal) {
  // Binary search on packed (room << 8) | ordinal key — both room and
  // ordinal are small enough that a single 32-bit pack is overkill but
  // makes the compare cheap and branch-free against the inline ordering.
  uint32 want = ((uint32)dungeon_room << 8) | chest_ordinal;
  int lo = 0, hi = (int)kRandoChestLookup_COUNT - 1;
  while (lo <= hi) {
    int mid = lo + ((hi - lo) >> 1);
    uint32 got = ((uint32)kRandoChestLookup[mid].room << 8) | kRandoChestLookup[mid].ordinal;
    if (got == want) return kRandoChestLookup[mid].loc_id;
    if (got < want) lo = mid + 1;
    else            hi = mid - 1;
  }
  return 0xFFFFu;  // not mapped — vanilla item grants
}

uint8 Rando_ChestDispatch(uint16 dungeon_room, uint8 chest_ordinal,
                          uint8 vanilla_lttp_code) {
  uint16 loc_id = chest_lookup(dungeon_room, chest_ordinal);
  if (loc_id == 0xFFFFu) return vanilla_lttp_code;  // not mapped — vanilla
  // We don't know the vanilla registry id for unmapped chests; pass 0xFFFF
  // and let Rando_DispatchVanillaGrant's fall-back handle it.
  return Rando_DispatchVanillaGrant(loc_id, 0xFFFFu, vanilla_lttp_code);
}

// === Phase B sprite/shop dispatch: begin ===
// ---------------------------------------------------------------------------
// Retro shop-slot lookup (#53). (room-low-byte, entrance-door) selects one
// of the 9 ALTTPR shops; that shop's three purchasable slots occupy three
// consecutive registry ids (LOC base .. base+2). Capacity-Upgrade slots
// (264/265) are identity-placed and dispatched separately by their own site.
//
// Provenance: ALTTPR shop room_id/door_id pairs from
// `../alttp_vt_randomizer/app/Region/Standard/**/*.php` `new Shop(...)`
// 4th/5th constructor args; LOC bases from
// `assets/rando/location_registry.yaml` ids 237..263. The (room, door)
// disambiguation matches z3randomizer `shopkeeper.asm` SpritePrep_ShopKeeper
// (ShopTable room+door match). Door values equal the vanilla overworld
// entrance ids (kOverworld_Entrance_Id), which is exactly what
// `which_entrance` holds while the player stands in the cave.
//
// NOTE: low-byte room is ambiguous (0x0F backs four DW shops; 0x12 backs DW
// Death-Mountain + LW Lake-Hylia), so for most shops the door (== vanilla
// `which_entrance` / ALTTPR `PreviousOverworldDoor`) disambiguates.
//
// EXCEPTION — room-only-match shops: ALTTPR's shop identifier (z3randomizer
// `shopkeeper.asm`, ShopTable match loop) checks `ShopType & 0x0040`; when set,
// it matches on RoomIndex ALONE and SKIPS the door compare. That bit comes from
// the Shop's `config & 0xFC` (PHP `Shop::getBytes`): the Light World Death
// Mountain Shop is constructed with `config = 0x43` (= 0x03 | 0x40) per
// `../alttp_vt_randomizer/app/Region/Standard/LightWorld/DeathMountain/East.php`,
// so its `door_id = 0x00` is deliberately ignored. Keying that shop on the door
// would silently fail (0x00 also = the "no entrance" reset value), so it is
// flagged room_only and matched on room (0xFF) alone — exactly as the ROM does.
typedef struct { uint8 room; uint8 door; uint16 loc_base; bool room_only; } RandoShopSlot;
static const RandoShopSlot kRandoShopSlots[] = {
  // room  door   base  room_only   shop (LOC ids base..base+2)
  { 0x0F, 0x6F, 237, false },  // Dark World Potion Shop          (237/238/239)
  { 0x10, 0x75, 240, false },  // Dark World Forest Shop          (240/241/242)
  { 0x0F, 0x57, 243, false },  // Dark World Lumberjack Hut       (243/244/245)
  { 0x0F, 0x60, 246, false },  // Dark World Village of Outcasts  (246/247/248)
  { 0x0F, 0x74, 249, false },  // Dark World Lake Hylia Shop      (249/250/251)
  { 0x12, 0x6E, 252, false },  // Dark World Death Mountain Shop  (252/253/254)
  { 0xFF, 0x00, 255, true  },  // Light World Death Mountain Shop (255/256/257) — config 0x43 -> room-only
  { 0x1F, 0x46, 258, false },  // Light World Kakariko Shop       (258/259/260)
  { 0x12, 0x58, 261, false },  // Light World Lake Hylia Shop     (261/262/263)
};

static uint16 shop_lookup(uint8 room, uint8 entrance, uint8 pos) {
  if (pos > 2) return 0xFFFFu;  // shops have exactly 3 slots
  for (uint32 i = 0; i < sizeof(kRandoShopSlots) / sizeof(kRandoShopSlots[0]); i++) {
    if (kRandoShopSlots[i].room != room) continue;  // room must always match
    // Room-only shops match on the room alone (ShopType & 0x40); others also
    // require the door (entrance) to match, since their room is shared.
    if (kRandoShopSlots[i].room_only || kRandoShopSlots[i].door == entrance) {
      return (uint16)(kRandoShopSlots[i].loc_base + pos);
    }
  }
  return 0xFFFFu;  // not a known shop slot — vanilla item grants
}

uint8 Rando_ShopDispatch(uint8 room, uint8 entrance, uint8 pos,
                         uint8 vanilla_lttp_code) {
  uint16 loc_id = shop_lookup(room, entrance, pos);
  if (loc_id == 0xFFFFu) return vanilla_lttp_code;  // not mapped — vanilla
  // Vanilla registry id is not threaded through the shop receipt path; pass
  // 0xFFFF so Rando_DispatchVanillaGrant treats the slot as "always overridden
  // when present in the table" and falls back to the vanilla LttP code when the
  // slot is absent (non-Retro seeds) — identical to the chest-dispatch contract.
  return Rando_DispatchVanillaGrant(loc_id, 0xFFFFu, vanilla_lttp_code);
}
// === Phase B sprite/shop dispatch: end ===

// ---------------------------------------------------------------------------
// Rando_BumpReachabilityCounter — invalidates the tracker's cached
// reachability. Phase A0 stub: increment the counter. The tracker (task 10.2)
// will consume the value.
// ---------------------------------------------------------------------------
void Rando_BumpReachabilityCounter(void) {
  g_reachability_state_counter++;
}

uint32 Rando_GetReachabilityCounter(void) {
  return g_reachability_state_counter;
}

// ---------------------------------------------------------------------------
// Phase B Slice 1 — tracker overlay state + checked-location bitmap.
// ---------------------------------------------------------------------------
bool g_rando_show_item_tracker = false;
bool g_rando_show_location_tracker = false;
uint8 g_rando_checked_bitmap[kRandoCheckedBitmapBytes];
uint8 g_rando_mushroom_held = 0;
uint8 g_rando_flute_shovel_owned = 0;

// Phase B Inverted runtime — the active slot's world_state, captured at
// Rando_ActivateSidecarSlot from the slot header's additive @68 byte (only
// meaningful when settings_ext_present). Lets the starting-inventory grant in
// rando_placement.c recognize an Inverted slot on reload, where the full
// RandoSettings struct is not available (the sidecar persists only
// settings_hash + the additive ext bytes, not the canonical settings blob).
// Defaults to kWorldState_Open (0) — the safe no-op — when no slot is active
// or the slot predates the world_state ext.
static uint8 g_rando_active_world_state = kWorldState_Open;

uint8 Rando_GetActiveWorldState(void) {
  return g_rando_slot_active ? g_rando_active_world_state : (uint8)kWorldState_Open;
}

bool Rando_SuppressHyruleCastleEscape(void) {
  return (enhanced_features1 & kFeatures1_RandomizerActive) &&
         Rando_GetActiveWorldState() != (uint8)kWorldState_Standard;
}

bool Rando_MushroomHeld(void) {
  return g_rando_slot_active && g_rando_mushroom_held != 0;
}

void Rando_DeliverMushroom(void) {
  g_rando_mushroom_held = 0;
}

// Flute/shovel decouple — see rando.h. Called from the receive-item path
// (AncillaAdd_ItemReceipt, misc.c) when a shovel/flute is granted under rando,
// instead of the vanilla unconditional write to link_item_flute. Records the
// item in the ownership bitfield (additive, never lost), then sets the shared
// link_item_flute slot to the SELECTED function with NEVER-DOWNGRADE semantics:
// the byte is the max of its current value and this item's level (shovel=1,
// flute=2, active flute=3). So acquiring the shovel can never drop the slot
// below an owned flute (fixing "flute then shovel loses the flute"), while
// acquiring the flute selects it. Whenever both are owned the player flips the
// slot's function with the item-menu toggle (Hud_NormalMenu); that toggle may
// set the byte below this floor, and that's fine — it only persists a player
// choice and never runs at grant time.
void Rando_GrantFluteShovel(uint8 lttp_code) {
  uint8 floor;  // lowest link_item_flute value consistent with this item
  if (lttp_code == 0x13) {  // shovel
    g_rando_flute_shovel_owned |= kRandoFluteShovel_Shovel;
    floor = 1;
  } else {                  // 0x14 inactive flute, 0x4a active flute
    g_rando_flute_shovel_owned |= kRandoFluteShovel_Flute;
    if (lttp_code == 0x4a) {
      g_rando_flute_shovel_owned |= kRandoFluteShovel_FluteActive;
      floor = 3;
    } else {
      floor = 2;
    }
  }
  if (link_item_flute < floor)
    link_item_flute = floor;
}

bool Rando_FluteShovelCanToggle(void) {
  return g_rando_slot_active &&
         (g_rando_flute_shovel_owned & kRandoFluteShovel_Shovel) &&
         (g_rando_flute_shovel_owned &
          (kRandoFluteShovel_Flute | kRandoFluteShovel_FluteActive));
}

uint8 Rando_FluteShovelEffectiveLevel(void) {
  // The selected-function byte is the only ownership signal in vanilla — and in
  // a rando save written before the @69 ownership field existed — so start from
  // it. Under an active slot, also fold in the tracked ownership bits and take
  // the max, so owning a flute while the shovel is the selected function still
  // reads as "has the flute" (and never downgrades below the byte).
  uint8 level = link_item_flute;
  if (g_rando_slot_active) {
    uint8 owned = (g_rando_flute_shovel_owned & kRandoFluteShovel_FluteActive) ? 3
                : (g_rando_flute_shovel_owned & kRandoFluteShovel_Flute)       ? 2
                : (g_rando_flute_shovel_owned & kRandoFluteShovel_Shovel)      ? 1
                : 0;
    if (owned > level)
      level = owned;
  }
  return level;
}

void Rando_MarkLocationChecked(uint16 location_id) {
  if (!g_rando_slot_active) return;
  if (location_id >= 512) return;
  uint32 byte_idx = location_id >> 3;
  uint8 bit_mask = (uint8)(1u << (location_id & 7));
  g_rando_checked_bitmap[byte_idx] |= bit_mask;
  // Mark the tracker's reachability cache stale so the overlay re-paints
  // the location's status glyph on the next draw.
  g_reachability_state_counter++;
}

bool Rando_IsLocationChecked(uint16 location_id) {
  if (!g_rando_slot_active) return false;
  if (location_id >= 512) return false;
  return (g_rando_checked_bitmap[location_id >> 3] & (1u << (location_id & 7))) != 0;
}

void Rando_PopulateSlotBitmap(struct RandoSidecarSlot *out_slot) {
  if (out_slot == NULL || !g_rando_slot_active) return;
  // sizeof(out_slot->checked_bitmap) == kRandoCheckedBitmapBytes by
  // construction (both derived from the same 512-bit cap).
  memcpy(out_slot->checked_bitmap, g_rando_checked_bitmap, kRandoCheckedBitmapBytes);
  // Persist Mushroom possession alongside the checked bitmap so an undelivered
  // Mushroom survives save/reload (otherwise a reload could re-lock the
  // Potion Shop check).
  out_slot->header.mushroom_held = g_rando_mushroom_held;
  // Persist flute/shovel ownership so owning both survives save/reload (the
  // single link_item_flute byte can't carry it — see Rando_GrantFluteShovel).
  out_slot->header.flute_shovel_owned = g_rando_flute_shovel_owned;
}

void Rando_OnGameSave(int slot_index, const uint8 *paired_sram_slot, uint32 paired_sram_slot_size) {
  if (!g_rando_slot_active) return;
  if (slot_index < 0 || slot_index >= 3) return;  // 3 sidecar slots

  // Read the existing sidecar slot (preserves header + placement table).
  // If the slot isn't a rando slot, skip silently — the in-game save
  // still recorded SRAM via ZeldaWriteSram; we just don't have a sidecar
  // to update.
  RandoSidecarSlot slot;
  if (!Rando_LoadSidecarSlot(slot_index, &slot)) return;
  if (slot.header.slot_kind != kSlotKind_Randomizer) return;

  // Refresh the bitmap from the in-memory session and write back.
  Rando_PopulateSlotBitmap(&slot);
  (void)Rando_WriteSidecarSlot(slot_index, &slot, paired_sram_slot, paired_sram_slot_size);
}

// ---------------------------------------------------------------------------
// §7.6 — generic confirmation cue for direct-grant placements that skip
// Link_ReceiveItem entirely. Matches the standard receive-item sound used
// inside AncillaAdd_ItemReceipt (`sound_effect_2 = Link_CalculateSfxPan() | 0xf`)
// so the auditory feedback is identical to a normal pickup. Refreshes the HUD so
// any inventory cell that changed (prize bits, dungeon-item bits, Triforce
// counter) reflects immediately rather than waiting for the next implicit
// refresh.
//
// Phase B Slice 9 (add-rando-confirmation-icons): extends Phase A's audio +
// HUD-only cue with a visible icon ancilla. The granted item id is looked up
// in kDirectGrantIcons[item_id] (codegen'd from
// assets/rando/direct_grant_icons.yaml). When the table entry has a
// non-zero gfx (the item's receive-animation sprite bundle),
// AncillaAdd_RandoIconReceipt DMAs that bundle and pops the icon above Link's
// head. Entries with gfx == 0 (the audio-only sentinel) fall back to the
// Phase A audio + HUD behavior — never crash, never spawn a blank ancilla.
//
// Deliberately NOT emitted from within `Rando_DispatchVanillaGrant` — the
// caller knows whether its own code path already provides visual context
// (e.g., the §6.6 boss-kill spawns a FallingPrize regardless of sentinel,
// so the player sees that sprite). Pushing the confirmation to the call
// site lets each integration choose whether to add the cue.
// ---------------------------------------------------------------------------
void Rando_ShowDirectGrantConfirmation(uint8 item_id) {
  // Cluster audit LOW-5 — every caller passes `(uint8)Rando_LastDispatched
  // ItemId()`; the cast loses precision if the sentinel value 0xFFFF
  // ever reaches us. The skip-sentinel path only runs AFTER a successful
  // Rando_DispatchVanillaGrant, which populates g_last_dispatched_item_id
  // with a valid item id (< ITEM__COUNT, currently 125), so the
  // truncation is unreachable in normal flow. The bounds check at
  // `(size_t)item_id < icon_table_len` defends the array access
  // regardless; this assert just makes the invariant explicit so a
  // future change that calls this WITHOUT a prior dispatch fires loudly.
  assert(item_id != 0xFFu /* sentinel byte from 0xFFFF truncation */ ||
         Rando_LastDispatchedItemId() != 0xFFFFu);
  sound_effect_2 = (uint8)(Link_CalculateSfxPan() | 0x0f);
  Hud_RefreshIcon();

  // Slice 9 — look up the per-item icon. Entries with gfx == 0 (the audio-only
  // fallback sentinel: HalfMagic / QuarterMagic / TriforcePiece — no vanilla
  // receive-item GFX) fall back to audio + HUD only, preserving Phase A
  // behavior. gfx != 0 spawns a per-item icon ancilla that DMAs the item's
  // receive-animation sprite bundle and draws it above Link, mirroring the
  // vanilla pickup animation.
  const size_t icon_table_len =
      sizeof(kDirectGrantIcons) / sizeof(kDirectGrantIcons[0]);
  if ((size_t)item_id < icon_table_len) {
    const DirectGrantIconEntry *e = &kDirectGrantIcons[item_id];
    if (e->gfx != 0) {
      AncillaAdd_RandoIconReceipt(e->gfx, e->big, e->oam_flags);
    }
  }
}

void Rando_ReceiveOrConfirm(uint8 lttp_code, uint8 item_id) {
  if (Rando_ShouldSkipReceive(lttp_code)) {
    Rando_ShowDirectGrantConfirmation(item_id);
  } else {
    Link_ReceiveItem(lttp_code, 0);
  }
}

// ---------------------------------------------------------------------------
// §6.6 boss-kill dispatch helpers. Each boss kill grants TWO rando locations
// (BossHeart + Prize). Phase A's default `bossHeartsInPool=false` policy
// identity-places BossHeartContainer at every _Boss slot — so the dispatch
// is still fired for uniformity (caller treats the no-op identity case as
// "the player gets a heart container, vanilla behavior").
//
// Dungeon ID layout (cur_palace_index_x2 >> 1) — game-side convention.
// Derived from kDungeonCrystalPendantBit / kBossFinishedFallingItem
// (zelda_rtl.c, dungeon.c). NOTE: TH lives at 10, not 3 — this is not
// the ALTTPR id ordering.
//   0 HCE  (no boss; Sanctuary chest is the heart container slot)
//   1 (unused sub-area, no prize)
//   2 EP   3 DP
//   4 HCT  (Agahnim; not a heart-drop boss — handled separately)
//   5 PoD  6 SP   7 SW   8 TT   9 IP  10 TH  11 MM  12 TR
//  13 GT   (Agahnim 2; same as HCT path)
// ---------------------------------------------------------------------------
uint16 Rando_GetBossHeartLocation(uint8 dungeon_id) {
  static const uint16 kBossHeartByDungeon[14] = {
    0xFFFFu,                       //  0  HCE
    0xFFFFu,                       //  1  (unused)
    LOC_Eastern_Palace_Boss,       //  2  EP
    LOC_Desert_Palace_Boss,        //  3  DP
    0xFFFFu,                       //  4  HCT (Agahnim path)
    LOC_Palace_of_Darkness_Boss,   //  5  PoD
    LOC_Swamp_Palace_Boss,         //  6  SP
    LOC_Skull_Woods_Boss,          //  7  SW
    LOC_Thieves_Town_Boss,         //  8  TT
    LOC_Ice_Palace_Boss,           //  9  IP
    LOC_Tower_of_Hera_Boss,        // 10  TH
    LOC_Misery_Mire_Boss,          // 11  MM
    LOC_Turtle_Rock_Boss,          // 12  TR
    0xFFFFu                        // 13  GT (Agahnim 2 path)
  };
  if (dungeon_id >= 14) return 0xFFFFu;
  return kBossHeartByDungeon[dungeon_id];
}

uint16 Rando_GetBossPrizeLocation(uint8 dungeon_id) {
  static const uint16 kBossPrizeByDungeon[14] = {
    0xFFFFu,                       //  0  HCE
    0xFFFFu,                       //  1  (unused)
    LOC_Eastern_Palace_Prize,      //  2  EP
    LOC_Desert_Palace_Prize,       //  3  DP
    0xFFFFu,                       //  4  HCT
    LOC_Palace_of_Darkness_Prize,  //  5  PoD
    LOC_Swamp_Palace_Prize,        //  6  SP
    LOC_Skull_Woods_Prize,         //  7  SW
    LOC_Thieves_Town_Prize,        //  8  TT
    LOC_Ice_Palace_Prize,          //  9  IP
    LOC_Tower_of_Hera_Prize,       // 10  TH
    LOC_Misery_Mire_Prize,         // 11  MM
    LOC_Turtle_Rock_Prize,         // 12  TR
    0xFFFFu                        // 13  GT
  };
  if (dungeon_id >= 14) return 0xFFFFu;
  return kBossPrizeByDungeon[dungeon_id];
}

// ---------------------------------------------------------------------------
// Per-seed shuffle-assignment globals consumed by Logic_ComputeReachability.
// ---------------------------------------------------------------------------
static const uint8 *g_dungeon_prize_assignment = NULL;
static const uint8 *g_medallion_assignment = NULL;

void Rando_SetDungeonPrizeAssignment(const uint8 *assignment) {
  g_dungeon_prize_assignment = assignment;
}
void Rando_SetMedallionAssignment(const uint8 *assignment) {
  g_medallion_assignment = assignment;
}
const uint8 *Rando_GetDungeonPrizeAssignment(void) { return g_dungeon_prize_assignment; }
const uint8 *Rando_GetMedallionAssignment(void) { return g_medallion_assignment; }

// ---------------------------------------------------------------------------
// Session-persistent placement storage. The sidecar struct lives in the
// file-select cache (and is overwritten when the cache reloads), so we copy
// its placements[] into this static buffer before installing — that way the
// pointer Placement_Install holds stays valid for the duration of gameplay.
// ---------------------------------------------------------------------------
#define kRando_SessionPlacementCapacity 512
static RandoPlacement g_session_placements[kRando_SessionPlacementCapacity];
static RandoPlacementTable g_session_placement_table;

// `g_wanted_zelda_features1` lives in main.c — declared here so we can
// mirror the feature-bit update into the wanted bank as well as the
// in-RAM enhanced bank (zelda_rtl.c's frame loop syncs wanted → enhanced).
extern uint32 g_wanted_zelda_features1;

// §62 — cache for the active slot's textual share string (50 base32 chars +
// NUL). Populated at Rando_ActivateSidecarSlot from the slot header's raw
// binary; consumed by Rando_RevealActiveSlotSpoiler to resolve the on-disk
// spoiler path. Cleared on Rando_DeactivateSlot.
static char g_rando_active_share_string[64] = {0};

// Active slot's recovered settings + shuffle assignments (format_version >= 2
// slots carry the canonical settings blob). g_rando_active_settings_valid gates
// the runtime reachability engine the tracker windows consume: when false
// (older v1 slot, snapshot-restore, or a slot whose writer didn't populate the
// blob), reachability is SUPPRESSED rather than computed from guessed defaults —
// a wrong prize_shuffle flag mis-seeds the shuffle stream and yields
// confidently-wrong prize/medallion gating.
static RandoSettings g_rando_active_settings;
static bool g_rando_active_settings_valid = false;
// Session-lifetime buffers the predicate VM borrows via Rando_Get*Assignment().
// These MUST outlive activation — the setters store the pointer, not a copy — so
// they live here as file-statics, NOT in the placer's function-statics (which
// aren't repopulated on a slot reload).
static uint8 g_rando_active_prize_assignment[kRandoDungeonCount];
static uint8 g_rando_active_medallion_assignment[kRandoMedallionEntranceCount];

void Rando_ActivateSidecarSlot(const RandoSidecarSlot *src) {
  if (src == NULL || src->header.slot_kind != kSlotKind_Randomizer) {
    Rando_DeactivateSlot();
    return;
  }
  uint16 n = src->placement_count;
  if (n > kRando_SessionPlacementCapacity) n = kRando_SessionPlacementCapacity;
  memcpy(g_session_placements, src->placements, (size_t)n * sizeof(RandoPlacement));
  g_session_placement_table.entries = g_session_placements;
  g_session_placement_table.count = n;
  Placement_Install(&g_session_placement_table);
  g_rando_slot_active = 1;
  g_wanted_zelda_features1 |= kFeatures1_RandomizerActive;
  enhanced_features1 |= kFeatures1_RandomizerActive;

  // Wire the snapshot-tail TLV emitter (per rando_save.h "UI also calls
  // Rando_SetSnapshotContext"). Without this, `RandoSnapshotTail_Save`
  // early-returns at `!g_has_ctx` and the snapshot file has no rando TLV;
  // a later Ctrl+F1 then restores `g_ram`'s rando-active bit but leaves
  // `g_active_placement == NULL` (placement table lives in the heap, not
  // g_ram), so the next chest dispatch falls back to vanilla items.
  Rando_SetSnapshotContext(src->header.generator_version,
                           src->header.settings_hash,
                           src->header.share_string);

  // Phase B Slice 1 — copy the slot's checked-location bitmap into the
  // session state. Bitmap size matches between slot and session
  // (both kRandoCheckedBitmapBytes = 64).
  memcpy(g_rando_checked_bitmap, src->checked_bitmap, kRandoCheckedBitmapBytes);
  g_rando_mushroom_held = src->header.mushroom_held;
  g_rando_flute_shovel_owned = src->header.flute_shovel_owned;
  // Phase B Inverted runtime — capture the slot's world_state from the
  // additive @68 ext byte. Only trust it when settings_ext_present is set
  // (older slots wrote 0 there, which already maps to kWorldState_Open).
  g_rando_active_world_state = src->header.settings_ext_present
                                   ? src->header.world_state
                                   : (uint8)kWorldState_Open;
  // Force the tracker to repaint after activation.
  g_reachability_state_counter++;

  // §62 — cache the active slot's textual share string for the in-binary
  // reveal action. Race-mode slots have a suppressed spoiler at
  // <spoiler_dir>/<share_string>.json that the reveal action regenerates.
  // Non-race slots populate the cache anyway (harmless; reveal will fall
  // through to FileNotFound when there's no ZRSR file).
  g_rando_active_share_string[0] = '\0';
  (void)Share_EncodeRaw(src->header.share_string, g_rando_active_share_string,
                        (int)sizeof(g_rando_active_share_string));

  // === Reachability settings + shuffle assignments (tracker engine) ===
  // The runtime reachability engine (Logic_ComputeReachability, consumed by the
  // Check/Map tracker windows) needs the seed's FULL settings plus the prize /
  // medallion shuffle assignments. Nothing else installs the assignments at
  // reload — the placer set them at generation time (rando_placement.c) and that
  // doesn't re-run here. Recover the canonical settings blob (format_version >=
  // 2) and recompute the assignments from (settings, seed) in the EXACT placer
  // order (Rng_SeedFromU64 → PrizeShuffle_Run → MedallionShuffle_Run on one
  // shared rng; medallion output is stream-position-dependent on prize_shuffle).
  // When the blob is absent, mark settings invalid and clear the assignments so
  // the tracker layer suppresses reachability instead of guessing.
  g_rando_active_settings_valid = false;
  if (src->header.settings_present &&
      Settings_CanonicalDeserialize(src->settings_canonical, &g_rando_active_settings) == 0) {
    ShareString ss;
    if (Share_Decode(g_rando_active_share_string, &ss) == kShareDecodeOk) {
      RandoRng shuffle_rng;
      Rng_SeedFromU64(&shuffle_rng, ss.seed_u64);
      PrizeShuffle_Run(&g_rando_active_settings, &shuffle_rng, g_rando_active_prize_assignment);
      MedallionShuffle_Run(&g_rando_active_settings, &shuffle_rng, g_rando_active_medallion_assignment);
      Rando_SetDungeonPrizeAssignment(g_rando_active_prize_assignment);
      Rando_SetMedallionAssignment(g_rando_active_medallion_assignment);
      g_rando_active_settings_valid = true;
    }
  }
  if (!g_rando_active_settings_valid) {
    Rando_SetDungeonPrizeAssignment(NULL);
    Rando_SetMedallionAssignment(NULL);
  }

  // === Phase B hints: regenerate telepathic-tile hints for this slot ===
  // Resolves the prior audit-of-audit HIGH-3 TODO. Hints are a pure function
  // of (settings, placement table); the generator (rando_hints.c) reads only
  // the `hints` and `goal` axes from RandoSettings. Rather than the one-way
  // settings_hash, the slot header carries those two axes additively in its
  // reserved tail (rando_save.h settings extension). We synthesize a settings
  // struct from defaults, override `hints`/`goal` from the ext, and
  // regenerate — so a slot loaded from disk (including share-string imports)
  // shows hints without re-running the full seed generator.
  {
    RandoSettings hint_settings;
    Settings_SetDefaults(&hint_settings);
    if (src->header.settings_ext_present) {
      hint_settings.hints = src->header.hints_setting;
      hint_settings.goal = src->header.goal;
    } else {
      // Older slot (or writer that did not populate the ext): default to
      // hints-on so existing rando slots still surface telepathic-tile hints.
      // goal stays at the Settings_SetDefaults value (Murahdahla won't fire
      // unless it happens to be a Triforce/Ganon-hunt default).
      hint_settings.hints = kHintsMode_On;
    }
    Rando_GenerateHints(&hint_settings, &g_session_placement_table, NULL);
  }
  // === Phase B hints: end ===
}

void Rando_DeactivateSlot(void) {
  Placement_Install(NULL);
  g_session_placement_table.entries = NULL;
  g_session_placement_table.count = 0;
  g_rando_slot_active = 0;
  g_wanted_zelda_features1 &= ~(uint32)kFeatures1_RandomizerActive;
  enhanced_features1 &= ~(uint32)kFeatures1_RandomizerActive;
  // Pair with the SetSnapshotContext in Activate — leaving stale metadata
  // would let a future snapshot save emit a TLV bound to the wrong slot.
  Rando_ClearSnapshotContext();

  // Phase B Slice 1 — clear the checked bitmap and reset tracker visibility
  // so the next slot launches with the documented "trackers default hidden"
  // contract.
  memset(g_rando_checked_bitmap, 0, kRandoCheckedBitmapBytes);
  g_rando_mushroom_held = 0;
  g_rando_flute_shovel_owned = 0;
  g_rando_active_world_state = kWorldState_Open;
  g_rando_show_item_tracker = false;
  g_rando_show_location_tracker = false;

  // Phase B Slice 5 — clear the hint set so it doesn't leak across slot
  // switches once the hint generator lands (stub today; no-op).
  Rando_ClearHints();

  // §62 — clear the share-string cache; reveal action returns FileNotFound
  // when no slot is active.
  g_rando_active_share_string[0] = '\0';

  // Reachability: invalidate the recovered settings and NULL the shuffle
  // assignment pointers. Without this, switching to a vanilla/empty slot would
  // leave eval_has_prize / eval_medallion_opens reading the prior slot's stale
  // assignment table. (The VM treats NULL as "no prize/medallion reachable".)
  g_rando_active_settings_valid = false;
  Rando_SetDungeonPrizeAssignment(NULL);
  Rando_SetMedallionAssignment(NULL);

  // Reset the starting-inventory gate so an in-session slot-switch (slot A
  // already received its grant, then user backs out and loads slot B) lets
  // slot B's grant fire on its next Module05_LoadFile. Without this, the
  // same-boot gate at g_ram[0x65e] stays set and slot B silently misses its
  // escape-fill on a brand-new save. The cold-boot exploit guard in
  // Rando_TryGrantStartingInventory still gates on sram_progress_indicator
  // so in-progress saves aren't re-granted.
  g_rando_starting_inventory_granted = 0;
}

// Whether the active slot's settings were recovered (format_version >= 2 blob)
// and the shuffle assignments installed. The tracker windows gate their
// reachability display on this — false means "settings unknown", show only
// checked/unchecked, not reachable.
// Whether a randomizer slot is currently active (a placement is installed).
bool Rando_IsActive(void) { return g_rando_slot_active != 0; }

bool Rando_HasActiveSettings(void) { return g_rando_active_settings_valid; }

// The recovered active settings, or NULL when unavailable. Used by the runtime
// reachability bridge (Rando_GetLiveReachability).
const RandoSettings *Rando_GetActiveSettings(void) {
  return g_rando_active_settings_valid ? &g_rando_active_settings : NULL;
}

// ---------------------------------------------------------------------------
// Live reachability bridge (tracker windows).
//
// Rando_BuildRuntimeCounts maps the live g_ram inventory into the logical
// RandoCounts the predicate VM reads. The macros (macros.yaml) accept the
// progressive form via HAS_AMOUNT(Progressive*, n), so populating the
// progressive counts satisfies every tier disjunct — we don't need the
// absolute L1Sword/etc. ids. Prizes (crystals/pendants) are NOT set here: the
// reachability fixed-point derives them from reachable dungeon bosses
// (OP_HAS_PRIZE + cleared_dungeons), i.e. logical accessibility. Event items
// the VM treats as inventory (RescuedZelda, DefeatAgahnim) are derived from
// actual game/rando progress.
// ---------------------------------------------------------------------------
void Rando_BuildRuntimeCounts(RandoCounts *out) {
  if (out == NULL) return;
  memset(out, 0, sizeof(*out));

  // Progressive tiers. Sword byte 0xFF == none (not 0).
  uint8 sword = link_sword_type;
  if (sword >= 1 && sword <= 4) out->by_item_id[ITEM_ProgressiveSword] = sword;
  out->by_item_id[ITEM_ProgressiveShield] = link_shield_type;  // 0..3
  out->by_item_id[ITEM_ProgressiveArmor] = link_armor;          // 0=green,1=blue,2=red
  out->by_item_id[ITEM_ProgressiveGlove] = link_item_gloves;    // 0..2
  // Bow byte is non-linear: 0 none, 1-2 wood, 3-4 silver (see progressive_to_lttp).
  uint8 bowb = link_item_bow;
  if (bowb >= 3) {
    out->by_item_id[ITEM_ProgressiveBow] = 2;
    out->by_item_id[ITEM_SilverArrowUpgrade] = 1;
  } else if (bowb >= 1) {
    out->by_item_id[ITEM_ProgressiveBow] = 1;
  }

  // Single-presence items.
  out->by_item_id[ITEM_FireRod] = link_item_fire_rod ? 1 : 0;
  out->by_item_id[ITEM_IceRod] = link_item_ice_rod ? 1 : 0;
  out->by_item_id[ITEM_Hammer] = link_item_hammer ? 1 : 0;
  out->by_item_id[ITEM_Hookshot] = link_item_hookshot ? 1 : 0;
  out->by_item_id[ITEM_Bombos] = link_item_bombos_medallion ? 1 : 0;
  out->by_item_id[ITEM_Ether] = link_item_ether_medallion ? 1 : 0;
  out->by_item_id[ITEM_Quake] = link_item_quake_medallion ? 1 : 0;
  out->by_item_id[ITEM_Lamp] = link_item_torch ? 1 : 0;
  out->by_item_id[ITEM_BugCatchingNet] = link_item_bug_net ? 1 : 0;
  out->by_item_id[ITEM_BookOfMudora] = link_item_book_of_mudora ? 1 : 0;
  out->by_item_id[ITEM_CaneOfSomaria] = link_item_cane_somaria ? 1 : 0;
  out->by_item_id[ITEM_CaneOfByrna] = link_item_cane_byrna ? 1 : 0;
  out->by_item_id[ITEM_Cape] = link_item_cape ? 1 : 0;
  out->by_item_id[ITEM_MagicMirror] = link_item_mirror ? 1 : 0;
  out->by_item_id[ITEM_Boots] = link_item_boots ? 1 : 0;
  out->by_item_id[ITEM_Flippers] = link_item_flippers ? 1 : 0;
  out->by_item_id[ITEM_MoonPearl] = link_item_moon_pearl ? 1 : 0;

  // Boomerangs: byte 1=blue, 2=red (separate rando items).
  if (link_item_boomerang == 1) out->by_item_id[ITEM_BlueBoomerang] = 1;
  else if (link_item_boomerang == 2) out->by_item_id[ITEM_RedBoomerang] = 1;

  // Mushroom / Powder share byte 0xF344 (1=mushroom, 2=powder); true mushroom
  // possession is tracked separately in rando state so Powder-first can't lock
  // out the mushroom logic.
  if (Rando_MushroomHeld() || link_item_mushroom == 1) out->by_item_id[ITEM_Mushroom] = 1;
  if (link_item_mushroom == 2) out->by_item_id[ITEM_MagicPowder] = 1;

  // Flute / shovel decouple — true ownership in rando state (the vanilla
  // link_item_flute byte is one slot that can't hold both).
  uint8 fs = g_rando_flute_shovel_owned;
  if (fs & kRandoFluteShovel_Shovel) out->by_item_id[ITEM_Shovel] = 1;
  if (fs & kRandoFluteShovel_Flute) out->by_item_id[ITEM_OcarinaInactive] = 1;

  // Half / quarter magic: link_magic_consumption 1=half, 2=quarter.
  if (link_magic_consumption >= 1) out->by_item_id[ITEM_HalfMagic] = 1;
  if (link_magic_consumption >= 2) out->by_item_id[ITEM_QuarterMagic] = 1;

  // Bottles — count non-empty slots. HasBottle macro sums all bottle ids via
  // HAS_ANY_COUNT, so a single counter satisfies the count-based gates. (Content
  // -specific gates like GoodBee are not modeled; an acceptable approximation.)
  {
    uint8 bottles = 0;
    for (int i = 0; i < 4; i++) if (link_bottle_info[i] != 0) bottles++;
    out->by_item_id[ITEM_BottleEmpty] = bottles;
  }

  // Virtual / event items the VM reads as inventory.
  out->by_item_id[ITEM_StartingHeart] = 3;  // baseline 3 hearts
  // RescuedZelda: pre-collected in non-Standard worlds; in Standard it is earned
  // at the castle escape (sram_progress_indicator >= 2 == Zelda at sanctuary).
  // Use the SAME world-state source the reachability graph walks (the recovered
  // settings) so the event derivation can't diverge from the graph (audit LOW).
  uint8 ws = g_rando_active_settings_valid ? g_rando_active_settings.world_state
                                           : g_rando_active_world_state;
  bool rescued = (ws != (uint8)kWorldState_Standard) || (sram_progress_indicator >= 2);
  if (rescued) out->by_item_id[ITEM_RescuedZelda] = 1;
  // DefeatAgahnim: the Agahnim-1 location is checked when he is defeated.
  if (Rando_IsLocationChecked(LOC_Agahnim)) out->by_item_id[ITEM_DefeatAgahnim] = 1;

  // Vanilla-mode dungeon items are logically available in-place — pre-grant
  // exactly as the placer does (shared helper). NOTE: shuffled (keysanity)
  // dungeon-item classes are NOT yet read from live g_ram here, so the check
  // tracker under-reports dungeon-interior locations under those non-default
  // modes; default seeds are all-vanilla. (Follow-up: map live per-dungeon keys
  // / big keys / maps / compasses → registry ids for non-vanilla modes.)
  if (g_rando_active_settings_valid) {
    Rando_SeedVanillaDungeonItems(out, &g_rando_active_settings);
  }
}

// Memoized live reachability. Recomputed only when the reachability-state
// counter advances (bumped on item pickups, location checks, and progress
// events). Returns NULL when settings are unavailable (older slot / snapshot
// restore) — callers then suppress the reachability display. The result is
// snapshotted out of the shared Logic_ComputeReachability buffer so it stays
// valid across frames and across both tracker windows.
static uint32 g_live_reach_counter = 0xFFFFFFFFu;
static uint8 g_live_reach_progress = 0xFFu;
static bool g_live_reach_valid = false;

const RandoReachability *Rando_GetLiveReachability(void) {
  if (!g_rando_active_settings_valid) {
    g_live_reach_valid = false;
    return NULL;
  }
  uint32 cur = Rando_GetReachabilityCounter();
  // sram_progress_indicator feeds RescuedZelda in Standard (Rando_BuildRuntime-
  // Counts) but does NOT bump the reachability counter when it advances during
  // the castle-escape cutscene — so fold it into the memo key, else RescuedZelda
  // -gated regions stay dark until the next unrelated location check (audit M1).
  uint8 prog = sram_progress_indicator;
  if (g_live_reach_valid && cur == g_live_reach_counter && prog == g_live_reach_progress) {
    return Reachability_Snapshot(false);  // stable cached snapshot
  }
  RandoCounts counts;
  Rando_BuildRuntimeCounts(&counts);
  const RandoReachability *r = Logic_ComputeReachability(&counts, &g_rando_active_settings);
  if (r == NULL) {
    g_live_reach_valid = false;
    return NULL;
  }
  g_live_reach_counter = cur;
  g_live_reach_progress = prog;
  g_live_reach_valid = true;
  return Reachability_Snapshot(true);  // copy out of the shared buffer
}

void Rando_FillItemView(RandoItemView *out) {
  if (out == NULL) return;
  memset(out, 0, sizeof(*out));

  uint8 sword = link_sword_type;
  out->sword = (sword >= 1 && sword <= 4) ? sword : 0;  // 0xFF == none
  out->shield = link_shield_type;     // 0..3
  out->mail = link_armor;             // 0 green, 1 blue, 2 red
  out->gloves = link_item_gloves;     // 0..2
  uint8 bowb = link_item_bow;
  out->bow = (bowb >= 3) ? 2 : (bowb >= 1 ? 1 : 0);  // wood/silver
  out->boomerang = (link_item_boomerang <= 2) ? link_item_boomerang : 0;

  out->hookshot = link_item_hookshot != 0;
  out->firerod = link_item_fire_rod != 0;
  out->icerod = link_item_ice_rod != 0;
  out->hammer = link_item_hammer != 0;
  out->lamp = link_item_torch != 0;
  out->net = link_item_bug_net != 0;
  out->book = link_item_book_of_mudora != 0;
  out->somaria = link_item_cane_somaria != 0;
  out->byrna = link_item_cane_byrna != 0;
  out->cape = link_item_cape != 0;
  out->mirror = link_item_mirror != 0;
  out->boots = link_item_boots != 0;
  out->flippers = link_item_flippers != 0;
  out->moon_pearl = link_item_moon_pearl != 0;
  out->bombos = link_item_bombos_medallion != 0;
  out->ether = link_item_ether_medallion != 0;
  out->quake = link_item_quake_medallion != 0;

  // Shared-byte items. With a rando slot active, true ownership is in rando
  // state (the vanilla bytes are single slots that can't represent both). With
  // no slot active that state isn't tracked, so fall back to the raw vanilla
  // bytes for the informational view (audit LOW).
  if (g_rando_slot_active) {
    out->mushroom = Rando_MushroomHeld() || link_item_mushroom == 1;
    out->powder = link_item_mushroom == 2;
    uint8 fs = g_rando_flute_shovel_owned;
    out->flute = (fs & kRandoFluteShovel_Flute) != 0;
    out->shovel = (fs & kRandoFluteShovel_Shovel) != 0;
  } else {
    out->mushroom = link_item_mushroom == 1;
    out->powder = link_item_mushroom == 2;
    out->flute = link_item_flute >= 2;   // 0xF34C: 2=flute(inactive), 3=flute(active)
    out->shovel = link_item_flute == 1;  // 1=shovel
  }

  {
    uint8 bottles = 0;
    for (int i = 0; i < 4; i++) if (link_bottle_info[i] != 0) bottles++;
    out->bottles = bottles;
  }
  out->magic = link_magic_consumption;          // 0 normal, 1 half, 2 quarter
  out->hearts = (uint8)(link_health_capacity >> 3);
  out->heart_pieces = link_heart_pieces;

  // Crystals (7) and pendants (3) — vanilla bit masks (mirror messaging.c).
  static const uint8 kCrystalMask[7] = { 2, 0x40, 8, 0x20, 1, 4, 0x10 };
  static const uint8 kPendantMask[3] = { 4, 1, 2 };
  uint8 cbits = link_has_crystals, pbits = link_which_pendants;
  for (uint8 i = 0; i < 7; i++) {
    if (cbits & kCrystalMask[i]) { out->crystal_mask |= (uint8)(1 << i); out->crystals++; }
  }
  for (uint8 i = 0; i < 3; i++) {
    if (pbits & kPendantMask[i]) { out->pendant_mask |= (uint8)(1 << i); out->pendants++; }
  }

  out->agahnim = Rando_IsLocationChecked(LOC_Agahnim);
}

// ---------------------------------------------------------------------------
// Phase B Slice 6 — race-mode reveal action.
//
// Pipeline:
//   1. Read the suppressed file at the given path; verify magic + CRC.
//   2. Deserialize embedded settings (with race_mode = 0 by construction).
//   3. Decode the file's share string to recover seed_u64.
//   4. Regenerate placement via Place_AssumedFill(settings, seed_u64). This
//      is the SAME pipeline the CLI / settings-screen uses, so output is
//      bit-identical when generator inputs match.
//   5. Compute spheres via Logic_ComputeSpheres(settings, placements).
//   6. Build a RandoSpoiler view with race_mode = 0 and wall_clock = 0
//      (the same normalization used when computing the stamp at suppression
//      time) and write to a tmp file. SHA-256 the bytes.
//   7. Compare the computed SHA-256 to the file's spoiler_stamp. If
//      mismatch, return kRandoReveal_StampMismatch — the suppressed file
//      does not match the generator's current output for these inputs.
//   8. Write the full JSON + .txt spoiler to disk (overwriting the
//      suppressed binary).
// ---------------------------------------------------------------------------
#include "rando_spoiler.h"

extern void sha256_buffer(const uint8 *data, size_t len, uint8 out[32]);

RandoRevealResult Rando_RevealSpoiler(const char *suppressed_path,
                                      const char *expected_share_string) {
  char resolved[1024];
  if (suppressed_path == NULL) {
    if (expected_share_string == NULL) return kRandoReveal_FileNotFound;
    int n = Spoiler_ResolvePath(expected_share_string, ".json", resolved, (int)sizeof(resolved));
    if (n <= 0) return kRandoReveal_FileNotFound;
    suppressed_path = resolved;
  }

  // Idempotency (randomizer-ui spec §3.4): if the file at this path is
  // already a full JSON spoiler (first byte '{'), reveal already ran.
  // Return success without rewriting. The discriminator is the same one
  // §2.4 calls out — first byte 'Z' (suppressed magic 'ZRSR') vs. '{'
  // (JSON open brace) vs. anything else (malformed).
  {
    FILE *fdis = fopen(suppressed_path, "rb");
    if (fdis == NULL) return kRandoReveal_FileNotFound;
    int first = fgetc(fdis);
    fclose(fdis);
    if (first == '{') return kRandoReveal_Ok;
    if (first != 'Z') return kRandoReveal_ParseError;
  }

  RandoSuppressedSpoiler hdr;
  int rd = Spoiler_ReadSuppressed(suppressed_path, &hdr);
  if (rd == -1) return kRandoReveal_FileNotFound;
  if (rd == -2) return kRandoReveal_ParseError;
  if (rd == -3) return kRandoReveal_CrcMismatch;

  // Optional share-string equality check.
  if (expected_share_string != NULL) {
    size_t want_len = strlen(expected_share_string);
    if (want_len != hdr.share_string_len ||
        memcmp(expected_share_string, hdr.share_string, want_len) != 0) {
      return kRandoReveal_ShareStringMismatch;
    }
  }

  if (hdr.generator_version != (uint16)kGeneratorVersion) {
    return kRandoReveal_VersionMismatch;
  }

  RandoSettings settings;
  if (Settings_CanonicalDeserialize(hdr.settings_canonical, &settings) != 0) {
    return kRandoReveal_SettingsCorrupt;
  }
  // Defensive: ensure race_mode is 0 in the regenerated settings (matches
  // what compute_stamp normalizes for SHA input).
  settings.race_mode = 0;

  // Decode the share string to get seed_u64.
  char share_buf[kRandoSuppressedSpoilerShareStringMax + 1];
  uint32 sl = hdr.share_string_len;
  if (sl > kRandoSuppressedSpoilerShareStringMax) sl = kRandoSuppressedSpoilerShareStringMax;
  memcpy(share_buf, hdr.share_string, sl);
  share_buf[sl] = '\0';
  ShareString ss;
  if (Share_Decode(share_buf, &ss) != kShareDecodeOk) {
    return kRandoReveal_ParseError;
  }
  uint64 seed_u64 = ss.seed_u64;

  // Regenerate the placement table.
  // Slice 6 audit H2 — pass budget_seconds=0 (no wall-clock cutoff) so the
  // placer runs to its hard 8-attempt cap. This makes the placer's behavior
  // deterministic at reveal regardless of machine speed; combined with the
  // stamp's H1 normalization of attempts_used/forward_fill_fallback_count,
  // the stamp is reproducible across machines.
  static RandoPlacement scratch_entries[512];
  RandoPlacementTable table;
  table.entries = scratch_entries;
  table.count = 0;
  if (!Place_AssumedFill(&settings, seed_u64, /*budget_seconds=*/0, &table)) {
    if (table.count == 0) return kRandoReveal_PlacementFailed;
  }

  RandoSpheres spheres;
  Logic_ComputeSpheres(&settings, &table, &spheres);

  // Regenerate the hint set so the spoiler JSON's `hints[]` array
  // matches the generate-time bytes. Without this, the regenerated
  // spoiler emits empty `hints: []` (module-static g_hint_table is
  // zero in a fresh process), so any seed generated with hints=on
  // would fail the SHA-256 stamp comparison below with
  // kRandoReveal_StampMismatch. Determinism contract is in the
  // rando_hints.c header doc-comment — same (settings, placement) →
  // byte-identical hint text; mirrors the generate-time call site that
  // produces the generate-time bytes.
  (void)Rando_GenerateHints(&settings, &table, &spheres);

  // Build the spoiler view and write to a tmp file for stamp computation.
  // Slice 6 audit H1 — forward_fill_fallback_count and retry_attempts are
  // normalized in compute_stamp via the same constants (see
  // rando_spoiler.c). At reveal, the regen spoiler is also normalized so
  // the JSON bytes are byte-identical to the generate-time stamp input.
  RandoSpoiler regen;
  memset(&regen, 0, sizeof(regen));
  regen.share_string = share_buf;
  regen.seed_u64 = seed_u64;
  regen.generator_version = kGeneratorVersion;
  regen.settings = &settings;
  regen.placements = &table;
  regen.spheres = &spheres;
  regen.goal_completable = Goal_IsCompletable(&settings, &table);
  regen.forward_fill_fallback_count = 0;  // stamp normalization
  regen.retry_attempts = 1;               // stamp normalization
  regen.generation_wall_clock_ms = 0;     // stamp normalization

  // Write JSON to a tmp file next to the suppressed path so we can read it
  // back and SHA-256 it without disturbing the suppressed file.
  // Slice 6 audit M3 — removed dead tmpfile() open/close scaffolding.
  char tmp_path[1280];
  if (snprintf(tmp_path, sizeof(tmp_path), "%s.reveal-tmp", suppressed_path) >= (int)sizeof(tmp_path)) {
    return kRandoReveal_WriteFailed;
  }

  // Cleanup epilogue is reached via `goto fail` from every error path so
  // .reveal-tmp never leaks (Slice 6 audit M1).
  uint8 *bytes = NULL;
  RandoRevealResult result = kRandoReveal_WriteFailed;

  if (!Spoiler_WriteJson(&regen, tmp_path)) goto fail;

  // Read the tmp file back and SHA-256 it.
  FILE *rb = fopen(tmp_path, "rb");
  if (rb == NULL) goto fail;
  fseek(rb, 0, SEEK_END);
  long flen = ftell(rb);
  rewind(rb);
  if (flen <= 0) { fclose(rb); goto fail; }
  bytes = (uint8 *)malloc((size_t)flen);
  if (bytes == NULL) { fclose(rb); goto fail; }
  size_t got = fread(bytes, 1, (size_t)flen, rb);
  fclose(rb);
  if (got != (size_t)flen) goto fail;
  uint8 calc_stamp[32];
  sha256_buffer(bytes, (size_t)flen, calc_stamp);

  if (memcmp(calc_stamp, hdr.spoiler_stamp, 32) != 0) {
    // Slice 6 audit H3 — leave the suppressed file untouched on stamp
    // mismatch. The fact that we never wrote to suppressed_path is the
    // invariant.
    result = kRandoReveal_StampMismatch;
    goto fail;
  }

  // Stamp matched. Audit M1 — write to `.partial`, fsync-equivalent close,
  // then rename atomically over the suppressed file. The previous
  // fopen("wb") + fwrite pattern truncated the suppressed file before
  // writing; a mid-write failure (disk full, IO error) would lose the
  // original suppressed bytes AND leave a partial new file. The partial+
  // rename pattern keeps the suppressed file intact until the new bytes
  // are fully on disk.
  {
    char partial_path[1280];
    if (snprintf(partial_path, sizeof(partial_path), "%s.partial", suppressed_path) >= (int)sizeof(partial_path)) {
      goto fail;
    }
    FILE *out = fopen(partial_path, "wb");
    if (out == NULL) goto fail;
    size_t wrote = fwrite(bytes, 1, (size_t)flen, out);
    if (fclose(out) != 0 || wrote != (size_t)flen) {
      remove(partial_path);
      goto fail;
    }
    // On Windows rename() fails if the destination exists; use
    // remove()+rename(). The window between remove() and rename() is
    // small enough that a crash there is acceptable (the partial file
    // exists; the user can manually inspect / rescue). On POSIX rename()
    // is atomic-replace.
    remove(suppressed_path);
    if (rename(partial_path, suppressed_path) != 0) {
      // Partial file remains as evidence of the failed write; leave it
      // for inspection rather than auto-deleting.
      goto fail;
    }
  }

  // Write the .txt companion alongside.
  {
    char txt_path[1280];
    size_t pl = strlen(suppressed_path);
    if (pl >= 5 && strcmp(suppressed_path + pl - 5, ".json") == 0) {
      memcpy(txt_path, suppressed_path, pl - 5);
      strcpy(txt_path + pl - 5, ".txt");
      (void)Spoiler_WriteText(&regen, txt_path);
    } else if (pl + 4 < sizeof(txt_path)) {
      memcpy(txt_path, suppressed_path, pl);
      strcpy(txt_path + pl, ".txt");
      (void)Spoiler_WriteText(&regen, txt_path);
    }
    // If neither path-form fits, skip the .txt — success on the JSON path.
  }

  result = kRandoReveal_Ok;

fail:
  // Always drop the .reveal-tmp scratch file (Slice 6 audit M1) and free
  // the in-memory bytes (Slice 6 audit M1). `result` carries the outcome.
  if (bytes != NULL) free(bytes);
  remove(tmp_path);
  return result;
}

const char *Rando_RevealResultDescription(RandoRevealResult r) {
  switch (r) {
    case kRandoReveal_Ok:                  return "Spoiler revealed.";
    case kRandoReveal_FileNotFound:        return "Suppressed spoiler file not found.";
    case kRandoReveal_ParseError:          return "Suppressed spoiler is malformed.";
    case kRandoReveal_CrcMismatch:         return "Suppressed spoiler CRC mismatch — file is corrupt or tampered.";
    case kRandoReveal_ShareStringMismatch: return "Suppressed file's share string does not match the active slot.";
    case kRandoReveal_VersionMismatch:     return "Suppressed file was produced by a different generator version.";
    case kRandoReveal_StampMismatch:       return "Stamp mismatch — regenerated placement does not match the recorded stamp.";
    case kRandoReveal_PlacementFailed:     return "Placement regeneration failed.";
    case kRandoReveal_SettingsCorrupt:     return "Embedded settings are corrupt.";
    case kRandoReveal_WriteFailed:         return "Failed writing revealed spoiler to disk.";
    default:                               return "Unknown reveal error.";
  }
}

// §62 — in-binary reveal entry point. The host wires this to a key
// binding (kKeys_RandoRevealSpoiler). When the player presses the bound
// key, we attempt to reveal the active slot's race-mode ZRSR file.
//
// No-op (returns FileNotFound with a logged note) when:
//   - no rando slot is currently active (e.g., the player is on the
//     file-select screen);
//   - the active slot's share string was never captured (Activate path
//     not exercised — shouldn't happen in practice);
//   - the resolved spoiler path doesn't exist (non-race slot, or the
//     race-mode file was already revealed / never written).
RandoRevealResult Rando_RevealActiveSlotSpoiler(void) {
  if (!g_rando_slot_active || g_rando_active_share_string[0] == '\0') {
    fprintf(stderr, "rando reveal: no active randomizer slot.\n");
    return kRandoReveal_FileNotFound;
  }
  // §62 cluster-audit MED-1 — anti-cheat gate. Race-mode's design intent
  // is the spoiler stays off-disk until post-race. An in-binary key with
  // no terminal-state gate lets a self-disciplined runner peek mid-race
  // and defeats the design. Gate the in-binary action on game-completion
  // (main_module_index 24 = Ending intro, 25 = Credits — both set after
  // Ganon dies / Triforce collected, on the Ganon-defeat / Triforce-collected
  // path).
  // The `--reveal-spoiler=<path>` CLI flow stays unconditional (no in-
  // game state to check) for tournament admins / post-race tooling.
  if (main_module_index < 24) {
    fprintf(stderr,
            "rando reveal: refused — game not yet completed "
            "(use --reveal-spoiler CLI flag for tournament admin reveals).\n");
    return kRandoReveal_FileNotFound;
  }
  RandoRevealResult r =
      Rando_RevealSpoiler(NULL, g_rando_active_share_string);
  fprintf(stderr, "rando reveal: %s\n", Rando_RevealResultDescription(r));
  return r;
}

// ---------------------------------------------------------------------------
// Rando_DrawHashIcons (tasks.md §9.4b — 5-icon visual hash widget).
//
// CRITICAL invariant: the hash input is the FULL share_string_binary, NOT
// settings_hash. Two seeds with identical settings have identical
// settings_hash bytes but DIFFERENT share strings (because seed_u64 differs),
// so the 5-icon strip differs. Deriving from settings_hash silently produces
// "all-same icons for all seeds of these settings" — the architectural error
// caught in spec round 5 and re-emphasized in the cluster-3 briefing.
// ---------------------------------------------------------------------------
#include "icon_atlas.h"      // kHashIconAtlas + kHashIconAtlasSize

// Write a single OAM entry's 4 packed fields. We CANNOT use SetOamPlain
// here because that helper also writes bytewise_extended_oam[oam - oam_buf]
// — a global indexed by the OAM entry's offset from `oam_buf`. The widget
// is also exercised by --rando-selftest with stack-local buffers, where
// `oam - oam_buf` is a wild pointer-difference that would corrupt random
// memory. Setting the 4 fields directly is safe with any buffer, and the
// bytewise_extended_oam entry isn't needed for these tiles (the entries
// are tile-sized 8x8 = `big`=0 / x<256 — both bits we'd set would be 0).
void Rando_DrawHashIcons(int x, int y,
                         struct OamEnt *oam,
                         const uint8 share_string_binary[32]) {
  // share_string_binary is the raw 31-byte share blob (magic + version +
  // settings_hash + seed_u64 + checksum); the storage is sized to 32 with
  // the last byte zero per RandoSlotHeader.share_string[]. We hash the full
  // 32 bytes — the zero pad is part of the canonical input so different
  // payloads always produce different hashes.
  uint8 digest[32];
  sha256_buffer(share_string_binary, 32, digest);
  // Emit 5 OAM tiles. Palette flag 0x32 mirrors the file-select font palette
  // (see SelectFile_Func5_DrawOams's kSelectFile_Draw_Flags2 = 0x32/0x36/0x3a)
  // so the icons read cleanly on the dark file-select background.
  OamEnt *o = (OamEnt *)oam;
  for (int i = 0; i < 5; ++i) {
    uint8 idx = (uint8)(digest[i] % (uint8)kHashIconAtlasSize);
    uint8 tile = kHashIconAtlas[idx];
    o[i].x = (uint8)(x + i * 8);
    o[i].y = (uint8)y;
    o[i].charnum = tile;
    o[i].flags = 0x32;
  }
}

// ---------------------------------------------------------------------------
// Self-tests. Always linked; invoked from --rando-selftest CLI flag.
//
// Rando_SelfCheck validates the SHA-256 impl against the two NIST FIPS 180-2
// known vectors. Rando_RunAllSelfChecks invokes every subsystem's self-test
// in sequence; any failure exits with code 2.
//
// Cost: ~tens of microseconds per binary launch when invoked; not run during
// normal game frames.
// ---------------------------------------------------------------------------
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rando_rng.h"

static int hex_eq(const uint8 *hash, const char *hex) {
  for (int i = 0; i < 32; ++i) {
    unsigned hi = (unsigned char)hex[i * 2], lo = (unsigned char)hex[i * 2 + 1];
    unsigned hv = (hi <= '9' ? hi - '0' : (hi & 0x5f) - 'A' + 10);
    unsigned lv = (lo <= '9' ? lo - '0' : (lo & 0x5f) - 'A' + 10);
    if (hash[i] != (uint8)((hv << 4) | lv)) return 0;
  }
  return 1;
}

void Rando_SelfCheck(void) {
  uint8 out[32];
  static const char kExpectedEmpty[] = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
  static const char kExpectedAbc[]   = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";

  sha256_buffer((const uint8 *)"", 0, out);
  if (!hex_eq(out, kExpectedEmpty)) {
    fprintf(stderr, "Rando_SelfCheck: SHA-256 of empty input FAILED\n");
    exit(2);
  }
  sha256_buffer((const uint8 *)"abc", 3, out);
  if (!hex_eq(out, kExpectedAbc)) {
    fprintf(stderr, "Rando_SelfCheck: SHA-256 of 'abc' FAILED\n");
    exit(2);
  }

  // Dispatch wrapper coverage (§6.1).
  // When no placement table is installed, Rando_OnLocationCheck returns
  // vanilla_item_id unchanged → Rando_DispatchVanillaGrant returns the
  // vanilla LttP code unchanged.
  Placement_Install(NULL);
  if (Rando_OnLocationCheck(8, 5) != 5) {
    fprintf(stderr, "Rando_SelfCheck: pass-through OnLocationCheck failed\n");
    exit(2);
  }
  if (Rando_DispatchVanillaGrant(8, 5, 0x00) != 0x00) {
    fprintf(stderr, "Rando_SelfCheck: pass-through DispatchVanillaGrant failed\n");
    exit(2);
  }

  // Build a synthetic placement table and verify dispatch routes to the
  // new LttP code. ITEM_BugCatchingNet=31 has dispatch:vanilla:0x21.
  static RandoPlacement entries[2];
  entries[0].location_id = 8;   // LOC_Link_s_Uncle
  entries[0].item_id = 31;      // ITEM_BugCatchingNet
  entries[1].location_id = 166; // LOC_Bottle_Merchant
  entries[1].item_id = 43;      // ITEM_BottleEmpty (vanilla:0x16) — identity
  RandoPlacementTable t = { entries, 2 };
  Placement_Install(&t);
  // Uncle now holds BugCatchingNet: vanilla code for that is 0x21
  if (Rando_DispatchVanillaGrant(8, 5, 0x00) != 0x21) {
    fprintf(stderr,
      "Rando_SelfCheck: dispatch did not translate Uncle→BugCatchingNet "
      "(expected 0x21, got 0x%02x)\n",
      (unsigned)Rando_DispatchVanillaGrant(8, 5, 0x00));
    exit(2);
  }
  // Bottle Merchant identity placement still grants 0x16
  if (Rando_DispatchVanillaGrant(166, 43, 0x16) != 0x16) {
    fprintf(stderr, "Rando_SelfCheck: identity dispatch failed\n");
    exit(2);
  }
  // Unknown location → vanilla fall-back
  if (Rando_DispatchVanillaGrant(0xFFFF, 5, 0x00) != 0x00) {
    fprintf(stderr, "Rando_SelfCheck: unknown-location fall-back failed\n");
    exit(2);
  }
  Placement_Install(NULL);

  // Vanilla-dispatch table boundaries (must return 0xFF for progressive items
  // and 0xFF for ids beyond the table).
  if (Rando_VanillaItemForRegistryId(0) != 0xFF) {  // ITEM_ProgressiveSword
    fprintf(stderr, "Rando_SelfCheck: ProgressiveSword should have no vanilla dispatch\n");
    exit(2);
  }
  if (Rando_VanillaItemForRegistryId(31) != 0x21) {  // ITEM_BugCatchingNet
    fprintf(stderr, "Rando_SelfCheck: BugCatchingNet vanilla dispatch wrong\n");
    exit(2);
  }
  if (Rando_VanillaItemForRegistryId(0xFFFFu) != 0xFF) {
    fprintf(stderr, "Rando_SelfCheck: out-of-range vanilla dispatch should be 0xFF\n");
    exit(2);
  }

  // Chest dispatch — verify both unmapped-fall-through and a known mapping.
  // Unmapped (room, ordinal) returns vanilla unchanged.
  if (Rando_ChestDispatch(0xFFFE, 5, 0x05) != 0x05) {
    fprintf(stderr, "Rando_SelfCheck: chest dispatch did not fall back on unmapped\n");
    exit(2);
  }
  // Spot-check the generated kRandoChestLookup table against a curated subset
  // of (room, ordinal, expected_LOC_*) triples. These triples are derived
  // from audit.md §0.3.5 + the in-room ordering of the vanilla chest
  // table at $81e96e. If chest_lookup() returns the right LOC_*, the
  // dispatcher's caller bookkeeping is exercised by the placement test
  // above; here we just verify the lookup itself.
  {
    static const struct { uint16 room; uint8 ord; uint16 expected; } kCases[] = {
      {  17, 0, LOC_Sewers_Secret_Room_Left },
      {  17, 1, LOC_Sewers_Secret_Room_Middle },
      {  17, 2, LOC_Sewers_Secret_Room_Right },
      {  18, 0, LOC_Sanctuary },
      {  22, 0, LOC_Pyramid_Fairy_Left },
      {  22, 1, LOC_Pyramid_Fairy_Right },
      { 114, 0, LOC_Hyrule_Castle_Map_Chest },
      { 168, 0, LOC_Eastern_Palace_Compass_Chest },
      { 169, 0, LOC_Eastern_Palace_Big_Chest },
      {  47, 4, LOC_Kakariko_Well_Bottom },
      // unmapped fall-through (room 0xFFFE never appears in the table)
      { 0xFFFE, 0, 0xFFFFu },
    };
    for (int i = 0; i < (int)(sizeof(kCases)/sizeof(kCases[0])); ++i) {
      uint16 got = chest_lookup(kCases[i].room, kCases[i].ord);
      if (got != kCases[i].expected) {
        fprintf(stderr,
          "Rando_SelfCheck: chest_lookup(room=%u, ord=%u) = %u, expected %u\n",
          (unsigned)kCases[i].room, (unsigned)kCases[i].ord,
          (unsigned)got, (unsigned)kCases[i].expected);
        exit(2);
      }
    }
  }

  // §6.3 + §6.2 integration: install a direct-grant placement (TriforcePiece)
  // at a known chest location and verify Rando_ChestDispatch returns
  // kRandoLttpSkip. Regression guard for the bug where the universal chest
  // hook in player.c forgot to check Rando_ShouldSkipReceive() and would
  // pass 0xFE to Link_ReceiveItem (OOB index into 76-byte dispatch tables).
  {
    uint16 chest_loc = chest_lookup(114, 0);  // Hyrule Castle - Map Chest
    if (chest_loc == 0xFFFFu) {
      fprintf(stderr, "Rando_SelfCheck: chest_lookup(114,0) returned 0xFFFF (table broken?)\n");
      exit(2);
    }
    static RandoPlacement entries[1];
    entries[0].location_id = chest_loc;
    entries[0].item_id = 52;  // ITEM_TriforcePiece
    RandoPlacementTable t = { entries, 1 };
    Placement_Install(&t);
    g_rando_triforce_piece_count = 0;
    uint8 lttp = Rando_ChestDispatch(114, 0, 0x05);  // vanilla map = 0x05
    if (lttp != kRandoLttpSkip) {
      fprintf(stderr,
        "Rando_SelfCheck: chest dispatch with TriforcePiece placement should return"
        " kRandoLttpSkip (got 0x%02x) — Link_PerformOpenChest would OOB-index dispatch tables\n",
        (unsigned)lttp);
      exit(2);
    }
    if (g_rando_triforce_piece_count != 1) {
      fprintf(stderr, "Rando_SelfCheck: chest dispatch with TriforcePiece should tick counter\n");
      exit(2);
    }
    Placement_Install(NULL);
    g_rando_triforce_piece_count = 0;
  }

  // §6.2 TriforcePiece counter: install a placement that grants Triforce
  // Piece at Bottle Merchant, dispatch, verify counter ticked AND the
  // dispatch returned kRandoLttpSkip (caller bypasses Link_ReceiveItem).
  {
    static RandoPlacement entries[1];
    entries[0].location_id = 166;  // Bottle Merchant
    entries[0].item_id = 52;       // ITEM_TriforcePiece
    RandoPlacementTable t = { entries, 1 };
    Placement_Install(&t);
    g_rando_triforce_piece_count = 0;
    uint8 lttp = Rando_DispatchVanillaGrant(166, ITEM_BottleEmpty, 0x16);
    if (lttp != kRandoLttpSkip) {
      fprintf(stderr, "Rando_SelfCheck: TriforcePiece dispatch should return kRandoLttpSkip (got 0x%02x)\n", (unsigned)lttp);
      exit(2);
    }
    if (g_rando_triforce_piece_count != 1) {
      fprintf(stderr, "Rando_SelfCheck: TriforcePiece dispatch should tick counter\n");
      exit(2);
    }
    Placement_Install(NULL);
    g_rando_triforce_piece_count = 0;
  }

  // §6.2 HalfMagic/QuarterMagic direct-write tests. Placement HalfMagic at
  // Bottle Merchant; dispatch should write link_magic_consumption=1 and
  // return kRandoLttpSkip.
  {
    static RandoPlacement entries[1];
    entries[0].location_id = 166;
    entries[0].item_id = ITEM_HalfMagic;  // 41
    RandoPlacementTable t = { entries, 1 };
    Placement_Install(&t);
    link_magic_consumption = 0;
    uint8 lttp = Rando_DispatchVanillaGrant(166, ITEM_BottleEmpty, 0x16);
    if (lttp != kRandoLttpSkip) {
      fprintf(stderr, "Rando_SelfCheck: HalfMagic dispatch should return kRandoLttpSkip (got 0x%02x)\n", (unsigned)lttp);
      exit(2);
    }
    if (link_magic_consumption != 1) {
      fprintf(stderr, "Rando_SelfCheck: HalfMagic dispatch should set link_magic_consumption=1\n");
      exit(2);
    }
    // Same site with QuarterMagic should escalate.
    entries[0].item_id = ITEM_QuarterMagic;  // 42
    Placement_Install(&t);
    Rando_DispatchVanillaGrant(166, ITEM_BottleEmpty, 0x16);
    if (link_magic_consumption != 2) {
      fprintf(stderr, "Rando_SelfCheck: QuarterMagic dispatch should set link_magic_consumption=2\n");
      exit(2);
    }
    Placement_Install(NULL);
    link_magic_consumption = 0;
  }

  // §6.2 prize-item direct-write tests. Placement Prize_Crystal4 at Bottle
  // Merchant; dispatch should OR bit 0x40 into link_has_crystals.
  {
    static RandoPlacement entries[1];
    entries[0].location_id = 166;
    entries[0].item_id = ITEM_Prize_Crystal4;  // 117
    RandoPlacementTable t = { entries, 1 };
    Placement_Install(&t);
    link_has_crystals = 0;
    uint8 lttp = Rando_DispatchVanillaGrant(166, ITEM_BottleEmpty, 0x16);
    if (lttp != kRandoLttpSkip) {
      fprintf(stderr, "Rando_SelfCheck: Crystal4 dispatch should return kRandoLttpSkip (got 0x%02x)\n", (unsigned)lttp);
      exit(2);
    }
    if ((link_has_crystals & 0x40) == 0) {
      fprintf(stderr, "Rando_SelfCheck: Crystal4 dispatch should OR 0x40 into link_has_crystals\n");
      exit(2);
    }
    // Same site with Prize_GreenPendant.
    entries[0].item_id = ITEM_Prize_GreenPendant;
    Placement_Install(&t);
    link_which_pendants = 0;
    Rando_DispatchVanillaGrant(166, ITEM_BottleEmpty, 0x16);
    if ((link_which_pendants & 0x04) == 0) {
      fprintf(stderr, "Rando_SelfCheck: GreenPendant dispatch should OR 0x04 into link_which_pendants\n");
      exit(2);
    }
    Placement_Install(NULL);
    link_has_crystals = 0;
    link_which_pendants = 0;
  }

  // §6.2 per-placed-dungeon BigKey/Map/Compass direct-write tests.
  // Placement BigKey_GanonsTower (76) at Bottle Merchant; dispatch should
  // OR (0x8000 >> 13) = 0x0004 into link_bigkey (GT's game-side bit slot).
  {
    static RandoPlacement entries[1];
    entries[0].location_id = 166;
    entries[0].item_id = ITEM_BigKey_GanonsTower;  // 76
    RandoPlacementTable t = { entries, 1 };
    Placement_Install(&t);
    link_bigkey = 0;
    uint8 lttp = Rando_DispatchVanillaGrant(166, ITEM_BottleEmpty, 0x16);
    if (lttp != kRandoLttpSkip) {
      fprintf(stderr, "Rando_SelfCheck: BigKey_GT dispatch should return kRandoLttpSkip\n");
      exit(2);
    }
    if (link_bigkey != 0x0004) {
      fprintf(stderr, "Rando_SelfCheck: BigKey_GT dispatch should set link_bigkey=0x0004 (got 0x%04x)\n", (unsigned)link_bigkey);
      exit(2);
    }
    // Compass_EasternPalace (88) → game-side dungeon 2 → bit (0x8000 >> 2) = 0x2000
    entries[0].item_id = ITEM_Compass_EasternPalace;
    Placement_Install(&t);
    link_compass = 0;
    Rando_DispatchVanillaGrant(166, ITEM_BottleEmpty, 0x16);
    if (link_compass != 0x2000) {
      fprintf(stderr, "Rando_SelfCheck: Compass_EP dispatch should set link_compass=0x2000 (got 0x%04x)\n", (unsigned)link_compass);
      exit(2);
    }
    // Map_HCE (124) → dungeon 0 → bit 0x8000
    entries[0].item_id = 124;
    Placement_Install(&t);
    link_dungeon_map = 0;
    Rando_DispatchVanillaGrant(166, ITEM_BottleEmpty, 0x16);
    if (link_dungeon_map != 0x8000) {
      fprintf(stderr, "Rando_SelfCheck: Map_HCE dispatch should set link_dungeon_map=0x8000\n");
      exit(2);
    }
    Placement_Install(NULL);
    link_bigkey = link_compass = link_dungeon_map = 0;
  }

  // §6.2 follow-on — per-dungeon SmallKey counter direct-write. Under
  // key-shuffle a small key can be collected in a dungeon other than its own
  // (e.g. a Tower-of-Hera key found during the Hyrule Castle escape); the
  // grant must credit the DESTINATION dungeon's saved counter, not the live
  // current-dungeon counter.
  {
    static RandoPlacement entries[1];
    entries[0].location_id = 166;
    entries[0].item_id = ITEM_SmallKey_TowerOfHera;  // 56 → game-side dungeon 10
    RandoPlacementTable t = { entries, 1 };

    uint16 saved_palace = cur_palace_index_x2;
    uint8 saved_keys = link_num_keys;

    // (a) ToH key collected while in the Hyrule Castle escape (raw
    // cur_palace_index_x2 = 0). The escape's live counter must be untouched;
    // only ToH's saved slot (game-side index 10) ticks up.
    cur_palace_index_x2 = 0;
    link_num_keys = 3;  // pretend the escape has 3 keys in hand
    link_keys_earned_per_dungeon[10] = 0;
    Placement_Install(&t);
    uint8 lttp = Rando_DispatchVanillaGrant(166, ITEM_BottleEmpty, 0x16);
    if (lttp != kRandoLttpSkip) {
      fprintf(stderr, "Rando_SelfCheck: SmallKey_ToH dispatch should return kRandoLttpSkip\n");
      exit(2);
    }
    if (link_num_keys != 3) {
      fprintf(stderr, "Rando_SelfCheck: SmallKey_ToH in escape must not touch live key count (got %u)\n",
              (unsigned)link_num_keys);
      exit(2);
    }
    if (link_keys_earned_per_dungeon[10] != 1) {
      fprintf(stderr, "Rando_SelfCheck: SmallKey_ToH should credit ToH slot 10 (got %u)\n",
              (unsigned)link_keys_earned_per_dungeon[10]);
      exit(2);
    }

    // (b) ToH key collected while standing in Tower of Hera (raw
    // cur_palace_index_x2 = 20 = game-side index 10). The live counter moves
    // and the saved slot stays in sync.
    cur_palace_index_x2 = 20;
    link_num_keys = 1;
    link_keys_earned_per_dungeon[10] = 1;
    Placement_Install(&t);
    Rando_DispatchVanillaGrant(166, ITEM_BottleEmpty, 0x16);
    if (link_num_keys != 2 || link_keys_earned_per_dungeon[10] != 2) {
      fprintf(stderr, "Rando_SelfCheck: SmallKey_ToH in ToH should bump live+slot to 2 (got live=%u slot=%u)\n",
              (unsigned)link_num_keys, (unsigned)link_keys_earned_per_dungeon[10]);
      exit(2);
    }

    Placement_Install(NULL);
    cur_palace_index_x2 = saved_palace;
    link_num_keys = saved_keys;
    link_keys_earned_per_dungeon[10] = 0;
  }

  // §9.4b — 5-icon hash widget. Two share strings with identical settings
  // (zero settings_hash) but DIFFERENT seed_u64 MUST produce different icon
  // strips. This is the regression guard for the "input is share_string,
  // not settings_hash" architectural error caught in spec round 5.
  {
    uint8 share_a[32]; memset(share_a, 0, sizeof(share_a));
    uint8 share_b[32]; memset(share_b, 0, sizeof(share_b));
    // bytes 21..28 hold seed_u64 (LE) per the share-string layout. Different
    // seeds → different SHA-256 → different icon strip.
    share_a[21] = 0x01;
    share_b[21] = 0x02;
    OamEnt buf_a[5], buf_b[5];
    memset(buf_a, 0, sizeof(buf_a));
    memset(buf_b, 0, sizeof(buf_b));
    Rando_DrawHashIcons(0, 0, (struct OamEnt *)buf_a, share_a);
    Rando_DrawHashIcons(0, 0, (struct OamEnt *)buf_b, share_b);
    int any_differ = 0;
    for (int i = 0; i < 5; ++i) {
      if (buf_a[i].charnum != buf_b[i].charnum) { any_differ = 1; break; }
    }
    if (!any_differ) {
      fprintf(stderr, "Rando_SelfCheck: hash icons identical for distinct seeds — "
                      "widget is hashing settings_hash, not share_string\n");
      exit(2);
    }
    // Identical share strings must produce identical icon strips.
    OamEnt buf_c[5];
    memset(buf_c, 0, sizeof(buf_c));
    Rando_DrawHashIcons(0, 0, (struct OamEnt *)buf_c, share_a);
    for (int i = 0; i < 5; ++i) {
      if (buf_a[i].charnum != buf_c[i].charnum) {
        fprintf(stderr, "Rando_SelfCheck: hash icons non-deterministic at index %d\n", i);
        exit(2);
      }
    }
    // Atlas size sanity: every emitted tile MUST appear in kHashIconAtlas.
    // §9 cluster-3 audit LOW: the original loop's ternary always evaluated
    // to 0 and discarded the result via (void)expected — effectively dead
    // code. Rewrite as a real membership check.
    for (int i = 0; i < 5; ++i) {
      bool found = false;
      for (int j = 0; j < kHashIconAtlasSize; ++j) {
        if (buf_a[i].charnum == kHashIconAtlas[j]) { found = true; break; }
      }
      if (!found) {
        fprintf(stderr, "Rando_SelfCheck: hash icon tile 0x%02x at index %d not in kHashIconAtlas\n",
                (unsigned)buf_a[i].charnum, i);
        exit(2);
      }
    }
  }
}

void Rando_RunAllSelfChecks(void) {
  Rando_SelfCheck();
  Rando_Rng_SelfCheck();
  Share_SelfCheck();
  Settings_SelfCheck();
  Logic_SelfCheck();
  Placement_SelfCheck();
  Shuffles_SelfCheck();
  RandoSave_SelfCheck();
  RandoSnapshotTail_SelfCheck();
  TextField_SelfCheck();
  Hints_SelfCheck();
  fprintf(stderr, "Rando_RunAllSelfChecks: all subsystems OK.\n");
}
