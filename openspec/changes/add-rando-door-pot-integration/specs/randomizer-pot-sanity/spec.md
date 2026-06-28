## MODIFIED Requirements

### Requirement: Pot keys are first-class shuffled checks under shuffled key modes

Pot-key locations SHALL remain first-class shuffled checks when effective door
shuffle is active. Door shuffle SHALL NOT normalize `pot_shuffle` to `off`.

When effective door shuffle is active, small keys and big keys SHALL still normalize
to Dungeon mode. Active pot-key locations SHALL add their vanilla small key to the
same dungeon's shuffled key pool. Non-pot small-key drops SHALL remain free
in-context drops. Vanilla pot-key drops SHALL NOT be counted as free drops while the
corresponding pot key is an active itemized location.

#### Scenario: Door shuffle keeps requested pot tier active

- **WHEN** a seed is generated with `mode.state=open`, `door_shuffle=basic`, and
  `pot_shuffle=keys`
- **THEN** the effective settings keep `pot_shuffle=keys`
- **AND** the key-pot locations in shuffled dungeons participate in placement and
  reachability
- **AND** the generated seed is not byte-identical to the same request with
  `pot_shuffle=off` solely because of forced-off normalization

#### Scenario: Cave entrance shuffle still forces pots off

- **WHEN** a seed is generated with effective cave-entrance shuffle and any non-off
  pot tier
- **THEN** the effective settings report `pot_shuffle=off`
- **AND** every pot location is inactive

#### Scenario: Door shuffle uses dungeon key mode with active pots

- **WHEN** a seed is generated with `door_shuffle=basic`, `pot_shuffle=all`, and a raw
  non-dungeon small-key mode
- **THEN** the effective small-key mode is Dungeon
- **AND** the active pot-key items remain confined to their own dungeon
