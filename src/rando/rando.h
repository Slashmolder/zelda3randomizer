// rando.h — randomizer master header. Phase A0 stub.
//
// Per add-randomizer-support proposal: this module is the seed-generation
// pipeline, runtime override layer for vanilla item-grant sites, and host of
// the per-slot randomizer state. All public types and entry points the rest
// of the codebase consumes live here.
//
// Phase A0 status: this header declares the API surface. The .c files that
// implement it land incrementally through Phase A.

#ifndef ZELDA3_RANDO_H_
#define ZELDA3_RANDO_H_

#include "../types.h"

// ---------------------------------------------------------------------------
// kGeneratorVersion — bumped per tasks.md §13.6 whenever placement output
// could change. The bump triggers regression-corpus regeneration.
//
// Phase A0 starts at 0; first real placement-affecting change bumps to 1.
// ---------------------------------------------------------------------------
#define kGeneratorVersion 10u

// ---------------------------------------------------------------------------
// g_assets_hash — SHA-256 of the loaded asset blob (computed once after
// LoadAssets returns; see tasks.md §1.1a). Compared against kVanillaAssetsHash
// from the generated header src/rando/vanilla_assets_hash.h for the
// non-vanilla-asset-warning flow.
// ---------------------------------------------------------------------------
extern uint8 g_assets_hash[32];

// ---------------------------------------------------------------------------
// Rando_OnLocationCheck — the universal grant-site dispatcher
// (tasks.md §6.1; randomizer-placement spec).
//
// Called from every grant site listed in audit.md §0.3 with:
//   - location_id : ALTTPR canonical numeric id (from location_registry.yaml)
//   - vanilla_item_id : the item the vanilla game would have granted
//
// Returns the item_id to actually grant. On an unknown location_id (e.g., a
// rando slot from a future binary that has more locations than this one
// knows), the dispatcher returns vanilla_item_id unchanged so the vanilla
// grant path runs.
//
// Wrapped at each call site by:
//   if (enhanced_features1 & kFeatures1_RandomizerActive) {
//     item = Rando_OnLocationCheck(<location_id_const>, item);
//   }
// ---------------------------------------------------------------------------
uint16 Rando_OnLocationCheck(uint16 location_id, uint16 vanilla_item_id);

// ---------------------------------------------------------------------------
// Rando_DispatchVanillaGrant — convenience for §6 grant-site wrappers that
// route through the existing Link_ReceiveItem(uint8 lttp_code, ...) path.
//
// At every grant site that grants a vanilla LttP item code, wrap the call:
//
//     uint8 lttp_code = 0x16;  // BottleEmpty
//     if (enhanced_features1 & kFeatures1_RandomizerActive) {
//       lttp_code = Rando_DispatchVanillaGrant(LOC_<X>, ITEM_<vanilla>, lttp_code);
//     }
//     Link_ReceiveItem(lttp_code, 0);
//
// `vanilla_registry_id` is the rando registry id of the vanilla item this
// site would have granted (looked up via item_ids.h's ITEM_<Name>).
// `vanilla_lttp_code` is the LttP receive-item code the site would have
// passed to Link_ReceiveItem in vanilla play (e.g. 0x16 for a bottle).
//
// Returns the LttP code to actually grant. If the placed item has no
// vanilla LttP dispatch (progressive / dungeon-item / prize / virtual),
// returns `vanilla_lttp_code` unchanged — those item classes need
// per-class handlers (§6.2 work) that the universal dispatcher cannot
// emit through the existing receive-item path.
//
// §6.2 sentinel return: `kRandoLttpSkip` (0xFE) means "the rando subsystem
// has already granted the placed item via a direct write; caller MUST NOT
// invoke Link_ReceiveItem". This signals to the caller that all bookkeeping
// is done. Use the convenience wrapper `Rando_ShouldSkipReceive(code)` to
// test the return value.
// ---------------------------------------------------------------------------
#define kRandoLttpSkip 0xFEu
uint8 Rando_DispatchVanillaGrant(uint16 location_id,
                                 uint16 vanilla_registry_id,
                                 uint8 vanilla_lttp_code);

// Returns true if `lttp_code` is the §6.2 "skip Link_ReceiveItem" sentinel.
// Phase A1: enabled for HalfMagic/QuarterMagic/TriforcePiece/prize-bit
// items, which dispatch via direct writes inside Rando_DispatchVanillaGrant.
static inline int Rando_ShouldSkipReceive(uint8 lttp_code) {
  return lttp_code == kRandoLttpSkip;
}

