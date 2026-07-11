## MODIFIED Requirements

### Requirement: Enemy-drop check setting has an honest all tier

The `enemy_drop_checks` setting SHALL support values `Off` (0), `Keys` (1),
`Dungeon` (2), and `All` (3). `All` SHALL be accepted by settings validation, CSV
parsing, share strings, native UI, and file-select UI as a distinct requested value.
Interactive selectors MAY hide or disable `All` for setting combinations where
derived rules immediately lower it, but they SHALL NOT label the dungeon-only tier
as "all". New settings parsing SHALL NOT alias text `all` to `Dungeon`; any legacy
compatibility for that spelling must be version-scoped so an `All` request cannot
silently mean dungeon-only.

`All` is effective only when the `Keys` and `Dungeon` tiers plus every generated
compatible all-tier source can remain active for the selected settings. The current
generated all-tier source set is ordinary dungeon enemies plus static ordinary
overworld enemies with stable `(stage, area, source slot, block)` identity, plus
reviewed underworld cave/interior exceptions with stable room/source-slot identity
and direct access predicates, reviewed boss/miniboss event checks, and reviewed
finite scripted-spawn checks with stable parent/child identity. Unbounded/farmable
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
- boss shuffle: requested `All` remains `All`; boss/miniboss checks bind to the
  destination event and coexist with the existing boss reward path;
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
- **AND** static overworld, reviewed-underworld, boss, and scripted all-tier rows
  remain inactive

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

### Requirement: Settings canonical serialization order (normative)

The canonical byte 28 `enemy_drop_checks` encoding SHALL be updated to
`off=0`, `keys=1`, `dungeon=2`, and `all=3`, after derived rules. This replaces any
older byte-28 text that used `all=2` for the dungeon-only tier. The settings
serializer, v2 share-string encoder/decoder, CSV parser, fixed-settings selfchecks,
corpus manifests, and range checks SHALL use this four-value mapping.

Derived rules for byte 28 SHALL apply before canonical serialization and settings
hash computation. `Dungeon` keeps the existing dungeon-tier behavior. `All` keeps
`all=3` only when every compatible generated all-tier source is active for the
selected settings and the generated all-tier registry is fresh and within capacity.
Otherwise it normalizes according to the compatibility matrix in
`Enemy-drop check setting has an honest all tier` or generation rejects.

#### Scenario: Byte 28 distinguishes dungeon from all
- **WHEN** equivalent settings are serialized with effective `Dungeon` and effective
  `All`
- **THEN** byte 28 is `2` for `Dungeon` and `3` for `All`

#### Scenario: Legacy all alias cannot mean dungeon-only
- **WHEN** new settings text uses `enemy_drop_checks=all`
- **THEN** the requested value is `All` (`3`) before derived rules, not `Dungeon`
  (`2`)

#### Scenario: Out-of-range enemy-drop-check byte is rejected
- **WHEN** a v2 share string carries byte 28 greater than `3`
- **THEN** decoding rejects the settings instead of coercing to another tier
