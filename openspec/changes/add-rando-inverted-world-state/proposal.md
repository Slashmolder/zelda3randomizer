## Why

Phase A shipped Open + Standard world-states for 7 goals and 4 item-pool difficulties. **Inverted world-state is deferred to Phase B** per `add-randomizer-support / tasks.md §14.2` and the explicit `select_file.c` settings-screen world-state-picker gate that Phase A re-scoped (`tasks.md §14.1b`).

Inverted is a major community variant. Link starts in the Dark World as a bunny without the Moon Pearl; the overworld topology is flipped (Light/Dark World tile sources swap); the only path to gain Moon Pearl is to follow a different progression sequence than Standard/Open. ALTTPR's Inverted region definitions live at `../alttp_vt_randomizer/app/Region/Inverted/` — **verified 2977 lines across 24 PHP files** (13 top-level + 11 in `DarkWorld/` and `LightWorld/` subdirectories; recursive count). This is the largest single chunk of Phase B logic translation work.

Phase A also has an open bug (`audit_phase_a1.md` Bug #12, "STILL OPEN") that's an Inverted blocker: `Rando_TryGrantStartingInventory` exists at `src/rando/rando_placement.c:1360` but has no production caller. Inverted Link needs `MoonPearl + MagicMirror` injected at game-start per the bunny-state starting-inventory contract — without the call site, Inverted seeds are unplayable. Open mode is unaffected because its starting inventory grants nothing extra.

## What Changes

- **Translate `app/Region/Inverted/*.php` (2977 lines, recursive) into YAML** under `assets/rando/logic_parts/`. Mirror the Standard structure: one YAML file per region with location predicates referencing the named macros in `assets/rando/macros.yaml`. Per-macro source-line citations in `audit.md §"Macro provenance"` (Inverted-specific section).
- **Declare `LinksHouse_Inverted` start region.** Populate `kRandoStartRegionByWorldState[Inverted]` in `src/rando/rando_logic.c` (currently `0xFFFF`).
- **Activate the RegionRemap overlay.** Scaffolding is already in `src/rando/rando_logic.c`; `Rando_SetRegionRemap` is callable. This change populates the Inverted overlay table so Light World ↔ Dark World region accessors yield the inverted topology when `world_state == Inverted`.
- **Populate `world_state_filter` for Inverted-specific and Inverted-exclusion locations.** Currently all 237 locations have `filter=0` (universal). Inverted has a small number of locations that exist only in that world-state, and a few that are excluded (e.g., locations behind Dark World entrances that route differently).
- **Wire `Rando_TryGrantStartingInventory` at the game-start hook** to fix Phase A1 audit Bug #12. Two candidate call sites per audit: end of `Module05_LoadFile` or first `Module06_PreDungeon`. Decision recorded in this change's design.md.
- **Un-gate Inverted in the settings-screen picker** at `src/select_file.c:2520` (per Phase A §14.1b re-scope; the picker is currently capped at Standard).
- **Regenerate the regression corpus** via `assets/scripts/bump_rando_corpus.py`. `kGeneratorVersion` advances.
- **Update spoiler region grouping** so Inverted region names appear correctly in JSON + text spoilers.

## Capabilities

### New Capabilities

(none)

### Modified Capabilities

- `randomizer-logic`: ADDED Requirements for the Inverted region graph, RegionRemap overlay activation contract, and `world_state_filter` semantics. MODIFIED Requirement on the `kRandoStartRegionByWorldState` table to require an entry per declared world-state.
- `randomizer-placement`: ADDED Requirement for the starting-inventory call site (Bug #12 wiring). MODIFIED Requirement on "Starting inventory injection" to reference the new call site explicitly.
- `randomizer-core`: MODIFIED Requirement on `BuildItemPool` to support the Inverted item pool (likely no item changes vs. Open, but verified against ALTTPR's `World/Inverted.php`).
- `randomizer-ui`: ADDED Requirement "World-state picker accepts Inverted" — un-gates the picker at `select_file.c:2520` without modifying the Phase A "Settings screen" Requirement (sidesteps archive sequencing conflict with the parallel `add-rando-retro-world-state` change).

## Impact

- **YAML authoring**: ~15-20 new `logic_parts/*.yaml` files for Inverted regions (rough scaling from Standard's 16 files). Each averages 100-200 lines.
- **Code**: `src/rando/rando_logic.c` (RegionRemap activation, start-region table), `src/rando/rando_placement.c` (starting-inventory call wiring), `src/select_file.c` (picker un-gate), `assets/rando/macros.yaml` (any Inverted-specific macros).
- **Effort**: **4-6 weeks of focused work** (source doc estimated 3-4 weeks but undercounted the PHP source by ~2x — actual is 2977 lines, not 1500).
- **Regression risk**: `kGeneratorVersion` bumps, the entire corpus regenerates, and existing Open + Standard digests should remain byte-identical (the RegionRemap overlay only activates when `world_state == Inverted`). CI verifies non-Inverted seeds unchanged.
- **No dependency on other Phase B slices**; can ship alongside #4b Retro (independent) and #2 trackers (helpful for verification).
- **Translation discipline** per `add-randomizer-support / audit.md §0.10`: hand-translate PHP closures to YAML predicates with per-macro source-line citations. MIT attribution preserved in `NOTICE`.

## Status

**Fully authored** as of 2026-05-26. Promoted from initial stub after the user requested completing every Phase B plan. design.md captures the YAML directory layout decision (mirror PHP), RegionRemap overlay shape, Bug #12 call-site choice (end of `Module05_LoadFile`), and world_state_filter encoding. Priming directory at `assets/rando/logic_parts/inverted/` holds 24 stub YAML files (one per upstream PHP source). See [README.md](README.md) for the file index.
