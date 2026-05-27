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

  // Placement_Lookup returns vanilla_item_id when no active placement table
  // is installed (rando mode inactive), or when location_id is not in the
  // active table. See rando_placement.c.
  return Placement_Lookup(location_id, vanilla_item_id);
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
      uint8 tier = link_item_bow;
      if (tier >= 2) return 0xFF;
      // Bow=0x0b (tier 1) → SilverArrowUpgrade=0x29 (tier 2)
      return (uint8)((tier == 0) ? 0x0b : 0x29);
    }
    // Multi-tier rupees: vanilla LttP receive codes per Ancilla_AddRupees
    // (ancilla.c:6963). kGiveRupeeGift_Tab[5] = {1, 5, 20, 100, 50}:
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
    // them. kValueToGiveItemTo[32]=-1 means code 0x20's "magic" branch at
    // misc.c:751 runs special palette/cape logic but does NOT write to
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
    // SilverArrowUpgrade: LttP code 0x29 grants silver arrows by setting
    // link_item_bow=2 (per misc.c index 41). Works without progressive
    // ordering — direct silver-arrows upgrade.
    case ITEM_SilverArrowUpgrade: return 0x29;
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

// §6.2 per-placed-dungeon counter helpers. The vanilla LttP dispatcher at
// misc.c:746-747/772-775 indexes by `cur_palace_index_x2 >> 1` (the player's
// current dungeon). For rando placements where a key/map/compass belongs to
// a DIFFERENT dungeon than the player's current one, we have to write to
// that specific dungeon's bit/counter ourselves.
//
// Returns 1 if the placed item is a dungeon item and was direct-written.
// Caller treats this as "skip Link_ReceiveItem" via kRandoLttpSkip.
//
// Mappings (matching dungeon_id_for_item in rando_placement.c, which
// mirrors the kBigKeys / kMaps / kCompasses ordering):
//   SmallKey ids 53..65 contiguous → dungeon_id = id - 53 (HCE..GT, no skip)
//   BigKey ids 66..76 → dungeon ids 1,2,3,5,6,7,8,9,10,11,12 (skip HCE+HCT)
//   Map_HCE = 124 → dungeon 0
//   Map ids 77..87 → dungeon ids 1,2,3,5,6,7,8,9,10,11,12 (skip HCT)
//   Compass ids 88..98 → dungeon ids 1,2,3,5,6,7,8,9,10,11,12 (skip HCE+HCT)
//
// `link_bigkey` / `link_dungeon_map` / `link_compass` are uint16 bitfields.
// Per misc.c:746-747 special case for codes 0x25/0x32/0x33:
//     WORD(*p) |= 0x8000 >> (BYTE(cur_palace_index_x2) >> 1)
// So dungeon_id D maps to bit (0x8000 >> D). HCE=0 → 0x8000, EP=1 → 0x4000,
// ..., GT=12 → 0x0008. This matches the HUD's per-dungeon icon table.
static uint16 dungeon_bit_for_map_or_compass(uint8 dungeon_id) {
  if (dungeon_id >= 16) return 0;
  return (uint16)(0x8000u >> dungeon_id);
}

static uint8 dungeon_id_for_item_local(uint16 registry_id) {
  // SmallKey 53..65: HCE..GT in order (no skips).
  if (registry_id >= 53 && registry_id <= 65) return (uint8)(registry_id - 53);
  static const uint8 kBigKeyDungeon[11] = { 1, 2, 3, 5, 6, 7, 8, 9, 10, 11, 12 };
  if (registry_id >= 66 && registry_id <= 76) return kBigKeyDungeon[registry_id - 66];
  if (registry_id == 124) return 0;  // Map_HCE
  static const uint8 kMapDungeon[11] = { 1, 2, 3, 5, 6, 7, 8, 9, 10, 11, 12 };
  if (registry_id >= 77 && registry_id <= 87) return kMapDungeon[registry_id - 77];
  static const uint8 kCompassDungeon[11] = { 1, 2, 3, 5, 6, 7, 8, 9, 10, 11, 12 };
  if (registry_id >= 88 && registry_id <= 98) return kCompassDungeon[registry_id - 88];
  return 0xFF;
}

