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
#include "customizer.h"  // Customizer_SelfCheck (add-rando-customizer-mode)
#include "rando_save.h"
#include "rando_generate.h"  // RandoGenerate_SelfCheck (slot SRAM-init self-test)
#include "seed_shape.h"  // SeedShape_SelfCheck
#include "rando_snapshot_tail.h"
#include "rando_textfield.h"
#include "item_ids.h"
#include "location_ids.h"
#include "dungeon_ids.h"
#include "chest_lookup.h"  // (room, ordinal) -> LOC_*; §6.3 codegen
#include "direct_grant_icons.h"  // kDirectGrantIcons[] (Phase B Slice 9)
#include "rando_hints.h"  // Rando_ClearHints (Phase B Slice 5)
#include "shuffle_entrance.h"  // Phase C entrance shuffle (overlay + self-check)
#include "inverted_entrances.h"  // #82 static Inverted entrance/exit override
#include "inverted_maps.h"  // InvertedHoleBlocks_Install (no-art Ganon pit shadow)
#include "shuffle_cosmetic.h"  // Cosmetic_SetSeed (cosmetic_seed=0 -> slot seed)
#include "medallion_icons.h"  // Rando_MedallionIcons_SelfCheck
#include "../ancilla.h"  // AncillaAdd_RandoIconReceipt (Phase B Slice 9)
#include "../config.h"  // g_config.cosmetic_seed
#include "../types.h"
#include "../variables.h"  // §6.2 progressive-dispatch reads link_sword_type etc.
#include "../assets.h"     // Phase C entrance overlay: g_asset_ptrs[126] / kOverworld_Entrance_Id
#include "../features.h"   // g_rando_triforce_piece_count
#include "../misc.h"       // §7.6 Link_CalculateSfxPan
#include "../sprite.h"     // Sprite_ShowMessageUnconditional (trap dialogue)
#include "../hud.h"        // §7.6 Hud_RefreshIcon
#include "../player.h"     // §7.6 Link_ReceiveItem
#include "third_party/sha256/sha256.h"

// ---------------------------------------------------------------------------
// g_assets_hash — populated by LoadAssets() in src/main.c after the asset
// blob is read and validated. See task 1.1a.
// ---------------------------------------------------------------------------
uint8 g_assets_hash[32];

static bool rando_instant_flute_active(void);
static bool rando_trap_decoy_icon(uint16 item_id, uint16 location_id,
                                  DirectGrantIconEntry *out);

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

// True for the progressive items whose progressive_to_lttp result depends on
// the player's CURRENT tier — so a 0xFF return from progressive_to_lttp means
// "already at max tier", NOT "unknown item". Keep in sync with the tiered
// cases in progressive_to_lttp above.
static bool rando_is_progressive_item(uint16 registry_id) {
  switch (registry_id) {
    case ITEM_ProgressiveSword:
    case ITEM_ProgressiveShield:
    case ITEM_ProgressiveArmor:
    case ITEM_ProgressiveGlove:
    case ITEM_ProgressiveBow:
      return true;
    default:
      return false;
  }
}

