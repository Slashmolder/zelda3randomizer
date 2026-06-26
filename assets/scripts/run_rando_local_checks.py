#!/usr/bin/env python3
"""Local randomizer pre-merge checks.

This runner is intentionally local-only: it includes checks that need a built
binary plus ROM-derived/extracted artifacts that public CI cannot have. In
particular, it refreshes the gitignored pot ground-truth dump from the binary
and then runs the full byte-for-byte pot table freshness check.

Usage:
  python assets/scripts/run_rando_local_checks.py
  python assets/scripts/run_rando_local_checks.py --binary=bin/x64-Release/zelda3.exe
  python assets/scripts/run_rando_local_checks.py --skip-corpus
"""
from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


REPO = Path(__file__).resolve().parents[2]
POT_DUMP = REPO / "assets" / "rando" / "pot_dump.gen.txt"
CHEST_TABLE = REPO / "assets" / "rando" / "chest_table.gen.bin"
DEFAULT_TMP = REPO / "tmp" / "local-rando-checks"


def find_binary_default() -> Path:
    candidates = [
        REPO / "bin" / "x64-Release" / "zelda3.exe",
        REPO / "zelda3.exe",
        REPO / "zelda3",
    ]
    for candidate in candidates:
        if candidate.exists():
            return candidate
    return REPO / "zelda3"


def resolve_repo_path(path: Path) -> Path:
    if path.is_absolute():
        return path
    return (REPO / path).resolve()


def run(label: str, cmd: list[str]) -> int:
    print(f"\n==> {label}", flush=True)
    print(" ".join(cmd), flush=True)
    proc = subprocess.run(cmd, cwd=REPO)
    if proc.returncode != 0:
        print(f"\nrun_rando_local_checks: FAIL at {label} "
              f"(exit {proc.returncode})", flush=True)
    return proc.returncode


def require_file(path: Path, how_to_make: str) -> int:
    if path.exists():
        return 0
    rel = path.relative_to(REPO)
    print(f"run_rando_local_checks: missing {rel}")
    print(how_to_make)
    return 2


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("--binary", type=Path, default=find_binary_default(),
                        help="Path to the built zelda3 binary.")
    parser.add_argument("--tmp", type=Path, default=DEFAULT_TMP,
                        help="Scratch directory for local check dumps.")
    parser.add_argument("--skip-corpus", action="store_true",
                        help="Skip the full regression corpus.")
    parser.add_argument("--skip-selftest", action="store_true",
                        help="Skip --rando-selftest.")
    args = parser.parse_args(argv)

    binary = resolve_repo_path(args.binary)
    tmp = resolve_repo_path(args.tmp)

    if not binary.exists():
        print(f"run_rando_local_checks: binary {binary} not found. "
              f"Build first or pass --binary.")
        return 2

    rc = require_file(
        CHEST_TABLE,
        "Run the asset extraction/build once with the US ROM present; the full "
        "pot table generator reads this gitignored chest anchor artifact.",
    )
    if rc:
        return rc

    tmp.mkdir(parents=True, exist_ok=True)
    key_depth = tmp / "key_depth.txt"

    checks = [
        ("refresh pot ground-truth dump",
         [str(binary), "--dump-pot-table", str(POT_DUMP)]),
        ("gen_pot_tables --check",
         [sys.executable, "assets/scripts/gen_pot_tables.py", "--check"]),
        ("gen_pot_tables --check-overrides",
         [sys.executable, "assets/scripts/gen_pot_tables.py", "--check-overrides"]),
        ("dump key-depth ground truth",
         [str(binary), "--dump-key-depth", str(key_depth)]),
        ("gen_pot_key_depth --check",
         [sys.executable, "assets/scripts/gen_pot_key_depth.py",
          "--dump", str(key_depth), "--check"]),
    ]

    if not args.skip_selftest:
        checks.append(("rando self-test", [str(binary), "--rando-selftest"]))
    if not args.skip_corpus:
        checks.append(("regression corpus",
                       [sys.executable, "assets/scripts/run_rando_corpus.py",
                        "--binary", str(binary)]))

    for label, cmd in checks:
        rc = run(label, cmd)
        if rc:
            return rc

    print("\nrun_rando_local_checks: PASS", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
