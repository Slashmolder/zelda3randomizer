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
#include "shuffle_boss.h"   // BossShuffle_Generate/_Deactivate/_SelfCheck (Slice 7)
#include "shuffle_drops.h"  // DropShuffle_Generate/_Deactivate/_SelfCheck (Slice 8)
#include "shuffle_enemies.h"  // EnemyShuffle_Generate/_Deactivate/_SelfCheck (enemy shuffle)
#include "shuffle_doors.h"   // DoorShuffle_Generate/LayoutDigest (door shuffle)
#include "door_runtime.h"    // DoorRt_* (door-shuffle runtime redirect)
#include "rando_save.h"
#include "rando_generate.h"  // RandoGenerate_SelfCheck (slot SRAM-init self-test)
#include "rando_snapshot_tail.h"
#include "rando_textfield.h"
#include "item_ids.h"
#include "location_ids.h"
#include "chest_lookup.h"  // (room, ordinal) -> LOC_*; §6.3 codegen
#include "direct_grant_icons.h"  // kDirectGrantIcons[] (Phase B Slice 9)
#include "rando_hints.h"  // Rando_ClearHints (Phase B Slice 5)
#include "shuffle_entrance.h"  // Phase C entrance shuffle (overlay + self-check)
#include "inverted_entrances.h"  // #82 static Inverted entrance/exit override
#include "inverted_maps.h"  // InvertedHoleBlocks_Install (no-art Ganon pit shadow)
#include "shuffle_cosmetic.h"  // Cosmetic_SetSeed (cosmetic_seed=0 -> slot seed)
#include "../ancilla.h"  // AncillaAdd_RandoIconReceipt (Phase B Slice 9)
#include "../config.h"  // g_config.cosmetic_seed
#include "../types.h"
#include "../variables.h"  // §6.2 progressive-dispatch reads link_sword_type etc.
#include "../assets.h"     // Phase C entrance overlay: g_asset_ptrs[126] / kOverworld_Entrance_Id
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
    // LttP code grants Rupoor, so progressive_to_lttp returns 0xFF here — the
    // grant is handled directly by the ITEM_Rupoor case in
    // Rando_DispatchVanillaGrant (rupee drain + kRandoLttpSkip). It must NOT
    // fall through to the vanilla-item fallback: at chests whose US-ROM vanilla
    // item is progression (Zelda's Cell / Link's House / Secret Passage = Lamp)
    // that fallback granted a duplicate real item.
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