// §6.2 per-placed-dungeon counter helper. The vanilla LttP dispatcher
// Link_ReceiveItem indexes by `cur_palace_index_x2 >> 1` (the player's current
// dungeon). For rando placements where a key/map/compass belongs to a DIFFERENT
// dungeon than the player's current one, write the destination dungeon's RAM
// cell ourselves. All ALTTPR-id -> game-id conversion lives in dungeon_ids.h.
//
// Returns 1 if the placed item is a dungeon item and was direct-written.
// Caller treats this as "skip Link_ReceiveItem" via kRandoLttpSkip.
static int dungeon_item_direct_grant(uint16 registry_id) {
  uint8 dungeon = Rando_DungeonItemGameDungeon(registry_id);
  // Game-side indices range 0..13 (GT). 16 is the kUpperBitmasks size — past
  // that, Rando_DungeonBitForGameDungeon returns 0 and the OR would no-op.
  if (dungeon == 0xFF || dungeon >= 16) return 0;

  uint16 bit = Rando_DungeonBitForGameDungeon(dungeon);
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
    uint8 cur_slot = Rando_KeySlotFromRawPalace(cur);
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
static uint16 g_last_dispatched_location_id = 0xFFFFu;

uint16 Rando_LastDispatchedItemId(void) {
  return g_last_dispatched_item_id;
}

static bool rando_is_magic_upgrade_item(uint16 item_id) {
  return item_id == ITEM_HalfMagic || item_id == ITEM_QuarterMagic;
}

static uint16 rando_direct_grant_icon_item_pre_grant(uint16 item_id) {
  if (!rando_is_magic_upgrade_item(item_id))
    return item_id;
  return link_magic_consumption == 0 ? ITEM_HalfMagic : ITEM_QuarterMagic;
}

static uint16 rando_direct_grant_icon_item_post_grant(uint16 item_id) {
  if (!rando_is_magic_upgrade_item(item_id))
    return item_id;
  return link_magic_consumption >= 2 ? ITEM_QuarterMagic : ITEM_HalfMagic;
}

static const DirectGrantIconEntry *rando_direct_grant_icon_entry(uint16 item_id) {
  const size_t n = sizeof(kDirectGrantIcons) / sizeof(kDirectGrantIcons[0]);
  if ((size_t)item_id >= n || kDirectGrantIcons[item_id].gfx == 0)
    return NULL;
  return &kDirectGrantIcons[item_id];
}

static uint8 trap_ascii_to_font(char ch) {
  if (ch >= 'A' && ch <= 'Z') return (uint8)(ch - 'A');
  if (ch >= 'a' && ch <= 'z') return (uint8)(26 + (ch - 'a'));
  if (ch >= '0' && ch <= '9') return (uint8)(52 + (ch - '0'));
  switch (ch) {
    case '!': return 62;
    case '?': return 63;
    case '-': return 64;
    case '.': return 65;
    case ',': return 66;
    case '\'': return 81;
    case ' ': return 89;
    default: return 89;
  }
}

static int trap_write_ascii(uint8 *out, int o, const char *text) {
  while (*text && o < 240) out[o++] = trap_ascii_to_font(*text++);
  return o;
}

bool Rando_RenderTrapMessage(uint16 msg_id, uint8 *out_buffer) {
  if (msg_id != kRandoTrapDialogueId || out_buffer == NULL) return false;
  int o = 0;
  o = trap_write_ascii(out_buffer, o, "You are");
  out_buffer[o++] = 0x75;  // visual row 1 (middle)
  o = trap_write_ascii(out_buffer, o, "a fool!");
  out_buffer[o++] = 0x7f;  // terminator
  return true;
}

static bool rando_is_trap_item(uint16 item_id) {
  return item_id == ITEM_TrapDamage || item_id == ITEM_TrapFreeze;
}

enum {
  kRandoTrapEffect_None = 0,
  kRandoTrapEffect_Damage = 1,
  kRandoTrapEffect_Freeze = 2,
};

static uint8 g_rando_trap_stun_timer;
static uint8 g_rando_trap_effect;
static uint8 g_rando_trap_bad_sfx_timer;
static uint8 g_rando_trap_shove_timer;
static uint8 g_rando_trap_shove_dir;
static uint8 g_rando_trap_owns_forced_move;

static void rando_clear_trap_effect(void) {
  if (g_rando_trap_owns_forced_move) {
    force_move_any_direction = 0;
    g_rando_trap_owns_forced_move = 0;
  }
  g_rando_trap_stun_timer = 0;
  g_rando_trap_effect = kRandoTrapEffect_None;
  g_rando_trap_bad_sfx_timer = 0;
  g_rando_trap_shove_timer = 0;
  g_rando_trap_shove_dir = 0;
}

static bool rando_trap_stun_can_tick(void) {
  return main_module_index == 7 || main_module_index == 9 || main_module_index == 11;
}

static void rando_neutralize_trap_motion(void) {
  joypad1H_last = 0;
  joypad1L_last = 0;
  filtered_joypad_H = 0;
  filtered_joypad_L = 0;
  force_move_any_direction = 0;
  g_rando_trap_owns_forced_move = 0;

  Link_CancelDash();
  link_speed_setting = 0;
  link_y_vel = 0;
  link_x_vel = 0;
  link_actual_vel_x = 0;
  link_actual_vel_y = 0;
  link_actual_vel_z = 0;
  link_actual_vel_z_copy = 0;
  link_auxiliary_state = 0;
  button_mask_b_y = 0;
  bitfield_for_a_button = 0;
  button_b_frames = 0;
  link_delay_timer_spin_attack = 0;
  link_spin_attack_step_counter = 0;
  link_state_bits = 0;
  link_picking_throw_state = 0;
  link_grabbing_wall = 0;
  link_moving_against_diag_tile = 0;
  link_var30d = 0;
  link_var30e = 0;
  some_animation_timer_steps = 0;
  Link_ResetSwimmingState();
}

static uint8 rando_trap_recoil_dir(void) {
  static const uint8 kOppositeFacingDir[4] = {4, 8, 1, 2};
  return kOppositeFacingDir[(link_direction_facing >> 1) & 3];
}

static bool rando_is_bad_trap_wall_spark_residue(int k) {
  return ancilla_type[k] == 0x24 &&
         ancilla_item_to_link[k] == 5 &&
         ancilla_aux_timer[k] == 1 &&
         ancilla_timer[k] == 0 &&
         ancilla_step[k] == 4 &&
         ancilla_arr3[k] == 0 &&
         ancilla_G[k] == 0 &&
         ancilla_L[k] == 0;
}

static void rando_selfcheck_seed_bad_trap_wall_spark_residue(int k) {
  ancilla_type[k] = 0x24;
  ancilla_item_to_link[k] = 5;
  ancilla_aux_timer[k] = 1;
  ancilla_timer[k] = 0;
  ancilla_step[k] = 4;
  ancilla_arr3[k] = 0;
  ancilla_G[k] = 0;
  ancilla_L[k] = 0;
}

static void rando_clear_bad_trap_wall_spark_residue(void) {
  if (!g_rando_slot_active) return;
  if (link_something_with_hookshot || (bitmask_of_dragstate & 4)) return;

  for (int k = 0; k < 5; k++) {
    // Earlier trap feedback accidentally spawned type 0x24 through the wall
    // spark helper. Type 0x24 is the gravestone ancilla and does not expire
    // unless the real grave-push state owns it.
    if (rando_is_bad_trap_wall_spark_residue(k)) {
      ancilla_type[k] = 0;
    }
  }
}

static void rando_tick_trap_reveal_feedback(void) {
  if (g_rando_trap_bad_sfx_timer == 0) return;
  if (--g_rando_trap_bad_sfx_timer != 0) return;

  uint8 pan = Link_CalculateSfxPan();
  sound_effect_2 = pan | 0x26;
  sound_effect_1 = pan | (g_rando_trap_effect == kRandoTrapEffect_Freeze
                              ? 0x15
                              : 0x24);
  if (g_rando_slot_active) {
    AncillaAdd_SwordSwingSparkle(0x26, 4);
    if (g_rando_trap_effect == kRandoTrapEffect_Damage)
      AncillaAdd_DashTremor(29, 1);
  }
}

static void rando_apply_damage_trap_shove(void) {
  if (g_rando_trap_effect != kRandoTrapEffect_Damage ||
      g_rando_trap_shove_timer == 0)
    return;
  force_move_any_direction = g_rando_trap_shove_dir;
  link_direction = g_rando_trap_shove_dir;
  link_direction_last = g_rando_trap_shove_dir;
  link_speed_setting = 12;
  g_rando_trap_owns_forced_move = 1;
  g_rando_trap_shove_timer--;
}

static void rando_apply_freeze_trap_pulse(void) {
  if (g_rando_trap_effect != kRandoTrapEffect_Freeze) return;
  if ((g_rando_trap_stun_timer & 7) == 0 || countdown_for_blink < 6)
    countdown_for_blink = 10;
}

void Rando_TickTrapEffects(void) {
  rando_clear_bad_trap_wall_spark_residue();

  if (g_rando_trap_stun_timer == 0) return;

  // Let the player dismiss the trap dialogue; the actual freeze begins once
  // control returns to normal overworld/dungeon gameplay. The bad reveal cue is
  // delayed too, otherwise the VWF letter blip can overwrite it in the same
  // frame.
  if (main_module_index == 14 && dialogue_message_index == kRandoTrapDialogueId)
    return;

  if (!rando_trap_stun_can_tick()) return;
  rando_tick_trap_reveal_feedback();
  rando_neutralize_trap_motion();
  rando_apply_damage_trap_shove();
  rando_apply_freeze_trap_pulse();
  if (--g_rando_trap_stun_timer == 0) {
    g_rando_trap_effect = kRandoTrapEffect_None;
    g_rando_trap_shove_timer = 0;
    g_rando_trap_shove_dir = 0;
  }
}

static void rando_trigger_trap(uint16 item_id) {
  Sprite_ShowMessageUnconditional(kRandoTrapDialogueId);
  g_rando_trap_bad_sfx_timer = 8;

  if (item_id == ITEM_TrapDamage) {
    const uint8 damage = 8;  // one heart, clamped non-lethal
    if (link_health_current != 0) {
      link_health_current = (link_health_current > damage)
          ? (uint8)(link_health_current - damage)
          : 1;
    }
    link_hearts_filler = 0;
    countdown_for_blink = 64;
    g_rando_trap_effect = kRandoTrapEffect_Damage;
    g_rando_trap_stun_timer = 40;
    g_rando_trap_shove_timer = 12;
    g_rando_trap_shove_dir = rando_trap_recoil_dir();
  } else {
    countdown_for_blink = 32;
    g_rando_trap_effect = kRandoTrapEffect_Freeze;
    g_rando_trap_stun_timer = 96;
    g_rando_trap_shove_timer = 0;
    g_rando_trap_shove_dir = 0;
  }
}

uint8 Rando_DispatchVanillaGrant(uint16 location_id,
                                 uint16 vanilla_registry_id,
                                 uint8 vanilla_lttp_code) {
  uint16 placed = Rando_OnLocationCheck(location_id, vanilla_registry_id);
  g_last_dispatched_item_id = placed;
  g_last_dispatched_location_id = location_id;
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

  if (rando_is_trap_item(placed)) {
    rando_trigger_trap(placed);
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

  // A KNOWN progressive item already at its max tier yields 0xFF here (no
  // higher tier to grant). Skip the grant instead of falling through to the
  // chest's vanilla item: otherwise a maxed ProgressiveBow collected at
  // Desert Palace - Big Chest would grant that chest's vanilla Power Glove (an
  // unrelated duplicate). The location is already marked checked
  // (Rando_OnLocationCheck above), so skipping leaves the player at max tier
  // with no spurious item. Mainly reachable via the item-give debug cheat —
  // you can't normally collect a progressive beyond its pool count — but
  // granting the wrong item is never correct.
  if (rando_is_progressive_item(placed)) return kRandoLttpSkip;

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
// inactive flute=2, active flute=3). When the seed's instant_flute setting is
// enabled, any flute pickup promotes directly to active, skipping the vanilla
// weathervane activation trip. So acquiring the shovel can never drop the slot
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
    if (lttp_code == 0x4a || rando_instant_flute_active()) {
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
// HUD-only cue with a visible icon ancilla. Trap items get a deterministic
// good-item decoy selected from the active seed/location/trap type. Other
// direct-grant item ids are looked up in kDirectGrantIcons[item_id] (codegen'd
// from assets/rando/direct_grant_icons.yaml). Magic upgrades are resolved
// through the current progressive tier first, so either raw HalfMagic or
// QuarterMagic shows 1/2 on the first upgrade and 1/4 on the second. When an
// icon resolves, AncillaAdd_RandoIconReceipt DMAs that bundle and pops the icon
// above Link's head. Unmapped entries fall back to Phase A audio + HUD behavior
// — never crash, never spawn a blank ancilla.
//
// Deliberately NOT emitted from within `Rando_DispatchVanillaGrant` — the
// caller knows whether its own code path already provides visual context
// (e.g., the §6.6 boss-kill spawns a FallingPrize regardless of sentinel,
// so the player sees that sprite). Pushing the confirmation to the call
// site lets each integration choose whether to add the cue.
// ---------------------------------------------------------------------------
void Rando_ShowDirectGrantConfirmation(uint8 item_id) {
  // every caller passes `(uint8)Rando_LastDispatched
  // ItemId()`; the cast loses precision if the sentinel value 0xFFFF
  // ever reaches us. The skip-sentinel path only runs AFTER a successful
  // Rando_DispatchVanillaGrant, which populates g_last_dispatched_item_id
  // with a valid item id (< ITEM__COUNT), so the
  // truncation is unreachable in normal flow. The icon-entry helper defends the
  // array access regardless; this assert just makes the invariant explicit so a
  // future change that calls this WITHOUT a prior dispatch fires loudly.
  assert(item_id != 0xFFu /* sentinel byte from 0xFFFF truncation */ ||
         Rando_LastDispatchedItemId() != 0xFFFFu);
  // Traps deliberately start with the normal direct-grant chime; the trap-owned
  // delayed bad cue fires a few frames later so the pickup reads as a fakeout.
  sound_effect_2 = (uint8)(Link_CalculateSfxPan() | 0x0f);
  Hud_RefreshIcon();

  // Slice 9 — look up the visual icon. Traps use the same deterministic decoy
  // resolver as field-item sprites so the visible fake item and pickup popup
  // agree. Other direct-grant items use kDirectGrantIcons; gfx ids with the
  // 0x80 bit (TriforcePiece / magic decanters / Rupoor custom art,
  // add-rando-field-item-custom-art) load the kRandoCustomGfx_* tile + palette.
  DirectGrantIconEntry trap_decoy;
  if (rando_trap_decoy_icon(item_id, g_last_dispatched_location_id, &trap_decoy)) {
    AncillaAdd_RandoIconReceipt(trap_decoy.gfx, trap_decoy.big, trap_decoy.oam_flags);
    return;
  }
  uint16 icon_item = rando_direct_grant_icon_item_post_grant(item_id);
  const DirectGrantIconEntry *e = rando_direct_grant_icon_entry(icon_item);
  if (e != NULL) {
    AncillaAdd_RandoIconReceipt(e->gfx, e->big, e->oam_flags);
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

  DirectGrantIconEntry trap_decoy;
  if (rando_trap_decoy_icon(placed, location_id, &trap_decoy)) {
    *out_gfx = trap_decoy.gfx;
    *out_big = trap_decoy.big;
    *out_oam_flags = trap_decoy.oam_flags;
    return true;
  }

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

  // Tier 2 — items with a Slice-9 icon but no receive gfx: direct-grant items
  // (small keys, ...) and the custom-art items (TriforcePiece / HalfMagic /
  // QuarterMagic / Rupoor — gfx 0x80 bit, add-rando-field-item-custom-art;
  // Rando_EnsureRecvItemSlotGfx loads their tile + SP3-upper palette). Magic
  // upgrades are resolved to the NEXT progressive tier before indexing this
  // table, so a raw QuarterMagic placement draws as 1/2 until half magic is
  // owned. gfx==0 entries (none currently mapped) fall back to the vanilla
  // sprite.
  uint16 icon_item = rando_direct_grant_icon_item_pre_grant(placed);
  const DirectGrantIconEntry *e = rando_direct_grant_icon_entry(icon_item);
  if (e != NULL) {
    *out_gfx = e->gfx;
    *out_big = e->big;
    *out_oam_flags = e->oam_flags;
    return true;
  }
  return false;
}

// ---------------------------------------------------------------------------
// §6.6 boss-kill dispatch helpers. Each boss kill grants TWO rando locations
// (BossHeart + Prize). Boss-heart drops are shuffled locations; the dispatch
// treats the identity BossHeartContainer placement as vanilla behavior.
//
// Dungeon ID layout (cur_palace_index_x2 >> 1) — game-side convention.
// Derived from the asset room palace ids and kDungeonCrystalPendantBit /
// kBossFinishedFallingItem (zelda_rtl.c, dungeon.c). NOTE: TH lives at 10,
// not 3 — this is not the ALTTPR id ordering.
//   0 HCE  (no boss; Sanctuary chest is the heart container slot)
//   1 (unused sub-area, no prize)
//   2 EP   3 DP
//   4 HCT  (Agahnim; not a heart-drop boss — handled separately)
//   5 SP   6 PoD  7 MM   8 SW   9 IP  10 TH  11 TT  12 TR
//  13 GT   (Agahnim 2; same as HCT path)
// ---------------------------------------------------------------------------
uint16 Rando_GetBossHeartLocation(uint8 dungeon_id) {
  return Rando_BossHeartLocationForGameDungeon(dungeon_id);
}

uint16 Rando_GetBossPrizeLocation(uint8 dungeon_id) {
  return Rando_BossPrizeLocationForGameDungeon(dungeon_id);
}

// ---------------------------------------------------------------------------
// Per-seed shuffle-assignment globals consumed by Logic_ComputeReachability.
// ---------------------------------------------------------------------------
static uint8 g_dungeon_prize_assignment_store[kRandoDungeonCount];
static uint8 g_medallion_assignment_store[kRandoMedallionEntranceCount];
static const uint8 *g_dungeon_prize_assignment = NULL;
static const uint8 *g_medallion_assignment = NULL;
// Boss-shuffle LOGIC assignment (OP_CAN_KILL_BOSS). Independent of the
// shuffle_boss.c sprite-substitution activation: this drives reachability only.
static uint8 g_boss_logic_assignment_store[16];
static const uint8 *g_boss_logic_assignment = NULL;

void Rando_SetDungeonPrizeAssignment(const uint8 *assignment) {
  if (assignment == NULL) {
    g_dungeon_prize_assignment = NULL;
    return;
  }
  memcpy(g_dungeon_prize_assignment_store, assignment,
         sizeof(g_dungeon_prize_assignment_store));
  g_dungeon_prize_assignment = g_dungeon_prize_assignment_store;
}
void Rando_SetMedallionAssignment(const uint8 *assignment) {
  if (assignment == NULL) {
    g_medallion_assignment = NULL;
    return;
  }
  memcpy(g_medallion_assignment_store, assignment,
         sizeof(g_medallion_assignment_store));
  g_medallion_assignment = g_medallion_assignment_store;
}
void Rando_SetBossAssignment(const uint8 *assignment) {
  if (assignment == NULL) {
    g_boss_logic_assignment = NULL;
    return;
  }
  memcpy(g_boss_logic_assignment_store, assignment,
         sizeof(g_boss_logic_assignment_store));
  g_boss_logic_assignment = g_boss_logic_assignment_store;
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
// NUL; the v1 IDENTITY string — the slot stores the v1 raw blob, design D1 of
// add-rando-share-string-v2). Populated at Rando_ActivateSidecarSlot from the
// slot header's raw binary; consumed by Rando_RevealActiveSlotSpoiler to
// resolve the on-disk spoiler path. Cleared on Rando_DeactivateSlot.
static char g_rando_active_share_string[kShareStringBase32MaxLen] = {0};

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
// installed. INVARIANT: Entrance_RuntimeTeardown() MUST run before any
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
// exit_room / force_cached): this function is reached by mirror /
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

// Everything Entrance_RuntimeInstall installs for one slot, computed PURELY
// (no global writes): the per-mode permutations, the decoupled exit nets, and
// the built door overlay. Shared by the installer below and
// Rando_EntranceLayoutDigest24 (FIX #4) so the activation-time digest can
// never drift from what actually installs — any algorithm/pool change that
// alters an installed table alters the digest.
typedef struct EntranceRuntimeLayout {
  bool cross, cave, dun;                         // entry-shuffle modes
  bool decoupled, decoupled_dun, decoupled_cross;// one-way exit modes
  int cave_n, dun_n, cross_n, dec_n, dd_n, cd_n;
  uint8 cave_assign[kEntranceMaxInteriors];
  uint8 dun_assign[kEntranceMaxInteriors];
  uint8 cross_assign[kEntranceMaxInteriors];
  uint8 dec_assign[kEntranceMaxInteriors];
  uint8 dd_assign[kEntranceMaxInteriors];
  uint8 cd_assign[kEntranceMaxInteriors];
  uint32 overlay_len;
  uint8 overlay[kEntranceOverlayMax];
} EntranceRuntimeLayout;

// Scratch shared by Entrance_ComputeLayout's two callers (install + digest).
// File-static rather than stack — the overlay member is kEntranceOverlayMax
// bytes. Both callers run sequentially on the main thread.
static EntranceRuntimeLayout s_entrance_layout_scratch;

// The TRUE pristine vanilla entrance-id table, regardless of which subsystem
// currently owns g_asset_ptrs[126]. Two subsystems repoint 126 and
// each saves the pointer it displaced: the Inverted static override
// (inverted_entrances.c — active for the whole life of an Inverted slot) and
// the entrance-shuffle overlay (g_entrance_overlay_orig). Resolution prefers
// the deepest saved original: Inverted's saved pointer is captured after
// Entrance_RuntimeTeardown (activation order), so it is always the raw vanilla
// table; the overlay's saved pointer is likewise pristine because activation
// tears the Inverted override down before Entrance_RuntimeInstall captures.
// Reading the live table here instead used to bake an INVERTED-id digest into
// sidecars generated while an Inverted slot was active (permanent false
// refusal after restart) and false-refuse a clean entrance slot activated over
// a still-installed Inverted slot.
static const uint8 *Entrance_PristineVanillaIds(void) {
  const uint8 *inv_orig = InvertedEntrances_SavedEntranceIdOrig();
  if (inv_orig != NULL) return inv_orig;
  if (g_entrance_overlay_orig != NULL) return g_entrance_overlay_orig;
  return (const uint8 *)kOverworld_Entrance_Id;
}

// Compute the full entrance layout that `h`'s (entrance_axes, world_state,
// share-string seed, entrance_attempt) regenerate. Returns false when no
// entrance mode is active or the vanilla door table is unavailable. Reads the
// PRISTINE vanilla door table (Entrance_PristineVanillaIds) even while a
// slot's overlay — or an Inverted slot's static override — currently owns
// asset 126, so the generation-time digest, a slot-switch digest, and the
// installed overlay all see the same base.
static bool Entrance_ComputeLayout(const RandoSlotHeader *h, EntranceRuntimeLayout *out) {
  memset(out, 0, sizeof(*out));
  RandoSettings es;
  memset(&es, 0, sizeof(es));
  es.shuffle_cave_entrances = (h->entrance_axes & kEntranceAxis_ShuffleCaves) ? 1 : 0;
  es.shuffle_dungeon_entrances = (h->entrance_axes & kEntranceAxis_ShuffleDungeons) ? 1 : 0;
  es.shuffle_ganons_tower_entrance = (h->entrance_axes & kEntranceAxis_ShuffleGanonsTower) ? 1 : 0;
  es.cross_category = (h->entrance_axes & kEntranceAxis_CrossCategory) ? 1 : 0;
  es.decoupled = (h->entrance_axes & kEntranceAxis_Decoupled) ? 1 : 0;
  es.world_state = h->settings_ext_present ? h->world_state : (uint8)kWorldState_Open;
  out->cross = Entrance_IsCrossActive(&es);    // supersedes the separate paths
  out->cave = !out->cross && Entrance_IsActive(&es);// Inverted/Retro guard (defense in depth)
  out->dun = !out->cross && Entrance_IsDungeonActive(&es);
  // Decoupled (D.4) composes on top of the entry shuffle: cave decoupled needs the
  // cave shuffle, dungeon decoupled needs the dungeon shuffle.
  out->decoupled = Entrance_IsDecoupledActive(&es);
  out->decoupled_dun = Entrance_IsDungeonDecoupledActive(&es);
  out->decoupled_cross = Entrance_IsCrossDecoupledActive(&es);  // one-way over the mixed pool
  if (!out->cross && !out->cave && !out->dun &&
      !out->decoupled && !out->decoupled_dun && !out->decoupled_cross)
    return false;
  const uint8 *ids = Entrance_PristineVanillaIds();
  uint32 len = kOverworld_Entrance_Id_SIZE;
  if (ids == NULL || len == 0 || len > kEntranceOverlayMax) return false;
  out->overlay_len = len;
  // seed_u64 lives at raw share_string bytes [21..28] LE (per rando_share layout).
  uint64 seed = SlotSeedFromShareString(h->share_string);
  if (out->cross) {
    // Crossed: one combined pool; the unified overlay maps every door (cave or
    // dungeon) to its target's id.
    out->cross_n = Entrance_ComputeCrossPermutation(&es, seed, h->entrance_attempt, out->cross_assign);
    Entrance_BuildCrossOverlay(out->cross_assign, out->cross_n, ids, len, out->overlay);
  } else {
    if (out->cave)
      out->cave_n = Entrance_ComputePermutation(&es, seed, h->entrance_attempt, out->cave_assign);
    if (out->dun)
      out->dun_n = Entrance_ComputeDungeonPermutation(&es, seed, h->entrance_attempt, out->dun_assign);
    // Door overlay: cave pass copies vanilla + remaps cave slots (NULL = copy
    // only), then the dungeon pass remaps the disjoint dungeon slots in place.
    Entrance_BuildDoorOverlay(out->cave ? out->cave_assign : NULL, out->cave_n, ids, len, out->overlay);
    if (out->dun) Entrance_RemapDungeonDoors(out->dun_assign, out->dun_n, out->overlay, len);
  }
  // One-way exit permutations (regenerated deterministically; distinct salts).
  if (out->decoupled_cross) {
    int n = Entrance_ComputeCrossDecoupledExit(&es, seed, h->entrance_attempt, out->cd_assign);
    out->cd_n = (n > kEntranceMaxInteriors) ? kEntranceMaxInteriors : n;
  }
  if (out->decoupled) {
    int n = Entrance_ComputeDecoupledExit(&es, seed, h->entrance_attempt, out->dec_assign);
    out->dec_n = (n > kEntranceMaxInteriors) ? kEntranceMaxInteriors : n;
  }
  if (out->decoupled_dun) {
    int n = Entrance_ComputeDungeonDecoupledExit(&es, seed, h->entrance_attempt, out->dd_assign);
    out->dd_n = (n > kEntranceDungeonCount) ? kEntranceDungeonCount : n;
  }
  return true;
}

// FIX #4 — see rando_save.h. 24-bit FNV-1a fold (same recipe as
// DoorShuffle_LayoutDigest) over EVERYTHING Entrance_ComputeLayout produces:
// mode flags, every active permutation/net (count + bytes), and the built
// door overlay. Called at generation (store) and activation (compare).
uint32 Rando_EntranceLayoutDigest24(const RandoSlotHeader *hdr) {
  EntranceRuntimeLayout *lay = &s_entrance_layout_scratch;
  if (hdr == NULL || !Entrance_ComputeLayout(hdr, lay)) return 0;
  uint64 h = 0xcbf29ce484222325ull;
#define DIG8(b) (h = (h ^ (uint8)(b)) * 0x100000001b3ull)
  DIG8((lay->cross ? 1 : 0) | (lay->cave ? 2 : 0) | (lay->dun ? 4 : 0) |
       (lay->decoupled ? 8 : 0) | (lay->decoupled_dun ? 16 : 0) |
       (lay->decoupled_cross ? 32 : 0));
  DIG8(lay->cross_n);
  for (int i = 0; i < lay->cross_n; i++) DIG8(lay->cross_assign[i]);
  DIG8(lay->cave_n);
  for (int i = 0; i < lay->cave_n; i++) DIG8(lay->cave_assign[i]);
  DIG8(lay->dun_n);
  for (int i = 0; i < lay->dun_n; i++) DIG8(lay->dun_assign[i]);
  DIG8(lay->dec_n);
  for (int i = 0; i < lay->dec_n; i++) DIG8(lay->dec_assign[i]);
  DIG8(lay->dd_n);
  for (int i = 0; i < lay->dd_n; i++) DIG8(lay->dd_assign[i]);
  DIG8(lay->cd_n);
  for (int i = 0; i < lay->cd_n; i++) DIG8(lay->cd_assign[i]);
  DIG8(lay->overlay_len & 0xFF);
  DIG8((lay->overlay_len >> 8) & 0xFF);
  for (uint32 i = 0; i < lay->overlay_len; i++) DIG8(lay->overlay[i]);
#undef DIG8
  uint32 d = (uint32)(h ^ (h >> 32)) & 0xFFFFFF;
  return d != 0 ? d : 1;  // 0 is reserved for "absent" (legacy/no-shuffle headers)
}

// LOGIC-side (tracker/placer reachability) override installation for one
// computed entrance layout: the cross/cave/dungeon region+edge overrides plus
// the decoupled one-way exit edges. Extracted from Entrance_RuntimeInstall so
// Rando_ReinstallActiveSlotLogicOverlays can replay EXACTLY these
// sub-steps after a mid-session generation cleared the override stores —
// without touching the gameplay-side installs (asset-126 door overlay,
// decoupled runtime nets), which generation never disturbs. The Apply* calls
// self-reset their stores (Rando_Begin*Overrides), so no explicit Clear is
// needed first; ApplyDecoupledExitEdges layers onto the edge set begun by the
// dungeon/cross pass, or Begins it itself for cave-only decoupled.
static void Entrance_ApplyLogicOverrides(const EntranceRuntimeLayout *lay) {
  if (lay->cross) {
    // ApplyCrossOverrides installs all 4 cross-class override cases.
    Entrance_ApplyCrossOverrides(lay->cross_assign, lay->cross_n);
  } else {
    if (lay->cave) Entrance_ApplyRegionOverrides(lay->cave_assign, lay->cave_n);
    if (lay->dun) Entrance_ApplyEdgeOverrides(lay->dun_assign, lay->dun_n);
  }
  // dec_n > 0 only when the cave-decoupled axis computed an exit net (see
  // Entrance_ComputeLayout) — the same condition the install site used.
  if (lay->dec_n > 0)
    Entrance_ApplyDecoupledExitEdges(lay->dec_assign, lay->dec_n);  // tracker reachability
}

static void Entrance_RuntimeInstall(const RandoSlotHeader *h) {
  Entrance_RuntimeTeardown();
  EntranceRuntimeLayout *lay = &s_entrance_layout_scratch;
  if (!Entrance_ComputeLayout(h, lay)) return;
  // X.1 backward-load, LEGACY slots only (entrance_digest24 == 0 — pre-digest
  // sidecars): the entrance permutation π is REGENERATED from (seed, axes,
  // attempt) against this build's interior/dungeon pool, so a slot written by a
  // different generator_version may regenerate a DIFFERENT π → doors that don't
  // match the baked placement. Warn loudly; the placement itself still loads.
  // Slots WITH a digest don't need the warning: Rando_ActivateSidecarSlot
  // already hard-failed on any actual layout drift (FIX #4), so reaching here
  // means the regenerated layout is certified identical.
  if (h->entrance_digest24 == 0 &&
      Rando_DetectVersionDrift(h, (uint16)kGeneratorVersion)) {
    fprintf(stderr,
        "Rando WARNING: this entrance-shuffle slot was generated by version %u "
        "but this build is version %u. The door layout is regenerated from the "
        "pool and may not match — regenerate the seed for correct entrances.\n",
        (unsigned)h->generator_version, (unsigned)kGeneratorVersion);
  }
  // Logic-side overrides so the in-game location tracker reflects the shuffled
  // reachability (gameplay itself reads the baked placement, not reachability).
  // Shared with the replay helper — see Entrance_ApplyLogicOverrides.
  Entrance_ApplyLogicOverrides(lay);
  memcpy(g_entrance_overlay, lay->overlay, lay->overlay_len);
  // Decoupled (D.4): install the one-way exit permutation(s) + the exit edges
  // for the tracker. Reset all first so a re-install starts clean; the cave
  // path also arms the runtime arrival replay.
  if (lay->decoupled || lay->decoupled_dun || lay->decoupled_cross) {
    Decoupled_Reset();
    Dungeon_Decoupled_Reset();
    Cross_Decoupled_Reset();
    if (lay->cd_n > 0) {
      // Cross + decoupled: one-way exits over the mixed pool. NO logic edges
      // (coupled-equivalent reachability; the ENTRY logic is ApplyCrossOverrides
      // above). net permutes the combined endpoint space; the runtime resolves a
      // cave-vs-dungeon target per source door at the entry hook.
      g_cross_decoupled_n = lay->cd_n;
      memcpy(g_cross_net, lay->cd_assign, (size_t)g_cross_decoupled_n);
      g_cross_decoupled_active = 1;
    }
    if (lay->dec_n > 0) {
      // (The matching logic-side exit edges installed above via
      // Entrance_ApplyLogicOverrides; only the runtime net lives here.)
      g_decoupled_n = lay->dec_n;
      memcpy(g_decoupled_net, lay->dec_assign, (size_t)g_decoupled_n);
      g_decoupled_active = 1;
    }
    if (lay->dd_n > 0) {
      // No logic edges (coupled-equivalent reachability is conservative + correct);
      // net' is the runtime exit-room redirect only.
      g_dungeon_decoupled_n = lay->dd_n;
      memcpy(g_dungeon_decoupled_net, lay->dd_assign, (size_t)g_dungeon_decoupled_n);
      g_dungeon_decoupled_active = 1;
    }
  }
  // Capture via the pristine resolver, not the live g_asset_ptrs[126]: equal
  // to the live pointer in every sane state (the activation-order
  // teardown below guarantees the Inverted override is off 126 before this
  // runs, and the leading Entrance_RuntimeTeardown NULLed our own saved
  // pointer), but if a future path ever reaches here with the Inverted shadow
  // still installed, the overlay's "vanilla" lookups (coupled-exit /
  // force-cached / capture hooks) and the teardown restore stay pristine
  // instead of inheriting the contaminated shadow — install and digest can
  // never diverge.
  g_entrance_overlay_orig = Entrance_PristineVanillaIds();
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
// distinguishes "settings valid because a GENUINE slot is active" (false)
// from "valid because a snapshot COLD-REPLAY restored them" (true). The cold-
// replay gate must protect a genuine active slot, yet still let a NEW cold replay
// supersede a PRIOR one — else a 2nd Ctrl+F1 of a different seed (slot still not
// loaded) would early-return and keep the 1st seed's world_state/Inverted/shuffles.
static bool g_rando_settings_from_cold_replay = false;
// Active-slot assignment replay sources. Rando_Set*Assignment copies these into
// the VM-owned assignment stores; keeping the active seed's bytes here lets
// Rando_ReinstallActiveSlotLogicOverlays restore them after an out-of-band
// generation clobbers the VM stores.
static uint8 g_rando_active_prize_assignment[kRandoDungeonCount];
static uint8 g_rando_active_medallion_assignment[kRandoMedallionEntranceCount];
// Boss-shuffle assignment for the active slot — drives BOTH the render redirect
// (BossShuffle_Generate installs g_boss_assignment in shuffle_boss.c) AND the
// in-game logic VM (Rando_SetBossAssignment, so OP_CAN_KILL_BOSS reachability /
// the tracker agree with the bosses actually spawned). Regenerated from
// (settings, base seed) at slot load, same as prize/medallion.
static uint8 g_rando_active_boss_assignment[16];
// the EXACT settings the activation hint block synthesized for the
// ACTIVE slot: the full recovered blob when valid, else the header-ext /
// default fallbacks (v1/no-blob slots still get regenerated hints; their
// Rando_GetActiveSettings() is NULL). Rando_RegenerateActiveSlotHints() replays
// Rando_GenerateHints over these + the installed placement, so out-of-band
// hint-table clobbers (the native window's spoiler export regenerates hints
// from its generate-time snapshot) can restore the active seed's in-game hints
// exactly — activation and restore share one code path and cannot drift.
static RandoSettings g_rando_active_hint_settings;
static bool g_rando_active_hint_settings_valid = false;
// replay inputs for Rando_ReinstallActiveSlotLogicOverlays(): the
// ACTIVE slot's header (the same fields Entrance_RuntimeInstall and the door
// regen consumed at activation) plus whether activation installed a door logic
// layout. Captured once the slot's installs begin; invalidated by
// Rando_DeactivateSlot — including every activation refusal path, which routes
// through it.
static RandoSlotHeader g_rando_active_header;
static bool g_rando_active_header_valid = false;
static bool g_rando_active_door_logic = false;
// The ACTIVE slot's regenerated door layout. Persistent storage is required —
// Rando_SetDoorLogicLayout stores the POINTER — and file scope (rather than the
// former function-local static in Rando_ActivateSidecarSlot) lets the replay
// helper regenerate into the same storage. Generation uses its own
// static (g_door_gen_layout in rando_generate.c), so a mid-session generation
// never clobbers these bytes — only the installed pointer.
static DoorShuffleLayout s_active_door_layout;

static const uint16 kRandoTrapExtraGoodItemDecoys[] = {
  ITEM_Map_HyruleCastleEscape,
  ITEM_GenericKey,
  ITEM_BluePotion,
  ITEM_RedPotion,
  ITEM_BeeContents,
  ITEM_HeartRefill,
};

static uint32 rando_trap_good_item_decoy_count(void) {
  return (uint32)(ITEM_Compass_GanonsTower + 1) +
         (uint32)(ITEM_Prize_Crystal7 - ITEM_Prize_GreenPendant + 1) +
         (uint32)countof(kRandoTrapExtraGoodItemDecoys);
}

static uint16 rando_trap_good_item_decoy_at(uint32 idx) {
  if (idx <= ITEM_Compass_GanonsTower)
    return (uint16)idx;
  idx -= (uint32)(ITEM_Compass_GanonsTower + 1);
  if (idx <= (uint32)(ITEM_Prize_Crystal7 - ITEM_Prize_GreenPendant))
    return (uint16)(ITEM_Prize_GreenPendant + idx);
  idx -= (uint32)(ITEM_Prize_Crystal7 - ITEM_Prize_GreenPendant + 1);
  if (idx < countof(kRandoTrapExtraGoodItemDecoys))
    return kRandoTrapExtraGoodItemDecoys[idx];
  return 0xFFFFu;
}

static bool rando_icon_from_lttp_code(uint8 code, DirectGrantIconEntry *out) {
  if (out == NULL || code >= 76 || kReceiveItemGfx[code] == 0xff)
    return false;
  uint8 a = kWishPond2_OamFlags[code];
  if (a & 0x80)
    a = 5;
  out->gfx = kReceiveItemGfx[code];
  out->big = kReceiveItem_Tab1[code];
  out->oam_flags = (uint8)(a * 2 | 0x30);
  return true;
}

static uint8 rando_trap_decoy_lttp_for_item(uint16 item_id) {
  switch (item_id) {
    case ITEM_ProgressiveSword:  return 0x00;
    case ITEM_ProgressiveShield: return 0x04;
    case ITEM_ProgressiveArmor:  return 0x22;
    case ITEM_ProgressiveGlove:  return 0x1b;
    case ITEM_ProgressiveBow:    return 0x0b;
    case ITEM_BottleEmpty:           return 0x16;
    case ITEM_BottleWithFairy:       return 0x2c;
    case ITEM_BottleWithBee:         return 0x2b;
    case ITEM_BottleWithGoodBee:     return 0x3c;
    case ITEM_BottleWithRedPotion:   return 0x2d;
    case ITEM_BottleWithGreenPotion: return 0x3d;
    case ITEM_BottleWithBluePotion:  return 0x48;
    case ITEM_SilverArrowUpgrade: return 0x3b;
    default:
      return Rando_VanillaItemForRegistryId(item_id);
  }
}

static bool rando_good_item_decoy_icon(uint16 item_id, DirectGrantIconEntry *out) {
  const DirectGrantIconEntry *direct = rando_direct_grant_icon_entry(item_id);
  if (direct != NULL) {
    *out = *direct;
    return true;
  }
  return rando_icon_from_lttp_code(rando_trap_decoy_lttp_for_item(item_id), out);
}

static uint32 rando_trap_decoy_mix(uint64 seed, uint16 item_id, uint16 location_id) {
  uint64 x = seed ^ ((uint64)location_id * 0x9E3779B97F4A7C15ull) ^
             ((uint64)item_id * 0xBF58476D1CE4E5B9ull);
  x ^= x >> 30;
  x *= 0xBF58476D1CE4E5B9ull;
  x ^= x >> 27;
  x *= 0x94D049BB133111EBull;
  x ^= x >> 31;
  return (uint32)(x ^ (x >> 32));
}

static uint64 rando_trap_decoy_seed(void) {
  if (g_rando_active_header_valid)
    return SlotSeedFromShareString(g_rando_active_header.share_string);
  return 0xD1CE5AFE7A9B3C4Dull;
}

static bool rando_trap_decoy_icon(uint16 item_id, uint16 location_id,
                                  DirectGrantIconEntry *out) {
  if (!rando_is_trap_item(item_id) || out == NULL)
    return false;
  uint32 count = rando_trap_good_item_decoy_count();
  uint32 start = rando_trap_decoy_mix(rando_trap_decoy_seed(), item_id, location_id) % count;
  for (uint32 i = 0; i < count; i++) {
    uint16 decoy_item = rando_trap_good_item_decoy_at((start + i) % count);
    if (rando_good_item_decoy_icon(decoy_item, out))
      return true;
  }
  return false;
}

static bool rando_instant_flute_active(void) {
  // v1/no-blob slots and self-tests have no recovered settings; treat them as
  // default settings, where instant flute activation is ON.
  return !g_rando_active_settings_valid || g_rando_active_settings.instant_flute != 0;
}

// Re-derive + install this slot's logic-side shuffle assignments
// (prize/medallion + boss/drop/enemy) from (settings, BASE seed, prize_attempt)
// into the active-slot stores. Factored out of Rando_ActivateSidecarSlot so the
// snapshot cold-replay restore (Rando_SnapshotColdReplayRestore, below) re-derives
// IDENTICALLY and the two can't drift. Writes the g_rando_active_* assignment
// statics and repoints the VM via Rando_Set*Assignment (which now copy).
//
// shuffle_seed mirrors FIX #6 EXACTLY: the placer seeds prize/medallion from the
// ACCEPTED attempt's per-attempt seed (base ^ attempt*golden — see Place_AssumedFill),
// so re-derive with the same perturbation or the runtime falling-prize sprite
// (dungeon.c RandoFallingPrizeIndex) + OP_HAS_PRIZE reachability disagree with the
// prize/medallion BAKED into the placement table. prize_attempt is 0 for attempt-0
// + v1 slots (XOR 0 == the legacy base-seed derivation). DropShuffle ignores the
// placement-table arg (shuffle_drops.c), so g_session_placement_table being empty
// on the cold-replay path is harmless.
static void install_active_shuffles(const RandoSettings *s, uint64 base_seed,
                                    uint8 prize_attempt) {
  RandoRng shuffle_rng;
  uint64 shuffle_seed = base_seed ^ ((uint64)prize_attempt * 0x9E3779B97F4A7C15ull);
  Rng_SeedFromU64(&shuffle_rng, shuffle_seed);
  PrizeShuffle_Run(s, &shuffle_rng, g_rando_active_prize_assignment);
  MedallionShuffle_Run(s, &shuffle_rng, g_rando_active_medallion_assignment);
  Rando_SetDungeonPrizeAssignment(g_rando_active_prize_assignment);
  Rando_SetMedallionAssignment(g_rando_active_medallion_assignment);
  (void)DropShuffle_Generate(s, base_seed, &g_session_placement_table, NULL);
  (void)BossShuffle_Generate(s, base_seed, g_rando_active_boss_assignment);
  Rando_SetBossAssignment(g_rando_active_boss_assignment);
  (void)EnemyShuffle_Generate(s, base_seed);
}

// snapshot cold-replay restore. Called by RandoSnapshotTail_Load when a
// type-2 RandoSettings TLV is read, to reconstruct the slot's logic-side state
// (prize/medallion/boss/drop/enemy assignments + Inverted installs + the JP-glitch
// coupling) from the snapshot-carried (canonical settings, share string→seed,
// prize_attempt). Mirrors the corresponding arm of Rando_ActivateSidecarSlot via
// the shared install_active_shuffles helper, so the two can't drift.
//
// GATED on "no slot validly active": fires ONLY on a genuine cold replay — a
// fresh launch where the player pressed Ctrl+F1 on a rando snapshot WITHOUT first
// loading the slot, so nothing ran activation (g_rando_active_settings_valid is
// false). A within-session replay (the slot IS active) is left UNTOUCHED so the
// activation installs (the LIFO g_asset_ptrs[126] overlay stack, door redirects,
// etc.) aren't disturbed and can't double-install. (A v1/no-blob slot emits no
// type-2 TLV, so this never fires under it.) The caller restores the 4
// process-static ownership bytes unconditionally — that is separate, time-varying
// snapshot state this function does not touch.
void Rando_SnapshotColdReplayRestore(const RandoSettings *s,
                                     const uint8 *share_string_raw,
                                     uint8 prize_attempt) {
  if (s == NULL || share_string_raw == NULL) return;
  // Protect a GENUINE active slot (don't disturb its installs), but allow a new
  // cold replay to supersede a PRIOR cold replay's restore.
  if (g_rando_active_settings_valid && !g_rando_settings_from_cold_replay) return;

  g_rando_active_settings = *s;
  g_rando_active_world_state = s->world_state;
  uint64 seed = SlotSeedFromShareString(share_string_raw);
  install_active_shuffles(&g_rando_active_settings, seed, prize_attempt);
  // Inverted runtime installs (mirror Rando_ActivateSidecarSlot). Each is a
  // no-op unless world_state == Inverted; InvertedEntrances_Install self-tears-
  // down its prior overlay (leading Teardown), and on a true cold replay nothing
  // else owns g_asset_ptrs[126], so the stack stays LIFO-clean.
  InvertedEntrances_Install(g_rando_active_world_state);
  InvertedSecrets_Install(g_rando_active_world_state);
  InvertedHoleBlocks_Install(g_rando_active_world_state);
  g_rando_active_settings_valid = true;
  g_rando_settings_from_cold_replay = true;  // mark the source.
  // JP-glitch coupling (mirror activation D6): a glitch-logic seed forces the
  // JP-glitch runtime flag so the replayed frames reproduce the assumed glitches.
  if (Rando_SettingsAssumeJpGlitches(&g_rando_active_settings)) {
    g_config.features0      |= kFeatures0_RestoreJpGlitches;
    g_wanted_zelda_features |= kFeatures0_RestoreJpGlitches;
    enhanced_features0      |= kFeatures0_RestoreJpGlitches;
  }
}

void Rando_ActivateSidecarSlot(const RandoSidecarSlot *src) {
  if (src == NULL || src->header.slot_kind != kSlotKind_Randomizer) {
    Rando_DeactivateSlot();
    return;
  }
  rando_clear_trap_effect();
  // FIX #5 — refuse a slot whose canonical settings blob fails range
  // validation (Settings_CanonicalDeserialize now rejects out-of-range enum
  // bytes via Settings_Validate; undefined FLAG bits stay permissive). The
  // blob drives the door-shuffle gate, the prize/medallion regen, and the
  // tracker reachability below — a corrupt enum there would either flow into
  // `1u << world_state`-style consumers or silently skip the door drift gate.
  // Same refusal pathway as the digest-drift checks: deactivate, don't guess.
  if (src->header.settings_present) {
    RandoSettings vs;
    if (Settings_CanonicalDeserialize(src->settings_canonical, &vs) != 0) {
      fprintf(stderr,
              "Rando: slot settings blob failed range validation (corrupt sidecar?) "
              "— refusing to activate this slot\n");
      Rando_DeactivateSlot();
      return;
    }
  }
  // add-rando-door-shuffle — regenerate the door layout BEFORE installing any
  // slot state. The layout is not serialized; it regenerates from
  // (seed, settings, door_attempt @76). Drift HARD-FAILS: a regenerated
  // layout whose digest differs from the persisted @77-79 value can make the
  // certified-beatable placement unbeatable, so the slot is refused (treated
  // as no-rando) rather than silently loaded — unlike entrance shuffle's
  // non-blocking version-drift warning. Vanilla-door slots skip all of this.
  bool door_active = false;
  if (src->header.settings_present) {
    RandoSettings ds;
    if (Settings_CanonicalDeserialize(src->settings_canonical, &ds) == 0 &&
        Settings_EffectiveDoorShuffle(&ds) != kDoorShuffle_Vanilla) {
      uint64 slot_seed = SlotSeedFromShareString(src->header.share_string);
      bool ok = DoorShuffle_Generate(slot_seed, src->header.door_attempt,
                                     kDoorShuffle_MvpDungeonMask, &s_active_door_layout);
      uint32 digest = ok ? (DoorShuffle_LayoutDigest(&s_active_door_layout) & 0xFFFFFF) : 0;
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
      if (DoorRt_KindOverlaySelfCheck(&s_active_door_layout) != 0) {
        fprintf(stderr,
                "Rando: door-shuffle kind overlay rejected this layout "
                "— refusing to activate this slot on this build\n");
        Rando_DeactivateSlot();
        return;
      }
      door_active = true;
    }
  }
  // FIX #4 — entrance-layout drift gate, mirroring the door-shuffle digest
  // gate above. The entrance permutation is REGENERATED from (seed, axes,
  // attempt) at install; if a kGeneratorVersion bump changed the entrance
  // algorithm or pool, the regenerated layout silently differs from the one
  // the placement was certified against (doors that don't match the baked
  // placement can make a certified-beatable seed unbeatable). Recompute the
  // digest from the header and refuse on mismatch. entrance_digest24 == 0 =
  // legacy slot (pre-digest sidecar) or no entrance shuffle — those keep the
  // warn-only version-drift behavior inside Entrance_RuntimeInstall.
  if (src->header.entrance_digest24 != 0) {
    uint32 edigest = Rando_EntranceLayoutDigest24(&src->header);
    if (edigest != src->header.entrance_digest24) {
      fprintf(stderr,
              "Rando: entrance-shuffle layout drift (regen digest %06x != slot %06x) "
              "— refusing to activate this slot on this build\n",
              (unsigned)edigest, (unsigned)src->header.entrance_digest24);
      Rando_DeactivateSlot();
      return;
    }
  }
  uint16 n = src->placement_count;
  if (n > kRando_SessionPlacementCapacity) n = kRando_SessionPlacementCapacity;
  memcpy(g_session_placements, src->placements, (size_t)n * sizeof(RandoPlacement));
  g_session_placement_table.entries = g_session_placements;
  g_session_placement_table.count = n;
  Placement_Install(&g_session_placement_table);
  g_rando_slot_active = 1;
  // capture the replay inputs for
  // Rando_ReinstallActiveSlotLogicOverlays BEFORE the entrance/door installs
  // below, so the helper replays exactly the header those installs read. A
  // refusal below routes through Rando_DeactivateSlot, which invalidates this.
  g_rando_active_header = src->header;
  g_rando_active_header_valid = true;
  g_wanted_zelda_features1 |= kFeatures1_RandomizerActive;
  enhanced_features1 |= kFeatures1_RandomizerActive;

  // randomizer-save "Embedded placement table — upgrade safety": a slot whose
  // generator_version differs from this build still loads (the embedded
  // placement table is authoritative), but the spec's "Version drift loads with
  // warning" scenario requires a one-time informational warning. Door- and
  // entrance-shuffle slots are handled separately above — their layout drift
  // hard-fails in the digest gates, and legacy (digest-0) entrance slots warn
  // inside Entrance_RuntimeInstall — so a PLAIN slot (no door/entrance shuffle)
  // is the only path that otherwise activates silently. Warn on stderr, matching
  // the door/entrance drift-warning precedent in this function.
  if (!door_active && src->header.entrance_axes == 0 &&
      Rando_DetectVersionDrift(&src->header, (uint16)kGeneratorVersion)) {
    fprintf(stderr,
        "Rando WARNING: this randomizer slot was generated by version %u but "
        "this build is version %u. Its embedded placement loads as-is; "
        "regenerate the seed if you want this build's latest logic.\n",
        (unsigned)src->header.generator_version, (unsigned)kGeneratorVersion);
  }

  // Wire the snapshot-tail TLV emitter (per rando_save.h "UI also calls
  // Rando_SetSnapshotContext"). Without this, `RandoSnapshotTail_Save`
  // early-returns at `!g_has_ctx` and the snapshot file has no rando TLV;
  // a later Ctrl+F1 then restores `g_ram`'s rando-active bit but leaves
  // `g_active_placement == NULL` (placement table lives in the heap, not
  // g_ram), so the next chest dispatch falls back to vanilla items.
  Rando_SetSnapshotContext(src->header.generator_version,
                           src->header.settings_hash,
                           src->header.share_string);
  // also capture the canonical settings blob + prize_attempt so a COLD
  // snapshot replay (Ctrl+F1 on a fresh launch with the slot not loaded) can
  // reconstruct world_state + the prize/medallion/boss/drop/enemy assignments +
  // Inverted installs via Rando_SnapshotColdReplayRestore. NULL (a v1/no-blob
  // slot) suppresses the type-2 TLV, so cold replay degrades to placement-only.
  Rando_SetSnapshotSettingsContext(
      src->header.settings_present ? src->settings_canonical : NULL,
      src->header.prize_attempt);

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
  // tear down any PRIOR slot's Inverted override BEFORE the
  // entrance overlay installs over g_asset_ptrs[126]. Without this, switching
  // straight from an Inverted slot to an entrance-shuffle slot would let
  // Entrance_RuntimeInstall run while the Inverted shadow still owns 126, and
  // this slot's InvertedEntrances_Install below (whose leading Teardown
  // restores 126) would then yank the freshly-installed overlay back out from
  // under the shuffle. Keeps the 126 save/restore stack strictly LIFO; the
  // digest gate above is contamination-proof independently via
  // Entrance_PristineVanillaIds.
  InvertedEntrances_Teardown();
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
    Rando_SetDoorLogicLayout(&s_active_door_layout, s_active_door_layout.shuffled_mask);
    g_rando_active_door_logic = true;  // replay-input capture
    DoorRt_Reset();
    for (int i = 0; i < kDoorTbl_DoorCount; i++) {
      if (s_active_door_layout.pairing[i] != 0xFFFF)
        DoorRt_SetLink((uint16)i, s_active_door_layout.pairing[i]);
    }
    // Stage-1b kind overlay (relocated/un-keyed key-door KINDS). Cannot fail
    // here — DoorRt_KindOverlaySelfCheck validated this exact layout in the
    // gate above — but stay on the refusal pathway if it ever does.
    if (!DoorRt_InstallKindOverlay(&s_active_door_layout)) {
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
    g_rando_active_door_logic = false;  // replay-input capture
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
      // Re-derive + install prize/medallion/boss/drop/enemy assignments from the
      // recovered (settings, seed, prize_attempt). Factored into a shared helper
      // (above) so the snapshot cold-replay restore re-derives identically — see
      // the helper comment for the FIX #6 / falling-prize rationale.
      install_active_shuffles(&g_rando_active_settings, ss.seed_u64,
                              src->header.prize_attempt);
      g_rando_active_settings_valid = true;
      g_rando_settings_from_cold_replay = false;  // a GENUINE slot activation.
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
  // Hints are a pure function
  // of (settings, placement table); the generator (rando_hints.c) reads only
  // the `hints` and `goal` axes from RandoSettings. Rather than the one-way
  // settings_hash, the slot header carries those two axes additively in its
  // reserved tail (rando_save.h settings extension). We synthesize a settings
  // struct from defaults, override `hints`/`goal` from the ext, and
  // regenerate — so a slot loaded from disk (including share-string imports)
  // shows hints without re-running the full seed generator.
  if (g_rando_active_settings_valid) {
    // Most reliable source: the full canonical settings blob recovered just
    // above (the same one the reachability engine consumes). It carries the
    // real `hints` and `goal` axes, so it is immune to a stale or partially
    // written header ext byte — which would otherwise leave hints silently
    // off even though the seed was generated with hints on.
    g_rando_active_hint_settings = g_rando_active_settings;
  } else {
    // No canonical blob (older v1 slot / snapshot restore). Fall back to the
    // additive header ext byte, or default hints-on for the oldest slots so
    // existing rando slots still surface telepathic-tile hints. goal stays at
    // the Settings_SetDefaults value (Murahdahla won't fire unless it happens
    // to be a Triforce/Ganon-hunt default).
    Settings_SetDefaults(&g_rando_active_hint_settings);
    if (src->header.settings_ext_present) {
      g_rando_active_hint_settings.hints = src->header.hints_setting;
      g_rando_active_hint_settings.goal = src->header.goal;
    } else {
      g_rando_active_hint_settings.hints = kHintsMode_On;
    }
  }
  g_rando_active_hint_settings_valid = true;
  // single code path with the out-of-band restore: the synthesized
  // hint settings persist in g_rando_active_hint_settings so the bridge's
  // spoiler export can replay this exact regeneration after clobbering the
  // hint globals.
  Rando_RegenerateActiveSlotHints();
  // === Phase B hints: end ===
}

// regenerate the hint table for the CURRENTLY-ACTIVE slot,
// replaying exactly the activation-time hint block above (including the
// v1/no-blob header-ext fallbacks captured in g_rando_active_hint_settings).
// Called by activation itself and by RandoWindowBridge_WriteSpoilerFiles after
// its snapshot-hints export overwrote the hint globals. Clears the table
// (vanilla text) when no slot is active — matching the snapshot-restore
// convention.
void Rando_RegenerateActiveSlotHints(void) {
  const RandoPlacementTable *pt = Placement_GetActive();
  if (!g_rando_active_hint_settings_valid || pt == NULL || pt->entries == NULL) {
    Rando_ClearHints();
    return;
  }
  Rando_GenerateHints(&g_rando_active_hint_settings, pt, NULL);
}

// reinstall the ACTIVE slot's LOGIC-side overlays after an
// out-of-band generation cleared them. Rando_GenerateSlot unconditionally
// clears the entrance region/edge override stores (Rando_PlaceWithEntrances'
// leading clears) and the door logic layout (Rando_ClearGenerationLogicOverlays
// → Rando_SetDoorLogicLayout(NULL, 0)) — the same global stores
// Rando_ActivateSidecarSlot populated for the active slot — and never
// reinstalls, so generating a seed mid-session WITHOUT loading it reverted
// tracker/map LOGIC reachability to the vanilla graph until slot reload.
//
// This replays EXACTLY the activation install sub-steps from the captured
// active-slot header:
//   - entrance: Entrance_ComputeLayout over the header — the identical
//     (seed, entrance_axes, entrance_attempt) regeneration (and the same
//     pristine-table resolution, Entrance_PristineVanillaIds) that
//     Entrance_RuntimeInstall ran at activation — then the shared
//     Entrance_ApplyLogicOverrides sub-step;
//   - door: DoorShuffle_Generate from (seed, door_attempt) — identical inputs
//     to activation — re-installed via Rando_SetDoorLogicLayout. The digest
//     refusal re-check is deliberately SKIPPED: activation already validated
//     this exact regenerated layout against the slot's door_digest24 (drift
//     hard-fails there), and DoorShuffle_Generate is deterministic, so the
//     layout regenerated here is byte-identical to the validated one.
// Gameplay-side installs (the asset-126 entrance overlay, decoupled runtime
// nets, DoorRt redirects, and the boss/drop/enemy RENDER shuffles — generation
// calls only the pure BossShuffle_/DropShuffle_ComputeAssignment forms and
// never EnemyShuffle_*) are NOT touched by generation, so this helper leaves
// them alone — it is logic-side only. No active slot: leave the stores CLEARED
// (the correct idle state, matching what generation left) and return.
void Rando_ReinstallActiveSlotLogicOverlays(void) {
  // generation also replaces the logic-VM shuffle assignments
  // with the placer's per-run data (Place_AssumedFill installs boss plus
  // prize/medallion assignment bytes), i.e. the NEW seed's data.
  // Not tracker-only: RandoFallingPrizeIndex (dungeon.c) picks the falling
  // boss prize and the ancilla.c medallion casts gate MM/TR off these. The
  // active arrays (g_rando_active_{prize,medallion,boss}_assignment) are
  // file-statics that ONLY activation writes — generation never touches their
  // bytes — so copying them back into the assignment stores is sufficient;
  // mirror activation's install arm (condition: the settings block succeeded)
  // or its fail-closed NULL arm.
  if (g_rando_active_settings_valid) {
    Rando_SetDungeonPrizeAssignment(g_rando_active_prize_assignment);
    Rando_SetMedallionAssignment(g_rando_active_medallion_assignment);
    Rando_SetBossAssignment(g_rando_active_boss_assignment);
  } else {
    Rando_SetDungeonPrizeAssignment(NULL);
    Rando_SetMedallionAssignment(NULL);
    Rando_SetBossAssignment(NULL);
  }
  if (!g_rando_active_header_valid) {
    Entrance_ClearRegionOverrides();
    Entrance_ClearEdgeOverrides();
    Rando_SetDoorLogicLayout(NULL, 0);
    return;
  }
  EntranceRuntimeLayout *lay = &s_entrance_layout_scratch;
  if (Entrance_ComputeLayout(&g_rando_active_header, lay)) {
    Entrance_ApplyLogicOverrides(lay);
  } else {
    // Non-entrance slot: cleared stores ARE the activation-time logic state.
    Entrance_ClearRegionOverrides();
    Entrance_ClearEdgeOverrides();
  }
  if (g_rando_active_door_logic) {
    uint64 slot_seed = SlotSeedFromShareString(g_rando_active_header.share_string);
    if (DoorShuffle_Generate(slot_seed, g_rando_active_header.door_attempt,
                             kDoorShuffle_MvpDungeonMask, &s_active_door_layout)) {
      Rando_SetDoorLogicLayout(&s_active_door_layout, s_active_door_layout.shuffled_mask);
    } else {
      // Unreachable for a validly-activated slot (the same deterministic
      // inputs generated at activation); fail closed rather than install a
      // half-written layout.
      fprintf(stderr,
              "Rando: door layout reinstall regeneration failed — door logic cleared\n");
      Rando_SetDoorLogicLayout(NULL, 0);
    }
  } else {
    Rando_SetDoorLogicLayout(NULL, 0);
  }
  // Force a tracker recompute (mirrors activation): the stores round-tripped
  // through a cleared state, so don't trust any cached reachability.
  g_reachability_state_counter++;
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
  rando_clear_trap_effect();
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
  // switches once the hint generator lands (stub today; no-op). Also
  // invalidate the captured hint settings so a post-deactivation
  // Rando_RegenerateActiveSlotHints() clears rather than replaying a stale slot.
  g_rando_active_hint_settings_valid = false;
  Rando_ClearHints();

  // §62 — clear the share-string cache; reveal action returns FileNotFound
  // when no slot is active.
  g_rando_active_share_string[0] = '\0';

  // Reachability: invalidate the recovered settings and NULL the shuffle
  // assignments. Without this, switching to a vanilla/empty slot would
  // leave eval_has_prize / eval_medallion_opens reading the prior slot's stale
  // assignment table. (The VM treats NULL as "no prize/medallion reachable".)
  g_rando_active_settings_valid = false;
  g_rando_settings_from_cold_replay = false;  // reset the source flag.
  Rando_SetDungeonPrizeAssignment(NULL);
  Rando_SetMedallionAssignment(NULL);

  // invalidate the logic-overlay replay inputs so a
  // post-deactivation Rando_ReinstallActiveSlotLogicOverlays() clears the
  // stores instead of replaying a stale slot.
  g_rando_active_header_valid = false;
  g_rando_active_door_logic = false;

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
  // settings) so the event derivation can't diverge from the graph.
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

    for (int g = 0; g < 14; g++) {
      uint16 bit = Rando_DungeonBitForGameDungeon((uint8)g);
      uint16 small_key = Rando_SmallKeyItemForGameDungeon((uint8)g);
      uint16 big_key = Rando_BigKeyItemForGameDungeon((uint8)g);
      uint16 map = Rando_MapItemForGameDungeon((uint8)g);
      uint16 compass = Rando_CompassItemForGameDungeon((uint8)g);
      if (Settings_EffectiveSmallKeysMode(st) != kDungeonItemMode_Vanilla &&
          small_key != 0xFFFF)
        out->by_item_id[small_key] = link_keys_earned_per_dungeon[g];
      if (st->dungeon_big_keys_mode != kDungeonItemMode_Vanilla &&
          big_key != 0xFFFF && (link_bigkey & bit))
        out->by_item_id[big_key] = 1;
      if (st->dungeon_maps_mode != kDungeonItemMode_Vanilla &&
          map != 0xFFFF && (link_dungeon_map & bit))
        out->by_item_id[map] = 1;
      if (st->dungeon_compasses_mode != kDungeonItemMode_Vanilla &&
          compass != 0xFFFF && (link_compass & bit))
        out->by_item_id[compass] = 1;
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
  // -gated regions stay dark until the next unrelated location check.
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
  // bytes for the informational view.
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
  // pass budget_seconds=0 (no wall-clock cutoff) so the
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
  // forward_fill_fallback_count and retry_attempts are
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
  uint8 regen_medallion_assignment[kRandoMedallionEntranceCount];
  {
    const uint8 *assignment = Rando_GetMedallionAssignment();
    if (assignment != NULL) {
      memcpy(regen_medallion_assignment, assignment, sizeof(regen_medallion_assignment));
      regen.medallion_assignment = regen_medallion_assignment;
    }
  }
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
  char tmp_path[1280];
  if (snprintf(tmp_path, sizeof(tmp_path), "%s.reveal-tmp", suppressed_path) >= (int)sizeof(tmp_path)) {
    return kRandoReveal_WriteFailed;
  }

  // Cleanup epilogue is reached via `goto fail` from every error path so
  // .reveal-tmp never leaks.
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
    // leave the suppressed file untouched on stamp
    // mismatch. The fact that we never wrote to suppressed_path is the
    // invariant.
    result = kRandoReveal_StampMismatch;
    goto fail;
  }

  // Stamp matched. Write to `.partial`, fsync-equivalent close,
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
  // Always drop the .reveal-tmp scratch file and free
  // the in-memory bytes. `result` carries the outcome.
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
  // anti-cheat gate. Race-mode's design intent
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

static void Rando_DungeonIdSelfCheck(void) {
  if (Rando_GameDungeonFromRandoDungeon(kRandoDungeon_PalaceOfDarkness) != kGameDungeon_PalaceOfDarkness ||
      Rando_GameDungeonFromRandoDungeon(kRandoDungeon_SwampPalace) != kGameDungeon_SwampPalace ||
      Rando_GameDungeonFromRandoDungeon(kRandoDungeon_SkullWoods) != kGameDungeon_SkullWoods ||
      Rando_GameDungeonFromRandoDungeon(kRandoDungeon_ThievesTown) != kGameDungeon_ThievesTown ||
      Rando_GameDungeonFromRandoDungeon(kRandoDungeon_MiseryMire) != kGameDungeon_MiseryMire) {
    fprintf(stderr, "Rando_SelfCheck: rando->game dungeon mapping mismatch\n");
    exit(2);
  }
  if (Rando_RandoDungeonFromGameDungeon(kGameDungeon_SwampPalace) != kRandoDungeon_SwampPalace ||
      Rando_RandoDungeonFromGameDungeon(kGameDungeon_PalaceOfDarkness) != kRandoDungeon_PalaceOfDarkness ||
      Rando_RandoDungeonFromGameDungeon(kGameDungeon_MiseryMire) != kRandoDungeon_MiseryMire ||
      Rando_RandoDungeonFromGameDungeon(kGameDungeon_SkullWoods) != kRandoDungeon_SkullWoods ||
      Rando_RandoDungeonFromGameDungeon(kGameDungeon_ThievesTown) != kRandoDungeon_ThievesTown) {
    fprintf(stderr, "Rando_SelfCheck: game->rando dungeon mapping mismatch\n");
    exit(2);
  }
  if (Rando_KeySlotFromRawPalace(2) != kGameDungeon_HyruleCastleEscape ||
      Rando_KeySlotFromRawPalace(16) != kGameDungeon_SkullWoods ||
      Rando_KeySlotFromRawPalace(0xff) != kGameDungeon_None) {
    fprintf(stderr, "Rando_SelfCheck: raw-palace key-slot mapping mismatch\n");
    exit(2);
  }
  if (Rando_DungeonItemGameDungeon(ITEM_SmallKey_SkullWoods) != kGameDungeon_SkullWoods ||
      Rando_DungeonItemGameDungeon(ITEM_BigKey_SkullWoods) != kGameDungeon_SkullWoods ||
      Rando_DungeonItemGameDungeon(ITEM_Map_SwampPalace) != kGameDungeon_SwampPalace ||
      Rando_DungeonItemGameDungeon(ITEM_Compass_PalaceOfDarkness) != kGameDungeon_PalaceOfDarkness ||
      Rando_DungeonItemGameDungeon(ITEM_Map_HyruleCastleEscape) != kGameDungeon_HyruleCastleEscape) {
    fprintf(stderr, "Rando_SelfCheck: dungeon item -> game mapping mismatch\n");
    exit(2);
  }
  if (Rando_RandoDungeonFromDungeonItem(ITEM_SmallKey_HyruleCastleTower) != kRandoDungeon_HyruleCastleTower ||
      Rando_RandoDungeonFromDungeonItem(ITEM_BigKey_GanonsTower) != kRandoDungeon_GanonsTower ||
      Rando_RandoDungeonFromDungeonItem(ITEM_Map_HyruleCastleEscape) != kRandoDungeon_HyruleCastleEscape ||
      Rando_RandoDungeonFromDungeonItem(ITEM_Compass_MiseryMire) != kRandoDungeon_MiseryMire) {
    fprintf(stderr, "Rando_SelfCheck: dungeon item -> rando mapping mismatch\n");
    exit(2);
  }
  if (Rando_SmallKeyItemForGameDungeon(kGameDungeon_SkullWoods) != ITEM_SmallKey_SkullWoods ||
      Rando_BigKeyItemForGameDungeon(kGameDungeon_ThievesTown) != ITEM_BigKey_ThievesTown ||
      Rando_MapItemForGameDungeon(kGameDungeon_HyruleCastleEscape) != ITEM_Map_HyruleCastleEscape ||
      Rando_MapItemForGameDungeon(kGameDungeon_SwampPalace) != ITEM_Map_SwampPalace ||
      Rando_CompassItemForGameDungeon(kGameDungeon_PalaceOfDarkness) != ITEM_Compass_PalaceOfDarkness) {
    fprintf(stderr, "Rando_SelfCheck: game dungeon -> item mapping mismatch\n");
    exit(2);
  }
  if (Rando_PrizeRandoDungeonFromGameDungeon(kGameDungeon_MiseryMire) != kRandoDungeon_MiseryMire ||
      Rando_PrizeRandoDungeonFromGameDungeon(kGameDungeon_HyruleCastleTower) != kRandoDungeon_None ||
      Rando_BossPrizeLocationForGameDungeon(kGameDungeon_SkullWoods) != LOC_Skull_Woods_Prize ||
      Rando_BossHeartLocationForGameDungeon(kGameDungeon_PalaceOfDarkness) != LOC_Palace_of_Darkness_Boss) {
    fprintf(stderr, "Rando_SelfCheck: boss/prize game mapping mismatch\n");
    exit(2);
  }
  if (kRandoDungeonRuntimeRows[0].key_slot != kGameDungeon_HyruleCastleEscape ||
      kRandoDungeonRuntimeRows[0].bigkey_game_dungeon != kGameDungeon_None ||
      kRandoDungeonRuntimeRows[0].map_game_dungeon != kGameDungeon_HyruleCastleEscape ||
      kRandoDungeonRuntimeRows[0].compass_game_dungeon != kGameDungeon_None ||
      kRandoDungeonRuntimeRows[0].rando_dungeon != kRandoDungeon_HyruleCastleEscape) {
    fprintf(stderr, "Rando_SelfCheck: HCE runtime row axis mismatch\n");
    exit(2);
  }
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
  Rando_DungeonIdSelfCheck();

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

  {
    uint8 saved_slot_active = g_rando_slot_active;
    uint8 saved_flute_shovel_owned = g_rando_flute_shovel_owned;
    uint8 saved_link_item_flute = link_item_flute;
    RandoSettings saved_active_settings = g_rando_active_settings;
    bool saved_active_settings_valid = g_rando_active_settings_valid;
    // rando-exempt: self-test fabricates the rando flute/shovel grant state
    // and restores it before returning; this is not a gameplay grant site.
    g_rando_slot_active = 1;
    Settings_SetDefaults(&g_rando_active_settings);
    g_rando_active_settings_valid = true;
    g_rando_flute_shovel_owned = 0;
    link_item_flute = 0;
    Rando_GrantFluteShovel(0x14);
    if (link_item_flute != 3 ||
        g_rando_flute_shovel_owned != (kRandoFluteShovel_Flute | kRandoFluteShovel_FluteActive) ||
        Rando_FluteShovelEffectiveLevel() != 3) {
      fprintf(stderr, "Rando_SelfCheck: inactive flute pickup should grant active flute\n");
      exit(2);
    }
    Rando_GrantFluteShovel(0x13);
    if (link_item_flute != 3 ||
        g_rando_flute_shovel_owned != (kRandoFluteShovel_Shovel | kRandoFluteShovel_Flute |
                                       kRandoFluteShovel_FluteActive)) {
      fprintf(stderr, "Rando_SelfCheck: shovel pickup should not downgrade active flute\n");
      exit(2);
    }
    g_rando_active_settings.instant_flute = 0;
    g_rando_flute_shovel_owned = 0;
    link_item_flute = 0;
    Rando_GrantFluteShovel(0x14);
    if (link_item_flute != 2 ||
        g_rando_flute_shovel_owned != kRandoFluteShovel_Flute ||
        Rando_FluteShovelEffectiveLevel() != 2) {
      fprintf(stderr, "Rando_SelfCheck: instant_flute=false should leave 0x14 inactive\n");
      exit(2);
    }
    Rando_GrantFluteShovel(0x4a);
    if (link_item_flute != 3 ||
        g_rando_flute_shovel_owned != (kRandoFluteShovel_Flute | kRandoFluteShovel_FluteActive) ||
        Rando_FluteShovelEffectiveLevel() != 3) {
      fprintf(stderr, "Rando_SelfCheck: explicit active flute grant should activate\n");
      exit(2);
    }
    g_rando_slot_active = saved_slot_active;
    g_rando_active_settings = saved_active_settings;
    g_rando_active_settings_valid = saved_active_settings_valid;
    g_rando_flute_shovel_owned = saved_flute_shovel_owned;
    link_item_flute = saved_link_item_flute;
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

  // Progressive-item grant regression — a progressive item collected while
  // ALREADY at max tier must SKIP (kRandoLttpSkip), NOT fall through to the
  // chest's vanilla item. Original bug: a (customizer-pinned) ProgressiveBow at
  // Desert Palace - Big Chest (room 115, ord 0; vanilla LttP code 0x1b =
  // Power Glove) granted the Power Glove when the player already had the silver
  // bow (e.g. via the item-give cheat). See rando_is_progressive_item /
  // Rando_DispatchVanillaGrant.
  {
    uint16 bow_loc = chest_lookup(115, 0);  // Desert Palace - Big Chest
    if (bow_loc != 0xFFFFu) {
      static RandoPlacement entries[1];
      entries[0].location_id = bow_loc;
      entries[0].item_id = ITEM_ProgressiveBow;
      RandoPlacementTable t = { entries, 1 };
      Placement_Install(&t);
      uint8 saved_bow = link_item_bow;
      // Maxed (silver bow + arrows): must skip, not grant vanilla 0x1b.
      link_item_bow = 4;
      uint8 lttp = Rando_ChestDispatch(115, 0, 0x1b);
      if (lttp != kRandoLttpSkip) {
        fprintf(stderr,
          "Rando_SelfCheck: maxed ProgressiveBow chest should skip (got 0x%02x) "
          "- would grant the chest's vanilla item (Power Glove)\n",
          (unsigned)lttp);
        exit(2);
      }
      // Un-owned: must still grant the wooden-bow code (0x0b), never skip/vanilla.
      link_item_bow = 0;
      lttp = Rando_ChestDispatch(115, 0, 0x1b);
      if (lttp != 0x0b) {
        fprintf(stderr,
          "Rando_SelfCheck: un-owned ProgressiveBow chest should grant wood bow "
          "(0x0b), got 0x%02x\n", (unsigned)lttp);
        exit(2);
      }
      link_item_bow = saved_bow;
      Placement_Install(NULL);
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

  // add-rando-traps — direct dispatch, non-lethal damage clamp, trap-owned
  // freeze timer, and generated dialogue buffer.
  {
    uint8 buf[32];
    if (!Rando_RenderTrapMessage(kRandoTrapDialogueId, buf) ||
        buf[0] != 24 || buf[1] != 40 || buf[2] != 46 || buf[7] != 0x75 ||
        buf[15] != 0x7f) {
      fprintf(stderr, "Rando_SelfCheck: trap message render mismatch\n");
      exit(2);
    }
    if (Rando_RenderTrapMessage(0x00B5, buf)) {
      fprintf(stderr, "Rando_SelfCheck: trap renderer should ignore non-trap ids\n");
      exit(2);
    }

    uint8 saved_main = main_module_index;
    uint8 saved_sub = submodule_index;
    uint8 saved_subsub = subsubmodule_index;
    uint8 saved_saved = saved_module_for_menu;
    uint16 saved_dialogue = dialogue_message_index;
    uint8 saved_msg = messaging_module;
    uint8 saved_0223 = byte_7E0223;
    uint8 saved_health = link_health_current;
    uint8 saved_hearts_filler = link_hearts_filler;
    uint8 saved_blink = countdown_for_blink;
    uint8 saved_aux = link_auxiliary_state;
    uint8 saved_timer = link_incapacitated_timer;
    uint8 saved_vx = link_actual_vel_x;
    uint8 saved_vy = link_actual_vel_y;
    uint8 saved_vz = link_actual_vel_z;
    uint8 saved_vzc = link_actual_vel_z_copy;
    uint16 saved_z = link_z_coord;
    uint8 saved_speed = link_speed_setting;
    uint8 saved_sfx1 = sound_effect_1;
    uint8 saved_sfx2 = sound_effect_2;
    uint8 saved_trap_timer = g_rando_trap_stun_timer;
    uint8 saved_trap_effect = g_rando_trap_effect;
    uint8 saved_trap_bad_sfx = g_rando_trap_bad_sfx_timer;
    uint8 saved_trap_shove_timer = g_rando_trap_shove_timer;
    uint8 saved_trap_shove_dir = g_rando_trap_shove_dir;
    uint8 saved_trap_owns_force = g_rando_trap_owns_forced_move;
    uint8 saved_jh = joypad1H_last;
    uint8 saved_jl = joypad1L_last;
    uint8 saved_fh = filtered_joypad_H;
    uint8 saved_fl = filtered_joypad_L;
    uint16 saved_force = force_move_any_direction;
    uint8 saved_yvel = link_y_vel;
    uint8 saved_xvel = link_x_vel;
    uint8 saved_button_mask = button_mask_b_y;
    uint8 saved_a_bitfield = bitfield_for_a_button;
    uint8 saved_button_b = button_b_frames;
    uint8 saved_sword_delay = link_delay_timer_spin_attack;
    uint8 saved_spin_step = link_spin_attack_step_counter;
    uint8 saved_state_bits = link_state_bits;
    uint8 saved_pick_state = link_picking_throw_state;
    uint8 saved_grabbing = link_grabbing_wall;
    uint8 saved_diag = link_moving_against_diag_tile;
    uint8 saved_running = link_is_running;
    uint8 saved_dash_countdown = link_countdown_for_dash;
    uint8 saved_cant_dir = link_cant_change_direction;
    uint8 saved_30d = link_var30d;
    uint8 saved_30e = link_var30e;
    uint8 saved_dir = link_direction;
    uint8 saved_dir_last = link_direction_last;
    uint8 saved_dir_facing = link_direction_facing;
    uint8 saved_anim_timer_steps = some_animation_timer_steps;
    uint8 saved_swim_countdown = swimming_countdown;
    uint8 saved_swim_faster = link_maybe_swim_faster;
    uint8 saved_swim_hard = link_swim_hard_stroke;
    uint16 saved_swim1[2] = { swimcoll_var1[0], swimcoll_var1[1] };
    uint16 saved_swim3[2] = { swimcoll_var3[0], swimcoll_var3[1] };
    uint16 saved_swim5[2] = { swimcoll_var5[0], swimcoll_var5[1] };
    uint16 saved_swim7[2] = { swimcoll_var7[0], swimcoll_var7[1] };
    uint16 saved_swim9[2] = { swimcoll_var9[0], swimcoll_var9[1] };
    uint8 saved_slot_active = g_rando_slot_active;
    uint8 saved_hookshot = link_something_with_hookshot;
    uint8 saved_drag = bitmask_of_dragstate;
    uint8 saved_ancilla_type[5];
    uint8 saved_ancilla_item[5];
    uint8 saved_ancilla_aux[5];
    uint8 saved_ancilla_timer[5];
    uint8 saved_ancilla_step[5];
    uint8 saved_ancilla_arr3[5];
    uint8 saved_ancilla_g[5];
    uint8 saved_ancilla_l[5];
    for (int k = 0; k < 5; k++) {
      saved_ancilla_type[k] = ancilla_type[k];
      saved_ancilla_item[k] = ancilla_item_to_link[k];
      saved_ancilla_aux[k] = ancilla_aux_timer[k];
      saved_ancilla_timer[k] = ancilla_timer[k];
      saved_ancilla_step[k] = ancilla_step[k];
      saved_ancilla_arr3[k] = ancilla_arr3[k];
      saved_ancilla_g[k] = ancilla_G[k];
      saved_ancilla_l[k] = ancilla_L[k];
    }

    static RandoPlacement trap_entries[1];
    trap_entries[0].location_id = 166;
    trap_entries[0].item_id = ITEM_TrapDamage;
    RandoPlacementTable tt = { trap_entries, 1 };
    Placement_Install(&tt);
    link_is_running = 0;
    link_health_current = 4;
    link_incapacitated_timer = 0;
    link_auxiliary_state = 0;
    link_direction_facing = 0;
    rando_clear_trap_effect();
    uint8 lttp = Rando_DispatchVanillaGrant(166, ITEM_BottleEmpty, 0x16);
    if (lttp != kRandoLttpSkip || link_health_current != 1 ||
        g_rando_trap_stun_timer != 40 ||
        g_rando_trap_effect != kRandoTrapEffect_Damage ||
        g_rando_trap_bad_sfx_timer != 8 ||
        g_rando_trap_shove_timer != 12 || g_rando_trap_shove_dir != 4 ||
        link_incapacitated_timer != 0 || link_auxiliary_state != 0 ||
        dialogue_message_index != kRandoTrapDialogueId) {
      fprintf(stderr, "Rando_SelfCheck: TrapDamage dispatch/effect mismatch\n");
      exit(2);
    }
    joypad1H_last = 0x0f;
    filtered_joypad_H = 0x0f;
    Rando_TickTrapEffects();
    if (g_rando_trap_stun_timer != 40 || g_rando_trap_bad_sfx_timer != 8 ||
        joypad1H_last != 0x0f || filtered_joypad_H != 0x0f) {
      fprintf(stderr, "Rando_SelfCheck: trap timer should pause during dialogue\n");
      exit(2);
    }
    dialogue_message_index = 0;
    main_module_index = 9;
    g_rando_trap_bad_sfx_timer = 1;
    sound_effect_1 = 0;
    sound_effect_2 = 0;
    joypad1H_last = 0x0f;
    joypad1L_last = 0x80;
    filtered_joypad_H = 0x0f;
    filtered_joypad_L = 0x80;
    force_move_any_direction = 0x000f;
    link_y_vel = 5;
    link_x_vel = 6;
    link_actual_vel_y = 7;
    link_actual_vel_x = 8;
    link_actual_vel_z = 9;
    link_actual_vel_z_copy = 10;
    button_b_frames = 8;
    link_state_bits = 0xff;
    swimcoll_var3[1] = 1;
    swimcoll_var7[0] = 0x180;
    link_swim_hard_stroke = 0xc0;
    Rando_TickTrapEffects();
    if (g_rando_trap_stun_timer != 39 || g_rando_trap_shove_timer != 11 ||
        g_rando_trap_bad_sfx_timer != 0 ||
        (sound_effect_2 & 0x3f) != 0x26 || sound_effect_1 == 0 ||
        joypad1H_last != 0 ||
        joypad1L_last != 0 || filtered_joypad_H != 0 ||
        filtered_joypad_L != 0 || force_move_any_direction != 4 ||
        g_rando_trap_owns_forced_move == 0 ||
        link_direction != 4 || link_direction_last != 4 ||
        link_speed_setting != 12 ||
        link_y_vel != 0 || link_x_vel != 0 ||
        link_actual_vel_y != 0 || link_actual_vel_x != 0 ||
        link_actual_vel_z != 0 || link_actual_vel_z_copy != 0 ||
        button_b_frames != 0 || link_state_bits != 0 ||
        swimcoll_var3[1] != 0 || swimcoll_var7[0] != 0 ||
        link_swim_hard_stroke != 0 || link_incapacitated_timer != 0) {
      fprintf(stderr, "Rando_SelfCheck: trap timer tick did not neutralize motion\n");
      exit(2);
    }
    rando_clear_trap_effect();
    if (g_rando_trap_stun_timer != 0 || g_rando_trap_owns_forced_move != 0 ||
        force_move_any_direction != 0) {
      fprintf(stderr, "Rando_SelfCheck: trap clear should drop forced movement\n");
      exit(2);
    }
    trap_entries[0].item_id = ITEM_TrapFreeze;
    link_health_current = 0x28;
    link_incapacitated_timer = 0;
    link_auxiliary_state = 0;
    rando_clear_trap_effect();
    lttp = Rando_DispatchVanillaGrant(166, ITEM_BottleEmpty, 0x16);
    if (lttp != kRandoLttpSkip || link_health_current != 0x28 ||
        g_rando_trap_stun_timer != 96 ||
        g_rando_trap_effect != kRandoTrapEffect_Freeze ||
        g_rando_trap_bad_sfx_timer != 8 ||
        g_rando_trap_shove_timer != 0 || g_rando_trap_shove_dir != 0 ||
        link_incapacitated_timer != 0 || link_auxiliary_state != 0 ||
        dialogue_message_index != kRandoTrapDialogueId) {
      fprintf(stderr, "Rando_SelfCheck: TrapFreeze dispatch/effect mismatch\n");
      exit(2);
    }
    dialogue_message_index = 0;
    main_module_index = 9;
    countdown_for_blink = 0;
    Rando_TickTrapEffects();
    if (g_rando_trap_stun_timer != 95 || force_move_any_direction != 0 ||
        countdown_for_blink != 10 || g_rando_trap_effect != kRandoTrapEffect_Freeze) {
      fprintf(stderr, "Rando_SelfCheck: TrapFreeze tick/pulse mismatch\n");
      exit(2);
    }
    g_rando_slot_active = 1;
    g_rando_trap_stun_timer = 0;
    link_something_with_hookshot = 0;
    bitmask_of_dragstate = 0;
    for (int k = 0; k < 5; k++) {
      rando_selfcheck_seed_bad_trap_wall_spark_residue(k);
    }
    Rando_TickTrapEffects();
    for (int k = 0; k < 5; k++) {
      if (ancilla_type[k] != 0) {
        fprintf(stderr, "Rando_SelfCheck: stale trap gravestone residue not cleared\n");
        exit(2);
      }
    }
    const int residue_slot = 2;
    rando_selfcheck_seed_bad_trap_wall_spark_residue(residue_slot);
    link_something_with_hookshot = 1;
    bitmask_of_dragstate = 0;
    Rando_TickTrapEffects();
    if (ancilla_type[residue_slot] != 0x24) {
      fprintf(stderr, "Rando_SelfCheck: hookshot-owned gravestone was cleared\n");
      exit(2);
    }
    rando_selfcheck_seed_bad_trap_wall_spark_residue(residue_slot);
    link_something_with_hookshot = 0;
    bitmask_of_dragstate = 4;
    Rando_TickTrapEffects();
    if (ancilla_type[residue_slot] != 0x24) {
      fprintf(stderr, "Rando_SelfCheck: drag-owned gravestone was cleared\n");
      exit(2);
    }
    rando_selfcheck_seed_bad_trap_wall_spark_residue(residue_slot);
    link_something_with_hookshot = 0;
    bitmask_of_dragstate = 0;
    Rando_TickTrapEffects();
    if (ancilla_type[residue_slot] != 0) {
      fprintf(stderr, "Rando_SelfCheck: stale trap gravestone residue not cleared\n");
      exit(2);
    }
    Placement_Install(NULL);

    main_module_index = saved_main;
    submodule_index = saved_sub;
    subsubmodule_index = saved_subsub;
    saved_module_for_menu = saved_saved;
    dialogue_message_index = saved_dialogue;
    messaging_module = saved_msg;
    byte_7E0223 = saved_0223;
    link_health_current = saved_health;
    link_hearts_filler = saved_hearts_filler;
    countdown_for_blink = saved_blink;
    link_auxiliary_state = saved_aux;
    link_incapacitated_timer = saved_timer;
    link_actual_vel_x = saved_vx;
    link_actual_vel_y = saved_vy;
    link_actual_vel_z = saved_vz;
    link_actual_vel_z_copy = saved_vzc;
    link_z_coord = saved_z;
    link_speed_setting = saved_speed;
    sound_effect_1 = saved_sfx1;
    sound_effect_2 = saved_sfx2;
    g_rando_trap_stun_timer = saved_trap_timer;
    g_rando_trap_effect = saved_trap_effect;
    g_rando_trap_bad_sfx_timer = saved_trap_bad_sfx;
    g_rando_trap_shove_timer = saved_trap_shove_timer;
    g_rando_trap_shove_dir = saved_trap_shove_dir;
    g_rando_trap_owns_forced_move = saved_trap_owns_force;
    joypad1H_last = saved_jh;
    joypad1L_last = saved_jl;
    filtered_joypad_H = saved_fh;
    filtered_joypad_L = saved_fl;
    force_move_any_direction = saved_force;
    link_y_vel = saved_yvel;
    link_x_vel = saved_xvel;
    button_mask_b_y = saved_button_mask;
    bitfield_for_a_button = saved_a_bitfield;
    button_b_frames = saved_button_b;
    link_delay_timer_spin_attack = saved_sword_delay;
    link_spin_attack_step_counter = saved_spin_step;
    link_state_bits = saved_state_bits;
    link_picking_throw_state = saved_pick_state;
    link_grabbing_wall = saved_grabbing;
    link_moving_against_diag_tile = saved_diag;
    link_is_running = saved_running;
    link_countdown_for_dash = saved_dash_countdown;
    link_cant_change_direction = saved_cant_dir;
    link_var30d = saved_30d;
    link_var30e = saved_30e;
    link_direction = saved_dir;
    link_direction_last = saved_dir_last;
    link_direction_facing = saved_dir_facing;
    some_animation_timer_steps = saved_anim_timer_steps;
    swimming_countdown = saved_swim_countdown;
    link_maybe_swim_faster = saved_swim_faster;
    link_swim_hard_stroke = saved_swim_hard;
    swimcoll_var1[0] = saved_swim1[0];
    swimcoll_var1[1] = saved_swim1[1];
    swimcoll_var3[0] = saved_swim3[0];
    swimcoll_var3[1] = saved_swim3[1];
    swimcoll_var5[0] = saved_swim5[0];
    swimcoll_var5[1] = saved_swim5[1];
    swimcoll_var7[0] = saved_swim7[0];
    swimcoll_var7[1] = saved_swim7[1];
    swimcoll_var9[0] = saved_swim9[0];
    swimcoll_var9[1] = saved_swim9[1];
    g_rando_slot_active = saved_slot_active;
    link_something_with_hookshot = saved_hookshot;
    bitmask_of_dragstate = saved_drag;
    for (int k = 0; k < 5; k++) {
      ancilla_type[k] = saved_ancilla_type[k];
      ancilla_item_to_link[k] = saved_ancilla_item[k];
      ancilla_aux_timer[k] = saved_ancilla_aux[k];
      ancilla_timer[k] = saved_ancilla_timer[k];
      ancilla_step[k] = saved_ancilla_step[k];
      ancilla_arr3[k] = saved_ancilla_arr3[k];
      ancilla_G[k] = saved_ancilla_g[k];
      ancilla_L[k] = saved_ancilla_l[k];
    }
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

  // §6.2 HalfMagic/QuarterMagic direct-write tests. Magic upgrades are STRICTLY
  // PROGRESSIVE: either item advances link_magic_consumption by exactly one tier
  // (0->1->2), capped at 2, regardless of collection order, and never downgrades.
  {
    static RandoPlacement entries[1];
    entries[0].location_id = 166;
    entries[0].item_id = ITEM_HalfMagic;  // 41
    RandoPlacementTable t = { entries, 1 };
    Placement_Install(&t);
    const DirectGrantIconEntry *icon;
    // Draw-side/confirmation icons follow the same progressive tier as the
    // grant: the raw item identity is not visible to the player.
    link_magic_consumption = 0;
    icon = rando_direct_grant_icon_entry(
        rando_direct_grant_icon_item_pre_grant(ITEM_QuarterMagic));
    if (icon == NULL || icon->gfx != kRandoCustomGfx_HalfMagic) {
      fprintf(stderr, "Rando_SelfCheck: first magic upgrade should draw 1/2 icon\n");
      exit(2);
    }
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
    icon = rando_direct_grant_icon_entry(
        rando_direct_grant_icon_item_post_grant(ITEM_HalfMagic));
    if (icon == NULL || icon->gfx != kRandoCustomGfx_HalfMagic) {
      fprintf(stderr, "Rando_SelfCheck: first magic confirmation should show 1/2 icon\n");
      exit(2);
    }
    // A SECOND magic upgrade (QuarterMagic) advances half -> quarter.
    entries[0].item_id = ITEM_QuarterMagic;  // 42
    Placement_Install(&t);
    if (rando_direct_grant_icon_item_pre_grant(ITEM_HalfMagic) != ITEM_QuarterMagic ||
        rando_direct_grant_icon_item_pre_grant(ITEM_QuarterMagic) != ITEM_QuarterMagic) {
      fprintf(stderr, "Rando_SelfCheck: second magic upgrade should draw 1/4 icon\n");
      exit(2);
    }
    Rando_DispatchVanillaGrant(166, ITEM_BottleEmpty, 0x16);
    if (link_magic_consumption != 2) {
      fprintf(stderr, "Rando_SelfCheck: 2nd magic upgrade should advance to 2 (quarter)\n");
      exit(2);
    }
    icon = rando_direct_grant_icon_entry(
        rando_direct_grant_icon_item_post_grant(ITEM_QuarterMagic));
    if (icon == NULL || icon->gfx != kRandoCustomGfx_QuarterMagic) {
      fprintf(stderr, "Rando_SelfCheck: second magic confirmation should show 1/4 icon\n");
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
    icon = rando_direct_grant_icon_entry(
        rando_direct_grant_icon_item_pre_grant(ITEM_QuarterMagic));
    if (icon == NULL || icon->gfx != kRandoCustomGfx_HalfMagic) {
      fprintf(stderr, "Rando_SelfCheck: QuarterMagic first should draw 1/2 icon\n");
      exit(2);
    }
    Rando_DispatchVanillaGrant(166, ITEM_BottleEmpty, 0x16);
    if (link_magic_consumption != 1) {
      fprintf(stderr, "Rando_SelfCheck: QuarterMagic from 0 should advance to 1 (progressive, not jump to 2)\n");
      exit(2);
    }
    icon = rando_direct_grant_icon_entry(
        rando_direct_grant_icon_item_post_grant(ITEM_QuarterMagic));
    if (icon == NULL || icon->gfx != kRandoCustomGfx_HalfMagic) {
      fprintf(stderr, "Rando_SelfCheck: QuarterMagic first confirmation should show 1/2 icon\n");
      exit(2);
    }
    Placement_Install(NULL);
    link_magic_consumption = 0;
  }

  // Trap masquerade icons are drawn from the full good-item decoy pool, not
  // from the junk/resource items the placer replaced.
  {
    DirectGrantIconEntry first = {0, 0, 0}, icon = {0, 0, 0};
    bool saw_variation = false;
    for (uint32 i = 0; i < rando_trap_good_item_decoy_count(); i++) {
      uint16 decoy_item = rando_trap_good_item_decoy_at(i);
      if (!rando_good_item_decoy_icon(decoy_item, &icon)) {
        fprintf(stderr, "Rando_SelfCheck: trap good-item decoy %u has no icon\n",
                (unsigned)decoy_item);
        exit(2);
      }
      if (icon.gfx == 0x24 && icon.big == 0x00 && icon.oam_flags == 0x38) {
        fprintf(stderr, "Rando_SelfCheck: trap decoy pool includes green rupee icon\n");
        exit(2);
      }
      if (i == 0) {
        first = icon;
      } else if (icon.gfx != first.gfx || icon.big != first.big ||
                 icon.oam_flags != first.oam_flags) {
        saw_variation = true;
      }
    }
    if (!saw_variation) {
      fprintf(stderr, "Rando_SelfCheck: trap decoy pool should vary icons\n");
      exit(2);
    }
    if (!rando_trap_decoy_icon(ITEM_TrapDamage, 166, &first) ||
        !rando_trap_decoy_icon(ITEM_TrapFreeze, 167, &icon)) {
      fprintf(stderr, "Rando_SelfCheck: trap decoy resolver failed\n");
      exit(2);
    }
    if (first.gfx == 0x24 && first.big == 0x00 && first.oam_flags == 0x38) {
      fprintf(stderr, "Rando_SelfCheck: TrapDamage should not draw green rupee icon\n");
      exit(2);
    }
    if (icon.gfx == 0x24 && icon.big == 0x00 && icon.oam_flags == 0x38) {
      fprintf(stderr, "Rando_SelfCheck: TrapFreeze should not draw green rupee icon\n");
      exit(2);
    }
    static RandoPlacement trap_icon_entries[1];
    trap_icon_entries[0].location_id = 166;
    trap_icon_entries[0].item_id = ITEM_TrapDamage;
    RandoPlacementTable trap_icon_table = { trap_icon_entries, 1 };
    uint32 saved_features1 = enhanced_features1;
    bool saved_field_item_sprites = g_config.field_item_sprites;
    uint16 saved_last_location_id = g_last_dispatched_location_id;
    Placement_Install(&trap_icon_table);
    enhanced_features1 |= kFeatures1_RandomizerActive;
    g_config.field_item_sprites = true;
    uint8 field_gfx = 0, field_big = 0, field_oam = 0;
    if (!Rando_GetFieldItemIcon(166, ITEM_BottleEmpty,
                                &field_gfx, &field_big, &field_oam)) {
      fprintf(stderr, "Rando_SelfCheck: trap field icon did not resolve\n");
      exit(2);
    }
    g_last_dispatched_location_id = 166;
    if (!rando_trap_decoy_icon(ITEM_TrapDamage, g_last_dispatched_location_id,
                               &icon) ||
        field_gfx != icon.gfx || field_big != icon.big ||
        field_oam != icon.oam_flags) {
      fprintf(stderr, "Rando_SelfCheck: trap field/confirmation icons diverged\n");
      exit(2);
    }
    Placement_Install(NULL);
    enhanced_features1 = saved_features1;
    g_config.field_item_sprites = saved_field_item_sprites;
    g_last_dispatched_location_id = saved_last_location_id;
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
    // Regression coverage for the game-order dungeons that differ from the
    // ALTTPR registry order.
    entries[0].item_id = ITEM_BigKey_SkullWoods;  // game-side dungeon 8
    Placement_Install(&t);
    link_bigkey = 0;
    Rando_DispatchVanillaGrant(166, ITEM_BottleEmpty, 0x16);
    if (link_bigkey != 0x0080) {
      fprintf(stderr, "Rando_SelfCheck: BigKey_SW dispatch should set link_bigkey=0x0080 (got 0x%04x)\n",
              (unsigned)link_bigkey);
      exit(2);
    }
    entries[0].item_id = ITEM_Map_SwampPalace;  // game-side dungeon 5
    Placement_Install(&t);
    link_dungeon_map = 0;
    Rando_DispatchVanillaGrant(166, ITEM_BottleEmpty, 0x16);
    if (link_dungeon_map != 0x0400) {
      fprintf(stderr, "Rando_SelfCheck: Map_SP dispatch should set link_dungeon_map=0x0400 (got 0x%04x)\n",
              (unsigned)link_dungeon_map);
      exit(2);
    }
    entries[0].item_id = ITEM_Compass_PalaceOfDarkness;  // game-side dungeon 6
    Placement_Install(&t);
    link_compass = 0;
    Rando_DispatchVanillaGrant(166, ITEM_BottleEmpty, 0x16);
    if (link_compass != 0x0200) {
      fprintf(stderr, "Rando_SelfCheck: Compass_PoD dispatch should set link_compass=0x0200 (got 0x%04x)\n",
              (unsigned)link_compass);
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
    uint8 saved_slot7 = link_keys_earned_per_dungeon[7];
    uint8 saved_slot8 = link_keys_earned_per_dungeon[8];
    uint8 saved_slot10 = link_keys_earned_per_dungeon[10];

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

    // (c) Skull Woods key collected while standing in Skull Woods (raw
    // cur_palace_index_x2 = 16 = game-side index 8). This guards the dark-world
    // game-order remap: old code credited slot 7 and left the live counter at 0.
    entries[0].item_id = ITEM_SmallKey_SkullWoods;  // 60 -> game-side dungeon 8
    cur_palace_index_x2 = 16;
    link_num_keys = 0;
    link_keys_earned_per_dungeon[7] = 5;
    link_keys_earned_per_dungeon[8] = 0;
    Placement_Install(&t);
    Rando_DispatchVanillaGrant(166, ITEM_BottleEmpty, 0x16);
    if (link_num_keys != 1 || link_keys_earned_per_dungeon[8] != 1 ||
        link_keys_earned_per_dungeon[7] != 5) {
      fprintf(stderr, "Rando_SelfCheck: SmallKey_SW in SW should bump live+slot8 only "
                      "(got live=%u slot7=%u slot8=%u)\n",
              (unsigned)link_num_keys,
              (unsigned)link_keys_earned_per_dungeon[7],
              (unsigned)link_keys_earned_per_dungeon[8]);
      exit(2);
    }

    Placement_Install(NULL);
    cur_palace_index_x2 = saved_palace;
    link_num_keys = saved_keys;
    link_keys_earned_per_dungeon[7] = saved_slot7;
    link_keys_earned_per_dungeon[8] = saved_slot8;
    link_keys_earned_per_dungeon[10] = saved_slot10;
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
    // the original loop's ternary always evaluated
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

// End-to-end install check for the DROP shuffle runtime wiring + BOSS shuffle
// render/logic assignment install. The per-module self-checks
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
  // Boss shuffle ON in the SAME slot must install the render + logic assignment
  // matching BossShuffle_ComputeAssignment. This case guards both shuffles at
  // once.
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

  // (2) Reactivation with a DIFFERENT seed must OVERWRITE the drop table and
  // boss assignment (no stale leak).
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

// guard — the entrance-shuffle digest (and therefore the installed
// overlay, which shares Entrance_ComputeLayout) must read the PRISTINE vanilla
// entrance-id table even while the Inverted static override owns
// g_asset_ptrs[126]. Repro of the field bug: generating an entrance-shuffle
// seed from the native window while an Inverted slot was active baked a digest
// over the INVERTED table into the sidecar (permanent false refusal after
// restart), and activating a clean entrance slot over a still-installed
// Inverted slot false-refused once. Asserts digest(pristine) ==
// digest(Inverted installed) == digest(after teardown) for a fixed
// (seed, axes, attempt). The selftest runs without zelda3_assets.dat, so any
// absent shadowed asset gets a synthetic all-zero source for the duration
// (restored after); with real assets loaded the real tables are used.
static void Rando_EntranceContaminationSelfCheck(void) {
  enum { kSynthSize = 0x100, kMaxShadowed = 32 };
  static const uint8 zero_src[kSynthSize];  // shared read-only synthetic source
  uint8 idx[kMaxShadowed];
  bool synth[kMaxShadowed] = { false };
  int n = InvertedEntrances_ShadowedAssets(idx, kMaxShadowed);
  if (n > kMaxShadowed)
    tsc_die("EntranceContamination: shadowed-asset list outgrew kMaxShadowed");
  for (int i = 0; i < n; i++) {
    if (g_asset_ptrs[idx[i]] == NULL) {
      g_asset_ptrs[idx[i]] = zero_src;
      g_asset_sizes[idx[i]] = kSynthSize;
      synth[i] = true;
    }
  }

  // Fixed entrance-shuffle header: cave shuffle, Open world, attempt 0, fixed
  // seed in share_string bytes [21..28] (the layout SlotSeedFromShareString reads).
  RandoSlotHeader h;
  memset(&h, 0, sizeof(h));
  h.slot_kind = kSlotKind_Randomizer;
  h.generator_version = (uint16)kGeneratorVersion;
  h.entrance_axes = kEntranceAxis_ShuffleCaves;
  h.settings_ext_present = 1;
  h.world_state = (uint8)kWorldState_Open;
  ShareString ss;
  memset(&ss, 0, sizeof(ss));
  ss.version = (uint8)kGeneratorVersion;
  ss.seed_u64 = 0xC0A7A317D16E5713ull;
  Share_PackBinary(&ss, h.share_string);

  uint32 d0 = Rando_EntranceLayoutDigest24(&h);
  if (d0 == 0) tsc_die("EntranceContamination: baseline digest unavailable");

  // Install the REAL Inverted override (headless-safe: it only repoints asset
  // pointers), then prove the digest is computed over the pristine table, not
  // the live (inverted) one.
  InvertedEntrances_Install((uint8)kWorldState_Inverted);
  const uint8 *saved = InvertedEntrances_SavedEntranceIdOrig();
  if (saved == NULL)
    tsc_die("EntranceContamination: inverted install did not take (test setup)");
  uint32 len = kOverworld_Entrance_Id_SIZE;
  if (len == 0 ||
      memcmp((const uint8 *)kOverworld_Entrance_Id, saved, len) == 0)
    tsc_die("EntranceContamination: live table identical to pristine (trivial test)");
  uint32 d1 = Rando_EntranceLayoutDigest24(&h);
  InvertedEntrances_Teardown();
  uint32 d2 = Rando_EntranceLayoutDigest24(&h);

  // Restore the synthetic sources before any assertion can exit. (tsc_die
  // exits the process, so restoration only matters on the OK path, but keep
  // the asset table clean before the checks for hygiene.)
  for (int i = 0; i < n; i++) {
    if (synth[i]) {
      g_asset_ptrs[idx[i]] = NULL;
      g_asset_sizes[idx[i]] = 0;
    }
  }
  if (d1 != d0)
    tsc_die("EntranceContamination: digest drifted while the Inverted override owned asset 126");
  if (d2 != d0)
    tsc_die("EntranceContamination: digest drifted after Inverted teardown");
  fprintf(stderr, "[Rando_EntranceContaminationSelfCheck] OK\n");
}

// guard (+ assignment arm) — generate-then-reinstall must
// restore the ACTIVE slot's LOGIC overlays (entrance region/edge overrides +
// door logic layout) AND the logic-VM prize/medallion/boss assignment
// pointers, and with NO active slot the reinstall must be a safe clearing
// no-op with the assignments NULLed (fail-closed). Fakes the active-slot
// replay inputs (g_rando_active_header / g_rando_active_door_logic /
// g_rando_active_settings_valid + the active assignment arrays
// — the exact stores Rando_ReinstallActiveSlotLogicOverlays reads) directly,
// since full Rando_ActivateSidecarSlot needs a real sidecar slot. Entrance and
// door arms are exercised on SEPARATE fake slots (the two shuffles are
// mutually exclusive in real slots per apply_derived_rules). Headless-safe:
// synthesizes asset 126 when absent (the logic overrides derive from
// kCaveInteriors/kDungeons + the permutation, not the id-table bytes).
static uint64 ReinstallCheck_FoldLogicState(void) {
  // FNV-1a over the observable override state, via the same getters the
  // reachability engine uses. Bounds mirror rando_logic.c's stores
  // (kEntranceRegionOverrideMax = 512, kEntranceEdgeOverrideMax = 64); the
  // getters bound-check, so over-iteration is harmless.
  uint64 h = 0xcbf29ce484222325ull;
  for (uint32 i = 0; i < 512; i++) {
    uint16 v = Rando_GetEntranceRegionOverride((uint16)i);
    h = (h ^ (v & 0xFF)) * 0x100000001b3ull;
    h = (h ^ (v >> 8)) * 0x100000001b3ull;
  }
  for (uint32 i = 0; i < 64; i++) {
    uint16 v = Rando_GetEntranceEdgeOverride((uint16)i);
    h = (h ^ (v & 0xFF)) * 0x100000001b3ull;
    h = (h ^ (v >> 8)) * 0x100000001b3ull;
  }
  return h;
}

static void Rando_ReinstallOverlaysSelfCheck(void) {
  static const uint8 zero_ids[0x100];
  bool synth = false;
  if (g_asset_ptrs[126] == NULL) {
    g_asset_ptrs[126] = zero_ids;
    g_asset_sizes[126] = sizeof(zero_ids);
    synth = true;
  }

  // Generation-style clobber values for the assignment stores: stand-ins for
  // the placer's per-run assignments (boss + prize/medallion) that
  // Place_AssumedFill installs on every run.
  static uint8 gen_prize[kRandoDungeonCount];
  static uint8 gen_medallion[kRandoMedallionEntranceCount];
  static uint8 gen_boss[16];
  for (uint32 i = 0; i < (uint32)sizeof(gen_prize); i++)
    gen_prize[i] = (uint8)(0x10 + i);
  for (uint32 i = 0; i < (uint32)sizeof(gen_medallion); i++)
    gen_medallion[i] = (uint8)(0x20 + i);
  for (uint32 i = 0; i < (uint32)sizeof(gen_boss); i++)
    gen_boss[i] = (uint8)(0x30 + i);

  // --- 1) No active slot: install junk into all the stores (as a clobber
  // would), reinstall, and assert everything is CLEARED (the safe no-op) and
  // the assignment trio is NULL (fail-closed, mirroring activation's
  // !settings_valid arm).
  g_rando_active_header_valid = false;
  g_rando_active_door_logic = false;
  g_rando_active_settings_valid = false;
  Rando_BeginEntranceRegionOverrides();
  Rando_SetEntranceRegionOverride(3, 7);
  Rando_BeginEntranceEdgeOverrides();
  Rando_SetEntranceEdgeOverride(4, 9);
  static DoorShuffleLayout junk_layout;  // pointer install only; never read
  Rando_SetDoorLogicLayout(&junk_layout, 1);
  Rando_SetDungeonPrizeAssignment(gen_prize);
  Rando_SetMedallionAssignment(gen_medallion);
  Rando_SetBossAssignment(gen_boss);
  Rando_ReinstallActiveSlotLogicOverlays();
  uint16 mask = 0xFFFF;
  if (Rando_GetEntranceRegionOverride(3) != 0xFFFF ||
      Rando_GetEntranceEdgeOverride(4) != 4 ||
      Rando_GetDoorLogicLayout(&mask) != NULL || mask != 0)
    tsc_die("ReinstallOverlays: no-active-slot reinstall did not clear the stores");
  if (Rando_GetDungeonPrizeAssignment() != NULL ||
      Rando_GetMedallionAssignment() != NULL ||
      Rando_GetBossAssignment() != NULL)
    tsc_die("ReinstallOverlays: no-active-slot reinstall did not NULL the assignments (fail-closed)");

  // --- 2a) Entrance arm: fake an active cave+dungeon-shuffle slot, install
  // via the helper, digest the logic state, clobber exactly as a generation
  // does, reinstall, and assert the digest round-trips.
  RandoSlotHeader h;
  memset(&h, 0, sizeof(h));
  h.slot_kind = kSlotKind_Randomizer;
  h.generator_version = (uint16)kGeneratorVersion;
  h.entrance_axes = kEntranceAxis_ShuffleCaves | kEntranceAxis_ShuffleDungeons;
  h.settings_ext_present = 1;
  h.world_state = (uint8)kWorldState_Open;
  ShareString ss;
  memset(&ss, 0, sizeof(ss));
  ss.version = (uint8)kGeneratorVersion;
  ss.seed_u64 = 0x5EEDF00DCAFE1234ull;
  Share_PackBinary(&ss, h.share_string);
  g_rando_active_header = h;
  g_rando_active_header_valid = true;
  g_rando_active_door_logic = false;
  Rando_ReinstallActiveSlotLogicOverlays();
  uint64 d_pre = ReinstallCheck_FoldLogicState();
  Entrance_ClearRegionOverrides();   // == Rando_PlaceWithEntrances' clears
  Entrance_ClearEdgeOverrides();
  Rando_SetDoorLogicLayout(NULL, 0); // == Rando_ClearGenerationLogicOverlays
  if (ReinstallCheck_FoldLogicState() == d_pre)
    tsc_die("ReinstallOverlays: cleared state digests equal (trivial test)");
  Rando_ReinstallActiveSlotLogicOverlays();
  if (ReinstallCheck_FoldLogicState() != d_pre)
    tsc_die("ReinstallOverlays: entrance overrides did not round-trip");

  // --- 2b) Door arm on a separate fake slot (entrance_axes = 0). Find an
  // attempt the generator accepts (mirrors generation's retry loop), install
  // via the helper, clobber, reinstall, and assert pointer/mask/digest match.
  memset(&h, 0, sizeof(h));
  h.slot_kind = kSlotKind_Randomizer;
  h.generator_version = (uint16)kGeneratorVersion;
  h.settings_ext_present = 1;
  h.world_state = (uint8)kWorldState_Open;
  Share_PackBinary(&ss, h.share_string);
  uint32 datt = 0xFFFFFFFF;
  for (uint32 a = 0; a < 16; a++) {
    if (DoorShuffle_Generate(ss.seed_u64, a, kDoorShuffle_MvpDungeonMask,
                             &s_active_door_layout)) {
      datt = a;
      break;
    }
  }
  if (datt == 0xFFFFFFFF)
    tsc_die("ReinstallOverlays: no door layout generated in 16 attempts (test setup)");
  h.door_attempt = (uint8)datt;
  g_rando_active_header = h;
  g_rando_active_header_valid = true;
  g_rando_active_door_logic = true;
  Rando_ReinstallActiveSlotLogicOverlays();
  uint16 mask_pre = 0;
  const DoorShuffleLayout *lp = Rando_GetDoorLogicLayout(&mask_pre);
  if (lp == NULL || mask_pre == 0)
    tsc_die("ReinstallOverlays: door reinstall did not install a layout");
  uint32 ddig_pre = DoorShuffle_LayoutDigest(lp);
  Rando_SetDoorLogicLayout(NULL, 0);  // generation-style clobber
  Rando_ReinstallActiveSlotLogicOverlays();
  uint16 mask_post = 0;
  lp = Rando_GetDoorLogicLayout(&mask_post);
  if (lp == NULL || mask_post != mask_pre || DoorShuffle_LayoutDigest(lp) != ddig_pre)
    tsc_die("ReinstallOverlays: door logic layout did not round-trip");

  // --- 2c) Assignment arm: fake a valid active-settings
  // capture with recognizable bytes, install via the helper, clobber the
  // three logic-VM assignment stores exactly as a generation does, reinstall,
  // and assert the ACTIVE bytes are back. Then flip settings_valid off and
  // assert the fail-closed NULL arm.
  g_rando_active_door_logic = false;  // keep the door arm cheap/no-op here
  for (uint32 i = 0; i < (uint32)sizeof(g_rando_active_prize_assignment); i++)
    g_rando_active_prize_assignment[i] = (uint8)(0x40 + i);
  for (uint32 i = 0; i < (uint32)sizeof(g_rando_active_medallion_assignment); i++)
    g_rando_active_medallion_assignment[i] = (uint8)(0x60 + i);
  for (uint32 i = 0; i < (uint32)sizeof(g_rando_active_boss_assignment); i++)
    g_rando_active_boss_assignment[i] = (uint8)(0x80 + i);
  g_rando_active_settings_valid = true;
  Rando_ReinstallActiveSlotLogicOverlays();
  if (Rando_GetDungeonPrizeAssignment() == NULL ||
      Rando_GetMedallionAssignment() == NULL ||
      Rando_GetBossAssignment() == NULL ||
      memcmp(Rando_GetDungeonPrizeAssignment(), g_rando_active_prize_assignment,
             sizeof(g_rando_active_prize_assignment)) != 0 ||
      memcmp(Rando_GetMedallionAssignment(), g_rando_active_medallion_assignment,
             sizeof(g_rando_active_medallion_assignment)) != 0 ||
      memcmp(Rando_GetBossAssignment(), g_rando_active_boss_assignment,
             sizeof(g_rando_active_boss_assignment)) != 0)
    tsc_die("ReinstallOverlays: active-settings reinstall did not install the active assignments");
  Rando_SetDungeonPrizeAssignment(gen_prize);      // == place_assumed_fill_attempt
  Rando_SetMedallionAssignment(gen_medallion);     // == place_assumed_fill_attempt
  Rando_SetBossAssignment(gen_boss);               // == Place_AssumedFill
  if (Rando_GetDungeonPrizeAssignment() == NULL ||
      Rando_GetDungeonPrizeAssignment()[0] != 0x10 ||
      Rando_GetMedallionAssignment() == NULL ||
      Rando_GetMedallionAssignment()[0] != 0x20 ||
      Rando_GetBossAssignment() == NULL ||
      Rando_GetBossAssignment()[0] != 0x30)
    tsc_die("ReinstallOverlays: assignment clobber did not take (trivial test)");
  Rando_ReinstallActiveSlotLogicOverlays();
  if (Rando_GetDungeonPrizeAssignment() == NULL ||
      Rando_GetMedallionAssignment() == NULL ||
      Rando_GetBossAssignment() == NULL ||
      memcmp(Rando_GetDungeonPrizeAssignment(), g_rando_active_prize_assignment,
             sizeof(g_rando_active_prize_assignment)) != 0 ||
      memcmp(Rando_GetMedallionAssignment(), g_rando_active_medallion_assignment,
             sizeof(g_rando_active_medallion_assignment)) != 0 ||
      memcmp(Rando_GetBossAssignment(), g_rando_active_boss_assignment,
             sizeof(g_rando_active_boss_assignment)) != 0)
    tsc_die("ReinstallOverlays: assignments did not round-trip after a generation-style clobber");
  // Generation never writes the ACTIVE arrays' bytes — assert the source
  // contents survived untouched, not just the installed copy.
  if (Rando_GetDungeonPrizeAssignment()[0] != 0x40 ||
      Rando_GetMedallionAssignment()[0] != 0x60 ||
      Rando_GetBossAssignment()[15] != 0x8F)
    tsc_die("ReinstallOverlays: active assignment bytes were corrupted");
  // Fail-closed arm: invalid settings capture must NULL all three even with a
  // generation clobber still installed.
  g_rando_active_settings_valid = false;
  Rando_SetDungeonPrizeAssignment(gen_prize);
  Rando_SetMedallionAssignment(gen_medallion);
  Rando_SetBossAssignment(gen_boss);
  Rando_ReinstallActiveSlotLogicOverlays();
  if (Rando_GetDungeonPrizeAssignment() != NULL ||
      Rando_GetMedallionAssignment() != NULL ||
      Rando_GetBossAssignment() != NULL)
    tsc_die("ReinstallOverlays: invalid-settings reinstall did not fail closed to NULL assignments");

  // Restore the idle state (and the asset table when synthesized).
  g_rando_active_header_valid = false;
  g_rando_active_door_logic = false;
  g_rando_active_settings_valid = false;
  memset(g_rando_active_prize_assignment, 0, sizeof(g_rando_active_prize_assignment));
  memset(g_rando_active_medallion_assignment, 0, sizeof(g_rando_active_medallion_assignment));
  memset(g_rando_active_boss_assignment, 0, sizeof(g_rando_active_boss_assignment));
  Entrance_ClearRegionOverrides();
  Entrance_ClearEdgeOverrides();
  Rando_SetDoorLogicLayout(NULL, 0);
  Rando_SetDungeonPrizeAssignment(NULL);
  Rando_SetMedallionAssignment(NULL);
  Rando_SetBossAssignment(NULL);
  if (synth) {
    g_asset_ptrs[126] = NULL;
    g_asset_sizes[126] = 0;
  }
  fprintf(stderr, "[Rando_ReinstallOverlaysSelfCheck] OK\n");
}

void Rando_RunAllSelfChecks(void) {
  Rando_SelfCheck();
  Rando_Rng_SelfCheck();
  Share_SelfCheck();
  Settings_SelfCheck();
  Logic_SelfCheck();
  Placement_SelfCheck();
  SeedShape_SelfCheck();
  Shuffles_SelfCheck();
  BossShuffle_SelfCheck();
  DropShuffle_SelfCheck();
  EnemyShuffle_SelfCheck();  // add-rando-enemy-shuffle
  Customizer_SelfCheck();    // add-rando-customizer-mode
  Customizer_PlacementSelfCheck();  // customizer placement-path regression guard
  RandoSave_SelfCheck();
  RandoGenerate_SelfCheck();
  RandoSnapshotTail_SelfCheck();
  TextField_SelfCheck();
  Hints_SelfCheck();
  Entrance_SelfCheck();
  Rando_EntranceContaminationSelfCheck();  // digest vs Inverted-owned asset 126
  Rando_ReinstallOverlaysSelfCheck();      // generate-then-reinstall round-trip
  Cosmetic_SelfCheck();
  Rando_TrackerSelfCheck();
  Rando_StartingInventorySelfCheck();
  Rando_ShuffleInstallSelfCheck();
  Rando_MedallionIcons_SelfCheck();
  fprintf(stderr, "Rando_RunAllSelfChecks: all subsystems OK.\n");
}
