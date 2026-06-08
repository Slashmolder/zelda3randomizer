## ADDED Requirements

### Requirement: Enemy-shuffle live settings control

The native settings window SHALL render `enemy_shuffle` as a **live checkbox** in the rando-settings panel (`src/rando/rando_window/rando_window.cpp`, where `boss_shuffle` / `drop_shuffle` are wired live — `rando_window.cpp:803-814`; enemy_shuffle is presently a throwaway `off` placeholder at `:822`). This requirement **explicitly supersedes** the treatment of `enemy_shuffle` in the `ImGui-rendered settings panels at parity with the in-game screen` requirement's "Live axes…" scenario, which lists `enemy_shuffle` (and boss/drop) as disabled "coming soon" placeholders. Toggling it SHALL update the pending settings, the live `settings_hash`, and the share string exactly as the other shuffle checkboxes do. The control SHALL ship live only when the runtime substitution is actually installed for playable slots (`Rando_ActivateSidecarSlot` regenerates the enemy substitution), so the widget never lies about an inert axis. PC native window only; the in-game screen stays compiled out on PC.

> **Reconciliation note (apply-time):** the baseline parity scenario (`randomizer-native-window/spec.md:86`) is **already stale** — it says boss/drop shuffle are disabled placeholders "because boss/drop shuffle is runtime-inert," but both are live as-built (`rando.c:1932-1947`, `rando_window.cpp:803-814`); the `add-rando-shuffles-and-minigames` native-window delta (archive-pending) carries that correction. Rather than re-author three unarchived changes' co-owned requirement here (high transcription risk while the baseline is mid-flux), this change supersedes the enemy_shuffle clause in-text and flags the parity scenario for a single reconciliation at archive — removing boss/drop/enemy/entrance from the disabled list, leaving only genuinely-unshipped axes (e.g. the glitches shuffle).

#### Scenario: Enemy-shuffle checkbox is live and reflected in the share string
- **WHEN** the user toggles the enemy-shuffle checkbox in the native settings window
- **THEN** the pending settings update, the displayed `settings_hash` / share string change, and generating a slot with it on produces a runtime-active enemy substitution

#### Scenario: Tooltip is a durable player-fact
- **WHEN** the user hovers the enemy-shuffle checkbox
- **THEN** the tooltip states a concise durable fact (e.g. "Randomizes which enemies appear in each room") with no status, caveat, or implementation detail
