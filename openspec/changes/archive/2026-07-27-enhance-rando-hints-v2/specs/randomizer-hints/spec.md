## MODIFIED Requirements

### Requirement: Hint generation pipeline

The randomizer SHALL build a versioned semantic `HintPlan` after placement is
accepted and after every effective setting/layout/assignment state consumed by
the selected algorithm is installed. The plan SHALL separate:

- ordered typed `HintFact` truth;
- ordered tile/paid `HintSourceAssignment`;
- text `template_id`; and
- mutable `HintDiscoveryState`.

The plan SHALL contain at most 24 delivery facts with stable ids `0..23`:
up to 18 primaries plus up to six paid reserves. Discovery SHALL not be part of
the immutable plan.

Plan construction SHALL be deterministic and independent of placement/candidate
iteration order. It SHALL canonicalize candidates by typed ids and use a
dedicated domain-separated randomizer RNG stream derived from certified seed
inputs. The same supported `(algorithm_version, text_schema_version)` builder
and complete immutable inputs SHALL produce byte-identical facts, assignments,
rendering, canonical serialization, and SHA-256 plan digest on every platform.

The canonical digest SHALL include versions, ordered facts with typed targets,
template ids, and identity-bearing template parameters, plus every
primary/reserve assignment. It SHALL exclude rendered/localized strings,
discovery, checked state, UI state, and Murahdahla.
Spoiler writing SHALL receive the intended plan explicitly rather than reading
an unrelated module-global plan.

The same versioned builder SHALL serve initial generation, sidecar activation,
race reveal, native spoiler export, warm snapshot replay, and fresh-process cold
replay. Activation/replay SHALL compare the rebuilt digest with persisted
identity before installing discovery. Missing, unsupported, malformed, or
mismatched identity SHALL disable only hints and SHALL never reroll an existing
slot with the current builder.

Hints Off SHALL build the canonical empty plan. It SHALL emit no delivery facts
or discovery but SHALL retain current version/digest identity for persistence.

#### Scenario: Same seed produces same hints
- **WHEN** the same accepted settings, seed, placement, spheres, and effective
  assignments are built twice with the same supported versions
- **THEN** facts, source assignments, text, canonical bytes, and digest are
  byte-identical

#### Scenario: Placement iteration order does not select another deck
- **WHEN** semantically identical placement/candidate inputs are supplied in a
  different iteration order
- **THEN** canonical ordering produces the same plan and digest

#### Scenario: Full lifecycle reconstructs generation-time hints
- **WHEN** one complete plan is rebuilt through generation, activation, reveal,
  native export, warm replay, and cold replay
- **THEN** every path produces the same plan bytes/digest before discovery is
  applied

#### Scenario: Unsupported plan cannot be silently upgraded
- **WHEN** persisted identity requests an unsupported algorithm or text schema
- **THEN** rich hints, discovery, journal, and paid queues remain disabled for
  that activation and the current builder is not substituted

#### Scenario: Hints section present in spoiler
- **WHEN** a seed is generated with Balanced hints
- **THEN** the full spoiler serializes its explicit plan and additive typed
  metadata in canonical order

#### Scenario: Hints off skips generation
- **WHEN** a seed is generated with hints Off
- **THEN** the certified plan has zero facts/assignments/discovery and the text
  spoiler omits delivery rows

### Requirement: Hint source NPCs

The randomizer SHALL retain the following stable delivery surfaces:

1. **15 telepathic tiles**, in canonical positional order: Eastern Palace,
   Tower of Hera Floor 4, Spectacle Rock, Swamp Entrance, Thieves Town
   Upstairs, Misery Mire, Palace of Darkness, Desert Bonk Torch Room, Castle
   Tower, Ice Large Room, Turtle Rock, Ice Entrance, Ice Stalfos Knights Room,
   Tower of Hera Entrance, and South-East Darkworld Cave.
2. **Storytellers**, one logical paid source shared by every paid Dark-World
   Storyteller subtype.
3. **Fortune Light**, one logical paid source shared by Kakariko and Lake Hylia.
4. **Fortune Dark**, the Dark-World Fortune Teller source.

Primary assignments SHALL target 18 facts: one for each of the 15 tile sources
and queue position 0 for each of the three paid sources. Each paid source SHALL
then receive up to two additional exact-useful reserve assignments at queue
positions 1 and 2. No fact SHALL be duplicated merely to populate a source.
When candidate supply safely underfills, unassigned tiles use neutral text and
short paid queues exhaust safely.

