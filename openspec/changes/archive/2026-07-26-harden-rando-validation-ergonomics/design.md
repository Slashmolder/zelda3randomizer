# Design: Randomizer validation and developer ergonomics hardening

## Context

Current automation covers cross-platform corpus determinism, slot generation,
invariants, door generation, initialization + snapshot ownership, placer determinism, source
guards, and a large assetless self-test. The monolithic dispatch section proves
many direct-write and resolver cases, but it does not drive normal items through
`Rando_ReceiveOrConfirm -> Link_ReceiveItem -> AncillaAdd_ItemReceipt`.

The receipt allocator has five ancilla slots. Most animated randomizer callers
do not preflight capacity, and allocation failure is currently silent after
Link has entered receive state. Ether and Bombos tablets also ignore failure to
create their falling-prize ancilla after dispatch. Shops preflight capacity,
but that is a UX optimization rather than a system-wide correctness guarantee.

The engine's assetless CLI modes call C functions before SDL initialization;
they do not run `ZeldaRunFrame`. The design therefore uses direct state-machine
probes and extracted handler seams, not a pretend frame-replay harness.

## Goals / Non-goals

**Goals**

- Every placeable registry item has one explicit, generated grant class and one
  side-effect-free plan for receive, direct, no-op, and display behavior.
- Delivery is exactly-once and lossless under saturated receipt capacity.
- Gameplay handlers cannot forget to consume a dispatch result.
- Quick failures are locally discoverable; requested checks never green-skip.
- Header edits rebuild exactly their consumers without requiring `make clean`.
- Performance regressions have both coarse wall-time tripwires and deterministic
  work telemetry.

**Non-goals**

- Full gameplay/input replay or public asset-backed frame timing.
- Tight cross-runner latency budgets derived from a single machine.
- Placement/corpus changes.

## Decisions

### D1. One generated grant plan, with delivery separate from resolution

`rando_logic_gen.py` emits an `ITEM__COUNT`-sized semantic opcode + payload
table from every `dispatch:` declaration in `item_registry.yaml`. Payloads
include receive codes, bottle contents, rupee amounts, dungeon/prize identity,
and direct-handler identity; generation round-trips every token and fails on an
unknown or lossy mapping. C code does not maintain parallel item-id ranges.
`Rando_ResolveGrantPlan` is side-effect-free and classifies an item as a receive
code, direct handler, successful no-op, or invalid virtual item.

The plan is value-owned and includes location, item, semantic opcode/payload,
effective pre-grant receive/display descriptor, and any direct descriptor.
Inputs include a read-only inventory snapshot plus location, seed, and settings
needed by trap/confirmation display. Callers and deferred tokens never
recompute through singleton `g_last_dispatched_*` state.

Dispatch, field/shop drawing, confirmation icons, quiet delivery, and tablet
setup consume the same plan. The grant self-test enumerates the generated count
and proves it equals `ITEM__COUNT`; virtual items are explicitly non-grantable
and `Nothing` is an explicit accepted no-op. No substituted placement may
silently fall back to the location's vanilla item.

`Placement_TryLookup(location, &item)` distinguishes a present identity
placement from an absent location; value equality is never used as a presence
test. At-cap progressives, hearts/refills, and `Nothing` are accepted no-ops and
commit. A bottled item with no empty bottle is not accepted and remains
retryable. Invalid/virtual items are errors and never commit.

### D2. Receipt inventory effects have one authoritative implementation

`misc.c` exposes receipt-capacity and no-animation grant helpers around the
existing inventory table. The no-animation helper performs the same deferred
effects as a completed animated receipt: rupees, heart-piece rollover, heart
containers, refills, armor palette, and sword/shield graphics/palette.

`AncillaAdd_ItemReceipt` reports allocation success. In active randomizer mode,
receipt capacity/acceptance is checked before `Link_ReceiveItem` mutates any
auxiliary, input, movement, pose, damage, or dash state. When no slot is
available, the transaction calls the no-animation helper directly and retains
sound/HUD feedback; it does not enter and roll back the hold-up state. Vanilla
behavior is unchanged. The pot/terrain quiet path uses this helper instead of a
second deferred-effect switch. Shop preflight calls the shared capacity helper.

Tablet handlers check falling-prize creation. Failure immediately performs the
resolved quiet grant and exits the cutscene without double delivery.

### D3. Checked state follows transaction commit

Immediate transaction APIs combine presence-aware lookup, plan resolution,
acceptance/delivery, and checked-bit commit. Animated and quiet variants differ
only in presentation. Irreversible caller effects (price charge, sprite/event
despawn, minigame win consumption, obtained flag, or sibling lock) occur only
after accepted delivery, or are part of the same multi-location commit.

Take Any is the intentional multi-location case: successful delivery marks the
chosen location collected and atomically marks its mutually-exclusive sibling
forfeited/unavailable. The checked bitmap may encode both terminal states, but
the transaction contract and tests distinguish delivery from deliberate
forfeit; no sibling is forfeited before the chosen item succeeds.

Deferred visuals receive a value-owned token. The token freezes progressive and
display resolution at prepare time and stores location, item, receive/display
descriptor, and commit state in ancilla-owned `g_ram` that StateRecorder already
captures. Collision delivers and commits. Cancellation/room exit leaves the
location unchecked and retriggerable; snapshot reload reconstructs the same
token without re-resolution. Raw `Rando_OnLocationCheck` and
`Rando_DispatchVanillaGrant` become grant-core/test-only.

