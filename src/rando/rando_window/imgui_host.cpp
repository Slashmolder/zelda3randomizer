// imgui_host.cpp — multi-window Dear ImGui host (PC only). See imgui_host.h for
// the design contract (one ImGuiContext per window; strict save/restore so the
// single-context settings window is unaffected).
#ifdef Z3R_NATIVE_SETTINGS_WINDOW

#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_opengl3.h"

#include <SDL.h>
#include <SDL_opengl.h>
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <SDL_syswm.h>
#endif

#include "imgui_host.h"

// ---- GL entry points we call directly (clear + viewport). Resolved once via
// SDL_GL_GetProcAddress so this TU pulls in no GL loader (loader isolation). ---
typedef void(APIENTRY *PFN_glViewport)(GLint, GLint, GLsizei, GLsizei);
typedef void(APIENTRY *PFN_glClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
typedef void(APIENTRY *PFN_glClear)(GLbitfield);
static PFN_glViewport s_glViewport = nullptr;
static PFN_glClearColor s_glClearColor = nullptr;
static PFN_glClear s_glClear = nullptr;
static bool s_gl_resolved = false;

#ifdef __APPLE__
static const char *kGlslVersion = "#version 150";  // GL 3.2 core (macOS)
#else
static const char *kGlslVersion = "#version 130";  // GL 3.0
#endif

struct Z3RWindow {
  SDL_Window *win;
  SDL_GLContext gl;
  ImGuiContext *ctx;
  Z3RWindowDrawFn draw;
  void *user;
  bool in_use;
  bool shown;
};

#define kZ3RHostMaxWindows 8
static Z3RWindow s_windows[kZ3RHostMaxWindows];

static void ResolveGlOnce() {
  if (s_gl_resolved) return;
  s_glViewport = (PFN_glViewport)SDL_GL_GetProcAddress("glViewport");
  s_glClearColor = (PFN_glClearColor)SDL_GL_GetProcAddress("glClearColor");
  s_glClear = (PFN_glClear)SDL_GL_GetProcAddress("glClear");
  s_gl_resolved = true;
}

Z3RWindow *Z3RHost_Create(const char *title, int w, int h,
                          Z3RWindowDrawFn draw, void *user) {
  Z3RWindow *slot = nullptr;
  for (int i = 0; i < kZ3RHostMaxWindows; i++) {
    if (!s_windows[i].in_use) { slot = &s_windows[i]; break; }
  }
  if (!slot) return nullptr;  // table full

  // Save whatever GL/ImGui context is current so we leave it untouched.
  SDL_Window *prev_win = SDL_GL_GetCurrentWindow();
  SDL_GLContext prev_gl = SDL_GL_GetCurrentContext();
  ImGuiContext *prev_ctx = ImGui::GetCurrentContext();

  // GL attributes (same profile as the settings window). Applied at next
  // SDL_GL_CreateContext.
#ifdef __APPLE__
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
#else
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#endif
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

  SDL_Window *win = SDL_CreateWindow(
      title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h,
      SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI |
          SDL_WINDOW_HIDDEN);
  if (!win) goto fail_restore;

  {
    SDL_GLContext gl = SDL_GL_CreateContext(win);
    if (!gl) {
      SDL_DestroyWindow(win);
      goto fail_restore;
    }
    SDL_GL_MakeCurrent(win, gl);

    IMGUI_CHECKVERSION();
    ImGuiContext *ctx = ImGui::CreateContext();  // also sets current
    ImGui::SetCurrentContext(ctx);
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;  // don't write imgui.ini
    // Tracker windows never drive gamepad navigation; keep them off the game's
    // gamepad so N per-frame NewFrames don't contend with game input.
    io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
    ImGui::StyleColorsDark();

    ImGui_ImplSDL2_InitForOpenGL(win, gl);
    ImGui_ImplOpenGL3_Init(kGlslVersion);
    ImGui_ImplSDL2_SetGamepadMode(ImGui_ImplSDL2_GamepadMode_Manual, nullptr, 0);

    ResolveGlOnce();

    slot->win = win;
    slot->gl = gl;
    slot->ctx = ctx;
    slot->draw = draw;
    slot->user = user;
    slot->in_use = true;
    slot->shown = false;
  }

  // Restore the caller's contexts. Precondition: callers invoke Z3RHost_Create
  // while a GL context is current (the settings context, set up just before
  // Trackers_Init in main.c), so prev_gl is non-null and the restore runs. If a
  // future caller has no GL context current, this leaves the new tracker context
  // current — harmless under the SDL software game renderer, but the contract is
  // "call with a context current".
  ImGui::SetCurrentContext(prev_ctx);
  if (prev_gl) SDL_GL_MakeCurrent(prev_win, prev_gl);
  return slot;

fail_restore:
  ImGui::SetCurrentContext(prev_ctx);
  if (prev_gl) SDL_GL_MakeCurrent(prev_win, prev_gl);
  return nullptr;
}

void Z3RHost_Show(Z3RWindow *win) {
  if (!win || !win->in_use) return;
  SDL_ShowWindow(win->win);
  SDL_RaiseWindow(win->win);
  win->shown = true;
}

void Z3RHost_Hide(Z3RWindow *win) {
  if (!win || !win->in_use) return;
  SDL_HideWindow(win->win);
  win->shown = false;
}

void Z3RHost_ToggleShown(Z3RWindow *win) {
  if (!win || !win->in_use) return;
  if (win->shown) Z3RHost_Hide(win);
  else Z3RHost_Show(win);
}

bool Z3RHost_IsShown(Z3RWindow *win) {
  return win && win->in_use && win->shown;
}

#if defined(_WIN32)
static HWND Win32HandleForWindow(SDL_Window *win) {
  if (!win) return NULL;
  SDL_SysWMinfo info;
  SDL_VERSION(&info.version);
  if (!SDL_GetWindowWMInfo(win, &info)) return NULL;
  if (info.subsystem != SDL_SYSWM_WINDOWS) return NULL;
  return info.info.win.window;
}
#endif

void Z3RHost_RestackShownBehind(SDL_Window *anchor) {
#if defined(_WIN32)
  HWND insert_after = Win32HandleForWindow(anchor);
  if (!insert_after) return;
  for (int i = 0; i < kZ3RHostMaxWindows; i++) {
    Z3RWindow *w = &s_windows[i];
    if (!w->in_use || !w->shown) continue;
    if (SDL_GetWindowFlags(w->win) & SDL_WINDOW_MINIMIZED) continue;
    HWND hwnd = Win32HandleForWindow(w->win);
    if (!hwnd) continue;
    SetWindowPos(hwnd, insert_after, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    insert_after = hwnd;
  }
#else
  (void)anchor;
#endif
}

// Extract the target window id from a window-targeted event (mirrors main.c's
// settings routing). `is_windowed` is false for global events (SDL_QUIT,
// controller/joystick, audio device) that must reach the game path.
static Uint32 EventWindowId(const SDL_Event *e, bool *is_windowed) {
  *is_windowed = true;
  switch (e->type) {
  case SDL_WINDOWEVENT:    return e->window.windowID;
  case SDL_KEYDOWN:
  case SDL_KEYUP:          return e->key.windowID;
  case SDL_TEXTINPUT:      return e->text.windowID;
  case SDL_TEXTEDITING:    return e->edit.windowID;
  case SDL_MOUSEMOTION:    return e->motion.windowID;
  case SDL_MOUSEBUTTONDOWN:
  case SDL_MOUSEBUTTONUP:  return e->button.windowID;
  case SDL_MOUSEWHEEL:     return e->wheel.windowID;
  case SDL_DROPFILE:
  case SDL_DROPTEXT:
  case SDL_DROPBEGIN:
  case SDL_DROPCOMPLETE:   return e->drop.windowID;
  default:                 *is_windowed = false; return 0;
  }
}

bool Z3RHost_ProcessEvent(const SDL_Event *event) {
  bool is_windowed = false;
  Uint32 wid = EventWindowId(event, &is_windowed);
  if (!is_windowed || wid == 0) return false;

  for (int i = 0; i < kZ3RHostMaxWindows; i++) {
    Z3RWindow *w = &s_windows[i];
    if (!w->in_use) continue;
    if (SDL_GetWindowID(w->win) != wid) continue;

    if (event->type == SDL_WINDOWEVENT &&
        event->window.event == SDL_WINDOWEVENT_CLOSE) {
      Z3RHost_Hide(w);  // close button hides; never quits the app
      return true;
    }
    ImGuiContext *prev_ctx = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(w->ctx);
    ImGui_ImplSDL2_ProcessEvent(event);
    ImGui::SetCurrentContext(prev_ctx);
    return true;
  }
  return false;  // not ours → game path
}

void Z3RHost_RenderAll(void) {
  SDL_Window *prev_win = SDL_GL_GetCurrentWindow();
  SDL_GLContext prev_gl = SDL_GL_GetCurrentContext();
  ImGuiContext *prev_ctx = ImGui::GetCurrentContext();

  for (int i = 0; i < kZ3RHostMaxWindows; i++) {
    Z3RWindow *w = &s_windows[i];
    if (!w->in_use || !w->shown) continue;

    SDL_GL_MakeCurrent(w->win, w->gl);
    ImGui::SetCurrentContext(w->ctx);

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    if (w->draw) w->draw(w->user);

    ImGui::Render();
    int dw = 0, dh = 0;
    SDL_GL_GetDrawableSize(w->win, &dw, &dh);
    if (s_glViewport) s_glViewport(0, 0, dw, dh);
    if (s_glClearColor) s_glClearColor(0.09f, 0.09f, 0.11f, 1.0f);
    if (s_glClear) s_glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    SDL_GL_SwapWindow(w->win);
  }

  ImGui::SetCurrentContext(prev_ctx);
  if (prev_gl) SDL_GL_MakeCurrent(prev_win, prev_gl);
}

void Z3RHost_ApplyGeometry(Z3RWindow *win, int x, int y, int w, int h) {
  if (!win || !win->in_use) return;
  if (w < 200) w = 200;
  if (h < 200) h = 200;
  if (w > 16384) w = 16384;
  if (h > 16384) h = 16384;

  bool on_screen = false;
  int ndisp = SDL_GetNumVideoDisplays();
  for (int i = 0; i < ndisp; i++) {
    SDL_Rect db;
    if (SDL_GetDisplayBounds(i, &db) != 0) continue;
    SDL_Rect want = {x, y, w, h};
    SDL_Rect isect;
    if (SDL_IntersectRect(&want, &db, &isect) && isect.w > 0 && isect.h > 0) {
      on_screen = true;
      break;
    }
  }
  SDL_SetWindowSize(win->win, w, h);
  if (on_screen)
    SDL_SetWindowPosition(win->win, x, y);
  else
    SDL_SetWindowPosition(win->win, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
}

void Z3RHost_GetGeometry(Z3RWindow *win, int *x, int *y, int *w, int *h) {
  int px = 0, py = 0, pw = 0, ph = 0;
  if (win && win->in_use) {
    SDL_GetWindowPosition(win->win, &px, &py);
    SDL_GetWindowSize(win->win, &pw, &ph);
  }
  if (x) *x = px;
  if (y) *y = py;
  if (w) *w = pw;
  if (h) *h = ph;
}

void Z3RHost_Shutdown(void) {
  ImGuiContext *prev_ctx = ImGui::GetCurrentContext();
  bool prev_valid = false;
  for (int i = 0; i < kZ3RHostMaxWindows; i++)
    if (s_windows[i].in_use && s_windows[i].ctx == prev_ctx) prev_valid = true;

  for (int i = 0; i < kZ3RHostMaxWindows; i++) {
    Z3RWindow *w = &s_windows[i];
    if (!w->in_use) continue;
    SDL_GL_MakeCurrent(w->win, w->gl);
    ImGui::SetCurrentContext(w->ctx);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext(w->ctx);
    SDL_GL_DeleteContext(w->gl);
    SDL_DestroyWindow(w->win);
    w->in_use = false;
    w->shown = false;
    w->win = nullptr;
    w->gl = nullptr;
    w->ctx = nullptr;
  }
  // Only restore the previous ImGui context if it wasn't one we just destroyed.
  ImGui::SetCurrentContext(prev_valid ? prev_ctx : nullptr);
}

#endif  // Z3R_NATIVE_SETTINGS_WINDOW
