## MODIFIED Requirements

### Requirement: Enemy-drop checks selector and tracker presentation

The settings UI SHALL expose enemy-drop checks separately from `drop_shuffle`, with
distinct `Off`, `Enemy key drops`, `Dungeon enemies`, and `All enemies` choices.
It SHALL present the effective tier consistently: vanilla keys normalize the selector
to `Off`; door shuffle preserves supported `Keys`, `Dungeon`, and `All`; enemy shuffle
lowers `Dungeon`/`All` to `Keys` (or `Off` when forced-key checks are unsupported);
entrance shuffle lowers `All` to `Dungeon`; boss shuffle and pot shuffle preserve
`All`. A selector MAY disable a choice that would immediately normalize lower, but
it SHALL NOT label `Dungeon` as `All`.

Spoilers, the native location tracker, reach panel, and auto-tracker output SHALL
include only active enemy checks and group them by a stable player-usable domain:
dungeon/reviewed-underworld room, overworld area or screen, GT-miniboss arena, or
scripted parent. The spoiler SHALL record the effective tier so a normalized raw
request does not imply phantom checks.

#### Scenario: Selector is separate from drop shuffle
- **WHEN** the user opens randomizer settings
- **THEN** `drop_shuffle` remains the prize-pack control and enemy-drop checks use a
  separate four-tier selector

#### Scenario: Door shuffle preserves dungeon and all choices
- **WHEN** door shuffle is active and its generated enemy-check bridge is valid
- **THEN** the selector can expose effective `Dungeon` and `All` tiers

#### Scenario: Trackers show only active enemy checks
- **WHEN** an enemy-drop tier is effective
- **THEN** tracker and spoiler surfaces show exactly that tier's generated locations,
  grouped by their stable source domain

#### Scenario: All-tier tracker includes every reviewed domain
- **WHEN** effective `enemy_drop_checks = All`
- **THEN** tracker and spoiler surfaces include dungeon, overworld, reviewed
  underworld, GT-miniboss, and reviewed repeatable scripted locations

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
- **WHEN** entrance shuffle, enemy shuffle, or vanilla small-key mode would
  downgrade `all`
- **THEN** the enemy-drop-check selector does not present a selectable `all` tier
  that would immediately display as a lower effective tier

#### Scenario: Effective downgrade is visible
- **WHEN** a requested `all` setting downgrades to `dungeon`, `keys`, or `off`
- **THEN** the UI and spoiler identify the effective tier instead of displaying the
  raw request as if all enemies were active

### Requirement: All-enemy output groups locations by source domain

Spoilers, trackers, reachability panels, and autotracker output SHALL include every
generated all-tier location and group it by a stable player-usable source domain:
dungeon room, reviewed underworld room, overworld area/screen, GT-miniboss arena, or
scripted parent. Flat machine-readable placement rows MAY carry this grouping as
region/domain metadata rather than nested JSON. Location names SHALL distinguish
duplicate enemy types in the same group by coordinates, source slot, or another
stable disambiguator.

#### Scenario: Duplicate enemy type in one area
- **WHEN** two emitted all-enemy locations use the same enemy type in the same
  overworld area
- **THEN** spoiler and tracker names distinguish the two sources

#### Scenario: Visual marker suppressed but tracker present
- **WHEN** an all-enemy marker is hidden because of OAM or graphics pressure
- **THEN** tracker, spoiler, reachability, and checked-state output still include the
  location
