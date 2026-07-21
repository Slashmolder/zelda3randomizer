#!/usr/bin/env python3
"""Placer cross-run determinism guard.

The regression corpus proves cross-*build* determinism (the recorded digests
must reproduce on every platform), but every corpus seed completes on the
placer's first attempt, so the corpus never exercises — and never guards — the
RETRY loop, where non-determinism actually hides. This guard closes that gap.

Why this guard exists
---------------------
`Place_AssumedFill` retries up to `kAssumedFillMaxAttempts` and ships the
best-so-far when no attempt fully completes. A bug landed (and shipped for ~a
year) where the headless `--generate-seed` CLI defaulted to a 5-second wall-clock
budget on that retry loop: cutting the loop off early selects a *different*
best-so-far, so `placement_digest` + `goal_completable` became machine-speed /
load dependent for any seed that needs many retries.

It bit HARDEST on Windows because `clock()` there measures WALL-CLOCK time (not
process CPU time as on POSIX): a loaded dev box hit the budget mid-loop while an
idle CI runner ran the full cap — i.e. the corpus's cross-platform byte-identical
contract was silently breached and a hard seed flickered between "completable"
and "uncompletable, refusing". The fix was to default the budget to 0
(deterministic full-attempt cap), matching the in-game slot generator.

This guard has two prongs:

  1. `--source-only` (no build): a STATIC assert that the default budget is 0
     on BOTH generator entry points — the headless `--generate-seed` CLI
     (src/main.c `int budget_seconds = 0;`) and the playable-slot generator
     (src/rando/rando_generate.c: the `effective_budget` mapping of a
     negative/"use default" budget must contain no nonzero literal, i.e.
     budget<0 resolves to 0). Deterministically locks the specific fix — a
     positive default can never silently return on either path. Runs in the
     cheap source-guards job.

  2. `--binary=<path>`: a RUNTIME check that a deliberately HARD seed (one that
     exhausts the retry loop — verified via its `forward_fill_fallback`
     warning) produces a byte-identical placement + sphere digest across N
     back-to-back runs at the DEFAULT budget, and that the default path matches
     an explicit `--budget-seconds=0` run. Catches general placer
     non-determinism (rand/uninit/global-state/hash-order) that the all-easy
     corpus seeds miss, and — on a loaded runner — a budget-default regression.

Usage:
  python assets/scripts/check_placer_determinism.py --source-only
  python assets/scripts/check_placer_determinism.py --binary=./zelda3
"""
from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
MAIN_C = REPO / "src" / "main.c"
RANDO_GENERATE_C = REPO / "src" / "rando" / "rando_generate.c"

# The CANARY: a seed that EXHAUSTS the retry loop (never fully completes → runs
# the full attempt cap and forward-fills), so it is exactly the budget-sensitive
# path the corpus can't reach. Triforce-hunt with 30 placed pieces at the
# beatable tier is reliably hard; expert item pool seed 0x5 is a known-hard
# seed. If a future
# placer change makes this complete early (no forward_fill_fallback warning), the
# runtime check FAILS LOUDLY so the canary gets re-chosen rather than silently
# guarding nothing.
CANARY_SETTINGS = ("mode.state=retro,goal=triforce-hunt,item_pool=expert,"
                   "pieces_required=20,pieces_placed=30,accessibility=none")
CANARY_SEED = "0x5"
RUNS = 3

# `int budget_seconds = 0;` — the CLI default in MaybeRunGenerateSeedAndExit.
# The `--budget-seconds=` parse assigns via atoi(), not an int literal, so this
# only matches the declaration's default.
_BUDGET_DEFAULT_RE = re.compile(r"\bint\s+budget_seconds\s*=\s*(\d+)\s*;")

# The SLOT generator (Rando_GenerateSlot, called by both the in-game settings
# screen and the PC native settings window) takes `budget` with budget<0
# meaning "use the default", resolved by an `effective_budget = ...;`
# assignment. The determinism contract requires that default to resolve to 0:
# any nonzero integer literal in the RHS is a wall-clock default smuggled in
# (e.g. the historical `(budget < 0) ? ((settings->race_mode != 0) ? 0 : 10)
# : budget`, which made non-race slot generation machine-speed-dependent).
# Checking "no nonzero literal" rather than one exact expression keeps the
# guard robust to how the fixed mapping is phrased.
_EFFECTIVE_BUDGET_RE = re.compile(r"\beffective_budget\s*=\s*(.*?);", re.S)
_INT_LITERAL_RE = re.compile(r"\b(?:0[xX][0-9a-fA-F]+|\d+)\b")