// ---------------------------------------------------------------------------
// Rando_ChestDispatch — universal chest grant-site hook.
//
// Hooked at Link_PerformOpenChest, AFTER OpenChestForItem returns the item
// to give. Maps (dungeon_room_index, chest_ordinal) to an ALTTPR location_id
// via a generated lookup table, then invokes Rando_DispatchVanillaGrant.
//
// `dungeon_room` is the global dungeon room index (matches ALTTPR's room id).
// `chest_ordinal` is the 0-based index of this chest within the room (0..5).
// `vanilla_lttp_code` is the LttP receive-item code OpenChestForItem returned.
//
// Returns the LttP code to grant. When the (room, ordinal) pair isn't in the
// lookup table (universal hook covers chests we haven't enumerated yet), the
// vanilla code is returned — the chest grants its vanilla item, as if rando
// were inactive for that specific chest.
//
// Lookup table is generated by assets/rando_logic_gen.py from the embedded
// generated chest table (assets/chest_data.py) cross-referenced
// with ALTTPR PHP location names + assets/rando/location_registry.yaml; the
// header lives at src/rando/chest_lookup.h. 164 entries covering all
// Chest/BigChest-typed locations from audit.md §0.3.5 (the 165th, "Chest
// Game", is the minigame path handled at §6.8).
// ---------------------------------------------------------------------------
uint8 Rando_ChestDispatch(uint16 dungeon_room, uint8 chest_ordinal,
                          uint8 vanilla_lttp_code);

// ---------------------------------------------------------------------------
// Rando_BumpReachabilityCounter — invalidates the tracker's memoized
// reachability cache when a story-progress event flag is written
// (tasks.md §0.4a). Called from every reachability-affecting write site
// enumerated in audit.md §0.4a.
// ---------------------------------------------------------------------------
void Rando_BumpReachabilityCounter(void);

// ---------------------------------------------------------------------------
// §6.6 boss-kill dispatch helpers. The boss-kill code path in dungeon.c
// fires TWO grant sites per boss: the BossHeart drop (Sprite_HeartContainer)
// and the Prize crystal/pendant (RoomTag_GetHeartForPrize). Each has its
// own LOC_* per audit.md §0.3.5.
//
// `dungeon_id` is `cur_palace_index_x2 >> 1` (0..12; HCE=0, EP=1, ..., GT=12).
// Returns 0xFFFF for dungeons without a boss drop (HCE/HCT/GT — those have
// their own dispatch paths: Sanctuary chest, Agahnim event, Agahnim 2 event).
// ---------------------------------------------------------------------------
uint16 Rando_GetBossHeartLocation(uint8 dungeon_id);
uint16 Rando_GetBossPrizeLocation(uint8 dungeon_id);

// ---------------------------------------------------------------------------
// Active per-seed shuffle assignments. The predicate VM's OP_HAS_PRIZE and
// OP_MEDALLION_OPENS consult these via PredicateContext; the placer + sphere
// computation + tracker call Logic_ComputeReachability which reads them.
//
// Callers MUST set these before generating reachability — otherwise both ops
// degrade to "false", which makes prize-gated locations (Sahasrahla, GT
// entry, Master Sword Pedestal) and medallion-gated dungeons (MM, TR)
// unreachable.
//
// Pass NULL to clear (resets to "no assignment installed" — falls back to
// false). Pointers are borrowed; caller retains ownership.
// ---------------------------------------------------------------------------
void Rando_SetDungeonPrizeAssignment(const uint8 *assignment);    // [kRandoDungeonCount]
void Rando_SetMedallionAssignment(const uint8 *assignment);       // [kRandoMedallionEntranceCount]
const uint8 *Rando_GetDungeonPrizeAssignment(void);
const uint8 *Rando_GetMedallionAssignment(void);

// ---------------------------------------------------------------------------
// Rando_DrawHashIcons (tasks.md §9.4b — 5-icon visual hash widget).
//
// Renders a 5-tile horizontal strip starting at (x, y), one tile per index
// from kHashIconAtlas. The tile indices are derived from
// `SHA-256(share_string_binary)[0..4] mod kHashIconAtlasSize` per the
// randomizer-ui spec. Critical: the hash input is the FULL share-string
// binary (31 bytes: magic + version + settings_hash + seed_u64 + checksum),
// NOT settings_hash alone — otherwise every seed with identical settings
// would render identical icons (architectural error caught in spec round 5).
//
// Writes 5 consecutive OAM entries beginning at *oam. The widget reserves a
// horizontal strip of 5*8 = 40 px starting at x. The OAM palette flags
// match the file-select font palette so the icons read cleanly against the
// existing background.
//
// `share_string_binary` MUST be the 31-byte raw binary blob (the same data
// that Share_EncodeRaw base32-encodes for display). The buffer is sized to
// 32 to match RandoSlotHeader.share_string[]; the last byte is zero-pad
// per the rando_save.h spec.
// ---------------------------------------------------------------------------
struct OamEnt;  // forward decl (defined in zelda_rtl.h via spc_player.h)
void Rando_DrawHashIcons(int x, int y,
                         struct OamEnt *oam,
                         const uint8 share_string_binary[32]);

// ---------------------------------------------------------------------------
// Self-tests (tasks.md §2.2, §13.x). Always linked; CI invokes via
// `--rando-selftest`. Exits with code 2 on any failure.
// ---------------------------------------------------------------------------
void Rando_SelfCheck(void);            // SHA-256 NIST vectors
void Rando_RunAllSelfChecks(void);     // SHA-256 + RNG (+ future subsystems)

#endif  // ZELDA3_RANDO_H_

