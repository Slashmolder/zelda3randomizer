# randomizer-validation Specification

## Purpose
TBD - created by archiving change harden-rando-validation-ergonomics. Update Purpose after archive.
## Requirements
### Requirement: Exhaustive generated grant contract

The randomizer SHALL derive semantic grant opcode + payload metadata from the
authoritative item registry and SHALL classify every item id as receive-code,
direct, explicit no-op, or invalid virtual. Resolution SHALL be side-effect-free,
value-owned, and shared by grant and display surfaces. A substituted placeable
item SHALL NOT fall back to the checked location's vanilla item. Presence-aware
lookup SHALL distinguish a known identity placement from an absent location.

#### Scenario: Every registered item is intentional

- **WHEN** the grant self-test enumerates every id below `ITEM__COUNT`
- **THEN** every placeable id has an intentional receive/direct/no-op plan, every
  virtual id is explicitly rejected, every registry token round-trips to its
  opcode and payload, and no unknown dispatch kind reaches C codegen or runtime
  fallback

#### Scenario: Grant and display stay aligned

- **WHEN** a state-dependent progressive, bow, or boomerang item is resolved at
  each ownership boundary
- **THEN** animated grant, quiet grant, field sprite, shop icon, and direct
  confirmation use the same frozen effective item/code for that pre-grant state
  without consulting singleton last-dispatch state

#### Scenario: Accepted no-op versus retryable failure

- **WHEN** a valid `Nothing`, at-cap progressive, heart, or refill item is
  delivered
- **THEN** the transaction accepts the intentional no-op and commits the check
- **AND WHEN** a bottled item has no empty bottle, or an invalid virtual item is
  presented for delivery
- **THEN** delivery is not accepted and the check remains retryable/uncommitted

### Requirement: Lossless grant delivery

An active-randomizer grant SHALL establish capacity/acceptance before mutating
Link's receive state and SHALL deliver exactly once even when no receipt or
falling-prize ancilla can be allocated. Animated and quiet delivery SHALL have
the same final inventory and deferred resource effects. Existing unrelated
ancillae SHALL remain unchanged, and Link SHALL not remain in a receive or
immobilized state after fallback.

#### Scenario: Saturated normal receipt falls back safely

- **WHEN** all five ancilla slots are occupied by non-evictable entries and an
  active-randomizer normal receive-code item is granted
- **THEN** the item and all deferred effects are applied exactly once without an
  icon ancilla, existing ancillae remain intact, sound/HUD feedback remains, and
  Link's auxiliary/input/movement/pose/damage/dash state is never changed by an
  attempted hold-up animation

#### Scenario: Saturated tablet delivery commits

- **WHEN** Ether or Bombos tablet dispatch resolves to a receive-code item but
  the falling-prize ancilla cannot be created
- **THEN** the item is quiet-granted exactly once, the tablet cutscene exits,
  and the location becomes checked only after the grant succeeds

### Requirement: Transaction-owned checked state

Gameplay grant sites SHALL use approved animated, quiet, or deferred
transaction APIs. Immediate transactions SHALL resolve, deliver, then mark the
location checked. Deferred transactions SHALL mark checked only at their
delivery commit point. Prices, despawns, obtained flags, minigame wins, and
other irreversible caller effects SHALL occur only after accepted delivery or
inside the same commit. Raw lookup/dispatch and low-level bypass APIs SHALL be
restricted by an automated exhaustive callsite guard.

#### Scenario: A caller cannot drop a dispatch result

- **WHEN** a gameplay source file introduces a raw grant-dispatch call outside
  the approved grant core/test allowlist
- **THEN** the source guard fails with the file and line and CI rejects it

#### Scenario: Failed preparation does not claim an item

- **WHEN** a deferred presentation cannot be prepared and no fallback delivery
  has yet succeeded
- **THEN** the checked bit remains clear so the item can be retried or delivered
  by the transaction's fallback path

