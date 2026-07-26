# Validation/performance baseline (2026-07-19)

Commit: `71e52313feeeb539a458c9429530efb64b543073`

Environment: Windows x64, MSVC Release build, local ROM-derived registries and
asset blob present in the durable source workspace. Timings are single-host
developer measurements, not portable CI budgets. Process startup and disk I/O
are included where noted.

## Commands and results

| Check | Command shape | Result |
|---|---|---:|
| Clean MSVC build | `msbuild Zelda3.sln /m /t:Rebuild /p:Configuration=Release /p:Platform=x64` | 34.3 s |
| Incremental MSVC build | same build without clean | 1.36 s |
| Full assetless self-test | `zelda3.exe --rando-selftest` | PASS, 5.7 s |
| Door self-test | `zelda3.exe --door-selftest` | PASS, 260/260, 11.6 s |
| Logic microbenchmark | `zelda3.exe --rando-bench-logic --bench-iters=5000` | p50 0.0638 ms, p95 0.0753 ms |
| Default generation benchmark | `run_rando_benchmarks.py`, 20 samples | external median 31.5 ms; placement median 6 ms |
| Init-order replay | `check_init_order.py --strict` | PASS, 13 saves |
| Slot path | `check_rando_slot_path.py` | PASS, 12 cells |
| Invariant sweep | `check_rando_invariants.py` | PASS, 112 generations |
| Full local corpus | `run_rando_corpus.py` without local-family skips | PASS, 236/236, 81.9 s |

Source audit, codegen strictness, schema, and tracked-tree cleanliness also
passed. The isolated build output was cleaned after measurement.

## Representative complex generation rows

These are serial Windows Release measurements of fixed existing corpus rows.
They establish ranking and catastrophic headroom only; each target CI/local
environment still requires three clean measurements before its manifest budget
is finalized.

| Existing corpus scenario | Elapsed |
|---|---:|
| Chains + hunt + accessibility none | 2.31 s |
| Door basic, Open/Ganon | 9.27 s |
| Random key rings + door basic | 23.78 s |
| NPC souls + door shuffle | 67.16 s |
| Chains + pots + souls (local registry composition) | 1.89 s |
| Terrain + dungeon entrances (local registry composition) | 0.42 s |
| Door + NPC souls + pot keys (local registry composition) | 31.61 s |

The 67-second NPC-souls/door case already runs in the public corpus under its
per-entry timeout and SHALL NOT be duplicated in the standalone performance
suite. Corpus timings are telemetry under concurrent execution, not a total-time
gate.

## Post-change local benchmark calibration

On 2026-07-19, three clean Windows x64 Release runs at generator version 153
measured the local manifest scenarios as follows (external wall time, ms):

| Scenario | Run 1 | Run 2 | Run 3 | Gating max |
|---|---:|---:|---:|---:|
| terrain-dungeon-entrances | 496 | 630 | 477 | 5,000 |
| chains-pots-souls | 1,845 | 2,184 | 1,833 | 10,000 |
| door-npc-souls-pot-keys | 35,521 | 33,704 | 38,013 | 90,000 |

These are deliberately coarse catastrophic-regression tripwires. Public
complex scenarios remain telemetry-only until three hosted-Linux runs exist.
