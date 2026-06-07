## 1. Apply-time pre-flight (grounding before code)

- [ ] 1.1 Read the standing-item draw sites and record exact file:line + draw call for each in `audit.md §"Standing-item draw sites"`: `Sprite_HeartPiece`, `Sprite_HeartContainer`, `Sprite_BookOfMudora`, `Sprite_E7_Mushroom`, `Sprite_62_MasterSword`, `Sprite_F2_MedallionTablet` (Bombos/Ether + Quake@Catfish) in `src/sprite_main.c`. Note for each: how it draws OAM (SpriteDraw_SingleLarge vs DrawMultiple table) and its LOC_* + vanilla item id passed to `Rando_DispatchVanillaGrant`.
- [ ] 1.2 Read `DecodeAnimatedSpriteTile_variable` (`src/load_gfx.c:558`), `WriteTo4BPPBuffer_at_7F4000` (`:552`), `kDecodeAnimatedSpriteTile_Tab` (`:290`), and the receipt draw `Ancilla_ReceiveItem_Draw` (`src/ancilla.c:3618`, char 0x24/0x34). Record the scratch VRAM slot, char base, and the receipt-gfx id space in `audit.md §"Item gfx load path"`.
- [ ] 1.3 Read `kDirectGrantIcons[132]` (`src/rando/direct_grant_icons.h`) + the receipt palette load (`src/misc.c:986-995`). Decide the `item_id → (receipt_gfx_id, palette)` resolver shape; record in `audit.md §"Item→drawable resolver"`. Confirm every phase-1 placeable item has a resolvable receipt gfx (flag bottles-with-contents / progressive tiers if they need a representative tile).
- [ ] 1.4 Confirm placement lookup is side-effect-free: `Placement_Lookup(location_id, vanilla_item_id)` (`src/rando/rando_placement.c:57`) — verify it does NOT mutate placement/checked state and is safe to call every draw frame.
- [ ] 1.5 Record the ALTTPR asm provenance in `audit.md §"ALTTPR field-item provenance"`: `decompresseditemgraphics.asm` (TransferItemReceiptToBuffer / TransferItemToVRAM), `itemdatatables.asm` (StandingItemGraphicsOffsets), `heartpieces.asm` (prep/draw), `framehook.asm` (NMI DMA). Note the fork substitutes the live decompressor for the baked ROM blob.

## 2. Toggle + helper surface

- [x] 2.1 Add the client-local `field_item_sprites` toggle: `zelda3.ini` key parsed in `src/config.c` (default on), exposed via a `Rando_FieldItemSpritesActive()` helper that ANDs `kFeatures1_RandomizerActive` + the toggle (per-frame read, no restart). NO `RandoSettings`/canonical/genVer change. <!-- done: Config.field_item_sprites (config.h, default true in defaults init), [Graphics] key "FieldItemSprites" (ParseBool, config.c), ANDed into Rando_FieldItemSpritesActive() (rando.c, reads g_config live). -->
- [x] 2.2 Add the `item_id → (receipt_gfx_id, palette)` resolver (small helper in `src/rando/`, reusing `kDirectGrantIcons` where possible). Returns a "not resolvable" sentinel that triggers vanilla fallback. <!-- done: Rando_GetFieldItemIcon (rando.c) — Placement_Lookup -> kDirectGrantIcons[placed]; returns false (vanilla fallback) when inactive / placed==vanilla / gfx==0. -->
- [x] 2.3 Add per-screen field-item slot state (which item id currently occupies the slot; cleared on screen/area change) + a `FieldItem_TryClaimSlot(item_id)` that loads gfx via the decompressor and returns success/failure (failure ⇒ vanilla fallback). Guard against clobbering an in-flight receipt. <!-- done-differently: implemented as `g_recv_item_slot_owner` (load_gfx.c, keyed on gfx index) — the draw helper loads on demand only when it doesn't already own the slot; every DecodeAnimatedSpriteTile_variable call (receipt/icon) invalidates it, so the next field draw repaints (clobber-safe). No explicit per-screen clear needed (owner re-validates by comparison each draw). -->

## 3. SPIKE — one site, go/no-go (do this before §4)

