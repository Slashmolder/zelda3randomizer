## ADDED Requirements

### Requirement: Telepathic-tile hint dispatch

The randomizer SHALL surface generated hints in-game by intercepting the vanilla dialogue read, NOT by carving a dynamic dialogue-ID range. `Text_LoadCharacterBuffer` (`src/messaging.c`) SHALL call `Rando_RenderHintMessage(dialogue_message_index, messaging_text_buffer)` before the vanilla dialogue decode; when the randomizer slot is active, `settings.hints == on`, and `dialogue_message_index` is one of the 15 hint-bearing vanilla US telepathic-tile message ids (`0xB5, 0xB8, 0xB9, 0xBA, 0xBB, 0xBE, 0xBF, 0xC0..0xC7`; `0xB4` generic-default excluded), the function SHALL render the generated hint (font-encoded, `0x7f`-terminated) into the buffer and the engine SHALL skip the vanilla decode.

The same intercept ALSO reroutes the fork-extension NPCs through `Rando_RenderHintMessage`: the Storyteller's paid-tip message ids (`0xFF, 0x101, 0x102, 0x103`) and the Fortune-Teller reading ids (`0xEA..0xF1, 0xF6..0xFD`, mapped to the Kakariko or Dark-World hint by the current world bit `(savegame_is_darkworld >> 6) & 1`) render their generated hints the same way. The Lake-Hylia Fortune Teller shares the Kakariko room with no runtime discriminator, so it surfaces the Kakariko hint.

When the slot is inactive, hints are off, or the id is not a hint-tile id, `Rando_RenderHintMessage` SHALL return false and the vanilla text-engine flow SHALL proceed byte-identically.

`Rando_GetHintDialogueId(npc) → uint16` (returning `0x200 + (npc-1)`, or `0xFFFF` when no hint is allocated) SHALL exist and is consumed by the **spoiler emitter** as the entry's `dialogue_id` label. It is NOT consulted by any in-game sprite handler.

> **As-built note**: an earlier draft had hint NPC *sprite handlers* (Sahasrahla, storyteller, bookshelf, Murahdahla) invoking `Rando_GetHintDialogueId` to dispatch a slot-specific dialogue id from a carved dynamic range. The implementation instead intercepts the vanilla message ids in the messaging engine, and `Rando_GetHintDialogueId` survives only as a spoiler label. The Storyteller and the Kakariko/Dark-World Fortune Tellers ARE wired in-game through this same intercept (on their own message ids, above); the bookshelf was dropped and Murahdahla is spoiler-only (the fork has no Murahdahla sprite). `Rando_RemapTeleMsg` exists but is a vestigial unused stub.

#### Scenario: Telepathic tile in rando mode renders the generated hint
- **WHEN** the player reads a hint-bearing telepathic tile in an active randomizer slot with `hints=on`
- **THEN** `Rando_RenderHintMessage` returns true and the message box shows the slot's generated hint instead of the vanilla telepathic text

#### Scenario: Vanilla mode tiles unchanged
- **WHEN** no randomizer slot is active (or `hints=off`) and the player reads a telepathic tile
- **THEN** `Rando_RenderHintMessage` returns false and the standard vanilla text plays byte-identically
