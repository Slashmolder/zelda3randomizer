// rando_window.cpp — Dear ImGui native settings window (PC only).
//
// GL-CONTEXT DISCIPLINE (fully implemented in P2; do NOT remove the bind call):
//   Each frame, before ANY ImGui call (ImGui_ImplOpenGL3_NewFrame / NewFrame /
//   Render / RenderDrawData / SDL_GL_SwapWindow(settings_window)), call
//   SDL_GL_MakeCurrent(settings_window, settings_gl_context). After the swap, if
//   the GAME window uses the OpenGL renderer, restore with
//   SDL_GL_MakeCurrent(game_window, game_gl_context). The settings GL context is
//   ALWAYS created against the settings window, never the game window (the default
//   game renderer is SDL software with no GL context). The host (main.c) owns the
//   save/restore of the game's current context around RandoWindow_BeginFrame /
//   RandoWindow_Render; this TU only ever makes the SETTINGS context current.
//
// LOADER ISOLATION: the ImGui OpenGL3 backend uses its OWN embedded loader
// (imgui_impl_opengl3_loader.h). This TU must NEVER include third_party/gl_core
// (the game's loader lives only in src/opengl.c); keeping the two in disjoint TUs
// prevents symbol collisions. <SDL_opengl.h> only declares the gl* entry points
// (glViewport/glClear/...) — it pulls in no loader, so it is safe here.
#ifdef Z3R_NATIVE_SETTINGS_WINDOW

#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_opengl3.h"

#include <SDL.h>
#include <SDL_opengl.h>

#include <cstdio>  // snprintf

#include "rando_window.h"
#include "rando_window_bridge.h"

// ---- File-static state -----------------------------------------------------
static SDL_Window *s_settings_window = nullptr;
static SDL_GLContext s_settings_gl = nullptr;
static bool s_wants_shown = false;

// The three GL entry points we call directly (clear + viewport for the frame).
// We resolve them via SDL_GL_GetProcAddress rather than linking opengl32.lib /
// libGL so this TU adds NO new link dependency (the game's gl_core loader also
// resolves at runtime; the build links neither lib). The ImGui OpenGL3 backend
// loads everything else through its own embedded loader. Resolved once the
// settings context is current (in RandoWindow_Init).
typedef void(APIENTRY *PFN_glViewport)(GLint, GLint, GLsizei, GLsizei);
typedef void(APIENTRY *PFN_glClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
typedef void(APIENTRY *PFN_glClear)(GLbitfield);
static PFN_glViewport s_glViewport = nullptr;
static PFN_glClearColor s_glClearColor = nullptr;
static PFN_glClear s_glClear = nullptr;

// GLSL version string handed to the ImGui OpenGL3 backend. Must agree with the
// SDL_GL_SetAttribute profile/version chosen before context creation (set in the
// host or in RandoWindow_Init below).
#ifdef __APPLE__
static const char *s_glsl_version = "#version 150";  // GL 3.2 core (macOS)
#else
static const char *s_glsl_version = "#version 130";  // GL 3.0
#endif

// ---- Lifecycle -------------------------------------------------------------
void RandoWindow_Init(SDL_Window *window, SDL_GLContext gl_context) {
  s_settings_window = window;
  s_settings_gl = gl_context;

  // Make the settings context current before any ImGui/backend init touches GL.
  SDL_GL_MakeCurrent(s_settings_window, s_settings_gl);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::StyleColorsDark();

  ImGui_ImplSDL2_InitForOpenGL(s_settings_window, s_settings_gl);
  ImGui_ImplOpenGL3_Init(s_glsl_version);

  // Resolve the few GL entry points we call directly (context is current now).
  s_glViewport = (PFN_glViewport)SDL_GL_GetProcAddress("glViewport");
  s_glClearColor = (PFN_glClearColor)SDL_GL_GetProcAddress("glClearColor");
  s_glClear = (PFN_glClear)SDL_GL_GetProcAddress("glClear");
}

void RandoWindow_ProcessEvent(const void *sdl_event) {
  ImGui_ImplSDL2_ProcessEvent((const SDL_Event *)sdl_event);
}

void RandoWindow_BeginFrame(void) {
  SDL_GL_MakeCurrent(s_settings_window, s_settings_gl);

  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplSDL2_NewFrame();
  ImGui::NewFrame();

  // Single main window filling the whole OS window (the settings UI owns the
  // entire client area; no docking, no extra ImGui windows in P2).
  const ImGuiViewport *vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(vp->WorkPos);
  ImGui::SetNextWindowSize(vp->WorkSize);
  ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                           ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                           ImGuiWindowFlags_NoBringToFrontOnFocus;
  if (ImGui::Begin("Z3R Settings##main", nullptr, flags)) {
    if (ImGui::BeginTabBar("##z3r_tabs")) {
      // P4 fills these tabs with real panels. The General tab shows the live
      // bridge-derived state now so P2 has something visible to verify.
      if (ImGui::BeginTabItem("General")) {
        const RandoWindowBridge *b = &g_rando_window_bridge;
        ImGui::TextUnformatted("Share string:");
        ImGui::SameLine();
        ImGui::TextUnformatted(b->share_string[0] ? b->share_string : "(none)");

        // First 16 hex chars (8 bytes) of the pending settings hash.
        char hash16[17];
        for (int i = 0; i < 8; i++)
          snprintf(hash16 + i * 2, 3, "%02x", (unsigned)b->pending_hash[i]);
        hash16[16] = '\0';
        ImGui::TextUnformatted("Settings hash:");
        ImGui::SameLine();
        ImGui::TextUnformatted(hash16);

        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Dungeons")) { ImGui::EndTabItem(); }
      if (ImGui::BeginTabItem("Shuffles")) { ImGui::EndTabItem(); }
      if (ImGui::BeginTabItem("Asset Hash")) { ImGui::EndTabItem(); }
      if (ImGui::BeginTabItem("Spoiler")) { ImGui::EndTabItem(); }
      ImGui::EndTabBar();
    }
  }
  ImGui::End();
}

void RandoWindow_Render(void) {
  SDL_GL_MakeCurrent(s_settings_window, s_settings_gl);

  ImGui::Render();

  int w = 0, h = 0;
  SDL_GL_GetDrawableSize(s_settings_window, &w, &h);  // HiDPI-correct framebuffer size
  if (s_glViewport) s_glViewport(0, 0, w, h);
  if (s_glClearColor) s_glClearColor(0.10f, 0.10f, 0.12f, 1.0f);
  if (s_glClear) s_glClear(GL_COLOR_BUFFER_BIT);
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
  SDL_GL_SwapWindow(s_settings_window);
}

void RandoWindow_Shutdown(void) {
  if (ImGui::GetCurrentContext() != nullptr) {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
  }
}

// ---- Show / hide -----------------------------------------------------------
void RandoWindow_OpenForNewSlot(int slot_index) {
  g_rando_window_bridge.target_slot_index = slot_index;
  if (s_settings_window) {
    SDL_ShowWindow(s_settings_window);
    SDL_RaiseWindow(s_settings_window);
  }
  s_wants_shown = true;
}

void RandoWindow_Hide(void) {
  if (s_settings_window)
    SDL_HideWindow(s_settings_window);
  s_wants_shown = false;
  if (g_rando_window_bridge.target_slot_index >= 0)
    RandoWindowBridge_CancelTarget();
}

bool RandoWindow_WantsShown(void) { return s_wants_shown; }

#endif  // Z3R_NATIVE_SETTINGS_WINDOW
