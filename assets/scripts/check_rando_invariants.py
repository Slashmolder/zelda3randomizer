#!/usr/bin/env python3
"""Headless rando CLI invariant checker (complements --rando-selftest + the digest corpus).

The digest corpus (``run_rando_corpus.py``) pins ``placement_digest_hex`` for a
*fixed* set of (settings, seed) pairs -- it catches *unintended drift* but says
nothing about whether a freshly-chosen (settings, seed) is actually
**completable**, whether the settings hash is *unique* across distinct settings,
or whether determinism holds for seeds that aren't in the manifest. This script
fills those gaps by generating seeds across a small matrix and asserting
invariants on the spoiler JSON ``meta`` block.

What it checks
--------------
1. **Completability sweep** -- across
   ``mode.state``  in  {open, standard, inverted, retro}
   x ``goal``  in  {fast_ganon, ganon, dungeons, pedestal, triforce-hunt,
                 completionist, ganonhunt}
   x ``item_pool``  in  {normal, hard}, with a couple of seeds each:
   every generated seed must have ``meta.goal_completable == true`` and zero
   *unreachable* placements.

2. **settings_hash determinism + uniqueness** -- same (settings, seed) generated
   twice yields identical ``settings_hash_full_hex``; and varying ONE settings
   axis changes the hash (no silent collisions).

3. **Cross-seed placement determinism** -- same (settings, seed) twice yields
   identical ``placement_digest_hex`` and ``sphere_digest``.

4. **Share-string shape** -- ``meta.share_string`` is present, non-empty, and
   uppercase-base32-ish (the binary emits RFC4648 base32 without padding).

Which fallback_warnings are failures (read this before "fixing" assertion #1)
-----------------------------------------------------------------------------
The spoiler's ``meta.fallback_warnings`` array carries three kinds (see
``src/rando/rando_spoiler.c`` ~line 183 and ``src/rando/rando_placement.h``):

  - ``retry_attempts``         -- benign. The assumed-fill placer simply took
                                 more than one attempt before succeeding. Pure
                                 metadata; appears on most Standard/Inverted
                                 seeds.
  - ``forward_fill_fallback``  -- benign for completability. Some items were
                                 placed via forward-fill instead of assumed-fill,
                                 but the accepted attempt still has zero
                                 unreachable placements (the placer ranks
                                 attempts ``unreachable*100 + fallbacks`` and
                                 keeps the best -- see rando_placement.c ~740).
                                 Emitted routinely on triforce-hunt / ganonhunt
                                 goals. A completable seed can carry this.
  - ``unreachable_placements`` -- THE real softlock signal. The placer could not
                                 make every placement reachable. This is a
                                 genuine generation failure.

Note: there is no ``accessibility_none_seed`` warning anymore. Under the ALTTPR
three-way accessibility axis, ``none`` means "beatable only" — such seeds are
still guaranteed completable, so they are NOT failures. The emitter was removed
(see rando_spoiler.c) and this guard no longer treats it as a failure. (This
matrix only ever exercises the default ``accessibility=items`` anyway.)

The task brief's shorthand was "forward-fill / unreachable kinds are failures,"
but the binary demonstrably emits ``forward_fill_fallback`` on *completable*
hunt-goal seeds (verified empirically against the shipping binary). Gating on it
would fail every triforce-hunt/ganonhunt seed despite ``goal_completable==true``.
The authoritative, code-grounded completability signals are therefore
``goal_completable == true`` AND no ``unreachable_placements``. That is what this
script enforces.

CLI facts (verified against the binary, not assumed)
----------------------------------------------------
  - pool-difficulty settings key is ``item_pool`` (NOT ``item_pool_difficulty``;
    the latter is the JSON struct field name, not the CLI key).
  - world-state settings key is ``mode.state``.
  - settings are passed as ``--settings=k1=v1,k2=v2,...``.
  - seed is ``--seed=<decimal-or-0xhex>``.
  - spoiler path is ``--out-spoiler=<path>`` and the JSON ``meta`` block exposes
    ``goal_completable``, ``fallback_warnings``, ``settings_hash_full_hex``,
    ``placement_digest_hex``, ``sphere_digest``, ``share_string``.

Usage
-----
  python assets/scripts/check_rando_invariants.py
  python assets/scripts/check_rando_invariants.py --binary=bin/x64-Release/zelda3.exe
  python assets/scripts/check_rando_invariants.py --quiet
  python assets/scripts/check_rando_invariants.py --seeds-per-cell=1   # faster
  python assets/scripts/check_rando_invariants.py --jobs=1             # serial

Run with the working directory set to the repo root (where ``zelda3_assets.dat``
lives) so the binary can load assets. Exit code is 0 on all-pass, nonzero on any
failure -- matching the other ``check_*.py`` guard scripts.
"""
from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

