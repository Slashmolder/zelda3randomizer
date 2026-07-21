# Contributing

Start with an incremental validation pass while you work:

```sh
python assets/scripts/run_rando_validation.py quick
```

The runner is fail-closed: if a requested binary, object tree, savestate, or
local registry is missing, the check fails and tells you how to prepare it.
Use `--build-mode=prebuilt --binary=<path>` only when another build step has
already produced the binary and objects.

## Validation profiles

| Profile | Contract |
|---|---|
| `quick` | Incremental build, focused source guards, config/grant self-tests, logic microbenchmark, and a short default generator benchmark. |
| `ci` | Public assetless source/runtime contract. `--phase=source` and `--phase=runtime --build-mode=prebuilt` support split CI jobs. Local ignored registries are rejected. The source phase automatically resolves the generator-version diff range. |
| `full` | Worktree bootstrap, first build, regeneration of every local registry (including terrain), mandatory rebuild, all source/runtime checks (including the dedicated grant alias), full corpus, and performance checks. Calibrated local scenarios gate; any still-uncalibrated hosted scenarios collect telemetry until three target runs exist. |

Before asking for review on a randomizer change, run `full` with the original US
ROM-derived inputs available. A successful automated run does not replace the
owner gameplay playtest for runtime, rendering, or interaction changes.

The default binary is `./zelda3` on Linux/macOS and
`bin/x64-Release/zelda3.exe` on Windows. Every run writes machine-readable step
timings to `tmp/rando-validation-timings.json`; override this with
`--timings-json`.

For `ci --phase=source` and `full`, the generator-version guard automatically
uses the merge-base of `origin/main` (falling back to `main`) and `HEAD`. A
missing base fails closed; fetch `origin/main` or supply both `--base-sha` and
`--head-sha` explicitly.

## Build and generated artifacts

GNU Make emits and includes `.d` dependency files, so header edits rebuild their
actual C/C++ consumers. The behavioral guard parses those files and requires the
exact consumer set to rebuild, proves unrelated objects remain unchanged, and
tests `clean_obj` only against disposable scratch artifacts. It never cleans a
developer's working build. Randomizer codegen still runs on every Make invocation
because optional ignored registries can disappear; atomic no-change writes keep
an unchanged codegen pass from rebuilding objects.

Fresh linked worktrees should run:

```sh
python assets/scripts/setup_worktree.py
python assets/scripts/setup_worktree.py --verify --require-all-artifacts
```

The first command mirrors or prepares local inputs. The second is the strict
full-validation prerequisite check. After a build,
`run_rando_local_checks.py --prepare-only --binary=<path>` refreshes pot, enemy,
terrain, soul-room, bonk, and generated C lookup data.

## Randomizer changes

- Keep code, OpenSpec deltas, tests, and docs in one coherent change.
- Run `./zelda3 --rando-selftest=list` to discover focused groups; the
  unqualified `--rando-selftest` remains the complete suite.
- Placement-affecting changes require the generator-version gate and a fresh
  corpus regeneration. Do not rebaseline unexplained digest movement.
- Grant sites must use the public grant transaction API. The grant-consumer
  source guard owns the narrow allowlist for low-level resolver/delivery calls.
- Record manual playtest coverage separately from automated results.

Windows builds are supported through Visual Studio/MSBuild. The validation
runner finds MSBuild on `PATH` or through Visual Studio Installer's `vswhere`,
so an ordinary PowerShell session works when the Desktop development with C++
workload is installed. The historical TCC batch path cannot compile the C++
settings UI or recursive randomizer sources and is intentionally deprecated.
