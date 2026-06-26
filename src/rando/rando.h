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
#define kGeneratorVersion 97u
// The share-string binary layout packs version into 1 byte
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
// Called from every registered vanilla grant site with:
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

// Runtime-only message id for trap pickups. messaging.c asks the randomizer to
// render this id before falling back to the vanilla dialogue table, so no asset
// text row is required.
#define kRandoTrapDialogueId 0x0220u
bool Rando_RenderTrapMessage(uint16 msg_id, uint8 *out_buffer);

// Returns the item id resolved by the most recent Rando_DispatchVanillaGrant
// (or Rando_ChestDispatch) call. Used by direct-grant
// confirmation sites to feed the per-item icon lookup. Returns 0xFFFF when
// no dispatch has run yet this slot.
uint16 Rando_LastDispatchedItemId(void);

// ---------------------------------------------------------------------------
// Race-mode reveal action.
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

// True when an in-binary reveal of the active slot's spoiler is allowed right
// now: a race-mode slot is active AND the anti-cheat completion gate inside
// Rando_RevealActiveSlotSpoiler() would currently pass (seed completed). UI uses
// this to enable the Reveal button with a clear "after you finish the seed"
// state instead of surfacing a confusing FileNotFound when the gate refuses
// pre-completion. (Tournament admins bypass the gate via --reveal-spoiler.)
bool Rando_CanRevealActiveSlotSpoiler(void);

// Per-frame tick (main loop) that latches "this slot's seed was beaten" so the
// reveal survives leaving the credits screen. See rando.c.
void Rando_NoteFrameForReveal(void);

// Per-frame trap effect tick. Runs before Module_MainRouting so trap freeze can
// neutralize movement/input before the active player handler consumes them.
void Rando_TickTrapEffects(void);
// add-rando-trap-catalog — placement-time location-compatibility flags passed to
// Rando_PickTrapEffectId so context-locked effects (Cucco needs the overworld,
// Darkness needs a dungeon) are only ever placed where they will actually fire.
// Derived from committed location data (region dungeon_id + location type).
enum {
  kTrapLoc_IsDungeon = 1u << 0,  // the location's region is a dungeon
  kTrapLoc_IsOutdoor = 1u << 1,  // the location is an overworld free-standing type
};
// Placement-time trap effect selector (defined in rando.c, called from
// rando_placement.c). Deterministic in (seed, location_id) and the enabled-category
// mask; returns an ITEM_Trap* id compatible with loc_flags.
uint16 Rando_PickTrapEffectId(uint64 seed, uint16 location_id, uint8 categories,
                              uint8 loc_flags);

// Returns true if `lttp_code` is the §6.2 "skip Link_ReceiveItem" sentinel.
// Enabled for HalfMagic/QuarterMagic/TriforcePiece/prize-bit items, which
// dispatch via direct writes inside Rando_DispatchVanillaGrant.
static inline int Rando_ShouldSkipReceive(uint8 lttp_code) {
  return lttp_code == kRandoLttpSkip;
}

// ---------------------------------------------------------------------------
// Rando_ShowDirectGrantConfirmation — generic visual+audio confirmation
// for direct-grant placements.
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
// counter) updates immediately. Trap items get a deterministic good-item
// decoy icon based on the active seed/location/trap type; otherwise, when
// `item_id` maps to a non-zero gfx bundle in `kDirectGrantIcons[]`
// (codegen'd from `assets/rando/direct_grant_icons.yaml`), this additionally
// spawns the `kAncillaType_RandoIconReceipt` ancilla so the player sees what
// they got. Audio-only (gfx==0) entries fall back to the audio+HUD path.
// ---------------------------------------------------------------------------
void Rando_ShowDirectGrantConfirmation(uint8 item_id);

