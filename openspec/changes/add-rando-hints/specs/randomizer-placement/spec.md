## ADDED Requirements

### Requirement: Hint NPC dialogue-ID dispatch

Hint NPC sprite handlers (Sahasrahla telepathic, storyteller, bookshelf, Murahdahla) SHALL invoke `Rando_GetHintDialogueId(npc_id) → uint16` to determine which dialogue ID to dispatch. The returned ID maps to a slot-specific entry in the text-engine's dialogue table.

When `kFeatures1_RandomizerActive` is clear, hint NPC handlers SHALL preserve byte-identical vanilla behavior — the accessor returns the vanilla dialogue ID and the standard text-engine flow proceeds.

> **Stub status**: exact sprite-handler patch sites + dialogue-ID range carve-out deferred to apply-time.

#### Scenario: Sahasrahla in rando mode reads slot-specific dialogue
- **WHEN** the player triggers a Sahasrahla telepathic tile in a randomizer slot
- **THEN** `Rando_GetHintDialogueId(NPC_SahasrahlaTelepathic)` returns a dialogue ID pointing at the slot's per-NPC hint text; the text-engine renders that text

#### Scenario: Vanilla mode hint NPCs unchanged
- **WHEN** the binary is in vanilla mode and the player triggers a Sahasrahla telepathic tile
- **THEN** the accessor returns the vanilla dialogue ID; the standard text plays
