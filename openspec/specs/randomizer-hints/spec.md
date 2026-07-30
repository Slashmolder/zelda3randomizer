# randomizer-hints Specification

## Purpose
Define deterministic, truthful randomizer hint planning and delivery across
in-game sources, paid services, spoilers, discovery UI, persistence, and replay.
## Requirements
### Requirement: Hint generation pipeline

The randomizer SHALL build a versioned semantic `HintPlan` after placement is
accepted and every effective setting/layout/assignment state consumed by the
selected algorithm is installed. The plan SHALL retain the Hints v2 separation
of ordered typed facts, ordered tile/paid assignments, template identity, and
mutable discovery.

The plan SHALL contain no more than 24 compact delivery facts with contiguous
ids. Discovery SHALL not be part of the immutable plan. Construction SHALL be
deterministic, independent of placement/candidate iteration order, and use
canonical typed ids plus version/domain-separated randomizer RNG streams.

The binary SHALL support exactly these hint identity pairs:

- algorithm 1 / text schema 1: the frozen `enhance-rando-hints-v2` selection,
  assignments, rendering, and digest; and
- algorithm 2 / text schema 2: configurable mix, nested delivery policy, and
  localized paid-depth-zero framing.

Crossed, zero, or unknown pairs SHALL be unsupported. Current persisted
lifecycle paths SHALL dispatch pair 2/2 and SHALL NOT substitute another pair.
Algorithm 1 remains available only to the authenticated generator-156
race-reveal path; its facts, assignments, encoded text, canonical digest bytes,
empty plan, and legacy spoiler shape SHALL remain byte-identical to the frozen
artifact.

For algorithm 2, the canonical SHA-256 stream SHALL include normalized policy
before ordered facts and assignments. Algorithm 1 SHALL retain its original
stream without policy bytes. Both algorithms SHALL exclude rendered/localized
strings, discovery, checked state, UI state, and Murahdahla from their digest.
Spoiler writing SHALL receive the intended immutable plan explicitly.

The current builder SHALL serve generation, sidecar activation, ordinary save,
current race reveal, native spoiler export, warm snapshot replay, and
fresh-process cold replay. Activation/replay SHALL compare the rebuilt digest
with persisted identity before installing discovery. Missing, unsupported,
malformed, crossed, policy-inconsistent, or mismatched identity SHALL disable
only hints and SHALL never upgrade/reroll the slot. The frozen builder serves
only the separately authenticated generator-156 race reveal.

Hints Off SHALL build the current pair's canonical empty plan with no delivery
facts or discovery. Current generation SHALL stamp algorithm 2/text schema 2.

#### Scenario: Current configurable plan is deterministic
- **WHEN** the same accepted settings, seed, placement, spheres, overlays, and
  algorithm-2 policy are built twice
- **THEN** policy, facts, assignments, text, canonical bytes, and digest are
  byte-identical on every platform

#### Scenario: Candidate order does not select another custom deck
- **WHEN** semantically identical algorithm-2 candidates are supplied in
  reversed iteration order
- **THEN** canonicalization produces the same maximal and retained plan

#### Scenario: Algorithm 1 remains exact for its authentic artifact
- **WHEN** the genuine signed generator-156 race artifact is revealed by the
  current binary
- **THEN** it reproduces the frozen facts, assignments, encoded text, digest,
  and legacy spoiler shape without applying algorithm 2

#### Scenario: Algorithm 2 reconstructs generation-time policy
- **WHEN** a certified 2/2 custom slot is rebuilt through every lifecycle seam
- **THEN** persisted canonical settings recover the same normalized policy and
  the rebuilt policy-bound digest matches before discovery is applied

#### Scenario: Crossed version pair fails closed
- **WHEN** persisted identity names 1/2 or 2/1
- **THEN** plan, discovery, journal, and paid state are unavailable for that
  activation and neither supported builder is guessed

#### Scenario: Off remains certified
- **WHEN** a current slot has normalized hints Off
- **THEN** 2/2 produces the canonical policy-bound empty identity with zero
  facts/assignments/discovery and no delivery spoiler rows

#### Scenario: Same seed produces same hints
- **WHEN** the same accepted settings, seed, placement, spheres, overlays, and
  supported algorithm/policy are built twice
- **THEN** facts, source assignments, text, canonical bytes, and digest are
  byte-identical

#### Scenario: Placement iteration order does not select another deck
- **WHEN** semantically identical placement/candidate inputs are supplied in a
  different iteration order