# --- Matrix ---------------------------------------------------------------

WORLD_STATES = ["open", "standard", "inverted", "retro"]
GOALS = [
    "fast_ganon",
    "ganon",
    "dungeons",
    "pedestal",
    "triforce-hunt",
    "completionist",
    "ganonhunt",
]
ITEM_POOLS = ["normal", "hard"]

# A small spread of known-accepted smoke seeds; index [:seeds_per_cell] is taken
# per cell. This is not a random refusal/fuzz test: every seed here should
# generate across the default strict-accessibility matrix. Explicit
# reject/accept coverage lives in check_rando_slot_path.py.
SEED_POOL = [1, 2, 0xABCDEF, 31337, 85]

# fallback_warnings kinds that are genuine generation failures (vs. benign
# metadata). See module docstring for the full rationale.
FAILURE_WARNING_KINDS = {"unreachable_placements"}

GEN_TIMEOUT_S = 90


def find_binary_default() -> Path:
    """Mirror run_rando_corpus.find_binary_default()."""
    candidates = [
        Path("bin") / "x64-Release" / "zelda3.exe",  # MSBuild Release x64
        Path("./zelda3.exe"),                         # Repo-root copy (Windows)
        Path("./zelda3"),                             # Linux/macOS make target
    ]
    for c in candidates:
        if c.exists():
            return c
    return Path("./zelda3")  # legacy default; error path prints it


def settings_csv(settings: dict) -> str:
    return ",".join(f"{k}={v}" for k, v in settings.items())


class GenError(Exception):
    pass


