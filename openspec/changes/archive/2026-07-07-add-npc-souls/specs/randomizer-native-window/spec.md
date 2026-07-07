# Delta: randomizer-native-window (NPC souls)

## ADDED Requirements

### Requirement: NPC souls setting and tracker exposure

The PC settings window SHALL expose `npc_souls` as a checkbox in the souls settings group with a 1-2 durable-player-fact tooltip, and the tracker window's souls section SHALL list the 23 NPC souls (owned/un-owned) whenever the active slot has `npc_souls=on`.

#### Scenario: Toggle reaches the generated seed
- **WHEN** the user enables NPC souls in the settings window and generates a slot
- **THEN** the slot's settings hash, share string, and runtime suppression all reflect `npc_souls=on`

#### Scenario: Tracker hidden when off
- **WHEN** the active slot has `npc_souls=off`
- **THEN** the tracker souls section shows no NPC soul rows
