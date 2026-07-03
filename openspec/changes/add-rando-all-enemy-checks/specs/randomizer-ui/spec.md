## ADDED Requirements

### Requirement: All enemy tier is exposed distinctly from dungeon

The user-facing settings surfaces SHALL expose `enemy_drop_checks=all` as a
distinct tier above `dungeon` when it can remain effective for the current setting
combination. Labels, docs, spoilers, trackers, and native UI text SHALL NOT use
"all" for the dungeon-only tier. Interactive selectors MAY hide or disable `all`
when derived rules would immediately lower it to `dungeon`, `keys`, or `off`.

When a requested `all` value is downgraded or rejected because a setting combination
cannot honestly support all generated all-tier sources, the UI and generated spoiler
SHALL show the effective value or the specific rejection reason.

#### Scenario: Selector distinguishes dungeon from all
- **WHEN** the enemy-drop-check selector shows the all-enemy tier
- **THEN** `dungeon` and `all` are separate choices with separate effective behavior

#### Scenario: Selector does not over-promise downgraded all
- **WHEN** boss shuffle, entrance shuffle, door shuffle, enemy shuffle, or vanilla
  small-key mode would downgrade `all`
- **THEN** the enemy-drop-check selector does not present a selectable `all` tier
  that would immediately display as a lower effective tier

#### Scenario: Effective downgrade is visible
- **WHEN** a requested `all` setting downgrades to `dungeon`, `keys`, or `off`
- **THEN** the UI and spoiler identify the effective tier instead of displaying the
  raw request as if all enemies were active

### Requirement: All-enemy output groups locations by source domain

Spoilers, trackers, reachability panels, and autotracker output SHALL include every
generated all-tier location and group it by a stable player-usable source domain:
dungeon room, reviewed underworld room, or overworld area/screen in the current
implementation, with boss arena and scripted parent domains reserved for future
source registries. Location names SHALL distinguish duplicate enemy types in the
same group by coordinates, source slot, or another stable disambiguator.

#### Scenario: Duplicate enemy type in one area
- **WHEN** two emitted all-enemy locations use the same enemy type in the same
  overworld area
- **THEN** spoiler and tracker names distinguish the two sources

#### Scenario: Visual marker suppressed but tracker present
- **WHEN** an all-enemy marker is hidden because of OAM or graphics pressure
- **THEN** tracker, spoiler, reachability, and checked-state output still include the
  location