// ---------------------------------------------------------------------------
// Rando_ReceiveOrConfirm — convenience wrapper that combines the standard
// NPC grant pattern into a single call.
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
// confirmation cue (sound + HUD refresh + icon ancilla when `item_id` is a
// trap or has a verified entry in `kDirectGrantIcons[]`). Otherwise invokes
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
// add-rando-pot-sanity Phase 4 — runtime pot grant hook + recolor (design D3/D4).
//
// Rando_PotBreakHook runs at the TOP of RevealPotItem (dungeon.c) for all three
// callers (lift / ThievesAttic-hole / sword-break). On an active + un-checked pot
// it grants the placed item here (+ fires the receive/confirm cue); on a CHECKED
// key/empty pot it signals suppression (no duplicate-key re-drop). The return
// tells RevealPotItem whether to suppress its vanilla secret. Rando_PotShouldRecolor
// gates the un-checked-pot palette swap in RoomDraw_SinglePot. Both are inert
// (Vanilla / false) when rando is off or (room,pos4) is not a registered,
// tier-active pot — so the non-rando draw/grant path stays byte-identical.
// ---------------------------------------------------------------------------
enum {
  kRandoPot_Vanilla = 0,   // let RevealPotItem run its normal vanilla path
  kRandoPot_Suppress = 1,  // hook handled it; RevealPotItem must spawn no secret
};
// NOTE: the first param is named `room` (not `dungeon_room_index`) — the latter
// is a variables.h g_ram-accessor MACRO and can't be a parameter name. Callers
// pass the global `dungeon_room_index` value as the argument.
uint8 Rando_PotBreakHook(uint16 room, uint16 pos4);
bool Rando_PotShouldRecolor(uint16 room, uint16 pos4);
// Exposed for the --rando-selftest (room,pos4) -> LOC round-trip assertion.
uint16 Rando_GetPotLocation(uint16 room, uint16 pos4);

// ---------------------------------------------------------------------------
// add-rando-pot-sanity — animated gold "check" overlay for in-scope un-checked
// pots (supersedes the interim BG palette swap; design D4/D12).
//
// Why a sprite overlay and not a BG recolor: a pure-BG gold can't be BOTH
// consistent AND scoped to specific pots. Dungeon CGRAM has no free BG
// sub-palette row (rows 0-1 = HUD, 2-7 = the dungeon set) and the cgram is
// shared across BG layers, so forcing a row to a fixed gold would also recolor
// every other room tile on that row, while the pot's own 16x16 tile carries
// floor pixels in its corners (the interim swap tinted those too). So we draw a
// sprite-layer gold glint over each pot instead: its own injected gold palette
// (theme-independent), the floor is untouched, and it animates. Every entry
// point is inert off-rando / outside the dungeon module — the non-rando draw
// path stays byte-identical.
//
// Lifecycle: Dungeon_LoadRoom resets the list; RoomDraw_SinglePot captures each
// in-scope un-checked pot's tilemap pos; Module07_Dungeon draws the glints right
// after Sprite_Main; NMI golds the chosen sprite sub-palette row.
//
// Palette collision avoidance: ALL 8 sprite sub-palette rows are allocated in a
// dungeon (enemies = SP1-4, Link/armor/sword/shield = SP7, environment/aux =
// SP0/5/6), so no row is permanently free. The draw loop therefore picks, PER
// FRAME, a row that no on-screen sprite is using (and never Link's row 7) and
// golds only that one; NMI also restores the PREVIOUS frame's row from the
// vanilla buffer, so the instant a glint leaves or a sprite claims the row it
// reverts — no persistence, and Link/enemies are never tinted. (An earlier fixed
// row 7 clobbered Link's palette — F12-confirmed.)
void Rando_PotOverlayReset(void);
void Rando_PotOverlayCapture(uint16 room, uint16 pos4);
uint8 Rando_PotOverlayCount(void);
uint16 Rando_PotOverlayPos(uint8 i);
// Set true by the dungeon draw loop when >=1 glint was emitted this frame; gates
// the NMI gold injection. Cleared by the draw loop.
extern bool g_rando_pot_overlay_drawn;
// The sprite sub-palette row (0..7) the draw loop reserved for the glints this
// frame (no on-screen sprite uses it; never 7). 0xFF = no free row → glint skipped.
extern uint8 g_rando_pot_overlay_palette_row;
// In NMI: restore last frame's goldened row from main_palette_buffer, then (if a
// glint is on-screen) write the animated gold ramp into this frame's reserved
// row. PPU CGRAM copy only — g_ram stays vanilla (cosmetic-palette-mode style).
void Rando_PotOverlayApplyCgram(uint16 *cgram);

