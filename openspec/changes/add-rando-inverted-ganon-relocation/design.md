## Context

All Inverted runtime work currently lives on branch `inverted-relocation` (stack off `main`). That work deliberately kept Ganon in the Dark-World pyramid (area 0x5B) rather than relocating him under Hyrule Castle (area 0x1B) as ALTTPR does. The reason was concrete and is documented in `src/rando/inverted_maps_apply.c:43-57`: the screen-0x1B inverted "pyramid" overlay paints high pyramid map16 blocks that need Dark-World-pyramid gfx + palette, which cannot coexist with the Hyrule-Castle gfx loaded on screen 0x1B — so they rendered as garbage (mint-green tiles, seen on full screen rebuilds). The overlay was suppressed; the Ganon drop (`kFallHole` entrance 123 → room 0x000) stayed hardcoded to DW pyramid area 0x5B.

Because of that scoping decision, several ALTTPR `setInvertedMode()` writes were classified as non-gaps in `docs/inverted_alttpr_gaps.md` §C2 (the pyramid ExtraHole at area 0x1B; the pyramid/HC exit rows 0x06/0x37/0x3D + door 0x35) and the flute slot-8 override (0x5B→0x1B) was left vanilla. This change makes all of them live, gated on the Inverted world-state.

Authoritative source: ALTTPR `../alttp_vt_randomizer/app/Rom.php setInvertedMode()`; upstream asm `../z3randomizer/inverted.asm` + `darkworldspawn.asm` + `invertedmaps.asm`. Companion: the auto-memory note `inverted-entrance-topology-source`.

## Goals / Non-Goals

**Goals:**
- Relocate the Ganon fight to area 0x1B (under Hyrule Castle) in Inverted, byte-identical for non-Inverted.
- Render the screen-0x1B inverted overlay correctly on every entry method (the load-bearing piece).
- Reuse the existing `inverted_entrances.c` override subsystem for the exit-data + flute-slot rows already transcribed in prior research.
- Port the `SetDeathWorldChecked_Inverted` `.castle` death-respawn branch.

**Non-Goals:**
- Changing Inverted *logic / placement* reachability. Ganon's logic is already handled (`logic_parts/inverted/`, `Goal_IsCompletable` checks crystals + Agahnim 2); this is purely runtime overworld topology + rendering.
- Any change to Open / Standard / Retro Ganon behavior.
- The crystal→HC-Tower door-gate exactness (a separate gap, §B1 of the gaps doc).

## Decisions

- **Decision: resolve the gfx/palette coexistence with an apply-time spike before wiring anything else.** This is the only piece that can block the whole change, so it is sequenced first (tasks §1). Candidate approaches to evaluate: (a) load an Inverted-specific gfx-set / sprite-gfx pair on screen 0x1B that carries both castle and pyramid-hole blocks; (b) remap the pyramid-hole map16 blocks to tiles/palette slots that are already valid under the castle gfx; (c) extract a castle-compatible variant of the pyramid-hole blocks into the inverted maps data. Pick the smallest approach that renders cleanly on full-rebuild *and* walking-scroll. Alternative rejected: shipping the overlay as-is (the original garbage render — that is exactly what was suppressed).
- **Decision: relocate the fall-hole via the asset/extraction pipeline, not a runtime override.** The `inverted_entrances.c` shadow buffers are fixed-size and cannot grow a sorted array; `kFallHole_*` (assets 127/128/129) are built sorted-by-entrance-id (`assets/compile_resources.py`). Add the ExtraHole entry (Area=0x1B, Map16=0x0140, Entrance=0x7B) in the extraction so the sort places it correctly, and bump the hardcoded `i = 36/2` scan bound in `Overworld_GetPitDestination` (`src/overworld.c:3299-3321`). Gate the *consumption* of the new entry on the Inverted world-state so non-Inverted fall-through is unaffected. Alternative rejected: runtime override of 127/128/129 — blocked by the fixed-size shadow + sorted indexing.
- **Decision: reuse `inverted_entrances.c` for the exit-data + flute-slot rows.** Exits 0x06/0x37/0x3D + door 0x35 (area/pos/id) and `kBirdTravel_ScreenIndex[8]` 0x5B→0x1B are already transcribed (see prior research / the gaps doc) and fit the existing override-table mechanism (assets 124/125/126/130/131/132-138/113). Just add the rows + any new `g_shadows[]` registrations.
- **Decision: port the death-respawn `.castle` branch into `Death_Func15`.** Currently `src/messaging.c:797-799` forces `savegame_is_darkworld = 0x40` on Inverted death; once Ganon is at the LW castle, death-to-Ganon (the GanonPyramidRespawn-equivalent condition) must force the Light World instead — mirror `SetDeathWorldChecked_Inverted` `.castle`.

## Risks / Trade-offs

- **[Screen-0x1B gfx/palette coexistence is intractable or large]** → The spike (tasks §1) is gated first; if no clean approach renders without corruption, the change is paused and Ganon stays in the DW pyramid (revert = re-assert the current suppression). Do not ship a partial/garbage overlay.
- **[Fall-hole pipeline change invalidates `zelda3_assets.dat` + may move placement digests]** → Ganon's drop is game data, not placement, so the corpus placement digests should be unchanged; verify with `run_rando_corpus.py` and bump `kGeneratorVersion` only if a digest moves. The asset hash (`vanilla_assets_hash.h`) will change — regenerate.
- **[Reverses recorded non-gaps]** → On completion, update `docs/inverted_alttpr_gaps.md` (move §A1 to "done", drop the §C2 non-gap entries) and the memory note `inverted-entrance-topology-source` so the record stays accurate.
- **[Death-respawn `.castle` condition mis-fires and dumps a non-Ganon death into the LW]** → Gate precisely on the Ganon-fight respawn condition (indoors + DungeonID==0xFF + room 0x000 + respawn flag), not a broad Inverted-death gate; verify the normal DW death respawn (the existing `0x40` path) is untouched.

## Migration Plan

Runtime feature gated on the Inverted world-state; no save-data migration. Existing Inverted slots load unchanged (the world_state byte already drives the gate). Rollback is a straight revert (re-suppress the 0x1B overlay, restore the DW-pyramid fall-hole bound, drop the exit/flute rows, restore the `0x40` death respawn). Asset-blob consumers must regenerate `zelda3_assets.dat` after the fall-hole extraction change.

## Open Questions

- **The gfx/palette technique for screen 0x1B** (the spike) — unresolved until tasks §1; it determines feasibility and the bulk of the effort.
- **Does the existing Inverted logic graph route Ganon access through an 0x1B location or assume 0x5B?** Confirm `logic_parts/inverted/` + `Goal_IsCompletable` don't encode the DW-pyramid position in a way that needs updating (likely fine — logic gates on crystals + Agahnim 2, not a screen id — but verify).
- **Does relocating the fall-hole affect the Chris-Houlihan fallback** (the area-0x1B fall currently lands there)? Confirm the new ExtraHole entry takes precedence and the fallback is unaffected for non-Inverted.