- **THEN** canonical ordering produces the same maximal and retained plan and
  digest

#### Scenario: Full lifecycle reconstructs generation-time hints
- **WHEN** one current 2/2 plan is rebuilt through generation, activation,
  reveal, native export, warm replay, and cold replay
- **THEN** every path produces the same policy-bound plan bytes/digest before
  discovery is applied

#### Scenario: Unsupported plan cannot be silently upgraded
- **WHEN** persisted identity requests algorithm 1, a crossed pair, or another
  unsupported algorithm/text schema
- **THEN** rich hints, discovery, journal, and paid queues remain disabled for
  that activation and the current builder is not substituted

#### Scenario: Hints section present in spoiler
- **WHEN** a seed is generated with any enabled normalized hint policy
- **THEN** the full spoiler serializes its explicit policy-bound plan and
  additive typed metadata in canonical order

#### Scenario: Hints off skips generation
- **WHEN** a seed normalizes hints Off
- **THEN** the current certified plan has zero facts/assignments/discovery and
  the text spoiler omits delivery rows

### Requirement: Hint source NPCs

The randomizer SHALL retain the Hints v2 source identities: the 15 canonical
telepathic tiles, Storytellers, shared Fortune Light, and independent Fortune
Dark. Murahdahla SHALL remain a separate spoiler-only compatibility surface
with no delivery assignment or discovery bit.

Algorithm 2 SHALL build a maximal deck for the selected mix before applying
requested coverage and paid depth. It SHALL derive one domain-separated
seed-ranked permutation of the 15 canonical physical tiles and pair ordered
tile facts with ranked sources. It SHALL retain the first 0, 5, 10, or 15
pairs, making lower-coverage semantic/source pairs strict prefixes of higher
coverage for the same accepted input and mix.

Each paid source SHALL have a maximal compact queue of up to three distinct
exact facts. Requested depth SHALL retain positions `[0, depth)`. Fortune Light
SHALL remain shared by Kakariko and Lake Hylia; Fortune Dark SHALL remain
independent.

After pruning, the plan SHALL compact facts and assignments so every fact id is
contiguous and referenced and no assignment targets a removed fact. For a rich
pool, primary count SHALL be `tile_count + 3` when paid depth is nonzero (or
`tile_count` at depth zero), reserve count SHALL be
`3 * max(paid_depth - 1, 0)`, and total count SHALL remain at most 24. Scarce
pools SHALL report and surface actual underfill without duplicates or filler.

#### Scenario: Tile source coverage is nested
- **WHEN** the same seed/mix is built at tile coverage 5, 10, and 15
- **THEN** every fact/source pair in five is unchanged in ten and every pair in
  ten is unchanged in fifteen

#### Scenario: Paid queue depth is nested
- **WHEN** the same seed/mix is built at paid depth 1, 2, and 3
- **THEN** every retained service queue is a prefix of the next depth and no
  earlier fact moves to another service or position

#### Scenario: Rich custom topology reaches its exact capacity
- **WHEN** enough eligible facts exist for coverage 10 and paid depth 2
- **THEN** the plan has 13 primaries, three reserves, 16 facts, ten tile
  assignments, and two positions for each paid service

#### Scenario: Pruning leaves no orphan fact
- **WHEN** a maximal plan is reduced to any legal coverage/depth tuple
- **THEN** fact ids are contiguous, every fact is assigned exactly once, and
  every assignment references an active fact

#### Scenario: Scarce queue closes gaps before prefixing
- **WHEN** a maximal paid queue cannot populate an intermediate candidate
- **THEN** later eligible exact facts shift left before depth is applied and
  no empty position hides a later retained clue

#### Scenario: Shared and independent fortune identities survive policy
- **WHEN** Fortune Light advances under any nonzero depth
- **THEN** Kakariko/Lake share that progression and Fortune Dark remains
  unchanged

#### Scenario: Complete plan assigns all delivery surfaces
- **WHEN** an algorithm-2 Balanced plan has at least 24 distinct eligible facts
- **THEN** all 15 tiles have one primary, each paid source has a primary head
  plus two reserves, and no fact is assigned twice

#### Scenario: Scarce plan underfills honestly
- **WHEN** fewer eligible complete facts exist than the selected policy's
  source capacity
- **THEN** the plan leaves affected assignments empty instead of duplicating,
  fabricating, ambiguously rendering, or failing seed generation

