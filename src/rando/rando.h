// rando.h — randomizer master header.
//
// This module is the seed-generation pipeline, the runtime override layer for
// vanilla item-grant sites, and the host of the per-slot randomizer state. All
// public types and entry points the rest of the codebase consumes live here;
// the .c files under src/rando/ implement them.

#ifndef ZELDA3_RANDO_H_
#define ZELDA3_RANDO_H_

#include "../types.h"
#include "rando_settings.h"  // RandoSettings (Rando_GetActiveSettings)
#include "rando_logic.h"     // RandoCounts, RandoReachability (live reachability bridge)

// ---------------------------------------------------------------------------
// kGeneratorVersion — bumped per tasks.md §13.6 whenever placement output
// could change. The bump triggers regression-corpus regeneration.
// ---------------------------------------------------------------------------
#define kGeneratorVersion 48u  // 47→48: Inverted Floating Island (loc 205) gets an Inverted logic override gating it on region access only (`TRUE()`), matching ALTTPR (Inverted East.php's initalize() never sets its requirements). Previously the codegen "last-wins" merge left the STRICT Standard predicate applied to Inverted, making the Piece of Heart harder/potentially unplaceable. This changes Inverted reachability for that location, so an Inverted seed's placement could route differently and its sphere structure changes. Corpus regenerated (only b-inverted-ganon-7-7's sphere_digest shifted; placement digests stable).
                               // 46→47: fork-extension hint NPCs (Storyteller + Kakariko/Dark-World Fortune Tellers, ids 17-19) add three entries to the spoiler `hints[]` for any hints-on seed. The race-mode anti-tamper stamp is a SHA-256 over the full spoiler JSON, so this changes the stamp bytes for race-mode + hints-on seeds. Without this bump a v46 race seed (minted before the fork NPCs) would regenerate a 3-entry-larger hints[] at reveal → false `kRandoReveal_StampMismatch` ("tampered"). The bump makes the version gate fire first → honest `kRandoReveal_VersionMismatch` ("regenerate the seed"). Placement/sphere digests are unchanged (hints are generated post-placement; generator_version is not an RNG input), so the corpus is byte-identical — verified, not regenerated.
                               // 45→46: ALTTPR three-way accessibility ("beatable only"). The per-tier acceptance gate (Accessibility_SeedAcceptable) changes the semantics of all three accessibility values — `items` now requires every progression item reachable, `locations` every location, and `none`/"beatable only" is now guaranteed beatable (no longer ships unwinnable seeds). Share strings carry the version byte so old `none` seeds don't silently reproduce under the new semantics. Corpus regenerated.

