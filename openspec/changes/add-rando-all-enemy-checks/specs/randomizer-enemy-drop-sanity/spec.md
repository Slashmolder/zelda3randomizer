## ADDED Requirements

### Requirement: All tier covers every finite killable enemy source

`enemy_drop_checks=all` SHALL include the `keys` and `dungeon` tiers plus every
compatible emitted finite, authored, killable enemy source across modeled dungeon,
overworld, boss/miniboss, and finite scripted-spawn domains.

The all-enemy audit SHALL classify every source as included or excluded with a stable
reason. Non-killable actors such as thieves and NPC-like sprites, non-enemy hazards,
projectiles, decorative sprites, and unbounded farmable dynamic spawns SHALL NOT be
emitted as checks unless they are converted into finite one-shot sources with stable
identity and persistence.

Finite authored killable sources that lack runtime identity, death dispatch,
persistence, or conservative logic SHALL NOT be silently excluded from an effective
`all` tier. They SHALL either block `all` from shipping for that domain or force a
visible effective downgrade/rejection for the affected setting combination.

#### Scenario: Killable overworld source is emitted
- **WHEN** the all-enemy audit finds a finite overworld enemy source with stable
  identity, reachability, kill logic, and checked-state suppression
- **THEN** it emits one `Enemy` location for that source under
  `enemy_drop_checks=all`

#### Scenario: Thief-like source is excluded
- **WHEN** the audit finds a non-killable thief or NPC-like actor
- **THEN** the audit records an excluded non-killable reason and emits no location
  for that source

#### Scenario: Unbounded spawn is excluded
- **WHEN** the audit finds a farmable or unbounded dynamic enemy spawn with no finite
  one-shot source identity
- **THEN** the audit records an excluded unbounded reason and emits no location
  for that spawn

#### Scenario: Unclassified killable source fails closed
- **WHEN** a finite killable enemy source is present in local assets but the audit
  neither emits it nor records an exclusion reason
- **THEN** codegen fails instead of producing an incomplete `all` registry

#### Scenario: Unsupported killable source blocks honest all
- **WHEN** a finite authored killable source exists but runtime identity or logic is
  not yet supported for its domain
- **THEN** effective `enemy_drop_checks=all` is not allowed for that domain; the
  request visibly downgrades or generation rejects with a diagnostic

### Requirement: All-enemy source identity is stable across domains

Every emitted all-enemy location SHALL be keyed by an authored source identity, not
by enemy type alone. Dungeon rows MAY use the existing room/source-slot identity.
Overworld rows SHALL carry an equivalent stable source tuple. Boss/miniboss and
finite scripted-spawn rows SHALL carry stable parent identity plus any required child
index.

Enemy shuffle SHALL NOT change the location identity. In the first all-enemy
implementation, requested `all` SHALL normalize to the highest lower tier allowed by
existing derived rules while enemy shuffle is active, normally `keys` but `off` when
the keys tier is unsupported, because ordinary all-enemy logic does not yet consume
substituted type, HP, damage, or killability. A later change MAY keep `all` active
with enemy shuffle only after it makes those substituted per-source values part of
placement, logic, digest, and corpus expectations.

#### Scenario: Overworld source survives enemy type substitution
- **WHEN** enemy shuffle substitutes the enemy type for an emitted overworld source
- **THEN** the location identity remains the authored overworld source tuple
- **AND** the all-enemy row is inactive unless a future enemy-shuffle-aware
  all-enemy placement model is active

#### Scenario: Duplicate source identity fails closed
- **WHEN** two emitted all-enemy rows resolve to the same source identity
- **THEN** codegen fails with a duplicate identity diagnostic

### Requirement: All-enemy checks grant and persist at death time

Ordinary all-enemy locations SHALL dispatch their placed item at enemy death time.
The runtime SHALL resolve the source identity, guard already-checked locations, grant
the placed item, mark the location checked, and suppress future duplicate grants for
that source.

Checked sources SHALL stay checked across room/area reload, save/reload, snapshot
restore, screen transition, mirror transition, and world transition. Suppressing a
checked source SHALL preserve later source identities in the same spawn list.

Forced enemy-drop checks SHALL keep their existing pickup-time behavior from the
`keys` tier.

#### Scenario: Overworld enemy dies
- **WHEN** an active all-tier overworld enemy check dies
- **THEN** the runtime dispatches the placed item, marks the location checked, and
  prevents that source from granting again after reload or transition

#### Scenario: Boss enemy check coexists with boss reward
- **WHEN** an emitted boss/miniboss enemy check dies
- **THEN** the all-enemy check grants exactly once
- **AND** existing boss prize, heart-container, dungeon-prize, or scripted-progression
  behavior remains intact

#### Scenario: Save reload after checked enemy
- **WHEN** the player checks an all-tier enemy source, saves, and reloads
- **THEN** the checked source does not grant again and later sources keep stable
  identity

### Requirement: All-enemy visuals use existing marker policy

All-enemy checks SHALL use the existing enemy marker preference. Generic mode SHALL
draw the neutral gold glint for unchecked live carriers. Item mode SHALL draw the
real placed item only when the marker renderer can show it safely; otherwise it SHALL
draw the neutral gold glint or suppress cleanly. Item mode SHALL NOT draw a stand-in
item for a different placed item.

For every emitted all-tier source that can have an in-world marker, generated marker
data SHALL define domain-specific stable authored identity, screen-coordinate
derivation, scroll/camera basis, sorted-OAM region, and checked-source suppression
behavior. A domain MAY suppress in-world markers only when marker metadata cannot be
made safe; tracker, spoiler, reachability, and checked-state output SHALL still
include every emitted location.

#### Scenario: Dense all-enemy screen
- **WHEN** many all-tier enemy checks are visible on one screen
- **THEN** every marker that renders is either the correct placed item or the neutral
  gold glint
- **AND** OAM pressure does not produce corrupt, stale, or partial item graphics

#### Scenario: Tracker still lists suppressed visual markers
- **WHEN** a marker is suppressed because the screen lacks OAM or graphics capacity
- **THEN** the emitted location remains present in spoiler, tracker, reachability,
  and checked-state data

#### Scenario: Domain lacks marker metadata
- **WHEN** an emitted all-tier source belongs to a domain whose in-world marker
  coordinates or OAM region cannot be proven safe
- **THEN** the in-world marker is suppressed for that source or domain
- **AND** spoiler, tracker, reachability, and checked-state output still include the
  location
