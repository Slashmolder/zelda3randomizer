// imgui_host.{h,cpp} — a tiny multi-window Dear ImGui host (PC only).
//
// The native settings window (rando_window.cpp) was the project's first ImGui
// surface and hard-codes a single window/context. The rich tracker windows need
// SEVERAL independent OS windows, so this host generalizes the lifecycle: each
// Z3RWindow owns its own SDL_Window + GL context + ImGuiContext + backend init.
//
// ONE ImGuiContext PER WINDOW. The ImGui 1.91.6 SDL2/OpenGL3 backends store their
// state in io.BackendPlatformUserData / io.BackendRendererUserData (per-context),
// so N contexts are clean as long as SetCurrentContext() selects the right one
// before any ImGui/backend call. This host is a strict "good citizen": every
// entry point saves the ImGui current-context (and the SDL GL current
// window/context) on entry and restores it on exit, so the untouched settings
// window — which relies on ITS context being globally-current — keeps working.
//
// GL-context discipline mirrors rando_window.cpp: make the window's own GL
// context current before any GL/ImGui call, and the host restores the game's
// context after RenderAll. The OpenGL3 backend uses its own embedded loader, so
// this TU must NEVER include third_party/gl_core (loader isolation).
#ifndef ZELDA3_RANDO_RANDO_WINDOW_IMGUI_HOST_H_
#define ZELDA3_RANDO_RANDO_WINDOW_IMGUI_HOST_H_

#ifdef Z3R_NATIVE_SETTINGS_WINDOW

#include <SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Z3RWindow Z3RWindow;

// Called once per frame for a visible window, between ImGui::NewFrame() and
// ImGui::Render(). The callback owns its Begin()/End() (use the full-window
// helper in the tracker module). `user` is the pointer passed to Z3RHost_Create.
typedef void (*Z3RWindowDrawFn)(void *user);

// Create a hidden, resizable ImGui window with its own GL + ImGui context.
// Returns NULL on failure (window/context creation). Safe to call after the
// settings window is initialized; the previously-current ImGui/GL context is
// restored before returning.
Z3RWindow *Z3RHost_Create(const char *title, int w, int h,
                          Z3RWindowDrawFn draw, void *user);

void Z3RHost_Show(Z3RWindow *win);
void Z3RHost_Hide(Z3RWindow *win);
void Z3RHost_ToggleShown(Z3RWindow *win);
bool Z3RHost_IsShown(Z3RWindow *win);

// Restack visible host windows with the active game window without changing
// visibility, geometry, or focus. On Win32 this places host windows directly
// behind `anchor`; other platforms leave the stack unchanged.
void Z3RHost_RestackShownBehind(SDL_Window *anchor);

// Route one SDL event. Returns true iff the event targeted a host window (the
// caller should then `continue` and NOT pass it to the game input path). The
// window's own close button hides the window (does not quit the app).
bool Z3RHost_ProcessEvent(const SDL_Event *event);

// Render every visible host window. Self-contained: saves the current SDL GL
// window/context + ImGui context on entry and restores them on exit, so the
// game renderer and the settings window are unaffected. Call once per frame
// after the game frame is drawn.
void Z3RHost_RenderAll(void);

// Geometry persistence (mirrors RandoWindow_ApplyGeometry/GetGeometry). Apply
// BEFORE the window is first shown; the rect is clamped to on-screen.
void Z3RHost_ApplyGeometry(Z3RWindow *win, int x, int y, int w, int h);
void Z3RHost_GetGeometry(Z3RWindow *win, int *x, int *y, int *w, int *h);

// Tear down all host windows (contexts, GL contexts, SDL windows). Idempotent.
void Z3RHost_Shutdown(void);

#ifdef __cplusplus
}
#endif

#endif  // Z3R_NATIVE_SETTINGS_WINDOW
#endif  // ZELDA3_RANDO_RANDO_WINDOW_IMGUI_HOST_H_
