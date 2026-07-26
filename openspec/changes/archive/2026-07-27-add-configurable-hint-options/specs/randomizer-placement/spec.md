## MODIFIED Requirements

### Requirement: Telepathic-tile hint dispatch

The randomizer SHALL give the hint subsystem first refusal for the authoritative
15 original/US telepathic message ids:

`0xB4, 0xB5, 0xB8, 0xB9, 0xBA, 0xBB, 0xBE, 0xBF, 0xC0, 0xC1, 0xC2, 0xC3,
0xC4, 0xC5, 0xC6`.

`0xB4` SHALL remain Eastern Palace and `0xC7` SHALL remain excluded as the
Chris Houlihan-room message. Algorithm 1 SHALL retain its frozen positional
mapping. Algorithm 2 SHALL map the retained 0/5/10/15 assignments through its
certified seed-ranked tile permutation.

For a recognized tile assigned a fact in an active validated plan with a
rich-compatible locale, `Text_LoadCharacterBuffer` SHALL render that complete
fact and commit discovery only after successful encoding. A recognized but
inactive/unassigned tile in an active randomizer slot SHALL render the
locale-appropriate truthful neutral text and SHALL discover nothing. Hints Off,
unsupported/mismatched plans, and locales without a rich encoder SHALL follow
the same neutral randomizer behavior. No owned active tile may fall through to
a seed-invalid vanilla placement claim.

Paid Storyteller carrier ids and Fortune reading ids SHALL render only the
presentation latched by their owning transaction. A bare message lookup SHALL
not select/advance a queue. At algorithm-2 paid depth zero, the owned
pre-payment flow SHALL use the truthful no-clue/actual-price/healing framing
before the choice and SHALL create no hint latch.

When no randomizer slot is active or a physical discriminator does not match,
dispatch SHALL fall through to byte-identical vanilla text/control flow.
Stable spoiler dialogue labels SHALL remain labels rather than runtime
dialogue-table keys.

#### Scenario: Ranked active tile renders its assigned fact
- **WHEN** an algorithm-2 enabled plan retains a recognized physical tile
- **THEN** that tile renders its paired fact completely and discovers it only
  after success

#### Scenario: Unranked tile is neutral
- **WHEN** coverage five leaves another recognized tile outside the retained
  ranked prefix
- **THEN** that tile renders truthful neutral locale text and changes no
  discovery bit

#### Scenario: Nested coverage retains source identity
- **WHEN** the same seed/mix is regenerated at coverage five and ten
- **THEN** each five-tier physical tile still carries the same semantic fact in
  the ten-tier plan

#### Scenario: Failed tile render does not discover
- **WHEN** an assigned fact cannot be completely encoded
- **THEN** no partial rich text or discovery is committed and safe neutral text
  is used

#### Scenario: Paid depth zero cannot advertise a clue
- **WHEN** an owned paid service begins with configured depth zero
- **THEN** its pre-choice text states no clue, the real price, and healing, and
  no carrier fact is selected

#### Scenario: Paid carrier cannot advance by repeated decode
- **WHEN** an active paid presentation buffer loads more than once
- **THEN** it repeats its owning latch and neither selects nor discovers a
  later fact

#### Scenario: Vanilla tiles and services remain byte-identical
- **WHEN** no randomizer slot is active
- **THEN** tile and paid predicates refuse ownership and standard vanilla text
  and control flow are unchanged

#### Scenario: Correct tile-id endpoints are intercepted
- **WHEN** an algorithm-2 Balanced slot reads messages `0xB4` and `0xC6`
- **THEN** both render their distinct ranked assignments and can become
  discovered

#### Scenario: Chris Houlihan message is not a hint tile
- **WHEN** runtime message `0xC7` is loaded
- **THEN** the telepathic-tile predicate refuses it and no supported algorithm
  consumes, renders, or discovers a fact

#### Scenario: Telepathic tile in rando mode renders the generated hint
- **WHEN** an active recognized tile has a completely renderable assigned fact
- **THEN** the live buffer contains that exact fact and its discovery bit is
  set idempotently only after successful rendering

#### Scenario: Off tile text is truthful but not a clue
- **WHEN** an active randomizer slot with normalized hints Off reads a
  recognized tile in original/US, German, or French
- **THEN** it receives that locale's neutral randomizer text and no delivery
  fact is generated or discovered

#### Scenario: Vanilla mode tiles unchanged
- **WHEN** no randomizer slot is active and a telepathic tile is read
- **THEN** the hint renderer refuses the message and standard vanilla dialogue
  bytes and control flow remain unchanged
