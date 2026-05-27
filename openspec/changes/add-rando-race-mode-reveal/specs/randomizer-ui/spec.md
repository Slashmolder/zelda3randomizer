## ADDED Requirements

### Requirement: Race-mode settings-screen toggle

The settings screen SHALL expose a user-toggleable Race-mode field. The toggle SHALL default to off. When the user enables race mode, the settings-screen preview SHALL display a one-line warning: "Spoiler will be suppressed until Reveal is invoked." When race mode is off, the standard spoiler-emission contract applies.

Toggling race mode SHALL be permitted at any time before generation. After generation, the slot's `race_mode` is fixed by the stored `settings_hash`; the toggle on the settings screen has no effect on already-generated slots.

#### Scenario: Toggle default state
- **WHEN** the settings screen is opened for a fresh slot
- **THEN** the Race-mode field is unchecked (off); the spoiler-emission warning is not shown

#### Scenario: Toggle preview surfaces the consequence
- **WHEN** the user enables Race mode in the settings screen
- **THEN** a one-line warning appears in the preview: "Spoiler will be suppressed until Reveal is invoked."

#### Scenario: Toggle has no effect on existing slots
- **WHEN** the user toggles Race-mode on an already-generated slot from the file-select kind-edit menu
- **THEN** the toggle is non-interactive (display only); the existing slot's `race_mode` is whatever was stored in the slot's `settings_hash` at generation time

### Requirement: Race-mode reveal UI

The file-select screen's per-slot action menu SHALL include a "Reveal Spoiler" option, visible only when:
1. The active slot has `slot_kind == Randomizer`.
2. The slot's stored `race_mode` bit is 1.
3. The on-disk suppressed-spoiler file exists at `<spoiler_dir>/<share_string>.json`.

The action SHALL invoke `Rando_RevealSpoiler(slot_index)` (per `randomizer-core / Race-mode reveal action`). UI flow:
1. Confirmation dialog: "Reveal spoiler? This writes the placement to `<spoiler_dir>/<share_string>.json`." [Reveal / Cancel]
2. On confirm, run the action with a brief progress indicator (regeneration is bounded by the `randomizer-core / Generation performance budget` — 2s desktop / 5s Switch).
3. On success: dialog "Spoiler revealed at `<spoiler_dir>/<share_string>.json`. The race-mode bit remains set on the slot."
4. On failure: dialog with the specific failure reason (`StampMismatch`, `CrcMismatch`, `ShareStringMismatch`, `VersionMismatch`) and a recommendation (e.g., "VersionMismatch — use a binary built from generator_version N to reveal this slot").

The action SHALL be idempotent: invoking Reveal a second time on a slot with an already-revealed full spoiler returns success without rewriting (or with a confirmation to overwrite, at the implementer's discretion).

#### Scenario: Reveal entry visible only for race-mode slots
- **WHEN** the file-select cursor is on a Randomizer slot with `race_mode == 0`
- **THEN** the "Reveal Spoiler" action is not shown in the per-slot menu

#### Scenario: Reveal entry hidden when suppressed file missing
- **WHEN** the active slot is race-mode but `<spoiler_dir>/<share_string>.json` does not exist (deleted, wrong directory, etc.)
- **THEN** the "Reveal Spoiler" action is hidden; the player is advised to restore the file from backup, or the spoiler is permanently lost

#### Scenario: Reveal action completes within budget
- **WHEN** Reveal is invoked on a Phase A default-settings race-mode slot
- **THEN** the regeneration + stamp comparison completes within 2 seconds (desktop) / 5 seconds (Switch) — same budget as initial generation

#### Scenario: Failure preserves the suppressed file
- **WHEN** the action returns any failure code
- **THEN** the on-disk suppressed file is unchanged; the player sees the failure dialog; the slot remains usable (placement still loads from sidecar, only the spoiler is unrevealed)
