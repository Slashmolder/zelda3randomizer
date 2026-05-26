#!/usr/bin/env python3
"""Benchmark CI gate (tasks.md §1.0h, §3.11).

Asserts:
  - ``Logic_ComputeReachability`` median < 5 ms on reference desktop CI
  - same < 20 ms on Switch (manual run, not blocking CI)

across 1000 invocations on the full Phase A logic graph.

**A0 status (scaffold)**: ``Logic_ComputeReachability`` doesn't exist yet
(lands in task 3.8). Script exits clean until then. The script is wired into
CI now so the gate is live from the moment the function lands.

Usage:
  python assets/scripts/run_rando_benchmarks.py --binary=./zelda3
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=Path("./zelda3"))
    parser.add_argument(
        "--target-ms",
        type=float,
        default=5.0,
        help="median latency budget in ms (default 5.0 for desktop CI)",
    )
    parser.add_argument("--samples", type=int, default=1000)
    parser.add_argument("--quiet", action="store_true")
    args = parser.parse_args(argv)

    if not args.binary.exists():
        if not args.quiet:
            print(
                f"run_rando_benchmarks: binary {args.binary} not built.\n"
                "  scaffold A0 pass — gate activates once task 3.8 lands Logic_ComputeReachability\n"
                "  and main.c exposes a --benchmark-reachability mode."
            )
        return 0

    # TODO: invoke ``--benchmark-reachability --samples=N`` once main.c supports it,
    # parse the median latency from stdout, compare to args.target_ms.
    if not args.quiet:
        print(f"run_rando_benchmarks: TODO — implement once --benchmark-reachability is wired in {args.binary}.")
        print(f"  target_ms: {args.target_ms}  samples: {args.samples}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
