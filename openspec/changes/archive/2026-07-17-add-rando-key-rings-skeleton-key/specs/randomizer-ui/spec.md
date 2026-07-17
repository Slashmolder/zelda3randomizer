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
SHALL NOT expose selected or owned ring families, because doing so reveals whether
a dungeon rolled a ring before its key item is found. Those surfaces SHALL expose
only the live numeric remaining-key counter; Skeleton ownership MAY remain visible
as an ordinary bonus-item state. Spoiler output remains the explicit audit surface.

#### Scenario: Random seed is independently auditable

- **WHEN** a Random-ring spoiler is written
- **THEN** it lists eligible and selected dungeon names so the exact mix can be
  checked without rerunning selection

#### Scenario: Tracker does not reveal the pool shape

- **WHEN** a seed starts with either ordinary shuffled keys or a Key Ring for a
  dungeon
- **THEN** the tracker and autotracker both report zero keys and no ring-selection
  or ring-ownership field for that dungeon

#### Scenario: Ring collection updates the ordinary counter

- **WHEN** the player collects a dungeon's Key Ring
- **THEN** the tracker and autotracker report that dungeon's complete granted key
  stock through the same numeric counter used by ordinary key pickups
