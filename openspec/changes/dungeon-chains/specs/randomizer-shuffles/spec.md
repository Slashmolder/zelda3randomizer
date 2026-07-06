# randomizer-shuffles — delta for dungeon-chains

## ADDED Requirements

### Requirement: Dungeon chains axis and mutual exclusion

The settings surface SHALL expose a `dungeon_chains` boolean axis (default off)
packed into the existing canonical entrance-axis byte without changing the
canonical settings length. `apply_derived_rules` SHALL normalize `dungeon_chains`
to off unless ALL of the following hold: no entrance-shuffle axis is active,
`door_shuffle` is vanilla, `boss_shuffle` is off, the world state is Open or
Standard, and the logic tier is NoGlitches. Normalization is one-directional:
enabling `dungeon_chains` SHALL never coerce another axis; conflicting axes win
and chains yields. The default-off packing SHALL leave existing canonical
serializations and settings hashes byte-identical.

#### Scenario: Chains force in-dungeon keys

- **WHEN** `dungeon_chains` remains on through normalization
- **THEN** small keys and big keys serialize as in-dungeon mode, regardless of
  their requested raw modes

#### Scenario: Chains yields to entrance shuffle

- **WHEN** `dungeon_chains` is requested together with any entrance-shuffle axis
- **THEN** `apply_derived_rules` normalizes `dungeon_chains` to off before
  serialization, and the canonical settings reflect what was actually generated

#### Scenario: Chains yields to incompatible world states and tiers

- **WHEN** `dungeon_chains` is requested with Inverted or Retro world state, a
  glitched logic tier, `door_shuffle == basic`, or `boss_shuffle` on
- **THEN** `dungeon_chains` normalizes to off

#### Scenario: Compatible settings keep chains on

- **WHEN** `dungeon_chains` is requested with Open world state, NoGlitches, and
  no entrance/door/boss shuffle active
- **THEN** `dungeon_chains` remains on through normalization and is reflected in
  the settings hash

#### Scenario: Default is hash-stable

- **WHEN** settings leave `dungeon_chains` at its default (off)
- **THEN** the canonical serialization and `settings_hash` are byte-identical to
  builds predating the axis