Lake Hylia SHALL share both assignments and discovery progression with
Kakariko; it SHALL NOT own an indistinguishable fourth paid queue. Fortune Dark
SHALL remain independent.

Murahdahla SHALL remain a separately generated spoiler-only region summary when
`goal ∈ {triforce-hunt, ganon-hunt}`. It SHALL not consume a delivery fact,
source assignment, reserve position, paid queue, or discovery bit.

#### Scenario: Complete plan assigns all delivery surfaces
- **WHEN** at least 24 distinct eligible facts exist
- **THEN** all 15 tiles have one primary, each paid source has a primary head
  plus two reserves, and no fact is assigned twice

#### Scenario: Scarce plan underfills honestly
- **WHEN** fewer eligible complete facts exist than source capacity
- **THEN** the plan leaves affected assignments empty instead of duplicating,
  fabricating, ambiguously rendering, or failing seed generation

#### Scenario: Kakariko and Lake Hylia share one queue
- **WHEN** a fact is purchased from Kakariko and the next paid interaction is at
  Lake Hylia
- **THEN** Lake Hylia selects the next undiscovered Fortune Light assignment,
  not a separate or repeated queue head

#### Scenario: Dark Fortune queue is independent
- **WHEN** Fortune Light advances
- **THEN** Fortune Dark retains its own first undiscovered assignment

#### Scenario: Triforce Hunt populates Murahdahla
- **WHEN** a seed has `goal ∈ {triforce-hunt, ganon-hunt}` and Balanced hints
- **THEN** its full spoiler contains the Murahdahla region summary without
  changing delivery counts, assignments, digest, or discovery

#### Scenario: Non-hunt goals omit Murahdahla
- **WHEN** a seed has another goal
- **THEN** no Murahdahla compatibility row is emitted

#### Scenario: Fork-extension NPCs surface their hints in-game
- **WHEN** a Storyteller or Fortune-Teller paid interaction successfully
  prepares, renders, charges, and commits a queue fact
- **THEN** its existing carrier dialogue shows that exact latched fact, Lake
  Hylia shares Kakariko's Fortune Light progression, and no other source
  assignment is consumed

### Requirement: Vanilla dialogue hint redirects

A reviewed subset of vanilla dialogue that makes a concrete item/location claim
SHALL be replaced with placement-correct text in an active randomizer slot.
Item-to-location redirects SHALL name the fixed referenced item's active
randomized location. A physical location-to-item surface SHALL instead name the
item placed at that location.

The implemented runtime messages SHALL remain Sahasrahla's Green Pendant
direction (`0x33`), post-Agahnim Moon Pearl telepathy (`0x36`), the old mountain
man's Moon Pearl advice (`0x9E`), the Bumper Cave sign (`0xA8`), Stumpy's Flute
prompt (`0xE5`), Aginah's Book advice (`0x125`), and the Dark-World bully's
Moon Pearl advice (`0x15D`).

A redirect SHALL claim a surface only when a randomizer slot is active and its
runtime discriminator matches. In Balanced original/US context with valid
active placement, it SHALL resolve on every use; duplicate item targets SHALL
choose the lowest numeric location id deterministically. Complete rich text
SHALL fit the bounded renderer.

At a recognized/matching randomizer surface, hints Off, unavailable settings or
placement/target, or a locale without rich fact rendering SHALL use that
locale's truthful neutral randomizer template rather than repeat a seed-invalid
vanilla location claim. Original/US, German, and French SHALL have
grammar-correct neutral forms. A physical discriminator mismatch SHALL still
fall through to vanilla because the reused message id is not owned in that
context. A vanilla slot SHALL remain byte-identical.

Stumpy SHALL retain a hint-subsystem-owned post-decode rewrite with one fact or
neutral page and a second page containing the original Yes/No control command.
The ordinary early renderer SHALL refuse this interactive row. Redirects SHALL
NOT consume/advance a delivery fact, discovery bit, paid queue, plan RNG,
spoiler assignment, or telepathic-tile assignment. Active rich or neutral
dynamic rows SHALL remain readable under story-dialogue fast-forward.

Progressive-tier-ambiguous Master Sword prose, generic location-only flavor,
other interactive choice messages, telepathic tiles, and Fortune Teller carrier
ranges SHALL remain outside this redirect table.

#### Scenario: Aginah redirects to randomized Book location
- **WHEN** runtime message `0x125` is shown at its active original/US randomizer
  surface with Balanced hints and the Book at Sick Kid
