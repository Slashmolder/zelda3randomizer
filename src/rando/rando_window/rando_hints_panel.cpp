// rando_hints_panel.cpp — Randomizer > Hints panel: read-only list of the active
// slot's hints (Rando_GetHintString over the hint-NPC enum). PC only.
#ifdef Z3R_NATIVE_SETTINGS_WINDOW
#include "imgui.h"
#include "game_panels.h"

#include <cstdio>   // snprintf
#include <cctype>   // toupper

// Game-side C APIs. rando.h is plain C (its lone _Static_assert is now
// C++-guarded); wrap in extern "C" so the C++ linker resolves the C symbols.
extern "C" {
#include "../rando.h"          // Rando_IsActive, Rando_GetActiveSettings, RandoSettings
#include "../rando_hints.h"    // RandoHintNpc, Rando_GetHintString, Rando_GetHintNpcStringId
}

// Prettify a stable string-id ("telepathic_tile_eastern_palace") into a more
// readable label ("Telepathic Tile Eastern Palace") for the heading. Falls
// back to the raw id if it doesn't fit the scratch buffer.
static const char *PrettifyNpcLabel(const char *id, char *buf, size_t buflen) {
  if (!id || buflen == 0) return id;
  size_t n = 0;
  bool word_start = true;
  for (size_t i = 0; id[i] != '\0' && n + 1 < buflen; i++) {
    char c = id[i];
    if (c == '_') {
      buf[n++] = ' ';
      word_start = true;
    } else {
      buf[n++] = word_start ? (char)toupper((unsigned char)c) : c;
      word_start = false;
    }
  }
  buf[n] = '\0';
  return buf;
}

extern "C" void RandoHints_Render(void) {
  if (!Rando_IsActive()) {
    ImGui::TextDisabled("No randomizer slot active.");
    ImGui::TextDisabled("Start or load a randomizer slot to view its hints.");
    return;
  }

  // Hints are not spoiler-gated upstream (ALTTPR distributes hints in race
  // seeds too), but flag race mode so a tester knows the context.
  const RandoSettings *settings = Rando_GetActiveSettings();
  bool race = settings && settings->race_mode;

  // Count the NPCs that actually have a hint allocated this slot.
  int n_hints = 0;
  for (int npc = 1; npc < kRandoHintNpc__Count; npc++) {
    if (Rando_GetHintString((RandoHintNpc)npc) != NULL) n_hints++;
  }

  ImGui::Text("Hints: %d", n_hints);
  if (race) {
    ImGui::SameLine();
    ImGui::TextDisabled("(race mode)");
  }

  if (n_hints == 0) {
    ImGui::Separator();
    ImGui::TextDisabled("This slot has no hints (hints may be disabled, or the "
                        "hint generator is not active on this build).");
    return;
  }

  ImGui::Separator();

  char label_buf[96];
  for (int npc = 1; npc < kRandoHintNpc__Count; npc++) {
    const char *text = Rando_GetHintString((RandoHintNpc)npc);
    if (text == NULL) continue;  // NPC has no hint this slot.

    const char *id = Rando_GetHintNpcStringId((RandoHintNpc)npc);
    const char *label = id ? PrettifyNpcLabel(id, label_buf, sizeof label_buf)
                           : "(unknown npc)";

    // Heading: prettified NPC name, with the raw string-id as a dim suffix
    // so spoiler-JSON keys remain identifiable.
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.80f, 1.0f, 1.0f));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    if (id) {
      ImGui::SameLine();
      ImGui::TextDisabled("[%s]", id);
    }

    ImGui::Indent();
    ImGui::TextWrapped("%s", text);
    ImGui::Unindent();

    ImGui::Separator();
  }
}
#endif
