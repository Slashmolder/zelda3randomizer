# Enemy Drop Sanity - design

This design was revised through three fresh-eyes review rounds against the current
runtime, placement, and OpenSpec surfaces. The key outcome is a bounded first
implementation: forced enemy key drops become checks when effective small keys are
Wild/Retro or Dungeon, including door shuffle's forced Dungeon mode. Dungeon enemy
checks are implemented separately by the dungeon-only
`add-rando-dungeon-enemy-checks` follow-up; overworld ordinary enemies remain deferred.

## Context and grounded facts

- Current `drop_shuffle` shuffles the 56-entry prize-pack table only. Forced drops
  use `sprite_die_action` and bypass `DropShuffle_Lookup`.
- Dungeon forced-drop markers are encoded in the sprite list as `type == 0xe4`.
  `y == 0xfe` means small key; `y == 0xfd` means big key. The marker applies to the
  previous real sprite entry at room-load time.
- The local asset data has exactly one `y == 0xfd` marker: room `0x080`, carrier
  source type `0x6a` (Ball-n-chain / Morning Star guard), source slot 2. The current
  registry and logic model intentionally skip Hyrule Castle / Castle Tower big keys:
  `item_registry.yaml` has 11 big-key items and comments that HCE/HCT have no big key,
  while `gen_door_tables.py` explicitly passes over the `HC Big Key Drop`.
- `kDoorTblDropKeys` and the door key-depth dump describe key economy/prover rows,
  not stable runtime source identity. They are valuable cross-checks, but not the
  source of location ids.
- The forced-key death path currently sets `sprite_N = 255` before
  `PrepareEnemyDrop()`, which prevents the normal durable death flag until pickup
  restores the forced-key state. Phase 1 depends on preserving that "not collected
  until pickup" invariant.
- The existing `Rando_OnLocationCheck`/`Rando_DispatchVanillaGrant` path marks a
  location checked before the final receive/confirmation work. Phase 1 treats the
  pickup/absorption hook as the irreversible collection point, so it must guard
  checked locations before dispatch and suppress the vanilla key increment after
  dispatch.

## D1 - Setting and effective activation

Add `enemy_drop_checks` as a separate setting:

- `Off` (0): no enemy-drop locations are active.
- `Keys` (1): eligible forced enemy key drops are checks when the effective
  small-key mode is Wild/Retro or Dungeon (`Retro` maps to Wild and pools
  `GenericKey` for small-key rows; door shuffle forces Dungeon).
- `Dungeon` (2): includes `Keys` and the generated ordinary dungeon enemy checks
  supplied by the dungeon-enemy follow-up.

The implementation appends one canonical settings byte after the existing canonical
layout. It bumps `kSettingsCanonicalLen`, `kGeneratorVersion`, expected hashes, share
encode/decode length and CRC checks, sidecar slot settings replay, snapshot settings
TLVs, suppressed-spoiler fixed settings length, UI persistence, and corpus manifests.
Old canonical blobs decode as `Off`.

`apply_derived_rules` normalizes `enemy_drop_checks` to `Off` when effective small
keys are vanilla. Dungeon keys are supported through generated DROP-region
min-depth, combined free-drop accounting, and a door-oracle bridge when door
shuffle is active. A raw `Dungeon` request degrades to `Keys` under door shuffle until
ordinary enemy rows have their own door-region bridge, and under enemy shuffle
until placement can consume substituted enemy type and HP data. `enemy_shuffle`
still composes with the forced-key tier because location identity is keyed by
vanilla room/source slot rather than substituted enemy type. With inactive
settings, forced-key enemies remain free vanilla drops and do not become
identity-pinned locations.

## D2 - Generated registry and identity

The source of truth is a generated local registry, `assets/rando/enemy_drops.gen.yaml`,
derived from vanilla dungeon sprite assets. The generator scans forced-key markers and
emits active Phase 1 rows for `y == 0xfe` small-key markers plus the single reviewed
`y == 0xfd` Hyrule Castle big-key marker whose previous entry is a stable real enemy
source. It records at minimum:

- location id and human name;
- domain (`dungeon`), room, source sprite-list slot, vanilla sprite type;
- vanilla item (`SmallKey_<Dungeon>`, pooled as `GenericKey` under Retro, or
  `Nothing` for the one-shot big-key row);
- reviewed door identity (`door_dungeon`, `door_region`), region, and reach
  predicate;
