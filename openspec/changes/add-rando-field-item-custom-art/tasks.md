# add-rando-field-item-custom-art — task tracking

> Deferred follow-up to `add-rando-field-item-sprites`. Visual-only; rando-gated;
> NO genVer/settings_hash/corpus. Captures the "custom art" chunk for the two
> ALTTPR items with no vanilla receive bundle (Triforce Piece, Rupoor).

## 1. Apply-time pre-flight

- [ ] 1.1 **License check (BLOCKING):** verify the `z3randomizer` repo license permits reusing its gfx/palette data (`data/customitems.4bpp`, `custompalettes.asm`). The ALTTPR PHP is MIT; the asm repo's art is a separate question. If not reusable, plan to author equivalent 4bpp tiles instead. Record the verdict in `audit.md §"Custom-art provenance"`.
- [ ] 1.2 Pin the exact sources: triforce tile = gfx `$6C` (16×16) and rupoor tile = gfx `$59` in `z3randomizer/data/customitems.4bpp` (the `ItemReceiptGraphicsROM` blob, `LTTP_RND_GeneralBugfixes.asm:163-166`); palettes `PalettesCustom_off_black` (`custompalettes.asm:16`) and `PalettesVanilla_green_blue_guard+$0E`. Confirm tile offsets within the 4bpp blob.
- [ ] 1.3 Decide the asset home (D1/open-question): new custom-item-gfx blob in `zelda3_assets.dat` vs. appending to an existing custom-asset section. Confirm the loader path that can write the receive-item VRAM slot (chars 0x24/0x34).

## 2. Asset pipeline (the bulk)

- [ ] 2.1 Obtain the two 4bpp tiles (extract from `customitems.4bpp` if licensed, else author): a 16×16 Triforce piece and a Rupoor (rupee silhouette).
- [ ] 2.2 Add them to `assets/extract_resources.py` + `assets/compile_resources.py` (both ends) and the C-side reader in `assets.h` / `load_gfx.c`; bump/invalidate cached `zelda3_assets.dat`. Keep the data OUT of tracked source (gitignored artifact) per the repo's no-embedded-data rule.
- [ ] 2.3 Port the palettes: `PalettesCustom_off_black` (16 colours) and the triforce palette into fork palette data; add a `Palette_Load_*` for the off_black slot (mirror `Palette_Load_Sword`/`_Shield`).

## 3. Load + draw

- [ ] 3.1 Add a "decode custom item tile" path that writes the receive-item VRAM slot (chars 0x24/0x34) for the triforce / rupoor tiles — either a new index alongside `kDecodeAnimatedSpriteTile_Tab` or a small parallel helper. Reuse the field-item draw (`Rando_TryDrawFieldItemSprite`) unchanged.
- [ ] 3.2 SPIKE (per the original field-item gfx spike): wire ONE (Triforce Piece), build, F12/playtest that it renders the triforce with correct palette (not garbage) before doing Rupoor. Confirm the off_black palette load doesn't mis-tint other on-screen sprites.

## 4. Wire the resolver + confirmation icon

- [ ] 4.1 `Rando_GetFieldItemIcon` (`src/rando/rando.c`): route `ITEM_TriforcePiece` → triforce tile + palette; `ITEM_Rupoor` → rupoor tile + off_black palette (replace the current green-rupee reuse).
- [ ] 4.2 Sync `assets/rando/direct_grant_icons.yaml`: point TriforcePiece + Rupoor at the new custom tiles so the §7.6 grant-confirmation icon matches the field sprite (regen `direct_grant_icons.h`).

## 5. Verification + playtest

- [ ] 5.1 `check_audit_guard.py --strict` green; confirm NO `kGeneratorVersion`/corpus change (visual only — run the corpus once to prove digests unchanged).
- [ ] 5.2 Build clean: WSL `gcc -O2 -Werror` + MSVC.
- [ ] 5.3 Playtest: a Triforce-Hunt seed with a piece at a standing-item location shows a triforce; a seed with a Rupoor at one shows a grey/off-black rupee; confirmation icons match on collect; no palette bleed onto other sprites.

## 6. Docs + archive

- [ ] 6.1 Update the `docs/randomizer.md` "Field item sprites" section: Triforce Piece + Rupoor now have custom art (remove their "fallback / out-of-scope" notes).
- [ ] 6.2 Fresh-eyes audit per `[[cluster-audit-cadence]]`.
- [ ] 6.3 `openspec archive add-rando-field-item-custom-art` after playtest sign-off.
