## ADDED Requirements

### Requirement: Enemy-drop key logic from generated registry rows

Enemy-drop check locations SHALL be generated into the logic graph with stable ids,
regions, vanilla items, and reviewed reach predicates from
`assets/rando/enemy_drops.gen.yaml`. Missing registry fields, unknown regions,
duplicate `(room, source_slot)` rows, duplicate active rooms, unsupported drop kinds,
or missing one-shot big-key metadata SHALL fail codegen.

When enemy-drop keys are active, the formerly free small keys are item locations and
the lone castle big-key marker is a one-shot item check. Wild effective small-key
mode SHALL use `ENEMY_DROP_KEYS_WILD()` gates for generated worst-case key-depth
requirements; Retro reuses the same effective mode and placement pools
`GenericKey`. Vanilla-door Dungeon mode SHALL use `ENEMY_DROP_KEYS_DUNGEON()` gates
for generated min-depth requirements. Generated enemy-drop locations MAY suppress
their own inherited same-key predicates when replacing them with exact DROP-region
`key_mindepth` terms.

The generated registry SHALL also carry ordinary dungeon location key-depth rows
for every active enemy-drop small-key dungeon. Codegen SHALL wrap matching ordinary
locations and world-state overrides with enemy-drop key-depth requirements so
locations whose vanilla logic assumed forced enemy keys were free cannot appear
reachable before enough itemized keys are held or collected in-context. These
ordinary-location wraps SHALL be additive to avoid weakening existing vanilla or
pot-sanity gates.

The reviewed Hyrule Castle Ball-n-chain big-key marker SHALL expose a logic-visible
virtual `HyruleCastleBigKey` side effect when `enemy_drop_checks = Keys` and the
Ball-n-chain one-shot check is reachable. Runtime counts SHALL also derive the
same virtual item from the live Hyrule Castle big-key bit. Hyrule Castle Zelda's
Cell and the Zelda rescue event SHALL require this virtual side effect when
enemy-drop keys are active.

When door shuffle is active, generated enemy-drop locations SHALL be wrapped by
`DOORS_LOC_REACHABLE(loc_id)`, and the door oracle SHALL evaluate those enemy-drop
locations through the generated door x enemy-drop bridge.

The enemy-drop location itself SHALL use the generated region and predicate. When
enemy-drop checks are inactive, placement/tracker active-location iteration SHALL
skip the generated ids, enemy-drop key-depth predicates SHALL collapse to the
pre-existing reachability rules, and the Hyrule Castle virtual big-key side effect
SHALL not be derived from enemy-drop reachability.

#### Scenario: Active enemy key source has generated logic data
- **WHEN** an enemy forced small-key source becomes a check
- **THEN** its logic entry carries the generated region and reviewed reach predicate,
  and codegen fails if required registry fields are missing

#### Scenario: Dungeon keys use exact generated min-depth
- **WHEN** dungeon small keys are shuffled, door shuffle is vanilla, and enemy-drop keys are active
- **THEN** generated enemy-drop locations participate in reachability with exact
  DROP-region min-depth gates for their own key item

#### Scenario: Ordinary locations get enemy key-depth gates
- **WHEN** a non-enemy dungeon location is behind doors whose forced enemy keys are
  active checks
- **THEN** generated key-depth metadata adds Wild and Dungeon enemy-key gates to
  that location without removing existing vanilla or pot-sanity gates

#### Scenario: Hyrule Castle enemy-drop chain gates Zelda correctly
- **WHEN** `enemy_drop_checks = Keys` and Hyrule Castle small keys are Dungeon
- **THEN** Boomerang Chest/Boomerang Guard require the first HCE key, the
  Ball-n-chain one-shot check requires the second HCE key, Zelda's Cell requires
  the Ball-n-chain-derived `HyruleCastleBigKey`, and the Zelda rescue event also
  requires the sewer key path

#### Scenario: Door shuffle uses generated enemy bridge
- **WHEN** door shuffle and enemy-drop keys are active
- **THEN** generated enemy-drop locations participate in reachability through
  `DOORS_LOC_REACHABLE` and the generated door x enemy-drop bridge

#### Scenario: Wild keys use generated source predicates
- **WHEN** wild small keys are shuffled and enemy-drop keys are active
- **THEN** generated enemy-drop locations participate in reachability through their
  generated source-region predicates and generated worst-case enemy-key gates

#### Scenario: Inactive enemy-drop ids do not affect reachability
- **WHEN** `enemy_drop_checks` is effectively `Off`
- **THEN** enemy-drop ids are skipped by active placement/tracker iteration, generated
  enemy-key gates are inert, and the Hyrule Castle virtual big-key side effect is
  not derived from enemy-drop reachability
