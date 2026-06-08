## Why

In Inverted mode the player starts in the Dark World and the Magic Mirror only carries Light→Dark, so the *only* legitimate early way out to the Light World is a set of fixed "world-warp" tiles hidden under liftable rocks. That behavior existed in code but was undocumented in the spec, and a rendering bug left it broken on the most common entry path: the rocks were drawn only on full screen rebuilds (mirror / flute / cave-exit / save-and-quit), so **walking** onto a warp screen showed bare ground with nothing to lift. This change documents the warps as a spec requirement and records the walk-in rendering fix.

## What Changes

- Document the Dark World → Light World "under-rock" world-warps as a new requirement under the `randomizer-inverted-runtime` capability: their screens/positions, the world-flip on the revealed warp tile, glove accessibility, and the Light-World counterpart-rock removal.
- (Built this session) Fix: warp rocks now render on **walk-in**, not only on full screen rebuilds — `Overworld_EnsureInvertedWarpRock()` re-asserts the current warp screen's liftable rock tile each frame in the overworld player-control state.
- No placement, logic-graph, asset-format, or save-format change. Runtime-rendering + spec documentation only; Inverted-gated (vanilla / Open / Standard / Retro untouched).

## Capabilities

### New Capabilities
<!-- none -->

### Modified Capabilities
- `randomizer-inverted-runtime`: ADD a requirement documenting the DW→LW under-rock world-warps (warp records, overlay rocks, walk-in rendering, world-flip, Power-Glove accessibility, and Light-World hardcoded-rock removal).

## Impact

- **Code (already built):**
  - `src/overworld.c` — new `Overworld_EnsureInvertedWarpRock()` + call site in `Module09_00_PlayerControl`; existing Light-World hardcoded-rock removal in `Overworld_HandleOverlaysAndBombDoors`.
  - `src/rando/inverted_entrances.c` — `InvertedSecrets_Install` (type-0x82 warp records).
  - `src/rando/inverted_maps.c` / `inverted_maps_apply.c` — inverted overlay rocks (`Overworld_ApplyInvertedTiles`).
- **No corpus regeneration** (placement digests unaffected — runtime rendering only).
- **No asset/save-format change.** Behavior is gated on `RandomizerActive && world_state == Inverted`.
- Upstream references: ALTTPR `Rom.php` `setInvertedMode()` and z3randomizer `inverted.asm` `HardcodedRocks` (the vanilla Light-World rocks relocated to the Dark World under the warps).
