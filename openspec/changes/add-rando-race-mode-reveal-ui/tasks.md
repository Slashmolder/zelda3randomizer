# Tasks — add-rando-race-mode-reveal-ui

Follow-up carve-out from `add-rando-race-mode-reveal` (archived). The reveal
*action* (`Rando_RevealSpoiler`), suppression, and stamp-verify already shipped;
this change is the deferred in-binary UI only.

## 1. File-select reveal action menu
- [ ] 1.1 Add a per-slot action-menu surface to the file-select screen (it
  currently has only kind-toggle + load + copy buttons).
- [ ] 1.2 Add a "Reveal Spoiler" entry, gated on `slot_kind == Randomizer` AND
  stored `race_mode == 1` AND the suppressed file existing on disk.
- [ ] 1.3 Confirmation dialog → `Rando_RevealSpoiler(slot_index)` → success /
  failure result dialog (surface the `RandoRevealResult` code + recommendation).
- [ ] 1.4 Idempotent re-reveal (success without rewrite, or confirm-overwrite).

## 2. Settings-screen warning
- [ ] 2.1 Show the one-line preview warning "Spoiler will be suppressed until
  Reveal is invoked." when race mode is enabled in the settings UI.

## 3. Verify
- [ ] 3.1 Playtest: generate a race-mode slot, reveal via the file-select menu,
  confirm the JSON lands and the stamp verifies; failure path preserves the file.
- [ ] 3.2 Fresh-eyes audit before archive.
