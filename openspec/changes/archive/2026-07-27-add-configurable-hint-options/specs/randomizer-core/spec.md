## MODIFIED Requirements

### Requirement: Hints settings axis

The settings struct SHALL retain the binary `hints` axis at canonical byte 22
with stable numeric values `Off=0` and enabled/Balanced=`1`. It SHALL add typed
enabled-policy fields for telepathic-tile coverage, paid clues per service, and
hint mix. The normalized domains SHALL be:

- tile coverage: `0`, `5`, `10`, or `15`;
- paid depth: `0`, `1`, `2`, or `3`; and
- mix: `Variety`, `Important`, `Difficult`, or `WorldInfo`.

The unconditional default and numeric value 1 SHALL mean the exact Balanced
tuple `(15, 3, Variety)`. Existing `on`, `1`, `true`, `sahasrahla`, and `full`
aliases SHALL continue to resolve to that tuple.

The player-facing named profiles SHALL be:

| Profile | `hints` | Tiles | Paid | Mix |
|---|---:|---:|---:|---|
| Off | 0 | 0 | 0 | not applicable |
| Sparse | 1 | 5 | 1 | Variety |
| Balanced | 1 | 15 | 3 | Variety |
| Direct | 1 | 15 | 3 | Important |

Custom SHALL be derived whenever an enabled normalized tuple matches no enabled
named profile. It SHALL NOT have a serialized enum value or standalone CLI
value. An enabled tuple with zero tiles and zero paid depth SHALL normalize to
Off so the empty system has one canonical representation.

The policy SHALL occupy these exact formerly-required-zero bits:

| Field | Canonical bits | Encoding |
|---|---|---|
| Tiles | `[28]` bits 5..6 | `00=15`, `01=0`, `10=5`, `11=10` |
| Mix | `[28]` bit 7 low, `[29]` bit 7 high | `00=Variety`, `01=Important`, `10=Difficult`, `11=WorldInfo` |
| Paid | `[30]` bits 6..7 | `00=3`, `01=0`, `10=1`, `11=2` |

Off serialization SHALL clear all six policy bits independent of construction
or CSV key order. Canonical settings SHALL remain 31 bytes and share-string v2
SHALL remain exactly 76 characters. Existing Off and Balanced blobs, settings
hashes, and share strings SHALL remain byte-identical. Every effective
non-default enabled tuple SHALL affect the settings hash. A pre-change decoder
SHALL refuse a nonzero policy bit through its existing undefined-bit checks
rather than silently substitute Balanced.

CLI/settings text SHALL accept named values `off`, `sparse`, `balanced`, and
`direct` plus component keys `hint_tiles`, `hint_paid`, and `hint_mix`.
Profile selection SHALL provide a base tuple; explicit component keys SHALL
override that base after parsing independent of key order. Duplicate keys,
unknown values, and standalone `custom` SHALL be rejected. `hints=off` SHALL
normalize and clear component fields.

Ordinary presets SHALL preserve the complete hint policy. An explicit
Race-safe utility preset MAY set Balanced only when its UI/help discloses the
reset. Hint policy SHALL remain post-placement and SHALL NOT affect placement
or sphere digests.

#### Scenario: Legacy Balanced remains byte-identical
- **WHEN** a pre-change value-1 settings blob or share string with all policy
  bits zero is decoded and re-encoded
- **THEN** it displays Balanced `(15,3,Variety)` and every canonical byte,
  settings-hash byte, and share-string character is unchanged

#### Scenario: Legacy Off remains byte-identical
- **WHEN** hints Off is serialized after stale non-default component fields
  were present in memory
- **THEN** byte 22 is zero, all six policy bits are zero, and the result equals
  the pre-change Off canonical settings

#### Scenario: Sparse has a distinct settings identity
- **WHEN** otherwise identical settings select Sparse
- **THEN** they encode enabled, 5 tiles, paid depth 1, Variety, and have a
  different settings hash while retaining canonical length 31

#### Scenario: Custom is derived rather than serialized
- **WHEN** an enabled player changes Balanced tile coverage to 10
- **THEN** the UI reports Custom while canonical state contains only the
  enabled bit and three normalized policy fields

#### Scenario: Empty custom tuple collapses to Off
- **WHEN** enabled policy is constructed with tile coverage 0 and paid depth 0
- **THEN** normalization produces the one canonical Off encoding regardless of
  mix

#### Scenario: Profile and component parsing is order-independent
- **WHEN** `hints=sparse` and `hint_mix=difficult` appear in either order
- **THEN** the normalized result is enabled `(5,1,Difficult)` and displays
  Custom

#### Scenario: Legacy aliases still mean Balanced
- **WHEN** `hints=on`, `true`, `1`, `sahasrahla`, or `full` is parsed without
  component overrides
- **THEN** the exact Balanced tuple is selected and no hidden profile exists

#### Scenario: Old reader cannot drop custom policy
- **WHEN** a pre-change binary receives a current 31-byte canonical payload
  containing any nonzero policy bit
- **THEN** its strict undefined-bit validation refuses the payload instead of
  generating a Balanced seed

#### Scenario: Policy remains post-placement
- **WHEN** otherwise identical seeds differ only by a valid hint profile or
  component
- **THEN** settings/hint identity may differ but placement and sphere digests
  are identical

#### Scenario: hints axis participates in settings_hash
- **WHEN** the same seed is generated with `hints=off` and then with
  `hints=balanced`
- **THEN** the resulting settings hashes differ exactly as the former
  off/on values did

#### Scenario: hints default is Balanced
- **WHEN** a seed is generated without an explicit `hints=` override
- **THEN** the resolved setting is numeric value 1 and the Balanced
  `(15,3,Variety)` plan builder runs

