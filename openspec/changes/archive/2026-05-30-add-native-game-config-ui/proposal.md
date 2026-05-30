## Why

The native Dear ImGui settings window shipped (`add-rando-native-settings-window`) owns the **randomizer** settings surface on PC. Everything else a player can configure — controller mapping, keyboard hotkeys, video, audio, and the `[Features]` gameplay toggles — still requires hand-editing `zelda3.ini` in a text editor. That is a poor experience: the key-name and gamepad-button grammar is undocumented in-app, a typo silently drops a binding (`fprintf(stderr, "Unknown key…")` to a console nobody sees), conflicts are invisible until two actions fight, and there is no way to discover that `features0` toggles even exist. We already pay for a full ImGui window, a state bridge, a two-window event pump, and sidecar persistence; this change reuses all of it to make the game self-configuring.

This also lands the **in-place `Config_WriteIniFile` rewriter** that `add-rando-native-settings-window` explicitly deferred (its design.md §D9: *"the in-place `Config_WriteIniFile` rewriter is deferred… replace only the relevant sections in place, preserving comments and unknown sections verbatim"*). That writer is the missing primitive for any settings UI that persists to the user's canonical config rather than a sidecar.

## What Changes

- **Add a "Game Settings" top-level tab** to the existing `Z3R Settings` ImGui window, containing a nested tab bar: **Controls** (keyboard), **Controller** (gamepad), **Video**, **Audio**, **Gameplay** (the `[Features]` bits + `[General]` toggles). The randomizer tabs are unchanged.
- **Make the settings window openable for configuration alone**, independent of the randomizer flow. A new bindable command `kKeys_OpenSettings` (default `` ` `` / backquote) shows the window in *config mode* (no rando slot targeted; the Generate row is hidden). The existing "New Randomizer" kind-toggle entry continues to open it targeted at a slot.
- **Refactor the keybinding storage into an editable model.** Today `config.c` parses the INI directly into a lossy lookup hash (`keymap_hash`, `joymap_ents`) with no reverse path and no rebuild. Introduce authoritative editable arrays (`g_keybind_kbd[kKeys_Total]`, `g_keybind_pad[kKeys_Total]`) that are the source of truth; the lookup hash becomes a derived index rebuilt by a new `Config_RebuildKeymap()`. This enables **live rebinding without restart** and a faithful round-trip to the INI.
- **Implement `Config_WriteIniFile(const char *path)`** — a comment-preserving, in-place INI rewriter that updates only the keys this UI manages (across `[KeyMap]`, `[GamepadMap]`, `[Graphics]`, `[Sound]`, `[General]`, `[Features]`) and leaves comments, blank lines, unknown keys, and unknown sections (incl. `[Randomizer]`, `[RandoAssetDecisions]`) byte-for-byte intact. Writes atomically (tmp + flush + rename), mirroring `Config_SaveRandoWindowIni`. The target is the file `ParseConfigFile` actually loaded (`zelda3.user.ini` if present, else `zelda3.ini`).
- **Interactive binding capture.** A "Rebind" affordance per command captures the next key (or gamepad button) pressed while the settings window is focused, with on-the-spot conflict resolution (steal-with-warning) so the keymap never contains a duplicate.
- **Live-apply where it is cheap and safe** (keybindings, `features0` gameplay bits, window scale, fullscreen, MSU volume, perf-display, frame-delay); everything else (audio device, renderer/output method, aspect ratio, language, shader, Link graphics) is written to the INI and clearly labeled **"⟳ restart required."**
- **Document** the new tab and the open hotkey in `README.md`.

## Capabilities

### New Capabilities

- `game-config-ui`: The native ImGui surface for configuring non-randomizer game settings (controls, controller, video, audio, gameplay features), the editable keybinding model with live rebuild, interactive binding capture with conflict resolution, the comment-preserving in-place `Config_WriteIniFile` writer, the config-mode window open path, and the live-apply-vs-restart policy.

### Modified Capabilities

- `randomizer-native-window`: The window gains a config-only open mode (`kKeys_OpenSettings`) and the "Game Settings" tab; the Generate row is conditioned on a targeted slot so the window is meaningful when opened for configuration alone. No change to the randomizer panels, bridge, or generate flow.

## Impact

- **New code**: `src/rando/rando_window/game_config_panels.cpp` (ImGui panels), keybind-model + `Config_RebuildKeymap()` + `Config_WriteIniFile()` + reverse name/encode helpers in `src/config.c`/`.h`, `kKeys_OpenSettings` wiring in `src/main.c`.
- **Modified code**: `src/config.c`/`config.h` (editable model refactor — the parse path now also populates the arrays; the hash is derived), `src/rando/rando_window/rando_window.cpp`/`.h` (new tab, config-mode open, generate-row gating, key-capture hook), `src/main.c` (open hotkey, gamepad-capture interception), build files, `README.md`.
- **Build system**: add `game_config_panels.cpp` to the Makefile PC source list and the `.vcxproj`; Switch excludes it (lives under `src/rando/rando_window/`, which the non-recursive Switch glob does not pick up). No new dependency (ImGui already vendored).
- **Determinism / seed reproducibility**: **zero.** This change touches no `RandoSettings`, no `Settings_Canonical*`, no `kGeneratorVersion`. The regression corpus and `--rando-selftest` are unaffected.
- **Save / sidecar / snapshot formats**: unchanged. `Config_WriteIniFile` writes the user's INI; it never touches `sram.dat`, `sram_rando.dat`, the snapshot stream, or `saves/rando_window.ini` (which keeps its own writer).
- **RAM-compare side-by-side mode**: live-applying a `features0` bit retargets `g_wanted_zelda_features`, which the frame driver mirrors to `g_ram+0x64c` and records as a StateRecorder patch **only on non-replay frames** (`zelda_rtl.c`). While *recording* (incl. RAM-compare), the patch is logged for the emulated side too, so divergence is handled exactly as it is when `features0` is set at startup. While *replaying* the mirror does not run, so live feature/keymap apply is **gated off during `replay_mode`** (persisted to INI immediately, applied after replay/restart). See design §D6.
- **Audit guard**: the guard scans `src/**/*.c` (excluding paths containing `rando`) — so `src/config.c` and `src/main.c`, where the new **C** helpers live, ARE scanned, while the `.cpp` panels are not. The new helpers write no audit-tracked `g_ram` cell (live `features0` goes through `g_wanted_zelda_features`, the existing feature-sync path, not a direct `g_ram` write), so the guard has nothing to flag — it is not bypassed.
- **Switch**: unaffected — the panel file and the open hotkey's window calls are under `Z3R_NATIVE_SETTINGS_WINDOW`; the keymap enum entry is unconditional (index stability) but its handler no-ops without the window.
