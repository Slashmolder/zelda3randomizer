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

### Requirement: Configurable-hint validation is compatibility- and topology-aware

The validation suite SHALL exhaust all 64 raw policy-bit combinations through
decode, normalize, encode, hash, and round-trip self-tests. It SHALL separately
exercise a reviewed pairwise CLI/spoiler/lifecycle matrix that covers every
named profile, mix, coverage, paid depth, race state, required locale, scarcity
class, and lifecycle seam at least once.

Adversarial plan tests SHALL reverse candidate/placement iteration and verify
literal mix quotas/backfill, Variety's frozen full-content compatibility,
nested tile/source and paid-queue prefixes, compact ids, no orphan assignment,
global fact uniqueness, exact paid facts, safe underfill, and the prohibition
on unsupported necessity/barren claims.

Lifecycle tests SHALL pin current 2/2 policy-bound identity through generation,
sidecar activation/save, export, reveal, warm snapshot, and cold snapshot
paths. A genuine signed generator-156 race artifact SHALL independently pin
the frozen 1/1 facts, text, digest, legacy spoiler shape, and strict refusal of
new policy bits. Crossed/unknown/mismatched versions SHALL fail closed without
harming gameplay state.

Spoiler validation SHALL inspect every requested JSON spoiler, derive legal
topology from normalized policy, validate actual counts/source prefixes, retain
legacy fields, exclude runtime state, and independently recompute the semantic
digest. It SHALL not assume 18 primary/six reserve facts.

Paid-depth-zero tests SHALL cover every owned service in original/US, German,
and French through pre-choice framing, decline, insufficient funds, successful
single charge/healing, repeated buffer load, and absence of discovery/queue
state.

Closeout SHALL separately require strict OpenSpec, source/codegen/version
guards, full self-test and slot/snapshot checks, MSVC Release, GCC and Clang
`-Werror`, full corpus, independent fresh-eyes review, and focused owner
gameplay. Placement or sphere digest drift SHALL fail validation and SHALL NOT
be blindly rebaselined.

#### Scenario: Exhaustive wire-policy round trip
- **WHEN** all 64 assignments of the six policy bits are decoded
- **THEN** every result normalizes deterministically, re-encodes canonically,
  and Off/legacy Balanced retain their frozen bytes

#### Scenario: Pairwise matrix covers every policy value
- **WHEN** the reviewed expensive-test matrix is audited
- **THEN** every profile, component value, race state, locale, scarcity class,
  and lifecycle entry has explicit coverage without requiring the full
  Cartesian product

#### Scenario: Prefix regression fails
- **WHEN** a lower-coverage/depth plan changes a semantic fact/source pair that
  should be retained from the maximal plan
- **THEN** automated hint validation fails even if both individual digests are
  internally self-consistent

#### Scenario: Old builder text regression fails
- **WHEN** current code changes any frozen generator-156 1/1 fact, assignment,
  encoded text, digest byte, or legacy spoiler field
- **THEN** the signed race-artifact validation fails

#### Scenario: Variable spoiler topology is independently checked
- **WHEN** the spoiler validator reads Sparse, paid-only, scarce, or Off output
- **THEN** it derives expected legal counts from policy and recomputes the
  digest rather than accepting fixed-count assumptions

#### Scenario: Hint-only change cannot move placement
- **WHEN** the generator-160 corpus is compared with the integrated
  generator-159 placement/sphere baseline for otherwise equal seeds
- **THEN** any digest movement is a stop-and-diagnose failure

#### Scenario: Automated green does not close gameplay
- **WHEN** every automated/build/corpus gate passes
- **THEN** the change remains unarchived until the focused owner gameplay
  matrix is completed and accepted

### Requirement: NPC reward-preview validation gates

Automated validation SHALL cover settings, the shared resolver and formatters
for every registered item ID, representative audited source-specific contexts
and transaction paths across paid, one-time, shop, and Take-Any sources, locale
fallback, lifecycle, spoilers, race reveal, and post-placement orthogonality.
Source audits and guards SHALL retain ownership of the full source roster.
Owner gameplay SHALL remain a separate closeout gate.

Current-schema JSON settings and text spoilers SHALL report the canonical
preview boolean. Generator-156 compatibility JSON/text SHALL omit it. A
CRC-correct generator-156 fixture mutation setting `[25]` bit 7 SHALL be refused
as `SettingsCorrupt` without modifying the artifact or writing output/scratch.

The same-seed Off/On pair SHALL differ in canonical `[25]` bit 7, settings hash,
and share identity only. Placement digest, sphere digest, clue plan/digest, and
hint rows SHALL remain identical.

#### Scenario: Public spoiler pair proves independence
- **WHEN** the public generator creates the same Hints-Off seed with previews
  Off and On
- **THEN** JSON/text mirror each boolean, settings identities differ, and
  placement/sphere/plan/row identities match

#### Scenario: Every registered item fits the shared formatters
- **WHEN** validation renders every registered item ID through the shared
  reward-page and inventory formatters and exercises representative seller,
  free/trade, Fairy, shop-slot, and Take-Any templates
- **THEN** the full qualified identity and price fit or the renderer preserves
  the prior complete dialogue without partial output

#### Scenario: Representative sources preserve transaction ownership
- **WHEN** source-specific and grant-transaction self-tests exercise
  representative paid and one-time view, decline, insufficient-funds,
  acceptance, retryable-failure, checked-replay, repeated-input, and lifecycle
  paths
- **THEN** presentation remains read-only and each successful transaction
  charges, grants, and checks exactly once

#### Scenario: Locale boundary is explicit and safe
- **WHEN** Original/US, German, and French cases exercise the shared formatters
  and representative source families
- **THEN** Original/US receives complete exact previews, German/French remain
  byte-identical, and both configuration UIs disclose the supported locale

#### Scenario: Corpus does not hide presentation drift
- **WHEN** the full generator-160 corpus runs with focused preview-on coverage
- **THEN** existing placement and sphere digests remain unchanged and any drift
  is diagnosed instead of rebaselined

#### Scenario: Owner gameplay remains open after automation
- **WHEN** all automated and cross-platform gates pass
- **THEN** paid/free/trade NPCs, shopsanity, Take-Any, retry/replay, race,
  locale, save/snapshot, and slot-switch gameplay remain explicitly pending
  until the owner signs them off