- **THEN** the complete rendered text names the Book and Sick Kid instead of the
  Library

#### Scenario: Library contents do not drive Book resolution
- **WHEN** a Bottle is placed at the Library and the Book is elsewhere
- **THEN** `0x125` names the Book's placement and ignores the Library's item

#### Scenario: Moon Pearl advice follows its active placement
- **WHEN** Moon Pearl exists in active placement and `0x36`, `0x9E`, or `0x15D`
  is shown under its redirect gates
- **THEN** each surface names the same deterministically resolved location

#### Scenario: Green Pendant direction follows prize placement
- **WHEN** Sahasrahla shows `0x33` and the Green Pendant is outside Eastern
  Palace
- **THEN** the complete redirect names its active placement

#### Scenario: Bumper Cave sign resolves location to item
- **WHEN** `0xA8` is read outdoors on screen `0x4A` and Bumper Cave contains
  Hookshot
- **THEN** the sign names Hookshot rather than Piece of Heart with complete
  concise framing

#### Scenario: Other Heart Pieces do not drive Bumper Cave
- **WHEN** another location contains a Heart Piece and Bumper Cave contains a
  different item
- **THEN** `0xA8` resolves exact `LOC_Bumper_Cave` and ignores the other copies

#### Scenario: Bumper Cave context fails closed
- **WHEN** `0xA8` is requested indoors or on an overworld screen other than
  `0x4A`
- **THEN** the redirect does not own that physical surface and preserves its
  vanilla dialogue

#### Scenario: Stumpy preserves the interactive quest flow
- **WHEN** `0xE5` is shown and Flute is placed at Desert Ledge
- **THEN** the completed original/US buffer names Flute and Desert Ledge,
  presents Yes/No at the canonical low-row cursor columns, and ends its choice
  page with the expected Choose command

#### Scenario: Interactive hint cursor movement preserves labels
- **WHEN** the player presses Up or Down on Stumpy's generated prompt or a
  localized paid no-clue choice
- **THEN** the built-in cursor overlay changes only the canonical prefix and
  neither option label is erased, cut off, or XOR-corrupted

#### Scenario: Stumpy's early renderer cannot remove gameplay control flow
- **WHEN** `0xE5` actively resolves
- **THEN** the ordinary early renderer refuses it and only the post-decode
  interactive hook may replace the prompt

#### Scenario: Stumpy neutral fallback keeps the choice
- **WHEN** Stumpy is a recognized active randomizer surface but rich hint text
  is unavailable or hints are Off
- **THEN** the locale's neutral page replaces the false placement claim while
  Yes/No and the original control command remain operative

#### Scenario: Stumpy's randomized reward remains independent
- **WHEN** the player selects Yes
- **THEN** the handler advances normally and `LOC_Stumpy` names/grants its own
  placed item exactly once, independent of the Flute's placement or hint plan

#### Scenario: Duplicate target items are deterministic
- **WHEN** customizer placement has multiple copies of a redirect target
- **THEN** the rich redirect names the lowest numeric location id independent
  of placement iteration order

#### Scenario: Missing target uses neutral randomizer text
- **WHEN** a matching active randomizer surface lacks its referenced item,
  location, settings, or placement
- **THEN** it renders the locale's neutral template, makes no false placement
  claim, and changes no plan/discovery state

#### Scenario: Hints Off does not restore false placement prose
- **WHEN** a matching redirect surface is used in an active hints-Off slot
- **THEN** it shows truthful neutral randomizer text rather than a rich clue or
  seed-invalid vanilla location claim

#### Scenario: German and French redirects remain truthful
- **WHEN** a matching redirect is shown under German or French dialogue
- **THEN** a grammar-correct neutral message using that locale's encoder is
  shown and no original/US glyph/command bytes are injected

#### Scenario: Adjacent and already-owned hint IDs are not intercepted
- **WHEN** an adjacent id, telepathic-tile id, or Fortune carrier id is shown
- **THEN** the vanilla-dialogue redirect table does not claim it and its owning
  subsystem/vanilla path continues

#### Scenario: Dynamic story hints remain readable
- **WHEN** story-dialogue fast-forward is enabled and `0x36` resolves to either
  a rich or neutral dynamic randomizer message
- **THEN** story fast-forward does not auto-advance that message

#### Scenario: Every Bumper item name remains useful
- **WHEN** any currently eligible registry item is placed at Bumper Cave
- **THEN** one complete bounded form retains its qualified item identity, or
  the rich fact is rejected and neutral text is used without truncation

