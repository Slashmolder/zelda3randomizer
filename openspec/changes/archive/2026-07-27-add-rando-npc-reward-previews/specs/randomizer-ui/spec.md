## ADDED Requirements

### Requirement: Independent in-game NPC reward-preview control

The in-game advanced Hints page SHALL expose the independent NPC reward-preview
boolean and disclose “Original/US dialogue only.” It SHALL remain selectable
with clue profile Off. Profile cycle/reset actions SHALL preserve the boolean.

Cursor bounds, help text, focus, controller/keyboard navigation, backing out,
and Switch compile guards SHALL include the new row without overlapping the
existing detail-page information area.

#### Scenario: In-game Hints Off plus previews On
- **WHEN** the player selects clue profile Off and enables NPC reward previews
- **THEN** the seed summary shows the two independent values and generation
  retains Off plus On

#### Scenario: Detail-page navigation reaches the new row
- **WHEN** the player navigates every advanced Hints row in either direction
- **THEN** focus reaches the preview row exactly once, remains within bounds,
  and the corresponding help text names the Original/US limitation

#### Scenario: Reset does not erase independent choice
- **WHEN** previews are On and the player resets or cycles the clue profile
- **THEN** only clue-policy fields change and previews remain On
