## ADDED Requirements

### Requirement: Race-mode settings-screen toggle

The settings screen SHALL expose a user-toggleable Race-mode field. The toggle SHALL default to off. When race mode is off, the standard spoiler-emission contract applies; when on, the on-disk spoiler is suppressed per `randomizer-save` / `randomizer-core`.

Toggling race mode SHALL be permitted at any time before generation. After generation, the slot's `race_mode` is fixed by the stored `settings_hash`; the toggle on the settings screen has no effect on already-generated slots.

> **Carved scope**: the settings-screen *preview warning string* shown when race
> mode is enabled, and the file-select per-slot "Reveal Spoiler" action menu,
> were deferred to follow-up change `add-rando-race-mode-reveal-ui`. The reveal
> *action* itself shipped in this change and is reachable via the CLI
> `--reveal-spoiler` and the `RandoRevealSpoiler` keybind.

#### Scenario: Toggle default state
- **WHEN** the settings screen is opened for a fresh slot
- **THEN** the Race-mode field is unchecked (off)

#### Scenario: Toggle has no effect on existing slots
- **WHEN** the user toggles Race-mode on an already-generated slot from the file-select kind-edit menu
- **THEN** the toggle is non-interactive (display only); the existing slot's `race_mode` is whatever was stored in the slot's `settings_hash` at generation time
