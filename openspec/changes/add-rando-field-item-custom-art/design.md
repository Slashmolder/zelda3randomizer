## Context

`add-rando-field-item-sprites` resolves a placed item to a draw via the receive-
item tables (`kReceiveItemGfx`/`kReceiveItem_Tab1`/`kWishPond2_OamFlags`) indexed
by an LttP receive code, loading the tile with `DecodeAnimatedSpriteTile_variable`
into the shared receive-item VRAM slot (chars 0x24/0x34). Every item with a
vanilla pickup sprite works. Three ALTTPR items have no vanilla bundle:

- **Triforce Piece**: `ITEM_TriforcePiece` only ticks `g_rando_triforce_piece_count`
  (a HUD counter) — no sprite tile. `kDirectGrantIcons[52].gfx==0` (audio-only).
- **Rupoor**: `ITEM_Rupoor` drains rupees; no vanilla receive code. Currently
  mapped (via `direct_grant_icons.yaml`) to the green-rupee bundle (gfx 0x24),
  so it reads as a green rupee.

Runtime-truth findings already gathered:
- The rupee body uses sprite-palette indices 11 (main) + 12 (highlight); green/
  red/blue are palettes 4/1/2. Scanning a live CGRAM dump, **no sprite palette is
  reliably grey at those indices**, and palette contents are area-dependent — so
  a plain `oam_flags` slot-swap cannot give a stable grey Rupoor.
- ALTTPR's answer (`z3randomizer/itemdatatables.asm`): custom gfx + custom palette
  per item. Rupoor `$59` + `PalettesCustom_off_black`; Triforce Piece `$6C` (16×16)
  + `PalettesVanilla_green_blue_guard+$0E`. Tiles in `data/customitems.4bpp`.

## Goals / Non-Goals

**Goals:**
- Triforce Piece draws a recognizable triforce on the field (and as its grant
  confirmation icon).
- Rupoor draws an off-black / grey rupee, matching ALTTPR's recognizable cue.
- Reuse the existing field-item draw path (chars 0x24/0x34 + palette); add only
  the missing tiles + palettes.
- Stay visual-only: rando-gated, no genVer / settings_hash / corpus.

**Non-Goals:**
- No new gameplay, logic, or placement behavior.
- Not porting z3randomizer's whole custom-item-gfx subsystem — just the two tiles
  (+ palettes) these items need.
- HalfMagic / QuarterMagic stay audio-only (separate, lower value).

## Decisions

### D1 — Add a tiny custom-item-gfx asset, don't shoehorn into the vanilla sheets
The vanilla receive-gfx sheets (decompressed by `Decomp_spr` 0x5a–0x5d, indexed by
`kDecodeAnimatedSpriteTile_Tab`) have no triforce/rupoor tile and no obvious free
slot. Add a small dedicated custom-item-gfx blob (2 tiles, 4bpp) to the asset
pipeline + a loader that writes the receive-item VRAM slot (chars 0x24/0x34), so
the existing draw path consumes it unchanged. *Alternative:* find spare room in a
vanilla sheet → rejected (fragile, no guaranteed free tile).

### D2 — Source the tiles from z3randomizer, license permitting; else author them
`z3randomizer/data/customitems.4bpp` already contains the exact tiles ALTTPR uses
($6C triforce, $59 rupoor). Extracting them is the fast path. **Gate:** verify the
z3randomizer repo's license covers reusing its gfx data (the ALTTPR PHP is MIT;
the asm repo's art assets need their own check). If not reusable, author two
equivalent 4bpp tiles (a triforce is trivial geometry; a rupoor is a rupee
silhouette).

### D3 — Custom palettes loaded at draw, like sword/shield
Port `PalettesCustom_off_black` (16 colours) for Rupoor and the triforce palette,
and load them into the draw's sprite palette slot the way `Palette_Load_Sword` /
`Palette_Load_Shield` do (the field draw already calls those for sword/shield
gfx). This gives a *stable* colour independent of area palettes (the reason a
slot-swap fails).

### D4 — Resolve in `Rando_GetFieldItemIcon`, sync the confirmation icon
Add explicit cases: `ITEM_TriforcePiece` → triforce tile + palette; `ITEM_Rupoor`
→ rupoor tile + off_black palette (replacing the green-rupee reuse). Update
`direct_grant_icons.yaml` so the §7.6 grant-confirmation icon matches (single
source of truth for both surfaces). No change to `rando_item_display_lttp` (these
have no receive code; they resolve through the icon/custom path).

## Risks / Trade-offs

- **License of z3randomizer art** → Mitigation: verify before copying; author
  originals if needed (D2). Do not commit copied data until cleared.
- **Asset-pipeline plumbing for a new gfx blob is the bulk** (extract + compile +
  reader + cache invalidation), per "asset format changes require both ends." →
  Mitigation: keep the blob to 2 tiles; mirror the existing custom-asset patterns.
- **VRAM-slot sharing** (chars 0x24/0x34) already handled by the field-item draw
  (`g_recv_item_slot_owner` + per-frame reservation) — custom tiles inherit it.
- **Palette slot management** for the off_black load → mirror sword/shield's
  approach; confirm it doesn't tint other on-screen sprites (playtest).

## Migration Plan

Additive + reversible. Until built, Triforce Piece keeps its vanilla fallback and
Rupoor keeps the green-rupee look (both already shipped in
`add-rando-field-item-sprites`). No save/format/corpus migration.

## Open Questions

- z3randomizer art license — reusable, or author originals?
- Cleanest fork home for a 2-tile custom-item gfx blob (new asset entry vs.
  appending to an existing custom-asset section in `zelda3_assets.dat`).
- Does the off_black palette load need a dedicated sprite slot, or can it
  transiently reuse one (and must it restore)? Resolve in an apply-time spike,
  same as the original field-item gfx spike.
