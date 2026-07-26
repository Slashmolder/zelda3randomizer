## ADDED Requirements

### Requirement: Configurable hint policy preserves v14 and type-11 layouts

Configurable hints SHALL NOT grow randomizer sidecar format 14, its 278-byte
slot extension, the 24-bit discovery domain, snapshot hint TLV type 11, or its
41-byte payload-format-1 layout. The existing persisted canonical settings
SHALL be the only persisted policy source. No profile label, policy duplicate,
fact, assignment, rendered text, queue cursor, resolved marker, or in-flight
presentation SHALL be appended.

Current generation SHALL write algorithm 2/text schema 2 plus the corresponding
policy-bound digest and zero discovery. Every normal activation, save,
export, warm replay, and fresh-process cold replay SHALL dispatch current 2/2
identity from the persisted fields. The exact algorithm-1/text-schema-1 builder
is retained for the separately authenticated generator-156 race-reveal
artifact; no 1/1 sidecar or snapshot was released.

A crossed, zero, unknown,
settings-policy-inconsistent, malformed, or digest-mismatched record SHALL
disable only hints non-destructively. It SHALL not rewrite settings/identity,
upgrade the pair, try the other builder, alter placement/gameplay state, or
apply partial discovery.

For 2/2, activation and replay SHALL install accepted canonical settings and
all effective overlays before rebuilding policy/facts/assignments and comparing
the digest.

#### Scenario: Sidecar layout does not grow
- **WHEN** a custom 2/2 slot is written
- **THEN** sidecar remains format 14 with the same 278-byte extension and exact
  algorithm/text/digest/discovery offsets

#### Scenario: Snapshot layout does not grow
- **WHEN** a custom 2/2 snapshot is saved
- **THEN** type 11 remains payload format 1 and exactly 41 bytes with no
  duplicate policy field

#### Scenario: Policy reconstructs from canonical settings
- **WHEN** a 2/2 sidecar or cold snapshot is activated
- **THEN** its accepted settings recover the normalized policy, the rebuilt
  policy-bound digest matches, and only then is discovery installed

#### Scenario: Legacy builder is not a persistence format
- **WHEN** a v14 sidecar or type-11 snapshot record names pair 1/1
- **THEN** hints fail closed without importing, rebuilding, exporting, or
  perpetuating that identity; the direct 1/1 builder remains available only to
  the authenticated generator-156 race-reveal path

#### Scenario: Crossed pair is not guessed
- **WHEN** persisted identity names algorithm 1/schema 2 or algorithm 2/schema 1
- **THEN** the otherwise playable slot/replay has hints unavailable and neither
  supported builder runs as a fallback

#### Scenario: Policy mismatch disables only hints
- **WHEN** accepted canonical policy cannot reproduce a stored 2/2 digest
- **THEN** plan, discovery, journal, and paid state clear for that activation
  while the sidecar/snapshot and gameplay state remain untouched
