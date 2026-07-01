## ADDED Requirements

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
markers in the same class unambiguously, default) and `generic` (non-placement
key-style marker for live carriers) modes. Spawned forced-key and big-key drops
SHALL try to render the real placed item and suppress other enemy-drop markers
while visible so the shared tile slot is not overwritten. The marker SHALL be
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
- **THEN** the marker uses the generic key-style icon instead of the placed item icon

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
