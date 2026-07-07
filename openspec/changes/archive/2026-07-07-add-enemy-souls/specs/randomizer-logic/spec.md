# randomizer-logic Specification (delta)

## ADDED Requirements

### Requirement: OP_SOULS_TIER_AT_LEAST predicate handler
The predicate VM SHALL provide an `OP_SOULS_TIER_AT_LEAST <tier:u8>` opcode that evaluates true when the seed's `souls_shuffle` tier is at or above the operand (off=0, bosses=1, bosses+enemies=2), enabling `NeedsSoul(X) := NOT SOULS_TIER_AT_LEAST(t) OR HAS_ITEM(Soul_X)`-style macros so a single logic blob serves all tiers.

#### Scenario: Off seed reproduces baseline reachability
- **WHEN** reachability is computed for a `souls_shuffle=off` seed
- **THEN** every souls-conditional predicate evaluates as if the soul requirement were absent, and reachability matches the pre-souls baseline

#### Scenario: Tier gates bind at their tier
- **WHEN** a predicate requires `SOULS_TIER_AT_LEAST(2)` context and the seed is `souls_shuffle=bosses`
- **THEN** the enclosed soul requirement does not bind, while `SOULS_TIER_AT_LEAST(1)` requirements do

### Requirement: Boss kill requires the assigned boss's soul
When a souls tier is active, `OP_CAN_KILL_BOSS` SHALL additionally require possession of the soul of the boss actually assigned to that dungeon, resolved through the same per-seed `boss_assignment` input (vanilla-boss fallback when boss shuffle is off) via a generated boss-pool-index → soul-item table in which both Agahnim entries map to the single Agahnim Soul.

#### Scenario: Soul requirement follows boss shuffle
- **WHEN** boss shuffle assigns Mothula to Eastern Palace in a souls-tier seed
- **THEN** Eastern Palace's boss and prize locations require the Mothula Soul (not the Armos Knights Soul), and the placer guarantees it is reachable before those locations are required

#### Scenario: Vanilla assignment without boss shuffle
- **WHEN** boss shuffle is off in a souls-tier seed
- **THEN** each dungeon's boss/prize locations require that dungeon's vanilla boss's soul

### Requirement: Kill-gated room and enemy-held-key soul requirements
Under the `bosses+enemies` tier, logic SHALL require the resident species' souls for everything a kill-gated room gates, derived from game data by a committed generator (`gen_soul_room_tables.py`): room-header kill tags classify each room (kill→doors vs kill→chest, verified against the `kDungTagroutines` dispatch), room sprite lists give the resident soul sets, and a flood over the committed vanilla door graph (`door_tables.gen.c`) yields the regions/locations reachable only through each room's shutter doors. The generated wraps SHALL cover fork chest/boss/prize locations (by id), the Agahnim 1 event, and generated pot/enemy-check/enemy-drop locations (by door-region/room); rooms whose residents carry no enemy soul are waived (soul-less species always spawn and enemy shuffle substitutes only souled species), and boss-soul rooms (GT refights) are covered by the `CanKill<Boss>` macro terms instead. Generation SHALL fail closed — `BuildItemPool` refuses a `bosses+enemies` seed — when the generated table was absent at codegen (`kRandoSoulRoomsBaked == 0`).

#### Scenario: Kill-gated traversal requires resident souls
- **WHEN** a progression path routes through a kill-to-open-door room under the `bosses+enemies` tier
- **THEN** logic requires the souls of that room's resident species before considering onward locations reachable (e.g. every Ice Palace location requires the Bari and Pengator souls; Agahnim 1 requires the Soldier, Ball-and-chain Trooper, and Keese souls)

#### Scenario: Kill-revealed chest requires resident souls
- **WHEN** a chest is revealed by a kill-clear room tag under the `bosses+enemies` tier
- **THEN** that chest's predicate requires the room's resident souls

#### Scenario: Non-itemized enemy-held keys stay collectable (runtime exemption)
- **WHEN** `enemy_drop_checks` does not itemize forced key drops (effective off) under the `bosses+enemies` tier
- **THEN** the forced-drop holder spawns regardless of soul ownership (static-hook exemption keyed on the vanilla drop-source slot), so the vanilla free-key counting model stays true and key-availability logic needs no soul terms for those drops