#### Scenario: Vanilla mode preserves byte-identical dialogue
- **WHEN** no randomizer slot is active
- **THEN** vanilla dialogue bytes, Stumpy's choice flow, and every paid handler
  remain unchanged

#### Scenario: Existing generated hints remain independent
- **WHEN** any rich or neutral redirect renders
- **THEN** facts, assignments, discovery, paid selection, plan digest, spoiler
  rows, and tile mappings remain unchanged

#### Scenario: Inapplicable redirects preserve vanilla dialogue
- **WHEN** no randomizer slot is active or a reused message id's physical
  discriminator does not match
- **THEN** the redirect table does not own the surface and preserves its
  original dialogue buffer and control flow

#### Scenario: F12 reports redirect resolution
- **WHEN** F12 is pressed while a recognized redirect message is current
- **THEN** the diagnostic identifies the redirect and reports its source,
  surface kind, resolved target when active, or the exact fail-closed reason

## ADDED Requirements

### Requirement: Balanced semantic composition

Balanced SHALL classify candidates from one authoritative item/location hint
metadata source and SHALL target the following primary composition:

1. two high-priority-item placement facts;
2. four other diverse major-item placement facts;
3. three high-friction exact location-content facts;
4. up to three truthful goal or active-setting facts;
5. up to two positive region-value facts;
6. three other varied useful placement facts; and
7. at most one truthful flavor fact.

Missing categories SHALL backfill deterministically from major then useful exact
facts. Every fact SHALL be directly derivable from accepted immutable
settings/placement inputs. Region summaries SHALL be positive counted value
only. This change SHALL NOT emit "required", "Way of the Hero", "foolish",
"barren", path-necessity, or other counterfactual claims.

The reserve pass SHALL choose only distinct exact-useful facts and SHALL exclude
flavor and vague region summaries. No fact SHALL lose dungeon-item identity,
multi-check location suffix, or another qualifier needed to distinguish its
typed target.

#### Scenario: Primary categories meet available quotas
- **WHEN** a candidate pool contains enough eligible facts in every category
- **THEN** the 18 primaries have the target composition and stable deterministic
  order

#### Scenario: Missing category backfills deterministically
- **WHEN** a category has fewer eligible facts than its target
- **THEN** major then useful exact facts fill available capacity in canonical
  deterministic order without changing another run's result

#### Scenario: Reserve facts are exact and useful
- **WHEN** all six reserve positions are populated
- **THEN** each fact has an exact target, is distinct from all prior facts, and
  is neither flavor nor a vague region summary

#### Scenario: No unsupported necessity claim is emitted
- **WHEN** a fact's truth can be proven only by removing an item/location and
  re-proving beatability
- **THEN** it is ineligible because the Hints v2 builder has no counterfactual
  oracle

#### Scenario: Positive region value is literal
- **WHEN** a region summary is selected
- **THEN** its value/count follows directly from typed eligible placements and
  does not label another region barren, foolish, or required

### Requirement: Complete semantic rendering and localized neutral fallback

Every eligible delivery fact SHALL have a stable template id and compact typed
names that retain complete semantic identity. The renderer SHALL encode into
bounded scratch storage first and SHALL succeed only when the entire message,
commands, and terminator fit. Only a successful complete rendering may replace
the live message buffer or make a fact discoverable.

The authoritative alias layer SHALL distinguish dungeon keys, key rings, maps,
and compasses by dungeon, SHALL retain multi-check location suffixes, and SHALL
provide separate complete qualified and lossless compact-qualified item names
plus lossless compact location/region names. Codegen SHALL reject the source if
any declared eligible registry item/location pair, supported region-count form,
fixed redirect, or Bumper item form cannot complete in the actual encoded glyph
envelope. Unknown future qualified identities SHALL be "not renderable" until
explicit metadata is added; runtime SHALL NOT collapse them to a generic kind,
numeric fallback, or truncated base name.

The checked-in merged-registry contract SHALL approve the complete shipped
6,364-location/251-region superset, including identity, naming, region/type,
source-provenance, and family-count fingerprints. Full-artifact codegen SHALL
match that exact approval. Public assetless codegen MAY emit only the exact
324-location/40-region base while binding both metadata fingerprints to the
complete approved policy object. Any partial or in-between artifact set SHALL
fail closed, and an approved merged-registry change SHALL require both the
algorithm and text-schema compatibility axes to advance.

