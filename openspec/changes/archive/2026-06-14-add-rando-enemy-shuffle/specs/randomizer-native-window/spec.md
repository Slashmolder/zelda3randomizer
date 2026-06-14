## ADDED Requirements

### Requirement: Enemy-shuffle live settings control

The native settings window SHALL render `enemy_shuffle` as a **live checkbox** in the rando-settings panel. Toggling it SHALL update the pending settings, the live `settings_hash`, and the share string exactly as the other shuffle checkboxes do. The control SHALL ship live only when the runtime substitution is actually installed for playable slots (`Rando_ActivateSidecarSlot` regenerates the enemy substitution), so the widget never lies about an inert axis. PC native window only; the in-game screen stays compiled out on PC.

#### Scenario: Enemy-shuffle checkbox is live and reflected in the share string
- **WHEN** the user toggles the enemy-shuffle checkbox in the native settings window
- **THEN** the pending settings update, the displayed `settings_hash` / share string change, and generating a slot with it on produces a runtime-active enemy substitution

#### Scenario: Tooltip is a durable player-fact
- **WHEN** the user hovers the enemy-shuffle checkbox
- **THEN** the tooltip states a concise durable fact (e.g. "Randomizes which enemies appear in each room") with no status, caveat, or implementation detail