#### Scenario: Itemized enemy-held key requires the holder's soul
- **WHEN** `enemy_drop_checks` itemizes a forced key drop as a location under the `bosses+enemies` tier
- **THEN** that location's predicate requires the vanilla holder's soul and normal suppression applies to the holder

#### Scenario: Missing generated table fails generation
- **WHEN** the room → soul-set table is absent and a `bosses+enemies` seed is requested
- **THEN** seed generation fails with a diagnostic instead of producing a seed with silently weakened logic

### Requirement: Souls degrade to off under door shuffle
`Settings_EffectiveSoulsShuffle` SHALL degrade `souls_shuffle` (any tier) to `off` when door shuffle is active: the door-shuffle oracle is species-blind, the enemies tier's generated kill-room requirements are computed against the vanilla door graph, and even the bosses tier's soul-gated boss/prize predicates make the door-layout fill exhaust its attempt budget per layout candidate. The pool, the logic VM, the placer gate, the runtime suppression, and the canonical settings hash SHALL all read the effective value.

#### Scenario: Door shuffle disables souls everywhere
- **WHEN** a seed requests any `souls_shuffle` tier with `door_shuffle=basic`
- **THEN** no souls enter the pool, no suppression binds at runtime, souls-conditional logic evaluates as off, and the canonical settings hash reflects the degraded (off) value

### Requirement: Enemy-check locations require the source species' soul
Under the `bosses+enemies` tier, every enemy-check location (`LOCTYPE_Enemy`) and forced enemy-drop location (`LOCTYPE_EnemyDrop`) SHALL additionally require the soul of its source species, emitted by the enemy-check table generator (which already carries `source_type` per check) into the location's predicate; the 13 boss/miniboss check locations SHALL instead be covered by the boss-soul resolution and the GT-refight gates, and the virtual `HyruleCastleBigKey` derivation SHALL inherit its underlying forced-drop check's soul requirement.

#### Scenario: Ordinary enemy check gated on its species
- **WHEN** a `bosses+enemies` seed places an item on an enemy-check location whose source species' soul is un-owned
- **THEN** logic treats that location as unreachable until the soul is in the player's sphere, and the placer orders placements accordingly

#### Scenario: Off and bosses tiers leave enemy-check predicates unchanged
- **WHEN** a seed uses `souls_shuffle=off` or `souls_shuffle=bosses` with any `enemy_drop_checks` tier
- **THEN** ordinary enemy-check predicates evaluate exactly as without the souls feature (boss/miniboss checks gate through boss souls at the `bosses` tier)

#### Scenario: Standard escape big-key guard
- **WHEN** a Standard-mode `bosses+enemies` seed gates the Ball-and-chain guard's big-key drop on that guard's soul
- **THEN** the placer places that soul reachable within the pre-rescue sphere (or refuses the seed), never producing a seed where the escape cannot be completed

### Requirement: Boss-species kill gates bind at the bosses tier
The Ganon's Tower refight rooms (Armos Knights, Lanmolas, Moldorm) SHALL carry static soul requirements for those three bosses under any active souls tier, since boss species suppress at the `bosses` tier and the refight rooms are outside the boss-shuffle room set.

#### Scenario: GT climb requires refight souls
- **WHEN** a `souls_shuffle=bosses` seed's logic evaluates the GT climb past a refight room
- **THEN** progression beyond each refight requires that boss's soul

### Requirement: Goal predicates require Agahnim and Ganon souls
When a souls tier is active, goal-completion and world-progression predicates that require killing Agahnim or Ganon SHALL additionally require the Agahnim Soul or Ganon Soul respectively.

#### Scenario: Ganon goal gated on his soul
- **WHEN** a souls-tier seed's goal requires defeating Ganon
- **THEN** the goal-completion predicate requires the Ganon Soul, and the placer places it reachable before it is required

#### Scenario: Agahnim-dependent dark-world access gated
- **WHEN** logic evaluates a route to the Dark World that goes through defeating Agahnim 1
- **THEN** that route requires the Agahnim Soul (alternative non-Agahnim routes are unaffected)
