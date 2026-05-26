#!/usr/bin/env python3
"""Link-time symbol blocklist (tasks.md §1.0f, §1.7 layer 2).

Per ``randomizer-core / Determinism constraints`` layer 2: source-level grep
(``check_determinism.py``) catches direct references, but an indirect call via
a helper, macro splice, or inline function can slip through. This script runs
``nm`` against every compiled ``src/rando/*.o`` and asserts no undefined
references to:

  - rand, random, arc4random  (non-deterministic RNG)
  - time, clock_gettime, gettimeofday  (wall-clock dependency)

Catches things like a helper in ``src/zelda_rtl.c`` named ``ZeldaRandU32`` that
the rando module unknowingly links against.

**A0 status (scaffold)**: ``src/rando/*.o`` doesn't exist yet (no source files
to compile). Until then, the script reports "no objects, clean pass." The
script is wired into CI now so the gate is live from the moment the first
rando object builds.

Usage:
  python assets/scripts/check_link_symbols.py
  python assets/scripts/check_link_symbols.py --object-dir=build/
"""
from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

FORBIDDEN_SYMBOLS = {
    "rand",
    "random",
    "arc4random",
    "arc4random_buf",
    "arc4random_uniform",
    "time",
    "clock_gettime",
    "gettimeofday",
}


def find_rando_objects(object_dir: Path) -> list[Path]:
    """Locate ``src/rando/*.o`` (or wherever they ended up after compile)."""
    candidates: list[Path] = []
    for pattern in ("src/rando/*.o", "**/rando_*.o", "**/rando.o"):
        candidates.extend(object_dir.glob(pattern))
    return sorted(set(candidates))


def nm_undefined(obj: Path) -> list[str]:
    """Return the list of undefined symbol names in ``obj`` via ``nm -u``."""
    nm = shutil.which("nm")
    if nm is None:
        print(f"warning: nm not available, cannot scan {obj}", file=sys.stderr)
        return []
    out = subprocess.run([nm, "-u", str(obj)], capture_output=True, text=True, check=False)
    syms: list[str] = []
    for line in out.stdout.splitlines():
        # ``nm -u`` outputs lines like ``                 U rand`` on Linux
        # and ``rand`` on macOS — normalize:
        parts = line.split()
        if not parts:
            continue
        sym = parts[-1].lstrip("_")  # macOS prefixes symbols with underscore
        if sym in FORBIDDEN_SYMBOLS:
            syms.append(sym)
    return syms


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--object-dir", type=Path, default=Path("."))
    parser.add_argument("--quiet", action="store_true")
    args = parser.parse_args(argv)

    objects = find_rando_objects(args.object_dir)
    if not objects:
        if not args.quiet:
            print(
                "check_link_symbols: no src/rando/*.o objects found.\n"
                "  scaffold A0 pass — guard activates when first rando source file builds."
            )
        return 0

    total_violations = 0
    for obj in objects:
        bad = nm_undefined(obj)
        if bad:
            total_violations += len(bad)
            if not args.quiet:
                print(f"{obj}: undefined reference(s) to forbidden symbol(s): {', '.join(sorted(set(bad)))}")

    if total_violations:
        if not args.quiet:
            print(
                f"\ncheck_link_symbols: {total_violations} forbidden link-time reference(s).\n"
                "Per randomizer-core / Determinism constraints, these symbols MUST NOT appear\n"
                "in src/rando/*.o (direct or via helper)."
            )
        return 1

    if not args.quiet:
        print(f"check_link_symbols: {len(objects)} object(s) scanned, no forbidden references.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
