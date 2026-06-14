// file_dialog.cpp — the ONLY TU that includes portable-file-dialogs. See
// file_dialog.h for the rationale (SDL2 has no native dialog; interim until SDL3).
#ifdef Z3R_NATIVE_SETTINGS_WINDOW

// PFD includes <windows.h> on Win32. Keep its min/max macros from clobbering
// std::min/std::max used by <regex> etc. — must precede the PFD include.
#ifndef NOMINMAX
#define NOMINMAX 1
#endif

// Relative path (not -I/$(SolutionDir)-dependent) so it resolves identically
// under the Makefile and MSBuild. file_dialog.cpp is src/rando/rando_window/,
// so the repo root is three levels up.
#include "../../../third_party/portable-file-dialogs/portable-file-dialogs.h"

#include "file_dialog.h"

#include <cstdio>   // snprintf
#include <string>
#include <vector>

namespace FileDialog {

bool OpenFile(const char *title, const char *default_path,
              const char *filter_label, const char *filter_patterns,
              char *out, std::size_t out_sz) {
  if (out == nullptr || out_sz == 0)
    return false;
  // No desktop helper (zenity/kdialog/osascript/Win32) -> behave like a cancel
  // so the caller falls back to the typed path. (Always true on Windows.)
  if (!pfd::settings::available())
    return false;

  std::vector<std::string> filters;
  if (filter_label != nullptr && filter_patterns != nullptr) {
    filters.push_back(filter_label);
    filters.push_back(filter_patterns);
  }
  filters.push_back("All Files");
  filters.push_back("*");

  pfd::open_file dlg(title != nullptr ? title : "Open File",
                     default_path != nullptr ? default_path : "",
                     filters, pfd::opt::none);
  std::vector<std::string> sel = dlg.result();  // blocks until the user is done
  if (sel.empty() || sel[0].empty())
    return false;

  snprintf(out, out_sz, "%s", sel[0].c_str());
  return true;
}

}  // namespace FileDialog

#endif  // Z3R_NATIVE_SETTINGS_WINDOW
