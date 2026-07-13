# randomizer-enemy-drop-sanity Specification

## Purpose
TBD - created by archiving change add-rando-enemy-drop-sanity. Update Purpose after archive.
## Requirements
### Requirement: Enemy-drop check tiered scope

The randomizer SHALL provide an `enemy_drop_checks` axis with values `Off` (0),
`Keys` (1), `Dungeon` (2), and `All` (3). `Keys` covers vanilla dungeon enemy
forced small-key drops plus the reviewed Hyrule Castle one-shot forced big-key check.
`Dungeon` adds generated ordinary dungeon enemies. `All` adds the compatible static
overworld rows, reviewed all-tier underworld exceptions, reviewed GT-miniboss events,
and reviewed repeatable finite scripted children.

The existing `drop_shuffle` axis SHALL remain a deterministic prize-pack permutation;
enemy-drop checks remain a separate location-expansion axis. Derived settings SHALL
apply consistently to placement, logic, runtime, spoiler, and trackers: vanilla keys
normalize every enemy-check tier to `Off`; door shuffle preserves supported `Keys`,
`Dungeon`, and `All` through generated bridge rows; enemy shuffle normalizes
`Dungeon`/`All` to `Keys` (or `Off` when keys are unsupported); entrance shuffle
normalizes `All` to `Dungeon`; boss shuffle and pot shuffle preserve `All`, with
thrown-pot routes disabled while pot sanity is active.

#### Scenario: Off preserves current enemy drops
- **WHEN** `enemy_drop_checks = Off`
- **THEN** no enemy-drop location is active, forced key drops remain vanilla/free
  drops, and `drop_shuffle` continues to affect only non-forced prize-pack drops

#### Scenario: Vanilla keys normalize enemy checks off
- **WHEN** any non-off enemy-drop tier is requested with effective vanilla small keys
- **THEN** the effective enemy-drop tier is `Off`

#### Scenario: Door shuffle preserves supported tiers
- **WHEN** door shuffle is active with a requested `Keys`, `Dungeon`, or `All` tier
- **THEN** the requested tier remains effective through the generated enemy-check
  bridge and digest/replay contract

#### Scenario: Enemy shuffle keeps only the forced-key tier
- **WHEN** enemy shuffle is active with requested `Dungeon` or `All`
- **THEN** the effective tier is `Keys`, or `Off` when forced-key checks are unsupported

#### Scenario: Entrance shuffle excludes overworld all-tier rows
- **WHEN** entrance shuffle is active with requested `All`
- **THEN** the effective tier is `Dungeon` unless another rule lowers it further

#### Scenario: All activates every compatible reviewed domain
- **WHEN** `enemy_drop_checks = All` remains effective
- **THEN** keys, dungeon enemies, static overworld enemies, reviewed underworld
  exceptions, reviewed GT minibosses, and reviewed repeatable scripted children are
  active

### Requirement: Generated forced-key enemy-drop registry

Enemy-drop check locations SHALL be generated from vanilla dungeon sprite assets,
not hand-authored. The generator SHALL scan forced-key marker entries and emit active
Phase 1 rows for small-key markers (`type == 0xe4` with `y == 0xfe`) whose previous
sprite-list entry is a stable real enemy source. The single reviewed big-key marker
(`type == 0xe4` with `y == 0xfd` in room `0x080`) SHALL also enter the active
registry as a pure one-shot check with `vanilla_item = Nothing` and policy
`one_shot_preserve_vanilla_big_key`.

Each generated row SHALL carry a stable location id, name, room, source runtime slot,
vanilla source sprite type, source coordinates, region, vanilla key item, runtime
lookup key, and reviewed door identity metadata (`door_dungeon`, `door_region`).
Small-key rows SHALL also carry exact key-depth dump metadata (`door_drop_index`,
`key_depth`, `key_mindepth`, and the human DROP row name).
Identity SHALL be the vanilla source tuple `(domain=dungeon, room, source_slot,
vanilla_type)`, not the enemy type after `enemy_shuffle`.