def _check_main_c() -> int:
    if not MAIN_C.exists():
        print(f"check_placer_determinism: {MAIN_C} not found", file=sys.stderr)
        return 1
    text = MAIN_C.read_text(encoding="utf-8")
    matches = _BUDGET_DEFAULT_RE.findall(text)
    if not matches:
        print("check_placer_determinism: could not find `int budget_seconds = N;` "
              "default in src/main.c — the guard needs updating (was the CLI "
              "budget refactored?). Failing closed to preserve the determinism "
              "contract.", file=sys.stderr)
        return 1
    bad = [v for v in matches if int(v) != 0]
    if bad:
        print(f"check_placer_determinism: --generate-seed default budget_seconds "
              f"= {bad[0]} (expected 0). A positive default puts a WALL-CLOCK "
              f"cutoff (clock() is wall-clock on Windows) on Place_AssumedFill's "
              f"retry loop, making placement machine-speed-dependent and breaking "
              f"the corpus's cross-platform determinism contract. Use 0 (the "
              f"deterministic full-attempt cap); pass --budget-seconds only for "
              f"batch/debug callers that accept non-determinism.", file=sys.stderr)
        return 1
    return 0


def _check_slot_generator() -> int:
    if not RANDO_GENERATE_C.exists():
        print(f"check_placer_determinism: {RANDO_GENERATE_C} not found",
              file=sys.stderr)
        return 1
    text = RANDO_GENERATE_C.read_text(encoding="utf-8")
    matches = _EFFECTIVE_BUDGET_RE.findall(text)
    if not matches:
        print("check_placer_determinism: could not find an `effective_budget = "
              "...;` assignment in src/rando/rando_generate.c — the guard needs "
              "updating (was the slot-generator budget refactored?). Failing "
              "closed to preserve the determinism contract.", file=sys.stderr)
        return 1
    rc = 0
    for rhs in matches:
        nonzero = [t for t in _INT_LITERAL_RE.findall(rhs) if int(t, 0) != 0]
        if nonzero:
            print(f"check_placer_determinism: src/rando/rando_generate.c resolves "
                  f"the slot-generator default budget with a NONZERO literal "
                  f"({', '.join(nonzero)}) in `effective_budget = "
                  f"{' '.join(rhs.split())};`. The negative/'use default' budget "
                  f"MUST resolve to 0: a positive budget is a wall-clock cutoff "
                  f"on Place_AssumedFill's retry loop, so slot generation (the "
                  f"in-game settings screen + native settings window) becomes "
                  f"machine-speed-dependent and can diverge from the headless "
                  f"--generate-seed result for the same (settings, seed).",
                  file=sys.stderr)
            rc = 1
    return rc


def check_source() -> int:
    rc = _check_main_c()
    rc |= _check_slot_generator()
    if rc == 0:
        print("check_placer_determinism: OK (default budgets are 0 on both the "
              "--generate-seed CLI and the Rando_GenerateSlot slot generator — "
              "deterministic).")
    return rc


def _generate(binary: Path, extra_args: list[str]) -> dict | None:
    """Run --generate-seed for the canary; return its meta dict, or None on failure.

    None means the generator refused (wrote no spoiler) OR the run errored — both
    are treated as a guard failure by the caller. The refusal path is the one to
    watch: with accessibility=none the canary is written only if the best-of-N
    forward-filled result is still goal-completable, so a future placer/logic
    shift could make this seed un-completable and refused EVERY run. That would
    look like a determinism breakage but is really "re-pick the canary" — see the
    caller's message.
    """
    try:
        with tempfile.TemporaryDirectory() as td:
            out = Path(td) / "o.json"
            r = subprocess.run(
                [str(binary), "--generate-seed",
                 f"--settings={CANARY_SETTINGS}", f"--seed={CANARY_SEED}",
                 f"--out-spoiler={out}", *extra_args],
                capture_output=True, timeout=180,
            )
            if not out.exists():
                print(f"check_placer_determinism: canary wrote no spoiler "
                      f"(exit {r.returncode}). If this PERSISTS across runs the "
                      f"canary is likely no longer goal-completable under the "
                      f"current placer/logic — re-select CANARY_SEED/CANARY_SETTINGS "
                      f"(it must be BOTH retry-loop-exhausting and goal-completable). "
                      f"stderr:\n{r.stderr.decode(errors='replace')[:500]}",
                      file=sys.stderr)
                return None
            return json.loads(out.read_text(encoding="utf-8")).get("meta", {})
    except (subprocess.TimeoutExpired, subprocess.SubprocessError,
            OSError, json.JSONDecodeError) as e:
        print(f"check_placer_determinism: canary generation errored: {e!r}",
              file=sys.stderr)
        return None


