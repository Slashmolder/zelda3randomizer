// file_dialog.h — minimal native OS file-dialog wrapper (PC settings window).
//
// Why this exists: SDL2 (vendored here, 2.32.x) has NO native file-dialog API —
// SDL_ShowOpenFileDialog is SDL3-only. Until the project moves to SDL3, this is a
// thin interim shim over the single-header `portable-file-dialogs` (PFD) library.
// When SDL3 lands, delete this TU + third_party/portable-file-dialogs/ and route
// callers through SDL_ShowOpenFileDialog instead.
//
// ISOLATION: PFD drags in <windows.h> (on Win32), <regex>, <future>, <thread>,
// <iostream>. Including it in rando_window.cpp would leak windows.h's min/max
// macros into the ImGui/SDL TU and balloon its compile. So PFD is confined to
// file_dialog.cpp ALONE; callers see only this tiny POD-string interface.
#pragma once

#include <cstddef>

namespace FileDialog {

// Blocking native "open file" dialog. On success, fills `out` with the chosen
// path (UTF-8, NUL-terminated, truncated to out_sz) and returns true. Returns
// false if the user cancelled OR no backend is available (e.g. a headless Linux
// box with no zenity/kdialog) — in that case `out` is left untouched, so callers
// can keep whatever the user typed.
//
//   title          window caption, e.g. "Select customizer manifest".
//   default_path   initial file/dir the dialog opens at; "" for the last used.
//   filter_label   human label for the primary filter, e.g. "YAML manifest"
//                  (nullptr to offer only "All Files").
//   filter_patterns space-separated globs for that filter, e.g. "*.yaml *.yml".
// "All Files (*)" is always appended as a secondary filter.
bool OpenFile(const char *title, const char *default_path,
              const char *filter_label, const char *filter_patterns,
              char *out, std::size_t out_sz);

}  // namespace FileDialog