#### Scenario: Kakariko and Lake Hylia share one queue
- **WHEN** a fact is purchased from Kakariko and the next paid interaction is
  at Lake Hylia
- **THEN** Lake Hylia selects the next undiscovered Fortune Light assignment,
  not a separate or repeated queue head

#### Scenario: Dark Fortune queue is independent
- **WHEN** Fortune Light advances
- **THEN** Fortune Dark retains its own first undiscovered assignment

#### Scenario: Triforce Hunt populates Murahdahla
- **WHEN** a seed has `goal ∈ {triforce-hunt, ganon-hunt}` and enabled hints
- **THEN** its full spoiler contains the Murahdahla region summary without
  changing policy, delivery counts, assignments, digest, or discovery

#### Scenario: Non-hunt goals omit Murahdahla
- **WHEN** a seed has another goal
- **THEN** no Murahdahla compatibility row is emitted

#### Scenario: Fork-extension NPCs surface their hints in-game
- **WHEN** a Storyteller or Fortune-Teller paid interaction under nonzero depth
  successfully prepares, renders, charges, and commits a queue fact
- **THEN** its carrier dialogue shows that exact latched fact, Lake Hylia shares
  Kakariko's Fortune Light progression, and no other source is consumed

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

### Requirement: Hint regions describe the shuffled world graph

Every region a hint names SHALL be the location's EFFECTIVE region under the
active seed — world-state predicate override first, then the active per-seed
entrance override — matching the region the placer and the runtime reachability
engine use. A hint SHALL NOT name a location's vanilla region when entrance
shuffle has moved it, because the resulting hint would be false.

This is a deliberate exception to the player-knowledge invariant in
`randomizer-player-knowledge`, and the two capabilities SHALL be read together.
That capability limits surfaces to statements true under EVERY assignment the
player's observations allow; a hint is instead an intentional disclosure whose
entire payload is seed-specific truth, so consuming the true entrance overlay
is correct here and only here. The consumption is pinned by
`assets/scripts/check_knowledge_consumers.py`, whose allowlist for
`Rando_GetEntranceRegionOverride` names the hint builder explicitly; a new
consumer of that getter is a build break until classified.

Hint text SHALL remain race-gated at display, so this disclosure never reaches
a race player before the reveal flow.

#### Scenario: Entrance shuffle moves a location's region

- **WHEN** an entrance-shuffle slot relocates a location and a hint names that
  location's region
- **THEN** the hint states the effective (shuffled) region, identical to the
  region the reachability engine and placer use for that location

#### Scenario: Entrance-override consumption stays enumerated

- **WHEN** a source file outside the audited allowlist calls
  `Rando_GetEntranceRegionOverride`
- **THEN** the knowledge-consumer guard fails the build rather than allowing a
  new surface to consume true topology silently

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

### Requirement: Configurable semantic composition

Algorithm 2 SHALL use only Hints v2 fact kinds, typed truth inputs, eligible
metadata, template ids, complete-fit rendering, and safe deterministic
backfill. It SHALL NOT emit required-item, Way-of-the-Hero, foolish/barren,
path-necessity, or another counterfactual claim.

Variety SHALL select the same full-coverage fact multiset, rendered text, and
paid queues as algorithm 1 for the same accepted input. It SHALL reorder only
the 15 tile facts through this literal band schedule, skipping exhausted bands
and retaining original canonical order within a band:

`I, D, W, U, F, I, D, W, U, I, D, W, U, I, W`

where `I` is high-priority/major exact, `D` is high-friction exact,
`W` is goal/setting or positive region value, `U` is other useful exact, and
`F` is truthful flavor.

Other mixes SHALL target the following 15 maximal tile facts:

- Important: 2 high-priority exact, 8 other major exact, 5 useful exact;
- Difficult: 9 high-friction exact, 2 high-priority exact, 2 other major exact,
  2 useful exact; and
- WorldInfo: 3 goal/active-setting, 6 positive region-value, 3 high-friction
  exact, 3 useful exact.

Shortages SHALL use the deterministic backfill orders pinned in the change
design. Paid facts SHALL always be exact, distinct, and selected in canonical
queue-position/service round-robin order. New semantic fact kinds and new rich
templates SHALL be ineligible in this change.

#### Scenario: Full Variety preserves algorithm-1 content
- **WHEN** a rich seed is built with algorithm 1 and with algorithm-2 Balanced
- **THEN** selected fact semantics, rendered text, and each paid queue match,
  while physical tile assignments may differ only by the documented
  prefix-balanced ranking