// Audit L7 — the share-string binary layout packs version into 1 byte
// (rando_share.h: ShareString.version is uint8). Compile-time enforce
// kGeneratorVersion ≤ 255 so silent truncation can't ship.
// C++ uses the static_assert keyword; C11 uses _Static_assert. rando.h is
// included from the C++ tracker-window TUs, so pick the right spelling.
#ifdef __cplusplus
static_assert(kGeneratorVersion <= 0xFFu,
#else
_Static_assert(kGeneratorVersion <= 0xFFu,
#endif
               "kGeneratorVersion exceeds the share-string uint8 version field; "
               "bump ShareString.version to uint16 and rev the share-string binary layout "
               "before incrementing past 255.");

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

// Returns the item id resolved by the most recent Rando_DispatchVanillaGrant
// (or Rando_ChestDispatch) call. Phase B Slice 9 — used by direct-grant
// confirmation sites to feed the per-item icon lookup. Returns 0xFFFF when
// no dispatch has run yet this slot.
uint16 Rando_LastDispatchedItemId(void);

// ---------------------------------------------------------------------------
// Phase B Slice 6 — race-mode reveal action.
//
// Rando_RevealSpoiler reads a race-mode suppressed spoiler at the given path
// (or, if `suppressed_path == NULL`, derives the path from `share_string`
// via Spoiler_ResolvePath), verifies its integrity, reproduces the placement
// deterministically from the embedded settings + seed (decoded from the
// share string), confirms the stamp matches the regenerated spoiler, and
// writes the full JSON + .txt spoiler back to disk (overwriting the
// suppressed binary).
//
// The reveal flow is also the verification flow: stamp mismatch means the
// suppressed file no longer corresponds to the current generator's output
// for this (settings, seed_u64) pair — either the generator changed or the
// file was tampered with.
// ---------------------------------------------------------------------------
typedef enum RandoRevealResult {
  kRandoReveal_Ok = 0,
  kRandoReveal_FileNotFound = 1,
  kRandoReveal_ParseError = 2,
  kRandoReveal_CrcMismatch = 3,
  kRandoReveal_ShareStringMismatch = 4,
  kRandoReveal_VersionMismatch = 5,
  kRandoReveal_StampMismatch = 6,
  kRandoReveal_PlacementFailed = 7,
  kRandoReveal_SettingsCorrupt = 8,
  kRandoReveal_WriteFailed = 9,
} RandoRevealResult;

// Reveal the suppressed spoiler at `suppressed_path`. If `expected_share_string`
// is non-NULL, the reveal also verifies the file's embedded share string
// matches. Caller may pass either (the file path explicitly) OR (an
// expected share string only; path resolved via Spoiler_ResolvePath).
RandoRevealResult Rando_RevealSpoiler(const char *suppressed_path,
                                      const char *expected_share_string);

// Player-facing one-line description for a reveal result, suitable for
// surfacing in the file-select dialog or the CLI's stderr line. Never NULL.
const char *Rando_RevealResultDescription(RandoRevealResult r);

// §62 — in-binary reveal action. Reveals the active slot's suppressed
// spoiler (race-mode `.json` containing the ZRSR header). Returns the
// reveal result enum; emits a one-line log to stderr describing the
// outcome. Returns kRandoReveal_FileNotFound when no slot is active or
// the slot's share string was never captured (no race-mode spoiler to
// reveal).
RandoRevealResult Rando_RevealActiveSlotSpoiler(void);

// Returns true if `lttp_code` is the §6.2 "skip Link_ReceiveItem" sentinel.
// Phase A1: enabled for HalfMagic/QuarterMagic/TriforcePiece/prize-bit
// items, which dispatch via direct writes inside Rando_DispatchVanillaGrant.
static inline int Rando_ShouldSkipReceive(uint8 lttp_code) {
  return lttp_code == kRandoLttpSkip;
}

// ---------------------------------------------------------------------------
// Rando_ShowDirectGrantConfirmation — generic visual+audio confirmation
// for direct-grant placements (tasks.md §7.6 + Phase B Slice 9).
//
// When Rando_DispatchVanillaGrant returns kRandoLttpSkip the caller skips
// Link_ReceiveItem entirely — no animation, no sound. For sites that have
// no other confirmation visual (the §6.5 tablets, several §6.4 NPCs, the
// §6.7 Pyramid Fairy item drop), the player can't tell that anything was
// granted. Callers should invoke this immediately in the skip branch:
//
//   if (Rando_ShouldSkipReceive(lttp_code))
//     Rando_ShowDirectGrantConfirmation(placed_item_id);
//   else
//     Link_ReceiveItem(lttp_code, 0);
//
// where `placed_item_id` is the ITEM_* id of the item that was actually
// direct-written by the dispatcher (resolve via Placement_Lookup or via the
// vanilla item id when no rando swap occurred).
//
// Plays the standard item-receipt sound effect and refreshes the HUD so
// any visible inventory change (prize icons, dungeon-item bits, Triforce
// counter) updates immediately. When `item_id` maps to a non-zero gfx bundle
// in `kDirectGrantIcons[]` (codegen'd from
// `assets/rando/direct_grant_icons.yaml`), additionally spawns the
// `kAncillaType_RandoIconReceipt` ancilla so the player sees what they
// got. Audio-only (gfx==0) entries fall back to the audio+HUD path.
// ---------------------------------------------------------------------------
void Rando_ShowDirectGrantConfirmation(uint8 item_id);

// ---------------------------------------------------------------------------
// Rando_ReceiveOrConfirm — convenience wrapper that combines the standard
// §6 NPC pattern into a single call (tasks.md §7.6 + Phase B Slice 9).
//
// Replaces:
//   if (Rando_ShouldSkipReceive(lttp_code))
//     Rando_ShowDirectGrantConfirmation(item_id);
//   else
//     Link_ReceiveItem(lttp_code, 0);
//
// with:
//   Rando_ReceiveOrConfirm(lttp_code, item_id);
//
// Behavior: when `lttp_code` is the §6.2 skip-sentinel, fires the §7.6
// confirmation cue (sound + HUD refresh + icon ancilla if `item_id` has a
// verified entry in `kDirectGrantIcons[]`). Otherwise invokes
// Link_ReceiveItem with chest_position=0 — every existing call site at the
// NPC dispatch pattern passes 0; sites that need non-zero chest_position
// (chest opens) continue to call Link_ReceiveItem directly with an explicit
// confirmation gate.
//
// `item_id` is the ITEM_* id of the placed item (Placement_Lookup result).
// When the dispatcher returns a vanilla lttp_code (no swap), `item_id` is
// unused; pass the vanilla ITEM_* for symmetry and forward-compat.
// ---------------------------------------------------------------------------
void Rando_ReceiveOrConfirm(uint8 lttp_code, uint8 item_id);

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
// Lookup table (src/rando/chest_lookup.h) is generated by rando_logic_gen.py
// from the chest_table.gen.bin artifact, cross-referenced with ALTTPR names +
// location_registry.yaml. 164 entries; the 165th ("Chest Game") is the
// minigame path handled at §6.8.
// ---------------------------------------------------------------------------
uint8 Rando_ChestDispatch(uint16 dungeon_room, uint8 chest_ordinal,
                          uint8 vanilla_lttp_code);

// ---------------------------------------------------------------------------
// Rando_ShopDispatch — Retro-world-state shop-purchase grant hook (#53).
//
// Hooked at ShopItem_HandleReceipt (src/sprite_main.c), the universal
// shop-item grant point. Maps a shop purchase to an ALTTPR shop-slot
// location_id via the (room, entrance-door, slot-position) disambiguation
// contract, then routes through Rando_DispatchVanillaGrant.
//
// Disambiguation mirrors ALTTPR's SpritePrep_ShopKeeper (z3randomizer
// shopkeeper.asm): vanilla LttP reuses one physical shop room for several
// overworld entrances (e.g. low-byte room 0x0F backs the DW Potion,
// Lumberjack, Outcasts, and Lake-Hylia shops; room 0x12 backs both the DW
// Death-Mountain and LW Lake-Hylia shops). Room alone is ambiguous, so the
// entrance door (`which_entrance`, g_ram+0x10E — ALTTPR's
// PreviousOverworldDoor) selects the specific shop, and `pos` (0..2) selects
// the slot within it.
//
// `room` is BYTE(dungeon_room_index); `entrance` is `which_entrance`;
// `pos` is the 0-based slot index the shopkeeper spawned the item at.
// `vanilla_lttp_code` is the LttP receive code the shop would grant in vanilla.
//
// Returns the LttP code to grant. When (room, entrance, pos) isn't a known
// shop slot — every non-shop ShopItem_HandleReceipt caller (gift thief,
// bomb shop) and every non-Retro seed (shop slots absent from the placement
// table) — the vanilla code is returned unchanged, so behavior is identical
// to vanilla. May return kRandoLttpSkip (test with Rando_ShouldSkipReceive)
// for direct-grant placements.
// ---------------------------------------------------------------------------
uint8 Rando_ShopDispatch(uint8 room, uint8 entrance, uint8 pos,
                         uint8 vanilla_lttp_code);

// ---------------------------------------------------------------------------
// Retro TakeAny caves (Phase B Slice 3b). See add-rando-retro-takeany/design.md.
//
// ALTTPR converts 5-of-31 ordinary overworld caves into "take-any" caves per
// seed (an old man offers a free item; take ONE of two and the cave locks).
// The generator pins the active caves' rewards into the placement table
// (LOC ids 266..327 = 31 caves x 2 slots; only ~9 active). The runtime:
//   1. Overworld_UseEntrance redirects an active cave's overworld door to its
//      take-any host room and captures the door id in g_rando_takeany_door_id.
//   2. SpritePrep_Shopkeeper presents the live slots and suppresses the host
//      room's regular shop (the host rooms ARE regular shops — collision).
//   3. ShopItem_TakeAny grants the taken item free and locks the cave.
// ---------------------------------------------------------------------------

// Set by Overworld_UseEntrance when the player steps into an active take-any
// cave (= ALTTPR PreviousOverworldDoor = overworld row-index lx + 1). 0 means
// "this entrance is not an active take-any" — read by the host-room shopkeeper
// prep/dispatch to disambiguate from a normal visit to the shared host room.
// Transient (per cave visit), reset on every overworld entrance.
extern uint8 g_rando_takeany_door_id;

// Phase C Stage 2 — dungeon entrance-shuffle coupling. The overworld entry hook
// sets g_rando_entrance_exit_room (via Rando_EntranceCoupledExitRoom) when a
// shuffled dungeon door is entered; the dungeon-exit room-keyed search uses it so
// the player returns to the SOURCE door. 0 = no override (caves auto-couple).
extern uint16 g_rando_entrance_exit_room;
uint16 Rando_EntranceCoupledExitRoom(uint16 lx);
// Dungeon decoupled (Insanity): the one-way exit-search target room for door-slot
// lx, keyed on the LOADED dungeon (overlay) so the exit emerges at net'[loaded]'s
// door instead of the source. 0 when inactive / not a pooled dungeon / self-map
// (then the coupled return-to-source applies). Overrides the coupled room at entry.
uint16 Rando_EntranceDungeonDecoupledExitRoom(uint16 lx);
// Cross-category (Stage 3): set at the entry hook for a cave→dungeon redirect so
// the dungeon exit uses the cached source-cave position. Consumed at the exit.
extern uint8 g_rando_entrance_force_cached;
bool Rando_EntranceForceCachedExit(uint16 lx);

// Inverted spawn-select respawn redirect (add-rando-inverted-dark-chapel-spawn).
// Set by Module1B_SpawnSelect when an Inverted slot commits a respawn-menu choice
// ("@'s House" / "Dark Chapel" / "Dark Mountain"); consumed by the next
// LoadOverworldFromDungeon, which forces the spawn-anchor's overworld exit into
// the Dark World (screen |= 0x40). Runtime-gated rather than an asset override so
// the Sanctuary / Mountain-Cave *checks* (same rooms, but entered from the
// overworld, not the menu) still exit to the Light World unchanged. 0 = inactive.
extern uint8 g_rando_inverted_spawn_redirect;

// add-rando-inverted-dark-chapel-spawn: for an active Inverted slot, rename the
// post-Agahnim spawn-select options "Sanctuary" -> "Dark Chapel" and "The Mountain
// Cave" -> "Dark Mountain" (ALTTPR labels). Called on the FINISHED character
// buffer (after Text_LoadCharacterBuffer's vanilla decode) for dialogue 0x184 /
// 0x185 — swaps the location word's font-byte run in place. No-op for any other
// id / non-Inverted slot, so vanilla / Open / Standard / Retro are byte-identical.
void Rando_RewriteInvertedSpawnMenu(uint16 msg_id, uint8 *buf);

// Decoupled / Insanity (Stage 4, D.4) runtime hooks. SetEnteredDoor: at the
// overworld entry hook, record the entered cave door. CaptureArrival: after
// Dungeon_LoadEntrance caches *_exit, snapshot that door's overworld arrival.
// ReplaceArrival: at the cave-class exit (before LoadCachedEntranceProperties),
// swap in net[entered]'s captured arrival so Link emerges at a DIFFERENT door;
// returns false (→ coupled return) when inactive / target uncaptured.
void Rando_DecoupledSetEnteredDoor(uint16 lx);
// Read+clear the entered-interior global (consume-once at LoadOverworldFromDungeon top).
uint16 Rando_DecoupledConsumeEntered(void);
bool Rando_DecoupledReplaceArrival(uint16 entered);

// Cross + decoupled runtime: SetExit resolves the source door's one-way exit
// target (cave or dungeon) at the entry hook. ConsumeExit reads+clears the stashed
// kind (0 none / 1 cave / 2 dungeon) at the LoadOverworldFromDungeon top, writing
// the target cave interior to *out_cave. ReplayCave emerges at that cave's arrival.
void Rando_CrossDecoupledSetExit(uint16 lx);
uint8 Rando_CrossDecoupledConsumeExit(uint8 *out_cave);
bool Rando_CrossDecoupledReplayCave(uint8 target_cave);
// D.3 capture-for-bake (dev opt-in via ZELDA3_CAPTURE_ARRIVALS): snapshot each
// entered cave's overworld arrival, keyed by the entered cave interior.
// RecordEnteredDoorForCapture runs at the walk-in entry hook; RecordEnteredFallhole
// at the genuine fall-hole path (entrance-ids 0x76-0x81; also sets the decoupled
// exit key so falling in emerges decoupled). NOTE: heart_piece_cave_3 is a normal
// WALK-IN cave (dark screen 0x22), not a drop cave — it is NOT in the fall-hole table.
void Rando_RecordEnteredDoorForCapture(uint16 lx);
void Rando_RecordEnteredFallhole(void);
void Rando_CaptureArrivalForBake(void);

// If the cave at overworld row-index `lx` (door_id = lx+1) is an ACTIVE
// take-any this seed (rando active + Retro + its slot-0 LOC is in the placement
// table), return its host-room entrance (0x58/0x60/0x46). Else 0.
uint8 Rando_TakeAnyHostByDoorIndex(uint8 lx);

// For host-room presentation: the take-any LOC id for (door_id, slot pos) if it
// is active AND not yet collected (should be presented), else 0xFFFF. `room`
// (BYTE(dungeon_room_index)) is a sanity cross-check against the host room.
uint16 Rando_TakeAnyLiveSlot(uint8 room, uint8 door_id, uint8 pos);

// On taking a take-any item at (door_id, pos): grant the placed item and LOCK
// the whole cave (mark every active slot LOC checked, matching the asm
// ShopState|=$07). Returns the LttP code (caller drives Rando_ReceiveOrConfirm).
uint8 Rando_TakeAnyDispatch(uint8 room, uint8 door_id, uint8 pos,
                            uint8 vanilla_lttp_code);

// Icon kind (shop-item subtype2) for a take-any slot's placed item: 14 = heart,
// 15 = potion (generic). Both tiles are present in every host room's shop GFX.
uint8 Rando_TakeAnyDrawKind(uint8 door_id, uint8 pos);

// ---------------------------------------------------------------------------
// Rando_BumpReachabilityCounter — invalidates the tracker's memoized
// reachability cache when a story-progress event flag is written
// (tasks.md §0.4a). Called from every reachability-affecting write site
// enumerated in audit.md §0.4a.
// ---------------------------------------------------------------------------
void Rando_BumpReachabilityCounter(void);
uint32 Rando_GetReachabilityCounter(void);

// ---------------------------------------------------------------------------
// Phase B Slice 1 — tracker overlay toggle state. In-memory only, not
// persisted. Both default to false at process start AND on every
// Rando_DeactivateSlot (so launching/loading defaults to hidden).
// Keybindings: kKeys_RandoToggleItemTracker, kKeys_RandoToggleLocationTracker.
// ---------------------------------------------------------------------------
extern bool g_rando_show_item_tracker;
extern bool g_rando_show_location_tracker;

// ---------------------------------------------------------------------------
// Phase B Slice 1 — checked-location bitmap (one bit per location_id, 0..511).
// Set when Rando_OnLocationCheck fires for a location, OR when an audit-
// exempt event-flag bump site updates Rando_BumpReachabilityCounter AND has
// a corresponding LOC_*.
//
// Bitmap is heap-resident per the Phase A spec (NOT in g_ram). Loaded from
// RandoSidecarSlot.checked_bitmap on activate; written back on sidecar save.
// ---------------------------------------------------------------------------
#define kRandoCheckedBitmapBytes ((512 + 7) >> 3)  // 64 bytes = 512 bits
extern uint8 g_rando_checked_bitmap[kRandoCheckedBitmapBytes];

// Set the bit for `location_id`. No-op if loc_id >= 512 or rando not active.
void Rando_MarkLocationChecked(uint16 location_id);
// Test the bit for `location_id`. Returns false for OOB or no slot active.
bool Rando_IsLocationChecked(uint16 location_id);

// Rando Mushroom-possession state. True between obtaining the Mushroom item
// and handing it to the Witch — tracked independently of link_item_mushroom
// (which doubles as the Powder slot) so Powder-first pickups can't lock out
// the Potion Shop check. Persisted via RandoSlotHeader.mushroom_held.
extern uint8 g_rando_mushroom_held;
// True iff a rando slot is active and the player holds an undelivered Mushroom.
bool Rando_MushroomHeld(void);
// Clear the possession flag — call when the Witch accepts the Mushroom.
void Rando_DeliverMushroom(void);

// Flute/shovel decouple (old-style single inventory slot).
//
// Vanilla packs the shovel and flute into one byte, link_item_flute (0xF34C):
// 1=shovel, 2=flute (inactive), 3=flute (activated). Rando shuffles the flute
// and the shovel as INDEPENDENT items, but that byte can't represent owning
// both — so acquiring the second of the pair used to overwrite (and lose) the
// first, softlocking seeds that require both (e.g. Shovel to dig "Flute Spot"
// AND the flute to fly). We track true ownership in g_rando_flute_shovel_owned
// and treat link_item_flute purely as the currently-SELECTED function, which
// the player swaps in the item menu (see Hud_NormalMenu). Mirrors ALTTPR's
// itemdowngrade.asm / inventory.asm model. Persisted via
// RandoSlotHeader.flute_shovel_owned.
enum {
  kRandoFluteShovel_Shovel      = 0x01,  // shovel obtained
  kRandoFluteShovel_Flute       = 0x02,  // flute obtained (inactive)
  kRandoFluteShovel_FluteActive = 0x04,  // flute has been activated (selects level 3)
};
extern uint8 g_rando_flute_shovel_owned;
// Record a shovel/flute pickup: set the ownership bit and raise link_item_flute
// (the selected function) to this item's level without ever downgrading, so the
// shovel can't drop the slot below an owned flute. `lttp_code` is the vanilla
// receive code (0x13 shovel, 0x14 flute, 0x4a active flute). Call only when
// rando is active, from the receive path in misc.c.
void Rando_GrantFluteShovel(uint8 lttp_code);
// True iff a rando slot is active and the player owns BOTH a flute and the
// shovel, so the single Y-slot's function can be toggled in the item menu.
bool Rando_FluteShovelCanToggle(void);
// Effective flute/shovel level for OWNERSHIP tests (0 none, 1 shovel, 2 flute,
// 3 active flute): the highest function the player actually owns, regardless of
// which one is currently selected in link_item_flute. Use this — not the raw
// link_item_flute byte — wherever a sprite/NPC asks "does the player have the
// flute" (vs. "is the flute the selected Y item"), so toggling to the shovel
// doesn't make the player look flute-less. Returns the raw byte verbatim when
// no rando slot is active, so the vanilla side-by-side path is byte-identical.
uint8 Rando_FluteShovelEffectiveLevel(void);

// Phase B Inverted runtime — the active slot's world_state (WorldState enum),
// captured at Rando_ActivateSidecarSlot from the slot header's additive @68
// byte. Returns kWorldState_Open (0) when no slot is active or the slot
// predates the world_state ext. Used by the starting-inventory grant to
// recognize an Inverted slot on reload, where the full RandoSettings struct
// is unavailable.
uint8 Rando_GetActiveWorldState(void);

// True when the Hyrule Castle escape story sequence (uncle death / sewers /
// cell rescue / throne push) must NOT engage. Non-Standard rando seeds
// (Open / Inverted / Retro) start POST-escape: the placer pre-grants
// RescuedZelda and treats the overworld as free-roam, and the fresh-save SRAM
// is seeded post-escape (sram_progress_indicator=2, sram_progress_flags=0x14).
// But the HC interior is still physically reachable, and the vanilla escape
// story-beat sprites would re-run the sequence on entry — dropping
// sram_progress_indicator below 2 and writing an escape-only which_starting_point
// (cell=2, post-uncle sewers=3, throne=4), which both hard-traps a save-and-quit
// in the sealed escape and leaves the sprite CHR in its escape-specific
// half-refreshed state (the green-guard body-tile GFX corruption). Standard
// returns false: its escape is the real, intended start and progress climbs
// through it. Gate any HC escape story-beat trigger on this.
bool Rando_SuppressHyruleCastleEscape(void);

// Forward-declared so the prototypes below that take a RandoSidecarSlot* are
// at file scope (clang -Wvisibility errors if a struct tag is first introduced
// inside a function-parameter list). The full definition lives in rando_save.h.
struct RandoSidecarSlot;

// Copy g_rando_checked_bitmap into the supplied slot's checked_bitmap field.
// Callers about to write the ACTIVE rando slot to disk should invoke this
// just before calling Rando_WriteSidecarSlot so the in-memory checks survive
// the save/reload cycle. Safe to call when no slot is active (no-op).
void Rando_PopulateSlotBitmap(struct RandoSidecarSlot *out_slot);

// Persist the active session's checked-location bitmap to the sidecar slot
// at `slot_index`. Hook from in-game save points (SaveGameFile and its
// peers). Reads the existing sidecar slot (which has the header and
// placement table from the last write), overwrites checked_bitmap from
// the in-memory session, and writes back. No-op when no rando slot is
// active or `slot_index` is out of range.
void Rando_OnGameSave(int slot_index, const uint8 *paired_sram_slot, uint32 paired_sram_slot_size);

// ---------------------------------------------------------------------------
// §6.6 boss-kill dispatch helpers. The boss-kill code path in dungeon.c
// fires TWO grant sites per boss: the BossHeart drop (Sprite_HeartContainer)
// and the Prize crystal/pendant (RoomTag_GetHeartForPrize). Each has its
// own LOC_* per audit.md §0.3.5.
//
// `dungeon_id` is `cur_palace_index_x2 >> 1` — the GAME's dungeon index,
// NOT the ALTTPR id ordering. Range 0..13:
//   0 HCE  1 (unused)  2 EP  3 DP  4 HCT  5 PoD  6 SP  7 SW
//   8 TT   9 IP       10 TH 11 MM 12 TR  13 GT
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
// Active-slot settings recovery (format_version >= 2). On slot activation the
// canonical settings blob is deserialized and the prize/medallion shuffle
// assignments recomputed from (settings, seed). Rando_HasActiveSettings() is
// true only when that succeeded — the tracker windows gate their reachability
// display on it (false = "settings unknown", show checked/unchecked only, never
// confidently-wrong reachability). Rando_GetActiveSettings() returns the
// recovered settings, or NULL when unavailable.
// ---------------------------------------------------------------------------
bool Rando_IsActive(void);
bool Rando_HasActiveSettings(void);
const RandoSettings *Rando_GetActiveSettings(void);

// True when the active slot's spoiler-grade data (placed item names, hint text)
// must stay HIDDEN: either it is a race seed, OR the settings could not be
// recovered (snapshot-restore / legacy format_version-1 slot), in which case we
// fail CLOSED. Deriving this policy per call site previously fail-OPENED on NULL
// settings (`settings && settings->race_mode` => false => revealed), leaking a
// race seed's placements in the tracker after a snapshot replay. The tracker /
// reachability / hints panels MUST gate on this, never on the raw settings.
bool Rando_ActiveSlotHidesSpoiler(void);

// ---------------------------------------------------------------------------
// Live reachability bridge for the tracker windows. Rando_BuildRuntimeCounts
// maps the live g_ram inventory into a logical RandoCounts; Rando_GetLiveReach-
// ability computes (memoized on the reachability counter) the set of currently
// reachable locations/regions, or NULL when settings are unavailable (caller
// then shows checked/unchecked only). See rando_logic.h Reachability_Has*.
// ---------------------------------------------------------------------------
void Rando_BuildRuntimeCounts(RandoCounts *out);
const RandoReachability *Rando_GetLiveReachability(void);

// ---------------------------------------------------------------------------
// Compact item-tracker view of the live inventory, filled from g_ram by
// Rando_FillItemView. A clean data boundary so the ImGui item-tracker window
// renders from this struct instead of the full variables.h RAM-macro namespace.
// Levels: 0 means "not obtained". Bow/boomerang/mushroom-powder/flute-shovel
// use the rando-aware decoupling (shared vanilla bytes resolved here).
// ---------------------------------------------------------------------------
typedef struct RandoItemView {
  uint8 sword;        // 0..4 (0 none; 1 fighter .. 4 gold)
  uint8 shield;       // 0..3 (0 none; 1 fighter, 2 red, 3 mirror)
  uint8 mail;         // 0 green (start), 1 blue, 2 red
  uint8 gloves;       // 0 none, 1 power, 2 titan
  uint8 bow;          // 0 none, 1 wood, 2 silver
  uint8 boomerang;    // 0 none, 1 blue, 2 red
  uint8 bottles;      // 0..4
  uint8 magic;        // 0 normal, 1 half, 2 quarter
  uint8 hearts;       // heart containers
  uint8 heart_pieces; // 0..3 toward next container
  uint8 crystals;     // count obtained (0..7)
  uint8 pendants;     // count obtained (0..3)
  uint8 crystal_mask; // bit per crystal# (0..6) obtained
  uint8 pendant_mask; // bit0 green(courage actually idx), see fill code
  bool hookshot, firerod, icerod, hammer, lamp, net, book;
  bool somaria, byrna, cape, mirror, boots, flippers, moon_pearl;
  bool bombos, ether, quake;
  bool mushroom, powder, flute, shovel;
  bool agahnim;       // Agahnim 1 defeated
  // Per-dungeon items. bigkey/map/compass are bitfields with bit
  // (0x8000 >> game_index) where game_index = cur_palace_index_x2>>1. Hyrule
  // Castle's big-key/compass bit is at game_index 1 (0x4000) — when standing in
  // HC the live cur_palace_index_x2 is 2 (verified by F12 dump). Small keys are
  // a separate axis: SaveDungeonKeys folds HC (raw dungeon id 2) into key slot
  // 0, so HC's bit index (1) and key slot (0) differ. (Map_HCE, the rando map
  // GRANT, dispatches to index 0 = the sewers/escape sub-area — a separate
  // index from the HC dungeon proper; do not conflate the two.)
  uint8 dungeon_small_keys[16];
  uint16 bigkey_bits;
  uint16 map_bits;
  uint16 compass_bits;
} RandoItemView;

void Rando_FillItemView(RandoItemView *out);

// ---------------------------------------------------------------------------
// Rando_ActivateSidecarSlot / Rando_DeactivateSlot — bridge between the
// file-select slot pick and the runtime randomizer dispatcher.
//
// Activate copies the sidecar slot's placements into a session-persistent
// buffer, installs them via Placement_Install, sets g_rando_slot_active = 1,
// and ORs kFeatures1_RandomizerActive into enhanced_features1 +
// g_wanted_zelda_features1. Subsequent Rando_OnLocationCheck calls consult
// the installed placement table.
//
// Deactivate is the inverse — clears the installed placement, the slot-
// active flag, and the feature bit. Call when a vanilla slot is picked, or
// when the user returns to the title screen.
//
// Caller passes a sidecar slot in `src`; if NULL or its slot_kind is not
// kSlotKind_Randomizer, Activate falls through to Deactivate.
// ---------------------------------------------------------------------------
struct RandoSidecarSlot;
void Rando_ActivateSidecarSlot(const struct RandoSidecarSlot *src);
void Rando_DeactivateSlot(void);

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