- for small-key rows, exact key-depth dump metadata (`door_drop_index`,
  `key_depth`, and `key_mindepth`);
- visual/runtime lookup keys.

The generator hard-fails on marker-at-first-entry, consecutive markers, absent
source, control/overlord/non-real source, room mismatch, duplicate source identity,
duplicate active small-key room, unsupported special rooms, or any source whose
runtime marker-to-carrier behavior cannot be proven. Unsupported big-key markers fail
closed; the one supported big-key row carries `one_shot_preserve_vanilla_big_key`.

## D2b - Lone forced big-key one-shot

The single forced big-key marker is active as a pure one-shot check. The carrier can
hold a placed item, but the runtime also grants the vanilla current-dungeon big key
silently so the castle big-key requirement remains available exactly once. This avoids
adding a modeled Hyrule Castle / Castle Tower big-key item and keeps the one-shot row
out of dungeon-key item-pool accounting.

Public assetless builds emit empty generated tables. Any generation request whose
effective `enemy_drop_checks` is active fails closed when the local registry is
absent or stale.

## D3 - Runtime state and staged dispatch

At pickup time, the runtime looks up the generated location id by
`(room, source_slot, drop_kind)`. The absorbable key sprite carries the source slot in
existing snapshot-persisted sprite state, so Phase 1 does not need a separate pending
location table. Normal save/reload before pickup recomputes from the registry on
room reload.

The pickup/absorption hook resolves the placed item with a forced dispatch sentinel
(`vanilla_registry_id = 0xFFFF`) after guarding already-checked locations. The
existing dispatch path marks the checked bit as irreversible pickup intent, then
routes direct grant, confirmation, or receive animation and suppresses the vanilla
case-12 small-key increment.

After a successful check, future room loads consult `Rando_IsLocationChecked(loc)` and
suppress that source's forced-key behavior. Checked enemy-key locations may still
spawn the enemy as a normal enemy if that is safest, but they must not respawn a forced
key, re-run the placed check, or increment the current dungeon key count through the
vanilla path.

## D4 - Placement and key economy

Define one effective predicate, `enemy_drop_keys_active(settings)`, and use it
everywhere: open-location filtering, item-pool construction, junk padding,
self-checks, spoiler/tracker emission, logic wrapping, and runtime activation.

When inactive, enemy-drop locations are skipped and forced key drops remain vanilla.
When active:

- each mapped enemy small-key source is a fillable location;
- its vanilla small key enters the item pool;
- under wild keys, it enters the world pool;
- under Retro, it uses `GenericKey`;
- under Dungeon keys, it enters its own dungeon pool and generated
  `ENEMY_DROP_KEYS_DUNGEON` min-depth gates replace inherited same-key terms for
  vanilla doors.

For Dungeon small keys, assumed-fill seeds only the drops that remain vanilla/free:
`door_drop_total - active key pots - active enemy key drops` per dungeon. This
keeps pot-only, enemy-only, and pot+enemy key-source combinations from
double-counting or under-counting in-context key drops.

The invariant is: active mapped enemy-key locations are present in both the generated
logic registry and the runtime lookup, or the active setting fails closed.

Customizer may pin items on active key-tier enemy-drop locations like other real
non-empty checks. Trap eligibility must be explicit and context-safe; if a trap class
cannot be delivered through the forced-key pickup path, that trap class is not allowed
on enemy-drop locations.

## D5 - Logic model

`rando_logic_gen.py` emits enemy-drop locations with stable ids, regions, vanilla
items, and reviewed source predicates from `assets/rando/enemy_drops.gen.yaml`.
Active enemy-drop keys use those predicates plus `ENEMY_DROP_KEYS_WILD()` worst-case
key-depth gates in Wild/Retro mode. In vanilla-door Dungeon mode, generated
same-key predicates on enemy-drop rows are suppressed behind
`ENEMY_DROP_KEYS_DUNGEON()` and replaced by exact DROP-region `key_mindepth`
terms. The generated table also carries ordinary dungeon location key-depth rows;
codegen applies those as additive gates to matching locations and world-state
overrides so vanilla logic that assumed forced enemy keys were free cannot open too
early. Hyrule Castle additionally derives a virtual `HyruleCastleBigKey` from the
Ball-n-chain one-shot check so Zelda's Cell and the Zelda rescue event can require
the vanilla big-key side effect.

