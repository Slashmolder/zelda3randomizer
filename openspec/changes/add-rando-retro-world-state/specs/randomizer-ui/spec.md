## ADDED Requirements

### Requirement: World-state picker accepts Retro

The settings-screen world-state field SHALL accept `Retro` as a selectable value, completing the world-state-axis picker (alongside Open and Standard from Phase A). Phase A pinned the picker to Open + Standard via the `select_file.c:2520` re-scope gate per `tasks.md §14.1b`; this change un-gates Retro at that site.

This is an ADDED Requirement (not a modification of the Phase A "Settings screen" Requirement) so the Phase A scenarios (Preset application, Share-string paste, Invalid share string, Non-vanilla asset data dialog) are preserved verbatim and the un-gate doesn't conflict with the parallel Phase B `add-rando-inverted-world-state` change.

> **Stub status**: implementation-side gate-removal detail (precise edit at `select_file.c:2520`) deferred to apply-time.

#### Scenario: World-state row cycles to Retro
- **WHEN** the player cycles the world-state row left/right after this change has archived (and the Inverted un-gate has not yet archived)
- **THEN** the field sequence is `Open → Standard → Retro → Open`; cycling past Retro wraps to Open (Inverted is still gated until `add-rando-inverted-world-state` archives)

#### Scenario: Both un-gates archived
- **WHEN** both this change and `add-rando-inverted-world-state` have archived
- **THEN** the field sequence is the full Phase A axis: `Open → Standard → Inverted → Retro → Open`; cycle order matches `mode_state` enum ordering from `randomizer-core` settings-canonical-serialization (item #1)

#### Scenario: Retro seeds generate cleanly
- **WHEN** the player selects Retro and clicks Generate
- **THEN** generation runs against the Retro item pool (see `randomizer-core / Retro world-state item pool`) which inherits Open's region graph + adds shop locations; the resulting `settings_hash` differs from an otherwise-identical Open seed

#### Scenario: Phase A "Settings screen" scenarios preserved
- **WHEN** any Phase A "Settings screen" Requirement scenario is exercised against a Retro seed
- **THEN** behavior matches the Phase A scenario verbatim — the Retro un-gate adds capability without altering existing scenarios
