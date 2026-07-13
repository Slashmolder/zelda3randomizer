## ADDED Requirements

### Requirement: Key-item settings and effective-state UI

The native settings window SHALL expose `Key rings` (`Off`, `Random mix`, `All`)
near small-key mode and a `Skeleton Key` checkbox labeled as bonus-only/not used by
logic. When rings normalize Off, the control SHALL show the effective value and a
reason rather than implying rings will generate.

#### Scenario: Vanilla key mode explains disabled rings

- **WHEN** small keys are effectively Vanilla and the user requests Key Rings
- **THEN** the UI shows effective Off with a Vanilla-keys explanation while leaving
  Skeleton Key independently selectable

### Requirement: Key-item spoiler and tracking output

Spoiler JSON/text SHALL include requested/effective ring mode, eligible and selected
family masks/names, selection algorithm version, and Skeleton enabled state, in
addition to normal placement rows for actual items. Tracker/autotracker output
SHALL expose selected/owned rings and Skeleton ownership while preserving numeric
remaining-key counters. Unselected ring families SHALL not appear as missing
progression.

#### Scenario: Random seed is independently auditable

- **WHEN** a Random-ring spoiler is written
- **THEN** it lists eligible and selected dungeon names so the exact mix can be
  checked without rerunning selection

#### Scenario: Tracker separates ownership from remaining keys

- **WHEN** a ring is owned and some of its granted keys have been spent
- **THEN** the tracker reports the ring owned and the current numeric key counter
  separately
