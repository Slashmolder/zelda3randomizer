## ADDED Requirements

### Requirement: Door-shuffle settings axis in canonical serialization

The door-shuffle axis SHALL join the `RandoSettings` canonical serialization in the
existing reserved zero-pad byte: `door_shuffle ∈ {vanilla, basic}` packs into byte
`[27]` **bits 0-1** (`kDoorShuffleAxis_Mask`; `intensity` is pinned to 1 and not
serialized), so `kSettingsCanonicalLen` stays 28, no size-coupling cascade fires,
and a default-settings (`door_shuffle == vanilla`) `settings_hash` stays
byte-identical to the pre-change value (the entrance-axes `[25]` / enemy-shuffle
`[26]` precedent). The CSV settings key is `door_shuffle` (`vanilla|basic`), and
the deserializer unpacks the axis from `[27]`. `Settings_EffectiveDoorShuffle`
SHALL report the normalized (post-`apply_derived_rules`) value — definitionally
canonical byte `[27]`'s axis bits.

`apply_derived_rules` SHALL normalize incompatible combinations on the private
copy before serialization (door shuffle coerces to `vanilla` off Open/Standard +
NoGlitches or under entrance shuffle; otherwise it forces in-dungeon small + big
keys — see `randomizer-shuffles`), so the default tuple still packs `[27]` to
zero.

The follow-on axes (`intensity 2/3`, `door_type_mode`, `trap_door_mode`,
`decoupledoors`, `door_self_loops`, `partitioned`/`crossed`) exceed byte `[27]`'s
8 bits and SHALL grow `kSettingsCanonicalLen`, which is a `generator_version` bump
trigger and triggers the coupled-site cascade (the `kSettingsCanonicalLen`
`_Static_assert`s in `rando_settings.h`, `rando_spoiler.{h,c}`, `rando_save.c`,
`main.c`, plus the corpus constants). This change pins the MVP into `[27]`
precisely to defer that cascade until the follow-on axes are real.

#### Scenario: Default (vanilla) door shuffle keeps settings_hash byte-identical

- **WHEN** a default-settings seed (`door_shuffle == vanilla`) is generated after
  this change
- **THEN** byte `[27]` packs to zero, `kSettingsCanonicalLen` stays 28, and the
  `settings_hash` is byte-identical to the pre-change value for the same axis tuple

#### Scenario: Basic door shuffle changes the per-seed settings_hash

- **WHEN** a seed sets `door_shuffle == basic` (and the pins permit it)
- **THEN** byte `[27]`'s bits 0-1 carry the axis, the `settings_hash` differs from
  the `vanilla` seed's, and the round-trip
  (serialize → deserialize → `Settings_EffectiveDoorShuffle`) reports `basic`

#### Scenario: Follow-on axes trigger the canonical-length cascade

- **WHEN** a follow-on change adds axes that exceed byte `[27]`
- **THEN** `kSettingsCanonicalLen` grows, all coupled `_Static_assert` sites +
  corpus constants are updated together, and `generator_version` advances
