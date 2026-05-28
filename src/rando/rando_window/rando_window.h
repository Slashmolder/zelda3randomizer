// rando_window.{h,cpp} — the Dear ImGui native settings window (PC only).
// The single C++ surface in the project; every game-side call it makes is a C
// function. Excluded on Switch (lives under src/rando/rando_window/, and gated by
// Z3R_NATIVE_SETTINGS_WINDOW).
#ifndef ZELDA3_RANDO_RANDO_WINDOW_RANDO_WINDOW_H_
#define ZELDA3_RANDO_RANDO_WINDOW_RANDO_WINDOW_H_

#ifdef Z3R_NATIVE_SETTINGS_WINDOW

#include <SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

// Lifecycle. RandoWindow_Init takes the settings window + its dedicated GL context
// (created against the settings window, NEVER the game window — see rando_window.cpp).
void RandoWindow_Init(SDL_Window *window, SDL_GLContext gl_context);
void RandoWindow_BeginFrame(void);   // builds the ImGui frame for this game frame
void RandoWindow_Render(void);       // renders + swaps the settings window
void RandoWindow_Shutdown(void);

// Feeds one SDL event to the ImGui SDL2 backend. The host event pump routes
// every event whose windowID matches the settings window here (and ONLY here —
// such events never reach the game input path). `sdl_event` is a `const SDL_Event*`
// passed as void* to keep the header callable from the C host without dragging
// the C++ ImGui types into it.
void RandoWindow_ProcessEvent(const void *sdl_event);

// Show + target the window for a new randomizer slot (kind-toggle entry on PC).
void RandoWindow_OpenForNewSlot(int slot_index);

// Hide the settings window (settings-window close button / cancel). Clears any
// pending kind-toggle target so a hidden window doesn't leave a slot waiting.
void RandoWindow_Hide(void);

// Whether the settings window should currently be visible (driven by user show/hide).
bool RandoWindow_WantsShown(void);

#ifdef __cplusplus
}
#endif

#endif  // Z3R_NATIVE_SETTINGS_WINDOW
#endif  // ZELDA3_RANDO_RANDO_WINDOW_RANDO_WINDOW_H_
