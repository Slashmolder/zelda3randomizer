## MODIFIED Requirements

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

## ADDED Requirements

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