Rich fact rendering in this change SHALL support the original/US grammar.
Recognized hint-owned surfaces SHALL provide grammar-correct neutral
original/US, German, and French templates for Off, unavailable/invalid plan,
unfilled assignment, unsupported rich locale, and paid exhaustion. Neutral text
SHALL make no item/location/path claim. Full rich fact localization is deferred.

#### Scenario: Complete cross-product fit is enforced
- **WHEN** every eligible item/location/template combination is checked
- **THEN** every declared current registry combination produces one complete
  canary-safe encoded message preserving typed identity, or codegen fails

#### Scenario: Dungeon identity is never generic
- **WHEN** two dungeon-specific small keys, maps, compasses, or key rings are
  candidates
- **THEN** their accepted rendered names remain distinguishable

#### Scenario: Multi-check suffix survives
- **WHEN** two checks share a base location name but have different semantic
  suffixes
- **THEN** each accepted fact renders the suffix needed to identify its exact
  target

#### Scenario: Locale without rich encoder uses neutral text
- **WHEN** a German or French player uses a recognized Balanced hint surface
- **THEN** the surface renders a grammar-correct neutral message with its own
  command/font encoding and discovers no rich fact

#### Scenario: Unknown qualified name fails closed
- **WHEN** a new registry identity has no lossless compact alias
- **THEN** codegen/self-check or eligibility rejects that rich fact rather than
  emitting an ambiguous/truncated/numeric name

#### Scenario: Partial generated registries cannot redefine compatibility
- **WHEN** ordinary codegen sees merged counts or fingerprints matching neither
  the exact approved full registry nor the exact public assetless base
- **THEN** it refuses the artifact set instead of emitting metadata under an
  ambiguous partial-registry identity

### Requirement: Discovery journal state

Each active validated delivery plan SHALL have one 24-bit discovery state keyed
by stable fact id. Tile discovery SHALL commit only after a complete active-plan
fact render. Paid discovery SHALL commit only through the paid transaction
requirement below. Discovery commits SHALL be idempotent and monotonic within a
session except when authoritative save/snapshot load replaces the state.

Read-only fact getters, spoiler serialization, F12 diagnostics, native journal
rendering, the confirmed full viewer, plan rebuild, and checked-location changes
SHALL NOT discover a fact. An exact discovered fact's resolved marker SHALL
derive from the checked-location bitmap and SHALL NOT be persisted separately.
Murahdahla and vanilla-dialogue redirects SHALL not consume discovery bits.

#### Scenario: Reading a tile discovers one fact
- **WHEN** one assigned tile completes its rich render
- **THEN** exactly its fact id becomes discovered and a repeated render is
  idempotent

#### Scenario: Read-only inspection cannot discover
- **WHEN** spoiler, F12, journal, and confirmed full-deck getters enumerate the
  plan
- **THEN** discovery bytes and paid selection remain unchanged

#### Scenario: Resolution is derived after discovery
- **WHEN** an exact discovered target later becomes checked
- **THEN** the journal reports it resolved without changing the fact's
  discovery bit or plan digest

#### Scenario: Authoritative replay replaces discovery
- **WHEN** a valid sidecar or snapshot identity is activated
- **THEN** its persisted discovery replaces prior process state only after the
  rebuilt plan digest verifies

### Requirement: Paid hint queue transaction

The hint subsystem SHALL own three logical paid queues:

- Storytellers, shared by paid subtypes 0/1/2/4;
- Fortune Light, shared by Kakariko and Lake Hylia; and
- Fortune Dark, selected by the normalized Dark-World bit.

Each queue SHALL have fixed assignment order `[primary head, reserve 1,
reserve 2]`, with absent underfilled positions allowed. Selection SHALL scan for
the first undiscovered assigned fact and SHALL dynamically skip an exact fact
whose target location is already checked. Skipping SHALL not discover the fact
or waste a paid clue.

A paid interaction SHALL use prepared-presentation and Commit phases. Prepare
SHALL select Ready or Exhausted without discovery mutation, capture active plan
epoch/source/queue/fact, completely encode the selected fact or localized
neutral message into transaction-owned scratch, and arm that exact presentation
before any new hint charge. An incomplete encoding SHALL fail Prepare. Every
carrier load SHALL reuse the prepared scratch. Commit SHALL revalidate the
epoch/source/queue/fact and complete scratch, mark a Ready fact discovered
idempotently, and leave the exact presentation latched until the owning source
completes. Exhausted SHALL commit no discovery.

