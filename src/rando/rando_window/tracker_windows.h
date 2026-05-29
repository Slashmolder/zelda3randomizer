// tracker_windows.{h,cpp} — the rich randomizer tracker windows (PC only):
// Item Tracker, Check (location) Tracker, Map Tracker. Each is a separate OS
// window built on the imgui_host. Auto-tracked from live game state — no manual
// clicks, no RAM-watcher desync (the native-port advantage over emulator+tool).
//
// C-callable surface for the C host (main.c). The drawing/ImGui bodies live in
// the .cpp (C++). Gated by Z3R_NATIVE_SETTINGS_WINDOW (PC only; Switch keeps the
// in-game OAM overlay trackers).
#ifndef ZELDA3_RANDO_RANDO_WINDOW_TRACKER_WINDOWS_H_
#define ZELDA3_RANDO_RANDO_WINDOW_TRACKER_WINDOWS_H_

#ifdef Z3R_NATIVE_SETTINGS_WINDOW

#ifdef __cplusplus
extern "C" {
#endif

enum {
  kTracker_Item = 0,
  kTracker_Check = 1,
  kTracker_Map = 2,
  kTracker_Count = 3,
};

// Create the (hidden) tracker windows. Call once at startup, after the settings
// window is initialized (so the host restores the settings ImGui context).
void Trackers_Init(void);

// Toggle a tracker window's visibility (one of the kTracker_* ids). Bound to the
// RandoItemTrackerWindow / RandoCheckTrackerWindow / RandoMapTrackerWindow keys.
void Trackers_Toggle(int kind);

// Show/hide a specific tracker window (used by the settings-window "Trackers"
// section and by per-window geometry/visibility persistence).
void Trackers_SetShown(int kind, bool shown);
bool Trackers_IsShown(int kind);

// Geometry persistence (saves/rando_window.ini). Apply before first show.
void Trackers_ApplyGeometry(int kind, int x, int y, int w, int h);
void Trackers_GetGeometry(int kind, int *x, int *y, int *w, int *h);

void Trackers_Shutdown(void);

#ifdef __cplusplus
}
#endif

#endif  // Z3R_NATIVE_SETTINGS_WINDOW
#endif  // ZELDA3_RANDO_RANDO_WINDOW_TRACKER_WINDOWS_H_