def _digests(meta: dict) -> tuple[str | None, str | None]:
    return meta.get("placement_digest_hex"), meta.get("sphere_digest")


def _is_hard(meta: dict) -> bool:
    """True iff the canary EXHAUSTED the retry loop (ran the full attempt cap).

    Both `forward_fill_fallback` and `unreachable_placements` warnings are emitted
    ONLY on the exhausted-loop path (Place_AssumedFill prints them when no attempt
    achieved full reachability with zero forward-fills — i.e. it ran all
    kAssumedFillMaxAttempts). Either one proves the seed exercised the retry loop,
    which is the whole point of the canary. (Checking only forward_fill_fallback
    would miss a seed that exhausts the loop but whose best attempt happened to
    forward-fill nothing — narrower than this guard wants.)
    """
    HARD_KINDS = {"forward_fill_fallback", "unreachable_placements"}
    for w in meta.get("fallback_warnings", []):
        if w.get("kind") in HARD_KINDS and w.get("count", 0) > 0:
            return True
    return False


def check_binary(binary: Path, *, allow_missing: bool = False) -> int:
    if not binary.is_file():
        if allow_missing:
            print(f"check_placer_determinism: binary {binary} not found — "
                  "explicitly skipped (--allow-missing-binary).")
            return 0
        print(f"check_placer_determinism: binary {binary} not found; the "
              "requested runtime canary cannot run. Build it, select "
              "--source-only, or pass --allow-missing-binary explicitly.",
              file=sys.stderr)
        return 2

    # First run also validates the canary is still genuinely hard.
    first = _generate(binary, [])
    if first is None:
        return 1
    if not _is_hard(first):
        print("check_placer_determinism: the canary seed no longer exhausts the "
              "retry loop (no forward_fill_fallback warning) — it would guard "
              "nothing. Pick a harder CANARY_SETTINGS/CANARY_SEED so the test "
              "exercises Place_AssumedFill's retry loop.", file=sys.stderr)
        return 1

    base = _digests(first)
    if None in base:
        print("check_placer_determinism: canary spoiler missing a digest field",
              file=sys.stderr)
        return 1

    # N back-to-back runs at the DEFAULT budget must be byte-identical.
    for i in range(2, RUNS + 1):
        m = _generate(binary, [])
        if m is None:
            return 1
        d = _digests(m)
        if d != base:
            print(f"check_placer_determinism: NON-DETERMINISTIC placement across "
                  f"runs at the default budget.\n  run 1: {base}\n  run {i}: {d}\n"
                  f"  The placer produced a different result for the same "
                  f"(settings, seed) — a wall-clock budget, rand/time, "
                  f"uninitialized memory, or order-dependent global state.",
                  file=sys.stderr)
            return 1

    # The default path must equal an explicit deterministic (budget=0) run.
    det = _generate(binary, ["--budget-seconds=0"])
    if det is None:
        return 1
    if _digests(det) != base:
        print(f"check_placer_determinism: the DEFAULT --generate-seed budget does "
              f"not match --budget-seconds=0 on a hard seed — the default path is "
              f"non-deterministic (a positive default budget?).\n"
              f"  default:        {base}\n  --budget-seconds=0: {_digests(det)}",
              file=sys.stderr)
        return 1

    print(f"check_placer_determinism: OK ({RUNS} runs of the hard canary "
          f"({CANARY_SEED}) produced identical placement + sphere digests; "
          f"default budget == deterministic budget=0).")
    return 0


def main(argv: list[str]) -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--source-only", action="store_true",
                   help="static check only (no binary): default budget must be 0")
    p.add_argument("--binary", type=Path, default=REPO / "zelda3",
                   help="path to the zelda3 binary for the runtime check")
    p.add_argument("--allow-missing-binary", action="store_true",
                   help="explicitly skip the runtime prong if its binary is absent")
    args = p.parse_args(argv)

    rc = check_source()
    if args.source_only or rc != 0:
        return rc
    # Local-Windows ergonomics: the default is `zelda3` (no extension, matching
    # the CI/Make convention); fall back to `zelda3.exe` if only that exists.
    binary = args.binary
    if not binary.exists() and binary.with_suffix(".exe").exists():
        binary = binary.with_suffix(".exe")
    # subprocess runs a bare name like "zelda3" via a $PATH lookup (cwd is not on
    # $PATH), so a relative --binary=./zelda3 — which str(Path(...)) normalizes to
    # "zelda3" — fails with FileNotFoundError even though the file exists relative
    # to cwd. Resolve to an absolute path first (matching run_rando_corpus.py).
    if binary.exists():
        binary = binary.resolve()
    return check_binary(binary, allow_missing=args.allow_missing_binary)


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
