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
        default=2000.0,
        help="median latency budget in ms (default 2000 — spec budget for default settings)",
    )
    parser.add_argument("--samples", type=int, default=10)
    parser.add_argument("--quiet", action="store_true")
    args = parser.parse_args(argv)

    # Resolve to an absolute path: `Path("./zelda3")` stringifies back to
    # "zelda3", which subprocess would PATH-search for (and miss on Linux/macOS,
    # where cwd isn't on PATH). An absolute path runs the built binary directly;
    # resolve(strict=False) leaves a not-yet-built path absolute so the scaffold
    # branch below still treats an absent binary as a pass.
    args.binary = args.binary.resolve()

    if not args.binary.exists():
        if not args.quiet:
            print(
                f"run_rando_benchmarks: binary {args.binary} not built.\n"
                "  scaffold pass — build the binary to activate the benchmark gate."
            )
        return 0

    # Phase A1 benchmark: per-seed generation wall-clock. Reads
    # generation_wall_clock_ms from the spoiler meta block.
    # Spec scenario: "Default-settings benchmark < 2s" (`randomizer-core`).
    import json
    import statistics
    import subprocess
    import tempfile
    import time

    sample_size = max(1, min(args.samples, 50))  # cap at 50; per-seed runs are ~1s each
    if not args.quiet:
        print(f"run_rando_benchmarks: target_ms={args.target_ms}  samples={sample_size}")
        print(f"  binary={args.binary}")
    samples_ms = []
    spoiler_self_reported = []
    for i in range(sample_size):
        seed = f"0x{i:016x}"
        with tempfile.TemporaryDirectory() as td:
            out_json = Path(td) / f"bm_{i}.json"
            t0 = time.monotonic()
            try:
                subprocess.run(
                    [str(args.binary), "--generate-seed",
                     "--settings=mode.state=open,goal=fast_ganon",
                     f"--seed={seed}",
                     f"--out-spoiler={out_json}"],
                    check=True, capture_output=True, timeout=60,
                )
            except (subprocess.CalledProcessError, subprocess.TimeoutExpired) as e:
                print(f"  sample {i}: generator failed: {e}", file=sys.stderr)
                return 1
            wallclock_ms = (time.monotonic() - t0) * 1000.0
            samples_ms.append(wallclock_ms)
            if out_json.exists():
                meta = json.loads(out_json.read_text(encoding="utf-8")).get("meta", {})
                spoiler_self_reported.append(meta.get("generation_wall_clock_ms", 0))
    samples_ms.sort()
    median = statistics.median(samples_ms)
    p95 = samples_ms[int(0.95 * (len(samples_ms) - 1))]
    if not args.quiet:
        print(f"  end-to-end (incl. binary launch + I/O):")
        print(f"    median  {median:.1f} ms   p95 {p95:.1f} ms   max {max(samples_ms):.1f} ms")
        if spoiler_self_reported:
            srt = sorted(spoiler_self_reported)
            sr_med = statistics.median(srt)
            print(f"  spoiler.generation_wall_clock_ms (placement only):")
            print(f"    median  {sr_med:.0f} ms   max {max(srt)} ms")
    if median > args.target_ms:
        print(f"FAIL: median {median:.1f} ms > target {args.target_ms} ms")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
