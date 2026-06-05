# Tasks — add-rando-race-mode-reveal-ui

Follow-up carve-out from `add-rando-race-mode-reveal` (archived). The reveal
*action* (`Rando_RevealSpoiler`), suppression, and stamp-verify already shipped;
this change is the deferred in-binary UI only.

## As-built (2026-06-04)

Surface changed from the original plan. The proposal/spec described a **SNES
file-select per-slot action menu**. As built, the UI lives in the **PC native
ImGui window** (`src/rando/rando_window/rando_window.cpp`, `Panel_General`),
because (a) the PC settings/admin UI is the native window — the SNES settings
screen is compiled out on PC — and (b) `select_file.c` is a hot, render-
regression-prone file (memory `compile_out_hot_files`). Divergences:

- **Active slot, not per-slot-from-file-select.** The reveal operates on the
  *active* (loaded) race-mode slot via `Rando_RevealActiveSlotSpoiler()`. The
  "reveal a slot you can see but haven't loaded" case remains the admin path
  (CLI `--reveal-spoiler`). Switch retains the existing reveal keybind + CLI; it
  gets no new UI (native window is PC-only).
- **Anti-cheat completion gate.** The in-binary reveal is gated on seed
  completion (`main_module_index >= 24`), mirroring the core's existing MED-1
  gate in `Rando_RevealActiveSlotSpoiler()` — otherwise an in-binary button
  would let a runner peek mid-race and defeat race mode. New accessor
  `Rando_CanRevealActiveSlotSpoiler()` (`rando.c` / `rando.h`) drives the
  button's enabled state so the player sees "(available after you finish the
  seed)" instead of a confusing `FileNotFound`.
- **File-missing handled via result, not hidden.** Rather than pre-checking the
  on-disk file to hide the entry, the result dialog surfaces `FileNotFound`
  (with `Rando_RevealResultDescription`) when the suppressed file is absent.

Gate uses `Rando_ActiveSlotHidesSpoiler()` (fail-closed) per memory
`race_mode_null_settings_failopen`. No `kGeneratorVersion` bump; guards clean;
`--rando-selftest` green; built + linked.

## 1. File-select reveal action menu  → delivered as native-window reveal
- [x] 1.1 Reveal action-menu surface. *(Done-differently: a "Race-mode spoiler"
  section in the native window `Panel_General`, not the SNES file-select menu —
  see As-built.)*
- [x] 1.2 Gated on `slot_kind == Randomizer` AND `race_mode == 1` AND the
  suppressed file existing. *(Gated on `Rando_IsActive() &&
  Rando_ActiveSlotHidesSpoiler()` — Randomizer slot + race/hidden, fail-closed.
  File-existence is surfaced via the result dialog's `FileNotFound`, not by
  hiding the entry.)*
- [x] 1.3 Confirmation dialog → reveal → success / failure result dialog.
  *(Confirm modal → `Rando_RevealActiveSlotSpoiler()` → result modal showing
  `Rando_RevealResultDescription(r)`, green on Ok / red on failure.)*
- [x] 1.4 Idempotent re-reveal. *(Re-clicking re-runs the action; the core
  overwrites and returns `Ok`. Plus the anti-cheat completion gate above.)*

## 2. Settings-screen warning
- [x] 2.1 Show "Spoiler will be suppressed until Reveal is invoked." when race
  mode is enabled. *(Added under the "Race mode" checkbox in `Panel_General`;
  shown only while `s->race_mode` is on.)*

## 3. Verify
- [ ] 3.1 Playtest: generate a race-mode slot, finish it, reveal via the native
  window, confirm the JSON lands and the stamp verifies; failure path preserves
  the file. *(Owner loop; needs the rebuilt binary.)*
- [ ] 3.2 Fresh-eyes audit before archive.
