// rando_hints_panel.cpp — Randomizer > Hints discovered journal and explicit
// non-race full-plan viewer. PC only; every game-side call is read-only.
#ifdef Z3R_NATIVE_SETTINGS_WINDOW
#include "imgui.h"
#include "game_panels.h"
#include "rando_window_bridge.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// Game-side C APIs. Keep the panel behind the public read-only hint surface:
// the immutable plan and mutable discovery remain owned by rando_hints.c.
extern "C" {
#include "../rando.h"
#include "../rando_hints.h"
}

namespace {

constexpr size_t kDigestPrefixBytes = 8;
constexpr const char *kViewAllPopup =
    "Reveal all seed hints?##z3r_hint_full_confirm";

struct HintViewerSession {
  bool has_plan_identity;
  bool view_all;
  uint16 algorithm;
  uint16 text_schema;
  uint8 digest[kRandoHintPlanDigestBytes];
};

HintViewerSession g_viewer_session = {};

void CommitHintPolicyEdit(RandoSettings *s) {
  Settings_NormalizeHintPolicy(s);
  RandoWindowBridge_RecomputeDerived();
}

const char *HintTileCoverageName(uint8 value) {
  switch (value) {
  case kHintTileCoverage_None:
    return "None";
  case kHintTileCoverage_Sparse:
    return "Few (5)";
  case kHintTileCoverage_Standard:
    return "Many (10)";
  case kHintTileCoverage_Full:
    return "All (15)";
  default:
    return "Unknown";
  }
}

const char *HintMixName(uint8 value) {
  switch (value) {
  case kHintMix_Variety:
    return "Variety";
  case kHintMix_Important:
    return "Important items";
  case kHintMix_Difficult:
    return "Difficult checks";
  case kHintMix_WorldInfo:
    return "World info";
  default:
    return "Unknown";
  }
}

void RenderNextSeedSetup() {
  RandoSettings *s = &g_rando_window_bridge.pending;
  HintProfile profile = Settings_GetHintProfile(s);

  ImGui::SeparatorText("Clue profile");
  const HintProfile profiles[] = {
      kHintProfile_Off, kHintProfile_Sparse, kHintProfile_Balanced,
      kHintProfile_Direct};
  for (size_t i = 0; i < sizeof(profiles) / sizeof(profiles[0]); i++) {
    if (i != 0) ImGui::SameLine();
    const HintProfile candidate = profiles[i];
    if (ImGui::RadioButton(Settings_HintProfileTitle(candidate),
                           profile == candidate)) {
      Settings_ApplyHintProfile(s, candidate);
      RandoWindowBridge_RecomputeDerived();
      profile = Settings_GetHintProfile(s);
    }
  }
  if (profile == kHintProfile_Custom) {
    ImGui::SameLine();
    ImGui::TextDisabled("%s", Settings_HintProfileTitle(profile));
  }
  ImGui::TextDisabled(
      "Profiles are shortcuts. Changing a delivery control derives Custom.");

  const bool enabled = Settings_HintsEnabled(s);
  ImGui::SeparatorText("Delivery and content");
  if (!enabled) ImGui::BeginDisabled();
  const uint8 coverage_values[] = {
      kHintTileCoverage_None, kHintTileCoverage_Sparse,
      kHintTileCoverage_Standard, kHintTileCoverage_Full};
  if (ImGui::BeginCombo("Telepathic tiles",
                        HintTileCoverageName(s->hint_tile_coverage))) {
    for (size_t i = 0;
         i < sizeof(coverage_values) / sizeof(coverage_values[0]); i++) {
      const uint8 value = coverage_values[i];
      const bool selected = s->hint_tile_coverage == value;
      if (ImGui::Selectable(HintTileCoverageName(value), selected) &&
          !selected) {
        s->hints = kHintsMode_Balanced;
        s->hint_tile_coverage = value;
        CommitHintPolicyEdit(s);
      }
      if (selected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
  ImGui::TextDisabled(
      "How many of the 15 telepathic tiles carry a clue.");

  char paid_preview[24];
  std::snprintf(paid_preview, sizeof(paid_preview), "%u per service",
                (unsigned)Settings_EffectiveHintPaidDepth(s));
  if (ImGui::BeginCombo("Paid clues", paid_preview)) {
    for (uint8 value = 0; value <= 3; value++) {
      char label[24];
      std::snprintf(label, sizeof(label), "%u per service", (unsigned)value);
      const bool selected = Settings_EffectiveHintPaidDepth(s) == value;
      if (ImGui::Selectable(label, selected) && !selected) {
        s->hints = kHintsMode_Balanced;
        s->hint_paid_depth = value;
        CommitHintPolicyEdit(s);
      }
      if (selected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
  ImGui::TextDisabled(
      "Each of the three paid services has its own deterministic queue.");

  const uint8 mix_values[] = {
      kHintMix_Variety, kHintMix_Important, kHintMix_Difficult,
      kHintMix_WorldInfo};
  if (ImGui::BeginCombo("Hint mix", HintMixName(s->hint_mix))) {
    for (size_t i = 0; i < sizeof(mix_values) / sizeof(mix_values[0]); i++) {
      const uint8 value = mix_values[i];
      const bool selected = s->hint_mix == value;
      if (ImGui::Selectable(HintMixName(value), selected) && !selected) {
        s->hints = kHintsMode_Balanced;
        s->hint_mix = value;
        CommitHintPolicyEdit(s);
      }
      if (selected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
  ImGui::TextDisabled(
      "Variety mixes categories; the other choices emphasize one kind of clue.");
  if (!enabled) ImGui::EndDisabled();

  ImGui::SeparatorText("NPC disclosures");
  bool reveal_npc_rewards = s->hint_npc_reward_reveal != 0;
  if (ImGui::Checkbox("Reveal NPC rewards", &reveal_npc_rewards)) {
    s->hint_npc_reward_reveal = reveal_npc_rewards ? 1 : 0;
    RandoWindowBridge_RecomputeDerived();
  }
  ImGui::TextDisabled(
      "Reward NPCs and shop clerks name unchecked placed items before commitment.");
  ImGui::TextDisabled(
      "Independent of clue profiles. Rich dialogue is Original/US only.");

  const int tiles = enabled ? Settings_HintTileCount(s) : 0;
  const int paid_depth =
      enabled ? Settings_EffectiveHintPaidDepth(s) : 0;
  const int paid = paid_depth * 3;
  ImGui::Spacing();
  ImGui::Text("Up to %d delivery clues: %d tile + %d paid",
              tiles + paid, tiles, paid);
  ImGui::TextWrapped(
      "Paid clues are always exact. Coverage and queue depths are deterministic, "
      "so increasing them retains the clues from smaller settings.");
  if (!enabled) {
    ImGui::TextDisabled(
        "Hints are Off. Pick an enabled profile, then adjust its controls.");
  } else if (paid_depth == 0) {
    ImGui::TextDisabled(
        "Paid services still heal, but clearly advertise that no new clue is "
        "available before asking for payment.");
  }
}

// Prettify a stable string id ("telepathic_tile_eastern_palace") into a more
// readable source label ("Telepathic Tile Eastern Palace"). If the complete
// result does not fit, return the stable id instead of showing a truncation.
const char *PrettifyNpcLabel(const char *id, char *buf, size_t buflen) {
  if (id == nullptr || buf == nullptr || buflen == 0) return id;
  size_t n = 0;
  bool word_start = true;
  for (size_t i = 0; id[i] != '\0'; i++) {
    if (n + 1 >= buflen) return id;
    char c = id[i];
    if (c == '_') {
      buf[n++] = ' ';
      word_start = true;
    } else {
      buf[n++] =
          word_start ? (char)std::toupper((unsigned char)c) : c;
      word_start = false;
    }
  }
  buf[n] = '\0';
  return buf;
}

const char *HintSourceLabel(RandoHintNpc npc, char *buf, size_t buflen) {
  switch (npc) {
  case kRandoHintNpc_ForkStoryteller:
    return "Storyteller";
  case kRandoHintNpc_ForkFortuneTellerKak:
  case kRandoHintNpc_ForkFortuneTellerLakeHylia:
    return "Fortune Teller - Light World (Kakariko / Lake Hylia)";
  case kRandoHintNpc_ForkFortuneTellerDark:
    return "Fortune Teller - Dark World";
  default:
    break;
  }

  const char *id = Rando_GetHintNpcStringId(npc);
  const char *label = PrettifyNpcLabel(id, buf, buflen);
  return label != nullptr ? label : "(source unavailable)";
}

void FormatDigestPrefix(const uint8 *digest, char out[17]) {
  static const char kHex[] = "0123456789abcdef";
  if (digest == nullptr) {
    std::snprintf(out, 17, "%s", "unavailable");
    return;
  }
  for (size_t i = 0; i < kDigestPrefixBytes; i++) {
    out[i * 2] = kHex[digest[i] >> 4];
    out[i * 2 + 1] = kHex[digest[i] & 15];
  }
  out[kDigestPrefixBytes * 2] = '\0';
}

bool HintFullViewAllowed(bool active, bool race, RandoHintPlanStatus status) {
  return active && !race && status == kRandoHintPlanStatus_Ready;
}

bool HintFactVisible(bool discovered, bool view_all, bool race) {
  return discovered || (view_all && !race);
}

bool SamePlanIdentity(uint16 algorithm, uint16 text_schema,
                      const uint8 *digest) {
  return g_viewer_session.has_plan_identity && digest != nullptr &&
         g_viewer_session.algorithm == algorithm &&
         g_viewer_session.text_schema == text_schema &&
         std::memcmp(g_viewer_session.digest, digest,
                     kRandoHintPlanDigestBytes) == 0;
}

void UpdateViewerSession(bool active, bool race, RandoHintPlanStatus status,
                         uint16 algorithm, uint16 text_schema,
                         const uint8 *digest) {
  const bool identity_valid =
      active && status == kRandoHintPlanStatus_Ready && digest != nullptr;
  if (!identity_valid) {
    std::memset(&g_viewer_session, 0, sizeof(g_viewer_session));
    return;
  }
  if (!SamePlanIdentity(algorithm, text_schema, digest)) {
    std::memset(&g_viewer_session, 0, sizeof(g_viewer_session));
    g_viewer_session.has_plan_identity = true;
    g_viewer_session.algorithm = algorithm;
    g_viewer_session.text_schema = text_schema;
    std::memcpy(g_viewer_session.digest, digest,
                kRandoHintPlanDigestBytes);
  }
  // This is also applied while a confirmation popup is open. A direct switch
  // to a race slot can therefore never inherit a prior non-race reveal.
  if (race) g_viewer_session.view_all = false;
}

const char *UnavailablePlanMessage(RandoHintPlanStatus status) {
  switch (status) {
  case kRandoHintPlanStatus_None:
    return "No validated hint plan is active for this slot.";
  case kRandoHintPlanStatus_MissingMetadata:
    return "This save has no supported Hints v2 plan identity.";
  case kRandoHintPlanStatus_UnsupportedAlgorithm:
    return "This save uses a hint algorithm this build cannot reproduce.";
  case kRandoHintPlanStatus_UnsupportedTextSchema:
    return "This save uses a hint text schema this build cannot reproduce.";
  case kRandoHintPlanStatus_DigestMismatch:
    return "The rebuilt hint plan did not match the saved plan identity.";
  case kRandoHintPlanStatus_BuildFailed:
    return "The active hint plan could not be rebuilt.";
  default:
    return "Hints are unavailable for this slot.";
  }
}

void RenderPlanIdentity(RandoHintPlanStatus status, bool race,
                        uint16 algorithm, uint16 text_schema,
                        const uint8 *digest) {
  if (race) {
    // The normal race journal exposes only delivered facts. Versions and the
    // full-plan digest are developer identity data, so keep them on the
    // explicitly unrestricted F12 diagnostic rather than this player UI.
    ImGui::TextDisabled("Plan: %s | race journal",
                        Rando_GetHintPlanStatusName());
    return;
  }
  char digest_prefix[17];
  char algorithm_text[16];
  char schema_text[16];
  FormatDigestPrefix(digest, digest_prefix);
  if (algorithm != 0)
    std::snprintf(algorithm_text, sizeof(algorithm_text), "%u",
                  (unsigned)algorithm);
  else
    std::snprintf(algorithm_text, sizeof(algorithm_text), "%s", "n/a");
  if (text_schema != 0)
    std::snprintf(schema_text, sizeof(schema_text), "%u",
                  (unsigned)text_schema);
  else
    std::snprintf(schema_text, sizeof(schema_text), "%s", "n/a");

  ImGui::TextDisabled("Plan: %s | Algorithm: %s | Text schema: %s",
                      Rando_GetHintPlanStatusName(), algorithm_text,
                      schema_text);
  ImGui::TextDisabled("Digest: %s...", digest_prefix);
  (void)status;
}

void RenderFact(uint8 fact_id, bool show_discovery_state) {
  const RandoHintFact *fact = Rando_GetHintFact(fact_id);
  if (fact == nullptr) return;

  RandoHintNpc npc = kRandoHintNpc_None;
  uint8 queue_index = 0;
  bool assigned =
      Rando_GetHintFactAssignment(fact_id, &npc, &queue_index);
  char source_buf[128];
  const char *source =
      assigned ? HintSourceLabel(npc, source_buf, sizeof(source_buf))
               : "(source unavailable)";
  bool discovered = Rando_IsHintFactDiscovered(fact_id);
  bool resolved = Rando_IsHintFactResolved(fact_id);

  ImGui::PushID((int)fact_id);
  ImGui::PushStyleColor(ImGuiCol_Text,
                        ImVec4(0.55f, 0.80f, 1.0f, 1.0f));
  ImGui::TextUnformatted(source);
  ImGui::PopStyleColor();
  if (resolved) {
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.50f, 1.0f), "[Resolved]");
  }
  if (show_discovery_state && !discovered) {
    ImGui::SameLine();
    ImGui::TextDisabled("[Undiscovered]");
  }

  ImGui::Indent();
  ImGui::TextWrapped("%s", fact->text);
  ImGui::Unindent();
  ImGui::Separator();
  ImGui::PopID();
  (void)queue_index;  // Assignment order stays out of the discovered journal.
}

}  // namespace

static void RenderActiveSlotJournal(void) {
  const bool active = Rando_IsActive();
  if (!active) {
    std::memset(&g_viewer_session, 0, sizeof(g_viewer_session));
    ImGui::TextDisabled("Start or load a randomizer slot to view its hints.");
    return;
  }

  const bool race = Rando_ActiveSlotHidesSpoiler();
  const RandoHintPlanStatus status = Rando_GetHintPlanStatus();
  const uint16 algorithm = Rando_GetHintPlanAlgorithmVersion();
  const uint16 text_schema = Rando_GetHintTextSchemaVersion();
  const uint8 *digest = Rando_GetHintPlanDigest();
  UpdateViewerSession(active, race, status, algorithm, text_schema, digest);

  RenderPlanIdentity(status, race, algorithm, text_schema, digest);
  ImGui::Separator();

  const RandoSettings *active_settings = Rando_GetActiveSettings();
  if (active_settings != nullptr) {
    ImGui::Text("NPC reward previews: %s",
                active_settings->hint_npc_reward_reveal ? "On" : "Off");
    ImGui::TextDisabled("Rich dialogue is Original/US only.");
  } else {
    ImGui::TextDisabled("NPC reward previews: active settings unavailable");
  }
  ImGui::Separator();

  if (status == kRandoHintPlanStatus_Off) {
    ImGui::TextDisabled("Hints are disabled for this slot.");
    return;
  }
  if (status != kRandoHintPlanStatus_Ready || digest == nullptr) {
    ImGui::TextDisabled("%s", UnavailablePlanMessage(status));
    ImGui::TextDisabled("No prior slot's hint journal is being shown.");
    return;
  }

  const uint8 fact_count = Rando_GetHintFactCount();
  const uint8 discovered_count = Rando_GetHintDiscoveredCount();
  if (race) {
    ImGui::Text("Discovered: %u", (unsigned)discovered_count);
  } else {
    ImGui::Text("Discovered: %u of %u", (unsigned)discovered_count,
                (unsigned)fact_count);
  }

  const bool can_view_all =
      fact_count != 0 && HintFullViewAllowed(active, race, status);
  if (can_view_all && !g_viewer_session.view_all) {
    if (ImGui::Button("View all seed hints — placement spoilers"))
      ImGui::OpenPopup(kViewAllPopup);
  } else if (can_view_all && g_viewer_session.view_all) {
    ImGui::SameLine();
    ImGui::TextDisabled("(showing the full spoiler-bearing deck)");
    if (ImGui::Button("Return to discovered hints"))
      g_viewer_session.view_all = false;
  }

  // Always service an already-open popup so a slot/race transition closes it
  // before any confirmation can take effect.
  if (ImGui::BeginPopupModal(kViewAllPopup, nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    if (!can_view_all) {
      ImGui::CloseCurrentPopup();
    } else {
      ImGui::TextWrapped(
          "This reveals every undiscovered seed hint, including placement "
          "spoilers and paid reserve clues.");
      ImGui::TextWrapped(
          "Viewing is session-only and does not discover or consume hints.");
      ImGui::Spacing();
      if (ImGui::Button("Cancel"))
        ImGui::CloseCurrentPopup();
      ImGui::SameLine();
      if (ImGui::Button("Reveal all seed hints")) {
        g_viewer_session.view_all = true;
        ImGui::CloseCurrentPopup();
      }
    }
    ImGui::EndPopup();
  }

  const bool view_all = can_view_all && g_viewer_session.view_all;
  if (fact_count == 0 && !race) {
    ImGui::Separator();
    ImGui::TextDisabled("This validated plan contains no delivery hints.");
    return;
  }
  if (!view_all && discovered_count == 0) {
    ImGui::Separator();
    ImGui::TextDisabled("No hints have been discovered yet.");
    ImGui::TextDisabled(
        "Read a telepathic tile or buy a clue to add it to this journal.");
    return;
  }

  ImGui::Separator();
  for (uint8 fact_id = 0; fact_id < fact_count; fact_id++) {
    const bool discovered = Rando_IsHintFactDiscovered(fact_id);
    if (!HintFactVisible(discovered, view_all, race)) continue;
    RenderFact(fact_id, view_all);
  }
}

extern "C" const char *RandoHints_PendingProfileName(void) {
  return Settings_HintProfileTitle(
      Settings_GetHintProfile(&g_rando_window_bridge.pending));
}

extern "C" void RandoHints_Render(void) {
  if (ImGui::BeginTabBar("##z3r_hint_views")) {
    if (ImGui::BeginTabItem("Next seed setup")) {
      RenderNextSeedSetup();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Active-slot journal")) {
      RenderActiveSlotJournal();
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }
}

extern "C" void RandoHints_SelfCheck(void) {
  int failures = 0;
#define CHECK_HINT_UI(cond, msg)                                           \
  do {                                                                     \
    if (!(cond)) {                                                         \
      std::fprintf(stderr, "[RandoHints_SelfCheck] FAIL: %s\n", (msg));   \
      failures++;                                                          \
    }                                                                      \
  } while (0)

  CHECK_HINT_UI(HintFactVisible(true, false, false),
                "discovered non-race fact hidden");
  CHECK_HINT_UI(HintFactVisible(true, false, true),
                "discovered race fact hidden");
  CHECK_HINT_UI(!HintFactVisible(false, false, false),
                "default journal exposed undiscovered fact");
  CHECK_HINT_UI(HintFactVisible(false, true, false),
                "confirmed non-race full view hid undiscovered fact");
  CHECK_HINT_UI(!HintFactVisible(false, true, true),
                "race full-view state exposed undiscovered fact");

  CHECK_HINT_UI(
      HintFullViewAllowed(true, false, kRandoHintPlanStatus_Ready),
      "ready non-race plan denied full-view confirmation");
  CHECK_HINT_UI(
      !HintFullViewAllowed(true, true, kRandoHintPlanStatus_Ready),
      "race plan allowed full view");
  CHECK_HINT_UI(
      !HintFullViewAllowed(true, false, kRandoHintPlanStatus_DigestMismatch),
      "mismatched plan allowed full view");
  CHECK_HINT_UI(
      !HintFullViewAllowed(false, false, kRandoHintPlanStatus_Ready),
      "inactive slot allowed full view");

  RandoSettings profile_settings;
  Settings_SetDefaults(&profile_settings);
  profile_settings.hint_npc_reward_reveal = 1;
  CHECK_HINT_UI(
      Settings_GetHintProfile(&profile_settings) == kHintProfile_Balanced,
      "NPC reward disclosure changed Balanced profile derivation");
  Settings_ApplyHintProfile(&profile_settings, kHintProfile_Sparse);
  CHECK_HINT_UI(
      Settings_GetHintProfile(&profile_settings) == kHintProfile_Sparse &&
          Settings_HintTileCount(&profile_settings) == 5 &&
          Settings_EffectiveHintPaidDepth(&profile_settings) == 1 &&
          profile_settings.hint_npc_reward_reveal == 1,
      "Sparse profile tuple changed independent NPC disclosure");
  Settings_ApplyHintProfile(&profile_settings, kHintProfile_Direct);
  CHECK_HINT_UI(
      Settings_GetHintProfile(&profile_settings) == kHintProfile_Direct &&
          profile_settings.hint_mix == kHintMix_Important &&
          profile_settings.hint_npc_reward_reveal == 1,
      "Direct profile tuple changed independent NPC disclosure");
  profile_settings.hint_paid_depth = 2;
  CHECK_HINT_UI(
      Settings_GetHintProfile(&profile_settings) == kHintProfile_Custom,
      "derived policy was not labeled Custom");
  Settings_ApplyHintProfile(&profile_settings, kHintProfile_Off);
  CHECK_HINT_UI(
      Settings_GetHintProfile(&profile_settings) == kHintProfile_Off &&
          profile_settings.hint_tile_coverage == kHintTileCoverage_Full &&
          profile_settings.hint_mix == kHintMix_Variety &&
          profile_settings.hint_paid_depth == 3 &&
          profile_settings.hint_npc_reward_reveal == 1,
      "Off profile did not preserve independent NPC disclosure");

  char label[128];
  CHECK_HINT_UI(
      std::strcmp(HintSourceLabel(kRandoHintNpc_ForkFortuneTellerKak,
                                  label, sizeof(label)),
                  "Fortune Teller - Light World (Kakariko / Lake Hylia)") ==
          0,
      "Fortune Light label omitted shared Kakariko/Lake source");
  CHECK_HINT_UI(
      std::strcmp(HintSourceLabel(kRandoHintNpc_ForkFortuneTellerDark,
                                  label, sizeof(label)),
                  "Fortune Teller - Dark World") == 0,
      "Fortune Dark label incorrect");

  uint8 digest[kRandoHintPlanDigestBytes] = {};
  digest[0] = 0x01;
  digest[1] = 0x23;
  digest[2] = 0x45;
  digest[3] = 0x67;
  digest[4] = 0x89;
  digest[5] = 0xab;
  digest[6] = 0xcd;
  digest[7] = 0xef;
  char prefix[17];
  FormatDigestPrefix(digest, prefix);
  CHECK_HINT_UI(std::strcmp(prefix, "0123456789abcdef") == 0,
                "digest prefix formatting changed");

  std::memset(&g_viewer_session, 0, sizeof(g_viewer_session));
  UpdateViewerSession(true, false, kRandoHintPlanStatus_Ready, 1, 1,
                      digest);
  g_viewer_session.view_all = true;
  UpdateViewerSession(true, true, kRandoHintPlanStatus_Ready, 1, 1, digest);
  CHECK_HINT_UI(!g_viewer_session.view_all,
                "race transition retained full-view state");
  g_viewer_session.view_all = true;
  digest[0] ^= 0xff;
  UpdateViewerSession(true, false, kRandoHintPlanStatus_Ready, 1, 1,
                      digest);
  CHECK_HINT_UI(!g_viewer_session.view_all,
                "plan identity change retained full-view state");
  UpdateViewerSession(false, false, kRandoHintPlanStatus_None, 0, 0, nullptr);
  CHECK_HINT_UI(!g_viewer_session.has_plan_identity &&
                    !g_viewer_session.view_all,
                "deactivation retained viewer session state");

#undef CHECK_HINT_UI
  if (failures != 0) {
    std::fprintf(stderr, "[RandoHints_SelfCheck] %d failure(s)\n", failures);
    std::exit(2);
  }
  std::fprintf(stderr, "[RandoHints_SelfCheck] OK\n");
}
#endif
