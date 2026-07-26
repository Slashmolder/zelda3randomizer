## MODIFIED Requirements

### Requirement: Hints settings axis

The settings struct SHALL include a binary `hints` axis occupying canonical
serialization byte 22 with stable numeric values `Off=0` and `Balanced=1`.
The player-facing name for value 1 SHALL be **Balanced**. `on`, `1`, `true`,
`sahasrahla`, and `full` SHALL remain accepted aliases for Balanced and SHALL
NOT select another algorithm or mode. The axis SHALL continue to participate
in the settings hash and its unconditional default SHALL remain value 1.

- `Off`: build the canonical empty hint plan, emit no delivery facts, and use
  truthful neutral randomizer text on recognized hint-owned surfaces.
- `Balanced`: build the versioned semantic plan defined by
  `randomizer-hints`; surface its assigned facts in-game and in the full
  spoiler.

The rename from `on` to Balanced SHALL NOT change byte 22, the settings hash for
either numeric value, the share-string encoding, placement generation, or
sphere generation. An internal `kHintsMode_On` alias MAY remain during source
migration but SHALL equal Balanced rather than create a third mode.

#### Scenario: hints axis participates in settings_hash
- **WHEN** the same seed is generated with `hints=off` and then with
  `hints=balanced`
- **THEN** the resulting settings hashes differ exactly as the former
  off/on values did

#### Scenario: hints default is Balanced
- **WHEN** a seed is generated without an explicit `hints=` override
- **THEN** the resolved setting is numeric value 1 and the Balanced semantic
  plan builder runs

#### Scenario: legacy aliases collapse to Balanced
- **WHEN** a seed is generated with `hints=on`, `true`, `1`, `sahasrahla`, or
  `full`
- **THEN** the resolved setting is the same numeric Balanced value and no
  distinct preset or algorithm is selected

#### Scenario: hints default is on
- **WHEN** a seed is generated without an explicit `hints=` override
- **THEN** the resolved stored value remains numeric value 1 (the legacy
  `on` value), is displayed as Balanced, and runs the Balanced semantic plan
  builder

#### Scenario: tri-state aliases collapse to on
- **WHEN** a seed is generated with `hints=sahasrahla` or `hints=full`
- **THEN** the resolved stored value remains numeric value 1 (the legacy
  `on` value), is displayed as Balanced, and selects no distinct mode

#### Scenario: player-facing rename is serialization-neutral
- **WHEN** a pre-change value-1 canonical settings blob or share string is
  decoded by the Hints v2 build
- **THEN** it is displayed as Balanced and re-encodes byte-identically

#### Scenario: hints remain post-placement
- **WHEN** otherwise identical seeds differ only between Off and Balanced
- **THEN** placement and sphere digests are identical even though settings,
  full spoiler, and race stamp identity may differ

### Requirement: Hints spoiler section

The full JSON spoiler SHALL retain a top-level `hints` array and SHALL add a
plan-level hint identity object. Every delivery-fact row SHALL preserve the
existing backward-compatible fields:

- `npc` (string);
- `dialogue_id` (integer label, not a runtime dialogue-table key); and
- `text` (string).

Each delivery row SHALL additionally carry stable typed metadata identifying
its fact id, fact kind/precision, template id, source assignment, paid queue
position or tile assignment, and any typed item/location/region targets
applicable to that fact. Reserve rows SHALL reuse their paid source's stable
`npc`/dialogue label and SHALL be distinguished by fact id and queue position.

The plan-level object SHALL carry the hint algorithm version, text-schema
version, lowercase hexadecimal SHA-256 plan digest, primary count, and reserve
count. Delivery rows SHALL be emitted in canonical fact/assignment order.
Balanced SHALL target up to 18 primary and six reserve rows; Off SHALL emit an
empty delivery plan with its canonical empty-plan identity.

Murahdahla SHALL remain a separate spoiler-only compatibility row for
`goal ∈ {triforce-hunt, ganon-hunt}`. It SHALL retain its legacy
`npc`, `dialogue_id`, and `text` fields but SHALL NOT count toward the 18
primary facts, six reserves, plan digest, or discovery bits.

The text spoiler SHALL mirror the full plan under a `Hints:` heading outside
suppressed race output. `meta.hints_count` SHALL equal the number of rows in the
JSON `hints` array, including Murahdahla when present. Discovery bits,
checked/resolved state, paid cursor state, and presentation latches SHALL never
enter JSON/text spoiler content or the canonical race stamp.

#### Scenario: Hints array deterministic across runs
- **WHEN** the same accepted seed is generated twice with Balanced hints
- **THEN** plan identity, every legacy field, typed metadata, row order, and
  rendered text are byte-identical

#### Scenario: Legacy spoiler fields remain available
- **WHEN** existing tooling reads any Hints v2 delivery row
- **THEN** it can still read `npc`, `dialogue_id`, and `text` without depending
  on the new typed fields

#### Scenario: Complete paid queue is represented
- **WHEN** a Balanced seed has all 24 eligible delivery facts
- **THEN** the plan reports 18 primaries and six reserves, and each of the three
  paid sources has queue positions 0, 1, and 2

#### Scenario: Safe underfill reports actual counts
- **WHEN** fewer than 24 distinct useful and completely renderable facts exist
- **THEN** the spoiler reports the smaller actual primary/reserve counts and
  contains no duplicate, fabricated, ambiguous, or filler row

#### Scenario: Triforce Hunt surfaces a Murahdahla summary
- **WHEN** a Triforce-Hunt or Ganon-Hunt seed is generated with Balanced hints
- **THEN** `hints` contains the Murahdahla compatibility summary in addition to
  the delivery plan, while the plan counts and digest exclude it

#### Scenario: hints off omits the section
- **WHEN** a seed is generated with hints Off
- **THEN** its `hints` array is empty and the text Hints section omits every
  delivery row while retaining only the certified plan identity and zero fact
  counts; the plan-level object carries current versions, zero counts, and the
  canonical empty-plan digest

#### Scenario: Discovery cannot change race verification
- **WHEN** the player discovers or resolves facts and later performs a valid
  race reveal
- **THEN** the regenerated canonical spoiler/stamp is identical to generation
  because discovery and checked state are excluded
