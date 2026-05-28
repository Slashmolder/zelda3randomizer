## Why

The randomizer settings flow today lives inside the SNES screen: a custom in-game settings screen, an on-screen alphabet picker, and a from-scratch text-input layer (`src/rando/rando_textfield.{c,h}`, plus the settings-screen branch of `src/rando/rando.c` and `src/select_file.c`). That UI was the right Phase A choice when the game was the only window the player had, but it's a poor fit for the actual settings surface: ~20 enum/integer/bool fields with cross-field validation (Triforce Hunt's `pieces_required ≤ pieces_placed`, accessibility = `locations` forced by Completionist, crystal-count interactions), plus the share-string text entry. Doing all of that with a D-pad + on-screen alphabet inside a 256×224 framebuffer is slow, easy to misclick, and a maintenance tax — every new setting (Phase B will add ~10 more) needs new tile rendering, new cursor navigation, and new translation handling. PC players already sit in front of a desktop OS with a mouse, keyboard, and pixel-precise widgets; we should use them.

Replacing the in-game settings UI with a native OS window also unblocks features that don't fit a 256×224 screen at all: a live spoiler/placement browser, search-as-you-type location lookups, copy/paste share strings, multi-column display of all dungeon-item modes at once, and a real preset gallery.

## What Changes

- Add a second OS-native window (Dear ImGui, vendored under `third_party/imgui/`) that runs alongside the existing game window on Windows/Linux/macOS. The window is always present from app startup; closing it does not close the game and vice-versa.
- Render the full randomizer settings surface (all `RandoSettings` fields per `src/rando/rando_settings.h`, presets per `Settings_ApplyPreset`, share-string entry, asset-hash warning acknowledgement) in the native window using ImGui widgets.
- Add a shared state bridge (`src/rando/rando_window_bridge.{c,h}`) that owns the canonical pending-settings `RandoSettings` struct, exposes thread-safe read/apply primitives, and signals the game side when "Generate & start new slot" is requested.
- **BREAKING (PC builds)**: remove the in-game randomizer settings screen, on-screen alphabet picker, and `rando_textfield` text-input layer from Windows/Linux/macOS builds. These files (or the settings-screen entry points within them) are gated behind a new compile-time flag `Z3R_NATIVE_SETTINGS_WINDOW` that is on for PC and off for Switch.
- Keep the in-game settings UI fully intact on Nintendo Switch (Switch has no second-window concept; its text-input gap is already covered by `add-rando-switch-swkbd`). File-select's randomizer-slot rendering, slot banner with the 5-icon hash, and the recommended-features opt-in panel remain in-game on all platforms — only the *settings entry* path moves to the native window on PC.
- Persist the user's last-used settings to a new section in `zelda3.ini` (`[rando_window]`) so the window opens preselected to their previous configuration. The persisted blob is the same canonical-serialized bytes (`kSettingsCanonicalLen`, currently 28) that feed `settings_hash` — no parallel format. Persisted as hex (no base64 helper exists in the codebase today; see design §D9). Asset-hash "Always allow" decisions are NOT duplicated into this section; they continue to use the existing `[RandoAssetDecisions]` section (`src/config.c:535-552`, `src/select_file.c:2229-2245`) so there is one source of truth. `config.c` is read-only today (per the deferred-writer note at `src/select_file.c:2235`); shipping any of this persistence requires building the INI writer first — see design.md §D9.
- Add a settings-window-only "Generate now" button that runs the existing seed-generation pipeline on the game thread (not the UI thread) and reports progress/errors back into the ImGui window via the bridge.
- Add a read-only Spoiler/Placement viewer tab in the ImGui window (collapsible, hidden by default for race-mode slots) backed by the existing `rando_spoiler` writer output, so the design replaces UI surface area on the SNES side rather than just relocating it.
- Update `main.c`'s SDL init to create two windows (game + settings), pump events for both in the existing main loop, and route ImGui input to the settings window only. The game window's input handling and frame timing are unchanged.

## Capabilities

### New Capabilities

- `randomizer-native-window`: The Dear ImGui-based second OS window that owns the randomizer settings UI on PC builds. Covers window lifetime, two-window SDL event pumping, ImGui rendering integration with the existing SDL2+OpenGL stack, the settings-state bridge between window and game, persistence of last-used settings to `zelda3.ini`, the Generate-and-apply flow, the spoiler/placement viewer, and the Switch-fallback compile guard.

### Modified Capabilities

- `randomizer-ui`: The Text-input infrastructure, On-screen alphabet picker, and Settings screen requirements become **PC-conditional** — they remain in force on Switch builds and are removed on Windows/Linux/macOS builds, where the native window handles those responsibilities. The Three-slot file-select with kind-toggle, 5-icon visual hash, Slot banner with truncation, and Recommended-features opt-in panel requirements are unchanged on all platforms.

## Impact

- **New code surface**: `third_party/imgui/` (vendored ImGui + SDL2/OpenGL backends, ~10 .cpp/.h files), `src/rando/rando_window/` (new directory: window lifecycle, ImGui rendering of each settings panel, bridge), additions to `src/main.c` for the second window, additions to `config.c` for `[rando_window]` persistence.
- **Modified code**: `src/main.c` (SDL init for two windows, event-pump dispatch), `src/select_file.c` (rando-slot creation path on PC defers seed generation to the native window instead of opening the in-game settings screen), `src/rando/rando.c` (settings-screen module entry points compile out under `Z3R_NATIVE_SETTINGS_WINDOW`).
- **Removed code (PC builds only)**: in-game randomizer settings screen module and its tile rendering; on-screen alphabet picker; `src/rando/rando_textfield.{c,h}` excluded from PC build. Switch build retains all of these unchanged.
- **Build system**:
  - Makefile: add ImGui sources to the PC build, exclude on `PLATFORM=switch`. Add `-DZ3R_NATIVE_SETTINGS_WINDOW=1` on PC.
  - MSBuild `.vcxproj`: add ImGui source files; the existing C++ toolchain handles them (ImGui is C++).
  - Switch Makefile (`src/platform/switch/Makefile`): no change required; the new flag stays off.
  - CI: existing Linux/macOS `make -j$(nproc) zelda3` continues to work with the additional sources.
- **Dependencies**: Dear ImGui is the only new external dep. Vendored, no system package required. Adds ~3 MB to the source tree and a small (~200 KB) increase in the stripped binary.
- **Determinism / seed reproducibility**: zero impact. Settings still serialize through the same `Settings_CanonicalSerialize` path; `settings_hash` is identical whether the values were entered in the in-game UI or the native window.
- **Save format / sidecar / snapshot format**: unchanged. The native window only writes to in-memory `RandoSettings` and to `zelda3.ini` (preferences); it never touches `sram.dat`, `sram_rando.dat`, or the snapshot stream.
- **RAM-compare side-by-side mode**: unaffected — the native window does not run game code or write `g_ram`.
- **Race mode**: the native window's Spoiler/Placement viewer tab is hidden when the active slot is in race mode (same gate as the existing spoiler-suppression rule in `randomizer-save`).
- **Audit guard impact**: none — the audit guard scans game-code writes to `g_ram` cells, which this change does not introduce.
