## ADDED Requirements

### Requirement: Hint-plan identity and discovery persistence

Current randomizer sidecar writes SHALL use file format version 14. Version 14
SHALL grow the append-only slot extension from 238 to 278 bytes and append the
following exact fields after every version-13 field:

| Extension offset | Width | Field | Encoding |
|---:|---:|---|---|
| 238 | 2 | `hint_algorithm_version` | little-endian `u16` |
| 240 | 2 | `hint_text_schema_version` | little-endian `u16` |
| 242 | 32 | `hint_plan_digest` | SHA-256 bytes |
| 274 | 4 | `hint_discovered_bits` | little-endian `u32` |

The plan digest SHALL be the canonical semantic digest defined by
`randomizer-hints`, covering all ordered facts, primary/reserve assignments,
and template ids but excluding discovery. Only bits 0..23 of
`hint_discovered_bits` are defined; current writers SHALL zero bits 24..31 and
readers SHALL mask them before applying discovery.

Every newly generated slot SHALL write the current supported algorithm and text
versions, its generation-time plan digest, and zero discovery. Hints Off SHALL
write the current versions, canonical empty-plan digest, and zero discovery.
A normal game save SHALL retain the certified versions/digest and copy the live
discovery bits. The sidecar SHALL NOT persist rendered text, facts, assignments,
paid queue cursors, resolved markers, or in-flight presentation state.

The normal paired-save path SHALL follow the existing sidecar-first
atomic-commit contract. It SHALL stage the new SRAM slot bytes, durably replace
the sidecar carrying current discovery and the checksum of those staged bytes,
and only then write vanilla SRAM. If the sidecar update fails, the paired SRAM
write SHALL not proceed. An interruption between durable replacements SHALL be
reported by the existing checksum/drift mechanism rather than silently loading
new spent-rupee SRAM with stale discovery.

Activation SHALL install accepted settings, placement, spheres/derived
assignments, and all applicable shuffle/layout overlays before invoking the
recorded versioned hint builder. It SHALL compare the rebuilt SHA-256 digest to
the stored digest before applying discovery. A pre-v14 slot, missing/malformed
metadata, unsupported algorithm/text pair, build failure, or digest mismatch
SHALL leave the slot playable but disable the hint plan, journal, discovery,
and paid queue state for that activation. It SHALL NOT modify the sidecar,
reroll with the current algorithm, or invalidate otherwise accepted placement
and gameplay state.

Future binaries MAY retain explicitly supported older builders. Once an
algorithm/text pair is listed as supported, reconstruction for that pair SHALL
remain byte-identical. An unknown pair SHALL always fail the hint subsystem
closed.

#### Scenario: New Balanced slot round-trips plan identity
- **WHEN** a current Balanced slot is generated, saved, and activated
- **THEN** version 14's exact fields round-trip, the requested builder
  reproduces the stored digest, and zero initial discovery is installed

#### Scenario: Discovery persists without changing the digest
- **WHEN** several delivery facts are discovered and the game is saved/reloaded
- **THEN** those 24-bit discovery values round-trip while algorithm version,
  text-schema version, and plan digest remain unchanged

#### Scenario: Off stores the certified empty plan
- **WHEN** a current hints-Off slot is written
- **THEN** it carries current versions, the canonical empty-plan digest, zero
  discovery, and no delivery facts are activated on load

#### Scenario: Reserved discovery bits are not gameplay state
- **WHEN** a v14 slot is serialized after any valid discovery combination
- **THEN** bits 24..31 are zero; if a reader encounters them nonzero it masks
  them without discovering a fact or moving a paid queue

#### Scenario: Legacy slot does not silently gain a new deck
- **WHEN** a valid pre-v14 randomizer slot is loaded
- **THEN** placement/gameplay activation continues under existing compatibility
  rules but Hints v2 remains unavailable and owned surfaces use neutral text

#### Scenario: Unsupported version disables only hints
- **WHEN** a v14 slot names an algorithm or text-schema pair the binary does not
  support
- **THEN** the slot remains playable, the file is untouched, and plan,
  discovery, journal, and paid state are cleared for that activation