#### Scenario: Sparse Variety spans available bands
- **WHEN** all five Variety bands are populated and coverage is five
- **THEN** its retained tile prefix contains one fact from each band in the
  pinned order

#### Scenario: Important contains only exact placement information
- **WHEN** Important has enough eligible candidates
- **THEN** every tile and paid fact is exact high-priority, major, or useful
  placement information

#### Scenario: Difficult favors authored high-friction checks
- **WHEN** at least nine distinct high-friction exact candidates exist
- **THEN** the maximal tile selection contains nine such facts before its
  pinned exact quotas/backfill

#### Scenario: WorldInfo remains literal
- **WHEN** WorldInfo emits goal, setting, or region facts
- **THEN** each follows directly from accepted settings or positive counted
  placements and makes no required/barren/path claim

#### Scenario: Mix change may change the deck
- **WHEN** only mix changes for an otherwise identical accepted seed
- **THEN** the selected semantic deck may change deterministically while
  placement and sphere digests remain unchanged

### Requirement: Paid-depth-zero truthful service

At paid depth zero, each Storyteller and Fortune Teller SHALL render complete
locale-correct pre-choice framing that states no clue is available, identifies
the actual price that acceptance will charge, and states that acceptance still
provides the existing healing service. The framing SHALL preserve all existing
choice/control commands and SHALL be available in original/US, German, and
French.

Decline and insufficient funds SHALL charge nothing, heal nothing, latch no
fact, and discover nothing. Successful acceptance SHALL perform exactly the
existing one deduction/receipt/healing choreography, create no fact
presentation, and commit no discovery. Repeated text loads, save/reload,
snapshot replay, or slot switch SHALL not fabricate a queue.

A build that cannot completely and truthfully encode the pre-choice flow for
every owned service and required locale SHALL fail source validation and SHALL
NOT expose paid depth zero as selectable.

#### Scenario: Zero-depth choice is informed before payment
- **WHEN** the player opens a paid service at depth zero
- **THEN** the no-clue, actual-price, and healing facts are visible before the
  accept/decline decision

#### Scenario: Zero-depth acceptance charges only for healing
- **WHEN** an adequately funded player accepts the disclosed service
- **THEN** the service deducts exactly once, heals normally, and changes no
  hint fact, assignment, discovery bit, or paid cursor

#### Scenario: Zero-depth decline is non-mutating
- **WHEN** the player declines
- **THEN** rupees, health, discovery, and all paid source state are unchanged

#### Scenario: Zero-depth insufficient funds is non-mutating
- **WHEN** the player accepts without the disclosed price
- **THEN** the existing insufficient-funds path occurs without charge, healing,
  discovery, or queue state

#### Scenario: Every required locale is truthful
- **WHEN** each zero-depth paid surface is encoded in original/US, German, and
  French
- **THEN** complete grammar-correct text and control commands fit and no text
  implies that a clue exists or was purchased

### Requirement: Exact pre-commit NPC reward previews

When `hint_npc_reward_reveal` is enabled, an active Original/US randomizer slot
SHALL identify the exact randomized reward before the player pays, accepts,
trades, or irreversibly chooses it at every owned person-mediated source.

The owned paid sources SHALL be Bottle Merchant, King Zora, Blacksmith, Chest
Game, and Digging Game. The owned free/trade sources SHALL be Sahasrahla,
Potion Shop trade, Magic Bat, Hobo, Old Man, Catfish, Purple Chest, Stumpy, and
the next Fairy gift. Shopsanity clerks SHALL summarize their active unchecked
items and prices. Take-Any clerks SHALL summarize their currently live
irreversible choices.

The preview SHALL resolve from the exact active placement and exact runtime
source context on every use. An audited source-exclusive message ID MAY serve
as that context; a message ID shared by multiple sources SHALL additionally
require its actor, room, entrance, shop, or Take-Any discriminator. A
missing/invalid placement, checked source, stale slot, wrong discriminator,
invalid item, unsupported locale, or incomplete text fit SHALL preserve the
complete existing truthful generic/vanilla dialogue. The implementation SHALL
NOT use a vanilla-placement fallback to fabricate a reward.

Existing exact post-acceptance randomized-reward rewrites SHALL remain
unconditional. The setting SHALL NOT consume or discover a semantic clue,
advance a paid clue queue, enter the journal, or affect the clue-plan digest.

#### Scenario: Paid NPC states item and price before choice
- **WHEN** an unchecked owned paid source offers a randomized item with previews
  enabled
