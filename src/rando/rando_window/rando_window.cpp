// rando_window.cpp — Dear ImGui native settings window (PC only).
//
// GL-CONTEXT DISCIPLINE (fully implemented in P2; do NOT remove the bind call):
//   Each frame, before ANY ImGui call (ImGui_ImplOpenGL3_NewFrame / NewFrame /
//   Render / RenderDrawData / SDL_GL_SwapWindow(settings_window)), call
//   SDL_GL_MakeCurrent(settings_window, settings_gl_context). After the swap, if
//   the GAME window uses the OpenGL renderer, restore with
//   SDL_GL_MakeCurrent(game_window, game_gl_context). The settings GL context is
//   ALWAYS created against the settings window, never the game window (the default
//   game renderer is SDL software with no GL context).
//
// LOADER ISOLATION: the ImGui OpenGL3 backend uses its OWN embedded loader
// (imgui_impl_opengl3_loader.h). This TU must NEVER include third_party/gl_core
// (the game's loader lives only in src/opengl.c); keeping the two in disjoint TUs
// prevents symbol collisions.
#ifdef Z3R_NATIVE_SETTINGS_WINDOW

#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_opengl3.h"

#include "rando_window.h"

// P1 skeleton: prove the C++ toolchain + ImGui compile and link end-to-end. The
// real init (backends, style, panels, GL discipline) lands in P2/P4. Bodies are
// no-ops so the build links with a settings window that does nothing yet.

void RandoWindow_Init(SDL_Window *window, SDL_GLContext gl_context) {
  (void)window;
  (void)gl_context;
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();  // link-proof; backend init + style follow in P2
}

void RandoWindow_BeginFrame(void) {}

void RandoWindow_Render(void) {}

void RandoWindow_Shutdown(void) {
  if (ImGui::GetCurrentContext() != nullptr) {
    ImGui::DestroyContext();
  }
}

void RandoWindow_OpenForNewSlot(int slot_index) { (void)slot_index; }

bool RandoWindow_WantsShown(void) { return false; }

#endif  // Z3R_NATIVE_SETTINGS_WINDOW
