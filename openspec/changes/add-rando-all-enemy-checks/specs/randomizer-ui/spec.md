## ADDED Requirements

### Requirement: All enemy tier is exposed distinctly from dungeon

The user-facing settings surfaces SHALL reserve `enemy_drop_checks=all` as a
distinct tier above `dungeon`. Labels, docs, spoilers, trackers, and native UI text
SHALL NOT use "all" for the dungeon-only tier. Interactive selectors MAY hide or
disable `all` until generation can honestly include every compatible emitted
killable enemy source.

When a requested `all` value is downgraded or rejected because a setting combination
cannot honestly support all emitted killable enemy sources, the UI and generated
spoiler SHALL show the effective value or the specific rejection reason.

#### Scenario: Selector distinguishes dungeon from all
- **WHEN** the enemy-drop-check selector shows the all-enemy tier
- **THEN** `dungeon` and `all` are separate choices with separate effective behavior

#### Scenario: Selector does not over-promise incomplete all
- **WHEN** the complete all-enemy registry is unavailable
- **THEN** the enemy-drop-check selector does not present a selectable `all` tier
  that would only activate dungeon enemy checks

#### Scenario: Effective downgrade is visible
- **WHEN** a requested `all` setting downgrades to `dungeon`, `keys`, or `off`
- **THEN** the UI and spoiler identify the effective tier instead of displaying the
  raw request as if all enemies were active

### Requirement: All-enemy output groups locations by source domain

Spoilers, trackers, reachability panels, and autotracker output SHALL include every
emitted all-enemy location and group it by a stable player-usable source domain:
dungeon room, overworld area/screen, boss arena, or scripted parent source. Location
names SHALL distinguish duplicate enemy types in the same group by coordinates,
source slot, or another stable disambiguator.

#### Scenario: Duplicate enemy type in one area
- **WHEN** two emitted all-enemy locations use the same enemy type in the same
  overworld area
- **THEN** spoiler and tracker names distinguish the two sources

#### Scenario: Visual marker suppressed but tracker present
- **WHEN** an all-enemy marker is hidden because of OAM or graphics pressure
- **THEN** tracker, spoiler, reachability, and checked-state output still include the
  location
