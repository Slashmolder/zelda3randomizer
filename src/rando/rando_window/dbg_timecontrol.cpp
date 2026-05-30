// dbg_timecontrol.cpp — Debug > Time panel: pause / single frame-advance / speed
// multiplier, via the MainHost_* time hooks in main.c. PC only.
#ifdef Z3R_NATIVE_SETTINGS_WINDOW
#include "imgui.h"
#include "game_panels.h"

// Host time-control hooks (defined in main.c, also declared in src/config.h).
// Declared locally so this TU doesn't pull in all of config.h for five trivial
// signatures; kept in sync with config.h:307-311.
extern "C" {
void  MainHost_SetPaused(bool paused);
bool  MainHost_GetPaused(void);
void  MainHost_RequestFrameStep(void);  // lets exactly one frame through while paused
void  MainHost_SetSpeed(float mult);     // clamps 0.25..4.0; scales frame-pacing delay only
float MainHost_GetSpeed(void);
}

extern "C" void DbgTime_Render(void) {
  ImGui::TextDisabled("Pause the game loop, step one frame at a time, or run fast/slow. "
                      "The settings window stays interactive while paused, so stepping works.");
  ImGui::Separator();

  // --- Pause -------------------------------------------------------------
  bool paused = MainHost_GetPaused();
  if (ImGui::Checkbox("Pause", &paused)) MainHost_SetPaused(paused);

  // --- Step frame (only meaningful while paused) -------------------------
  ImGui::SameLine();
  if (!paused) ImGui::BeginDisabled();
  if (ImGui::Button("Step frame")) MainHost_RequestFrameStep();
  if (!paused) ImGui::EndDisabled();
  if (!paused) {
    ImGui::SameLine();
    ImGui::TextDisabled("(pause first)");
  }

  ImGui::SeparatorText("Speed");

  // --- Speed slider ------------------------------------------------------
  float speed = MainHost_GetSpeed();
  if (ImGui::SliderFloat("Speed", &speed, 0.25f, 4.0f, "%.2fx")) MainHost_SetSpeed(speed);

  // Quick presets.
  struct { const char *label; float mult; } presets[] = {
    {"0.25x", 0.25f}, {"0.5x", 0.5f}, {"1x (Normal)", 1.0f}, {"2x", 2.0f}, {"4x", 4.0f},
  };
  for (int i = 0; i < (int)(sizeof presets / sizeof presets[0]); i++) {
    if (i) ImGui::SameLine();
    if (ImGui::Button(presets[i].label)) MainHost_SetSpeed(presets[i].mult);
  }

  ImGui::Spacing();
  ImGui::TextDisabled("Speed scales frame-pacing only; audio stays real-time.");
  ImGui::TextDisabled("Has no effect if DisableFrameDelay is set in the INI (the loop skips pacing).");
}
#endif