The generator SHALL fail on marker-at-first-entry, consecutive markers, missing or
non-real previous entry, control/overlord source, non-killable or key-banned source,
room mismatch, duplicate source identity, duplicate active small-key room, unsupported
Tower of Hera cage room, any marker whose runtime carrier cannot be proven to be
the previous source entry, or any small-key row that does not join exactly to a
key-depth DROP row. Door tables and key-depth dumps are review/depth oracles; they
are not the identity source.

#### Scenario: Small-key marker maps to a stable enemy source
- **WHEN** the generator finds a forced small-key marker after a valid killable enemy
  source entry
- **THEN** it emits one `EnemyDrop` location bound to that source tuple and records
  the reviewed door identity metadata

#### Scenario: Big-key marker becomes one-shot check
- **WHEN** the generator finds the reviewed Hyrule Castle forced big-key marker
  (`y == 0xfd`)
- **THEN** it emits one active `EnemyDrop` location with one-shot big-key policy and
  no modeled big-key item in the placement pool

### Requirement: Lone forced big-key marker is a pure one-shot check

The single vanilla forced big-key enemy marker SHALL become active under
`enemy_drop_checks = Keys` as a pure one-shot check. The location SHALL be fillable
with a regular placed item, while the runtime separately grants the current dungeon's
vanilla big key exactly once. The one-shot row SHALL NOT add a modeled castle
big-key item to the item registry, item pool, or dungeon-key placement accounting.

#### Scenario: Big-key one-shot preserves vanilla key
- **WHEN** the active forced big-key source is unchecked and the player collects it
- **THEN** the placed item is dispatched through the randomizer location and the
  vanilla current-dungeon big-key bit is granted silently

#### Scenario: Bad marker fails closed
- **WHEN** a forced small-key marker has no valid previous real enemy source or no
  reviewed room binding
- **THEN** codegen fails with a diagnostic instead of emitting a partial key-check
  registry

### Requirement: Runtime forced-key check dispatch

Active enemy-drop checks SHALL recover their location id at forced-key pickup time
from the generated `(room, source_slot, drop_kind)` lookup. The absorbable key sprite
SHALL keep the carrier source slot in snapshot-persisted vanilla sprite state, so no
separate C-side pending location table is required in Phase 1. The generated registry
SHALL be the only source of location identity.

The pickup/absorption hook SHALL guard checked locations before dispatch because
`Rando_DispatchVanillaGrant` is not idempotent. For an unchecked active source, it
SHALL resolve the placed item with a forced dispatch sentinel
(`vanilla_registry_id = 0xFFFF`). Dispatch marks the checked-location bit as the
existing grant system's irreversible pickup intent, then delivers the placed item via
direct grant, confirmation, or receive animation as appropriate.

After a successful check, future room loads SHALL consult `Rando_IsLocationChecked`
and suppress that source's forced-key behavior. The vanilla case-12 small-key
increment SHALL NOT run for active enemy-drop checks, including identity placements,
so a checked enemy-drop location cannot double grant or increment the current dungeon
key outside placement semantics.

#### Scenario: Pickup grants a placed item exactly once
- **WHEN** an active forced small-key enemy-drop location is unchecked and the player
  picks up its spawned key/drop
- **THEN** the dispatch grants the placed item, marks the location checked through the
  existing grant system, and suppresses the vanilla small-key increment

#### Scenario: Leaving before pickup does not strand the check
- **WHEN** the player kills the carrier but leaves the room before picking up the
  forced-key drop
- **THEN** the location remains unchecked and the source/drop is reattemptable on
  re-entry or save/reload

#### Scenario: Checked source cannot duplicate the key
- **WHEN** a checked enemy-drop source is loaded again
- **THEN** the runtime suppresses its forced-key behavior and does not respawn a
  vanilla forced key or rerun the placed check
