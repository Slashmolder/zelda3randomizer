## ADDED Requirements

### Requirement: Customizer-mode settings-screen entry

The PC native settings window SHALL provide a customizer-mode toggle (Randomizer → General → "Customizer") that, when enabled, exposes a manifest path field + "Load manifest" button (text-entry, matching the spoiler-save pattern — SDL2 has no portable native file dialog). The field SHALL accept a path to a manifest in the strict line-based YAML subset defined in `customizer.h`. When a manifest loads successfully, the panel SHALL display:

- The number of manual placements pinned (count of `placements:` entries).
- A pool-override summary when present (`pool +N/-M` add/remove counts).
- A capped per-pin preview (`location: item` bullets) under a collapsible node.
- The Generate button relabeled "Generate from manifest & start new slot".

A failed load SHALL display the parser's one-line error (with line number) inline and uninstall any previously-loaded manifest; the cross-field validation SHALL then block Generate ("no manifest is loaded") until a manifest loads or the toggle is turned off. Enabling race mode together with customizer mode SHALL be blocked by the same inline validation.

When customizer mode is disabled, the manifest reference SHALL be cleared (uninstalled) and the standard Generate flow proceeds unchanged. The manifest is SESSION state: a `customizer_active` bit restored from persisted window settings at startup SHALL be cleared (nothing re-installs the manifest, so a stale flag would block Generate with no visible cause).

#### Scenario: Customizer toggle exposes manifest picker
- **WHEN** the player enables Customizer mode in the settings window
- **THEN** the manifest path field + Load button appear; the Generate button is relabeled "Generate from manifest & start new slot"

#### Scenario: Invalid manifest surfaces inline error
- **WHEN** the user loads a manifest that references an unknown location name
- **THEN** the panel displays the parser error naming the line + unknown name; Generate is blocked until a valid manifest loads

#### Scenario: Customizer disabled returns to standard flow
- **WHEN** the player disables Customizer mode after loading a manifest
- **THEN** the manifest is uninstalled; the standard Generate flow runs

#### Scenario: Persisted customizer flag does not survive restart
- **WHEN** the window persisted settings with `customizer_active` set and the program restarts
- **THEN** the restored pending settings have `customizer_active` cleared; the user re-enables the toggle and reloads the manifest
