# Dungeon Enemy Checks - design

## D1 - Eligibility and emitted registry

The dungeon-enemy tier is built from generated static source registries backed by
local ROM assets:

- audit source: `zelda3_assets.dat`;
- dungeon runtime identity: `(dungeon_room_index, sprite_N source slot)`;
- safety oracle: the curated `shuffle_enemies.c` `kEnemyTable` source-type table;
- forced-key exclusion: existing `EnemyDrop` key carriers are not duplicated as
  ordinary `Enemy` locations.

Initial eligibility is deliberately conservative. A source type must be
`ESF_RANDOMIZABLE` and `ESF_KILLABLE`, must not be an existing forced-key
enemy-drop check, and must have a reviewed room reachability predicate. The local
registry scans 464 eligible underworld sprite-table sources, emits the 270
dungeon candidates with conservative room predicates, records 158 key-depth-only
candidates as audit-only, and records 36 no-key-depth underworld sources as
outside the dungeon-only scope.

Overworld enemies remain audit-only. The audit may report otherwise eligible
overworld candidates, but runtime currently lacks stable original source identity
for overworld sprite spawns.

## D2 - Runtime collection model

Ordinary `Enemy` checks use death-time direct grant. When an active eligible
source dies, the runtime dispatches the placed item, marks the location checked,
and gives the quiet receive or confirmation feedback used by other direct-grant
checks. This avoids the loss mode where vanilla marks an enemy dead before a
spawned item is picked up and the player leaves, saves, or snapshots.

Checked ordinary enemies are suppressed on room reload by consuming their source
slot and marking the vanilla room death bit. Consuming the slot preserves
`sprite_N` identity for later sprites in the room, which keeps the lookup stable
after a subset of enemy checks has already been collected.

Forced-key `EnemyDrop` rows keep their existing pickup-time path. Ordinary rows
use the separate `Enemy` location type so `enemy_drop_checks=keys` cannot
accidentally activate every generated enemy row.

## D3 - Capacity and persistence gate

The dungeon-only emitted registry must fit the existing location capacity. This
phase does not raise `kRandoLocationCapacity`; instead, selfchecks verify that
ordinary enemy rows fit alongside pot-sanity and forced enemy-drop rows and that
the generated runtime lookup count matches the emitted `Enemy` locations.

Because capacity is unchanged, the existing checked-location bitmap, sidecar
payload, snapshot tail payload, placement table, spoiler, tracker, and
autotracker bounds remain compatible.

## D4 - Logic model

Dungeon-enemy locations require conservative reach predicates. Dungeon enemies use
reviewed room predicates plus key-depth terms. Underworld sources without
dungeon key-depth coverage are outside this dungeon-only tier rather than treated
as free checks.

Each ordinary enemy row also carries a source-type kill predicate. The inventory
branch uses the dungeon's generic combat macro unless a source type has a
reviewed narrower requirement. The thrown-pot branch is generated from engine
data: indoor thrown pots use the thrown-sprite damage preset, the enemy damage
matrix gives the HP damage for each source type, and `kSpriteInit_Health` gives
the number of pot hits required. That branch is emitted only when the room has at
least that many reachable liftable pots in `pots.gen.yaml`, so a two-pot kill
does not become logical with only one pot.

Door shuffle currently degrades requested `enemy_drop_checks=dungeon` to effective
`keys`. The generated door x enemy-drop bridge only models forced key sources,
not arbitrary ordinary enemies, so ordinary `Enemy` rows stay disabled under door
shuffle until a non-key door-region bridge is designed and validated.

Enemy shuffle also degrades requested `enemy_drop_checks=dungeon` to effective
`keys`. Source-slot identity remains stable enough for forced-key checks, but the
placement graph cannot currently see the per-seed substituted enemy type or
enemy-shuffle HP scaling for ordinary rows.

## D5 - UI and tracker model

The setting is a three-value selector:

- `off`
- `keys`
- `dungeon`

The effective value may be lower than the requested value after derived settings
are applied. Vanilla small-key mode becomes `off`; door shuffle turns requested
`dungeon` into effective `keys`; enemy shuffle also turns requested `dungeon`
into effective `keys`.

Enemy check markers reuse `[Graphics] EnemyDropMarker`. `item` draws the placed
item over unchecked carriers only when every active carrier marker in the room
resolves to the same icon; otherwise it falls back to the pot-style gold check
glint because the renderer has one shared receive-item tile slot and item mode
must not show a misleading item stand-in. `generic` draws that same glint on live
carriers without revealing the placement. Spawned forced enemy-drop pickups try
to render the real placed item and suppress live carrier markers while visible;
if the pickup cannot render the real item safely, it uses the glint instead of an
item stand-in. This marker preference is client-local and does not enter
canonical settings, the share string, generator version, or corpus.

Spoiler, tracker, reachability, and autotracker output use normal generated
dungeon location rows with the `Enemy` type. They do not expose a separate
runtime-only flat list.
