## Why

`add-rando-field-item-sprites` draws the placed item's graphic at free-standing
locations by reusing the engine's existing **receive-item gfx bundles** (the
vanilla `kReceiveItemGfx` / `DecodeAnimatedSpriteTile_variable` tiles). That
covers every item that has a vanilla pickup sprite. Three ALTTPR-specific items
do **not** have a vanilla bundle, so they currently fall back to the vanilla
location sprite (or a stand-in):

- **Triforce Piece** — only a Triforce-Hunt HUD counter exists; there is no
  triforce *sprite* tile. It shows the location's vanilla sprite (e.g. a heart
  piece) — misleading in Triforce-Hunt, where spotting pieces on the field
  matters most.
- **Rupoor** — reuses the green-rupee bundle, so it looks like a normal green
  rupee. ALTTPR's Rupoor is an off-black / grey rupee (a recognizable "this
  drains you" cue). No vanilla sprite palette is reliably grey (palette contents
  are area-dependent), so a slot-swap can't fix it — it needs a custom palette.

ALTTPR already solves both with **custom art**: `z3randomizer/itemdatatables.asm`
maps Rupoor (item `$59`) to a custom tile + the `PalettesCustom_off_black`
palette, and Triforce Piece (`$6C`) to a dedicated 16×16 triforce tile + a
vanilla palette offset. The gfx live in `z3randomizer/data/customitems.4bpp`
(the `ItemReceiptGraphicsROM` blob) — which this fork does **not** have. Porting
that small custom-art surface is the remaining chunk; recording it here so the
work isn't lost.

This is **deferred follow-up** to `add-rando-field-item-sprites` (which ships
without it). Purely visual; gated on `kFeatures1_RandomizerActive`; no
placement / share-string / `settings_hash` / `kGeneratorVersion` / corpus impact.

## What Changes (intended scope)

- **Add a small custom-item-gfx surface** to the fork's asset pipeline (the fork
  has no `customitems.4bpp` equivalent). Two tiles are needed: a 16×16 Triforce
  piece and a Rupoor tile. Source: extract from `z3randomizer/data/customitems.4bpp`
  at the `$6C` / `$59` offsets, **or** author equivalent 4bpp tiles. (Verify the
  z3randomizer license before copying its art data — the ALTTPR *PHP* is MIT, but
  the asm repo's gfx assets need their own license check before porting.)
- **Port the custom palettes**: `PalettesCustom_off_black` (16 colours, from
  `z3randomizer/custompalettes.asm:16`) for Rupoor; the Triforce palette
  (`PalettesVanilla_green_blue_guard+$0E`). Load them for the draw the way
  `Palette_Load_Sword` / `Palette_Load_Shield` load their slots.
- **Make the custom tiles loadable** into the receive-item VRAM slot (chars
  0x24/0x34) so the existing field-item draw path can use them — either by adding
  a new gfx-bundle index alongside `kDecodeAnimatedSpriteTile_Tab`, or a small
  parallel "decode a custom item tile" helper that writes the same slot.
- **Wire the resolver**: in `Rando_GetFieldItemIcon` (`src/rando/rando.c`), route
  `ITEM_TriforcePiece` → triforce tile + palette, and `ITEM_Rupoor` → rupoor tile
  + `off_black` palette (replacing the current green-rupee reuse). Keep the
  `direct_grant_icons.yaml` entries in sync so the direct-grant **confirmation
  icon** matches the field sprite.
- **No new settings, no genVer/corpus bump.** Visual only.

## Capabilities

### Modified Capabilities

- `randomizer-field-item-sprites`: ADDED Requirement — custom-art rendering for
  the ALTTPR items that have no vanilla receive bundle (Triforce Piece, Rupoor),
  so they draw a recognizable sprite instead of the vanilla-location fallback.

## Impact

- **Assets:** a new small custom-item-gfx blob (2 tiles) + custom palette data in
  the asset pipeline (`assets/` extract + compile, `zelda3_assets.dat`,
  `assets.h`/`load_gfx.c` reader) — the "asset format changes require both ends"
  path. This is the bulk of the effort.
- **Code:** `src/load_gfx.c` (custom-tile decode into the receipt slot + palette
  load), `src/rando/rando.c` (`Rando_GetFieldItemIcon` routing for TriforcePiece
  / Rupoor), `assets/rando/direct_grant_icons.yaml` (sync the confirmation icon).
- **Provenance:** `z3randomizer/itemdatatables.asm:507` (Rupoor `$59` +
  `off_black`), `:526` (Triforce Piece `$6C`), `custompalettes.asm:16`
  (`off_black`), `data/customitems.4bpp` (the tiles). PHP upstream is MIT; the
  asm-repo art license is unverified — check before porting.
- **Regression risk:** zero by design (visual, rando-gated, no genVer/corpus).
- **Effort:** small-to-medium — the asset-pipeline plumbing for a custom gfx blob
  is the long pole; the wiring is a few lines once the tile loads.