// Retro genericKeys grant — a GenericKey (id 125, ROM 0xAF) pickup feeds the
// single shared small-key pool, not a per-dungeon counter. Bump the persisted
// shared counter `link_generic_keys` (link_keys_earned_per_dungeon[15] = ALTTPR
// $7EF38B); when the player is standing in a dungeon the LIVE counter
// link_num_keys mirrors the shared one (loaded on entry, write-through on
// consume), so resync it too. Outside a dungeon link_num_keys is the 0xff
// sentinel and is left alone — it reloads from the shared slot on the next
// dungeon entry. Mirrors ALTTPR newitems.asm KeyGK (+1 to CurrentGenericKeys).
static void rando_grant_generic_key(void) {
  // rando-exempt: grant — Retro shared generic small-key counter.
  if (link_generic_keys < 0xfe) link_generic_keys += 1;
  if ((uint8)cur_palace_index_x2 != 0xff) {
    // rando-exempt: grant — live counter mirrors the shared pool in-dungeon.
    link_num_keys = link_generic_keys;
  }
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
  if (registry_id == ITEM_HalfMagic || registry_id == ITEM_QuarterMagic) {
    // Strictly progressive: each magic upgrade advances ONE tier
    // (full=0 -> half=1 -> quarter=2), regardless of which item it is, so the
    // 1st pickup is always 1/2 and the 2nd is always 1/4 in any collection
    // order — no pickup is ever wasted. (Deliberate local choice: ALTTPR's
    // QuarterMagic jumps straight to 1/4; this caps at +1 tier instead. Never
    // downgrades because it only ever increments.)
    if (link_magic_consumption < 2) link_magic_consumption++;
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

  // Retro genericKeys shared small-key. GenericKey (125) has dispatch
  // `vanilla:0xAF`, which the LttP receive path does NOT understand — route it to
  // the shared-counter grant instead and skip the vanilla dispatcher. Only ever
  // placed under genericKeys (Retro), so no settings gate is needed here.
  if (placed == ITEM_GenericKey) {
    rando_grant_generic_key();
    return kRandoLttpSkip;
  }

  // Rupoor (ALTTPR-only junk item, dispatch `direct_rupoor`). Vanilla ALTTP has
  // no Rupoor, so NO Link_ReceiveItem code grants it. Without a handler here it
  // falls through to the vanilla-LttP fallback at the bottom of this function
  // and grants the CHEST'S US-ROM vanilla item instead — and three chests
  // (Hyrule Castle - Zelda's Cell, Link's House, Secret Passage) carry US ROM
  // item byte 0x12 = LAMP. That silently turned a junk Rupoor into a duplicate
  // progression Lamp ("got the Lamp twice"). Match ALTTPR (newitems.asm
  // .rupoor): drain RupoorDeduction (=10) rupees from link_rupees_goal; the HUD
  // ticker (hud.c) animates the displayed count down and plays the drain sfx.
  // Clamp at 0 — if the goal underflowed below link_rupees_actual the ticker's
  // fill branch would instead race the count UPWARD (the existing shop/cost
  // sites use the same `>= cost` guard).
  if (placed == ITEM_Rupoor) {
    link_rupees_goal = (link_rupees_goal >= 10) ? (uint16)(link_rupees_goal - 10) : 0;
    return kRandoLttpSkip;
  }

  uint8 lttp = Rando_VanillaItemForRegistryId(placed);
  if (lttp != 0xFF) {
    // Boomerang is strictly PROGRESSIVE under rando: the 1st collected is always
    // blue, the 2nd always red — regardless of which color item the placer put
    // at this location (see Rando_GrantBoomerang). This returned LttP code drives
    // BOTH the grant routing (-> Rando_GrantBoomerang, already progressive) AND
    // the receive-animation graphic + "You got the … Boomerang" text. Without
    // this remap, collecting the RED item FIRST plays the magical-boomerang
    // animation even though the player is actually granted blue. Re-derive the
    // shown color from current ownership so it always matches the tier being
    // granted: blue (0x0c) until blue is owned, then red (0x2a). Computed
    // pre-grant — g_rando_boomerang_owned / link_item_boomerang still hold the
    // state before this pickup, exactly as Rando_GrantBoomerang reads them.
    if (placed == ITEM_BlueBoomerang || placed == ITEM_RedBoomerang) {
      bool blue_owned = (g_rando_boomerang_owned & kRandoBoomerang_Blue) ||
                        link_item_boomerang >= 1;
      lttp = blue_owned ? 0x2a : 0x0c;
    }
    return lttp;
  }

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

// === Phase B Slice 3b — Retro TakeAny runtime ===
//
// Runtime cave table. Index = cave index (MUST match the generator + registry:
// location id = 266 + 2*cave + slot). door_id = ALTTPR overworld door
// (= row-index lx + 1); host_entrance = the redirected destination entrance
// (0x58 -> host room 0x112, 0x60 -> 0x10F, 0x46 -> 0x11F). Verified across all
// 31 caves against app/Region/Standard/** (door_id-1 == the 0xDBB73 redirect
// offset). See add-rando-retro-takeany/design.md §3.
#define kRandoTakeAnyCaveCount 31
#define kRandoTakeAnyLocBase   266
uint8 g_rando_takeany_door_id;  // transient; set by Overworld_UseEntrance
// Phase C Stage 2 — dungeon entrance-shuffle coupling. Set at the overworld entry
// hook when a SHUFFLED dungeon door is entered; the dungeon-exit room-keyed search
// uses it instead of the loaded dungeon's room so the player returns to the SOURCE
// door (avoids stranding, e.g. exiting on Ice Palace's lake without flippers).
// 0 = no override. Consumed (cleared) by the exit path.
uint16 g_rando_entrance_exit_room;
// Phase C Stage 3 (cross-category) coupling: set at the entry hook when a CAVE
// source door is redirected to a DUNGEON interior (cave→dungeon). The loaded
// dungeon room takes the room-keyed SEARCH exit branch, which would NOT return
// to the cave; this flag forces the cached-exit branch (the *_exit shadow vars,
// cached at entry, hold the source cave door's overworld position) so the player
// returns to the cave door. 0 = normal. Consumed by the exit path.
uint8 g_rando_entrance_force_cached;
// Source cave's room for the force-cached exit above. Set TOGETHER with
// g_rando_entrance_force_cached inside Rando_EntranceForceCachedExit, consumed at
// the overworld-exit top. The cached *_exit shadow vars hold the cave DOOR
// position but not its ROOM, so the loaded dungeon's room (< 0x124) lingers in
// dungeon_room_index and skews LoadCachedEntranceProperties' vanilla room-keyed
// Y-adjust; restoring the cave room here (mirroring Rando_ReplayCaveArrival on
// the decoupled path) keys the adjust off the cave. 0 = none. (PLAYTEST-PENDING.)
uint16 g_rando_force_cached_room;
// Inverted spawn-select respawn redirect — see rando.h. Set by
// Module1B_SpawnSelect (Inverted slots only), consumed by the next
// LoadOverworldFromDungeon (forces the anchor exit screen |= 0x40 → Dark World).
uint8 g_rando_inverted_spawn_redirect;

// add-rando-inverted-dark-chapel-spawn: rename the post-Agahnim spawn-select
// options for an Inverted slot — "Sanctuary" -> "Dark Chapel", "The Mountain
// Cave" -> "Dark Mountain" (ALTTPR labels). Operates on the FINISHED character
// buffer (after Text_LoadCharacterBuffer's vanilla decode, so the player-name
// expansion, [Position] command, and menu structure are all handled normally):
// just swap the location word's font-byte run in place. The font-byte runs are
// the US text-alphabet encodings (assets/text_compression.py); the search runs
// match the vanilla decompressed menu (verified). 0x7F terminates the buffer.
// embedded-data-guard: allow short UI text font-byte runs (search/replace for the
// spawn-menu labels), not extracted/generated asset data.
static void RewriteFontRun(uint8 *buf, const uint8 *find, int fn,
                           const uint8 *repl, int rn) {
  int len = 0;
  while (buf[len] != 0x7F && len < 250) len++;  // buffer end (inclusive 0x7F)
  if (buf[len] != 0x7F) return;  // no terminator found within cap — refuse to shift
  for (int i = 0; i + fn <= len; i++) {
    if (memcmp(buf + i, find, (size_t)fn) != 0) continue;
    // Shift the tail (including the 0x7F terminator) to fit the replacement.
    memmove(buf + i + rn, buf + i + fn, (size_t)(len - (i + fn) + 1));
    memcpy(buf + i, repl, (size_t)rn);
    return;
  }
}

void Rando_RewriteInvertedSpawnMenu(uint16 msg_id, uint8 *buf) {
  if (buf == NULL) return;
  if (!(enhanced_features1 & kFeatures1_RandomizerActive)) return;
  if (Rando_GetActiveWorldState() != 2 /* kWorldState_Inverted */) return;
  if (msg_id != 0x184 && msg_id != 0x185) return;
  static const uint8 kSanctuary[]   = {0x12,0x1A,0x27,0x1C,0x2D,0x2E,0x1A,0x2B,0x32};
  static const uint8 kDarkChapel[]  = {0x03,0x1A,0x2B,0x24,0x59,0x02,0x21,0x1A,0x29,0x1E,0x25};
  RewriteFontRun(buf, kSanctuary, sizeof(kSanctuary), kDarkChapel, sizeof(kDarkChapel));
  if (msg_id == 0x185) {  // 3-option menu (Magic Mirror held): also Dark Mountain
    static const uint8 kMountainCave[]  = {0x13,0x21,0x1E,0x59,0x0C,0x28,0x2E,0x27,0x2D,0x1A,0x22,0x27,0x59,0x02,0x1A,0x2F,0x1E};
    static const uint8 kDarkMountain[]  = {0x03,0x1A,0x2B,0x24,0x59,0x0C,0x28,0x2E,0x27,0x2D,0x1A,0x22,0x27};
    RewriteFontRun(buf, kMountainCave, sizeof(kMountainCave), kDarkMountain, sizeof(kDarkMountain));
  }
}

typedef struct { uint8 door_id; uint8 host_entrance; } RandoTakeAnyCaveRt;
static const RandoTakeAnyCaveRt kRandoTakeAnyCaves[kRandoTakeAnyCaveCount] = {
  {0x56, 0x58}, {0x62, 0x58}, {0x6D, 0x58}, {0x69, 0x58}, {0x68, 0x60},  // 0..4
  {0x5A, 0x58}, {0x66, 0x60}, {0x59, 0x60}, {0x78, 0x58}, {0x81, 0x58},  // 5..9
  {0x6A, 0x58}, {0x7C, 0x58}, {0x70, 0x58}, {0x7B, 0x58}, {0x79, 0x58},  // 10..14
  {0x77, 0x58}, {0x72, 0x58}, {0x6B, 0x58}, {0x73, 0x46}, {0x6C, 0x58},  // 15..19
  {0x67, 0x46}, {0x55, 0x58}, {0x5E, 0x58}, {0x65, 0x46}, {0x44, 0x46},  // 20..24
  {0x3C, 0x58}, {0x76, 0x46}, {0x3E, 0x46}, {0x3F, 0x46}, {0x4A, 0x46},  // 25..29
  {0x50, 0x58},                                                           // 30
};

// Cave index for an overworld door id, or -1 if not a take-any door.
static int takeany_cave_for_door(uint8 door_id) {
  for (int i = 0; i < kRandoTakeAnyCaveCount; i++)
    if (kRandoTakeAnyCaves[i].door_id == door_id) return i;
  return -1;
}

// True iff loc is present in the active placement table (the generator only
// emits active caves' slots, so presence == "active this seed").
static bool takeany_loc_in_table(uint16 loc) {
  return Placement_Lookup(loc, 0xFFFFu) != 0xFFFFu;
}

uint8 Rando_TakeAnyHostByDoorIndex(uint8 lx) {
  if (!(enhanced_features1 & kFeatures1_RandomizerActive)) return 0;
  if (Rando_GetActiveWorldState() != kWorldState_Retro) return 0;
  int cave = takeany_cave_for_door((uint8)(lx + 1));
  if (cave < 0) return 0;
  // A cave is active iff its slot-0 LOC is in the placement table (both potion
  // and weapon caves always populate slot 0).
  if (!takeany_loc_in_table((uint16)(kRandoTakeAnyLocBase + 2 * cave))) return 0;
  return kRandoTakeAnyCaves[cave].host_entrance;
}

// Icon kind (shop-item subtype2) for a take-any slot's placed item: 14 = heart
// (BossHeartContainer), 15 = potion (BluePotion, plus the weapon cave's
// sword/rupee — no sword tile exists in the host rooms' shop GFX, so it falls
// back to the potion icon). Both tiles are always loaded (these rooms sell
// RedPotion + a heart in vanilla).
uint8 Rando_TakeAnyDrawKind(uint8 door_id, uint8 pos) {
  int cave = takeany_cave_for_door(door_id);
  if (cave < 0) return 15;
  uint16 loc = (uint16)(kRandoTakeAnyLocBase + 2 * cave + pos);
  uint16 item = Placement_Lookup(loc, 0xFFFFu);
  return (item == ITEM_BossHeartContainer) ? 14 : 15;
}

uint16 Rando_TakeAnyLiveSlot(uint8 room, uint8 door_id, uint8 pos) {
  (void)room;  // door_id is globally unique across the 31 caves; room is advisory
  if (pos > 1) return 0xFFFFu;
  int cave = takeany_cave_for_door(door_id);
  if (cave < 0) return 0xFFFFu;
  uint16 loc = (uint16)(kRandoTakeAnyLocBase + 2 * cave + pos);
  if (!takeany_loc_in_table(loc)) return 0xFFFFu;   // inactive slot
  if (Rando_IsLocationChecked(loc)) return 0xFFFFu; // already taken (cave locked)
  return loc;
}

uint8 Rando_TakeAnyDispatch(uint8 room, uint8 door_id, uint8 pos,
                            uint8 vanilla_lttp_code) {
  (void)room;
  int cave = takeany_cave_for_door(door_id);
  if (cave < 0) return vanilla_lttp_code;
  uint16 loc = (uint16)(kRandoTakeAnyLocBase + 2 * cave + pos);
  // Grant the placed item (Rando_OnLocationCheck inside also marks loc checked).
  // The weapon cave's Rupee300 reward (only when mode.weapons is vanilla/swordless)
  // resolves through Rando_DispatchVanillaGrant -> progressive_to_lttp, which maps
  // ITEM_Rupee300 -> LttP code 0x46; Link_ReceiveItem(0x46) drives the receipt
  // ancilla whose Ancilla_AddRupees grants +300 rupees (src/ancilla.c). The default
  // ProgressiveSword reward likewise dispatches via progressive_to_lttp.
  uint8 lttp = Rando_DispatchVanillaGrant(loc, 0xFFFFu, vanilla_lttp_code);
  // Lock the whole cave: mark every active slot's LOC checked so the other
  // offered item vanishes and the cave stays empty on revisit (matches the asm
  // ShopState |= $07 + PurchaseCounts[idx] = 1). Slot `pos` was just marked by
  // the grant; mark the sibling slot too if it is an active slot.
  for (uint8 s = 0; s < 2; s++) {
    if (s == pos) continue;
    uint16 sib = (uint16)(kRandoTakeAnyLocBase + 2 * cave + s);
    if (takeany_loc_in_table(sib)) Rando_MarkLocationChecked(sib);
  }
  return lttp;
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
uint8 g_rando_boomerang_owned = 0;
uint8 g_rando_bow_owned = 0;

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

bool Rando_IsRetroActive(void) {
  return (enhanced_features1 & kFeatures1_RandomizerActive) &&
         Rando_GetActiveWorldState() == (uint8)kWorldState_Retro;
}

// genericKeys is pinned on for Retro (app/World/Retro.php), so the runtime gate
// equals Rando_IsRetroActive today. Distinct name: see the rando.h doc comment.
bool Rando_IsGenericKeysActive(void) {
  return Rando_IsRetroActive();
}

// Retro genericKeys BUYABLE shop-slot grant (ALTTPR ShopKey, Randomizer.php:746-747
// — `$shop->addInventory(1, Item::get('ShopKey', ...), 100)`). The predicate VM
// treats holding >=1 GenericKey as opening EVERY small-key door (eval_has_item /
// eval_has_amount wildcard), but the placed GenericKey pool is FINITE (~30) and
// decrements per door, so keys spent out of order can strand a progression door.
// ALTTPR avoids that with an UNLIMITED buyable ShopKey supply; the fork had dropped
// it. This is that supply: each purchase feeds the shared counter exactly like a
// GenericKey pickup, with no cap on how many can be bought (re-enter the shop room
// to buy again). Same grant as rando_grant_generic_key(); the shop handler owns the
// rupee cost + purchase feedback. Only reached from the genericKeys-gated shop slot.
void Rando_GrantGenericKeyPurchase(void) {
  rando_grant_generic_key();
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

// Boomerang/bow decouple — see rando.h. Called from the receive-item path
// (AncillaAdd_ItemReceipt, misc.c) under rando instead of the vanilla
// unconditional `*p = v`, so a lower-tier pickup never downgrades the slot and
// the higher tier is remembered for the item-menu swap.
void Rando_GrantBoomerang(void) {
  // Strictly PROGRESSIVE: the FIRST boomerang collected is always blue, the
  // SECOND always red — regardless of which item (Blue/Red) is actually placed
  // at the location. The ownership bits are the tier counter; the byte
  // (link_item_boomerang) tracks the currently-SELECTED color, which the player
  // swaps blue<->red in the item menu once both are owned (Hud_NormalMenu /
  // Rando_BoomerangCanToggle). Safe because no logic predicate ever requires a
  // boomerang of either color (verified: assets/rando has no Boomerang gate).
  //
  // Fold the current byte tier into ownership first (pre-feature-save / debug
  // poke compat): byte 1 (blue) implies blue owned; byte 2 (red) implies both,
  // since red can't exist without having advanced through blue.
  if (link_item_boomerang >= 1) g_rando_boomerang_owned |= kRandoBoomerang_Blue;
  if (link_item_boomerang >= 2) g_rando_boomerang_owned |= kRandoBoomerang_Red;
  // Advance to the next unowned tier (never downgrades — increment only).
  if (!(g_rando_boomerang_owned & kRandoBoomerang_Blue)) {
    g_rando_boomerang_owned |= kRandoBoomerang_Blue;
    if (link_item_boomerang < 1) link_item_boomerang = 1;  // select blue
  } else if (!(g_rando_boomerang_owned & kRandoBoomerang_Red)) {
    g_rando_boomerang_owned |= kRandoBoomerang_Red;
    link_item_boomerang = 2;  // select the newly-granted red
  }
  // both tiers owned: capped, no change.
}

void Rando_GrantBow(uint8 lttp_code) {
  // Fold the current tier into ownership (pre-feature save / debug-poke compat):
  // the byte only ever shows a tier the player actually has.
  if (link_item_bow >= 1) g_rando_bow_owned |= kRandoBow_Wood;
  if (link_item_bow >= 3) g_rando_bow_owned |= kRandoBow_Silver;
  bool silver = (lttp_code == 0x3b);
  // Silver arrows imply a basic bow, so record both — the swap is then available
  // even on a silver-first pickup, and the wood tier is never "lost".
  g_rando_bow_owned |= silver ? (kRandoBow_Silver | kRandoBow_Wood) : kRandoBow_Wood;
  uint8 target = silver ? 3 : 1;  // the no-arrows form of this strength tier
  if (link_item_bow < target) {
    // Raising the strength tier (or first pickup). Preserve the current arrow
    // bit so a silver upgrade on a wood+arrows bow stays "has arrows".
    bool has_arrows = (link_item_bow == 2 || link_item_bow == 4);
    link_item_bow = has_arrows ? (uint8)(target + 1) : target;
  }
  // link_item_bow >= target already (e.g. silver owned, re-granting wood): leave
  // the higher tier in place — never downgrade.
}

bool Rando_BoomerangCanToggle(void) {
  return g_rando_slot_active &&
         (g_rando_boomerang_owned & kRandoBoomerang_Blue) &&
         (g_rando_boomerang_owned & kRandoBoomerang_Red);
}

bool Rando_BowCanToggle(void) {
  // Owning silver implies the wood capability (Rando_GrantBow sets both bits),
  // so a single silver bit is enough to offer the wood<->silver arrow swap.
  return g_rando_slot_active && (g_rando_bow_owned & kRandoBow_Silver) != 0;
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
  // Persist boomerang/bow ownership for the same reason: a swapped-down byte
  // (blue selected, or wood arrows selected) must not lose the higher tier
  // across save/reload. See Rando_GrantBoomerang / Rando_GrantBow.
  out_slot->header.boomerang_owned = g_rando_boomerang_owned;
  out_slot->header.bow_owned = g_rando_bow_owned;
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
// Field item sprites (add-rando-field-item-sprites) — resolver half. The draw
// half (gfx DMA + OAM) lives in sprite.c. See rando.h for the contract.
// ---------------------------------------------------------------------------
bool Rando_FieldItemSpritesActive(void) {
  // Active rando slot AND the client-local field_item_sprites toggle (read live
  // from zelda3.ini / the native window, so toggling takes effect without a
  // restart). Inert in non-rando play regardless of the toggle.
  return (enhanced_features1 & kFeatures1_RandomizerActive) != 0 &&
         g_config.field_item_sprites;
}

// Palette/priority table, indexed by LttP receive code (defined in sprite_main.c,
// declared in sprite.h — redeclared here to avoid pulling sprite.h into rando.c).
extern const uint8 kWishPond2_OamFlags[76];

// Resolve a placed item to the LttP receive code that drives its receive-
// animation graphic — the SAME chain Rando_DispatchVanillaGrant uses to pick the
// shown item (Rando_VanillaItemForRegistryId primary + the progressive-boomerang
// colour remap, falling back to progressive_to_lttp), MINUS the side effects.
// Returns 0xFF when the item has no LttP receive code. Keep in sync with the
// resolution block in Rando_DispatchVanillaGrant (a draw/grant drift here is the
// bug class that left rupees, then boomerangs, drawing as the vanilla sprite).
static uint8 rando_item_display_lttp(uint16 placed) {
  uint8 lttp = Rando_VanillaItemForRegistryId(placed);
  if (lttp != 0xFF) {
    if (placed == ITEM_BlueBoomerang || placed == ITEM_RedBoomerang) {
      bool blue_owned = (g_rando_boomerang_owned & kRandoBoomerang_Blue) ||
                        link_item_boomerang >= 1;
      lttp = blue_owned ? 0x2a : 0x0c;  // red once blue owned, else blue
    }
    return lttp;
  }
  return progressive_to_lttp(placed);  // progressive items (sword/bow/...) or 0xFF
}

bool Rando_GetFieldItemIcon(uint16 location_id, uint16 vanilla_item_id,
                            uint8 *out_gfx, uint8 *out_big, uint8 *out_oam_flags) {
  if (!Rando_FieldItemSpritesActive())
    return false;
  // Placement_Lookup returns vanilla_item_id when no table is active or the
  // location is absent — both mean "draw the vanilla sprite".
  uint16 placed = Placement_Lookup(location_id, vanilla_item_id);
  if (placed == vanilla_item_id)
    return false;

  // Tier 1 — items the normal receive animation draws (rupees, equipment,
  // boomerang, bottles, ...). Mirror Ancilla_ReceiveItem_Draw EXACTLY: gfx,
  // size, and palette are all indexed by the LttP receive code, so the field
  // sprite looks like the held-aloft pickup. NOTE: kDirectGrantIcons does NOT
  // cover these — it only holds the Slice-9 direct-grant items, which is why a
  // placed red rupee (code 0x36) / boomerang previously fell back to vanilla.
  uint8 code = rando_item_display_lttp(placed);
  if (code < 76 && kReceiveItemGfx[code] != 0xff) {
    *out_gfx = kReceiveItemGfx[code];
    *out_big = kReceiveItem_Tab1[code];
    uint8 a = kWishPond2_OamFlags[code];
    if (a & 0x80)               // sign8 fallback, matching Ancilla_ReceiveItem_Draw
      a = 5;
    *out_oam_flags = (uint8)(a * 2 | 0x30);
    return true;
  }

  // Tier 2 — direct-grant items (small keys, ...) that have a Slice-9 icon but
  // no receive gfx. gfx==0 entries (HalfMagic/QuarterMagic/TriforcePiece) have
  // no drawable sprite → fall back to the vanilla sprite.
  const size_t n = sizeof(kDirectGrantIcons) / sizeof(kDirectGrantIcons[0]);
  if ((size_t)placed < n && kDirectGrantIcons[placed].gfx != 0) {
    *out_gfx = kDirectGrantIcons[placed].gfx;
    *out_big = kDirectGrantIcons[placed].big;
    *out_oam_flags = kDirectGrantIcons[placed].oam_flags;
    return true;
  }
  return false;
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
// Boss-shuffle LOGIC assignment (OP_CAN_KILL_BOSS). Independent of the
// shuffle_boss.c sprite-substitution activation: this drives reachability only.
static const uint8 *g_boss_logic_assignment = NULL;

void Rando_SetDungeonPrizeAssignment(const uint8 *assignment) {
  g_dungeon_prize_assignment = assignment;
}
void Rando_SetMedallionAssignment(const uint8 *assignment) {
  g_medallion_assignment = assignment;
}
void Rando_SetBossAssignment(const uint8 *assignment) {
  g_boss_logic_assignment = assignment;
}
const uint8 *Rando_GetDungeonPrizeAssignment(void) { return g_dungeon_prize_assignment; }
const uint8 *Rando_GetMedallionAssignment(void) { return g_medallion_assignment; }
const uint8 *Rando_GetBossAssignment(void) { return g_boss_logic_assignment; }

// Runtime open gate for the medallion-shuffled MM/TR overworld doors. The placer
// and tracker consult g_medallion_assignment via OP_MEDALLION_OPENS; the ancilla
// spell handlers consult it here so the dungeon actually opens for whichever
// medallion the seed requires. entrance_index: 0 = Misery Mire, 1 = Turtle Rock
// (the kRandoMedallionEntranceCount ordering used by MedallionShuffle_Run and
// the logic codegen's _resolve_entrance_id). Returns false when no assignment is
// installed so the caller can keep the vanilla Ether->MM / Quake->TR mapping.
bool Rando_MedallionOpens(uint8 cast_medallion, uint8 entrance_index) {
  const uint8 *assignment = g_medallion_assignment;
  if (assignment == NULL || entrance_index >= kRandoMedallionEntranceCount)
    return false;
  return assignment[entrance_index] == cast_medallion;
}

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
// add-rando-major-glitch D6 — the features0 wanted bank (the source the
// per-frame mirror syncs into enhanced_features0). Forcing the JP-glitch bit
// here (not just enhanced_features0) survives the per-frame re-sync AND a
// mid-session Config_ApplyLive (which rewrites wanted from g_config.features0).
extern uint32 g_wanted_zelda_features;

// §62 — cache for the active slot's textual share string (50 base32 chars +
// NUL). Populated at Rando_ActivateSidecarSlot from the slot header's raw
// binary; consumed by Rando_RevealActiveSlotSpoiler to resolve the on-disk
// spoiler path. Cleared on Rando_DeactivateSlot.
static char g_rando_active_share_string[64] = {0};

// ---------------------------------------------------------------------------
// Phase C entrance shuffle — runtime door overlay. Owns a shadow of the vanilla
// door→entrance-id table (kOverworld_Entrance_Id = g_asset_ptrs[126]) and
// repoints the asset pointer while a cave-shuffle slot is active. The
// permutation is regenerated from (seed, axes, attempt) carried in the slot
// header, so save/quit + reload restores the same π. Coupling (enter A ⇒ exit A)
// is automatic for caves: Dungeon_LoadEntrance caches the SOURCE overworld
// position at entry, before the interior loads from which_entrance.
// ---------------------------------------------------------------------------
#define kEntranceOverlayMax 4096
static uint8 g_entrance_overlay[kEntranceOverlayMax];
// Saved g_asset_ptrs[126] (the vanilla door table) while the overlay is
// installed. INVARIANT (audit L1): Entrance_RuntimeTeardown() MUST run before any
// LoadAssets() reload — LoadAssets unconditionally rewrites every g_asset_ptrs[i]
// into a fresh buffer, which would both drop the overlay and leave this pointer
// dangling into the freed asset buffer. Today all LoadAssets call sites are
// startup/CLI-only (never mid-session), so this is latent; a future hot-reload
// feature must tear the overlay down first.
static const uint8 *g_entrance_overlay_orig = NULL;

// ---------------------------------------------------------------------------
// Decoupled (Insanity) runtime — D.4. Productionizes the validated arrival
// capture-and-replay (spike branch claude/insanity-arrival-spike): each cave
// interior's overworld-arrival block (g_ram[0xC140..0xC172) + the two world
// flags) is captured the first time its door is entered; on a decoupled cave
// EXIT the player emerges at net[entered_interior]'s door by replaying THAT
// interior's captured arrival, then letting LoadCachedEntranceProperties run.
// `net` (entered-interior → emerge-interior) is the deterministic decoupled
// permutation regenerated at slot-load. Until a target is captured this session,
// the exit falls back to the normal coupled return (never strands the player) —
// the static-table bake (D.3) removes that gap for never-visited doors.
// ---------------------------------------------------------------------------
static uint8 g_decoupled_active;
static int   g_decoupled_n;                       // pool size (cave interiors)
static uint8 g_decoupled_net[kEntranceMaxInteriors];   // entered-interior → emerge
static uint16 g_decoupled_entered;                // vanilla interior of entered door; 0xFFFF = none
// Dungeon decoupled (Insanity for dungeons): one-way dungeon EXITS. net'[D] = the
// dungeon-pool index of the door Link emerges at after exiting loaded dungeon D.
// No arrival table — the runtime just retargets the room-keyed exit search.
static uint8 g_dungeon_decoupled_active;
static int   g_dungeon_decoupled_n;
static uint8 g_dungeon_decoupled_net[kEntranceDungeonCount];
// Cross + decoupled: one-way exits over the MIXED cross pool. net[e] = the endpoint
// Link emerges at after exiting door e (a cave OR a dungeon). The exit kind is
// resolved at the entry hook and consumed at LoadOverworldFromDungeon.
static uint8 g_cross_decoupled_active;
static int   g_cross_decoupled_n;
static uint8 g_cross_net[kEntranceMaxInteriors];
static uint8 g_cross_decoupled_exit_kind;  // 0 none, 1 cave, 2 dungeon (consumed at exit)
static uint8 g_cross_decoupled_exit_cave;  // target cave interior when kind == 1
typedef struct { uint8 block[0x32]; uint8 is_dark; uint8 save_dark; uint8 valid; } RandoCaveArrival;
// Single per-interior overworld-arrival table used by BOTH the bake-capture and
// the decoupled runtime. Persisted to cave_arrival_capture.bin and reloaded each
// session, so captures accumulate across restarts and the runtime emerges at any
// door captured this session OR a prior one (option B: just play, it fills in).
static RandoCaveArrival g_cave_capture[kEntranceMaxInteriors];
static int g_cave_capture_count;
static bool g_cave_capture_loaded;  // initialized the table this session yet?
static void Rando_LoadArrivalCaptureIfNeeded(void);
// Committed baked arrival table (the D.3 walkabout result) — the default source
// so every door is one-way from the start, no on-disk capture needed.
#include "cave_arrival_baked.h"
// Baked-table length, used to bound reads FROM it. kEntranceCaveInteriorCount (the
// runtime cave count) is private to shuffle_entrance.c, so couple at the bound we
// can see here: the table must fit g_cave_capture[]. The load loop also clamps to
// this length, so bumping the cave count without re-baking degrades to coupled
// (valid 0) for the new interiors instead of reading past the array.
#define kCaveArrivalBakedCount ((int)(sizeof(kCaveArrivalBaked) / sizeof(kCaveArrivalBaked[0])))
_Static_assert(kCaveArrivalBakedCount <= kEntranceMaxInteriors,
               "baked cave-arrival table exceeds kEntranceMaxInteriors");

// D.3 capture: vanilla cave interior of the door just entered, recorded at the
// overworld entry hook for EVERY game (shuffle or not) so the capture-for-bake
// works regardless of mode. 0xFFFF = the entered door isn't a cave.
static uint16 g_rando_entered_door_interior = 0xFFFF;
void Rando_RecordEnteredDoorForCapture(uint16 lx) {
  g_rando_entered_door_interior = 0xFFFF;
  uint32 len = kOverworld_Entrance_Id_SIZE;
  if (lx >= len) return;
  // The door's VANILLA entrance-id: the saved original when an overlay is
  // installed (shuffle), otherwise the live table (vanilla/non-entrance game).
  uint8 vid = (g_entrance_overlay_orig != NULL) ? g_entrance_overlay_orig[lx]
                                                : ((const uint8 *)kOverworld_Entrance_Id)[lx];
  int interior = Entrance_InteriorOfEntranceId(vid);
  if (interior >= 0 && interior < kEntranceMaxInteriors)
    g_rando_entered_door_interior = (uint16)interior;
}

// Genuine fall-hole caves (fall-hole table entrance-ids 0x76-0x81) have no
// walk-in door slot — the fall sets which_entrance directly (the fall-hole table
// isn't shuffled), so key on it. Lets the capture tool record a fall-in arrival
// and the decoupled exit emerge elsewhere. Non-cave fall-hole ids resolve to
// interior -1 below and are ignored. (heart_piece_cave_3 is NOT here — it is a
// normal walk-in cave handled by the door-slot entry hook.)
void Rando_RecordEnteredFallhole(void) {
  g_rando_entered_door_interior = 0xFFFF;
  int interior = Entrance_InteriorOfEntranceId(which_entrance);
  if (interior < 0 || interior >= kEntranceMaxInteriors) return;
  g_rando_entered_door_interior = (uint16)interior;
  if (g_decoupled_active && interior < g_decoupled_n)
    g_decoupled_entered = (uint16)interior;
}

static void Dungeon_Decoupled_Reset(void) {
  g_dungeon_decoupled_active = 0;
  g_dungeon_decoupled_n = 0;
}

static void Cross_Decoupled_Reset(void) {
  g_cross_decoupled_active = 0;
  g_cross_decoupled_n = 0;
  g_cross_decoupled_exit_kind = 0;
  g_cross_decoupled_exit_cave = 0xFF;
}

// Entry hook: under cross-decoupled, resolve the source door's one-way exit target
// (cave or dungeon) and stash it for the exit. For a dungeon target this sets the
// room-keyed exit search target; a cave target is replayed at the cached branch.
void Rando_CrossDecoupledSetExit(uint16 lx) {
  g_cross_decoupled_exit_kind = 0;
  g_cross_decoupled_exit_cave = 0xFF;
  if (!g_cross_decoupled_active || g_entrance_overlay_orig == NULL) return;
  if (lx >= kOverworld_Entrance_Id_SIZE) return;
  uint16 value = 0;
  int kind = Entrance_CrossDecoupledExit(g_cross_net, g_cross_decoupled_n,
                                         g_entrance_overlay_orig[lx], &value);
  // When active, cross-decoupled is AUTHORITATIVE over the coupled exit the prior
  // hook set: a cave target forces the cached branch (kind 1, replay), a dungeon
  // target forces the search branch at its room (kind 2). Self-map / non-pool
  // (kind 0) leaves the coupled cross return-to-source in place.
  if (kind == 1) {
    // Only force the cached branch if the target cave's arrival is actually
    // CAPTURED — otherwise fall back to the coupled return (kind 0) rather than
    // forcing a cached replay against a stale *_exit block (the loaded interior may
    // be a dungeon). The production baked table makes every cave valid, so this
    // never falls back in shipped play; it's a dev/incomplete-table footgun guard.
    Rando_LoadArrivalCaptureIfNeeded();
    if (value < kEntranceMaxInteriors && g_cave_capture[value].valid) {
      g_cross_decoupled_exit_kind = 1;
      g_cross_decoupled_exit_cave = (uint8)value;
      g_rando_entrance_exit_room = 0;
      g_rando_entrance_force_cached = 0;
    }
  } else if (kind == 2) {
    g_cross_decoupled_exit_kind = 2;
    g_rando_entrance_exit_room = value;
    g_rando_entrance_force_cached = 0;
  }
}

// Consume-once at LoadOverworldFromDungeon top: returns the exit kind (0 none,
// 1 cave, 2 dungeon) and writes the target cave interior to *out_cave, clearing
// both. Same consume-at-top discipline as the sibling exit-coupling globals.
uint8 Rando_CrossDecoupledConsumeExit(uint8 *out_cave) {
  uint8 k = g_cross_decoupled_exit_kind;
  if (out_cave) *out_cave = g_cross_decoupled_exit_cave;
  g_cross_decoupled_exit_kind = 0;
  g_cross_decoupled_exit_cave = 0xFF;
  return k;
}

static void Decoupled_Reset(void) {
  g_decoupled_active = 0;
  g_decoupled_n = 0;
  g_decoupled_entered = 0xFFFF;
  // NOTE: do NOT clear g_cave_capture here — it's the persisted arrival table and
  // must survive slot teardown / reload (that's what makes captures accumulate).
}

// Entry hook: remember the vanilla cave interior of the door slot just entered
// (its overworld position is what we may capture / emerge at).
void Rando_DecoupledSetEnteredDoor(uint16 lx) {
  g_decoupled_entered = 0xFFFF;
  if (!g_decoupled_active || g_entrance_overlay_orig == NULL) return;
  if (lx >= kOverworld_Entrance_Id_SIZE) return;
  int interior = Entrance_InteriorOfEntranceId(g_entrance_overlay_orig[lx]);
  if (interior >= 0 && interior < g_decoupled_n) g_decoupled_entered = (uint16)interior;
}

// (Capture is handled by Rando_CaptureArrivalForBake on every cave entry — it
// fills + persists the same g_cave_capture table the runtime reads below.)

// Consume-once accessor for the entered-interior global. LoadOverworldFromDungeon
// reads+clears it at the top UNCONDITIONALLY (same discipline as g_rando_entrance_
// exit_room / force_cached, audit HIGH-1): this function is reached by mirror /
// special-area / ending warps too, so a stale entered-interior left set could be
// consumed by a LATER cave-class exit → wrong-door warp. 0xFFFF = none.
uint16 Rando_DecoupledConsumeEntered(void) {
  uint16 v = g_decoupled_entered;
  g_decoupled_entered = 0xFFFF;
  return v;
}

// Exit hook (cave-class cached branch, before LoadCachedEntranceProperties):
// replace the live *_exit with net[entered]'s captured arrival so Link emerges
// at a DIFFERENT door. `entered` is the consumed entered-interior. Returns false
// (→ coupled return) when inactive, the target is the same interior, or the
// target hasn't been captured yet.
// Replay cave interior `target_cave`'s captured overworld arrival into the live
// *_exit block + world flags + target room/door-settings (the Y-adjust). Shared by
// the cave-decoupled and cross-decoupled exit paths. False → not captured (coupled
// fallback) or out of range.
static bool Rando_ReplayCaveArrival(int target_cave) {
  if (target_cave < 0 || target_cave >= kEntranceMaxInteriors) return false;
  Rando_LoadArrivalCaptureIfNeeded();  // pull in prior-session captures (persisted)
  const RandoCaveArrival *e = &g_cave_capture[target_cave];
  if (!e->valid) return false;  // target door not captured yet → coupled fallback
  memcpy(g_ram + 0xC140, e->block, 0x32);
  g_ram[0xFFF] = e->is_dark;      // is_in_dark_world
  g_ram[0xF3CA] = e->save_dark;   // savegame_is_darkworld
  // Target door's room + door-settings drive the cached-exit Y-adjust (spike iter4).
  uint8 rid = Entrance_CaveRepresentativeId(target_cave);
  if ((uint32)rid < kEntranceData_rooms_SIZE / 2u) dungeon_room_index = kEntranceData_rooms[rid];
  if ((uint32)rid < kEntranceData_doorSettings_SIZE / 2u) ow_entrance_value = kEntranceData_doorSettings[rid];
  return true;
}

// Exit hook (cave-class cached branch, before LoadCachedEntranceProperties):
// replace the live *_exit with net[entered]'s captured arrival so Link emerges
// at a DIFFERENT door. `entered` is the consumed entered-interior. Returns false
// (→ coupled return) when inactive, the target is the same interior, or the
// target hasn't been captured yet.
bool Rando_DecoupledReplaceArrival(uint16 entered) {
  if (!g_decoupled_active || entered >= (uint16)g_decoupled_n) return false;
  int target = g_decoupled_net[entered];
  if (target < 0 || target >= g_decoupled_n || target == (int)entered) return false;
  return Rando_ReplayCaveArrival(target);
}

// Cross-decoupled exit: replay the target CAVE's arrival (target resolved at the
// entry hook). Returns false (→ coupled return) when not captured.
bool Rando_CrossDecoupledReplayCave(uint8 target_cave) {
  return Rando_ReplayCaveArrival((int)target_cave);
}

// ---------------------------------------------------------------------------
// D.3 cave-arrival table. Production: the committed kCaveArrivalBaked is loaded
// once and used by the decoupled runtime (every door one-way from launch). The
// re-capture/dump tooling below (used to PRODUCE the baked table) is a developer
// opt-in — set the env var ZELDA3_CAPTURE_ARRIVALS=1 to record fresh arrivals
// into cave_arrival_capture.{bin,txt} for re-baking. Off by default, so normal
// play writes no files and prints nothing.
// ---------------------------------------------------------------------------
static bool Rando_ArrivalCaptureEnabled(void) {
  static int cached = -1;  // -1 unknown, 0 off, 1 on
  if (cached < 0) {
    const char *v = getenv("ZELDA3_CAPTURE_ARRIVALS");
    cached = (v != NULL && v[0] != '\0' && v[0] != '0') ? 1 : 0;
  }
  return cached != 0;
}

static void Rando_LoadArrivalCaptureIfNeeded(void) {
  if (g_cave_capture_loaded) return;
  g_cave_capture_loaded = true;
  int n = Entrance_CaveInteriorCount();
  // Committed baked table — the production source (every door one-way from launch).
  for (int i = 0; i < n && i < kEntranceMaxInteriors && i < kCaveArrivalBakedCount; i++)
    g_cave_capture[i] = kCaveArrivalBaked[i];
  g_cave_capture_count = 0;
  for (int i = 0; i < n && i < kEntranceMaxInteriors; i++)
    if (g_cave_capture[i].valid) g_cave_capture_count++;
  if (!Rando_ArrivalCaptureEnabled()) return;
  // Dev re-capture overlay: a live capture file overrides matching entries so
  // re-captures take effect without rebaking (valid-only, so a partial file
  // never erases baked data).
  FILE *f = fopen("cave_arrival_capture.bin", "rb");
  if (f != NULL) {
    static RandoCaveArrival tmp[kEntranceMaxInteriors];
    if (fread(tmp, sizeof(tmp), 1, f) == 1)
      for (int i = 0; i < n && i < kEntranceMaxInteriors; i++)
        if (tmp[i].valid) g_cave_capture[i] = tmp[i];
    fclose(f);
    g_cave_capture_count = 0;
    for (int i = 0; i < n && i < kEntranceMaxInteriors; i++)
      if (g_cave_capture[i].valid) g_cave_capture_count++;
  }
  fprintf(stderr, "[ARRIVAL-CAPTURE] capture mode ON — %d/%d doors\n",
          g_cave_capture_count, n);
}

static void Rando_DumpArrivalCapture(void) {
  // Binary sidecar first (the source of truth for resume across restarts).
  FILE *b = fopen("cave_arrival_capture.bin", "wb");
  if (b != NULL) { fwrite(g_cave_capture, sizeof(g_cave_capture), 1, b); fclose(b); }
  FILE *f = fopen("cave_arrival_capture.txt", "wb");
  if (f == NULL) { fprintf(stderr, "[ARRIVAL-CAPTURE] dump fopen failed\n"); return; }
  int n = Entrance_CaveInteriorCount();
  fprintf(f, "// Generated by the D.3 arrival capture walkabout — paste into\n"
             "// src/rando/cave_arrival_baked.h. interior order matches kCaveInteriors.\n");
  fprintf(f, "static const RandoCaveArrival kCaveArrivalBaked[%d] = {\n", n);
  for (int i = 0; i < n && i < kEntranceMaxInteriors; i++) {
    const RandoCaveArrival *e = &g_cave_capture[i];
    fprintf(f, "  {{");
    for (int b = 0; b < 0x32; b++) fprintf(f, "0x%02X,", e->block[b]);
    fprintf(f, "}, 0x%02X, 0x%02X, %u},  // %d %s\n",
            e->is_dark, e->save_dark, e->valid, i, Entrance_CaveInteriorName(i));
  }
  fprintf(f, "};\n");
  fclose(f);
  fprintf(stderr, "[ARRIVAL-CAPTURE] wrote cave_arrival_capture.txt (%d/%d)\n",
          g_cave_capture_count, n);
}

void Rando_CaptureArrivalForBake(void) {
  if (!Rando_ArrivalCaptureEnabled()) return;  // dev opt-in only; silent otherwise
  Rando_LoadArrivalCaptureIfNeeded();  // resume prior session's captures (restart-safe)
  // Key by the entered door's VANILLA interior (recorded at the entry hook), which
  // is correct in ANY mode: the cached *_exit is always the entered DOOR's
  // overworld arrival, even when an overlay redirects what loads behind it.
  int interior = g_rando_entered_door_interior;
  if (interior < 0 || interior >= kEntranceMaxInteriors) return;
  uint16 lx = (uint16)(g_ram[0xC14A] | (g_ram[0xC14B] << 8));  // link_x_coord_exit
  uint16 ly = (uint16)(g_ram[0xC148] | (g_ram[0xC149] << 8));  // link_y_coord_exit
  if (lx < 0x100 && ly < 0x100) return;  // degenerate startup
  RandoCaveArrival *e = &g_cave_capture[interior];
  bool was_new = !e->valid;
  memcpy(e->block, g_ram + 0xC140, 0x32);
  e->is_dark = g_ram[0xFFF];
  e->save_dark = g_ram[0xF3CA];
  e->valid = 1;
  int total = Entrance_CaveInteriorCount();
  if (was_new) {
    g_cave_capture_count++;
    fprintf(stderr, "[ARRIVAL-CAPTURE] %d/%d — interior %d %s\n",
            g_cave_capture_count, total, interior, Entrance_CaveInteriorName(interior));
  } else {
    fprintf(stderr, "[ARRIVAL-CAPTURE] re-captured interior %d %s\n",
            interior, Entrance_CaveInteriorName(interior));
  }
  // Persist on EVERY capture (new OR re-capture). A re-walk to correct a bad
  // earlier capture must reach disk, not just RAM — otherwise the stale row
  // survives and the fix is lost on exit. A partial walkabout stays recoverable.
  Rando_DumpArrivalCapture();
  if (was_new && g_cave_capture_count >= total)
    fprintf(stderr, "[ARRIVAL-CAPTURE] COMPLETE — all %d captured\n", total);
}

// Restore the vanilla door table + clear the logic overrides. Idempotent.
static void Entrance_RuntimeTeardown(void) {
  if (g_entrance_overlay_orig != NULL) {
    g_asset_ptrs[126] = (void *)g_entrance_overlay_orig;
    g_entrance_overlay_orig = NULL;
  }
  Entrance_ClearRegionOverrides();
  Entrance_ClearEdgeOverrides();
  g_rando_entrance_exit_room = 0;
  g_rando_entrance_force_cached = 0;
  g_rando_force_cached_room = 0;  // reset the companion room with the flag
  g_rando_inverted_spawn_redirect = 0;
  Decoupled_Reset();
  Dungeon_Decoupled_Reset();
  Cross_Decoupled_Reset();
}

// Install the overlay + logic overrides for an entrance-shuffle slot (caves
// and/or dungeons). Tears down any prior install first (so a slot-switch without
// an intervening Deactivate is safe). No-op for non-shuffle slots.
// seed_u64 lives at raw share_string bytes [21..28] LE (rando_share layout).
// Single decoder shared by the entrance regen, the door-shuffle regen, and
// the cosmetic seed — a layout move must change exactly one place.
static uint64 SlotSeedFromShareString(const uint8 *sb) {
  return (uint64)sb[21] | ((uint64)sb[22] << 8) | ((uint64)sb[23] << 16) |
         ((uint64)sb[24] << 24) | ((uint64)sb[25] << 32) |
         ((uint64)sb[26] << 40) | ((uint64)sb[27] << 48) | ((uint64)sb[28] << 56);
}

static void Entrance_RuntimeInstall(const RandoSlotHeader *h) {
  Entrance_RuntimeTeardown();
  RandoSettings es;
  memset(&es, 0, sizeof(es));
  es.shuffle_cave_entrances = (h->entrance_axes & kEntranceAxis_ShuffleCaves) ? 1 : 0;
  es.shuffle_dungeon_entrances = (h->entrance_axes & kEntranceAxis_ShuffleDungeons) ? 1 : 0;
  es.shuffle_ganons_tower_entrance = (h->entrance_axes & kEntranceAxis_ShuffleGanonsTower) ? 1 : 0;
  es.cross_category = (h->entrance_axes & kEntranceAxis_CrossCategory) ? 1 : 0;
  es.decoupled = (h->entrance_axes & kEntranceAxis_Decoupled) ? 1 : 0;
  es.world_state = h->settings_ext_present ? h->world_state : (uint8)kWorldState_Open;
  bool cross = Entrance_IsCrossActive(&es);    // supersedes the separate paths
  bool cave = !cross && Entrance_IsActive(&es);// Inverted/Retro guard (defense in depth)
  bool dun = !cross && Entrance_IsDungeonActive(&es);
  // Decoupled (D.4) composes on top of the entry shuffle: cave decoupled needs the
  // cave shuffle, dungeon decoupled needs the dungeon shuffle.
  bool decoupled = Entrance_IsDecoupledActive(&es);
  bool decoupled_dun = Entrance_IsDungeonDecoupledActive(&es);
  bool decoupled_cross = Entrance_IsCrossDecoupledActive(&es);  // one-way over the mixed pool
  if (!cross && !cave && !dun && !decoupled && !decoupled_dun && !decoupled_cross) return;
  // X.1 backward-load: the entrance permutation π is REGENERATED from
  // (seed, axes, attempt) against this build's interior/dungeon pool. The pool
  // composition is part of the generator version, so a slot written by a
  // different generator_version may regenerate a DIFFERENT π → doors that don't
  // match the baked placement. Warn loudly; entrance-shuffle seeds are
  // version-locked (regenerate after a randomizer update). The placement itself
  // is still loaded; only the door layout is at risk.
  if (Rando_DetectVersionDrift(h, (uint16)kGeneratorVersion)) {
    fprintf(stderr,
        "Rando WARNING: this entrance-shuffle slot was generated by version %u "
        "but this build is version %u. The door layout is regenerated from the "
        "pool and may not match — regenerate the seed for correct entrances.\n",
        (unsigned)h->generator_version, (unsigned)kGeneratorVersion);
  }
  const uint8 *ids = kOverworld_Entrance_Id;
  uint32 len = kOverworld_Entrance_Id_SIZE;
  if (ids == NULL || len == 0 || len > kEntranceOverlayMax) return;
  // seed_u64 lives at raw share_string bytes [21..28] LE (per rando_share layout).
  const uint8 *sb = h->share_string;
  uint64 seed = SlotSeedFromShareString(sb);
  uint8 cave_assign[kEntranceMaxInteriors]; int cave_n = 0;
  uint8 dun_assign[kEntranceMaxInteriors]; int dun_n = 0;
  uint8 cross_assign[kEntranceMaxInteriors]; int cross_n = 0;
  // Logic-side overrides so the in-game location tracker reflects the shuffled
  // reachability (gameplay itself reads the baked placement, not reachability).
  if (cross) {
    // Crossed: one combined pool. ApplyCrossOverrides installs all 4 cases; the
    // unified overlay maps every door (cave or dungeon) to its target's id.
    cross_n = Entrance_ComputeCrossPermutation(&es, seed, h->entrance_attempt, cross_assign);
    Entrance_ApplyCrossOverrides(cross_assign, cross_n);
    Entrance_BuildCrossOverlay(cross_assign, cross_n, ids, len, g_entrance_overlay);
  } else {
    if (cave) {
      cave_n = Entrance_ComputePermutation(&es, seed, h->entrance_attempt, cave_assign);
      Entrance_ApplyRegionOverrides(cave_assign, cave_n);
    }
    if (dun) {
      dun_n = Entrance_ComputeDungeonPermutation(&es, seed, h->entrance_attempt, dun_assign);
      Entrance_ApplyEdgeOverrides(dun_assign, dun_n);
    }
    // Door overlay: cave pass copies vanilla + remaps cave slots (NULL = copy
    // only), then the dungeon pass remaps the disjoint dungeon slots in place.
    Entrance_BuildDoorOverlay(cave ? cave_assign : NULL, cave_n, ids, len, g_entrance_overlay);
    if (dun) Entrance_RemapDungeonDoors(dun_assign, dun_n, g_entrance_overlay, len);
  }
  // Decoupled (D.4): install the one-way exit permutation(s) (regenerated
  // deterministically) + the exit edges for the tracker. Reset both first so a
  // re-install starts clean; the cave path also arms the runtime arrival replay.
  if (decoupled || decoupled_dun || decoupled_cross) {
    Decoupled_Reset();
    Dungeon_Decoupled_Reset();
    Cross_Decoupled_Reset();
    if (decoupled_cross) {
      // Cross + decoupled: one-way exits over the mixed pool. NO logic edges
      // (coupled-equivalent reachability; the ENTRY logic is ApplyCrossOverrides
      // above). net permutes the combined endpoint space; the runtime resolves a
      // cave-vs-dungeon target per source door at the entry hook.
      uint8 cd_assign[kEntranceMaxInteriors];
      int cdn = Entrance_ComputeCrossDecoupledExit(&es, seed, h->entrance_attempt, cd_assign);
      if (cdn > 0) {
        g_cross_decoupled_n = (cdn > kEntranceMaxInteriors) ? kEntranceMaxInteriors : cdn;
        memcpy(g_cross_net, cd_assign, (size_t)g_cross_decoupled_n);
        g_cross_decoupled_active = 1;
      }
    }
    if (decoupled) {
      uint8 dec_assign[kEntranceMaxInteriors];
      int dn = Entrance_ComputeDecoupledExit(&es, seed, h->entrance_attempt, dec_assign);
      if (dn > 0) {
        Entrance_ApplyDecoupledExitEdges(dec_assign, dn);  // tracker reachability
        g_decoupled_n = (dn > kEntranceMaxInteriors) ? kEntranceMaxInteriors : dn;
        memcpy(g_decoupled_net, dec_assign, (size_t)g_decoupled_n);
        g_decoupled_active = 1;
      }
    }
    if (decoupled_dun) {
      // No logic edges (coupled-equivalent reachability is conservative + correct);
      // net' is the runtime exit-room redirect only. Regenerated from (seed, attempt).
      uint8 dd_assign[kEntranceMaxInteriors];
      int ddn = Entrance_ComputeDungeonDecoupledExit(&es, seed, h->entrance_attempt, dd_assign);
      if (ddn > 0) {
        g_dungeon_decoupled_n = (ddn > kEntranceDungeonCount) ? kEntranceDungeonCount : ddn;
        memcpy(g_dungeon_decoupled_net, dd_assign, (size_t)g_dungeon_decoupled_n);
        g_dungeon_decoupled_active = 1;
      }
    }
  }
  g_entrance_overlay_orig = (const uint8 *)g_asset_ptrs[126];
  g_asset_ptrs[126] = g_entrance_overlay;
}

// Coupling helper (Stage 2): for the overworld entry hook. Given the door-slot
// `lx`, return the room the dungeon-exit room-keyed search should target so the
// player returns to THIS source door (rather than the loaded dungeon's vanilla
// door). 0 when the slot isn't a shuffled dungeon door (caves auto-couple; an
// unredirected door needs no override). Reads the saved vanilla table.
uint16 Rando_EntranceCoupledExitRoom(uint16 lx) {
  if (g_entrance_overlay_orig == NULL) return 0;
  uint32 len = kOverworld_Entrance_Id_SIZE;
  if (lx >= len) return 0;
  uint8 vanilla_id = g_entrance_overlay_orig[lx];
  // Only when this door was actually redirected (overlay value differs).
  if (kOverworld_Entrance_Id[lx] == vanilla_id) return 0;
  return Entrance_DungeonSourceExitRoom(vanilla_id);  // 0 if not a v1 dungeon door
}

// Cross-category coupling (Stage 3): true iff door-slot `lx` is a CAVE source
// door that the overlay redirected to a DUNGEON interior (loaded room < 0x100).
// Such a door takes the room-keyed search exit (it loads a dungeon room) but must
// return to the cave — the entry hook sets g_rando_entrance_force_cached so the
// exit uses the cached source-cave position instead. (cave→cave loads a cave room
// and already uses the cached branch, so it is intentionally NOT flagged here —
// keeps the playtest-confirmed within-category path untouched.)
bool Rando_EntranceForceCachedExit(uint16 lx) {
  g_rando_force_cached_room = 0;  // set together with the flag (only on the true path)
  if (g_entrance_overlay_orig == NULL) return false;
  uint32 len = kOverworld_Entrance_Id_SIZE;
  if (lx >= len) return false;
  uint8 vanilla_id = g_entrance_overlay_orig[lx];
  if (!Entrance_IsCaveEntranceId(vanilla_id)) return false;   // source not a cave
  uint8 target = kOverworld_Entrance_Id[lx];
  if (target == vanilla_id) return false;                     // not redirected
  const uint16 *rooms = kEntranceData_rooms;
  uint32 rc = kEntranceData_rooms_SIZE / 2u;
  if (rooms == NULL || target >= rc) return false;
  if (rooms[target] >= 0x100) return false;                   // target not a dungeon
  // Stash the SOURCE cave's room so the exit can restore it into
  // dungeon_room_index before LoadCachedEntranceProperties (whose vanilla
  // room-keyed Y-adjust would otherwise see the loaded dungeon room).
  if (vanilla_id < rc) g_rando_force_cached_room = rooms[vanilla_id];
  return true;
}

// Dungeon decoupled (Insanity): the exit-search target room for a one-way dungeon
// exit. Keyed on the LOADED dungeon (the overlay value at slot lx), so exiting the
// dungeon physically behind this door emerges at net'[loaded]'s overworld door
// instead of the source. 0 when inactive, not a pooled dungeon, or a self-map
// (caller keeps the coupled return-to-source). Mirrors Rando_EntranceCoupledExitRoom.
uint16 Rando_EntranceDungeonDecoupledExitRoom(uint16 lx) {
  if (!g_dungeon_decoupled_active) return 0;
  uint32 len = kOverworld_Entrance_Id_SIZE;
  if (lx >= len) return 0;
  uint8 loaded_id = ((const uint8 *)kOverworld_Entrance_Id)[lx];  // overlay → loaded dungeon
  return Entrance_DungeonDecoupledExitRoom(g_dungeon_decoupled_net,
                                           g_dungeon_decoupled_n, loaded_id);
}

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
// Boss-shuffle assignment for the active slot — drives BOTH the render redirect
// (BossShuffle_Generate installs g_boss_assignment in shuffle_boss.c) AND the
// in-game logic VM (Rando_SetBossAssignment, so OP_CAN_KILL_BOSS reachability /
// the tracker agree with the bosses actually spawned). Regenerated from
// (settings, base seed) at slot load, same as prize/medallion.
static uint8 g_rando_active_boss_assignment[16];

void Rando_ActivateSidecarSlot(const RandoSidecarSlot *src) {
  if (src == NULL || src->header.slot_kind != kSlotKind_Randomizer) {
    Rando_DeactivateSlot();
    return;
  }
  // add-rando-door-shuffle — regenerate the door layout BEFORE installing any
  // slot state. The layout is not serialized; it regenerates from
  // (seed, settings, door_attempt @76). Drift HARD-FAILS: a regenerated
  // layout whose digest differs from the persisted @77-79 value can make the
  // certified-beatable placement unbeatable, so the slot is refused (treated
  // as no-rando) rather than silently loaded — unlike entrance shuffle's
  // non-blocking version-drift warning. Vanilla-door slots skip all of this.
  static DoorShuffleLayout s_door_layout;
  bool door_active = false;
  if (src->header.settings_present) {
    RandoSettings ds;
    if (Settings_CanonicalDeserialize(src->settings_canonical, &ds) == 0 &&
        Settings_EffectiveDoorShuffle(&ds) != kDoorShuffle_Vanilla) {
      uint64 slot_seed = SlotSeedFromShareString(src->header.share_string);
      bool ok = DoorShuffle_Generate(slot_seed, src->header.door_attempt,
                                     kDoorShuffle_MvpDungeonMask, &s_door_layout);
      uint32 digest = ok ? (DoorShuffle_LayoutDigest(&s_door_layout) & 0xFFFFFF) : 0;
      if (!ok || digest != src->header.door_digest24) {
        fprintf(stderr,
                "Rando: door-shuffle layout drift (regen digest %06x != slot %06x) "
                "— refusing to activate this slot on this build\n",
                (unsigned)digest, (unsigned)src->header.door_digest24);
        Rando_DeactivateSlot();
        return;
      }
      // Stage-1b — validate the kind overlay (relocated key doors) BEFORE any
      // slot state installs, on the same refusal pathway as digest drift: a
      // chosen key door the overlay can't render makes the certified-beatable
      // placement unbeatable, so refuse rather than load.
      if (DoorRt_KindOverlaySelfCheck(&s_door_layout) != 0) {
        fprintf(stderr,
                "Rando: door-shuffle kind overlay rejected this layout "
                "— refusing to activate this slot on this build\n");
        Rando_DeactivateSlot();
        return;
      }
      door_active = true;
    }
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
  g_rando_boomerang_owned = src->header.boomerang_owned;
  g_rando_bow_owned = src->header.bow_owned;
  // Phase B Inverted runtime — capture the slot's world_state from the
  // additive @68 ext byte. Only trust it when settings_ext_present is set
  // (older slots wrote 0 there, which already maps to kWorldState_Open).
  g_rando_active_world_state = src->header.settings_ext_present
                                   ? src->header.world_state
                                   : (uint8)kWorldState_Open;
  // Phase C — install the entrance-shuffle door overlay + region overrides for
  // this slot (no-op when the slot carries no cave shuffle). Done before the
  // hint regeneration below so hints see the shuffled reachability, and before
  // the tracker repaint counter bump.
  Entrance_RuntimeInstall(&src->header);

  // add-rando-door-shuffle — install the drift-checked regenerated layout:
  // logic oracle + the runtime redirect table. (Mutually exclusive with
  // entrance shuffle per apply_derived_rules, so the two installs never
  // contend.) The Stage-1b door-KIND overlay installs here too once built.
  if (door_active) {
    Rando_SetDoorLogicLayout(&s_door_layout, s_door_layout.shuffled_mask);
    DoorRt_Reset();
    for (int i = 0; i < kDoorTbl_DoorCount; i++) {
      if (s_door_layout.pairing[i] != 0xFFFF)
        DoorRt_SetLink((uint16)i, s_door_layout.pairing[i]);
    }
    // Stage-1b kind overlay (relocated/un-keyed key-door KINDS). Cannot fail
    // here — DoorRt_KindOverlaySelfCheck validated this exact layout in the
    // gate above — but stay on the refusal pathway if it ever does.
    if (!DoorRt_InstallKindOverlay(&s_door_layout)) {
      fprintf(stderr, "Rando: door-shuffle kind overlay install failed — deactivating slot\n");
      Rando_DeactivateSlot();
      return;
    }
    DoorRt_Activate();
    g_wanted_zelda_features1 |= kFeatures1_DoorShuffleActive;
    enhanced_features1 |= kFeatures1_DoorShuffleActive;
  } else {
    DoorRt_Reset();
    Rando_SetDoorLogicLayout(NULL, 0);
    g_wanted_zelda_features1 &= ~(uint32)kFeatures1_DoorShuffleActive;
    enhanced_features1 &= ~(uint32)kFeatures1_DoorShuffleActive;
  }

  // #82 Inverted world-state — repoint the static Inverted entrance/exit
  // overrides (Link's House<->Bomb Shop, GT<->AT). No-op unless the slot is
  // Inverted. Runs AFTER Entrance_RuntimeInstall, which is itself a no-op on an
  // Inverted slot (the shuffle bails for non-Open/Standard), so the two never
  // contend for g_asset_ptrs[126].
  InvertedEntrances_Install(g_rando_active_world_state);
  // Inverted DW->LW under-rock warps (overworld-secret type-0x82). Separate
  // asset set (157/158) from the entrance overrides, so order is independent.
  InvertedSecrets_Install(g_rando_active_world_state);
  // Inverted hole-only Ganon relocation: append the no-art pit blocks to a
  // kMap16ToMap8 (asset 70) shadow. Separate asset, order-independent.
  InvertedHoleBlocks_Install(g_rando_active_world_state);

  // Cosmetic shuffles: when CosmeticSeed is 0 (default), the look tracks the
  // slot's seed_u64 (share_string bytes [21..28] LE). Re-seeds palette + music
  // tables; the sprite pick already happened at launch (documented limitation).
  Cosmetic_SetSeed(g_config.cosmetic_seed,
                   SlotSeedFromShareString(src->header.share_string));

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
      // FIX #6 — the placer seeds the prize/medallion shuffle from the ACCEPTED
      // attempt's per-attempt seed, not the base seed. place_assumed_fill_attempt
      // runs PrizeShuffle_Run/MedallionShuffle_Run on attempt_seed =
      // base_seed ^ (attempt * 0x9E3779B97F4A7C15) (see Place_AssumedFill). Re-
      // derive with the SAME perturbation so the runtime falling-prize sprite
      // (dungeon.c RandoFallingPrizeIndex) + OP_HAS_PRIZE tracker reachability
      // agree with the prize/medallion BAKED into the stored placement table.
      // prize_attempt is 0 for the common attempt-0 case and for older/v1 slots
      // (XOR with 0 == the legacy base-seed derivation), preserving compat.
      uint64 shuffle_seed = ss.seed_u64 ^
          ((uint64)src->header.prize_attempt * 0x9E3779B97F4A7C15ull);
      Rng_SeedFromU64(&shuffle_rng, shuffle_seed);
      PrizeShuffle_Run(&g_rando_active_settings, &shuffle_rng, g_rando_active_prize_assignment);
      MedallionShuffle_Run(&g_rando_active_settings, &shuffle_rng, g_rando_active_medallion_assignment);
      Rando_SetDungeonPrizeAssignment(g_rando_active_prize_assignment);
      Rando_SetMedallionAssignment(g_rando_active_medallion_assignment);
      // Phase B Slice 8 — INSTALL the drop shuffle for this slot so the runtime
      // drop substitution (DropShuffle_Lookup in src/sprite.c) fires. Drop
      // sprites (hearts/rupees/bombs/...) use the always-loaded common prize
      // GFX, so a shuffled drop renders correctly. Regenerated deterministically
      // from (settings, seed) — NOT off the prize/medallion `shuffle_rng` stream
      // above — matching the headless --generate-seed path + the spoiler. Off →
      // identity (byte-identical to vanilla).
      (void)DropShuffle_Generate(&g_rando_active_settings, ss.seed_u64,
                                 &g_session_placement_table, NULL);
      // Boss shuffle RENDER (Enemizer pointer-redirect model). INSTALL the boss
      // assignment so a shuffled boss room redirects its sprite-data + sprite-gfx
      // to the assigned boss's home boss room (BossShuffle_RenderHomeRoom, consumed
      // in dungeon.c / sprite.c). Regenerated from (settings, BASE seed) — matching
      // the placer's logic install (Place_AssumedFill) + the spoiler — so the
      // bosses spawned at runtime are exactly the ones placement gated on. Off →
      // vanilla identity → RenderHomeRoom returns 0xFFFF → byte-identical to
      // vanilla. Also install the SAME table into the logic VM
      // (Rando_SetBossAssignment) so the in-game tracker / OP_CAN_KILL_BOSS
      // reachability agree with the spawned bosses (else the tracker would gate on
      // the vanilla boss while the shuffled one is in the room).
      (void)BossShuffle_Generate(&g_rando_active_settings, ss.seed_u64,
                                 g_rando_active_boss_assignment);
      Rando_SetBossAssignment(g_rando_active_boss_assignment);
      // add-rando-enemy-shuffle — INSTALL the enemy (sprite-type) substitution
      // for this slot so the runtime swap (EnemyShuffle_PickDungeon/_Overworld
      // in src/sprite.c) fires. Regenerated deterministically from
      // (settings, BASE seed) — matching the headless path — so it's
      // reproducible for races. Off → inactive → vanilla enemies (byte-identical).
      // Orthogonal to placement (draws no fill RNG, adds no predicate).
      (void)EnemyShuffle_Generate(&g_rando_active_settings, ss.seed_u64);
      g_rando_active_settings_valid = true;
    }
  }
  if (!g_rando_active_settings_valid) {
    Rando_SetDungeonPrizeAssignment(NULL);
    Rando_SetMedallionAssignment(NULL);
    // No trustworthy (settings, seed) — do NOT guess a boss/drop shuffle.
    // Tear down any assignment a prior slot installed so it can't leak into
    // this (v1 / snapshot-restored) slot. Fail closed = vanilla bosses/drops.
    BossShuffle_Deactivate();
    Rando_SetBossAssignment(NULL);  // logic VM falls back to vanilla boss-kill
    DropShuffle_Deactivate();
    // add-rando-enemy-shuffle — fail closed: tear down any prior slot's enemy
    // substitution so it can't leak into this (v1 / snapshot-restored) slot.
    EnemyShuffle_Deactivate();
  }

  // add-rando-major-glitch D6 — couple a glitch-logic slot to the JP-1.0
  // glitch runtime flag. AUTHORITATIVE runtime guarantee: runs on EVERY slot
  // activation (generate->play AND reload->play, incl. imported share strings),
  // unlike the generate-time recommend path. When the recovered settings show
  // the placement assumed a restored glitch (logic>=OverworldGlitches or the
  // fake-flippers trick), force the flag on live: g_config (persist),
  // g_wanted_zelda_features (survives the per-frame mirror + a mid-session
  // Config_ApplyLive), and enhanced_features0 (this frame). Only force ON,
  // never off — a non-glitch slot leaves the user's own setting untouched, so a
  // plain logic=0 / no-glitch-trick seed never gets the flag forced. The
  // point-of-use gate JpGlitchEnabled() still self-suppresses under side-by-side
  // (!ZeldaIsEmulatorAttached()), so this stays RAM-compare-safe. features0 is
  // config state, NOT canonical settings → placement/corpus byte-identical.
  if (g_rando_active_settings_valid &&
      Rando_SettingsAssumeJpGlitches(&g_rando_active_settings)) {
    g_config.features0      |= kFeatures0_RestoreJpGlitches;
    g_wanted_zelda_features |= kFeatures0_RestoreJpGlitches;
    enhanced_features0      |= kFeatures0_RestoreJpGlitches;
  }

  // Persist the swordless flag in g_ram so a StateRecorder snapshot captures it
  // (g_ram is restored verbatim by LoadSnesState on replay/Ctrl+F1 restore, but
  // the C-static g_rando_active_settings is NOT). Rando_IsSwordlessActive falls
  // back to this byte when post-restore settings are NULL, keeping the runtime
  // swordless patches (hammer-Ganon/Agahnim, medallion/tablet/curtain) firing.
  // Authoritative against the recovered settings: 0 unless this slot is known
  // swordless. Cleared in Rando_DeactivateSlot. Rando-gated — vanilla never
  // touches this byte.
  g_rando_swordless = (g_rando_active_settings_valid &&
                       g_rando_active_settings.mode_weapons == kModeWeapons_Swordless)
                          ? 1 : 0;

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
    if (g_rando_active_settings_valid) {
      // Most reliable source: the full canonical settings blob recovered just
      // above (the same one the reachability engine consumes). It carries the
      // real `hints` and `goal` axes, so it is immune to a stale or partially
      // written header ext byte — which would otherwise leave hints silently
      // off even though the seed was generated with hints on.
      hint_settings = g_rando_active_settings;
    } else {
      // No canonical blob (older v1 slot / snapshot restore). Fall back to the
      // additive header ext byte, or default hints-on for the oldest slots so
      // existing rando slots still surface telepathic-tile hints. goal stays at
      // the Settings_SetDefaults value (Murahdahla won't fire unless it happens
      // to be a Triforce/Ganon-hunt default).
      Settings_SetDefaults(&hint_settings);
      if (src->header.settings_ext_present) {
        hint_settings.hints = src->header.hints_setting;
        hint_settings.goal = src->header.goal;
      } else {
        hint_settings.hints = kHintsMode_On;
      }
    }
    Rando_GenerateHints(&hint_settings, &g_session_placement_table, NULL);
  }
  // === Phase B hints: end ===
}

void Rando_DeactivateSlot(void) {
  // Phase C — restore the vanilla door table + clear entrance region overrides
  // before anything else (mirror of Entrance_RuntimeInstall in Activate).
  Entrance_RuntimeTeardown();
  // add-rando-door-shuffle — clear the per-seed door redirect + logic layout
  // (mirror of DoorShuffle_RuntimeInstall in Activate).
  DoorRt_Reset();
  Rando_SetDoorLogicLayout(NULL, 0);
  g_wanted_zelda_features1 &= ~(uint32)kFeatures1_DoorShuffleActive;
  enhanced_features1 &= ~(uint32)kFeatures1_DoorShuffleActive;
  // #82 Inverted override teardown — reverse of the Activate install order.
  // Restores g_asset_ptrs[126/130/131] to their saved vanilla originals.
  InvertedEntrances_Teardown();
  InvertedSecrets_Teardown();  // restore g_asset_ptrs[157/158]
  InvertedHoleBlocks_Teardown();  // restore g_asset_ptrs[70]
  // Phase B Slice 7/8 — tear down the boss + drop shuffle so the sprite
  // substitution reverts to a hard passthrough (vanilla bosses/drops) once no
  // rando slot is active; pairs with the install in Rando_ActivateSidecarSlot.
  BossShuffle_Deactivate();
  Rando_SetBossAssignment(NULL);  // logic VM back to vanilla boss-kill fallback
  DropShuffle_Deactivate();
  EnemyShuffle_Deactivate();  // add-rando-enemy-shuffle — revert to vanilla enemies
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
  g_rando_boomerang_owned = 0;
  g_rando_bow_owned = 0;
  // Transient Retro take-any redirect target. Reset with the other per-slot
  // transients so a stale door id from a prior slot can't mis-key a host-room
  // shop after a slot switch. (Within a slot it is set/cleared by
  // Overworld_UseEntrance; clearing it here is the slot-boundary backstop.)
  g_rando_takeany_door_id = 0;
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

  // Clear the persisted swordless flag so a stale byte can't make a subsequent
  // non-swordless / non-rando slot read as swordless (pairs with the write in
  // Rando_ActivateSidecarSlot).
  g_rando_swordless = 0;
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

bool Rando_ActiveSlotHidesSpoiler(void) {
  // Fail CLOSED: unknown settings (NULL) are treated as race so a race seed
  // whose settings weren't recovered (snapshot replay / v1 slot) can't leak.
  const RandoSettings *s = Rando_GetActiveSettings();
  return s == NULL || s->race_mode;
}

// True when the active slot is swordless (mode.weapons=swordless). Gates the
// runtime swordless patches (hammer damages Ganon/Agahnim, medallions cast
// without a sword, Agahnim curtains pre-opened, tablets hammer-readable) so they
// fire ONLY under swordless and never alter vanilla/non-swordless behavior.
// When settings are known, that is authoritative. When settings are unknown
// (NULL — snapshot replay-restore or pre-swordless v1 slot), fall back to the
// persisted g_rando_swordless flag in g_ram, which LoadSnesState restores
// verbatim and Rando_ActivateSidecarSlot set from the slot's recovered
// settings. Without this fallback a Ctrl+F1 replay-restore of a swordless seed
// (which doesn't re-run activation) would revert to sword-required gates
// (medallion/tablet/Agahnim/Ganon) and softlock. Gated on an active slot so a
// cleared/zero byte under a non-rando slot reads as not-swordless.
bool Rando_IsSwordlessActive(void) {
  const RandoSettings *s = Rando_GetActiveSettings();
  if (s != NULL) return s->mode_weapons == kModeWeapons_Swordless;
  return g_rando_slot_active != 0 && g_rando_swordless != 0;
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
  // Read true ownership (g_rando_bow_owned) so a player who owns silver but has
  // the slot toggled to wood arrows still counts as silver-capable; fall back to
  // the raw byte for pre-feature saves (ownership 0).
  uint8 bowb = link_item_bow;
  bool has_silver = (g_rando_bow_owned & kRandoBow_Silver) || bowb >= 3;
  bool has_bow = (g_rando_bow_owned & kRandoBow_Wood) || bowb >= 1;
  if (has_silver) {
    out->by_item_id[ITEM_ProgressiveBow] = 2;
    out->by_item_id[ITEM_SilverArrowUpgrade] = 1;
  } else if (has_bow) {
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

  // Boomerangs: byte 1=blue, 2=red (separate rando items). Read true ownership
  // (g_rando_boomerang_owned) so a player who owns both but has the slot toggled
  // to one color still counts as having the other; fall back to the raw byte for
  // pre-feature saves (ownership 0).
  if ((g_rando_boomerang_owned & kRandoBoomerang_Blue) || link_item_boomerang == 1)
    out->by_item_id[ITEM_BlueBoomerang] = 1;
  if ((g_rando_boomerang_owned & kRandoBoomerang_Red) || link_item_boomerang == 2)
    out->by_item_id[ITEM_RedBoomerang] = 1;

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

  // Dungeon items. Vanilla-mode classes are logically available in-place, so
  // pre-grant them exactly as the placer does (shared helper). For shuffled
  // (Dungeon/Wild) classes, read what the player has ACTUALLY collected from the
  // live vanilla per-dungeon cells and map game-dungeon index -> registry id.
  // Symbolic ITEM_ ids (not arithmetic) avoid the game-order vs registry-order
  // mismatch (e.g. ToH/Castle-Tower swap). 0xFFFF = no such item for that
  // dungeon (HC/Castle-Tower have no big key/map/compass).
  if (g_rando_active_settings_valid) {
    const RandoSettings *st = &g_rando_active_settings;
    Rando_SeedVanillaDungeonItems(out, st);

    // game dungeon index (0=HC,1=unused,2=EP,3=DP,4=CT,5=PoD,6=SP,7=SW,8=TT,
    // 9=IP,10=ToH,11=MM,12=TR,13=GT) -> registry item id.
    static const uint16 kGToSmallKey[16] = {
      ITEM_SmallKey_HyruleCastleEscape, 0xFFFF, ITEM_SmallKey_EasternPalace,
      ITEM_SmallKey_DesertPalace, ITEM_SmallKey_HyruleCastleTower,
      ITEM_SmallKey_PalaceOfDarkness, ITEM_SmallKey_SwampPalace,
      ITEM_SmallKey_SkullWoods, ITEM_SmallKey_ThievesTown, ITEM_SmallKey_IcePalace,
      ITEM_SmallKey_TowerOfHera, ITEM_SmallKey_MiseryMire, ITEM_SmallKey_TurtleRock,
      ITEM_SmallKey_GanonsTower, 0xFFFF, 0xFFFF,
    };
    static const uint16 kGToBigKey[16] = {
      0xFFFF, 0xFFFF, ITEM_BigKey_EasternPalace, ITEM_BigKey_DesertPalace, 0xFFFF,
      ITEM_BigKey_PalaceOfDarkness, ITEM_BigKey_SwampPalace, ITEM_BigKey_SkullWoods,
      ITEM_BigKey_ThievesTown, ITEM_BigKey_IcePalace, ITEM_BigKey_TowerOfHera,
      ITEM_BigKey_MiseryMire, ITEM_BigKey_TurtleRock, ITEM_BigKey_GanonsTower,
      0xFFFF, 0xFFFF,
    };
    static const uint16 kGToMap[16] = {
      0xFFFF, 0xFFFF, ITEM_Map_EasternPalace, ITEM_Map_DesertPalace, 0xFFFF,
      ITEM_Map_PalaceOfDarkness, ITEM_Map_SwampPalace, ITEM_Map_SkullWoods,
      ITEM_Map_ThievesTown, ITEM_Map_IcePalace, ITEM_Map_TowerOfHera,
      ITEM_Map_MiseryMire, ITEM_Map_TurtleRock, ITEM_Map_GanonsTower, 0xFFFF, 0xFFFF,
    };
    static const uint16 kGToCompass[16] = {
      0xFFFF, 0xFFFF, ITEM_Compass_EasternPalace, ITEM_Compass_DesertPalace, 0xFFFF,
      ITEM_Compass_PalaceOfDarkness, ITEM_Compass_SwampPalace, ITEM_Compass_SkullWoods,
      ITEM_Compass_ThievesTown, ITEM_Compass_IcePalace, ITEM_Compass_TowerOfHera,
      ITEM_Compass_MiseryMire, ITEM_Compass_TurtleRock, ITEM_Compass_GanonsTower,
      0xFFFF, 0xFFFF,
    };
    for (int g = 0; g < 14; g++) {
      uint16 bit = (uint16)(0x8000u >> g);
      if (Settings_EffectiveSmallKeysMode(st) != kDungeonItemMode_Vanilla &&
          kGToSmallKey[g] != 0xFFFF)
        out->by_item_id[kGToSmallKey[g]] = link_keys_earned_per_dungeon[g];
      if (st->dungeon_big_keys_mode != kDungeonItemMode_Vanilla &&
          kGToBigKey[g] != 0xFFFF && (link_bigkey & bit))
        out->by_item_id[kGToBigKey[g]] = 1;
      if (st->dungeon_maps_mode != kDungeonItemMode_Vanilla &&
          kGToMap[g] != 0xFFFF && (link_dungeon_map & bit))
        out->by_item_id[kGToMap[g]] = 1;
      if (st->dungeon_compasses_mode != kDungeonItemMode_Vanilla &&
          kGToCompass[g] != 0xFFFF && (link_compass & bit))
        out->by_item_id[kGToCompass[g]] = 1;
    }
    // Retro genericKeys — the per-dungeon SmallKey cells above are all 0 under
    // genericKeys (keys live in the shared slot). Feed the live tracker/reach
    // panel the real shared count so the predicate VM's small-key collapse
    // (any door open with >=1 GenericKey) evaluates against the player's actual
    // keys. Matches the gen-time assumed inventory (by_item_id[ITEM_GenericKey]).
    if (Settings_GenericKeysActive(st))
      out->by_item_id[ITEM_GenericKey] = link_generic_keys;
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
  // Bow/boomerang: prefer true rando ownership (a swapped-down byte must not
  // read as the lower tier); fall back to the raw bytes with no slot active.
  uint8 bowb = link_item_bow;
  if (g_rando_slot_active && (g_rando_bow_owned & kRandoBow_Silver))
    out->bow = 2;                                    // silver
  else
    out->bow = (bowb >= 3) ? 2 : (bowb >= 1 ? 1 : 0);  // wood/silver
  if (g_rando_slot_active && (g_rando_boomerang_owned & kRandoBoomerang_Red))
    out->boomerang = 2;                              // red
  else
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

  // Crystals (7) and pendants (3). crystal_mask bit (N-1) = rando Crystal N
  // obtained, indexed by the PRIZE crystal number (kPrize_Crystal1..7) the
  // tracker and placement use — NOT the HUD display order. The per-crystal
  // link_has_crystals bits mirror prize_item_direct_grant: C1=0x10, C2=0x02,
  // C3=0x01, C4=0x40, C5=0x04, C6=0x20, C7=0x08. pendant_mask bit0=green,
  // 1=red (ToH/Wisdom), 2=blue (DP/Power) by in-game DISPLAY color (the registry
  // Red/Blue Pendant names are swapped vs display); PrizeIcon maps prize->bit.
  static const uint8 kCrystalMask[7] = { 0x10, 0x02, 0x01, 0x40, 0x04, 0x20, 0x08 };
  static const uint8 kPendantMask[3] = { 4, 1, 2 };
  uint8 cbits = link_has_crystals, pbits = link_which_pendants;
  for (uint8 i = 0; i < 7; i++) {
    if (cbits & kCrystalMask[i]) { out->crystal_mask |= (uint8)(1 << i); out->crystals++; }
  }
  for (uint8 i = 0; i < 3; i++) {
    if (pbits & kPendantMask[i]) { out->pendant_mask |= (uint8)(1 << i); out->pendants++; }
  }

  out->agahnim = Rando_IsLocationChecked(LOC_Agahnim);

  // Per-dungeon items (indexed by game-side dungeon index; bitfields use bit
  // 0x8000 >> index). These reflect what the player has actually collected
  // (shuffled dungeon items write these vanilla cells on receipt; vanilla-mode
  // dungeons fill them in-place).
  for (int i = 0; i < 16; i++) out->dungeon_small_keys[i] = link_keys_earned_per_dungeon[i];
  out->bigkey_bits = link_bigkey;
  out->map_bits = link_dungeon_map;
  out->compass_bits = link_compass;
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
  // Use the SHARED placement+entrance regen (same code the generate path runs)
  // so the regenerated spoiler — including the entrance_mapping section that
  // feeds the SHA-256 stamp — is byte-identical. Without this, revealing a
  // race-mode + entrance-shuffle seed always false-failed as "tampered" because
  // the regen omitted entrance_mapping (the bug this fix closes). budget 0 = the
  // deterministic hard cap, matching race-mode generation.
  //
  // The accepted π's LOGIC overrides are left active by the helper. For an
  // active entrance slot this re-derives the identical π (deterministic from
  // seed/axes/accepted-attempt), so it restores — not pollutes — the slot's
  // tracker override state; and the in-binary reveal is gated to post-game, so
  // reachability is moot anyway. The gameplay door overlay is never touched
  // (the helper applies only logic overrides, not Entrance_RuntimeInstall).
  RandoEntranceRegen reg;
  if (!Rando_PlaceWithEntrances(&settings, seed_u64, /*budget_seconds=*/0, &table, &reg)) {
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
  // Match the generate-time spoiler's entrance_mapping section (the omission of
  // which caused the stamp mismatch on race-mode + entrance-shuffle seeds).
  Rando_SpoilerSetEntranceFields(&regen, &reg);
  // Match the generate-time spoiler's boss_assignments / drop_map sections
  // (see Rando_GenerateSlot in rando_generate.c). Omitting these caused the
  // SHA-256 stamp to mismatch for race seeds generated with boss_shuffle or
  // drop_shuffle on — same omission class as the entrance_mapping fix above.
  // The buffers are declared at function scope so they outlive Spoiler_WriteJson
  // (stamp input) and the later Spoiler_WriteText. Computed from the PURE forms
  // (no runtime-global side effects), deterministic from (settings, seed) →
  // byte-identical to the generate path. NULL pointers omit the section when off.
  uint8 regen_boss_assignment[16];
  uint8 regen_drop_map[kDropTableEntryCount];
  bool regen_drop_used_fallback = false;
  BossShuffle_ComputeAssignment(&settings, seed_u64, regen_boss_assignment);
  DropShuffle_ComputeAssignment(&settings, seed_u64, regen_drop_map, &regen_drop_used_fallback);
  regen.boss_assignment = settings.boss_shuffle ? regen_boss_assignment : NULL;
  regen.drop_map = settings.drop_shuffle ? regen_drop_map : NULL;
  regen.drop_used_fallback = regen_drop_used_fallback;
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
// Race-mode reveal completion latch. main_module_index is 0x19/0x1A only WHILE
// the Triforce room / credits are on screen; once the player leaves (file
// select, or reloads the beaten save) it reads not-beaten and the reveal would
// refuse even though the seed is done. Latch completion per session, keyed to
// the active slot's share string (loading a different slot re-keys and clears
// it). Ticked each frame from the main loop (Rando_NoteFrameForReveal).
// Session-scoped: an app restart clears it (re-beat, or use --reveal-spoiler).
static bool g_reveal_beaten = false;
static char g_reveal_beaten_share[sizeof g_rando_active_share_string] = {0};

void Rando_NoteFrameForReveal(void) {
  if (!g_rando_slot_active || g_rando_active_share_string[0] == 0)
    return;
  if (strcmp(g_reveal_beaten_share, g_rando_active_share_string) != 0) {
    snprintf(g_reveal_beaten_share, sizeof g_reveal_beaten_share, "%s",
             g_rando_active_share_string);
    g_reveal_beaten = false;
  }
  if (main_module_index == 0x19 || main_module_index == 0x1A)
    g_reveal_beaten = true;
}

// True iff the active slot's seed has been beaten this session (or the ending
// is on screen now). Both reveal gates route through this so they stay in sync.
static bool Rando_ActiveSlotBeaten(void) {
  if (main_module_index == 0x19 || main_module_index == 0x1A)
    return true;
  return g_reveal_beaten &&
         strcmp(g_reveal_beaten_share, g_rando_active_share_string) == 0;
}

RandoRevealResult Rando_RevealActiveSlotSpoiler(void) {
  if (!g_rando_slot_active || g_rando_active_share_string[0] == '\0') {
    fprintf(stderr, "rando reveal: no active randomizer slot.\n");
    return kRandoReveal_FileNotFound;
  }
  // §62 cluster-audit MED-1 — anti-cheat gate. Race-mode's design intent
  // is the spoiler stays off-disk until post-race. An in-binary key with
  // no terminal-state gate lets a self-disciplined runner peek mid-race
  // and defeats the design. Gate the in-binary action on game-completion:
  // the seed is beaten ONLY at TriforceRoom (0x19) or Credits (0x1A).
  // Deliberately NOT `>= 0x18` — that also matches GanonEmerges (0x18, the
  // fight is only just starting) and SpawnSelect (0x1B, the spawn-point menu
  // the load path transiently passes through, blipping "completed" for one
  // frame on slot load). Mirrors the beaten check in auto_tracker.c.
  // The `--reveal-spoiler=<path>` CLI flow stays unconditional (no in-
  // game state to check) for tournament admins / post-race tooling.
  if (!Rando_ActiveSlotBeaten()) {
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

bool Rando_CanRevealActiveSlotSpoiler(void) {
  // Mirrors the gate inside Rando_RevealActiveSlotSpoiler(): an active slot with
  // a captured share string, past the anti-cheat completion threshold — the seed
  // is beaten ONLY at TriforceRoom (0x19) or Credits (0x1A). Keep this in sync
  // with that function (and the beaten check in auto_tracker.c).
  return g_rando_slot_active && g_rando_active_share_string[0] != '\0' &&
         Rando_ActiveSlotBeaten();
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
#if kRandoChestLookup_COUNT > 0
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

  // Regression guard — "got the Lamp twice". A Rupoor (ITEM_Rupoor, ALTTPR-only
  // junk, dispatch direct_rupoor) placed at a chest whose US-ROM vanilla item is
  // the Lamp (Hyrule Castle - Zelda's Cell, room 128, vanilla byte 0x12) must
  // dispatch to kRandoLttpSkip and DRAIN rupees — NOT fall through to the
  // vanilla code and grant a duplicate Lamp (the original bug: Rupoor had no
  // dispatch handler, so Rando_DispatchVanillaGrant returned vanilla_lttp_code).
  {
    uint16 rupoor_loc = chest_lookup(128, 0);  // Hyrule Castle - Zelda's Cell
    if (rupoor_loc != 0xFFFFu) {
      static RandoPlacement entries[1];
      entries[0].location_id = rupoor_loc;
      entries[0].item_id = ITEM_Rupoor;  // 110
      RandoPlacementTable t = { entries, 1 };
      Placement_Install(&t);
      link_rupees_goal = 50;
      uint8 lttp = Rando_ChestDispatch(128, 0, 0x12);  // 0x12 = vanilla Lamp
      if (lttp != kRandoLttpSkip) {
        fprintf(stderr,
          "Rando_SelfCheck: Rupoor chest dispatch should return kRandoLttpSkip "
          "(got 0x%02x) — would grant the chest's vanilla item (Lamp 0x12)\n",
          (unsigned)lttp);
        exit(2);
      }
      if (link_rupees_goal != 40) {
        fprintf(stderr,
          "Rando_SelfCheck: Rupoor should drain 10 rupees (goal 50 -> 40, got %u)\n",
          (unsigned)link_rupees_goal);
        exit(2);
      }
      // Floor: with < 10 rupees the goal must clamp to 0, never underflow (an
      // underflowed goal would make the HUD ticker race the count UPWARD).
      link_rupees_goal = 4;
      (void)Rando_ChestDispatch(128, 0, 0x12);
      if (link_rupees_goal != 0) {
        fprintf(stderr,
          "Rando_SelfCheck: Rupoor with <10 rupees should clamp goal to 0 (got %u)\n",
          (unsigned)link_rupees_goal);
        exit(2);
      }
      Placement_Install(NULL);
      link_rupees_goal = 0;
    }
  }
#else
  // No chest table artifact present (e.g. CI build with no assets): the
  // kRandoChestLookup table is empty so chest mapping cannot be self-checked.
  // The unmapped fall-through above already exercises the empty-table path.
  fprintf(stderr,
    "Rando_SelfCheck: chest table empty (no assets extracted) - skipping "
    "chest-mapping spot-checks\n");
#endif

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

  // §6.2 HalfMagic/QuarterMagic direct-write tests. Magic upgrades are STRICTLY
  // PROGRESSIVE: either item advances link_magic_consumption by exactly one tier
  // (0->1->2), capped at 2, regardless of collection order, and never downgrades.
  {
    static RandoPlacement entries[1];
    entries[0].location_id = 166;
    entries[0].item_id = ITEM_HalfMagic;  // 41
    RandoPlacementTable t = { entries, 1 };
    Placement_Install(&t);
    // HalfMagic from full -> half.
    link_magic_consumption = 0;
    uint8 lttp = Rando_DispatchVanillaGrant(166, ITEM_BottleEmpty, 0x16);
    if (lttp != kRandoLttpSkip) {
      fprintf(stderr, "Rando_SelfCheck: HalfMagic dispatch should return kRandoLttpSkip (got 0x%02x)\n", (unsigned)lttp);
      exit(2);
    }
    if (link_magic_consumption != 1) {
      fprintf(stderr, "Rando_SelfCheck: HalfMagic dispatch (from 0) should advance to 1 (half)\n");
      exit(2);
    }
    // A SECOND magic upgrade (QuarterMagic) advances half -> quarter.
    entries[0].item_id = ITEM_QuarterMagic;  // 42
    Placement_Install(&t);
    Rando_DispatchVanillaGrant(166, ITEM_BottleEmpty, 0x16);
    if (link_magic_consumption != 2) {
      fprintf(stderr, "Rando_SelfCheck: 2nd magic upgrade should advance to 2 (quarter)\n");
      exit(2);
    }
    // A THIRD upgrade is capped (never exceeds quarter, never downgrades).
    Rando_DispatchVanillaGrant(166, ITEM_BottleEmpty, 0x16);
    if (link_magic_consumption != 2) {
      fprintf(stderr, "Rando_SelfCheck: magic upgrade past quarter should stay 2\n");
      exit(2);
    }
    // Progressive, order-independent: QuarterMagic collected FIRST gives half
    // (one tier), not quarter — the new behavior vs the old absolute jump-to-2.
    link_magic_consumption = 0;
    Rando_DispatchVanillaGrant(166, ITEM_BottleEmpty, 0x16);
    if (link_magic_consumption != 1) {
      fprintf(stderr, "Rando_SelfCheck: QuarterMagic from 0 should advance to 1 (progressive, not jump to 2)\n");
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

static void tsc_die(const char *msg) {
  fprintf(stderr, "[Tracker_SelfCheck] FAIL: %s\n", msg);
  exit(2);
}

// End-to-end check of the slot-recovery + live-reachability path (Phase 1a/1b),
// which the corpus / other selftests do NOT cover (the playable-slot path is
// otherwise test-free per CLAUDE.md). Generates a default placement, round-trips
// it through a sidecar slot + Rando_ActivateSidecarSlot, and asserts: settings
// recovered, prize/medallion shuffle assignments installed at activation, and
// reachability is non-empty from the empty starting inventory AND expands when a
// broad item kit is added. Pure (no file IO; g_ram inventory is zero at selftest
// time, before game init). Leaves no active slot (Deactivate at the end).
void Rando_TrackerSelfCheck(void) {
  RandoSettings s;
  Settings_SetDefaults(&s);
  uint64 seed = 0x0123456789abcdefull;

  static RandoPlacement entries[512];
  RandoPlacementTable table;
  table.entries = entries;
  table.count = 0;
  if (!Place_AssumedFill(&s, seed, 0, &table) && table.count == 0)
    tsc_die("placement failed");

  RandoSidecarSlot slot;
  memset(&slot, 0, sizeof(slot));
  slot.header.slot_kind = kSlotKind_Randomizer;
  slot.header.generator_version = (uint16)kGeneratorVersion;
  uint16 maxloc = 0;
  for (uint16 i = 0; i < table.count && i < 512; i++) {
    slot.placements[i] = table.entries[i];
    if (table.entries[i].location_id > maxloc) maxloc = table.entries[i].location_id;
  }
  slot.placement_count = table.count;
  slot.header.placement_table_size = (uint16)(((uint32)maxloc + 1) * 2);
  slot.header.settings_ext_present = 1;
  slot.header.world_state = s.world_state;
  slot.header.goal = s.goal;
  Settings_CanonicalSerialize(&s, slot.settings_canonical);
  slot.header.settings_present = 1;
  ShareString ss;
  memset(&ss, 0, sizeof(ss));
  ss.version = (uint8)kGeneratorVersion;
  ss.seed_u64 = seed;
  Share_PackBinary(&ss, slot.header.share_string);

  Rando_ActivateSidecarSlot(&slot);
  if (!Rando_HasActiveSettings()) tsc_die("settings not recovered after activate");
  const RandoSettings *rec = Rando_GetActiveSettings();
  if (rec == NULL || rec->world_state != s.world_state || rec->goal != s.goal ||
      rec->prize_shuffle != s.prize_shuffle || rec->medallion_shuffle != s.medallion_shuffle)
    tsc_die("recovered settings mismatch");
  if (Rando_GetDungeonPrizeAssignment() == NULL || Rando_GetMedallionAssignment() == NULL)
    tsc_die("shuffle assignments not installed at activate");

  RandoCounts counts;
  Rando_BuildRuntimeCounts(&counts);
  const RandoReachability *r0 = Logic_ComputeReachability(&counts, rec);
  int n0 = 0;
  for (uint32 i = 0; i < kRandoLocationsCount; i++)
    if (Reachability_HasLocation(r0, kRandoLocations[i].id)) n0++;
  if (n0 == 0) tsc_die("no locations reachable from the starting inventory");

  // A broad progression kit must strictly expand reachability (monotonic logic).
  counts.by_item_id[ITEM_ProgressiveSword] = 4;
  counts.by_item_id[ITEM_ProgressiveGlove] = 2;
  counts.by_item_id[ITEM_Hammer] = 1;
  counts.by_item_id[ITEM_Lamp] = 1;
  counts.by_item_id[ITEM_MoonPearl] = 1;
  counts.by_item_id[ITEM_Flippers] = 1;
  counts.by_item_id[ITEM_Hookshot] = 1;
  counts.by_item_id[ITEM_FireRod] = 1;
  counts.by_item_id[ITEM_ProgressiveBow] = 2;
  counts.by_item_id[ITEM_CaneOfSomaria] = 1;
  counts.by_item_id[ITEM_MagicMirror] = 1;
  counts.by_item_id[ITEM_Bombos] = counts.by_item_id[ITEM_Ether] = counts.by_item_id[ITEM_Quake] = 1;
  const RandoReachability *r1 = Logic_ComputeReachability(&counts, rec);
  int n1 = 0;
  for (uint32 i = 0; i < kRandoLocationsCount; i++)
    if (Reachability_HasLocation(r1, kRandoLocations[i].id)) n1++;
  if (n1 <= n0) tsc_die("reachability did not expand when a full item kit was added");

  Rando_DeactivateSlot();
  fprintf(stderr, "[Tracker_SelfCheck] OK (%d -> %d reachable)\n", n0, n1);
}

// Build a Randomizer sidecar slot for the given settings/seed into `slot`
// (placement + canonical settings + share string). Mirrors the slot-build in
// Rando_TrackerSelfCheck. The static placement scratch is safe to reuse because
// Rando_ActivateSidecarSlot copies the placements via Placement_Install.
static void rando_selfcheck_build_slot(RandoSidecarSlot *slot, RandoSettings *s, uint64 seed) {
  static RandoPlacement entries[512];
  RandoPlacementTable table = { entries, 0 };
  if (!Place_AssumedFill(s, seed, 0, &table) && table.count == 0)
    tsc_die("StartingInventory_SelfCheck: placement failed");
  memset(slot, 0, sizeof(*slot));
  slot->header.slot_kind = kSlotKind_Randomizer;
  slot->header.generator_version = (uint16)kGeneratorVersion;
  uint16 maxloc = 0;
  for (uint16 i = 0; i < table.count && i < 512; i++) {
    slot->placements[i] = table.entries[i];
    if (table.entries[i].location_id > maxloc) maxloc = table.entries[i].location_id;
  }
  slot->placement_count = table.count;
  slot->header.placement_table_size = (uint16)(((uint32)maxloc + 1) * 2);
  slot->header.settings_ext_present = 1;
  slot->header.world_state = s->world_state;
  slot->header.goal = s->goal;
  Settings_CanonicalSerialize(s, slot->settings_canonical);
  slot->header.settings_present = 1;
  ShareString ss;
  memset(&ss, 0, sizeof(ss));
  ss.version = (uint8)kGeneratorVersion;
  ss.seed_u64 = seed;
  Share_PackBinary(&ss, slot->header.share_string);
}

// Runtime starting-inventory injection wiring (Rando_TryGrantStartingInventory).
// Pure / in-process (no file IO, no full game init) — g_ram is zero at selftest
// time. Pins the grant + idempotency + Inverted-bunny + cold-boot-dedupe gates
// so a regression in the runtime grant wiring trips here, not only in playtest.
// Guards the slot_world_state_persistence (Inverted bunny softlock) + double-
// grant classes. Leaves no active slot and restores the g_ram cells it touches.
static void Rando_StartingInventorySelfCheck(void) {
  // (0) No active slot ⇒ injection must no-op.
  Rando_DeactivateSlot();
  g_rando_starting_inventory_granted = 0;
  if (Rando_TryGrantStartingInventory(NULL))
    tsc_die("StartingInventory: must no-op when no slot is active");

  RandoSettings s;
  RandoSidecarSlot slot;

  // (1) Inverted: Moon Pearl pre-grant on a FRESH save. The Magic Mirror is
  // deliberately NOT pre-granted (add-rando-inverted-dark-chapel-spawn) — it is a
  // found item so the spawn-select "Dark Mountain" option unlocks vanilla-style.
  Settings_SetDefaults(&s);
  s.world_state = kWorldState_Inverted;
  rando_selfcheck_build_slot(&slot, &s, 0x0123456789abcdefull);
  Rando_ActivateSidecarSlot(&slot);
  g_rando_starting_inventory_granted = 0;
  sram_progress_indicator = 0;  // brand-new save (pre-escape)
  link_item_moon_pearl = 0;
  link_item_mirror = 0;
  if (!Rando_TryGrantStartingInventory(NULL))
    tsc_die("StartingInventory(inverted): first inject should grant");
  if (g_rando_starting_inventory_granted != 1)
    tsc_die("StartingInventory(inverted): grant gate not set");
  if (link_item_moon_pearl != 1)
    tsc_die("StartingInventory(inverted): Moon Pearl not pre-granted");
  if (link_item_mirror != 0)
    tsc_die("StartingInventory(inverted): Magic Mirror must NOT be pre-granted");
  // Idempotent: a second call in the same boot must NOT re-grant.
  if (Rando_TryGrantStartingInventory(NULL))
    tsc_die("StartingInventory(inverted): second inject must be deduped");
  Rando_DeactivateSlot();

  // (2) Standard fresh save: injection grants and sets the gate.
  Settings_SetDefaults(&s);
  s.world_state = kWorldState_Standard;
  rando_selfcheck_build_slot(&slot, &s, 0x3ull);
  Rando_ActivateSidecarSlot(&slot);
  g_rando_starting_inventory_granted = 0;
  sram_progress_indicator = 0;
  if (!Rando_TryGrantStartingInventory(NULL))
    tsc_die("StartingInventory(standard): first inject should grant");
  if (g_rando_starting_inventory_granted != 1)
    tsc_die("StartingInventory(standard): grant gate not set");
  Rando_DeactivateSlot();

  // (3) Cold-boot dedupe: an in-progress save (progress > 0) must short-circuit
  // the escape-fill but still set the within-boot dedupe gate.
  Settings_SetDefaults(&s);
  rando_selfcheck_build_slot(&slot, &s, 0x7ull);
  Rando_ActivateSidecarSlot(&slot);
  g_rando_starting_inventory_granted = 0;
  sram_progress_indicator = 1;  // past Uncle's gift
  if (Rando_TryGrantStartingInventory(NULL))
    tsc_die("StartingInventory(cold-boot): progress>0 must short-circuit the grant");
  if (g_rando_starting_inventory_granted != 1)
    tsc_die("StartingInventory(cold-boot): dedupe gate must still be set");
  Rando_DeactivateSlot();

  // Restore the g_ram cells we touched so later selfchecks see a clean slate.
  g_rando_starting_inventory_granted = 0;
  link_item_moon_pearl = 0;
  link_item_mirror = 0;
  sram_progress_indicator = 0;
  fprintf(stderr, "[StartingInventory_SelfCheck] OK\n");
}

// End-to-end install check for the DROP shuffle runtime wiring + a guard that
// BOSS shuffle stays runtime-disabled. The per-module self-checks
// (BossShuffle/DropShuffle_SelfCheck) cover the ALGORITHM; the corpus is blind
// to boss/drop (orthogonal to placement). NOTHING else proves the slot-
// activation INSTALL path (Rando_ActivateSidecarSlot) regenerates + installs
// the drop table from the recovered (settings, seed) — so a regression there
// (wrong seed, off the prize/medallion RNG stream, skipped install, leak on
// reactivation) would be invisible until playtest. It ALSO guards the
// boss-shuffle RENDER install: a boss_shuffle=1 slot MUST install the boss
// assignment (render redirect + logic VM) matching ComputeAssignment, and a
// boss_shuffle=0 slot MUST leave the render a hard passthrough (vanilla).
static void Rando_ShuffleInstallSelfCheck(void) {
  RandoSettings s, off;
  RandoSidecarSlot slot;

  // (1) Drop shuffle ON: the install must (a) actually shuffle and (b) byte-
  // match DropShuffle_ComputeAssignment for the slot's (settings, seed).
  // Boss shuffle ON in the SAME slot must NOT install at runtime — boss
  // substitution is held back until per-boss GFX loading lands (a pure
  // sprite-type swap renders garbage; proven by playtest F12 of the EP boss
  // room — see Rando_ActivateSidecarSlot). This case guards both at once.
  Settings_SetDefaults(&s);
  s.boss_shuffle = 1;
  s.drop_shuffle = 1;
  const uint64 seedA = 0xA17F0001B0552233ull;
  rando_selfcheck_build_slot(&slot, &s, seedA);
  Rando_ActivateSidecarSlot(&slot);

  uint8 exp_drop[kDropTableEntryCount];
  bool fb = false;
  DropShuffle_ComputeAssignment(&s, seedA, exp_drop, &fb);

  // Non-identity sanity: the chosen seed must produce a real drop shuffle, else
  // the match assertion below would pass trivially against the vanilla table.
  off = s;
  off.drop_shuffle = 0;
  uint8 van_drop[kDropTableEntryCount];
  bool fb2 = false;
  DropShuffle_ComputeAssignment(&off, seedA, van_drop, &fb2);
  bool drop_diff = false;
  for (uint8 i = 0; i < kDropTableEntryCount; i++) if (exp_drop[i] != van_drop[i]) drop_diff = true;
  if (!drop_diff) tsc_die("ShuffleInstall: drop_shuffle on produced the identity (test seed is not a shuffle)");

  // Drop: installed runtime table must equal ComputeAssignment.
  for (uint8 i = 0; i < kDropTableEntryCount; i++)
    if (DropShuffle_Lookup(i) != exp_drop[i])
      tsc_die("ShuffleInstall: installed drop table != ComputeAssignment(settings, seed)");
  // Boss: the render install must be LIVE for a boss_shuffle=1 slot, must match
  // BossShuffle_ComputeAssignment for (settings, base seed), the LOGIC assignment
  // must be installed too, and the render redirect must fire for >=1 boss room
  // (else the test seed is not a real shuffle and these checks pass trivially).
  {
    uint8 exp_boss[16];
    BossShuffle_ComputeAssignment(&s, seedA, exp_boss);
    for (uint8 d = 0; d < 13; d++)
      if (BossShuffle_GetForDungeon(d) != exp_boss[d])
        tsc_die("ShuffleInstall: installed boss assignment != ComputeAssignment(settings, seed)");
    if (Rando_GetBossAssignment() == NULL)
      tsc_die("ShuffleInstall: boss LOGIC assignment not installed for a boss_shuffle=1 slot");
    static const uint16 kBossRoomsSC[10] = {200,51,7,90,6,41,172,222,144,164};
    bool any_redirect = false;
    for (uint8 i = 0; i < 10; i++)
      if (BossShuffle_RenderHomeRoom(kBossRoomsSC[i]) != 0xFFFF) any_redirect = true;
    if (!any_redirect)
      tsc_die("ShuffleInstall: boss_shuffle=1 produced no render redirect (test seed is not a shuffle)");
  }

  // Teardown reverts the drop table to a hard passthrough.
  Rando_DeactivateSlot();
  if (DropShuffle_Lookup(5) != 5)
    tsc_die("ShuffleInstall: drop table not torn down on deactivate");
  if (BossShuffle_GetForDungeon(1) != 0xFF || Rando_GetBossAssignment() != NULL)
    tsc_die("ShuffleInstall: boss assignment not torn down on deactivate");
  if (BossShuffle_RenderHomeRoom(200) != 0xFFFF)
    tsc_die("ShuffleInstall: boss render redirect not a passthrough after teardown");

  // (2) Reactivation with a DIFFERENT seed must OVERWRITE the drop table (no
  // stale leak); boss stays uninstalled.
  const uint64 seedB = 0xB0552244C0FFEE99ull;
  rando_selfcheck_build_slot(&slot, &s, seedB);
  Rando_ActivateSidecarSlot(&slot);
  uint8 expB[kDropTableEntryCount];
  bool fb3 = false;
  DropShuffle_ComputeAssignment(&s, seedB, expB, &fb3);
  for (uint8 i = 0; i < kDropTableEntryCount; i++)
    if (DropShuffle_Lookup(i) != expB[i])
      tsc_die("ShuffleInstall: reactivation did not overwrite the prior slot's drop table");
  {
    uint8 expBoss[16];
    BossShuffle_ComputeAssignment(&s, seedB, expBoss);
    for (uint8 d = 0; d < 13; d++)
      if (BossShuffle_GetForDungeon(d) != expBoss[d])
        tsc_die("ShuffleInstall: reactivation did not overwrite the prior slot boss assignment");
  }
  Rando_DeactivateSlot();

  // (3) Drop OFF slot: the install still runs (identity), proven because we
  // enter from a torn-down state — a skipped install would also read identity,
  // so re-confirm via a deactivate/activate boundary leaving the identity table.
  Settings_SetDefaults(&s);  // boss/drop default off
  const uint64 seedC = 0x0FF0C0DE0FF0C0DEull;
  rando_selfcheck_build_slot(&slot, &s, seedC);
  Rando_ActivateSidecarSlot(&slot);
  for (uint8 i = 0; i < kDropTableEntryCount; i++)
    if (DropShuffle_Lookup(i) != i)
      tsc_die("ShuffleInstall: off-slot drop table must be the identity");
  // Boss off: installed (vanilla identity) but the render redirect MUST be a hard
  // passthrough for every boss room -> byte-identical to vanilla.
  {
    static const uint16 kBossRoomsOff[10] = {200,51,7,90,6,41,172,222,144,164};
    for (uint8 i = 0; i < 10; i++)
      if (BossShuffle_RenderHomeRoom(kBossRoomsOff[i]) != 0xFFFF)
        tsc_die("ShuffleInstall: boss_shuffle=0 must produce no render redirect (vanilla)");
  }
  Rando_DeactivateSlot();

  fprintf(stderr, "[Rando_ShuffleInstallSelfCheck] OK\n");
}

void Rando_RunAllSelfChecks(void) {
  Rando_SelfCheck();
  Rando_Rng_SelfCheck();
  Share_SelfCheck();
  Settings_SelfCheck();
  Logic_SelfCheck();
  Placement_SelfCheck();
  Shuffles_SelfCheck();
  BossShuffle_SelfCheck();
  DropShuffle_SelfCheck();
  EnemyShuffle_SelfCheck();  // add-rando-enemy-shuffle
  RandoSave_SelfCheck();
  RandoGenerate_SelfCheck();
  RandoSnapshotTail_SelfCheck();
  TextField_SelfCheck();
  Hints_SelfCheck();
  Entrance_SelfCheck();
  Cosmetic_SelfCheck();
  Rando_TrackerSelfCheck();
  Rando_StartingInventorySelfCheck();
  Rando_ShuffleInstallSelfCheck();
  fprintf(stderr, "Rando_RunAllSelfChecks: all subsystems OK.\n");
}
