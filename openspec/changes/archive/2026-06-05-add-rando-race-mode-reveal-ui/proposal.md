## Why

Follow-up to `add-rando-race-mode-reveal`. That change shipped the race-mode
**core**: on-disk spoiler suppression at generate time, the CLI
`--reveal-spoiler` path (regenerate placement + verify the SHA-256 stamp + write
the full JSON), the native-window **Race mode** toggle
(`rando_window.cpp` `ImGui::Checkbox("Race mode", …)`), the race-mode Spoiler-tab
gate, and a key-bound in-binary reveal (`kKeys_RandoRevealSpoiler` →
`Rando_RevealSpoiler` for the active slot).

It **deferred** two in-binary UI surfaces that needed file-select / settings-
screen plumbing not present at the time, because the CLI + keybind already cover
the tournament-admin and convenience use cases:

1. a **file-select per-slot "Reveal Spoiler" action menu**, and
2. the **settings-screen preview warning** shown when race mode is toggled on.

These are carved here so the archived parent's spec baseline reflects only what
shipped (the parent's `randomizer-ui` delta had committed both as SHALLs).

## What Changes

- The file-select per-slot action menu gains a **"Reveal Spoiler"** entry,
  visible only for a Randomizer slot whose stored `race_mode` bit is 1 and whose
  suppressed-spoiler file exists on disk. Selecting it runs
  `Rando_RevealSpoiler(slot_index)` with a confirmation dialog and a
  success/failure result dialog (the same core action already reachable via the
  `RandoRevealSpoiler` keybind and the CLI).
- The settings UI shows a one-line preview warning when race mode is enabled:
  "Spoiler will be suppressed until Reveal is invoked."

## Capabilities

### New Capabilities

- `randomizer-ui`: ADDED "Race-mode reveal UI" (file-select per-slot action
  menu) and "Race-mode settings-screen warning" (the deferred preview string).

## Impact

- **Code**: the file-select per-slot action-menu surface
  (`src/select_file.c` / the `rando_window` file-select path) and the settings
  preview warning string. Builds on the already-shipped `Rando_RevealSpoiler`
  core — no new core logic.
- **No** placement or serialization change; **no** `kGeneratorVersion` bump.
- **Dependency**: `add-rando-race-mode-reveal` (archived) provides the reveal
  action, suppression, and stamp-verify this UI drives.
