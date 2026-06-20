## Context

The randomizer hooks the **grant** side of every collectible (the sprite handler calls `Rando_DispatchVanillaGrant(LOC_*, vanilla_item, lttp_code)`; `Placement_Lookup(location_id, vanilla_item_id)` at `src/rando/rando_placement.c:57` returns the placed item id, side-effect-free). The **draw** side is untouched: standing-item handlers in `src/sprite_main.c` draw their hardcoded vanilla graphics (e.g. `Sprite_HeartPiece` → `SpriteDraw_SingleLarge`, `Sprite_BookOfMudora` → `SpriteDraw_SingleLarge`). So the field always looks vanilla regardless of placement.

**The load-bearing constraint is GFX/VRAM.** To draw item X at a location, X's 4bpp tiles must be resident in VRAM for that screen. The engine already loads *arbitrary* item gfx on demand for two existing features, which is the architecture we build on:

1. **Item-receipt held-aloft animation.** `AncillaAdd_ItemReceipt` (`src/misc.c:713-844`) calls `DecodeAnimatedSpriteTile_variable(gfx)` (`src/load_gfx.c:558`), which decompresses the item's gfx and `WriteTo4BPPBuffer_at_7F4000` writes it to a scratch VRAM slot (VRAM `0x9000 + 0x2d40`, `src/load_gfx.c:552-555`). `Ancilla_ReceiveItem_Draw` (`src/ancilla.c:3618`) draws it from fixed char `0x24`/`0x34`. This is single-item, transient (received → animate → freed).
2. **Direct-grant confirmation icon.** `kDirectGrantIcons[132]` (`src/rando/direct_grant_icons.h`, codegen'd from `assets/rando/direct_grant_icons.yaml`) maps `item_id → (gfx_bundle_id, OAM size, palette flags)`; the `kAncillaType_RandoIconReceipt` ancilla renders an arbitrary placed item on screen. Proof the engine can already draw any placed item.

**ALTTPR's runtime** (the `z3randomizer` asm checkout) solves the standing-item case with a contained subsystem: a `StandingItemGraphicsOffsets` table distinct from `ItemReceiptGraphicsOffsets` (`itemdatatables.asm`), `TransferItemReceiptToBuffer_using_ReceiptID` + an NMI DMA `TransferItemToVRAM` into a **single per-screen dynamic-tile slot** (`decompresseditemgraphics.asm`, `framehook.asm`), and standing-item prep/draw hooks (`heartpieces.asm` -> `PrepDynamicTile_loot_resolved` / `DrawDynamicTile`). It works because standing items are essentially **solo per screen**. The fork lacks ALTTPR's baked `ItemReceiptGraphicsROM` blob; our substitute is the on-demand `DecodeAnimatedSpriteTile_variable` decompressor (so no new asset is needed).

## Goals / Non-Goals

**Goals:**
- Under an active rando slot, a free-standing item draws the **placed** item's sprite + palette, not the vanilla sprite.
- Reuse the existing on-demand gfx-load path; add **no new asset**.
- Zero impact on placement/logic/determinism/save: purely visual, gated on `kFeatures1_RandomizerActive`, client-local toggle.
- Vanilla path byte-identical when rando is inactive (the fork RAM-compares against the original ROM).
- Never render garbage — multi-item-per-screen or any unresolved gfx falls back to the vanilla sprite.

**Non-Goals:**
- **Chests** — out of scope (ALTTPR keeps chests closed).
- Items inside the inventory/HUD or shop slots (shops already draw their item via `SpriteDraw_ShopItem`).
- Animated/multi-frame item sprites beyond what the receipt decompressor produces (start with the static receipt tile).
- A canonical settings axis or any share-string/genVer change.
- Porting ALTTPR's baked-ROM gfx pipeline (we use the live decompressor instead).

## Decisions

### D1 — Build on `DecodeAnimatedSpriteTile_variable`, not a new gfx pipeline
Resolve the placed item, map it to the receipt-gfx id the decompressor already understands, and load it into a **dedicated field-item VRAM slot** (a second scratch slot, or a guarded re-use of the receipt slot when no receipt is in flight). *Alternative considered:* port ALTTPR's baked `ItemReceiptGraphicsROM` + dual offset tables → rejected (new ~8KB asset, asset-pipeline both-ends work, no upside over the live decompressor).

### D2 — Per-screen single slot + vanilla fallback for the rare multi-item case
Mirror ALTTPR: one field-item gfx slot per screen, loaded at sprite prep. If a second standing item preps on the same screen and the slot is taken by a *different* item, the second draws its **vanilla** sprite. *Alternative:* N slots / dynamic VRAM allocation → rejected for phase 1 (complexity; the solo-per-screen reality makes it unnecessary). The fallback guarantees "never garbage."

### D3 — Map placed `item_id → drawable` via the existing icon table
Prefer reusing `kDirectGrantIcons[item_id]` (gfx bundle + OAM size + palette) as the item→drawable source of truth, so field sprites and direct-grant confirmations stay visually consistent and there's one table to maintain. Where the receipt decompressor needs a *receipt-gfx id* rather than the icon bundle, add a thin `item_id → receipt_gfx` resolver next to it. *Alternative:* a fresh standalone table → rejected (duplication / drift risk).

### D4 — Hook at sprite **prep** (load) and sprite **draw** (render), gated
At prep (under rando + toggle): `placed = Placement_Lookup(LOC_*, vanilla)`; if `placed != vanilla` and resolvable, claim the screen slot and load its gfx; record per-sprite that it should draw the field-item tile. At draw: if flagged, draw the loaded tile + palette; else vanilla draw. Each hook is `if (Rando_FieldItemSpritesActive()) { ... } else { vanilla }` so the inactive path is byte-identical.

### D5 — Client-local cosmetic-class toggle, no canonical impact
`field_item_sprites` is read from `zelda3.ini` (like the cosmetic options), default **on** under rando. It does **not** enter `RandoSettings` canonical serialization, the settings hash, or the corpus — purely a presentation switch. Surfaced in the native window alongside cosmetics. *Alternative:* a real settings axis → rejected (would force a genVer bump + corpus regen for a visual-only switch; contradicts the cosmetic precedent in `docs/randomizer.md` "Cosmetics").

### D6 — Spike-first task order
Task 1 is a runtime go/no-go: wire **one** site (Piece of Heart) end-to-end, build, and confirm in-game via F12 dump + eyes that the placed item renders with correct gfx + palette (not garbage, not a slot clobber). Only after the spike passes do the remaining sites get wired. This directly applies the boss-shuffle lesson (a pure swap shipped, rendered garbage, was deactivated) and the "playtest at slice start" rule.

## Risks / Trade-offs

- **VRAM slot correctness (the spike).** Wrong slot/char base → garbage tiles. → Mitigation: D6 spike on one site with an F12 dump before fan-out; reuse the proven receipt slot machinery and verify char base in-game.
- **Palette mismatch.** Item drawn with the area's sprite palette instead of the item's → wrong colors. → Mitigation: drive palette from `kDirectGrantIcons` palette flags / the receipt palette load (`src/misc.c:986-995`); verify per-item in the spike + playtest.
- **Receipt-lifecycle clobber.** Calling the decompressor at prep could disturb an in-flight receive animation that uses the same scratch slot. → Mitigation: use a **separate** field-item slot, or guard: if a receipt is active, skip the field load this frame and fall back to vanilla; re-load when free.
- **Multi-item screens.** Two standing items, one slot. → Mitigation: D2 vanilla fallback for the loser.
- **RAM-compare under rando.** The fork only RAM-compares in vanilla side-by-side mode; under `RandomizerActive` it already diverges, so OAM/draw changes here don't break the compare contract. Still gate strictly so vanilla play is untouched.
- **Switch parity.** Pure C draw/gfx path, no platform-specific code → builds on Switch unchanged; manual smoke at release cut.

## Migration Plan

Additive and reversible. The toggle defaults on under rando but `field_item_sprites=0` in `zelda3.ini` restores the exact vanilla-looking field instantly (live per-frame read, no restart). No save/format/asset migration. Rollback = revert the commits; no data cascade.

## Open Questions

- **Receipt-gfx id vs. icon bundle**: does every phase-1 placed item have a receipt-gfx the decompressor can produce, or do a few (e.g. bottles-with-contents, progressive tiers) need a representative-tile decision? Resolve while authoring the `item_id → receipt_gfx` resolver (verify against `kDecodeAnimatedSpriteTile_Tab`, `src/load_gfx.c:290`).
- **Zora's Ledge**: confirm whether its standing pickup has a reachable draw hook or needs new sprite instrumentation (archived audit says no enumerated vanilla handler). If new instrumentation is required, defer it to a follow-up rather than blocking phase 1.
- **Dedicated slot vs. guarded reuse**: decide in the spike whether a second VRAM scratch slot is needed or the receipt slot can be safely shared with the in-flight guard (D-risk above).