- **AND** the carrier itself may respawn under vanilla rules, with later kills using
  ordinary vanilla/drop-shuffled prize behavior

### Requirement: Enemy-drop check visual marker

The runtime SHALL provide a visible marker for active, unchecked enemy-drop checks.
Phase 1 MAY mark the spawned forced-key drop rather than the live carrier; a live
carrier marker requires separate OAM-budget and playtest work. The marker SHALL be
configurable through a client-local `[Graphics] EnemyDropMarker` preference with
at least `item` (placed item when the shared tile slot can represent all active
markers in the same class unambiguously, default; otherwise the neutral gold
check glint) and `generic` (non-placement gold check glint for live carriers)
modes. Spawned forced-key and big-key drops
SHALL try to render the real placed item and suppress other enemy-drop markers
while visible so the shared tile slot is not overwritten; when the real item
cannot be rendered safely, the spawned drop SHALL use the neutral gold check
glint instead of an item stand-in. The marker SHALL be
gated by the effective setting, generated location identity, and
`Rando_IsLocationChecked`. Checked, inactive, vanilla, and excluded forced drops
SHALL draw no marker.

#### Scenario: Unchecked forced-key check is marked
- **WHEN** an active enemy-drop check has not been collected
- **THEN** the player can distinguish the forced-key check/drop from a vanilla forced
  key through the configured marker

#### Scenario: Generic marker does not reveal placement
- **WHEN** an active unchecked enemy-drop check holds a non-key placed item and
  `EnemyDropMarker=generic`
- **THEN** the marker uses the neutral gold check glint instead of the placed item icon

#### Scenario: Spawned drop shows placed item
- **WHEN** an enemy-drop carrier dies and its unchecked forced drop is active
- **THEN** the spawned drop renders the placed item when the shared item slot can
  represent active spawned drops unambiguously
- **AND** remaining live carrier markers do not overwrite that spawned-drop icon

#### Scenario: Marker clears after check
- **WHEN** the location is checked and the room is still active or later reloaded
- **THEN** the marker no longer renders for that source

### Requirement: Dungeon enemy tier is owned by the follow-up registry

The `Dungeon` value SHALL be valid only through the generated ordinary enemy-check
registry supplied by `add-rando-dungeon-enemy-checks`. Builds without that
registry SHALL fail closed for active `Dungeon` seeds.

#### Scenario: Dungeon tier requires generated ordinary enemy data
- **WHEN** a settings source requests `Dungeon` and the generated ordinary enemy-check
  registry is unavailable
- **THEN** placement rejects the seed instead of generating with missing dungeon-enemy
  locations

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
- **THEN** the enemy follows vanilla room-history and respawn behavior in its
  original source slot
- **AND** it has no enemy-check marker, cannot grant the randomized location again,
  and produces its ordinary vanilla/drop-shuffled prize on later kills

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

### Requirement: Enemy marker defaults to non-spoiler

When `[Graphics] EnemyDropMarker` is absent, the client SHALL default to `generic`
and SHALL mark live active enemy checks without revealing their placed items. The
explicit `item` value SHALL remain supported for players who opt into placed-item
markers. This preference SHALL remain client-local and SHALL NOT alter seed data.

#### Scenario: Fresh configuration hides enemy-check contents
- **WHEN** the client loads a configuration with no `EnemyDropMarker` key
- **THEN** live active enemy checks use the neutral gold glint
- **AND** selecting `EnemyDropMarker=item` still enables exact placed-item markers

### Requirement: Enemy item-marker mode supports multiple simultaneous icons

`[Graphics] EnemyDropMarker=item` SHALL render real placed-item icons for active
enemy markers when a bounded marker-icon pool can represent those icons safely.
Enemy markers SHALL NOT rely on unowned OBJ cells or on a single shared receive-item
tile slot in a way that lets the last loaded icon overwrite other markers.

