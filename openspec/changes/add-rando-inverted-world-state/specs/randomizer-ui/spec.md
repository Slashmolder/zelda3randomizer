## ADDED Requirements

### Requirement: World-state picker accepts Inverted

The settings-screen world-state field SHALL accept `Inverted` as a selectable value, completing the world-state-axis picker (alongside Open and Standard from Phase A). Phase A pinned the picker to Open + Standard via the `select_file.c:2520` re-scope gate per `tasks.md §14.1b`; this change un-gates Inverted at that site.

This is an ADDED Requirement (not a modification of the Phase A "Settings screen" Requirement) so the Phase A scenarios (Preset application, Share-string paste, Invalid share string, Non-vanilla asset data dialog) are preserved verbatim and the un-gate doesn't conflict with the parallel Phase B `add-rando-retro-world-state` change.

> **Stub status**: implementation-side gate-removal detail (precise edit at `select_file.c:2520`) deferred to apply-time.

#### Scenario: World-state row cycles to Inverted
- **WHEN** the player cycles the world-state row left/right after this change has archived (and the Retro un-gate has not yet archived)
- **THEN** the field sequence is `Open → Standard → Inverted → Open`; cycling past Inverted wraps to Open (Retro is still gated until `add-rando-retro-world-state` archives)

#### Scenario: Inverted seeds generate cleanly
- **WHEN** the player selects Inverted and clicks Generate
- **THEN** generation runs against the Inverted region graph (see `randomizer-logic / Inverted world-state region graph`); the resulting `settings_hash` differs from an otherwise-identical Standard or Open seed

#### Scenario: Phase A "Settings screen" scenarios preserved
- **WHEN** any Phase A "Settings screen" Requirement scenario is exercised against an Inverted seed (preset application, share-string paste, invalid share string, non-vanilla assets dialog)
- **THEN** behavior matches the Phase A scenario verbatim — the Inverted un-gate adds capability without altering existing scenarios
