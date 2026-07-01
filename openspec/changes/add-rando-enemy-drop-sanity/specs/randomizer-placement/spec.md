## ADDED Requirements

### Requirement: Enemy forced-key drops as shuffled key checks

The placer SHALL treat each generated enemy forced small-key drop and the one-shot
castle big-key drop as a real location in assumed-fill when `enemy_drop_checks = Keys`
is effective. Small-key rows SHALL add their vanilla key item to the item pool and
their source locations SHALL enter the open-location set. The one-shot big-key row
SHALL add only a deterministic filler item because no modeled castle big-key item
exists. Wild effective small-key mode uses the world pool; under Retro it is
represented by the shared `GenericKey`. Vanilla-door Dungeon small-key mode SHALL
use generated DROP-region min-depth gates and combined free-drop accounting.

When the effective small-key mode is vanilla, enemy-drop checks SHALL be inactive
rather than identity-pinned: no enemy-drop location enters placement, no mapped row
is removed from free-drop accounting, and forced key drops remain vanilla. When door
shuffle is active, enemy-drop checks SHALL stay active through the door x enemy-drop
bridge.

The same `enemy_drop_keys_active(settings)` predicate SHALL gate open-location
collection, item-pool construction, junk padding, self-checks, spoiler/tracker
emission, logic wrapping, and runtime activation. Assetless builds with an effective
active setting SHALL fail closed instead of generating a seed with missing
enemy-drop locations.

#### Scenario: Shuffled key mode pools enemy-drop keys
- **WHEN** `enemy_drop_checks = Keys` and small keys are Wild or Retro
- **THEN** mapped enemy forced small-key drops are fillable locations whose keys enter
  the correct world or generic-key pool, and the one-shot big-key row is a fillable
  non-key check

#### Scenario: Dungeon key mode pools enemy-drop keys per dungeon
- **WHEN** `enemy_drop_checks = Keys` and small keys are Dungeon
- **THEN** mapped enemy forced small-key drops are fillable locations whose keys enter
  their own dungeon pools, and remaining free drops are seeded as
  `door_drop_total - active key pots - active enemy key drops`

#### Scenario: Door shuffle itemizes enemy drops
- **WHEN** door shuffle is active
- **THEN** mapped enemy forced small-key drops remain active locations, the door
  prover removes their DROP rows from free-drop accounting, and their keys enter
  their own dungeon pools

#### Scenario: Vanilla key mode leaves drops vanilla
- **WHEN** small keys are vanilla
- **THEN** enemy-drop checks are inactive, mapped rows remain free vanilla drops, and
  the placement digest matches enemy-drop checks off

### Requirement: Enemy-drop staged grant and suppression

Active enemy-drop checks SHALL dispatch placed items through a staged grant path, not
through the vanilla small-key increment path. The staged path SHALL guard checked
locations before dispatch, then force placement resolution with
`vanilla_registry_id = 0xFFFF` for unchecked locations. The existing dispatch path
marks the checked bit as pickup intent and routes direct grants, confirmations, and
receive animations according to the placed item.

After a successful grant, the runtime SHALL suppress future forced-key behavior for
that checked source. This suppression SHALL apply even when the placed item is the
same small key that the vanilla drop would have granted; identity placements still use
placement semantics and SHALL NOT also run the vanilla current-dungeon key increment.

Customizer may pin items on active key-tier enemy-drop locations like other non-empty
locations. Trap shuffle SHALL target enemy-drop locations only for trap classes whose
delivery path is proven safe through the staged pickup hook.

#### Scenario: Identity placement does not double grant
- **WHEN** an active enemy-drop location is identity-placed with its own small key
- **THEN** the staged dispatch grants the placed key once and the vanilla case-12
  current-dungeon increment is bypassed

#### Scenario: Trap eligibility is delivery-safe
- **WHEN** trap shuffle considers an enemy-drop location
- **THEN** only trap classes that can be delivered through the staged forced-key
  pickup path are eligible