// ---------------------------------------------------------------------------
// Field item sprites — draw the PLACED item's
// graphics at free-standing item locations instead of the vanilla sprite.
//
// The gfx come from the existing per-item receipt decompressor
// (DecodeAnimatedSpriteTile_variable) writing the shared receive-item VRAM slot
// (chars 0x24/0x34); the item->gfx mapping reuses kDirectGrantIcons[]. The draw
// helper loads the slot on demand (cached via g_recv_item_slot_owner). Because
// that slot holds ONE item at a time, only one field item per screen renders its
// real gfx — a second standing item on the same screen shows this one's
// (documented limitation; phase 2 = a dedicated slot).
//
// Split across TUs: the resolver (placement + icon table) lives in rando.c; the
// gfx-load + OAM draw live in sprite.c (the OAM/gfx primitives are there).
// ---------------------------------------------------------------------------

// True when field-item sprites should be drawn: active rando slot AND the
// client-local field_item_sprites toggle (read live). Inert in non-rando play.
bool Rando_FieldItemSpritesActive(void);

// Resolve a free-standing location to a drawable placed-item icon. Returns
// false (draw the vanilla sprite) when field-item sprites are inactive, the
// placement equals the vanilla item, or the placed item has no receive gfx
// (the audio-only sentinel). On true, fills the kDirectGrantIcons fields.
bool Rando_GetFieldItemIcon(uint16 location_id, uint16 vanilla_item_id,
                            uint8 *out_gfx, uint8 *out_big, uint8 *out_oam_flags);

// DRAW (call each frame in place of the vanilla sprite draw): draw the placed
// item's icon (chars 0x24/0x34) at sprite `k`'s position, loading its gfx into
// the shared slot on demand. Returns true when it drew (caller skips the vanilla
// draw); false when the vanilla sprite should be drawn instead.
bool Rando_TryDrawFieldItemSprite(int k, uint16 location_id, uint16 vanilla_item_id);

// As above, but draw the icon at a fixed screen offset (dx,dy) from sprite `k` —
// for an NPC whose vanilla pose holds an item out (the Hobo's bottle). The caller
// still draws the NPC's body; this only adds the held icon.
bool Rando_TryDrawHeldItemSprite(int k, uint16 location_id, uint16 vanilla_item_id,
                                 int dx, int dy);

// Draw a trap-cucco (sprite 0x0B with the trap signal byte set) as a single 16x16
// large OAM entry from the shared recv-item slot with the custom cucco tile +
// palette, so the flock renders in ANY area. Mirrors the vanilla large draw
// (Sprite_PrepAndDrawSingleLargeNoPrep) but with a fixed charnum/palette. Vanilla
// and home-area cuccos keep the sheet-3 draw. Implemented in sprite.c.
void Rando_DrawTrapCucco(int k);

// Load `gfx` into the shared receive-item VRAM slot (chars 0x24/0x34) unless it
// already holds it (g_recv_item_slot_owner cache; handles the sword/shield
// decompress side-loads). For draw sites that render the slot themselves —
// e.g. the Flute Spot dig ancilla. Implemented in sprite.c.
void Rando_EnsureRecvItemSlotGfx(uint8 gfx);

