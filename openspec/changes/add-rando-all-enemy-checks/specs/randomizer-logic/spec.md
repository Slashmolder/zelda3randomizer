## ADDED Requirements

### Requirement: All-enemy logic includes reachability and kill routes

Every emitted `enemy_drop_checks=all` location SHALL require both source reachability
and a reviewed kill route. Reachability SHALL use the correct dungeon room,
overworld area/screen, boss arena, or scripted-spawn parent predicate. Kill routes
SHALL use the effective enemy type and HP for the source.

Static overworld source reachability SHALL include the generated logic region, the
active overworld sprite-list stage, and a conservative kill route. Post-Agahnim
stage-2 rows SHALL remain gated on `DefeatAgahnim`; placement SHALL separately
prevent Agahnim-prerequisite item classes from landing there.

Thrown-object routes SHALL be allowed when engine damage data shows that the thrown
object can damage the source and the reachable area contains enough usable throwables
to deal lethal damage. If a source requires N thrown-pot hits, the route SHALL require
at least N reachable pots or equivalent throwables; one pot SHALL NOT satisfy a
two-pot kill. Each thrown-object branch SHALL record the specific throwable source
set or an equivalent disjoint-count proof so logic cannot count the same pot as both
a required pot-sanity item check and a future thrown weapon before the enemy check.

#### Scenario: Inventory kill route
- **WHEN** an emitted all-enemy source has a reviewed inventory-based kill route
- **THEN** the location requires the source reach predicate and that combat predicate

#### Scenario: Counted thrown-pot kill route
- **WHEN** a killable source requires two thrown-pot hits and the reachable room has
  exactly two usable pots before the check is collected
- **THEN** logic MAY allow the enemy check through the thrown-pot route

#### Scenario: Insufficient thrown pots
- **WHEN** a killable source requires two thrown-pot hits and the reachable room has
  only one usable pot
- **THEN** logic SHALL NOT allow the enemy check through the thrown-pot route

#### Scenario: Required pot item cannot also be weapon
- **WHEN** a pot's placed item is required before an enemy check and lifting that pot
  consumes the throwable needed for the enemy kill route
- **THEN** that pot does not count toward the thrown-object branch for that enemy
  check

#### Scenario: Independent pot item and weapon ordering
- **WHEN** an enemy's thrown-pot route uses pots that are not needed as prior
  pot-sanity item checks for that same branch
- **THEN** logic MAY count those pots as weapons if the damage and count
  requirements are met

#### Scenario: Unknown kill route blocks honest all
- **WHEN** no conservative kill route can be modeled for a finite enemy source
- **THEN** the source SHALL not be emitted
- **AND** effective `All` SHALL downgrade visibly or reject until a reviewed
  predicate exists

### Requirement: All-enemy interaction logic is explicit

Door shuffle, pot shuffle, enemy shuffle, boss shuffle, and entrance shuffle SHALL
interact with `all` only through explicit generated predicates or explicit visible
normalization/rejection. Door shuffle requires non-key enemy door-region bridges or
equivalent shuffled-door reachability before `all` can stay active. Enemy shuffle
normalizes requested `All` to the highest lower tier allowed by existing derived
rules, normally `Keys` but `Off` when the keys tier is unsupported, until a future
change explicitly makes enemy shuffle placement-affecting for all-enemy kill logic
and updates digest/corpus expectations. Boss shuffle normalizes requested `All` to
`Dungeon` until boss/miniboss identity is modeled against assigned boss rooms and
rewards, unless another rule lowers the effective tier further. Entrance shuffle
normalizes requested `All` to `Dungeon` until all-enemy overworld/domain reachability
is modeled against the entrance graph, unless another rule lowers the effective tier
further.

#### Scenario: Door shuffle lacks all-enemy bridge
- **WHEN** `enemy_drop_checks=all` is requested with door shuffle but no non-key
  all-enemy door bridge exists
- **THEN** derived settings normalize to `Keys` instead of treating all-enemy
  locations as vanilla-door reachable

#### Scenario: Enemy shuffle normalizes all
- **WHEN** `enemy_drop_checks=all` is requested with enemy shuffle
- **THEN** derived settings normalize to the highest lower tier allowed by existing
  derived rules instead of using vanilla kill predicates for shuffled enemies

#### Scenario: Boss shuffle normalizes boss-domain all
- **WHEN** `enemy_drop_checks=all` is requested with boss shuffle before boss-domain
  all-enemy identity is modeled
- **THEN** derived settings normalize to `Dungeon` unless another rule lowers the
  effective tier further

#### Scenario: Entrance shuffle normalizes all
- **WHEN** `enemy_drop_checks=all` is requested with entrance shuffle before
  all-enemy entrance-graph reachability is modeled
- **THEN** derived settings normalize to `Dungeon` unless another rule lowers the
  effective tier further