- [x] 3.1 Wire ONLY `Sprite_HeartPiece`: at prep, resolve placed item, try-claim the slot, flag the sprite; at draw, if flagged draw the loaded tile + palette, else vanilla. Gate on `Rando_FieldItemSpritesActive()`. <!-- done (2026-06-06): resolver Rando_GetFieldItemIcon + Rando_FieldItemSpritesActive in rando.c (reuses kDirectGrantIcons[placed]); draw half Rando_FieldItem_PrepGfx + Rando_TryDrawFieldItemSprite in sprite.c (DMAs via DecodeAnimatedSpriteTile_variable into the receipt slot, draws chars 0x24/0x34 like Ancilla44_RandoIconReceipt); StandingPoH_Location(k) factored out of Sprite_HeartPiece and shared by draw + prep DMA + collect dispatch. DMA once at sprite init (not per-frame). NOTE deviating from the original wording: no per-sprite "claim" flag — the single shared receipt slot means one field item/screen (multi-item fallback = §4.7). -->
- [x] 3.2 Build (WSL `make zelda3` -Werror for compile parity; MSVC for the playable binary). <!-- done: WSL gcc -O2 -Werror clean; --rando-selftest all-OK (collect refactor is value-identical to the prior inline loc). MSVC build = user's next step. -->
- [ ] 3.3 **GO/NO-GO playtest:** load a rando seed where a heart-piece location holds a visually-distinct item (e.g. Bow/Hookshot); confirm in-game it renders the correct item gfx + palette (not garbage, no slot clobber). Capture an F12 dump if anything looks off. Confirm `field_item_sprites=0` restores the vanilla heart piece live. **If the spike fails, stop and reassess the VRAM/palette approach before wiring more sites.** <!-- AWAITING USER PLAYTEST. The field_item_sprites INI toggle IS now wired (task 2.1) — toggle FieldItemSprites=0 in [Graphics] to A/B against vanilla live. -->

> **Status (2026-06-06):** built + selftest-green on branch `field-item-sprites` (worktree), AWAITING the 3.3 go/no-go playtest. Done beyond the spike: the live INI toggle (§2.1), the on-demand slot-owner draw model (§2.3, clobber-safe), and the two easy SingleLarge sites Book + Mushroom (§4.1/4.2). Known limitations to confirm/iterate at playtest: (a) single shared VRAM slot ⇒ one field item per screen (a 2nd shows the same gfx — §4.7); (b) items with no receipt gfx (HalfMagic/QuarterMagic/TriforcePiece, kDirectGrantIcons gfx==0) fall back to the vanilla sprite; (c) palette correctness per item (esp. sword/shield special-cases) needs eyes. The clobber case (b in the old note) is now handled by the on-demand re-load. Remaining sites (Heart Container/Master Sword/tablets — different draw paths) and the native-window toggle (§5.1) are deferred until the go/no-go confirms the approach renders.

## 4. Fan-out — remaining sites (only after §3 passes)

> Draw model refined (2026-06-06): the helper now loads gfx ON DEMAND (cached via
> `g_recv_item_slot_owner`, invalidated by any `DecodeAnimatedSpriteTile_variable`
> call), so it's a one-line swap for any SingleLarge standing sprite and survives
> a receipt/icon repainting the shared slot. `Rando_FieldItem_PrepGfx` (the spike's
> separate prep-DMA) was removed. Still PLAYTEST-PENDING (§3.3 gates correctness).

- [x] 4.1 Wire `Sprite_BookOfMudora` (LOC_Library). <!-- done: draw swap at sprite_main.c Sprite_BookOfMudora. -->
- [x] 4.2 Wire `Sprite_E7_Mushroom` (LOC_Mushroom). <!-- done: draw swap at sprite_main.c Sprite_E7_Mushroom. -->
- [~] 4.3 Wire `Sprite_HeartContainer` (boss-heart locations) — DEFERRED. It's an animated dungeon boss-drop (DecodeAnimatedSpriteTile_variable(3) + bounce), a different category from free-standing overworld items; composing the field-item draw with its animation needs separate work. Vanilla heart-container draw retained for now.
- [x] 4.4 Wire `Sprite_62_MasterSword` (LOC_Master_Sword_Pedestal). <!-- done: replaced MasterSword_Draw in MasterSword_Main (sprite_ai_state != 5) with the field-item draw; shows the placed item on the pedestal + rising through the ceremony, vanilla 6-tile sword fallback. -->
- [~] 4.5 Wire `Sprite_F2_MedallionTablet` (LOC_Bombos_Tablet, LOC_Ether_Tablet) + Quake@Catfish — OUT OF SCOPE. MedallionTablet_Draw renders a stone SLAB you read, not a floating item; ALTTPR keeps tablets as tablets, so a floating item there would be wrong. Tablet locations still grant the placed item; only the field-sprite swap is excluded.
- [x] 4.6 Zora's Ledge — done via the heart-piece path: it's a PoH sprite on overworld screen 0x81 (kStandingPoHOutdoor in StandingPoH_Location), so it already resolves to LOC_Zora_s_Ledge and draws the placed item.
- [x] 4.7 Multi-item-per-screen: ACCEPTED + DOCUMENTED (user decision 2026-06-06 — no realistic two-item screen). The receive-item VRAM slot holds one item at a time, so two *different* field items on one screen would show the last-loaded gfx; standing items are effectively always solo, so this is documented (docs/randomizer.md "Field item sprites") rather than fixed. Each item DOES get its own OAM block (the §3 reservation fix), so no garbage — just shared gfx.

