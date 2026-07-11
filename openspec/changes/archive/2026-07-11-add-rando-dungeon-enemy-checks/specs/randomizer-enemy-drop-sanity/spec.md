## ADDED Requirements

### Requirement: Dungeon-enemy tier emits ordinary dungeon enemy checks

The randomizer SHALL provide `enemy_drop_checks=dungeon` as a higher tier than
`keys`. When active, it SHALL include every active forced-key `EnemyDrop` check
and the generated ordinary dungeon `Enemy` checks whose sources have reviewed
eligibility and reachability metadata.

#### Scenario: Registry emits eligible dungeon enemies
- **WHEN** local ROM assets and key-depth metadata are available
- **THEN** the generator emits one ordinary `Enemy` location for each eligible
  dungeon source with a reviewed room reachability predicate and writes the
  runtime `(room, source_slot) -> loc_id` lookup

#### Scenario: Key-depth-only dungeon enemies stay audit-only
- **WHEN** a safe dungeon source has only key-depth ROOM metadata and no reviewed
  room reachability predicate
- **THEN** the generator SHALL record it as audit-only instead of emitting it as a
  lobby-reachable ordinary `Enemy` location

#### Scenario: Existing key-drop checks are not duplicated
- **WHEN** the dungeon-enemy generator scans dungeon forced-key carriers that are
  already modeled by `enemy_drop_checks=keys`
- **THEN** those sources are excluded from the ordinary `Enemy` registry

#### Scenario: No-key-depth underworld sources stay out of dungeon scope
- **WHEN** a safe underworld sprite-table source lacks a key-depth ROOM row
- **THEN** the generator SHALL record it as outside the dungeon-only scope instead
  of emitting it as a freely reachable ordinary `Enemy` location

#### Scenario: Missing local registry fails closed
- **WHEN** `enemy_drop_checks=dungeon` is active but no generated ordinary enemy
  registry is present in the build
- **THEN** placement SHALL reject the seed instead of silently generating with an
  empty dungeon-enemy table

### Requirement: Ordinary enemy checks grant and persist at death time

Ordinary `Enemy` checks SHALL dispatch their placed item when the checked enemy
source dies, not by spawning a pickup that can be abandoned after the vanilla
death flag is set.

#### Scenario: Ordinary enemy dies
- **WHEN** an active ordinary enemy check dies in a dungeon room
- **THEN** the runtime dispatches that location's placed item, records the
  location checked, and presents the quiet receive or confirmation feedback

#### Scenario: Checked ordinary enemy room reloads
- **WHEN** a room containing an already checked ordinary enemy source is loaded
- **THEN** the runtime suppresses that enemy and consumes its source slot so later
  sprites in the same room keep stable source-slot identity

#### Scenario: Forced-key tier remains pickup based
- **WHEN** `enemy_drop_checks=keys` is effective
- **THEN** existing forced-key `EnemyDrop` rows keep their pickup-time carrier/drop
  path and ordinary `Enemy` rows remain inactive

### Requirement: Dungeon-enemy marker mode is configurable

Unchecked enemy-drop and ordinary enemy-check carriers SHALL support a local
marker preference that either reveals the placed item when the shared tile slot
can represent all active carrier markers unambiguously or intentionally uses the
neutral gold check glint.

#### Scenario: Item marker mode
- **WHEN** `[Graphics] EnemyDropMarker=item`
- **THEN** unchecked active enemy carriers draw the placed item marker when all
  active carrier markers resolve to the same icon
- **AND** they fall back to the neutral gold check glint when the placed item
  marker cannot be shown unambiguously

#### Scenario: Generic marker mode
- **WHEN** `[Graphics] EnemyDropMarker=generic`
- **THEN** unchecked active enemy carriers draw the neutral gold check glint without
  revealing the placed item

#### Scenario: Spawned enemy-drop item wins the shared icon slot
- **WHEN** an unchecked forced enemy-drop pickup is active after its carrier dies
- **THEN** the pickup attempts to render the real placed item
- **AND** live carrier markers do not overwrite the pickup icon while it is active

### Requirement: Door shuffle composes with ordinary dungeon-enemy rows

Requested `enemy_drop_checks=dungeon` SHALL remain effective when door shuffle is
active. Generated door x ordinary-enemy bridge rows SHALL carry the reviewed source
predicates and participate in door-layout digest/replay identity.

#### Scenario: Door shuffle requests dungeon
- **WHEN** settings request `enemy_drop_checks=dungeon` and `door_shuffle=basic`
- **THEN** derived settings keep effective `enemy_drop_checks=dungeon` and ordinary
  `Enemy` rows remain active through the generated door bridge

### Requirement: Enemy shuffle disables ordinary dungeon-enemy rows

Requested `enemy_drop_checks=dungeon` SHALL degrade to effective `keys` when enemy
shuffle is active, because placement does not currently know the actual shuffled
enemy type or enemy-shuffle HP scaling for each ordinary source slot.

#### Scenario: Enemy shuffle requests dungeon
- **WHEN** settings request `enemy_drop_checks=dungeon` and `enemy_shuffle=true`
- **THEN** derived settings use effective `enemy_drop_checks=keys` and ordinary
  `Enemy` rows are inactive

### Requirement: Ordinary enemy logic includes kill requirements

Generated ordinary `Enemy` locations SHALL require both room/key access and a
source-type kill route. A kill route MAY be inventory-based or thrown-pot based.

#### Scenario: Generic enemy requires generic combat
- **WHEN** an ordinary enemy source has no narrower reviewed source-type
  requirement
- **THEN** its inventory kill route uses the dungeon's generic combat predicate

#### Scenario: Thrown pots count as a kill route
- **WHEN** the engine damage table shows thrown pots deal normal HP damage to the
  source type
- **THEN** the generator MAY add a thrown-pot kill route for rooms with enough
  reachable liftable pots

#### Scenario: Multi-pot kills require enough pots
- **WHEN** a source type requires N thrown-pot hits to kill
- **THEN** the thrown-pot kill route is emitted only for rooms with at least N
  reachable liftable pots
