## ADDED Requirements

### Requirement: Independent native NPC reward-preview control

The native Hints tab SHALL label the semantic selector “Clue profile” and SHALL
offer an independent “NPC reward previews” boolean in the pending next-seed
surface. The preview control SHALL remain enabled when clue profile is Off and
SHALL state that rich dialogue is Original/US only.

The active-slot surface SHALL report the loaded slot's certified preview value
separately from pending settings. Editing pending settings SHALL NOT alter the
active value, active dialogue, clue journal, or clue plan.

#### Scenario: Preview can be enabled with clues Off
- **WHEN** the pending clue profile is Off
- **THEN** the preview checkbox remains editable and enabling it does not enable
  any clue delivery

#### Scenario: Pending and active values remain separate
- **WHEN** an active slot has previews Off and pending next-seed settings are
  changed to On
- **THEN** the active status remains Off until a slot carrying the new settings
  is generated and loaded

#### Scenario: Named profile action preserves preview
- **WHEN** previews are On and the player clicks any named clue profile
- **THEN** the preview checkbox remains On