Each marker icon slot SHALL reserve the complete OBJ tile footprint required by the
icon, including multi-entry 16x16 or custom-art layouts. The implementation SHALL
use the marker-owned objTileAdr2 scratch range `0xF0..0xFF` as four fixed slots:
`F0-F3`, `F4-F7`, `F8-FB`, and `FC-FF`. Each slot SHALL be laid out as top-left,
top-right, bottom-left, bottom-right 8x8 tiles.

Exact markers SHALL draw only explicit small OAM entries. They SHALL NOT use large
OAM, SHALL NOT address `base + 0x10`, and SHALL NOT use the shared receive-item
slot `0x24/0x34`.

The renderer SHALL treat the neutral gold glint as the fallback for any marker whose
exact placed item cannot be rendered safely due to tile-slot capacity, palette
availability, missing art, receipt/direct-grant conflict, field-item conflict, or
OAM pressure. In item mode, the runtime SHALL show either the exact placed item or
the neutral glint; it SHALL NOT show a key, rupee, or other stand-in for a different
placed item.

#### Scenario: Distinct unchecked carriers get distinct icons
- **WHEN** two active unchecked live enemy checks in the same room hold different
  placed items and both icon keys fit in a marker-owned marker-icon pool
- **THEN** each carrier renders its own placed item icon
- **AND** neither carrier renders the other carrier's icon

#### Scenario: Marker scratch slot does not wrap
- **WHEN** an exact marker uses the fourth marker slot
- **THEN** its OAM charnums stay within `0xFC..0xFF`
- **AND** no marker tile references `0x24`, `0x34`, or a wrapped `base + 0x10`

#### Scenario: Identical icons share one marker slot
- **WHEN** multiple active enemy markers resolve to the same icon key
- **THEN** the renderer MAY load that icon once and draw all matching markers from
  the same marker tile slot

#### Scenario: Marker capacity is exhausted
- **WHEN** more distinct active enemy marker icons are visible than the marker-icon
  pool can safely represent
- **THEN** lower-priority markers fall back to the neutral gold glint or suppress
  cleanly according to the documented fallback policy
- **AND** no marker draws corrupt or stale item graphics

#### Scenario: Dense sorted room preserves a marker per check
- **WHEN** exact enemy icons and active pot/enemy checks compete for sorted-sprite
  OAM in a dense room
- **THEN** the overlay reserves one glint entry per active check before allocating
  multi-entry exact icons
- **AND** fallback glints MAY use verified-free ancilla-region entries after the
  normal floor region fills
- **AND** the overlay SHALL NOT overwrite OAM already allocated to an ancilla

#### Scenario: Multi-entry icon slot is incomplete
- **WHEN** an active enemy marker's placed item requires a multi-entry OBJ footprint
  and the marker pool cannot reserve every required tile cell
- **THEN** that marker draws the neutral gold glint instead of a partial item icon

#### Scenario: Exact item cannot be represented
- **WHEN** an active enemy marker's placed item requires missing art, an unavailable
  palette row, or an unsafe custom draw policy
- **THEN** that marker draws the neutral gold glint instead of a substitute item

### Requirement: Spawned enemy-drop pickups have item-marker priority

Spawned forced-drop pickups SHALL have higher marker priority than live enemy
carriers. Once a carrier has been killed and its active forced drop is visible, the
spawned pickup SHALL render the real placed item when the marker-icon pool can
represent it safely. Other live enemy markers MAY still render their own real item
icons if resources remain; otherwise they SHALL fall back to the neutral gold glint
or suppress cleanly.

#### Scenario: Spawned pickup and live carrier coexist
- **WHEN** an active forced enemy-drop pickup is visible and another unchecked live
  enemy marker remains in the room
- **THEN** the spawned pickup receives allocation priority for its real placed item
- **AND** the live carrier renders its own real placed item only if resources remain

#### Scenario: Spawned pickup cannot render exact item
- **WHEN** the spawned forced-drop pickup's real placed item cannot be rendered
  safely
- **THEN** the spawned pickup draws the neutral gold glint instead of a substitute
  item

