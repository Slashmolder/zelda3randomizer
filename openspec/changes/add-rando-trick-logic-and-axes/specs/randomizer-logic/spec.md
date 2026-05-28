## ADDED Requirements

### Requirement: OP_TRICK predicate handler

`OP_TRICK trick_id` (op-code 15) SHALL evaluate true when bit `trick_id` is set in `settings.tricks` (uint64 bitmask). The trick_id space is enumerated in `assets/rando/op_registry.yaml` `tricks:` table; each entry has a stable bit position and a kebab-case name (`boots-clip`, `fake-flippers`, `bunny-revival`, etc.).

When `settings.tricks == 0` (Phase A default), every `OP_TRICK` predicate evaluates false; existing Phase A logic-graph behavior is preserved — non-trick seeds have the same reachability output before and after this change.

> **Stub status**: the exact trick set + per-location applicability is deferred to apply-time (depends on ALTTPR PHP grep findings).

#### Scenario: Default tricks=0 reproduces Phase A reachability
- **WHEN** a Phase A default-settings seed is generated (tricks=0)
- **THEN** `Logic_ComputeReachability` returns the same set of reachable locations as before this change; corpus digests match byte-for-byte

#### Scenario: Trick predicate unlocks location only when bit set
- **WHEN** a location's predicate is `AND(<base>, OP_TRICK boots-clip)`
- **AND** the seed has `settings.tricks` with the `boots-clip` bit cleared
- **THEN** the location is unreachable
- **WHEN** the same seed is regenerated with the `boots-clip` bit set
- **THEN** the location is reachable (assuming `<base>` is also satisfied)

### Requirement: OP_DIFFICULTY_AT_LEAST predicate handler

`OP_DIFFICULTY_AT_LEAST threshold` (op-code 16) SHALL evaluate true when `settings.item_pool_difficulty >= threshold`. The threshold encoding mirrors the enum: `easy=0 < normal=1 < hard=2 < expert=3`. Phase A already supports all four pool levels in pool composition; the op exposes the same axis in predicates.

> **Stub status**: per-location applicability is deferred to apply-time.

#### Scenario: Normal-difficulty seed satisfies "at-least-easy" predicate
- **WHEN** a seed has `settings.item_pool_difficulty = normal` and a location's predicate is `OP_DIFFICULTY_AT_LEAST easy`
- **THEN** the predicate evaluates true

#### Scenario: Easy-difficulty seed fails "at-least-hard" predicate
- **WHEN** a seed has `settings.item_pool_difficulty = easy` and a location's predicate is `OP_DIFFICULTY_AT_LEAST hard`
- **THEN** the predicate evaluates false; the location is unreachable

### Requirement: OP_GLITCH_LEVEL_AT_LEAST predicate handler

`OP_GLITCH_LEVEL_AT_LEAST threshold` (op-code 17) SHALL evaluate true when `settings.logic >= threshold`. The threshold encoding: `NoGlitches=0 < OverworldGlitches=1 < MajorGlitches=2`. Phase B exposes the first three; `HybridMG` and `NoLogic` are reserved for later phases.

When `settings.logic == NoGlitches` (Phase A default), every `OP_GLITCH_LEVEL_AT_LEAST` predicate with a non-zero threshold evaluates false; Phase A logic behavior is preserved.

> **Stub status**: per-location glitch predicate authoring deferred to apply-time.

#### Scenario: NoGlitches seed reproduces Phase A reachability
- **WHEN** a seed has `settings.logic == NoGlitches`
- **THEN** every `OP_GLITCH_LEVEL_AT_LEAST` predicate with non-zero threshold evaluates false; logic-graph reachability matches Phase A behavior

#### Scenario: OverworldGlitches unlocks Bumper Cave Ledge
- **WHEN** a seed has `settings.logic == OverworldGlitches` and the Bumper Cave Ledge location's predicate includes `OP_GLITCH_LEVEL_AT_LEAST OverworldGlitches`
- **THEN** the location is reachable (subject to its other predicate constraints)

### Requirement: Per-trick ROM-version verification status