#### Scenario: legacy aliases collapse to Balanced
- **WHEN** a seed is generated with `hints=on`, `true`, `1`, `sahasrahla`, or
  `full`
- **THEN** the resolved setting is the same numeric Balanced tuple and no
  distinct preset or algorithm is selected

#### Scenario: hints default is on
- **WHEN** a seed is generated without an explicit `hints=` override
- **THEN** the resolved stored value remains numeric value 1 (the legacy
  `on` value), displays Balanced, and runs the Balanced tuple

#### Scenario: tri-state aliases collapse to on
- **WHEN** a seed is generated with `hints=sahasrahla` or `hints=full`
- **THEN** the resolved stored value remains numeric value 1 (the legacy
  `on` value), displays Balanced, and selects no distinct mode

#### Scenario: player-facing rename is serialization-neutral
- **WHEN** a pre-change value-1 canonical settings blob or share string is
  decoded by the configurable-hints build
- **THEN** it displays Balanced and re-encodes byte-identically

#### Scenario: hints remain post-placement
- **WHEN** otherwise identical seeds differ only between Off and any enabled
  normalized hint policy
- **THEN** placement and sphere digests are identical even though settings,
  hint-plan, full-spoiler, and race-stamp identity may differ

### Requirement: Hints spoiler section

The full JSON spoiler SHALL retain its top-level `hints` array and plan-level
hint identity object. Every delivery row SHALL preserve `npc`, `dialogue_id`,
and `text` and the Hints v2 typed fact/template/target/source/queue metadata.

The plan-level object SHALL carry algorithm version, text-schema version,
lowercase hexadecimal SHA-256 digest, actual primary/reserve/fact counts, and
the normalized generation policy:

- `profile`: `off`, `sparse`, `balanced`, `direct`, or `custom`;
- `tile_count`: `0`, `5`, `10`, or `15`;
- `paid_depth`: `0`, `1`, `2`, or `3`; and
- `mix`: the pinned lowercase policy name or the pinned not-applicable value
  for Off.

Policy metadata SHALL report requested normalized policy even when fact
scarcity underfills delivery. Rows SHALL be emitted in canonical compact
fact/assignment order. Consumers SHALL NOT assume 18 primaries, six reserves,
15 populated tile sources, or queue depth three. Every reported count SHALL be
the actual retained topology.

Murahdahla SHALL remain a separate spoiler-only compatibility row for
`goal ∈ {triforce-hunt, ganon-hunt}` and SHALL remain outside plan counts,
digest, assignments, and discovery. `meta.hints_count` SHALL equal the actual
JSON row count including Murahdahla when present.

The text spoiler SHALL mirror policy, identity, counts, and rows under its
Hints heading outside suppressed race output. Discovery, checked/resolved
state, paid cursor/latch state, pending UI state, and presentation state SHALL
never enter JSON/text spoiler content or the canonical race stamp.

#### Scenario: Balanced spoiler retains compatible rows
- **WHEN** a full Balanced seed is generated under algorithm 2
- **THEN** every delivery row retains its legacy fields, policy reports
  `(balanced,15,3,variety)`, and actual topology is serialized canonically

#### Scenario: Sparse spoiler reports variable topology
- **WHEN** a rich Sparse seed is generated
- **THEN** policy reports five tiles and depth one, primary count is eight,
  reserve count is zero, and no absent assignment is emitted as a row

#### Scenario: Paid-only custom topology is honest
- **WHEN** coverage is zero and paid depth is three
- **THEN** the spoiler contains up to three paid heads and six paid reserves,
  no tile assignment, and actual underfill counts

#### Scenario: Off spoiler is one canonical empty plan
- **WHEN** tile and paid controls normalize to Off
- **THEN** policy reports Off, delivery counts are zero, no delivery row is
  emitted, and the current certified empty-plan identity is present

#### Scenario: Discovery cannot change configurable spoiler identity
- **WHEN** facts are discovered/resolved and the spoiler is regenerated
- **THEN** policy, facts, assignments, digest, text, and race stamp remain
  generation-time canonical values

#### Scenario: Hints array deterministic across runs
- **WHEN** the same accepted seed and normalized policy are generated twice
- **THEN** plan identity, every legacy field, typed metadata, row order, and
  rendered text are byte-identical

#### Scenario: Legacy spoiler fields remain available
- **WHEN** existing tooling reads any configurable delivery row
- **THEN** it can still read `npc`, `dialogue_id`, and `text` without depending
  on the policy or typed fields

#### Scenario: Complete paid queue is represented
- **WHEN** a Balanced seed has all 24 eligible delivery facts
- **THEN** the plan reports 18 primaries and six reserves, and each of the three
  paid sources has queue positions 0, 1, and 2

#### Scenario: Safe underfill reports actual counts
- **WHEN** fewer distinct useful and completely renderable facts exist than the
  selected policy's capacity
- **THEN** the spoiler reports smaller actual primary/reserve counts and
  contains no duplicate, fabricated, ambiguous, or filler row

#### Scenario: Triforce Hunt surfaces a Murahdahla summary
- **WHEN** a Triforce-Hunt or Ganon-Hunt seed is generated with enabled hints
- **THEN** `hints` contains one Murahdahla compatibility summary in addition to
  the delivery plan, while plan policy, counts, and digest exclude it

#### Scenario: hints off omits the section
- **WHEN** a seed normalizes hints Off
- **THEN** its `hints` array is empty and the text Hints section omits every
  delivery row while retaining only the current certified empty-plan identity
  and zero counts

#### Scenario: Discovery cannot change race verification
- **WHEN** the player discovers or resolves facts and later performs a valid
  race reveal
- **THEN** the regenerated canonical spoiler/stamp is identical to generation
  because discovery and checked state are excluded