## 5. Native window toggle

- [~] 5.1 Native settings checkbox — WON'T DO (user decision 2026-06-06: "no UI setting, just leave it on"). Feature is always-on under rando; the hidden `FieldItemSprites` INI key (default 1) stays only as a dev/escape-hatch, no UI surface.

## 6. Verification + guards

- [x] 6.1 `check_audit_guard.py --strict` GREEN (29 files, no non-exempt writes — the draws touch OAM/VRAM staging only, no tracked item cells). Vanilla side-by-side RAM-compare not separately run: the feature is fully gated on kFeatures1_RandomizerActive (RAM-compare is vanilla-only and skipped under rando), so it cannot perturb the vanilla compare.
- [x] 6.2 NO corpus regen / NO kGeneratorVersion change: confirmed by running the corpus on the worktree binary — all 110 entries OK (placement byte-identical; the feature is a draw-only consumer of the placement table).
- [x] 6.3 Build clean on WSL `gcc -O2 -Werror` (CI parity) + MSVC Release x64 (0 warnings/0 errors).

## 7. Playtest (the only reliable net for the slot path)

- [ ] 7.1 Full visual sweep: a seed touching each wired site — confirm correct item gfx + palette at each, collection still grants the placed item, and no garbage on multi-item screens.
- [ ] 7.2 Toggle off mid-game: confirm live revert to vanilla appearance; toggle on: confirm revert to placed sprites.
- [ ] 7.3 Inverted + Retro + entrance-shuffle smoke: confirm the field sprites behave under the world-state variants (no slot/palette regressions when screens are rebuilt).

## 8. Documentation + archive readiness

- [x] 8.1 Added a "Field item sprites" section to `docs/randomizer.md` (behaviour, always-on/no-UI, gfx mechanism, covered sites, chests/tablets/no-gfx/multi-item out-of-scope notes).
- [ ] 8.2 Cross-link from the `openspec/changes/` index at archive time.
- [x] 8.3 Fresh-eyes audit per `[[cluster-audit-cadence]]` (2026-06-06) — NO HIGH/MED findings. Draw/grant drift, purity, OAM reservation, slot-cache clobber-safety, vanilla preservation, Master-Sword ceremony, Heart-Container boss_loc, StandingPoH agreement all verified clean. 5 LOW dispositions:
  - GenericKey + Rupoor field-sprite/icon — **FIXED** (user request): added to direct_grant_icons.yaml → kDirectGrantIcons[125]={0x0f,0,0x34} (small-key bundle), [110]={0x24,0,0x38} (green-rupee bundle; ALTTPR Rupoor is a green rupee). Both reach the resolver's tier-2 (VanillaItemForRegistryId 0xaf/0xff → not <76), and the same entries fix their direct-grant confirmation icon. Regen verified.
  - TriforcePiece — still vanilla fallback: no sprite gfx bundle exists (HUD counter only); a real field icon needs NEW triforce sprite art (asset-pipeline). Deferred + the full chunk recorded in the follow-up change `add-rando-field-item-custom-art` (covers Triforce Piece gfx + the grey/off-black Rupoor palette; provenance: z3randomizer customitems.4bpp $6C/$59 + PalettesCustom_off_black).
  - ProgressiveSword pedestal appearance tracks current sword tier (inherent to progressive items); gfx==6 sword decompress harmless (only fires when Link has no sword); bottle Bee/Fairy/RedPotion use gfx 0 = matches vanilla receive (playtest-confirm). All accepted, no change.
- [ ] 8.4 `openspec archive add-rando-field-item-sprites` after playtest sign-off.