Decline, insufficient funds, or Prepare/render failure SHALL not charge for a
new hint, discover, or advance a fact. A stale epoch or replay/reload before
Commit SHALL fail validation, clear the stale presentation, and SHALL NOT
discover or advance a fact in either plan; source-specific payment/refund
handling SHALL preserve the established single-deduction choreography.
Exhausted services SHALL use normal price/healing behavior but commit no
discovery and never repeat an earlier clue.

Every generated paid no-clue choice SHALL align its two option labels with the
message engine's fixed messages 1/2 cursor overlay in every supported locale.

Storytellers SHALL deduct 20 rupees exactly once after successful Prepare,
Commit before showing the prepared carrier, refund if that Commit fails, and
retain their existing `0xA0` healing completion. Fortune Tellers SHALL show the
prepared reading in state 4, preserve their visible 10/15/20/30-rupee cost
receipt in state 5, perform exactly the one existing deduction, Commit in state
6, and heal by the existing amount. They SHALL NOT add a prepayment or
second-deduction marker. Hints Off SHALL retain paid-service ordering, price,
and healing while owned hint carriers render truthful neutral text.

#### Scenario: Successful Storyteller purchase commits then heals
- **WHEN** the player accepts with at least 20 rupees and Ready text renders
- **THEN** exactly 20 rupees are deducted, the selected fact commits once, its
  carrier repeats the same latched text as needed, and existing healing
  completes

#### Scenario: Fortune cost receipt does not double-charge
- **WHEN** a Ready Fortune interaction displays a selected 10/15/20/30 cost and
  shows its prepared reading in state 4
- **THEN** state 5 shows the existing receipt, state 6 deducts exactly once and
  commits that prepared fact, and healing/completion remain intact

#### Scenario: Decline and insufficient funds are non-mutating
- **WHEN** the player declines or cannot afford the selected service
- **THEN** no paid fact is discovered/advanced and the existing decline or
  insufficient-funds flow remains

#### Scenario: Render failure happens before charge
- **WHEN** a selected fact cannot be completely rendered
- **THEN** no new hint charge, discovery, queue movement, or partial carrier
  message occurs

#### Scenario: Checked exact target is skipped without waste
- **WHEN** the first undiscovered queue fact targets an already checked location
- **THEN** selection proceeds to the next eligible assignment without marking
  the skipped fact discovered or charging for it as a clue

#### Scenario: Repeated carrier loads remain on one fact
- **WHEN** the same prepared or committed paid message buffer loads multiple times
- **THEN** the presentation latch returns the same fact and no later assignment
  becomes discovered

#### Scenario: Stale transaction cannot commit into another plan
- **WHEN** slot switch, replay, rebuild, or deactivation changes the plan epoch
  between Prepare and Commit
- **THEN** Commit fails, source-specific refund handling applies where
  applicable, the stale latch clears, and discovery in both plans is unchanged

#### Scenario: Exhausted queue preserves service
- **WHEN** no undiscovered and unchecked assignment remains
- **THEN** the localized no-new-clues message is used, normal price/healing
  behavior completes, and no discovery or prior fact repeats

#### Scenario: Light queue progress is shared but Dark is isolated
- **WHEN** a Light fact is bought at Kakariko, followed by Lake Hylia and then
  Dark World
- **THEN** Lake Hylia selects Light's next assignment while Dark World selects
  Dark's independent current assignment

### Requirement: Hint diagnostics policy

F12 `dump_hints.txt` SHALL identify the active plan's algorithm version,
text-schema version, digest, activation/disable status, facts, primary/reserve
assignments, discovery bits, current paid selection/latch, and any recognized
vanilla-dialogue redirect resolution useful for development.

F12 SHALL be treated as an explicit spoiler-bearing developer surface. Race mode
SHALL NOT require fact, target, assignment, discovery, or queue redaction from
this dump. The dump header SHALL state that it contains spoilers and report the
active race flag so captured diagnostics cannot be mistaken for a
race-participant-safe artifact.

#### Scenario: F12 diagnoses plan mismatch
- **WHEN** plan reconstruction is disabled by unsupported versions or a digest
  mismatch
- **THEN** the dump records persisted/current versions, expected/computed
  digest when available, and the exact disable reason

#### Scenario: Race F12 remains a full developer dump
- **WHEN** F12 is invoked in a race slot
- **THEN** the header marks `race_mode` and spoiler-bearing content, while full
  internal hint state may remain visible

#### Scenario: F12 cannot mutate discovery
- **WHEN** a full plan/queue dump is written
- **THEN** plan, discovery, checked state, payment state, and presentation
  latches are unchanged
