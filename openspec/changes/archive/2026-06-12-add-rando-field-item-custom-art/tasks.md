# add-rando-field-item-custom-art — task tracking

> Deferred follow-up to `add-rando-field-item-sprites`. Visual-only; rando-gated;
> NO genVer/settings_hash/corpus. Captures the "custom art" chunk for the two
> ALTTPR items with no vanilla receive bundle (Triforce Piece, Rupoor).

## 1. Apply-time pre-flight

- [x] 1.1 **License check (BLOCKING):** verify the `z3randomizer` repo license permits reusing its custom art and palette data. If not reusable, plan to author equivalent 4bpp tiles instead. → **MIT (Equilateral IT 2016-17), reusable with notice; recorded in `NOTICE`.**
- [x] 1.2 Pin the final asset policy. → **The Triforce Piece and magic-decanter art stays as attributed z3randomizer custom art. The generator uses local palette-indexed pixel rows and an authored preview palette, with no ROM-derived preview palette, palette words, or item-offset tables. Rupoor uses the existing rupee tile plus the attributed z3randomizer `off_black` custom palette.**
- [x] 1.3 Decide the asset home (D1/open-question): new custom-item-gfx blob in `zelda3_assets.dat` vs. appending to an existing custom-asset section. Confirm the loader path that can write the receive-item VRAM slot (chars 0x24/0x34). → **New `kRandoCustomItemGfx` asset appended last in compile_resources.py, sourced from committed `assets/rando/custom_item_gfx.png`; loader seam = Rando_EnsureRecvItemSlotGfx → g_ram[0x9000+0x2d40].**

## 2. Asset pipeline (the bulk)

- [x] 2.1 Obtain the tiles → **The committed `assets/rando/custom_item_gfx.png` is regenerated from local pixel rows by `assets/scripts/gen_custom_item_gfx_png.py`; MIT attribution added to `NOTICE`.**
- [x] 2.2 Pipeline both ends → **`compile_resources.py print_rando_custom_item_gfx()` (PNG→4bpp, slot-shaped 0x80-byte entries) emits `kRandoCustomItemGfx` (asset 165); `src/assets.h` regenerated (kNumberOfAssets 166, new kAssets_Sig — old .dat invalidated as intended); fresh `zelda3_assets.dat` + `vanilla_assets_hash.h` built. No extract_resources change needed (source is the committed PNG, not the ROM).**
- [x] 2.3 Palettes → **`off_black` stays as an attributed custom palette in `src/sprite.c`. The PNG preview palette is authored and ignored by the compiler. Runtime palette application remains in `Rando_ApplyCustomItemGfxPalette`, re-applied per draw after room/area palette reloads.**

## 3. Load + draw

- [x] 3.1 Custom decode path → **`Rando_EnsureRecvItemSlotGfx` handles gfx ids with the 0x80 bit (`kRandoCustomGfx_TriforcePiece`/`_Rupoor`): blob memcpy into the slot (or `DecodeAnimatedSpriteTile_variable(0x24)` for Rupoor's vanilla tile) + palette apply. `Rando_TryDrawFieldItemSprite` unchanged. `DecodeAnimatedSpriteTile_variable` asserts custom ids never leak in. Confirmation ancilla routes through the same seam and re-applies the palette mid-float (transition reload case).**
- [x] 3.2 SPIKE → **autonomous form (no playtest available): offline render of the full chain confirmed the custom-art entries and Rupoor palette path. Mis-tint check deferred to the user playtest (5.3).**

## 4. Wire the resolver + confirmation icon

- [x] 4.1 Resolver routing → **flows through `Rando_GetFieldItemIcon` Tier 2 (`kDirectGrantIcons[52/110]`) — both items have non-vanilla dispatches so Tier 1 can't catch them; comments updated. Verified all `Rando_GetFieldItemIcon`/`AncillaAdd_RandoIconReceipt` callers (sprite.c, ancilla.c flute draw, player.c flute spawn-sizing) route the gfx through `Rando_EnsureRecvItemSlotGfx`.**
- [x] 4.2 `direct_grant_icons.yaml`: TriforcePiece `{gfx: 0x80, big: 2, oam: 0x36}`, Rupoor `{gfx: 0x81, big: 0, oam: 0x36}`; `direct_grant_icons.h` regenerated (60 mapped icons).

## 5. Verification + playtest

- [x] 5.1 **`check_audit_guard --strict` green; `check_no_embedded_data` / `check_determinism` / `check_codegen_wiring` green; corpus 117/117 OK (placement digests byte-identical — no kGeneratorVersion bump, visual-only confirmed); `--rando-selftest` all subsystems OK.**
- [x] 5.2 **MSVC Release x64 + WSL `make clean && make` (gcc -Werror) both clean.**
- [x] 5.3 Playtest (USER): **CONFIRMED 2026-06-11 — triforce + off-black rupoor field sprites and confirmation icons (first round), ½/¼ magic decanters (extension round); no palette bleed reported.** (Side discovery: pasting a share string while the window held different settings generated a different seed — by design, the share carries only settings-HASH + seed, but the warning is too quiet. Follow-up spec: share-string v2 embedding full settings.)

## 6. Docs + archive

- [x] 6.1 `docs/randomizer.md` "Field item sprites": custom-art subsection added; TriforcePiece removed from the no-graphic fallback list; assets.dat re-extract note.
- [x] 6.2 Review pass complete; fixes folded into the implementation.
- [ ] 6.3 `openspec archive add-rando-field-item-custom-art` after playtest sign-off.

## Playtest seeds (for 5.3 — generated against this build)

All on this branch's binary + the regenerated `zelda3_assets.dat` (REQUIRED:
the old .dat dies with "Invalid assets file").

1. **Triforce Piece, instant**: share `LJJFGU2DE3XUV3SUMDELFQ3LOW724YOXYQAQAAAAAAAAAABKDE`
   (open, triforce-hunt 20/30, expert pool, seed 1). The **Maze Race** prize is
   a Triforce Piece — yellow triforce standing at the finish, zero items
   needed; collect → triforce confirmation icon + HUD piece counter +1. Also:
   Zora's Ledge, Pyramid ledge, Spectacle Rock Cave PoH show pieces.
2. **Rupoor, instant collect**: share `LJJFGU2DPXU3RXZRMN25JP63H4N25AK4VMOAAAAAAAAAAAEVFA`
   (open, ganon, expert pool, seed 28). **Sunken Treasure** (dam grate south of
   Link's House) is a Rupoor — off-black rupee; collect → rupee count drains
   10 + off-black rupee confirmation icon.
3. **Rupoor, instant visual** (bonus): share `LJJFGU2DE3XUV3SUMDELFQ3LOW724YOXYQBQAAAAAAAAAAEMSY`
   (hunt seed 3). The **Master Sword Pedestal** holds a Rupoor (visible on
   walking up, collect needs pendants); **Library** shelf shows a Triforce
   Piece (visible without boots); the **Mushroom** spot is a ¼-Magic decanter.
4. **Magic decanters** (extension round): share `LJJFGU2DPXU3RXZRMN25JP63H4N25AK4VMAQAAAAAAAAAACFIM`
   (open, expert pool, seed 1). The **Maze Race** prize is the ½-Magic
   decanter (instant collect → decanter confirmation icon + magic upgrade);
   **Zora's Ledge** shows the ¼-Magic decanter (visible across the water).