For door shuffle, codegen emits a door x enemy-drop bridge from the same small-key
rows. Each bridge row carries the location id, door-table dungeon, door DROP index,
door region, and source predicate. `DoorShuffleLayout` stores the active
`enemy_drop_keys` flag plus the bridge digest. The door prover excludes active
enemy drops from vanilla free-drop counts and counts reached active enemy-drop
checks as itemized key sources, matching the existing door x pot bridge model.
The runtime `DOORS_LOC_REACHABLE` path searches the enemy bridge after the
door-table and pot bridge rows, gates it on the active shuffled dungeon mask, and
evaluates the generated source predicate after the door region is reached.

If a mapped enemy-drop row lacks required fields, uses an unknown region, duplicates
a source, or has an unsupported drop kind, codegen fails closed. If the setting is
inactive, placement/tracker active-location iteration skips generated enemy-drop ids,
generated enemy-key gates are inert, and the Hyrule Castle virtual big-key side
effect is not derived.

## D6 - Visuals, spoiler, and trackers

Phase 1 requires an unchecked indicator for the forced-key check state. Active
unchecked live carriers and spawned forced-key drops draw a configured marker:
`[Graphics] EnemyDropMarker=item` (default) draws the placed item marker only
when every active marker in the room resolves to the same icon; otherwise it
falls back to the generic marker because the renderer has one shared receive-item
tile slot. `generic` always draws the key-style marker without placement leakage.
This is a client-local visual preference; it does not affect canonical settings,
share strings, hashes, generator version, or corpus. The marker clears when the
location is checked and does not render for inactive, checked, or vanilla
forced-key drops.

Spoilers, native trackers, auto-tracker output, and reach panels group enemy-drop
checks by dungeon and room. They must expose the effective setting, not the raw
setting, so vanilla-key or incompatible combinations normalized to `Off` do not show
phantom checks.

## D7 - Interaction rules

- `drop_shuffle`: remains the prize-pack permutation. It applies to non-check enemy
  drops only. Active enemy-drop checks bypass the prize-pack table.
- `enemy_shuffle`: composes with forced-key enemy-drop checks because runtime
  lookup uses the preserved vanilla room/source slot, not the substituted enemy
  type. Requested dungeon-enemy checks degrade to `Keys` while enemy shuffle is
  active because ordinary enemy logic cannot yet consume substituted type/HP.
- `pot_shuffle`: composes in Wild/Retro mode through the existing pot behavior and
  generated enemy-drop predicates; it also composes in Dungeon mode through
  combined free-drop accounting.
- `door_shuffle`: forces Dungeon small keys and composes through the generated
  door x enemy-drop bridge and door-layout bridge digest.
- vanilla small keys: normalizes enemy-drop checks to `Off`.

## D8 - Dungeon-enemy track

The dungeon-enemy follow-up enables a conservative dungeon-only `Dungeon` tier. It proves:

- static dungeon spawn extraction with stable `(room, source_slot)` identity;
- exclusion of bosses, NPCs, objects, overlords, spawners, transient child sprites,
  dynamic spawns, forced key carriers, and non-killable sources;
- location-capacity headroom within the current post-pot 2048 ceiling;
- logic predicates or conservative exclusions for killability, counted thrown-pot
  alternatives, and room reachability;
- death-time direct-grant semantics that cannot strand an unchecked location;
- visual markers for simultaneous enemy checks;
- tracker/spoiler grouping through generated dungeon location rows.

Overworld ordinary enemy checks remain deferred until runtime carries stable
overworld source identity.

## D9 - Verification strategy

The implementation is not complete until it passes:

- `openspec validate add-rando-enemy-drop-sanity --strict`;
- local registry generation and freshness guards;
- stale registry/order drift tests (duplicate source, duplicate active room,
  unmapped marker, big-key excluded marker);
- Release build and `--rando-selftest`;
- `--door-selftest` for prover/key-source accounting;
- corpus cases for off/default, `drop_shuffle` only, enemy-drop keys under Wild,
  Retro, Dungeon, active door shuffle, and pot interactions;
- targeted runtime playtests: kill then leave before pickup, save/reload before
  pickup, snapshot before/after pickup, pickup then re-enter with no duplicate,
  non-key placed item at an enemy-key location, drop shuffle plus active key checks,
  and checked-bit visual clearing.