#### Scenario: Digest mismatch cannot reroll an existing slot
- **WHEN** the recorded versioned builder succeeds but its digest differs from
  `hint_plan_digest`
- **THEN** the hint subsystem disables non-destructively and never retries with
  another/current algorithm

#### Scenario: Plan is rebuilt after accepted runtime assignments
- **WHEN** an active slot uses any assignment/layout state consumed by the
  supported hint algorithm
- **THEN** the digest comparison occurs only after that state is accepted and
  installed, so generation and activation see identical inputs

#### Scenario: Paid discovery and rupees use paired save ordering
- **WHEN** a committed paid hint is included in a normal game save
- **THEN** the sidecar carrying its discovery and the checksum of the staged
  SRAM is durably replaced before vanilla SRAM is written

#### Scenario: Sidecar failure cannot persist a repeatable paid clue
- **WHEN** the sidecar update fails before a normal save's SRAM write
- **THEN** the paired SRAM write is suppressed, and an interruption between
  replacements is detected as drift rather than silently loading spent rupees
  with stale discovery

### Requirement: Snapshot hint identity and discovery TLV

Randomizer snapshots SHALL append
`kRandoSnapshotTail_Type_HintState = 11` when current hint identity context is
available. Its payload SHALL be exactly 41 bytes in payload format 1:

| Payload offset | Width | Field | Encoding |
|---:|---:|---|---|
| 0 | 1 | `format` | `1` |
| 1 | 2 | `hint_algorithm_version` | little-endian `u16` |
| 3 | 2 | `hint_text_schema_version` | little-endian `u16` |
| 5 | 32 | `hint_plan_digest` | SHA-256 bytes |
| 37 | 4 | `hint_discovered_bits` | little-endian `u32` |

Type 11 SHALL be scoped to an accepted type-1 randomizer-state TLV from the same
snapshot load. The loader SHALL hold the identity/discovery pending while
settings and layout TLVs are processed and SHALL rebuild/validate the plan only
at finish-load after `Rando_ReinstallActiveSlotLogicOverlays` (or the single
equivalent authoritative reinstall seam) has completed.

A missing, malformed, duplicate, orphaned, unsupported, or digest-mismatched
type-11 record SHALL disable only hints for the replay. It SHALL NOT install
partial discovery, use a stale process-global plan, fall back to the current
builder, or reject an otherwise valid randomizer snapshot. A later normal
sidecar slot load MAY activate its independently certified v14 hint identity.

Snapshot save/load SHALL persist no derived plan text, paid cursor, resolved
marker, or uncommitted paid transaction. Queue position derives from validated
assignments plus discovery after replay.

#### Scenario: Warm replay reproduces plan and discovery
- **WHEN** a snapshot with type 11 is loaded while its slot is already active
- **THEN** the plan is rebuilt after the snapshot's accepted runtime state,
  its digest verifies, and the snapshot's discovery replaces the live discovery

#### Scenario: Fresh-process cold replay reproduces the same deck
- **WHEN** a type-1/settings/layout/type-11 snapshot is loaded without a
  pre-existing active slot
- **THEN** the versioned plan, assignments, rendered text, digest, and discovery
  match the snapshot-creation state after all overlays reinstall

#### Scenario: Hint TLV cannot bind to another randomizer state
- **WHEN** type 11 appears before any accepted type-1 state or after that state
  has failed
- **THEN** it is treated as orphaned and no hint or discovery state is installed

#### Scenario: Duplicate or malformed type 11 fails hints closed
- **WHEN** a snapshot has two type-11 records, an unknown payload format, or a
  length other than 41
- **THEN** no type-11 payload is partially applied and the otherwise valid
  snapshot continues with hints unavailable

#### Scenario: Old snapshot does not inherit process-global hints
- **WHEN** a randomizer snapshot lacks type 11
- **THEN** replay clears any prior plan/discovery/paid latch and uses neutral
  owned-surface text until a certified normal slot activation occurs

#### Scenario: Checked bitmap and discovery remain independent
- **WHEN** replay restores both checked-location and hint-discovery state
- **THEN** discovery bits come only from type 11 while journal resolved markers
  derive from the restored checked bitmap
