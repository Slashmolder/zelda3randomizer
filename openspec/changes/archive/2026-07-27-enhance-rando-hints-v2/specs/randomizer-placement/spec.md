## MODIFIED Requirements

### Requirement: Telepathic-tile hint dispatch

The randomizer SHALL surface telepathic-tile facts by giving the hint subsystem
first refusal before vanilla dialogue decode. The authoritative 15 original/US
runtime message ids SHALL be:

`0xB4, 0xB5, 0xB8, 0xB9, 0xBA, 0xBB, 0xBE, 0xBF, 0xC0, 0xC1, 0xC2, 0xC3,
0xC4, 0xC5, 0xC6`.

`0xB4` is the Eastern Palace tile and SHALL be included. `0xC7` is the Chris
Houlihan-room message and SHALL be excluded. The 15 ids map positionally to the
15 canonical tile source assignments in the active `HintPlan`.

In an active Balanced slot with a validated plan and rich-compatible locale,
`Text_LoadCharacterBuffer` SHALL render the tile's assigned semantic fact
completely and commit its discovery only after successful rendering. In an
active randomizer slot where hints are Off, the assigned source is unfilled, the
plan is unavailable, or the locale has no rich encoder, the recognized tile
SHALL render the locale-appropriate truthful neutral message and SHALL discover
nothing. No active randomizer plan MAY silently fall back to a vanilla
placement claim on a recognized hint tile.

The paid Storyteller carrier ids (`0xFF`, `0x101`, `0x102`, `0x103`) and
Fortune-Teller reading ids (`0xEA..0xF1`, `0xF6..0xFD`) SHALL render only the
fact latched by their owning paid prepare/render/commit transaction. A bare
message-id lookup SHALL NOT independently select or advance a paid queue.
Kakariko and Lake Hylia share the Fortune Light source; the normalized current
world bit selects Fortune Dark.

When no randomizer slot is active or an id/physical discriminator is not owned
by this subsystem, rendering SHALL fall through to the standard vanilla text
path byte-identically.

Stable spoiler dialogue labels MAY remain
`kRandoHintDialogueBase + (legacy_npc_id - 1)` for compatibility, but SHALL NOT
be used as runtime dialogue-table keys. Vestigial dynamic-range/remap stubs
SHALL NOT be described as the live dispatch path.

#### Scenario: Correct tile-id endpoints are intercepted
- **WHEN** the active Balanced slot reads messages `0xB4` and `0xC6`
- **THEN** both render their distinct assigned tile facts and can become
  discovered

#### Scenario: Chris Houlihan message is not a hint tile
- **WHEN** runtime message `0xC7` is loaded
- **THEN** the telepathic-tile predicate refuses it and the hint plan does not
  consume, render, or discover a fact

#### Scenario: Telepathic tile in rando mode renders the generated hint
- **WHEN** a validated Balanced fact fits and is rendered for a recognized tile
- **THEN** the live buffer contains the complete encoded message and that fact's
  discovery bit is set idempotently

#### Scenario: Failed tile render does not discover
- **WHEN** a tile fact cannot be completely encoded into the destination buffer
- **THEN** no partial rich message is installed, no discovery bit changes, and
  the truthful neutral fallback is used when safe for that locale

#### Scenario: Off tile text is truthful but not a clue
- **WHEN** an active randomizer slot with hints Off reads a recognized tile in
  original/US, German, or French
- **THEN** it receives that locale's neutral randomizer text and no delivery
  fact is generated or discovered

#### Scenario: Vanilla mode tiles unchanged
- **WHEN** no randomizer slot is active and a telepathic tile is read
- **THEN** the hint renderer refuses the message and standard vanilla dialogue
  bytes and control flow remain unchanged

#### Scenario: Paid carrier cannot advance by repeated decode
- **WHEN** the text engine loads the same paid carrier buffer multiple times
  during one committed interaction
- **THEN** every load renders the same latched fact and neither selects nor
  discovers a later queue position