// ---------------------------------------------------------------------------
// Custom item art — gfx ids with the 0x80
// bit set are NOT vanilla DecodeAnimatedSpriteTile_variable bundle indices
// (vanilla tops out around 0x4B); they select custom art + palette for
// the ALTTPR items that have no vanilla receive bundle. The tile comes from
// the kRandoCustomItemGfx asset (or a re-coloured vanilla bundle) and an
// 8-colour palette is loaded into sprite palette 3's upper half (CGRAM words
// 0xB8..0xBF) — byte-for-byte the slot ALTTPR's LoadItemPalette uses — so the
// colour is stable regardless of the area's sprite palettes. Entries draw
// with OAM palette 3 (oam_flags 0x36). Routed exclusively through
// Rando_EnsureRecvItemSlotGfx; never pass these ids to
// DecodeAnimatedSpriteTile_variable (it asserts).
// ---------------------------------------------------------------------------
// Blob-backed ids are CONTIGUOUS from 0x80 (id 0x80+N = kRandoCustomItemGfx
// entry N — keep in lockstep with assets/rando/custom_item_gfx.png via
// assets/scripts/gen_custom_item_gfx_png.py); ids that re-colour a vanilla
// bundle instead of using the blob (Rupoor) come AFTER all blob entries.
enum {
  // kRandoCustomItemGfx entry 0 (16x16 triforce) + green_blue_guard upper-half
  // palette (kPalette_MainSpr[52..59] — what ALTTPR's
  // PalettesVanilla_green_blue_guard+$0E points at).
  kRandoCustomGfx_TriforcePiece = 0x80,
  // Entries 1/2: the 1/2 and 1/4 Magic decanters (16x16, same green_blue_guard
  // palette — z3r SpriteProps $4E/$4F).
  kRandoCustomGfx_HalfMagic = 0x81,
  kRandoCustomGfx_QuarterMagic = 0x82,
  // Vanilla green-rupee tile (bundle 0x24) + ALTTPR's off_black palette: the
  // Rupoor has NO custom tile upstream either (z3r itemdatatables.asm $59).
  kRandoCustomGfx_Rupoor = 0x83,
  // Cucco trap avenger (add-rando-trap-catalog): a dispatch id, NOT a blob cell —
  // the real cucco tile + palette load at runtime from VANILLA sheet 80 (the cucco
  // ALSO uses there) in the cucco branches of Rando_EnsureRecvItemSlotGfx /
  // Rando_ApplyCustomItemGfxPalette, drawn by Rando_DrawTrapCucco. So the flock
  // renders in EVERY area, not just the two that load that sheet into sprite slot 3.
  kRandoCustomGfx_Cucco = 0x84,
  kRandoCustomGfx_BlobEntries = 3,  // kRandoCustomItemGfx cell count (idx 0..2)
};

// (Re)load the custom item's 8-colour palette into SP3's upper half if it is
// not already there (compares the AUX buffer so it doesn't fight a palette
// fade). Called per draw — area/room transitions reload SP1-4 over the slot
// and a sprite that stays on screen must repaint it. Implemented in sprite.c.
void Rando_ApplyCustomItemGfxPalette(uint8 gfx);

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
// Retro TakeAny caves.
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

// Dungeon entrance-shuffle coupling. The overworld entry hook
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
// Source cave's room for the force-cached exit, set together with the flag by
// Rando_EntranceForceCachedExit; consumed at the exit to restore the Y-adjust.
extern uint16 g_rando_force_cached_room;
bool Rando_EntranceForceCachedExit(uint16 lx);

// Inverted spawn-select respawn redirect.
// Set by Module1B_SpawnSelect when an Inverted slot commits a respawn-menu choice
// ("@'s House" / "Dark Chapel" / "Dark Mountain"); consumed by the next
// LoadOverworldFromDungeon, which forces the spawn-anchor's overworld exit into
// the Dark World (screen |= 0x40). Runtime-gated rather than an asset override so
// the Sanctuary / Mountain-Cave *checks* (same rooms, but entered from the
// overworld, not the menu) still exit to the Light World unchanged. 0 = inactive.
extern uint8 g_rando_inverted_spawn_redirect;

// For an active Inverted slot, rename the
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
// (tasks.md §0.4a). Called from every reachability-affecting write site.
// ---------------------------------------------------------------------------
void Rando_BumpReachabilityCounter(void);
uint32 Rando_GetReachabilityCounter(void);

