# Tasks: harden-rando-validation-ergonomics

Work on `codex/rando-validation-hardening`. Every implementation phase ends
buildable and self-test green. Runtime-only hardening is placement-neutral: any
corpus digest movement stops the change for diagnosis.

## 1. Plan and baseline

- [x] 1.1 Audit current generation, runtime grant, caller, build, tooling, docs,
      and performance contracts with three independent read-only agent passes.
- [x] 1.2 Record clean baseline timings for MSVC build, full self-test, logic,
      default/complex generation, slot path, invariants, door tests, initialization/snapshot ownership,
      and the full local corpus in `baseline.md` with commit, platform, commands,
      artifact state, samples, and variance limitations.
- [x] 1.3 Validate this OpenSpec change strictly and complete independent
      architecture review before implementation.

## 2. Build and validation ergonomics

- [x] 2.1 Add GCC/Clang C+C++ depfiles, include them, clean them, and add a
      behavioral CI check proving touch/change/no-op/clean wiring. Verify ordinary and generated-header
      edits rebuild consumers while an unchanged codegen run rebuilds nothing.
- [x] 2.2 Make binary/object/artifact-dependent tools fail closed by default;
      retain only explicit schema/source/allow-missing modes. Enumerate and test
      slot path, benchmark, placer runtime, initialization/snapshot ownership, corpus version, corpus
      bump, and link-symbol prerequisite behavior.
- [x] 2.3 Complete worktree verification and local preparation for every local
      registry, including terrain; make the full profile self-contained.
- [x] 2.4 Add `run_rando_validation.py` with listed `quick`, `ci`, and `full`
      profiles, cross-platform builds, two-pass full rebuild, timings, and exact
      failure reporting.
- [x] 2.5 Add/update contributor docs, README, randomizer guide, PR checklist,
      and CLI discovery; remove or explicitly deprecate the broken TCC path and
      fix `extract_assets.bat` error handling.

## 3. Lossless receipt delivery

- [x] 3.1 Expose shared receipt-capacity, immediate-inventory, and
      no-animation/full-effect helpers; remove the duplicate quiet-effect
      switch and shop allocator scan.
- [x] 3.2 Make item-receipt allocation report success. Under active randomizer,
      decide acceptance before mutating Link state and fall back losslessly on
      saturation; preserve and parity-test randomizer-inactive behavior.
- [x] 3.3 Check tablet falling-prize allocation and quiet-grant/exit on failure.
- [x] 3.4 Test full non-evictable capacity, every rotate position with an
      evictable slot, same-type limits, exactly-once delivery, unchanged
      existing ancillae, full allowed-RAM deltas, input/action-state preservation,
      sound/HUD behavior, and all deferred effect classes.

## 4. Generated grant contract

- [x] 4.1 Emit compact semantic opcode+payload metadata for all registry items;
      round-trip every registry token and reject unknown/lossy dispatch kinds.
- [x] 4.2 Implement side-effect-free `RandoGrantPlan` resolution and use it for
      dispatch, field/shop drawing, confirmations, quiet grants, and tablets.
- [x] 4.3 Add a restorable/repeatable grant fixture and exhaustively classify
      the generated `ITEM__COUNT` baseline (currently 234): virtual ids derived
      and rejected, Nothing/at-cap explicit accepted no-op, every
      placeable item intentional, no substituted vanilla fallback.
- [x] 4.4 Cover every progressive boundary, non-linear bow, boomerang pre/post
      state, dungeon/prize/direct classes, bottles, rupees, and shared-byte
      preservation; assert display/grant agreement.

## 5. Grant transactions and event probes

- [x] 5.1 Add animated, quiet, deferred-prepare, and deferred-commit transaction
      APIs; add presence-aware identity lookup; define accepted no-op/failure;
      freeze deferred resolution in snapshot-safe ancilla state; commit checked
      state and all irreversible caller effects only after accepted delivery.
- [x] 5.2 Migrate chests, tablets, Digging Game, Chest Game, Hammer Pegs, NPC
      gifts, shops, Take Any, boss hearts/prizes, pots, terrain, bonks, enemies,
      and flute/key-drop paths from raw dispatch/sentinel ownership. Take Any
      commits the chosen delivery plus explicit sibling forfeit atomically.
- [x] 5.3 Add `check_grant_consumers.py`; both raw resolution APIs and low-level
      bypasses use a narrow exhaustive callsite allowlist.
- [x] 5.4 Add the exhaustive shared transaction fixture plus production-backed
      chest, tablet, shop/Take Any, boss-prize, standing-pickup, and
      bonk/enemy retry probes for normal/direct/no-op/saturated/replay branches,
      cleanup state, and exactly-once commits. Enforce the remaining migrated
      families structurally rather than simulating asset/physics frames.
- [x] 5.5 Add `--rando-selftest=list`, named groups, `--rando-grant-check`, and a
      dedicated cross-platform CI grant step; unqualified self-test still runs
      all groups and is repeatable.

## 6. Performance coverage

- [x] 6.1 Add manifest-driven public/local benchmark suites referencing corpus
      labels (unique/version/digest validated), one suite warmup, provenance-rich
      JSON output, per-case timeout above budget, and three-run-derived coarse
      median/max budgets.
- [x] 6.2 Run the 5000-iteration logic benchmark on all CI platforms and the
      complex generation suite on Linux; upload benchmark JSON.
- [x] 6.3 Add corpus per-row elapsed telemetry, ten-slowest summary, timings
      partial JSON including skipped/failed status, and `if: always()` CI artifact
      upload without a noisy aggregate gate.
- [x] 6.4 Add deterministic placement/reachability/door work counters as
      reset/frozen non-gating diagnostic output only; prove twice-in-process
      equality and counter-disabled canonical serialization/digest parity.

## 7. Version and determinism

- [x] 7.1 Advance `kGeneratorVersion` as required by the current source gate and
      restamp the corpus manifest while preserving every existing placement and
      sphere digest byte-for-byte. Wire any new generated file through Make,
      MSBuild, Switch, setup, and codegen-wiring guards.

## 8. Review and closeout

- [x] 8.1 Independent fresh-eyes review of runtime transaction ordering,
      saturation, state restoration, resolver completeness, source guards,
      artifact lifecycle, performance flake risk, and docs; remediate findings.
- [x] 8.2 Validate OpenSpec strictness and reconcile proposal/design/tasks/deltas
      to current source before archive.
- [x] 8.3 Run `git diff --check`, all source guards, Make depfile behavior,
      clean+incremental Unix and MSVC builds, every self-test group, grant/event
      probes, slot path, invariants, initialization/snapshot ownership, door tests, placer determinism,
      benchmarks, full corpus, and platform digest parity.
- [x] 8.4 Commit the focused branch. Owner gameplay playtest remains required
      before merge/archive; automation does not complete that gate.