- **THEN** its complete pre-choice dialogue names that exact item and its actual
  price before any rupees are charged

#### Scenario: King Zora does not repeat the item
- **WHEN** the player advances from King Zora's exact-item offer to the
  500-rupee Pay/Quit confirmation
- **THEN** the first prompt names the item once and the second prompt shows only
  the price and choices

#### Scenario: Free or trade source identifies its next grant
- **WHEN** an owned free/trade source is about to grant its unchecked
  randomized reward with previews enabled
- **THEN** the dialogue names that exact reward before the existing grant
  boundary without adding a new decline branch

#### Scenario: Stumpy preserves both facts and its choice
- **WHEN** Stumpy's Flute-location fact occupies one, two, or three rows and
  reward previews are enabled
- **THEN** the reward page, complete Flute fact page, and cursor-safe Yes/No
  page all render in order with the original choice command intact

#### Scenario: Shopsanity clerk reports only live checks
- **WHEN** the player talks to a correctly context-matched shopsanity clerk
- **THEN** the clerk lists each unchecked slot's exact randomized item and
  seed-derived price and omits already-checked vanilla restocks

#### Scenario: Take-Any clerk reports live irreversible choices
- **WHEN** the player talks to an active Retro Take-Any clerk before choosing
- **THEN** the clerk lists only that cave's currently available take-once
  choices and omits inactive, already-taken, and repeatable-key entries

#### Scenario: Context mismatch fails closed
- **WHEN** a shared dialogue ID is decoded in the wrong actor, room, entrance,
  shop, or Take-Any context
- **THEN** no placement from another source is named and the prior complete
  dialogue remains unchanged

#### Scenario: Checked replay does not promise an old reward
- **WHEN** a one-time source is already checked and its replay behavior is
  consolation, restock, or no reward
- **THEN** its old randomized placement is not advertised

#### Scenario: Preview setting does not gate post-acceptance truth
- **WHEN** previews are disabled and an existing post-acceptance exact reward
  message is shown
- **THEN** the message still identifies the randomized grant instead of
  restoring a false vanilla item claim

### Requirement: Reward-preview text and transaction safety

NPC reward previews SHALL preserve the complete useful identity of every
eligible item and every price. Text SHALL be composed into bounded scratch
storage and installed only after the full message and all control commands fit.
Multi-page choice flows SHALL retain their `WaitKey`, `Scroll`, row-selection,
`Choose`, and termination commands. Low-two-row choices SHALL use the
message-engine's canonical selected and unselected prefixes so its messages
1/2 cursor overlays never erase or redraw option-label glyphs.

Loading or viewing a preview SHALL be read-only. Decline and insufficient-funds
paths SHALL change no rupees, inventory, checked bits, grant/receipt state,
source state, hint discovery, or paid clue queue. Existing acceptance paths
SHALL retain exactly-once charge/grant/check behavior. Retryable grant failure
SHALL not charge or check early and SHALL re-present the same current reward.

Rich reward dialogue SHALL be Original/US only. German and French message
buffers SHALL remain byte-identical, and both seed-generation UIs SHALL disclose
that limitation.

#### Scenario: Long exact identity uses complete pages
- **WHEN** an owned source contains the longest supported qualified item name
- **THEN** the installed dialogue includes the full unambiguous name and price
  without clipping, partial replacement, retained prior-page pixels, or lost
  choice commands

#### Scenario: Moving the choice cursor preserves both labels
- **WHEN** the player presses Up or Down on a generated paid or acceptance
  choice
- **THEN** the vanilla cursor overlay changes only the canonical row prefix and
  both option labels remain complete and visually unchanged

#### Scenario: Render failure preserves prior dialogue
- **WHEN** an item name/glyph is invalid or the complete message cannot fit
- **THEN** no part of the live dialogue buffer is replaced

#### Scenario: Viewing and declining are inert
- **WHEN** a player opens a preview and declines
- **THEN** rupees, inventory, checked state, source state, grant state, and all
  semantic hint state remain unchanged

#### Scenario: Retryable grant failure does not commit
- **WHEN** acceptance reaches a retryable bottle-capacity or receipt-allocation
  failure
- **THEN** the source remains unchecked, no charge/grant is duplicated, and the
  same active reward is previewed on retry

#### Scenario: Non-US locale remains intact
- **WHEN** German or French dialogue reaches an owned reward source
- **THEN** its message buffer remains byte-identical and no Original/US item
  name or control command is injected
