## Why

The current enemy drop randomizer is intentionally narrow: `drop_shuffle`
permutes the 56-entry vanilla prize-pack table. It does not turn enemies into
randomizer locations, and it does not affect forced key drops. Those forced drops
still bypass the drop-prize table through `sprite_die_action`, so their keys remain
free in the door/key model instead of being shuffled as checks.

Pot Sanity recently established the pattern this feature should reuse: generated
stable location identity, a shared active predicate, key-economy integration, a
checked-location visual, and explicit runtime re-collect safety. Enemy drops need
the same discipline, but with a narrower first step because sprite identity, death
state, and pickup timing are riskier than pots.

The first shippable expansion is **enemy-carried forced key drops as checks**.
When effective small keys are Wild/Retro or Dungeon, including door shuffle's
forced Dungeon mode, each eligible
vanilla enemy that carries a forced small key becomes a real randomizer location;
the key can move elsewhere and the enemy can hold any placed item. The shipped
asset data also has exactly
one forced big-key marker (room `0x080`, a Ball-n-chain / Morning Star guard).
That source ships as a pure one-shot check because the fork currently has no
modeled Hyrule Castle / Castle Tower big-key item: the enemy can hold a placed
item, while the vanilla current-dungeon big-key grant is still preserved exactly
once. Generic prize-pack drops, Pikit/item-steal behavior, spawned enemies,
bosses, and arbitrary enemy drops stay out of Phase 1.

The more extreme dungeon-enemy option is implemented by the follow-up
`add-rando-dungeon-enemy-checks` change as a conservative dungeon-only tier. It emits
ordinary enemy checks only for reviewed dungeon sources that fit capacity and have
reachability metadata; overworld enemies remain excluded until runtime has stable
source identity for them.

## What Changes

- Add a separate `enemy_drop_checks` setting, not a new meaning for
  `drop_shuffle`.
  - `Off` (0): current behavior.
  - `Keys` (1): forced enemy key drops become checks when effective small keys
    are Wild/Retro or Dungeon. Retro maps to Wild and uses `GenericKey` for
    small-key rows; door shuffle forces Dungeon and uses the door x enemy-drop
    bridge.
  - `Dungeon` (2): includes `Keys` and the generated ordinary dungeon enemy
    checks supplied by the follow-up dungeon-enemy registry.
- Append one canonical settings byte for `enemy_drop_checks` instead of trying to
  consume remaining packed bits. Existing 28-byte settings blobs and old share
  strings decode with `enemy_drop_checks = Off`.
- Generate a local ROM-derived enemy-drop registry from vanilla dungeon sprite
  data. The registry is the source of truth for stable identity; door drop-key
  tables and key-depth dumps are cross-check/depth oracles, not identity sources.
- Record the single forced big-key marker as an active pure one-shot location with
  `vanilla_item=Nothing`; the runtime separately preserves the vanilla
  current-dungeon big-key grant.
- In active Wild/Retro modes, mapped enemy small-key drops enter the world/generic
  key pool. In Dungeon mode, they enter their own dungeon pool with generated
  DROP-region min-depth gates, a door x enemy-drop bridge when door shuffle is
  active, and combined free-drop accounting:
  `door_drop_total - active key pots - active enemy key drops`.
- At runtime, recover the generated location id at the pickup/absorption point from
  the generated `(room, source_slot, drop_kind)` lookup, using the snapshot-persisted
  source slot carried by the absorbable key sprite. This source-slot identity also
  lets enemy-drop checks compose with `enemy_shuffle`.
- Mark unchecked enemy-key checks visually on live carriers and spawned forced-key
  drops, with a client-local choice between placed-item and generic gold-glint
  markers. Placed-item mode falls back to generic when simultaneous markers need
  different item icons.
- Keep `drop_shuffle` as the prize-pack shuffle for non-check enemy drops. Active
  enemy-drop checks bypass the prize-pack table and cannot duplicate a vanilla
  forced key.

## Capabilities

### New Capabilities

- `randomizer-enemy-drop-sanity`: generated forced-key enemy-drop registry,
  stable source identity, runtime checked-state dispatch, visual indication, and
  the dungeon-enemy follow-up hook.

### Modified Capabilities

- `randomizer-core`: add the canonical `enemy_drop_checks` setting byte, derived
  normalization, generator-version bump, share-string/settings length handling, and
  fixed-length selfchecks.
- `randomizer-placement`: add active enemy-key locations to assumed-fill under
  Wild/Retro/Dungeon key modes, dispatch placed items safely, and define
  customizer/trap behavior.
- `randomizer-logic`: generate enemy-drop locations and reviewed predicates from
  the local registry, with fail-closed assetless behavior.
- `randomizer-shuffles`: define interaction with `drop_shuffle`, `enemy_shuffle`,
  door shuffle, pot shuffle, and Retro generic keys.
- `randomizer-save`: persist checked state via the existing sidecar sizing rules and
  keep in-room source-slot identity snapshot-safe.
- `randomizer-ui`: expose the enemy-drop-check tier selector, group enemy-drop
  checks in trackers/spoilers, and reflect effective downgrades.

## Non-Goals

- Do not randomize generic prize-pack drops as locations in Phase 1.
- Do not raise the location ceiling for dungeon-enemy checks in Phase 1.
- Do not include overworld ordinary enemy checks until stable overworld source
  identity exists.

## Impact

- **Code**: `src/sprite.c` forced-key marker/load/drop/pickup paths,
  `rando_placement.*`, `rando_logic.*`, `rando_settings.*`, `rando_save.*`,
  spoiler/tracker/native-window code.
- **Generated data**: new local gitignored `assets/rando/enemy_drops.gen.yaml` and
  generated lookup/header tables. Assetless builds emit empty tables and fail closed
  if the effective setting requests active enemy-drop checks.
- **Determinism**: `enemy_drop_checks=Off` and all existing old settings decode
  paths remain placement-byte-identical. Selecting `Keys` changes the settings hash
  and placement only when effective small keys are Wild/Retro or Dungeon and all
  compatibility gates pass.
- **Verification**: OpenSpec validation, generator freshness/drift guards,
  Release build, `--rando-selftest`, corpus coverage, and targeted runtime playtests
  for pickup/re-entry/save/snapshot/no-duplicate behavior.