// ---------------------------------------------------------------------------
// Tracker overlay toggle state. In-memory only, not
// persisted. Both default to false at process start AND on every
// Rando_DeactivateSlot (so launching/loading defaults to hidden).
// Keybindings: kKeys_RandoToggleItemTracker, kKeys_RandoToggleLocationTracker.
// ---------------------------------------------------------------------------
extern bool g_rando_show_item_tracker;
extern bool g_rando_show_location_tracker;

// ---------------------------------------------------------------------------
// Checked-location bitmap (one bit per location_id, 0..kRandoLocationCapacity-1).
// Set when Rando_OnLocationCheck fires for a location, OR when a reachability
// event-flag bump site updates Rando_BumpReachabilityCounter AND has a
// corresponding LOC_*.
//
// Bitmap is heap-resident (NOT in g_ram). Loaded from
// RandoSidecarSlot.checked_bitmap on activate; written back on sidecar save.
// Sized by kRandoLocationCapacity (rando_logic.h) so it tracks the registry.
// ---------------------------------------------------------------------------
#define kRandoCheckedBitmapBytes ((kRandoLocationCapacity + 7) >> 3)
extern uint8 g_rando_checked_bitmap[kRandoCheckedBitmapBytes];

// Set the bit for `location_id`. No-op if loc_id >= kRandoLocationCapacity or
// rando not active.
void Rando_MarkLocationChecked(uint16 location_id);
// Test the bit for `location_id`. Returns false for OOB or no slot active.
bool Rando_IsLocationChecked(uint16 location_id);

// Rando Mushroom/Powder decouple. The two share link_item_mushroom (0xF344):
// 1=mushroom, 2=powder. Vanilla never holds both (the Witch trade consumes the
// Mushroom to make Powder), but rando shuffles them as INDEPENDENT items, so we
// track the decoupled ownership here:
//   * bit 0 (Held)        — an undelivered Mushroom is in inventory. The Witch
//     trade keys off THIS, not the byte, so a Powder-first pickup can't lock out
//     the Potion Shop check (the byte may show Powder=2 the whole time).
//   * bit 1 (PowderOwned) — Magic Powder has been obtained. Lets the item-menu
//     swap show the Mushroom icon (byte=1) without logic/tracking reading the
//     player as having lost Powder.
// Persisted via RandoSlotHeader.mushroom_held.
enum {
  kRandoMushroom_Held        = 0x01,  // undelivered Mushroom in inventory
  kRandoMushroom_PowderOwned = 0x02,  // Magic Powder obtained
};
extern uint8 g_rando_mushroom_held;
// True iff a rando slot is active and the player holds an undelivered Mushroom.
bool Rando_MushroomHeld(void);
// Clear the Held bit — call when the Witch accepts the Mushroom. Leaves the
// PowderOwned bit intact (delivering the Mushroom doesn't surrender Powder).
void Rando_DeliverMushroom(void);
// True iff the player has Magic Powder regardless of which icon the shared slot
// currently shows: Powder in the byte (==2) counts in vanilla and rando; under
// an active rando slot the PowderOwned bit also counts, so a player who swapped
// the slot to the Mushroom icon still reads as Powder-capable. Single source of
// truth for "owns Powder"; mirrors Rando_MushroomHeld for the other tier. In
// vanilla this is exactly `link_item_mushroom == 2`.
bool Rando_PowderOwned(void);
// True iff a rando slot is active and the player owns BOTH an undelivered
// Mushroom and Powder, so the shared Y-slot icon can be toggled in the item
// menu. Cosmetic: the Witch trade and Powder use both already work off the
// decoupled state regardless of which icon is currently selected.
bool Rando_MushroomPowderCanToggle(void);

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
  kRandoFluteShovel_Flute       = 0x02,  // flute obtained
  kRandoFluteShovel_FluteActive = 0x04,  // flute can summon the bird (selects level 3)
};
extern uint8 g_rando_flute_shovel_owned;
// Record a shovel/flute pickup: set the ownership bit and raise link_item_flute
// (the selected function) to this item's level without ever downgrading, so the
// shovel can't drop the slot below an owned flute. `lttp_code` is the vanilla
// receive code (0x13 shovel, 0x14 flute, 0x4a active flute). When the active
// seed's instant_flute setting is on, 0x14 is promoted to active immediately.
// Call only when rando is active, from the receive path in misc.c.
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

