# randomizer-native-window Specification (delta)

## ADDED Requirements

### Requirement: Tracker windows present only player-knowledge

The Check Tracker, Map Tracker, and Reachability panel SHALL render from the
knowledge-limited live reachability, so no availability status, count, or
region tally states anything untrue under an assignment consistent with the
player's observations. Location names and region grouping remain the static
registry bindings (verified seed-independent — they carry no assignment
information and need no masking). Undiscovered hidden-identity dungeons keep
their static region names but SHALL carry a short "?" prefix marker (plus an
explanatory hover tooltip) instead of implying a dead end, and the Check Tracker SHALL show a summary line stating
how many hidden-identity dungeons have not yet been entered.

#### Scenario: Cave contents grey until their interior is discovered

- **WHEN** a cave-entrance-shuffle slot is active and a shuffled cave interior
  has not been entered
- **THEN** that interior's checks render unavailable (grey) in all three
  surfaces and are excluded from every availability count, and entering the
  door that leads to it lights them up live

#### Scenario: Unexplored dungeon affordance

- **WHEN** a dungeon-topology slot is active with unentered hidden-identity
  dungeons
- **THEN** their region rows render with the "?" prefix marker (with an explanatory
  hover tooltip) and the summary line reports the count of dungeons not yet
  entered

#### Scenario: Static presentation is seed-independent

- **WHEN** two seeds with different topology assignments but identical
  settings are compared at sphere 0 with nothing discovered
- **THEN** the tracker surfaces render identically (names, grouping, counts,
  statuses) — nothing displayed depends on the hidden assignments

### Requirement: Tracker text fits narrow windows and the default font

User-visible strings in the tracker and settings windows SHALL be ASCII-only:
the default ImGui font atlas covers Basic Latin plus Latin-1, so any other
glyph (e.g. an em-dash) renders as a fallback "?" character. Status markers
added to region rows SHALL be short enough that the trailing availability
counts remain visible at the default tiled-layout width, where the Check
Tracker occupies roughly a sixth of the display.

#### Scenario: Marker does not truncate the row's counts

- **WHEN** the Check Tracker is opened at the tiled-layout default width and a
  hidden-identity dungeon is unentered
- **THEN** its row shows the marker AND its full "N avail, M/T checked" counts
  without clipping

#### Scenario: No fallback glyphs

- **WHEN** any tracker or settings window string is rendered with the default
  font
- **THEN** every character resolves to a real glyph (no "?" substitutions)
