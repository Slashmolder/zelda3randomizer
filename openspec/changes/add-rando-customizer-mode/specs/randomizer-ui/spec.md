## ADDED Requirements

### Requirement: Customizer-mode settings-screen entry

The settings screen SHALL provide a customizer-mode toggle that, when enabled, exposes a manifest-file picker. The picker SHALL accept a path to a YAML or JSON manifest on the host filesystem. When a valid manifest is loaded, the settings-screen preview SHALL display:

- The number of manual placements in the manifest (count from `placements:` entries).
- Any pool-override summary (e.g., "+2 ProgressiveSword, -1 Rupoor").
- A "Generate from manifest" button replacing the standard "Generate" button.

When customizer mode is disabled, the existing settings-screen flow proceeds unchanged.

> **Stub status**: file-picker UI shape (modal vs. text-entry) deferred to Phase D apply-time.

#### Scenario: Customizer toggle exposes manifest picker
- **WHEN** the player enables Customizer mode in the settings screen
- **THEN** a manifest-file picker appears; the Generate button is replaced with "Generate from manifest"

#### Scenario: Invalid manifest surfaces inline error
- **WHEN** the user picks a manifest that references an unknown location id
- **THEN** the settings screen displays an inline error naming the unknown location; Generate is blocked

#### Scenario: Customizer disabled returns to standard flow
- **WHEN** the player disables Customizer mode after picking a manifest
- **THEN** the manifest reference is cleared; the standard Generate flow runs
