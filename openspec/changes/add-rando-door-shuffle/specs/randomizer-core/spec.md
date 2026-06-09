## ADDED Requirements

### Requirement: Door-shuffle settings axes in canonical serialization

The door-shuffle axes SHALL be appended to the `RandoSettings` canonical serialization
after the existing axes (which, as built, occupy bytes `[0..26]` with byte `[27]` reserved
zero-pad; `kSettingsCanonicalLen == 28`). The committed scope (`doorShuffle ∈
{vanilla, basic}` with `intensity` pinned to 1) SHALL bit-pack into the existing reserved
pad byte `[27]`, so `kSettingsCanonicalLen` stays 28, no size-coupling cascade fires, and
a default-settings (`doorShuffle == vanilla`) `settings_hash` stays byte-identical to the
pre-change value (the enemy-shuffle `[26]` precedent).

The follow-on axes (`intensity 2/3`, `door_type_mode`, `trap_door_mode`, `decoupledoors`,
`door_self_loops`, `partitioned`/`crossed`) exceed byte `[27]`'s 8 bits and SHALL grow
`kSettingsCanonicalLen`, which is a `generator_version` bump trigger and triggers the
coupled-site cascade (the `kSettingsCanonicalLen` `_Static_assert`s in
`rando_settings.h`, `rando_spoiler.{h,c}`, `rando_save.c`, `main.c`, plus the corpus
constants). This change pins the MVP into `[27]` precisely to defer that cascade until the
follow-on axes are real.

`apply_derived_rules` SHALL normalize incompatible combinations (door shuffle ⇒ force
in-dungeon keys for the committed scope) on the private copy before serialization, so the
default tuple still packs `[27]` to zero.

#### Scenario: Default (vanilla) door shuffle keeps settings_hash byte-identical

- **WHEN** a default-settings seed (`doorShuffle == vanilla`) is generated after this
  change
- **THEN** byte `[27]` packs to zero, `kSettingsCanonicalLen` stays 28, and the
  `settings_hash` is byte-identical to the pre-change value for the same axis tuple

#### Scenario: Basic door shuffle changes the per-seed settings_hash

- **WHEN** a seed sets `doorShuffle == basic`
- **THEN** byte `[27]`'s door-shuffle bits flip, the `settings_hash` differs from the
  `vanilla` seed, and `generator_version` advances; the corpus regenerates

#### Scenario: Follow-on axes trigger the canonical-length cascade

- **WHEN** a follow-on change adds axes that exceed byte `[27]`
- **THEN** `kSettingsCanonicalLen` grows, all coupled `_Static_assert` sites + corpus
  constants are updated together, and `generator_version` advances
