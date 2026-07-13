## MODIFIED Requirements

### Requirement: Enemy-drop-check canonical settings axis

The `enemy_drop_checks` setting SHALL support values `Off` (0), `Keys` (1),
`Dungeon` (2), and `All` (3). `All` SHALL be accepted by settings validation, CSV
parsing, share strings, native UI, and file-select UI as a distinct requested value.
The appended canonical enemy-drop byte SHALL encode those values as 0 through 3,
and older shorter canonical blobs and share strings SHALL decode it as `Off`.
Interactive selectors MAY hide or disable `All` for setting combinations where
derived rules immediately lower it, but they SHALL NOT label the dungeon-only tier
as "all". New settings parsing SHALL NOT alias text `all` to `Dungeon`; any legacy
compatibility for that spelling must be version-scoped so an `All` request cannot
silently mean dungeon-only.

`All` is effective only when the `Keys` and `Dungeon` tiers plus every generated
compatible all-tier source can remain active for the selected settings. The current
generated all-tier source set is ordinary dungeon enemies (including curated
finite check-only sources that are unsafe as generic enemy-shuffle replacements,
such as Red Eyegores) plus static ordinary
overworld enemies with stable `(stage, area, source slot, block)` identity, plus
reviewed underworld cave/interior exceptions with stable room/source-slot identity
and direct access predicates, reviewed GT-miniboss event checks, and reviewed
repeatable finite scripted-spawn checks with stable parent/child identity. Unbounded/farmable
spawns and sources without stable death-time identity remain explicit future
scope. The normalized effective value, not an incompatible raw request, SHALL feed
the settings hash, share strings, placement, logic, UI, spoiler, and runtime.
UI/spoiler output MAY also show the raw request with a downgrade or rejection
reason.

Adding the `all` value SHALL update generator-versioned semantics, fixed-settings
selfchecks, share/settings decode, settings hash expectations, UI persistence, and
corpus manifests as required for a new generation-affecting setting value.

Derived rules SHALL apply this compatibility table:

- effective vanilla small keys: requested `All` normalizes to `Off`;
- Wild/Retro/Dungeon small keys, no incompatible shuffles, fresh all-tier registry,
  and sufficient capacity: requested `All` remains `All`;
- missing, stale, duplicate, or capacity-overflowing generated all-tier registry:
  generation rejects active `All`;
- door shuffle: requested `All` remains `All` through generated door x ordinary
  enemy-check bridge rows, source predicates, and digest/replay support;
- enemy shuffle: requested `All` normalizes to the highest lower tier allowed by
  existing derived rules, normally `Keys` but `Off` when the keys tier is unsupported,
  until a future change makes enemy shuffle placement-affecting for all-enemy logic
  and updates digest/corpus expectations;
- boss shuffle: requested `All` remains `All`; emitted GT-miniboss checks are
  outside the shuffleable dungeon-boss room set;
- pot shuffle composes with `All`; generated thrown-pot kill routes require
  effective pot shuffle to be off, while active pot-sanity seeds use the reviewed
  inventory-combat branch so a shuffled pot cannot be counted twice;
- entrance shuffle, including cave entrance shuffle: requested `All` normalizes to
  `Dungeon` until all-tier overworld/domain reachability is modeled against the
  entrance graph, unless another rule lowers the effective tier further; existing
  cave-entrance pot/key derived rules still apply before this normalization.

#### Scenario: All does not silently mean dungeon
- **WHEN** a settings source requests `enemy_drop_checks=all`
- **THEN** the effective setting is `all` only if every compatible generated all-tier
  source is active
- **AND** otherwise the UI/spoiler reports a lower effective tier or generation fails
  with a specific incompatibility diagnostic

#### Scenario: Existing tiers remain distinct
- **WHEN** a settings source requests `enemy_drop_checks=dungeon`
- **THEN** only the dungeon-tier enemy checks are requested
- **AND** static overworld, reviewed-underworld, GT-miniboss, and scripted all-tier rows
  remain inactive

#### Scenario: Old settings decode as off
- **WHEN** a pre-enemy-drop share string or shorter canonical settings blob is decoded
- **THEN** `enemy_drop_checks` defaults to `Off` and all existing settings fields keep
  their prior values

#### Scenario: Selecting keys changes the settings hash only when effective
- **WHEN** `enemy_drop_checks = Keys` and effective small keys are in a supported mode
- **THEN** the appended canonical byte is non-zero and the settings hash changes
  relative to `Off`

#### Scenario: Vanilla key mode normalizes every enemy-check tier off
- **WHEN** any non-off `enemy_drop_checks` tier is requested with effective vanilla keys
- **THEN** derived settings serialize as `Off`, and placement/runtime behavior is
  byte-identical to enemy-drop checks off

#### Scenario: Dungeon key mode keeps key-tier checks active
- **WHEN** `enemy_drop_checks = Keys` and effective small keys are Dungeon
- **THEN** derived settings keep `enemy_drop_checks = Keys`

#### Scenario: Door shuffle preserves supported enemy-check tiers
- **WHEN** `enemy_drop_checks` is `Keys`, `Dungeon`, or `All` and door shuffle is active
- **THEN** the requested tier remains effective through the generated bridge and its
  digest/replay contract

#### Scenario: Enemy shuffle keeps only forced-key checks
- **WHEN** `enemy_drop_checks` is `Dungeon` or `All` and enemy shuffle is active
- **THEN** derived settings serialize as `Keys`, or `Off` when forced-key checks are
  unsupported

#### Scenario: Vanilla key mode normalizes all off
- **WHEN** `enemy_drop_checks=All` but effective small keys are vanilla
- **THEN** derived settings serialize with `enemy_drop_checks=Off`

#### Scenario: Door shuffle preserves all through the generated bridge
- **WHEN** `enemy_drop_checks=All` and door shuffle is active
- **THEN** derived settings serialize with `enemy_drop_checks=All`
- **AND** the door layout digest includes the generated enemy-check bridge digest

#### Scenario: Enemy shuffle normalizes all to supported lower tier
- **WHEN** `enemy_drop_checks=All` and enemy shuffle is active
- **THEN** derived settings serialize with the highest lower tier allowed by existing
  derived rules, normally `Keys` but `Off` when the keys tier is unsupported

#### Scenario: Boss shuffle preserves all
- **WHEN** `enemy_drop_checks=All` and boss shuffle is active, with no enemy shuffle,
  door shuffle, vanilla-key mode, or missing registry
- **THEN** derived settings serialize with `enemy_drop_checks=All`

#### Scenario: Entrance shuffle normalizes all to dungeon
- **WHEN** `enemy_drop_checks=All` and entrance shuffle is active before all-enemy
  entrance-graph reachability is modeled, with no lower-priority normalization rule
- **THEN** derived settings serialize with `enemy_drop_checks=Dungeon`

#### Scenario: Incomplete all registry rejects
- **WHEN** requested `All` would otherwise be effective but generated all-tier data
  is missing, stale, duplicated, or over capacity
- **THEN** generation rejects the seed with a specific diagnostic
