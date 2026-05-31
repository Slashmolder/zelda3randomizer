## Why

In ALTTPR's Inverted world-state, "Ganon has abandoned the Pyramid and is hiding underneath Hyrule Castle" — the final fight relocates from the Dark-World pyramid (area 0x5B) to under the Light-World Hyrule Castle (area 0x1B). This fork's Inverted implementation deliberately **kept Ganon in the DW pyramid** as a scoping decision (documented in `docs/inverted_alttpr_gaps.md` §A1): the screen-0x1B pyramid overlay was suppressed because its high pyramid map16 blocks need DW-pyramid gfx/palette that cannot coexist with the castle gfx loaded on screen 0x1B, so they rendered as garbage. This change closes that last intentional divergence from ALTTPR Inverted.

This is the single largest remaining Inverted gap and the only one that is an *intentional* topology difference (everything else is either done, playtest-pending, or a minor QoL/cosmetic item). It is also the highest-risk: the gfx/palette coexistence on screen 0x1B is a load-bearing blocker that an apply-time spike must resolve before the rest is wireable.

## What Changes

- **Relocate the Ganon fight to under Hyrule Castle (area 0x1B)** in Inverted, matching ALTTPR `app/Rom.php setInvertedMode()`. Gated on the active Inverted rando world-state so non-Inverted / Open / Standard / Retro play stays byte-identical (the fork RAM-compares against the original ROM).
- **Un-suppress + properly build the screen-0x1B inverted pyramid overlay** (`src/rando/inverted_maps_apply.c:43-57`). **Primary risk / spike:** solve the gfx/palette coexistence the suppression avoided (DW-pyramid blocks vs. castle gfx on the same screen).
- **Relocate the Ganon fall-hole to area 0x1B.** ALTTPR adds an ExtraHole (Map16=0x0140, Area=0x001B, Entrance=0x7B; `Rom.php:1842-1849`). The fork's `kFallHole_*` tables (assets 127/128/129) are built **sorted by entrance-id** (`assets/compile_resources.py`) and consumed by `Overworld_GetPitDestination` with a hardcoded scan bound (`src/overworld.c:3299-3321`) — adding a 20th entry needs the extraction-pipeline change **and** the bound bump.
- **Add an Inverted variant of `CreatePyramidHole`** (`src/overworld.c`) to carve the 0x1B hole.
- **Port the pyramid/HC exit-data relocations** (exits 0x06/0x37/0x3D + pyramid door move 0x35; `Rom.php`) into the `src/rando/inverted_entrances.c` override table — these rows are already transcribed in prior research and were dropped *only* because Ganon stayed in the DW.
- **Port `SetDeathWorldChecked_Inverted` `.castle` branch** (`darkworldspawn.asm`) into `src/messaging.c Death_Func15`: death-to-Ganon now respawns at the castle in the LIGHT world. This is currently **intentionally not ported** because it would be wrong while Ganon is in the DW pyramid.
- **Override flute slot 8** (`kBirdTravel_ScreenIndex[8]` 0x5B→0x1B) in `inverted_entrances.c`, currently left vanilla precisely because Ganon stayed at the DW pyramid.
- **Reverses prior "non-gap" decisions** recorded in `docs/inverted_alttpr_gaps.md` §C2 (the pyramid ExtraHole and pyramid/HC exit rows were declared non-gaps *because* Ganon stayed in the DW). This change makes them live.

## Capabilities

### New Capabilities
- `randomizer-inverted-runtime`: runtime overworld topology + rendering behavior of the Inverted world-state (this change establishes the capability with the Ganon-location requirement; the existing shipped Inverted runtime behaviors — entrance bindings, portals, flute, bunny-world — are documented here as the contract grows).

### Modified Capabilities
<!-- none — Ganon's overworld location is a runtime/rendering behavior, not a generation/placement/logic requirement, so no existing randomizer-* spec changes. -->

## Impact

- **Code:** `src/rando/inverted_maps_apply.c` (un-suppress 0x1B overlay), `src/overworld.c` (CreatePyramidHole inverted variant + fall-hole scan bound), `src/rando/inverted_entrances.c` (exit-data rows + flute slot 8), `src/messaging.c` (death-respawn `.castle`), `src/rando/inverted_maps.c` + `assets/scripts/gen_inverted_maps.py` (0x1B overlay data).
- **Asset pipeline:** `assets/compile_resources.py` fall-hole extraction (20th hole entry; sorted-index handling). Touches `zelda3_assets.dat` → cache invalidation.
- **Graphics:** the screen-0x1B gfx/palette coexistence (load-bearing spike) — may require a gfx-set / palette-slot solution analogous to other dual-gfx screens.
- **No generation/placement/logic impact:** Ganon's *logic* reachability is already handled (`logic_parts/inverted/`, `Goal_IsCompletable` checks crystals + Agahnim 2); this is purely runtime overworld topology + rendering. No `kGeneratorVersion` / settings-hash / corpus cascade expected (verify at apply-time).
- **Verification:** playtest only (the playable-slot path has no automated test, per `CLAUDE.md`); the gfx spike + the fall-hole drop landing in the 0x1B Ganon room are the two things an F12 dump must confirm.
