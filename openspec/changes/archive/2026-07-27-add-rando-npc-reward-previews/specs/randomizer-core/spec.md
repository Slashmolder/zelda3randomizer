## ADDED Requirements

### Requirement: Independent NPC reward-preview seed setting

The randomizer SHALL expose strict boolean setting
`hint_npc_reward_reveal`, default false. It SHALL serialize at canonical byte
`[25]` bit 7 without changing the 31-byte canonical length or 76-character v2
share-string length. The option SHALL remain independent of clue mode, clue
profile normalization/application, semantic hint-plan construction, and hint
plan digest.

Hints Off plus previews On SHALL be valid. Named clue-profile actions and
ordinary presets SHALL preserve the independent value. Enabling the value SHALL
change settings hash/share identity but SHALL NOT change placement, spheres,
semantic facts, delivery assignments, hint rows, or clue-plan digest.

The generator SHALL emit current version 160. Settings at the historical
additive schema floor of generator 157 or newer SHALL define the bit; the
special generator-156 race-reveal path SHALL require it to be zero. Persistence
SHALL recover the option from canonical settings using the integrated sidecar
v14 exact 278-byte extension and snapshot hint TLV type 11, without additional
format growth for this option.

#### Scenario: Hints Off retains NPC reward previews
- **WHEN** settings specify `hints=off,hint_npc_reward_reveal=true`
- **THEN** the normalized clue profile is Off, the preview setting remains true,
  and the seed has no semantic clue rows

#### Scenario: Named profiles preserve the independent option
- **WHEN** the preview setting is true and the player selects Off, Sparse,
  Balanced, or Direct
- **THEN** the profile writes only clue-policy fields and leaves the preview
  setting true

#### Scenario: Canonical bit is isolated
- **WHEN** otherwise identical normalized settings differ only by the preview
  boolean
- **THEN** only canonical byte `[25]` bit 7 differs and their settings
  hash/share identities differ

#### Scenario: Placement and clue plan are orthogonal
- **WHEN** the same seed/settings are generated once with previews Off and once
  with previews On
- **THEN** placement digest, sphere digest, semantic clue plan, plan digest, and
  hint rows are identical

#### Scenario: Generator-156 cannot claim the new bit
- **WHEN** a CRC-correct generator-156 suppressed artifact sets canonical
  `[25]` bit 7
- **THEN** reveal returns `SettingsCorrupt`, leaves the artifact byte-identical,
  and writes no output or scratch file
