#!/usr/bin/env python3
"""Determinism-constraints grep for src/rando/ (tasks.md §1.0b, §1.7 layer 1).

Rejects any source-level reference to non-deterministic / non-portable APIs
inside ``src/rando/*.[ch]``. Per ``randomizer-core / Determinism constraints``.

Empty ``src/rando/`` is a clean pass — the guard activates the moment the first
randomizer source file lands.

Forbidden tokens:
  - rand, random, arc4random  (non-deterministic RNG)
  - time(, clock_gettime       (wall-clock dependency)
  - "float ", "double "        (cross-platform floating-point ABI drift)

Usage:
  python assets/scripts/check_determinism.py            # scan + report
  python assets/scripts/check_determinism.py --quiet    # exit code only
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

RANDO_DIR = Path("src/rando")

# Word-boundary patterns to avoid false positives inside identifiers
# (e.g., ``my_random_helper`` should NOT trip ``\brand\b`` if we're careful).
# We deliberately allow ``randomizer`` since that's our subsystem name.
FORBIDDEN = [
    (r"\brand\b\s*\(", "rand() — non-deterministic stdlib RNG"),
    (r"\brandom\b\s*\(", "random() — non-deterministic stdlib RNG"),
    (r"\barc4random\b", "arc4random — non-deterministic RNG"),
    (r"\btime\s*\(", "time() — wall-clock dependency"),
    (r"\bclock_gettime\b", "clock_gettime — wall-clock dependency"),
    # Floating point: match the type keyword followed by an identifier
    # (declaration form) but allow it inside string literals / comments via
    # the line-level filter below.
    (r"(?<![a-zA-Z_])float\s+[a-zA-Z_]", "float declaration — ABI drift risk"),
    (r"(?<![a-zA-Z_])double\s+[a-zA-Z_]", "double declaration — ABI drift risk"),
]

ALLOWED_BASENAMES = {
    # If a file is intentionally exempt (e.g., a test harness wrapper), list
    # it here. None for now.
}


def strip_strings_and_comments(line: str) -> str:
    """Best-effort strip of C string literals and ``//`` comments from a line.

    We don't try to handle multi-line ``/* */`` blocks — the grep is intended
    to be conservative; a multi-line comment containing ``rand()`` won't trip
    it, but realistically determinism violations live in code, not prose.
    """
    # Strip // comments
    idx = line.find("//")
    if idx >= 0:
        line = line[:idx]
    # Strip "..." string literals (naive — doesn't handle escaped quotes
    # robustly, but adequate for guard purposes).
    line = re.sub(r'"(?:[^"\\]|\\.)*"', '""', line)
    return line


def scan_file(path: Path) -> list[tuple[int, str, str]]:
    violations: list[tuple[int, str, str]] = []
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except OSError as exc:
        print(f"warning: cannot read {path}: {exc}", file=sys.stderr)
        return violations
    for lineno, raw_line in enumerate(text.splitlines(), start=1):
        cleaned = strip_strings_and_comments(raw_line)
        for pattern, reason in FORBIDDEN:
            if re.search(pattern, cleaned):
                violations.append((lineno, raw_line.rstrip(), reason))
    return violations


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--quiet", action="store_true", help="exit code only, no output")
    parser.add_argument(
        "--root",
        type=Path,
        default=RANDO_DIR,
        help="directory to scan (default: src/rando)",
    )
    args = parser.parse_args(argv)

    if not args.root.exists():
        if not args.quiet:
            print(f"check_determinism: {args.root} does not exist yet — clean pass (A0 scaffold).")
        return 0

    files = sorted(p for p in args.root.rglob("*") if p.suffix in (".c", ".h"))
    if not files:
        if not args.quiet:
            print(f"check_determinism: no .c/.h files under {args.root} — clean pass.")
        return 0

    total_violations = 0
    for path in files:
        if path.name in ALLOWED_BASENAMES:
            continue
        for lineno, line, reason in scan_file(path):
            total_violations += 1
            if not args.quiet:
                print(f"{path}:{lineno}: {reason}")
                print(f"  > {line}")

    if total_violations:
        if not args.quiet:
            print(f"\ncheck_determinism: {total_violations} violation(s) in src/rando/.")
            print("Per randomizer-core / Determinism constraints, these APIs are forbidden.")
        return 1

    if not args.quiet:
        print(f"check_determinism: {len(files)} file(s) scanned, no violations.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
