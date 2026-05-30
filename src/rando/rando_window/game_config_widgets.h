// game_config_widgets.h — C-linkage interface to the native game-config panels
// (Controls / Controller / Video / Audio / Gameplay). Implemented in
// game_config_panels.cpp. Callable from the C++ window TU (rando_window.cpp) and
// from the C host (main.c). PC only (Z3R_NATIVE_SETTINGS_WINDOW).
#ifndef ZELDA3_RANDO_RANDO_WINDOW_GAME_CONFIG_WIDGETS_H_
#define ZELDA3_RANDO_RANDO_WINDOW_GAME_CONFIG_WIDGETS_H_

#ifdef Z3R_NATIVE_SETTINGS_WINDOW

#ifdef __cplusplus
extern "C" {
#endif

// Draw the "Game Settings" nested tab bar (Controls/Controller/Video/Audio/
// Gameplay) + the Apply/Revert/Reset action row. Called from rando_window.cpp
// inside the top-level "Game Settings" tab item.
void GameConfig_RenderTab(void);

// Draw the "Debug" tab — a live inventory/equipment/consumables editor that
// writes Link's g_ram save block directly (no INI/Apply). Gated to in-game,
// non-replay, non-emulator-attached. Called from rando_window.cpp inside the
// top-level "Debug" tab item.
void GameDebug_RenderTab(void);

// Re-sync the working copy from the live g_config + keybind model. Call when the
// window is (re)opened so the panels show current values.
void GameConfig_NotifyWindowOpened(void);

// Game-thread apply. The Apply button only raises a flag; the host calls these
// at the start of a game frame (game GL context current) to commit the working
// copy, rebuild the keymap, live-apply the safe subset, and write the INI.
bool GameConfig_HasPendingApply(void);
void GameConfig_ApplyPending(void);

// Tick the capture timeout (call once per frame with SDL_GetTicks()).
void GameConfig_CaptureTick(unsigned int now_ticks);

// Keyboard binding capture. The window's event pump asks WantsKeyCapture(); when
// true it routes the next non-modifier SDL_KEYDOWN here (Escape cancels) instead
// of to ImGui. keycode/keymod are the SDL event's keysym.sym / keysym.mod.
bool GameConfig_WantsKeyCapture(void);
void GameConfig_FeedCapturedKey(int sdl_keycode, int sdl_keymod);

// Gamepad binding capture. The host asks IsCapturingGamepad(); when true it
// routes the next SDL_CONTROLLERBUTTONDOWN here (button = kGamepadBtn_*,
// held_mask = currently-held buttons bitmask) instead of to the game.
bool GameConfig_IsCapturingGamepad(void);
void GameConfig_FeedCapturedButton(int button, unsigned int held_mask);

// Cancel any armed capture (focus loss / window hide).
void GameConfig_CancelCapture(void);

#ifdef __cplusplus
}
#endif

#endif  // Z3R_NATIVE_SETTINGS_WINDOW
#endif  // ZELDA3_RANDO_RANDO_WINDOW_GAME_CONFIG_WIDGETS_H_
