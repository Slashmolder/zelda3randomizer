## Why

Under an active randomizer slot, free-standing overworld/dungeon items still draw their **vanilla** sprite (a Piece-of-Heart location shows a floating heart piece even when it grants the Hookshot; the Book of Mudora still looks like the book). The randomizer hooks only the *grant* side (`Rando_DispatchVanillaGrant` / `Placement_Lookup`) — *what you receive* — never the *draw* side — *what the item looks like before pickup*. ALTTPR shows the real item on the field; this is a long-standing visual gap that makes seeds harder to read and route. Closing it makes the world honestly reflect placement.

## What Changes

- **Draw the placed item's sprite at free-standing item locations** when `kFeatures1_RandomizerActive` is set: at sprite *prep* resolve the location's placed item via `Placement_Lookup`, load that item's gfx into a per-screen scratch VRAM slot (reusing the existing on-demand decompressor `DecodeAnimatedSpriteTile_variable`), and at *draw* render the loaded tile + the item's palette instead of the vanilla sprite.
- **Spike-first delivery.** The first task is a runtime GFX go/no-go spike on a single location (build + F12/playtest), before any site is wired. This is a deliberate guard against the boss-shuffle failure mode (a pure swap that rendered garbage and was deactivated — see `add-rando-shuffles-and-minigames` tasks.md §2.4 / `src/rando/rando.c:1853`).
- **Sites (phase 1, solo-per-screen standing items):** Piece of Heart, Heart Container, Book of Mudora, Mushroom, Master Sword pedestal, the medallion tablets (Bombos/Ether/Quake). Zora's Ledge is included if a draw hook is reachable; otherwise deferred.
- **Chests are OUT of scope** — ALTTPR keeps chests closed; revealing chest contents is a separate (non-)feature.
- **Multi-item-per-screen fallback:** if more than one field item shares a screen and the single VRAM slot can't hold both, the extra item(s) fall back to the vanilla sprite (never garbage).
- **Client-local cosmetic-class toggle** `field_item_sprites` (default on under rando), backed by `zelda3.ini` like the cosmetic options — **NOT** a canonical settings axis. No `kGeneratorVersion` bump, no settings-hash change, no corpus regeneration.
- **Vanilla path byte-identical** when `kFeatures1_RandomizerActive` is clear.

## Capabilities

### New Capabilities

- `randomizer-field-item-sprites`: runtime draw-side substitution of free-standing item sprites with the placed item's graphics — gfx resolution (placed item → drawable gfx/palette), per-screen VRAM-slot management, the prep/draw hook contract for each standing-item sprite, the multi-item-per-screen vanilla fallback, and the client-local toggle. Read-only with respect to placement/logic (a pure consumer of the placement table); no RAM-state or save-format impact.

### Modified Capabilities

<!-- none — the feature is a new, purely-visual capability gated on RandomizerActive.
     The toggle is client-local (zelda3.ini), cosmetic-class, with no canonical-settings
     serialization, so randomizer-core / randomizer-save / randomizer-native-window
     requirements are unchanged. -->

## Impact

- **Code:** `src/sprite_main.c` (prep + draw hooks at the standing-item handlers), `src/load_gfx.c` (extend/wrap the on-demand item-gfx decompress for a field-item VRAM slot), a small new helper surface in `src/rando/` (resolve placed item → gfx/palette; per-screen slot state), `src/config.c` (the `field_item_sprites` INI key), `src/rando/rando_window/` (toggle in the native window's Cosmetics/Shuffles area).
- **Assets:** none required — gfx come from the existing per-item decompressor; the `kDirectGrantIcons` table (item_id → gfx/size/palette) is candidate infrastructure for the item→drawable mapping.
- **Determinism / save / corpus:** zero. Purely visual, gated on `RandomizerActive`, client-local toggle. No `kGeneratorVersion`, no `settings_hash`, no corpus regen.
- **Risk:** the load-bearing risk is graphics/VRAM (per-screen slot + palette correctness); mitigated by the spike-first task order and an end-to-end playtest (the playable-slot path has no automated test). Upstream runtime reference is the ALTTPR asm (`z3randomizer` checkout: `decompresseditemgraphics.asm`, `itemdatatables.asm`, `heartpieces.asm`), not the PHP (placement-only).