#### Scenario: Deferred plan survives snapshot replay

- **WHEN** a prepared falling grant is snapshotted and restored before collision,
  including when progressive ownership changes elsewhere before commit
- **THEN** the ancilla-owned token retains its original location, item, receive
  and display plan; it commits once on collision, while cancellation leaves the
  location unchecked and retriggerable

#### Scenario: Take Any forfeits only after chosen delivery

- **WHEN** one Take Any item is accepted
- **THEN** the chosen location commits as collected and its mutually-exclusive
  sibling commits as explicitly forfeited in the same transaction
- **AND WHEN** chosen delivery is not accepted
- **THEN** neither location becomes collected or forfeited

### Requirement: Discoverable filtered runtime self-tests

The assetless CLI SHALL list named self-test groups, run one requested group,
and retain an all-groups mode. A dedicated grant group SHALL include the
exhaustive contract, saturation, repeatability, and representative event-family
probes and SHALL run on every desktop CI platform. A handler probe SHALL invoke
the production adapter or an extracted seam used by production and assert its
source-specific state, retry, cleanup, or ordering behavior. Families without
an assetless handler probe SHALL still be covered by the exhaustive transaction
fixture and the callsite source guard; automation does not claim to execute
their asset, physics, or cutscene frames.

#### Scenario: Grant group is isolated and repeatable

- **WHEN** `--rando-selftest=grant` or `--rando-grant-check` runs twice in one
  process fixture
- **THEN** both executions produce the same passing state without leaking RAM,
  placement, checked, ancilla, settings, or process-local randomizer state

### Requirement: Correct incremental Make builds

The GCC/Clang Make build SHALL track C and C++ header dependencies, including
generated randomizer headers, without requiring clean builds. Missing optional
gitignored registries SHALL continue to trigger the always-run content-stable
codegen check.

#### Scenario: Header edit rebuilds consumers only

- **WHEN** a normal or generated header changes
- **THEN** Make recompiles each dependent object before linking and leaves
  unrelated objects untouched; when codegen emits byte-identical output, no
  consumer recompiles

### Requirement: Fail-closed tiered validation

Binary-, object-, savestate-, or artifact-dependent validation SHALL fail when
its requested prerequisite is absent unless an explicit source-only,
schema-only, or allow-missing mode was selected. A cross-platform entry point
SHALL expose documented quick, CI-equivalent, and full local profiles with the
full profile performing artifact preparation followed by a mandatory rebuild.

#### Scenario: Missing binary cannot green-skip

- **WHEN** a runtime check is requested with a missing binary
- **THEN** it exits nonzero with the missing path and a concrete build/bootstrap
  instruction, and the validation summary reports failure rather than pass/skip

### Requirement: Low-flake performance observability

CI SHALL run the logic microbenchmark on every desktop platform and a versioned
manifest of representative generation scenarios on one platform with coarse
budgets. The corpus SHALL report per-row elapsed telemetry and machine-readable
timings. Deterministic work counters SHALL be diagnostic-only until stable
baselines justify budgets.

#### Scenario: Default and complex regressions are bounded

- **WHEN** the public performance suite runs after an untimed warmup
- **THEN** it gates the default repeated median and complex deterministic maxima
  against three-run-derived manifest budgets, sets each timeout above its
  budget, and emits a provenance-rich JSON summary

#### Scenario: Corpus timings do not add a flaky aggregate gate

- **WHEN** the regression corpus completes
- **THEN** it reports the ten slowest rows and optionally writes per-entry JSON,
  while correctness/determinism and existing per-entry timeouts remain the
  blocking corpus criteria

#### Scenario: Work telemetry is non-canonical

- **WHEN** deterministic work counters are enabled or disabled for the same
  seed twice in one process
- **THEN** enabled counter values repeat exactly, while placement/sphere
  digests and every canonical spoiler, share-string, and race-stamp byte remain
  identical to the counter-disabled run

