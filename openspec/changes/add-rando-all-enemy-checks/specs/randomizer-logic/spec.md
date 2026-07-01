## ADDED Requirements

### Requirement: All-enemy locations require sound reach predicates

An all-enemy source SHALL NOT be emitted into logic unless it has a conservative
reach predicate for the room or overworld screen that contains it and a
source-type kill route.

#### Scenario: Candidate lacks reachability coverage
- **WHEN** the static candidate audit finds a killable enemy source but logic
  cannot map it to a reachable region or room predicate
- **THEN** the source remains audit-only and is not a fillable location

#### Scenario: Dungeon room predicate coverage is incomplete
- **WHEN** the audit reports dungeon candidates in rooms without a reusable room
  predicate
- **THEN** codegen SHALL NOT emit those candidates until a reviewed predicate is
  added or the source is explicitly excluded

#### Scenario: Enemy source requires a kill route
- **WHEN** codegen emits an ordinary `Enemy` location
- **THEN** its reach predicate includes a source-type inventory kill predicate or
  a generated thrown-pot kill predicate in addition to room/key access

#### Scenario: Thrown-pot route is count based
- **WHEN** a source type requires multiple thrown-pot hits according to engine HP
  and damage tables
- **THEN** the thrown-pot route requires at least that many reachable pots in the
  room
