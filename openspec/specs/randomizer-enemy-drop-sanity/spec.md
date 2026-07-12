# randomizer-enemy-drop-sanity Specification

## Purpose
TBD - created by archiving change add-rando-enemy-drop-sanity. Update Purpose after archive.
## Requirements
### Requirement: Enemy-drop check tiered scope

The randomizer SHALL provide an `enemy_drop_checks` axis with values `Off` (0),
`Keys` (1), and `Dungeon` (2). The `Keys` tier SHALL cover vanilla dungeon enemy
forced small-key drops plus the single Hyrule Castle one-shot forced big-key check
when the effective small-key mode is Wild/Retro or Dungeon. Retro computes to Wild
for this purpose; door shuffle forces effective Dungeon keys and SHALL compose
through the generated door x enemy-drop bridge. The `Dungeon` tier is supplied by the
follow-up dungeon-enemy change and includes ordinary generated dungeon enemy checks
when its additional gates pass.

The existing `drop_shuffle` axis SHALL retain its current meaning: a deterministic
permutation of the 56-entry prize-pack table. Enemy-drop checks are a separate
location-expansion axis, not a reinterpretation of prize-pack shuffle.

#### Scenario: Off preserves current enemy drops
- **WHEN** `enemy_drop_checks = Off`
- **THEN** no enemy-drop location is active, forced key drops remain vanilla/free
  drops, and `drop_shuffle` continues to affect only non-forced prize-pack drops

#### Scenario: Keys tier requires a supported effective small-key mode
- **WHEN** `enemy_drop_checks = Keys` but the effective small-key mode is vanilla
- **THEN** the effective enemy-drop tier is `Off`, no enemy-drop location is placed,
  and forced small-key drops remain vanilla free grants

#### Scenario: Door shuffle keeps key-tier enemy drops active
- **WHEN** `enemy_drop_checks = Keys` and door shuffle is active
- **THEN** the effective enemy-drop tier is `Keys` because door shuffle forces
  effective Dungeon small keys and installs the door x enemy-drop bridge

#### Scenario: Dungeon tier downgrades under door shuffle
- **WHEN** a settings source requests `Dungeon` while door shuffle is active
- **THEN** effective settings downgrade the tier to `Keys`, so forced enemy-key
  checks stay active and ordinary dungeon-enemy checks stay inactive

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
