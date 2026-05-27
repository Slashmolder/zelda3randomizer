## Why

Phase A §7.6 added `Rando_ShowDirectGrantConfirmation()` at `src/rando/rando.c:393` — when a direct-grant fires (HalfMagic / QuarterMagic, prize bits, dungeon-item bits, TriforcePiece), it plays the standard item-receipt sound and refreshes the HUD. **Audio + HUD only.** No item-receipt ancilla, no per-item icon. The player sees nothing pop above Link.

Three call-site categories are affected: the §6.5 tablets (Ether/Bombos), §6.4 NPCs that grant direct-write items, and the §6.7 Pyramid Fairy item drop (`src/sprite_main.c:1273`). When the player's music or SFX is muted, when the same frame already has other audio cues, or when the Triforce counter increment is the only state change, the player can't tell anything happened. Tablets in particular feel broken — Link strikes the tablet, magic-meter consumes, screen flashes, and... no visible item.

The Phase A `Rando_ShowDirectGrantConfirmation` header at `src/rando/rando.h:108-112` already calls out this limitation explicitly: *"Does NOT spawn an item-receipt ancilla — the direct-grant path's item has no corresponding LttP receive code, so an animation isn't available without per-item graphics work."*

This change does that per-item graphics work.

## What Changes

- **Author `assets/rando/direct_grant_icons.yaml`** — a per-item icon-tile mapping covering every item that reaches `Rando_ShowDirectGrantConfirmation` today: `HalfMagic`, `QuarterMagic`, `TriforcePiece`, the 10 prize items (`Prize_Crystal1..7`, `Prize_GreenPendant/RedPendant/BluePendant`), and the small-key / big-key / map / compass per-dungeon direct-grant items. Each entry pins a tile address into the existing zelda3 graphics blob (no new sprite art required — the icons exist in vanilla for HUD/inventory rendering; we just need a lookup table referencing them).
- **Extend the codegen pipeline** (`assets/rando_logic_gen.py`) to emit `src/rando/direct_grant_icons.h` as a `static const DirectGrantIconEntry kDirectGrantIcons[]` table indexed by `ITEM_<id>`. CI guard at `assets/scripts/check_codegen_wiring.py` covers the new header.
- **Add a new custom-icon item-receipt ancilla type to `src/ancilla.c`.** Mirrors the existing receive-item ancilla shape (Link holds the icon above head, scrolls up, fades) but draws an arbitrary 8×16 tile pair from the icon table rather than a hard-coded LttP item sprite.
- **Extend `Rando_ShowDirectGrantConfirmation()` signature** from `void` to `(uint8 item_id)`. Existing audio + HUD refresh behavior preserved; new visible ancilla spawned via the lookup. Backward compatibility is N/A — Phase A has 5 call sites and we update all of them in this change.
- **Update the 5 known call sites** (`player.c:594`, `player.c:634`, `player.c:3886`, `sprite_main.c:1273`, `sprite_main.c:18586`) to pass the granted item id. The call-site context already knows the item — each is in a §6.x branch that just direct-wrote a specific item.
- **No `kGeneratorVersion` bump.** Pure UX; placement output is unchanged.

## Capabilities

### New Capabilities

(none)

### Modified Capabilities

- `randomizer-placement`: extend the §6.x direct-grant confirmation requirement (currently audio + HUD only) to also include a per-item-type visible ancilla. Existing spec text at the dispatcher / receivable-items requirements is untouched; only the confirmation behavior changes.

## Impact

- **Code**: `src/rando/rando.c` (signature change), `src/rando/rando.h` (header signature + comment), `src/ancilla.c` + `src/ancilla.h` (new ancilla type), `src/player.c` (2 sites), `src/sprite_main.c` (2 sites). All updates are mechanical — each existing `Rando_ShowDirectGrantConfirmation()` call gets the granted item id appended.
- **Assets**: `assets/rando/direct_grant_icons.yaml` (new), `src/rando/direct_grant_icons.h` (generated; mirror `location_ids.h` / `item_ids.h` packaging).
- **Build wiring**: New generated header registered in `Makefile`, `Zelda3.vcxproj` pre-build, `src/platform/switch/Makefile`. `assets/scripts/check_codegen_wiring.py` exercises the multi-build-system guard.
- **Determinism guard**: No-op — no new symbols from `src/rando/*.c` against the forbidden list (no `rand`, no `time`).
- **Audit guard**: No-op — `Rando_ShowDirectGrantConfirmation` writes only `sound_effect_2` and triggers a HUD refresh; neither is a tracked inventory cell. No new `// rando-exempt:` comments needed.
- **Player-facing**: Tablets, Witch hut, Pyramid Fairy direct-drops, prize boss rooms, and dungeon-direct chests all gain a visible item-pop matching their granted item. Race-mode safe (the icon doesn't reveal *future* placement; it surfaces the current grant).
- **Switch parity**: Tile draw uses the same `Sprite_DrawMultiplePlayerDeferred` path as the existing receive-ancilla; no platform-specific work.