### Requirement: Generic marker mode remains non-spoiler

`[Graphics] EnemyDropMarker=generic` SHALL continue to draw the neutral gold glint
for live active enemy carriers without resolving or revealing their placed item
icons. This mode SHALL NOT allocate item-icon marker slots for live carriers.

#### Scenario: Generic live marker hides placement
- **WHEN** a live active enemy carrier holds a non-key placed item and
  `EnemyDropMarker=generic`
- **THEN** the live carrier draws the neutral gold glint and does not draw the
  placed item icon

#### Scenario: Stunned live carrier remains marked
- **WHEN** an unchecked live enemy carrier is stunned or frozen into sprite state
  11 by boomerang, hookshot, or ice interactions
- **THEN** the carrier remains eligible for its configured enemy marker until it is
  killed, checked, picked up, or otherwise leaves the live-carrier state

### Requirement: Enemy marker multi-icon rendering is visual-only

The multi-icon marker renderer SHALL be client-local presentation only. It SHALL NOT
change placement, logic, share strings, canonical settings, `settings_hash`,
`kGeneratorVersion`, checked-location persistence, sidecar format, snapshot format,
spoiler output, tracker reachability, or auto-tracker messages.

#### Scenario: Same seed with different local marker resources
- **WHEN** two clients load the same share string but one can allocate multiple enemy
  item marker icons and the other falls back to glints
- **THEN** their item placement, checked state, grants, spoiler digests, and save
  data remain identical

### Requirement: Enemy marker rendering composes with other visual systems

Enemy marker item icons SHALL coexist safely with pot-sanity glints, field-item
sprites, item receipts, direct-grant confirmations, enemy sprites, spawned pickups,
HUD/tracker overlays, and text boxes. If a required graphics, palette, DMA, or OAM
resource is already owned by a higher-priority visual path, enemy item markers SHALL
fall back to the neutral gold glint or suppress cleanly for that frame.

Marker palette writes SHALL be frame-scoped or confined to a proven marker-owned
palette row. A row shared with Link, enemies, pot glints, receipts, or field items
SHALL be restored before reuse. Multiple custom marker icons that need incompatible
palette contents SHALL NOT alternate or recolor each other; lower-priority markers
SHALL fall back to the neutral gold glint or suppress cleanly.

Before writing OAM for an item marker or glint fallback, the renderer SHALL reserve
the full OAM footprint in the correct sorted region. Multi-entry icons SHALL draw all
required OAM entries or none.

The legacy OAM tracker SHALL have priority over the `0xF0..0xFF` scratch range. If
the legacy OAM tracker will draw this frame, exact enemy item markers SHALL have
zero marker-icon capacity and SHALL fall back to glint or suppress cleanly. Native PC
tracker windows SHALL NOT consume this scratch range.

#### Scenario: Pot glints remain visible
- **WHEN** a room contains active pot-sanity glints and active enemy item markers
- **THEN** drawing enemy markers does not hide, recolor, or corrupt the pot glints

#### Scenario: Receipt animation keeps priority
- **WHEN** an item receipt, direct-grant confirmation, or equivalent placed-item
  presentation is active
- **THEN** enemy item markers do not overwrite its graphics, receive-slot staging
  buffers, receive-slot owner cache, or palette state

#### Scenario: Legacy OAM tracker keeps scratch priority
- **WHEN** the legacy OAM tracker will draw this frame
- **THEN** exact enemy marker icons are not uploaded into `0xF0..0xFF`
- **AND** enemy markers fall back to the neutral gold glint or suppress cleanly

#### Scenario: OAM pressure is handled safely
- **WHEN** the room lacks enough OAM capacity for all requested enemy markers
- **THEN** lower-priority enemy markers fall back or suppress cleanly
- **AND** no partial, stale, or offscreen garbage marker is drawn

#### Scenario: Custom marker palettes conflict
- **WHEN** two active custom-art enemy markers require incompatible contents in the
  same non-marker-owned palette row
