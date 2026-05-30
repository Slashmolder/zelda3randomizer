// game_cheats.h — shared cheat-core for the native Debug panels (PC only).
// One source of truth for the edit GATE, clamped g_ram POKES, and the small
// ImGui widget helpers, so every debug panel (inventory, flags, warp, ...) uses
// the same safety gate. All g_ram writes live in .cpp under src/rando/ — the
// audit guard scans only non-rando .c, so these are out of scope by construction.
#ifndef ZELDA3_RANDO_RANDO_WINDOW_GAME_CHEATS_H_
#define ZELDA3_RANDO_RANDO_WINDOW_GAME_CHEATS_H_

#ifdef Z3R_NATIVE_SETTINGS_WINDOW

#include "../../types.h"

#ifdef __cplusplus
extern "C" {
#endif

// --- gates ------------------------------------------------------------------
// True when live g_ram inventory edits are allowed: a save is loaded and Link is
// in a stable gameplay/menu module (0x07/0x09/0x0B/0x0E), and NOT during snapshot
// replay or while the original ROM is attached for side-by-side RAM compare.
bool Cheats_CanEdit(void);
// Stricter gate for warps: a normal gameplay run module (0x07/0x09/0x0B) at a
// stable submodule (0), not replay/emulator. (Warping from a menu/transition is
// unsafe.) See Cheats_Warp* in the warp panel.
bool Cheats_CanWarp(void);
// (enhanced_features1 & kFeatures1_RandomizerActive) != 0 — a rando slot is live.
bool Cheats_RandoActive(void);
// enhanced_features0 (g_ram+0x64c), for feature-aware caps (e.g. CarryMoreRupees).
uint32 Cheats_Features0(void);

// --- clamped, gated pokes ---------------------------------------------------
// Each re-checks Cheats_CanEdit() (defense in depth) and clamps to [lo,hi].
void Cheats_PokeByte(uint32 addr, int v, int lo, int hi);
void Cheats_PokeWord(uint32 addr, int v, int lo, int hi);   // little-endian
void Cheats_PokeBit(uint32 addr, int bit, bool on);

// --- ImGui widget helpers (read g_ram live for display, write via the pokes) -
// A read >= count previews as index 0 (handles sentinels like sword 0xFF).
void Cheats_ByteSlider(const char *label, uint32 addr, int lo, int hi);
void Cheats_Toggle(const char *label, uint32 addr);
void Cheats_Combo(const char *label, uint32 addr, const char *const *items, int count);
void Cheats_ComboVals(const char *label, uint32 addr, const char *const *items,
                      const int *vals, int count);
void Cheats_BitCheckbox(const char *label, uint32 addr, int bit);

// Headless self-check (invoked from --rando-selftest): asserts the edit gate
// keys off the module byte and that the pokes clamp / no-op when gated off.
// Prints OK/FAIL; exits(2) on failure.
void Cheats_SelfCheck(void);

#ifdef __cplusplus
}
#endif

#endif  // Z3R_NATIVE_SETTINGS_WINDOW
#endif  // ZELDA3_RANDO_RANDO_WINDOW_GAME_CHEATS_H_