// Boomerang / bow decouple + item-menu swap.
//
// Like the flute/shovel pair, these are independent shuffled items that vanilla
// packs into a single Y-slot byte:
//   * Boomerang (link_item_boomerang, 0xF341): 1 = blue, 2 = magical/red.
//   * Bow       (link_item_bow,       0xF340): 1/2 = wood (no-arrows/arrows),
//                                              3/4 = silver (no-arrows/arrows).
// Both are tracked with persistent ownership bits, but they grant differently:
//   * Boomerang is STRICTLY PROGRESSIVE — the 1st collected is always blue, the
//     2nd always red, regardless of which item is placed there (item color is
//     ignored). Safe because no logic predicate requires a boomerang.
//   * Bow is NEVER-DOWNGRADE — each item grants its own tier (wood item → wood,
//     silver arrows → silver) and a lower pickup can never lower the slot. (Bow
//     IS distinguished by logic — silver vs wood — so its item identity is kept.)
// In both cases the byte tracks the currently-SELECTED tier; once the player
// owns BOTH tiers, pressing A on the highlighted slot (Hud_NormalMenu) swaps
// which tier the slot performs — mirroring ALTTPR's item-menu swap (and the
// existing flute/shovel toggle). Ownership is persisted via
// RandoSlotHeader.boomerang_owned / .bow_owned so a swapped-down byte can't lose
// the higher tier across save/reload.
enum {
  kRandoBoomerang_Blue = 0x01,  // blue boomerang obtained
  kRandoBoomerang_Red  = 0x02,  // magical (red) boomerang obtained
};
extern uint8 g_rando_boomerang_owned;
enum {
  kRandoBow_Wood   = 0x01,  // a bow obtained (wood-arrow capability)
  kRandoBow_Silver = 0x02,  // silver arrows obtained (implies a bow)
};
extern uint8 g_rando_bow_owned;
// Record a boomerang pickup (progressive: advances to the next unowned tier —
// 1st collected = blue, 2nd = red — regardless of which boomerang item it is).
// Call only when rando is active, from the receive path in misc.c.
void Rando_GrantBoomerang(void);
// Record a bow pickup (lttp_code: 0x0b/0x3a = wood bow, 0x3b = silver arrows):
// set the ownership bit(s) and raise link_item_bow's strength tier without
// downgrading, preserving the current arrow bit. Call only when rando is active.
void Rando_GrantBow(uint8 lttp_code);
// True iff a rando slot is active and the player owns BOTH boomerang colors, so
// the Y-slot can be toggled between blue and red in the item menu.
bool Rando_BoomerangCanToggle(void);
// True iff a rando slot is active and the player owns silver arrows (which imply
// a bow), so the Y-slot can be toggled between wood and silver arrows.
bool Rando_BowCanToggle(void);

// Inverted runtime — the active slot's world_state (WorldState enum),
// captured at Rando_ActivateSidecarSlot from the slot header's additive @68
// byte. Returns kWorldState_Open (0) when no slot is active or the slot
// predates the world_state ext. Used by the starting-inventory grant to
// recognize an Inverted slot on reload, where the full RandoSettings struct
// is unavailable.
uint8 Rando_GetActiveWorldState(void);

// True iff a rando slot is active AND its world-state is Retro. This is the
// canonical RUNTIME gate for the four Retro gameplay flags (rupeeBow /
// genericKeys / takeAnys / wildKeys). Per design.md §8 Risk 8 the flags are
// NOT serialized bits — they are *computed* from `world_state == Retro` at the
// point of use, so no new bytes enter the settings struct and the canonical
// length stays 28. A gameplay site that must diverge under Retro wraps its
// divergence in this test; when it returns false the vanilla code path runs
// byte-identically (rando inactive OR non-Retro world-state). Mirrors the
// inline gate the TakeAny runtime already uses (Rando_TakeAnyHostByDoorIndex).
bool Rando_IsRetroActive(void);