// Per-placed-dungeon counter direct-grant. Returns 1 on success.
// Note: SmallKey writes only to per-dungeon Phase B work — Phase A1 falls
// back to the current-dungeon vanilla path since `link_num_keys` is a
// single counter (the per-dungeon array `kRam_DungeonKeysByDungeon[13]`
// would land with §6.2 follow-on work). For now SmallKey returns 0 here
// and the dispatcher falls through to the current-dungeon vanilla path.
static int dungeon_item_direct_grant(uint16 registry_id) {
  uint8 dungeon = dungeon_id_for_item_local(registry_id);
  if (dungeon == 0xFF || dungeon >= 13) return 0;

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
  // SmallKey — Phase A1 falls back to current-dungeon dispatcher path
  // (per-dungeon counter table is §6.2 follow-on).
  return 0;
}

// §6.2 prize-item direct-grant. The 7 crystals + 3 pendants OR into
// `link_has_crystals` / `link_which_pendants`. Each prize has a fixed bit
// per vanilla LttP convention (kDungeonCrystalPendantBit[] indexed by
// dungeon — but since prizes can be shuffled to any dungeon, we map by
// PRIZE id, not by dungeon).
//
// Per ALTTPR Prize\Pendant / Prize\Crystal classes and the vanilla bit
// allocations at misc.c:738-740 (pendant) and ancilla.c:3855 (crystal):
//   GreenPendant → link_which_pendants bit 2 (mask 0x04)
//   RedPendant   → link_which_pendants bit 0 (mask 0x01)
//   BluePendant  → link_which_pendants bit 1 (mask 0x02)
//   Crystal1 (PoD)  → link_has_crystals bit 4 (mask 0x10)
//   Crystal2 (SP)   → link_has_crystals bit 1 (mask 0x02)
//   Crystal3 (SW)   → link_has_crystals bit 0 (mask 0x01)
//   Crystal4 (TT)   → link_has_crystals bit 6 (mask 0x40)
//   Crystal5 (IP)   → link_has_crystals bit 2 (mask 0x04)
//   Crystal6 (MM)   → link_has_crystals bit 5 (mask 0x20)
//   Crystal7 (TR)   → link_has_crystals bit 3 (mask 0x08)
// Bits derived by cross-referencing kDungeonCrystalPendantBit[13] in
// src/zelda_rtl.c:50 against the vanilla dungeon→prize assignment in
// app/Region/Standard/<Dungeon>.php.
//
// Returns 1 on success.
static int prize_item_direct_grant(uint16 registry_id) {
  switch (registry_id) {
    case ITEM_Prize_GreenPendant: link_which_pendants |= 0x04; return 1;
    case ITEM_Prize_RedPendant:   link_which_pendants |= 0x01; return 1;
    case ITEM_Prize_BluePendant:  link_which_pendants |= 0x02; return 1;
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
// Link_ReceiveItem in vanilla — the only writer is sprite_main.c:11210
// (Magic Bat handler) writing link_magic_consumption = 1. Rando placements
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

uint8 Rando_DispatchVanillaGrant(uint16 location_id,
                                 uint16 vanilla_registry_id,
                                 uint8 vanilla_lttp_code) {
  uint16 placed = Rando_OnLocationCheck(location_id, vanilla_registry_id);
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

  // §6.2 prize-item direct-write (crystals + pendants). The vanilla path at
  // ancilla.c:3855 ORs the current dungeon's bit into link_has_crystals; for
  // rando placements at non-boss slots we set the prize's bit directly.
  if (prize_item_direct_grant(placed)) {
    return kRandoLttpSkip;
  }

  // §6.2 per-placed-dungeon BigKey/Map/Compass direct-write. Vanilla LttP's
  // dispatcher writes to the CURRENT dungeon's bit; for rando placements
  // where the placed item belongs to a DIFFERENT dungeon, we have to write
  // that specific dungeon's bit. SmallKey falls through here (Phase B work).
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

// ---------------------------------------------------------------------------
// Rando_BumpReachabilityCounter — invalidates the tracker's cached
// reachability. Phase A0 stub: increment the counter. The tracker (task 10.2)
// will consume the value.
// ---------------------------------------------------------------------------
void Rando_BumpReachabilityCounter(void) {
  g_reachability_state_counter++;
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
// Deliberately NOT emitted from within `Rando_DispatchVanillaGrant` — the
// caller knows whether its own code path already provides visual context
// (e.g., the §6.6 boss-kill spawns a FallingPrize regardless of sentinel,
// so the player sees that sprite). Pushing the confirmation to the call
// site lets each integration choose whether to add the cue.
// ---------------------------------------------------------------------------
void Rando_ShowDirectGrantConfirmation(void) {
  sound_effect_2 = (uint8)(Link_CalculateSfxPan() | 0x0f);
  Hud_RefreshIcon();
}

void Rando_ReceiveOrConfirm(uint8 lttp_code) {
  if (Rando_ShouldSkipReceive(lttp_code)) {
    Rando_ShowDirectGrantConfirmation();
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
// Dungeon ID layout (cur_palace_index_x2 >> 1):
//   0 HCE  (no boss; Sanctuary chest is the heart container slot)
//   1 EP   2 DP   3 TH
//   4 HCT  (Agahnim; not a heart-drop boss — handled separately)
//   5 PoD  6 SP   7 SW   8 TT   9 IP  10 MM  11 TR
//  12 GT   (Agahnim 2; same as HCT path)
// ---------------------------------------------------------------------------
uint16 Rando_GetBossHeartLocation(uint8 dungeon_id) {
  static const uint16 kBossHeartByDungeon[13] = {
    0xFFFFu,                       // 0  HCE
    LOC_Eastern_Palace_Boss,       // 1  EP
    LOC_Desert_Palace_Boss,        // 2  DP
    LOC_Tower_of_Hera_Boss,        // 3  TH
    0xFFFFu,                       // 4  HCT (Agahnim path)
    LOC_Palace_of_Darkness_Boss,   // 5  PoD
    LOC_Swamp_Palace_Boss,         // 6  SP
    LOC_Skull_Woods_Boss,          // 7  SW
    LOC_Thieves_Town_Boss,         // 8  TT
    LOC_Ice_Palace_Boss,           // 9  IP
    LOC_Misery_Mire_Boss,          // 10 MM
    LOC_Turtle_Rock_Boss,          // 11 TR
    0xFFFFu                        // 12 GT (Agahnim 2 path)
  };
  if (dungeon_id >= 13) return 0xFFFFu;
  return kBossHeartByDungeon[dungeon_id];
}

uint16 Rando_GetBossPrizeLocation(uint8 dungeon_id) {
  static const uint16 kBossPrizeByDungeon[13] = {
    0xFFFFu,                       // 0  HCE
    LOC_Eastern_Palace_Prize,      // 1  EP
    LOC_Desert_Palace_Prize,       // 2  DP
    LOC_Tower_of_Hera_Prize,       // 3  TH
    0xFFFFu,                       // 4  HCT
    LOC_Palace_of_Darkness_Prize,  // 5  PoD
    LOC_Swamp_Palace_Prize,        // 6  SP
    LOC_Skull_Woods_Prize,         // 7  SW
    LOC_Thieves_Town_Prize,        // 8  TT
    LOC_Ice_Palace_Prize,          // 9  IP
    LOC_Misery_Mire_Prize,         // 10 MM
    LOC_Turtle_Rock_Prize,         // 11 TR
    0xFFFFu                        // 12 GT
  };
  if (dungeon_id >= 13) return 0xFFFFu;
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
}

void Rando_DeactivateSlot(void) {
  Placement_Install(NULL);
  g_session_placement_table.entries = NULL;
  g_session_placement_table.count = 0;
  g_rando_slot_active = 0;
  g_wanted_zelda_features1 &= ~(uint32)kFeatures1_RandomizerActive;
  enhanced_features1 &= ~(uint32)kFeatures1_RandomizerActive;
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
  // OR (0x8000 >> 12) = 0x0008 into link_bigkey (GT's bit slot).
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
    if (link_bigkey != 0x0008) {
      fprintf(stderr, "Rando_SelfCheck: BigKey_GT dispatch should set link_bigkey=0x0008 (got 0x%04x)\n", (unsigned)link_bigkey);
      exit(2);
    }
    // Compass_EasternPalace (88) → dungeon 1 → bit (0x8000 >> 1) = 0x4000
    entries[0].item_id = ITEM_Compass_EasternPalace;
    Placement_Install(&t);
    link_compass = 0;
    Rando_DispatchVanillaGrant(166, ITEM_BottleEmpty, 0x16);
    if (link_compass != 0x4000) {
      fprintf(stderr, "Rando_SelfCheck: Compass_EP dispatch should set link_compass=0x4000 (got 0x%04x)\n", (unsigned)link_compass);
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
  fprintf(stderr, "Rando_RunAllSelfChecks: all subsystems OK.\n");
}
