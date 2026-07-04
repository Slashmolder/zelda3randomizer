## ADDED Requirements

### Requirement: All tier covers generated static overworld enemy sources and reviewed underworld exceptions

`enemy_drop_checks=all` SHALL include the `keys` and `dungeon` tiers plus every
compatible generated static overworld ordinary enemy source in the shipped
all-tier registry. It MAY also include audited underworld cave/interior ordinary
enemy sources when their room access can be modeled directly without dungeon
key-depth metadata. The shipped registry scope is dungeon ordinary enemies, static
authored overworld ordinary enemies, and reviewed underworld exceptions with stable
source identity, reachability, death dispatch, and checked-state suppression.

The all-enemy audit SHALL classify every scanned source in the supported static
dungeon/overworld domains, plus every reviewed underworld exception candidate, as
included or excluded with a stable reason.
Non-killable actors such as thieves and NPC-like sprites, non-enemy hazards,
projectiles, decorative sprites, and unbounded farmable dynamic spawns SHALL NOT
be emitted as checks unless a future change converts them into finite one-shot
sources with stable identity and persistence.

Boss/miniboss sources, finite scripted-spawn groups, unbounded/farmable spawns,
and shuffled enemy substitutions are future source domains for this change. They
SHALL NOT be silently counted as covered by the shipped `all` tier; affected
setting combinations either normalize visibly to a lower supported tier or remain
documented as future scope until source identity, death dispatch, persistence, and
logic are modeled.

#### Scenario: Killable overworld source is emitted
- **WHEN** the all-enemy audit finds a finite overworld enemy source with stable
  identity, reachability, kill logic, and checked-state suppression
- **THEN** it emits one `Enemy` location for that source under
  `enemy_drop_checks=all`

#### Scenario: Reviewed underworld exception is emitted
- **WHEN** the all-enemy audit finds a finite underworld enemy source with stable
  room/source-slot identity and a reviewed direct access predicate
- **AND** its kill route is modeled through inventory or counted throwable objects
- **THEN** it emits one `Enemy` location for that source under
  `enemy_drop_checks=all`
- **AND** the emitted row is marked all-tier-only so it does not activate under
  `enemy_drop_checks=dungeon`

#### Scenario: Shared-room throwable source is not on the reviewed enemy side
- **WHEN** an audited underworld exception shares a physical room with pots or
  other throwables from a different entrance side
- **AND** the reviewed enemy access predicate does not reach those throwables
- **THEN** the enemy's kill route SHALL NOT count those throwables as reachable
- **AND** the emitted logic SHALL require an inventory kill route instead

#### Scenario: Thief-like source is excluded
- **WHEN** the audit finds a non-killable thief or NPC-like actor
- **THEN** the audit records an excluded non-killable reason and emits no location
  for that source

#### Scenario: Unbounded spawn is excluded
- **WHEN** the audit finds a farmable or unbounded dynamic enemy spawn with no finite
  one-shot source identity
- **THEN** the audit records an excluded unbounded reason and emits no location
  for that spawn

#### Scenario: Unclassified static source fails closed
- **WHEN** a scanned static dungeon or overworld source is present in local assets
  but the audit neither emits it nor records an exclusion reason
- **THEN** codegen fails instead of producing an incomplete shipped all-tier
  registry

#### Scenario: Unsupported source domain is not silently covered
- **WHEN** a boss/miniboss, finite scripted-spawn, farmable, or enemy-shuffle
  substituted source exists but runtime identity or logic is not yet supported for
  its domain
- **THEN** the shipped static all-tier registry does not claim that source as
  covered, and affected settings visibly downgrade or remain future scope

### Requirement: All-enemy source identity is stable across domains

Every emitted all-enemy location SHALL be keyed by an authored source identity, not
by enemy type alone. Dungeon rows and reviewed underworld exceptions MAY use the
existing room/source-slot identity; underworld exceptions SHALL carry an all-tier
activation bit in the runtime lookup. Overworld rows SHALL carry an equivalent
stable source tuple. Future boss/miniboss and finite scripted-spawn rows SHALL
carry stable parent identity plus any required child index before those domains can
join the effective all tier.

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

#### Scenario: Future boss enemy check coexists with boss reward
- **WHEN** a future change emits a boss/miniboss enemy check and that enemy dies
- **THEN** the all-enemy check grants exactly once
- **AND** existing boss prize, heart-container, dungeon-prize, or
  scripted-progression behavior remains intact

#### Scenario: Save reload after checked enemy
- **WHEN** the player checks an all-tier enemy source, saves, and reloads
- **THEN** the checked source does not grant again and later sources keep stable
  identity

### Requirement: All-enemy visuals use existing marker policy

All-enemy checks SHALL use the existing enemy marker preference where domain
metadata and OAM pressure make markers safe. Dungeon all-tier rows use the
existing marker path. Static overworld all-tier rows MAY draw exact placed-item
markers in item mode; generic overworld glints and exact-item fallback glints
remain future post-sprite-overlay work. Marker code SHALL suppress cleanly rather
than draw a stand-in item for a different placed item.

For every emitted all-tier source that can have an in-world marker, generated marker
data SHALL define domain-specific stable authored identity, screen-coordinate
derivation, scroll/camera basis, sorted-OAM region, and checked-source suppression
behavior. A domain MAY suppress in-world markers only when marker metadata cannot be
made safe; tracker, spoiler, reachability, and checked-state output SHALL still
include every emitted location.

#### Scenario: Dense all-enemy screen
- **WHEN** many all-tier enemy checks are visible on one screen
- **THEN** every marker that renders is either the correct placed item or a
  domain-supported neutral glint
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