def generate(binary: Path, settings: dict, seed: int, out_dir: Path) -> dict:
    """Run one --generate-seed and return the parsed spoiler ``meta`` dict.

    Raises GenError on non-zero exit, timeout, or missing/unparseable spoiler.
    """
    out_dir.mkdir(parents=True, exist_ok=True)
    out_json = out_dir / f"s{seed}.json"
    csv = settings_csv(settings)
    try:
        proc = subprocess.run(
            [
                str(binary),
                "--generate-seed",
                f"--settings={csv}",
                f"--seed={seed}",
                f"--out-spoiler={out_json}",
            ],
            check=True,
            capture_output=True,
            timeout=GEN_TIMEOUT_S,
        )
    except subprocess.CalledProcessError as e:
        err = (e.stderr or b"").decode("utf-8", "replace").strip()
        raise GenError(f"generator exited {e.returncode}: {err[:400]}") from e
    except subprocess.TimeoutExpired as e:
        raise GenError(f"generator timed out after {GEN_TIMEOUT_S}s") from e

    if not out_json.exists():
        err = (proc.stderr or b"").decode("utf-8", "replace").strip()
        raise GenError(f"spoiler not written ({err[:200]})")
    try:
        data = json.loads(out_json.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as e:
        raise GenError(f"spoiler unparseable: {e}") from e
    meta = data.get("meta")
    if not isinstance(meta, dict):
        raise GenError("spoiler has no 'meta' object")
    return meta


def failure_warnings(meta: dict) -> list[dict]:
    """Return only the fallback_warnings that count as genuine failures."""
    out = []
    for w in meta.get("fallback_warnings", []):
        if isinstance(w, dict) and w.get("kind") in FAILURE_WARNING_KINDS:
            out.append(w)
    return out


SHARE_RE = re.compile(r"^[A-Z2-7]+$")  # RFC4648 base32 alphabet, no padding


# --- Checks ---------------------------------------------------------------


def default_jobs() -> int:
    return max(1, min(12, os.cpu_count() or 1))


def check_completability(binary: Path, out_dir: Path, seeds_per_cell: int,
                         quiet: bool, jobs: int) -> list[str]:
    """Invariant #1: every cell in the matrix generates a completable seed."""
    failures: list[str] = []
    seeds = SEED_POOL[:seeds_per_cell]
    n_cells = len(WORLD_STATES) * len(GOALS) * len(ITEM_POOLS) * len(seeds)
    if not quiet:
        print(f"[1] Completability sweep: {n_cells} generations "
              f"({len(WORLD_STATES)}x{len(GOALS)}x{len(ITEM_POOLS)} "
              f"x {len(seeds)} seed(s), jobs={jobs})")
    tasks = []
    for state in WORLD_STATES:
        for goal in GOALS:
            for pool in ITEM_POOLS:
                for seed in seeds:
                    tasks.append((len(tasks), state, goal, pool, seed))

    def run_one(task: tuple[int, str, str, str, int]) -> tuple[int, str | None]:
        idx, state, goal, pool, seed = task
        settings = {"mode.state": state, "goal": goal, "item_pool": pool}
        label = f"{state}/{goal}/{pool}/seed={seed}"
        try:
            meta = generate(binary, settings, seed, out_dir / f"cell_{idx:03d}")
        except GenError as e:
            return idx, f"[1] {label}: {e}"
        if meta.get("goal_completable") is not True:
            return idx, (
                f"[1] {label}: goal_completable="
                f"{meta.get('goal_completable')!r}"
            )
        fw = failure_warnings(meta)
        if fw:
            return idx, f"[1] {label}: failure warnings {fw}"
        return idx, None

    jobs = max(1, jobs)
    results: dict[int, str | None] = {}
    if jobs == 1:
        for task in tasks:
            idx, failure = run_one(task)
            results[idx] = failure
    else:
        with ThreadPoolExecutor(max_workers=jobs) as executor:
            future_to_idx = {executor.submit(run_one, task): task[0] for task in tasks}
            for future in as_completed(future_to_idx):
                idx = future_to_idx[future]
                try:
                    got_idx, failure = future.result()
                except Exception as e:
                    got_idx, failure = idx, f"[1] runner error: {e!r}"
                results[got_idx] = failure

    for idx in range(len(tasks)):
        failure = results.get(idx)
        if failure:
            failures.append(failure)
            if not quiet:
                print(f"    FAIL {failure[4:]}")
    if not quiet and not failures:
        print(f"    OK   all {n_cells} seeds completable, no unreachable warnings")
    return failures


def check_share_shape(binary: Path, out_dir: Path, quiet: bool) -> list[str]:
    """Invariant #4: share_string present and base32-shaped (cheap, reuses a gen)."""
    failures: list[str] = []
    settings = {"mode.state": "open", "goal": "ganon", "item_pool": "normal"}
    try:
        meta = generate(binary, settings, 2024, out_dir)
    except GenError as e:
        failures.append(f"[4] share-string gen failed: {e}")
        if not quiet:
            print(f"[4] Share-string shape: FAIL ({e})")
        return failures
    share = meta.get("share_string", "")
    ok = isinstance(share, str) and len(share) >= 16 and bool(SHARE_RE.match(share))
    if not quiet:
        print(f"[4] Share-string shape: '{share}'")
    if not ok:
        failures.append(f"[4] share_string malformed: {share!r}")
        if not quiet:
            print(f"    FAIL malformed share_string: {share!r}")
    elif not quiet:
        print("    OK   share_string present and base32-shaped")
    return failures


def check_determinism(binary: Path, out_dir: Path, quiet: bool) -> list[str]:
    """Invariants #2 (determinism half) + #3: same (settings, seed) -> identical
    settings_hash, placement_digest, sphere_digest across two runs."""
    failures: list[str] = []
    settings = {"mode.state": "standard", "goal": "dungeons", "item_pool": "normal"}
    seed = 31337
    if not quiet:
        print("[2/3] Determinism: two runs of identical (settings, seed)")
    try:
        a = generate(binary, settings, seed, out_dir / "det_a")
        b = generate(binary, settings, seed, out_dir / "det_b")
    except GenError as e:
        failures.append(f"[2/3] determinism gen failed: {e}")
        if not quiet:
            print(f"    FAIL gen error: {e}")
        return failures
    for field in ("settings_hash_full_hex", "placement_digest_hex",
                  "sphere_digest"):
        if a.get(field) != b.get(field):
            failures.append(
                f"[2/3] {field} not deterministic: {a.get(field)!r} != "
                f"{b.get(field)!r}")
            if not quiet:
                print(f"    FAIL {field} differs across runs")
    if not quiet and not failures:
        print("    OK   settings_hash / placement_digest / sphere_digest stable")
    return failures


def check_hash_uniqueness(binary: Path, out_dir: Path, quiet: bool) -> list[str]:
    """Invariant #2 (uniqueness half): varying ONE settings axis must change
    settings_hash_full_hex -- no silent collisions."""
    failures: list[str] = []
    seed = 55
    base = {"mode.state": "open", "goal": "ganon", "item_pool": "normal"}
    variants = {
        "mode.state": "inverted",
        "goal": "pedestal",
        "item_pool": "hard",
    }
    if not quiet:
        print("[2] settings_hash uniqueness: vary one axis at a time")
    try:
        base_meta = generate(binary, base, seed, out_dir / "uniq_base")
    except GenError as e:
        failures.append(f"[2] uniqueness base gen failed: {e}")
        if not quiet:
            print(f"    FAIL base gen: {e}")
        return failures
    base_hash = base_meta.get("settings_hash_full_hex")
    seen = {base_hash: "base"}
    for axis, val in variants.items():
        v = dict(base)
        v[axis] = val
        try:
            meta = generate(binary, v, seed, out_dir / f"uniq_{axis.replace('.', '_')}")
        except GenError as e:
            failures.append(f"[2] uniqueness gen ({axis}={val}) failed: {e}")
            if not quiet:
                print(f"    FAIL {axis}={val} gen: {e}")
            continue
        h = meta.get("settings_hash_full_hex")
        if h == base_hash:
            failures.append(
                f"[2] settings_hash collision: {axis}={val} hashes identical "
                f"to base ({h})")
            if not quiet:
                print(f"    FAIL {axis}={val} collides with base hash")
        elif h in seen:
            failures.append(
                f"[2] settings_hash collision: {axis}={val} == {seen[h]} ({h})")
            if not quiet:
                print(f"    FAIL {axis}={val} collides with {seen[h]}")
        else:
            seen[h] = f"{axis}={val}"
            if not quiet:
                print(f"    OK   {axis}={val} -> distinct hash {h[:16]}...")
    return failures


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--binary", type=Path, default=find_binary_default())
    parser.add_argument("--seeds-per-cell", type=int, default=2,
                        help="seeds per matrix cell for the completability sweep "
                             "(default 2; max %d)" % len(SEED_POOL))
    parser.add_argument("--jobs", type=int, default=default_jobs(),
                        help="parallel generator processes for the completability "
                             "sweep (default min(12, CPU count); use 1 for serial)")
    parser.add_argument("--quiet", action="store_true",
                        help="exit code + summary only")
    args = parser.parse_args(argv)

    if args.seeds_per_cell < 1 or args.seeds_per_cell > len(SEED_POOL):
        print(f"error: --seeds-per-cell must be 1..{len(SEED_POOL)}")
        return 2
    if not args.binary.exists():
        print(f"check_rando_invariants: binary {args.binary} not found. "
              f"Build first or pass --binary.")
        return 2
    args.binary = args.binary.resolve()

    if not args.quiet:
        print(f"check_rando_invariants: binary {args.binary}")
        print(f"check_rando_invariants: jobs {max(1, args.jobs)}")

    all_failures: list[str] = []
    with tempfile.TemporaryDirectory(prefix="zelda3_rando_inv_") as td:
        out_dir = Path(td)
        for sub in ("det_a", "det_b", "uniq_base", "uniq_mode_state",
                    "uniq_goal", "uniq_item_pool"):
            (out_dir / sub).mkdir(parents=True, exist_ok=True)
        all_failures += check_completability(args.binary, out_dir,
                                             args.seeds_per_cell, args.quiet,
                                             args.jobs)
        all_failures += check_determinism(args.binary, out_dir, args.quiet)
        all_failures += check_hash_uniqueness(args.binary, out_dir, args.quiet)
        all_failures += check_share_shape(args.binary, out_dir, args.quiet)

    if all_failures:
        print(f"\ncheck_rando_invariants: FAIL -- {len(all_failures)} invariant "
              f"violation(s):")
        for f in all_failures:
            print(f"  - {f}")
        return 1

    print("\ncheck_rando_invariants: PASS -- all invariants hold.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
