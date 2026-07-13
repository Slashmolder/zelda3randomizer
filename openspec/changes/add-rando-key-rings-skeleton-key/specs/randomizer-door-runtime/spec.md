## ADDED Requirements

### Requirement: Key Ring grants the complete dungeon key stock

Collecting a dungeon Key Ring SHALL max-write that family's persisted key counter
to the generated complete authored stock and SHALL update `link_num_keys` only when
the player is currently in that key family. Hyrule Castle proper SHALL use the
Escape/Sewers key slot. The grant SHALL never lower a counter and SHALL use the
normal direct-grant confirmation path.

#### Scenario: Off-dungeon grant parks the keys

- **WHEN** a Turtle Rock Key Ring is collected in Ice Palace
- **THEN** Turtle Rock's saved counter is credited and Ice Palace's live counter
  is unchanged

#### Scenario: In-dungeon grant updates HUD immediately

- **WHEN** a Turtle Rock Key Ring is collected in Turtle Rock
- **THEN** both the saved and live Turtle Rock counters show the full stock and
  the HUD refreshes

### Requirement: Skeleton Key bypasses only small-key payment

At a locked small-key door, owned Skeleton Key SHALL take the existing successful
open path without decrementing `link_num_keys` or `link_generic_keys`. The existing
already-open door-shuffle branch and partner mirroring SHALL remain authoritative.
Big-key-door checks SHALL be unchanged and SHALL occur independently of Skeleton
ownership.

#### Scenario: Relocated key door stays persistent

- **WHEN** Skeleton Key opens a relocated door-shuffle small-key door
- **THEN** both logical halves receive the existing open-state mirror and no key
  counter is spent

#### Scenario: GenericKey pool is preserved

- **WHEN** Retro is active, Skeleton Key is owned, and a small-key door opens
- **THEN** neither the live nor persisted GenericKey counter is decremented