// True iff ALTTPR's `rom.genericKeys` is in effect for the active slot — i.e. a
// rando slot is active AND its world-state is Retro (Retro pins genericKeys on,
// per app/World/Retro.php). Under genericKeys small keys form one shared pool:
// any key opens any locked door. The RUNTIME small-key sites (enter-load,
// exit-save, door-consume, key grants in dungeon.c / sprite*.c / this file) gate
// on this to route through the shared counter `link_generic_keys` instead of the
// per-dungeon `link_keys_earned_per_dungeon[]` cells. Equal to Rando_IsRetroActive
// today; kept as a distinct name so a future standalone genericKeys axis can
// diverge, and so the runtime intent reads clearly at each site. When it returns
// false the vanilla per-dungeon key path runs byte-identically.
bool Rando_IsGenericKeysActive(void);

// Rando_GrantGenericKeyPurchase — grant one shared generic small key, the way an
// in-world GenericKey pickup would (bumps the persisted shared counter, and the
// live link_num_keys when standing in a dungeon). Called by the Retro genericKeys
// BUYABLE shop slot (ShopItem_GenericKey, src/sprite_main.c) on each purchase to
// provide ALTTPR's unlimited ShopKey supply, so the predicate VM's >=1 small-key
// wildcard is sound against the finite placed-key pool. The shop handler owns the
// rupee cost and purchase feedback; this only does the grant.
void Rando_GrantGenericKeyPurchase(void);

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
// own LOC_*.
//
// `dungeon_id` is `cur_palace_index_x2 >> 1` — the GAME's dungeon index,
// NOT the ALTTPR id ordering. Range 0..13:
//   0 HCE  1 HC        2 EP  3 DP  4 HCT  5 SP   6 PoD 7 MM
//   8 SW   9 IP       10 TH 11 TT 12 TR  13 GT
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
// false). Non-NULL inputs are copied into owned storage, so callers may pass
// stack or temporary assignment buffers.
// ---------------------------------------------------------------------------
void Rando_SetDungeonPrizeAssignment(const uint8 *assignment);    // [kRandoDungeonCount]
void Rando_SetMedallionAssignment(const uint8 *assignment);       // [kRandoMedallionEntranceCount]
const uint8 *Rando_GetDungeonPrizeAssignment(void);
const uint8 *Rando_GetMedallionAssignment(void);

// Boss-shuffle assignment for the LOGIC VM (OP_CAN_KILL_BOSS). [16];
// entry = boss-pool index (kBoss_* in shuffle_boss.c) for that dungeon's boss
// room. Installed by the placer (from the base seed, matching the runtime
// install) so reachability gates each `- Boss`/`- Prize` on the SHUFFLED boss's
// kill predicate. This is INDEPENDENT of the sprite-substitution activation in
// shuffle_boss.c (g_boss_assignment_active): the logic table can be installed
// without the runtime sprite swap. NULL ⇒ OP_CAN_KILL_BOSS falls back to the
// vanilla boss (kRandoDungeonVanillaBoss). Non-NULL inputs are copied into
// owned storage. Pass NULL to clear.
void Rando_SetBossAssignment(const uint8 *assignment);           // [16]
const uint8 *Rando_GetBossAssignment(void);

// Runtime medallion-open gate for the Misery Mire (entrance_index 0) and Turtle
// Rock (entrance_index 1) overworld doors. `cast_medallion` is the item-registry
// id of the medallion the player just cast (Bombos=25, Ether=26, Quake=27).
// Returns true iff that medallion matches the shuffled assignment for the
// entrance. False when no assignment is installed (v1 / snapshot-restored slot)
// or entrance_index is out of range — callers fall back to the vanilla mapping.
bool Rando_MedallionOpens(uint8 cast_medallion, uint8 entrance_index);

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

