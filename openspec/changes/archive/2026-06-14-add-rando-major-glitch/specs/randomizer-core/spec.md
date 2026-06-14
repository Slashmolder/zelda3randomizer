## ADDED Requirements

### Requirement: Settings canonical serialization order — Phase D major-glitch un-pin

In addition to the byte order pinned at the post-archive `randomizer-core / Settings canonical serialization order (normative)` baseline (and any Phase B extensions, in particular `add-rando-trick-logic-and-axes`'s un-pinning of values 1-2 for the `logic` field at position #7), Phase D `add-rando-major-glitch` SHALL un-pin user input for `logic` field values **3 (`HybridMajorGlitches`)** and **4 (`NoLogic`)**.

The byte layout, enum value assignments, and field widths SHALL remain unchanged from the Phase A baseline — Phase A's spec already defines values 3 and 4. Phase D opens user-controllable access; it does NOT change the canonical-serialization byte sequence for any specific tuple of values.

Default-settings seeds (`logic = NoGlitches`) SHALL remain byte-identical in `placement_digest_hex` to Phase A and Phase B baselines.

This requirement is **ADDED** (not MODIFIED) to avoid a multi-change archive-sequencing conflict on the Phase A `Settings canonical serialization order (normative)` requirement (per memory `[[openspec-authoring-patterns]]`). Multiple changes can ADD their own per-axis extensions without colliding on the baseline.

#### Scenario: HybridMG value participates in settings_hash
- **WHEN** a seed is generated with `logic=hybrid_major_glitches`
- **THEN** the resulting `settings_hash` differs from the equivalent `logic=major_glitches` seed

#### Scenario: NoLogic value participates in settings_hash
- **WHEN** a seed is generated with `logic=no_logic`
- **THEN** the resulting `settings_hash` differs from any other `logic=...` seed

#### Scenario: Default NoGlitches preserved
- **WHEN** a Phase A default seed (`logic=NoGlitches`) is generated post-this-change
- **THEN** the `settings_hash` is byte-identical to the Phase A baseline

#### Scenario: Phase B un-pin still active
- **WHEN** a Phase B seed with `logic=overworld_glitches` is generated AFTER this Phase D change archives
- **THEN** the un-pin extension from `add-rando-trick-logic-and-axes` continues to apply; Phase D adds values 3-4 without removing Phase B's coverage of values 1-2