This replaces checked-as-intent behavior. Save/snapshot points occur between
frames, so a synchronous immediate transaction is the atomic unit;
process-crash atomicity inside that call is a non-goal. Deferred work is the
exception and carries its explicit snapshot-safe pending token. The checked bit
may not claim an undelivered item except the documented post-success Take Any
forfeit.

### D4. Structural enforcement beats regex data-flow guesses

The existing inventory-write audit remains. A new source guard rejects both raw
resolution APIs and low-level grant bypasses outside a narrow generated
callsite allowlist; gameplay code must call an approved animated, quiet, or
deferred transaction API. Legitimate vanilla calls are explicitly enumerated.
This avoids trying to infer C control flow with the audit guard's proximity
regex or treating all of `rando.c` as trusted core.

### D5. Handler probes are focused state-machine tests

The `grant` self-test group owns a fixture that snapshots/restores RAM,
placements, checked bits, ancilla state, settings, and relevant process-local
randomizer state. It is repeatable twice in one process. Small probes invoke
the production adapter or an extracted seam called by production. The shared
fixture covers normal/direct/no-op/retryable delivery, saturated allocation,
frozen progressive state, replay, identity placement, and checked-after-delivery
ordering. Adapter probes cover mapped chests, shops/Take Any, boss prizes,
standing Piece of Heart identity, tablet token replay, and the bonk/enemy
burn/fall/explosion retry choreography. The source guard proves every other
migrated gameplay family uses that same public transaction boundary; it does
not pretend to replay their physics or cutscene frames. Asset-touching DMA/render
work is split behind a narrow presentation seam so production decisions can run
with an empty asset table.

`--rando-selftest=list`, `--rando-selftest=<group>`, and
`--rando-grant-check` expose the groups. The unqualified flag continues to run
everything, and CI labels the grant group separately on every desktop platform.

### D6. Make depfiles coexist with always-run codegen

GCC/Clang C and C++ recipes emit `-MMD -MP` depfiles; Make includes them and
`clean_obj` removes them. Generated files remain order-only presence gates.
Randomizer codegen still runs on every build because optional gitignored inputs
can disappear; its atomic no-change writes keep consumer depfiles stable.

### D7. Requested validation fails closed and has one entry point

Runtime checks fail when their requested binary, object files, manifests,
savestates, or local artifacts are absent. Only explicit modes such as
`--schema-only`, `--source-only`, or a named allow-missing option may skip.

`run_rando_validation.py` provides fixed profiles:

- `quick`: incremental build, depfile/grant-consumer/source smoke, the `config`
  and `grant` self-test groups, logic microbenchmark, and default benchmark.
- `ci`: clean/prebuilt modes for the exact public assetless source/runtime
  contract. It refuses local ignored registries and asserts expected assetless
  registry counts.
- `full`: artifact bootstrap/verification, build, local artifact refresh
  including terrain, mandatory rebuild, every source/runtime/corpus check, and
  local performance scenarios.

The full profile is self-contained after the proprietary ROM-derived source
artifacts have been supplied; otherwise it fails early with the bootstrap
command. Feature closeout is `full` plus owner gameplay playtest. The runner prints
timings and exact failures and never reports a requested unavailable check as a
pass.

### D8. Performance gates are versioned, coarse, and observable

`tests/rando_benchmarks/manifest.yaml` references existing corpus labels for
settings/seeds. The runner performs an untimed warmup, records external and
spoiler-reported latency, emits optional JSON, and gates medians for repeated
short cases or max for deterministic complex cases.

The default median <= 2000 ms remains the normative catastrophic budget. Complex
Linux and local budgets are recorded only after three clean runs in the target
environment, with explicit headroom; each case timeout exceeds its budget. The
existing logic p50 <= 5 ms check runs 5000 iterations on Linux/macOS/Windows.
External wall time is the gate; spoiler `clock()` time is diagnostic because its
platform semantics differ. One suite warmup avoids doubling long scenarios.
The corpus times inside each worker, reports status and its ten slowest rows,
and writes partial timing JSON even on failure without adding a noisy aggregate
gate under concurrent execution.

Generator diagnostics expose reset-at-generation counters for placement
attempts, completed reachability expansion passes, and door-generation
attempts. They exclude UI, spoiler, and self-test work; avoid per-node hot-loop
increments; and appear only in optional benchmark JSON/stderr, never canonical
spoilers, race stamps, share strings, or digests. Same-seed twice-in-process
equality and counter-disabled serialized-output/digest parity are required.

## Risks / Trade-offs

- Transaction migration touches many gameplay call sites. It proceeds by event
  family with the structural guard enabled only after all families migrate.
- Assetless tests must not call DMA/decompression that assumes loaded assets;
  compare grant-state and resolver outputs at safe seams.
- Restoring all test globals is non-trivial. The fixture is deliberately a
  first-class API and repeatability is an acceptance test.
- Timing remains noisy; budgets are catastrophic-regression tripwires, while
  corpus timing and deterministic work counters provide trend evidence.

## Migration Plan

1. Land specification, depfiles, fail-closed tooling, validation profiles, and
   benchmark telemetry without runtime semantic changes.
2. Add receipt helpers and saturated-allocation fallback; prove animated/quiet
   final-state parity.
3. Generate grant metadata and land the exhaustive pure resolver contract.
4. Migrate immediate and deferred handler families, then enable the structural
   dispatch-consumer guard.
5. Add handler-family probes and filtered groups, run fresh-eyes review, and
   reconcile the change to as-built behavior before archive.

No corpus digest is rebaselined. Owner playtest remains the final gameplay gate.