// Regenerate the hint table for the CURRENTLY-ACTIVE slot, replaying exactly
// the activation-time hint block — including the v1/no-blob fallbacks
// (header-ext hints_setting/goal, default hints-on for the oldest slots),
// which Rando_GetActiveSettings() cannot express (it returns NULL there even
// though activation regenerated hints). Call after any out-of-band
// Rando_GenerateHints clobber (e.g. the native window's spoiler export) to
// restore the active seed's in-game telepathic-tile / fortune-teller hints.
// Clears the table (vanilla text) when no slot is active.
void Rando_RegenerateActiveSlotHints(void);

// Sibling of the above — reinstall the LOGIC-side overlays
// (entrance region/edge overrides + door logic layout) AND the logic-VM
// shuffle-assignment stores (prize/medallion/boss — the
// placer replaces them with per-run assignment bytes on every
// Place_AssumedFill, and
// they are not tracker-only: dungeon.c's falling-prize pick and ancilla.c's
// MM/TR medallion gates read them) for the CURRENTLY-ACTIVE slot, replaying
// the activation-time install sub-steps from the state captured at
// activation. Call after any out-of-band generation (Rando_GenerateSlot
// clears/replaces these global stores and never restores) so tracker/map
// LOGIC reachability and the prize/medallion gameplay gates don't follow the
// unloaded seed until slot reload. Gameplay-side installs (the asset-126
// entrance overlay, DoorRt runtime redirects, decoupled nets, boss/drop/enemy
// RENDER shuffles — generation only uses the pure *_ComputeAssignment forms)
// are untouched by generation and by this helper. No active slot: leaves the
// stores cleared and the assignments NULL (fail-closed, the correct idle
// state — mirrors activation's !settings_valid arm).
void Rando_ReinstallActiveSlotLogicOverlays(void);

// True when the active slot's spoiler-grade data (placed item names, hint text)
// must stay HIDDEN: either it is a race seed, OR the settings could not be
// recovered (snapshot-restore / legacy format_version-1 slot), in which case we
// fail CLOSED. Deriving this policy per call site previously fail-OPENED on NULL
// settings (`settings && settings->race_mode` => false => revealed), leaking a
// race seed's placements in the tracker after a snapshot replay. The tracker /
// reachability / hints panels MUST gate on this, never on the raw settings.
bool Rando_ActiveSlotHidesSpoiler(void);

// True when the active slot is swordless (mode.weapons=swordless). Gates the
// runtime swordless patches; false (vanilla behavior) when not swordless or when
// settings are unknown. See Rando_IsSwordlessActive in rando.c.
bool Rando_IsSwordlessActive(void);

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

// Snapshot cold-replay restore. Reconstructs the active-slot logic-side
// state (prize/medallion/boss/drop/enemy assignments + Inverted installs +
// JP-glitch coupling) from a type-2 RandoSettings snapshot TLV, given the
// recovered canonical settings, the raw 32-byte share string (→ base seed) and
// the accepted prize_attempt. GATED: a no-op when a slot is already validly
// active (within-session replay); fires only on a genuine cold replay (fresh
// launch, slot never loaded). Called by RandoSnapshotTail_Load.
void Rando_SnapshotColdReplayRestore(const RandoSettings *s,
                                     const uint8 *share_string_raw,
                                     uint8 prize_attempt);

// ---------------------------------------------------------------------------
// Rando_DrawHashIcons (tasks.md §9.4b — 5-icon visual hash widget).
//
// Renders a 5-tile horizontal strip starting at (x, y), one tile per index
// from kHashIconAtlas. The tile indices are derived from
// `SHA-256(share_string_binary)[0..4] mod kHashIconAtlasSize` per the
// randomizer-ui spec. Critical: the hash input is the FULL share-string
// binary (31 bytes: magic + version + settings_hash + seed_u64 + checksum),
// NOT settings_hash alone — otherwise every seed with identical settings
// would render identical icons.
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