ALTTPR upstream targets the Japanese 1.0 ROM; this fork targets the US 1.0 ROM. Tricks and glitch-level mechanics that ALTTPR's logic graph assumes available may have JP/US timing differences, mechanic differences, or be entirely absent on US 1.0. The randomizer SHALL track per-trick ROM-version verification status so users can distinguish "ALTTPR says this trick exists" from "we have confirmed this trick works on US 1.0."

> **Stub status**: scaffolding (op_registry `rom_version_status` field, codegen guard rejecting `jp10-only`, `fallback_warnings` emission for unverified tricks) deferred to apply-time per tasks §12.6.1-8. The fork-vs-ALTTPR ROM-version provenance gap is real (see `rom_version_unverified_tricks` memory) but is currently surfaced via task tracking, not by the binary.

Each entry in `assets/rando/op_registry.yaml`'s `tricks:` table SHALL carry a `rom_version_status` field with one of these values:

- `untested-on-us10` — trick is in the upstream logic graph but no contributor has confirmed it on US 1.0 (DEFAULT for newly-added tricks).
- `verified-us10` — trick has been performed end-to-end on a real US 1.0 build by a named contributor with the date recorded.
- `cross-version` — trick is a pure player skill (e.g., `dark-room-nav` — memorize the layout) OR a mechanic that has been verified to behave identically on JP 1.0 and US 1.0.
- `jp10-only` — trick has been confirmed to NOT work on US 1.0; SHALL NOT be used in trick gates.
- `us10-different` — trick exists on both ROMs but with different timing/mechanics; the upstream logic graph's assumptions may not transfer; needs per-site verification.

The same field SHALL be added to the `glitch_levels:` table in `op_registry.yaml` (OverworldGlitches, MajorGlitches, HybridMG, NoLogic), with the same value space.

The generator SHALL emit a spoiler `fallback_warnings` entry of kind `unverified_tricks_enabled` when any active trick bit (`settings.tricks`) or non-zero glitch level (`settings.logic`) corresponds to a trick whose `rom_version_status` is `untested-on-us10`, `jp10-only`, or `us10-different`. The warning detail SHALL list the offending trick names so race admins and seed validators can decide whether to accept the seed.

Tricks with `rom_version_status: jp10-only` SHALL NOT appear in any predicate body in `logic.yaml` or `logic_parts/*.yaml`; the codegen well-formedness pass SHALL reject any predicate that references them.

#### Scenario: Default seed has no unverified-tricks warning
- **WHEN** a seed is generated with `settings.tricks == 0` and `settings.logic == NoGlitches`
- **THEN** the spoiler's `fallback_warnings` array does NOT contain an `unverified_tricks_enabled` entry

#### Scenario: Enabling an untested-on-us10 trick surfaces warning
- **WHEN** a seed is generated with `settings.tricks` enabling a trick whose `rom_version_status` is `untested-on-us10`
- **THEN** the spoiler's `fallback_warnings` array contains an `unverified_tricks_enabled` entry naming that trick
- **AND** the seed is still generated (the warning is informational, not blocking)

#### Scenario: jp10-only trick rejected at codegen
- **WHEN** the codegen pass encounters a `logic.yaml` or `logic_parts/*.yaml` predicate that references a trick whose `rom_version_status` is `jp10-only`
- **THEN** codegen SHALL fail with an error citing the offending file and trick

#### Scenario: cross-version trick does not surface warning
- **WHEN** a seed is generated with `settings.tricks` enabling only tricks whose `rom_version_status` is `cross-version` or `verified-us10`
- **THEN** the spoiler's `fallback_warnings` array does NOT contain an `unverified_tricks_enabled` entry

#### Scenario: Glitch-level threshold same shape as trick verification
- **WHEN** a seed is generated with `settings.logic == OverworldGlitches` and `glitch_levels: [{name: OverworldGlitches, rom_version_status: untested-on-us10}]` in the registry
- **THEN** the spoiler's `fallback_warnings` array contains an `unverified_tricks_enabled` entry naming `OverworldGlitches`
