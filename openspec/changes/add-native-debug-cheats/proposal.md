## Why

The native settings window now configures the game, but there's no in-app way to edit Link's inventory for testing/debugging — you either hand-edit SRAM or use the coarse `PatchCommand` cheats (full life / keys / equipment). A live inventory editor makes playtesting (especially randomizer logic and the gameplay-feature toggles we just added) far faster: grant the exact items a check needs, top up consumables, or set up a specific state without grinding to it.

The infrastructure is already in place (the ImGui window, tabs, widgets, and the game-thread discipline), and the inventory is a clean, fully-documented `g_ram` block (`$7EF3xx`) the HUD already reads live — so this is a focused data-entry tab, not new architecture.

## What Changes

- Add a **Debug** top-level tab to the Z3R Settings window (PC only, `Z3R_NATIVE_SETTINGS_WINDOW`) with a **live inventory/equipment/consumables editor**: rupees, bombs, arrows, keys, magic, hearts (containers + refill), heart pieces; sword/shield/armor/gloves tiers; bow/boomerang/flute tiers; item toggles (hookshot, rods, hammer, lamp, capes, boots, flippers, moon pearl, medallions, etc.); a 3-way mushroom/powder selector (shared byte); 4 bottle-content selectors; an advanced collapsed section for pendants/crystals; and quick-action buttons.
- Widgets **read `g_ram` live** for display and **write it on change**, so edits reflect on the HUD next frame.
- All writes are **hard-gated**: only when a save is loaded and Link is in the world, and **never** during snapshot replay or while the original ROM is attached for side-by-side RAM compare. Add a `ZeldaIsEmulatorAttached()` accessor for the last clause.
- Every value is range-clamped via fixed-option combos / bounded sliders so no out-of-range byte can reach a game-code table index.

## Capabilities

### Modified Capabilities

- `game-config-ui`: gains a Debug tab with a live inventory editor. The editor is a runtime `g_ram` editor (distinct from the config tabs' Apply/INI flow): it does not persist to the INI or any save file; edited state lands in live RAM and only reaches SRAM if the player saves normally.

## Impact

- **New code, no new files:** `GameDebug_RenderTab()` + cheat read/write/gate helpers added to the existing `src/rando/rando_window/game_config_panels.cpp`; the Debug top-level tab added in `src/rando/rando_window/rando_window.cpp`; declaration in `game_config_widgets.h`. `ZeldaIsEmulatorAttached()` added to `src/zelda_rtl.{c,h}`.
- **Build:** no Makefile/`.vcxproj` change (no new translation unit). No Switch impact.
- **Determinism / seed reproducibility:** zero — runtime `g_ram` edits only; no `RandoSettings`/canonical/`kGeneratorVersion` touch. Corpus + `--rando-selftest` unaffected.
- **Save / sidecar / snapshot formats:** unchanged; edits are live RAM, persisted only by a normal in-game save.
- **RAM-compare:** the editor is disabled whenever the emulator is attached, so it can never diverge the comparator.
- **Audit guard:** none — the `g_ram` writes live in a `.cpp` under `src/rando/`, which the guard neither scans (`.cpp`) nor includes (`rando` path excluded).
