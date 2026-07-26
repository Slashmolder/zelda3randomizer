// game_panels.h — render entry points for the native Debug + Randomizer panels.
// Each is implemented in its own .cpp (file-disjoint, so panels can be authored
// independently). Called from the nested tab dispatchers in rando_window.cpp /
// game_config_panels.cpp. PC only (Z3R_NATIVE_SETTINGS_WINDOW). C linkage so the
// C++ panel TUs and the dispatcher share one declaration set.
#ifndef ZELDA3_RANDO_RANDO_WINDOW_GAME_PANELS_H_
#define ZELDA3_RANDO_RANDO_WINDOW_GAME_PANELS_H_

#ifdef Z3R_NATIVE_SETTINGS_WINDOW

#ifdef __cplusplus
extern "C" {
#endif

// Debug tab sub-panels.
void DbgWatch_Render(void);      // live g_ram state readout + the F12 dump button (dbg_watch.cpp)
void DbgInventory_Render(void);  // inventory/equipment editor (game_config_panels.cpp)
void DbgSnapshots_Render(void);  // save-state slot manager (dbg_snapshots.cpp)
void DbgTime_Render(void);       // pause / frame-advance / speed (dbg_timecontrol.cpp)
void DbgWarp_Render(void);       // teleport (dbg_warp.cpp; owns the warp g_ram sequence)
void DbgFlags_Render(void);      // story/progress flag editor (dbg_flags.cpp)

// Randomizer tab sub-panels.
void RandoReach_Render(void);    // reachability view (rando_reach_panel.cpp)
void RandoHints_Render(void);    // next-seed setup + active-slot journal (rando_hints_panel.cpp)
const char *RandoHints_PendingProfileName(void);  // summary for General tab
void RandoHints_SelfCheck(void); // race/full-view policy invariants

// Headless render smoke check (invoked from --rando-selftest): renders every
// panel once into a backend-less ImGui context to catch crashes / null-derefs.
void Panels_RenderSmokeCheck(void);

#ifdef __cplusplus
}
#endif

#endif  // Z3R_NATIVE_SETTINGS_WINDOW
#endif  // ZELDA3_RANDO_RANDO_WINDOW_GAME_PANELS_H_