- **THEN** at most one uses that palette row and the other draws the neutral glint or
  suppresses cleanly

### Requirement: Dense-room marker conflicts are explicitly tested

The implementation SHALL include targeted verification for rooms with multiple
active enemy checks whose placed items resolve to different icons. A dense scripted
room such as Eastern Palace room `0x0A8`, where four dynamic Red Stalfos can carry
different placed items, SHALL be covered by F12/OAM/VRAM inspection or an equivalent
automated visual dump.

#### Scenario: Dense scripted room does not show last-icon-wins corruption
- **WHEN** a dense scripted room has multiple active enemy markers with different
  placed items and `EnemyDropMarker=item`
- **THEN** each allocated marker draws its own placed item icon or the neutral glint
- **AND** no marker draws a stale icon loaded for another enemy check

### Requirement: All tier covers every supported stable enemy source domain

`enemy_drop_checks=all` SHALL include the `keys` and `dungeon` tiers plus every
compatible generated static overworld ordinary enemy source in the shipped
all-tier registry. It MAY also include audited underworld cave/interior ordinary
enemy sources when their room access can be modeled directly without dungeon
key-depth metadata. It SHALL include reviewed GT-miniboss event checks and
repeatable finite authored scripted-spawn enemy checks when their runtime identity,
reachability, death dispatch, persistence, and reward coexistence are modeled.
The shipped registry scope is dungeon ordinary enemies, static authored overworld
ordinary enemies, reviewed underworld exceptions, reviewed GT-miniboss events,
and reviewed repeatable finite scripted-spawn enemies with stable source identity,
reachability, death dispatch, and checked-state grant/marker suppression.
Killable source classes that are banned from carrying dungeon keys or are flying
MAY be excluded from the normal `dungeon` tier, but SHALL be emitted as
all-tier-only checks when their static source identity and room/area reachability
are modeled for `enemy_drop_checks=all`.

The all-enemy audit SHALL classify every scanned source in the supported static
dungeon/overworld domains, every reviewed underworld exception candidate, every
reviewed GT-miniboss event, and every reviewed repeatable finite scripted-spawn candidate
as included or excluded with a stable reason.
Non-killable actors such as thieves and NPC-like sprites, non-enemy hazards,
projectiles, decorative sprites, and unbounded farmable dynamic spawns SHALL NOT
be emitted as checks unless a future change converts them into finite one-shot
sources with stable identity and persistence.

Unbounded/farmable spawns and shuffled enemy substitutions remain unsupported
domains for this change. They SHALL NOT be silently counted as covered by the
shipped `all` tier; affected setting combinations either normalize visibly to a
lower supported tier or remain documented as future scope until source identity,
death dispatch, persistence, and logic are modeled.

#### Scenario: Killable overworld source is emitted
- **WHEN** the all-enemy audit finds a finite overworld enemy source with stable
  identity, reachability, kill logic, and duplicate-grant suppression
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

#### Scenario: Killable key-banned dungeon source is all-only
- **WHEN** the all-enemy audit finds a killable underworld source that is
  excluded from the `dungeon` tier only because it cannot safely carry keys or is
  a flying class
- **AND** the room already has modeled reachability and kill logic
- **THEN** it emits one all-tier-only `Enemy` location for that source under
  `enemy_drop_checks=all`
- **AND** the row remains inactive under `enemy_drop_checks=dungeon`

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

#### Scenario: Reviewed GT miniboss event is emitted
- **WHEN** the all-enemy audit finds a reviewed Ganon's Tower miniboss event with
  stable physical-room identity and modeled kill logic
- **THEN** it emits one all-tier `Enemy` location for that event
- **AND** the runtime grants it exactly once without suppressing existing scripted
  progression behavior

#### Scenario: Repeatable finite scripted-spawn source is emitted
- **WHEN** the all-enemy audit finds a reviewed repeatable finite scripted-spawn child with
  stable parent room/source-slot/type identity and child ordinal
