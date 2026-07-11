## ADDED Requirements

### Requirement: All-enemy logic includes reachability and kill routes

Every emitted `enemy_drop_checks=all` location SHALL require both source reachability
and a reviewed kill route. Reachability SHALL use the correct dungeon room, reviewed
underworld cave/interior predicate, overworld area/screen, boss arena, or
scripted-spawn parent predicate. Kill routes SHALL use the effective enemy type and
HP for the source.

Static overworld source reachability SHALL include the generated logic region, the
active overworld sprite-list stage, and a conservative kill route. Post-Agahnim
stage-2 rows SHALL remain gated on `DefeatAgahnim`; placement SHALL separately
prevent Agahnim-prerequisite item classes from landing there.

Reviewed underworld exceptions SHALL include a direct access predicate and SHALL
not inherit dungeon small-key depth terms unless the row also carries reviewed
key-depth metadata.

Thrown-object routes SHALL be allowed when engine damage data shows that the thrown
object can damage the source and the reachable area contains enough usable throwables
to deal lethal damage. If a source requires N thrown-pot hits, the route SHALL require
at least N reachable pots or equivalent throwables; one pot SHALL NOT satisfy a
two-pot kill. Generated thrown-pot branches SHALL additionally require effective pot
shuffle to be off. While any effective pot-sanity tier is active, those branches
SHALL be disabled and the enemy SHALL require its reviewed inventory-combat route.

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
- **THEN** effective pot sanity disables the thrown-pot branch for that enemy check
- **AND** the reviewed inventory-combat route remains required

#### Scenario: Independent pot item and weapon ordering
- **WHEN** an enemy's thrown-pot route uses pots that are not needed as prior
  pot-sanity item checks for that same branch
- **THEN** the current conservative model still disables that route while effective
  pot sanity is active
- **AND** a future per-source consumption model MAY safely restore the route

#### Scenario: Unknown kill route blocks honest all
- **WHEN** no conservative kill route can be modeled for a finite enemy source
- **THEN** the source SHALL not be emitted
- **AND** effective `All` SHALL downgrade visibly or reject until a reviewed
  predicate exists

### Requirement: All-enemy interaction logic is explicit

Door shuffle, pot shuffle, enemy shuffle, boss shuffle, and entrance shuffle SHALL
interact with `all` only through explicit generated predicates or explicit visible
normalization/rejection. Door shuffle composes through generated door x ordinary
enemy-check bridge rows and source predicates. Enemy shuffle normalizes requested
`All` to the highest lower tier allowed by existing derived rules, normally `Keys`
but `Off` when the keys tier is unsupported, until a future change explicitly makes
enemy shuffle placement-affecting for all-enemy kill logic and updates digest/corpus
expectations. Boss shuffle composes with `All` because boss/miniboss checks bind to
the destination event and preserve existing boss reward behavior, unless another
rule lowers the effective tier further. Entrance shuffle normalizes requested `All`
to `Dungeon` until all-enemy overworld/domain reachability is modeled against the
entrance graph, unless another rule lowers the effective tier further.

#### Scenario: Door shuffle uses all-enemy bridge
- **WHEN** `enemy_drop_checks=all` is requested with door shuffle
- **THEN** derived settings keep `All`
- **AND** door reachability uses generated enemy-check bridge rows and source
  predicates instead of treating all-enemy locations as vanilla-door reachable

#### Scenario: Enemy shuffle normalizes all
- **WHEN** `enemy_drop_checks=all` is requested with enemy shuffle
- **THEN** derived settings normalize to the highest lower tier allowed by existing
  derived rules instead of using vanilla kill predicates for shuffled enemies

#### Scenario: Boss shuffle preserves boss-domain all
- **WHEN** `enemy_drop_checks=all` is requested with boss shuffle
- **THEN** derived settings keep `All` unless another rule lowers the effective tier
  further

#### Scenario: Entrance shuffle normalizes all
- **WHEN** `enemy_drop_checks=all` is requested with entrance shuffle before
  all-enemy entrance-graph reachability is modeled
- **THEN** derived settings normalize to `Dungeon` unless another rule lowers the
  effective tier further
