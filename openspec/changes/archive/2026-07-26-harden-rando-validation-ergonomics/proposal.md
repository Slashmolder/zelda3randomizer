# Proposal: Harden randomizer validation and developer ergonomics

## Why

The randomizer has strong generation-time determinism coverage and substantial
direct-dispatch self-checks, but the validation envelope is uneven at the most
failure-prone boundary: resolving a placed item is tested more thoroughly than
delivering a normal receive-code item, consuming the result at each gameplay
event family, or retaining the item when the five-slot ancilla pool is full.
Two current runtime paths can still mark or advance a check before a visual
receipt allocation succeeds. At the same time, Unix builds do not track header
dependencies, several requested test tools report success when their binary is
missing, contributor documentation understates the existing automation, and
the performance gate measures only an extremely loose default seed budget.

This change establishes one hard-to-misuse grant transaction boundary and one
documented validation ladder before another large gameplay feature expands the
surface area.

## What Changes

- Make normal item receipts and tablet falling-prize setup lossless when the
  ancilla pool is saturated. Animated and quiet grants share the same inventory
  and deferred-effect implementation.
- Generate semantic dispatch opcode + payload metadata from
  `item_registry.yaml`, introduce a
  side-effect-free grant/display plan, and exhaustively classify all item ids.
- Replace gameplay-owned `dispatch -> sentinel -> consume` pairs with immediate
  or deferred transaction APIs. A location becomes checked only when delivery
  commits; raw dispatch becomes an internal/test seam guarded by a source check.
- Add a repeatable grant fixture, filtered self-test groups, and production-backed
  probes for the shared transaction, chest, tablet, shop/Take Any, boss-prize,
  standing-pickup, bonk/enemy retry, replay, and saturation contracts. Pair
  these with an exhaustive source guard over every migrated gameplay caller.
- Add compiler depfiles to the Make build while retaining the always-run,
  content-stable randomizer codegen contract.
- Make runtime validation fail closed on absent requested prerequisites, finish
  local artifact preparation (including terrain), and add one cross-platform
  `quick`/`ci`/`full` validation entry point.
- Add manifest-driven generation benchmarks, the existing logic microbenchmark
  to all CI platforms, corpus timing telemetry, and deterministic generator
  work counters. Timing budgets remain deliberately coarse.
- Correct stale contributor documentation, retire the unsupported TCC
  randomizer build path, and document exact validation tiers and playtest scope.

## Capabilities

### New Capabilities

- `randomizer-validation`: grant-contract automation, validation profiles,
  fail-closed tooling, dependency correctness, and performance observability.

### Modified Capabilities

- `randomizer-placement`: placed-item delivery becomes a commit-based
  transaction; raw resolution no longer marks a location before its sink is
  known to have accepted the item.

## Impact

- **Runtime:** `rando.c`, receipt allocation/inventory code in `misc.c`, tablet
  and other grant-family handlers, plus small handler-local self-check seams.
- **Generated contract:** `item_registry.yaml` remains authoritative;
  `rando_logic_gen.py` emits compact per-item dispatch metadata.
- **Build/tooling:** Makefile depfiles, validation/corpus/benchmark scripts,
  worktree artifact setup, source guards, and CI workflow.
- **Documentation:** contributor entry point, README, randomizer guide, PR
  checklist, and removal/deprecation of the broken TCC instructions.
- **Determinism:** no placement semantic is intentionally changed. The source
  gate requires a generator-version bump/restamp for touched randomizer source,
  but every existing placement and sphere digest must remain byte-identical;
  digest movement is a regression and SHALL NOT be rebaselined.

## Non-goals

- No public-CI gameplay-frame or render-time budget. Public CI has no ROM asset
  blob, `--vanilla-ram-check` runs zero frames, and the GUI loop is not a valid
  deterministic benchmark seam. A future local asset-backed replay mode may
  separately time update and PPU work after three-run baselines exist.
- No input replay, physics simulation, or assertion that automation replaces
  the owner gameplay playtest gate.