- **AND** its child kill route is modeled through inventory or counted throwable
  objects
- **THEN** it emits one all-tier `Enemy` location for that child
- **AND** already-checked children still spawn without a marker or randomized grant,
  while later child identities from the same authored parent remain stable

#### Scenario: Unsupported source domain is not silently covered
- **WHEN** a farmable, unbounded, projectile, or enemy-shuffle substituted source
  exists but runtime identity or logic is not supported for its domain
- **THEN** the shipped static all-tier registry does not claim that source as
  covered, and affected settings visibly downgrade or remain future scope

### Requirement: All-enemy source identity is stable across domains

Every emitted all-enemy location SHALL be keyed by an authored source identity,
not by enemy type alone. Dungeon rows and reviewed underworld exceptions MAY use
the existing room/source-slot identity; underworld exceptions SHALL carry an
all-tier activation bit in the runtime lookup. Overworld rows SHALL carry an
equivalent stable source tuple. GT-miniboss rows SHALL carry stable event
dungeon/room identity. Repeatable finite scripted-spawn rows SHALL carry stable parent
room/source-slot/type identity plus child index and child type.

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
that source. A successfully granted ordinary enemy check SHALL replace that kill's
normal prize-pack pickup. Special forced/carried drops and boss/event rewards SHALL
remain independent.

Checked sources SHALL stay checked across room/area reload, save/reload, snapshot
restore, screen transition, mirror transition, and world transition. Checked state
SHALL suppress only the randomized grant and marker. Ordinary actors SHALL keep their
authored source identity and follow vanilla spawn, room-history, death, and prize-drop
behavior after collection.

Forced enemy-drop checks SHALL keep their existing pickup-time behavior from the
`keys` tier: after collection the carrier may respawn normally, but its one-time
forced-key behavior SHALL remain suppressed.

#### Scenario: Overworld enemy dies
- **WHEN** an active all-tier overworld enemy check dies
- **THEN** the runtime dispatches the placed item, marks the location checked, and
  prevents that source from granting again after reload or transition
- **AND** the kill does not also produce a normal prize-pack pickup

#### Scenario: Dungeon boss retains its existing reward sequence
- **WHEN** a dungeon boss dies with all-enemy checks active
- **THEN** no separate enemy-check item is dispatched
- **AND** the existing heart-container and dungeon-prize sequence remains intact

#### Scenario: GT miniboss grants an enemy check
- **WHEN** an emitted Ganon's Tower miniboss enemy check dies
- **THEN** the all-enemy check grants exactly once
- **AND** existing scripted-progression behavior remains intact

#### Scenario: GT miniboss runtime room differs from its logical room
- **WHEN** a GT miniboss door region uses a logical room id that differs from the
  live `dungeon_room_index`
- **THEN** its generated event lookup uses the physical runtime room while retaining
  the logical door region for reachability
- **AND** generation fails if the reviewed runtime/logical room contract drifts

#### Scenario: Save reload after checked enemy
- **WHEN** the player checks an all-tier enemy source, saves, and reloads
- **THEN** the checked source follows vanilla respawn behavior without a check marker
  or another randomized grant
- **AND** later sources keep stable identity

### Requirement: All-enemy visuals use existing marker policy

All-enemy checks SHALL use the existing enemy marker preference where domain
metadata and OAM pressure make markers safe. Dungeon all-tier rows use the
existing marker path. Static overworld all-tier rows MAY draw exact placed-item
markers in item mode; overworld markers that cannot draw an exact item SHALL draw
the neutral gold glint when a safe post-sprite overlay row and OAM slot are
available. Marker code SHALL suppress cleanly rather than draw a stand-in item for
a different placed item.

For every emitted all-tier source that can have an in-world marker, generated marker
data SHALL define domain-specific stable authored identity, screen-coordinate
derivation, scroll/camera basis, sorted-OAM region, and checked-marker suppression
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
