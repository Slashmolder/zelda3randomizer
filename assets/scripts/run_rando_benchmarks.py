#!/usr/bin/env python3
"""Manifest-driven randomizer generation performance checks.

Scenarios in ``tests/rando_benchmarks/manifest.yaml`` reference authoritative
corpus labels rather than copying settings and seeds. The runner validates the
benchmark/corpus generator version and pinned placement digest before launching
the binary, performs one cheap untimed warmup for the selected suite, then gates
external process wall time. The spoiler's self-reported generation time is
telemetry only because C ``clock()`` has different semantics across platforms.

Usage:
  python assets/scripts/run_rando_benchmarks.py --schema-only
  python assets/scripts/run_rando_benchmarks.py --binary=./zelda3 --suite=public
  python assets/scripts/run_rando_benchmarks.py --binary=./zelda3 --suite=local \
      --json-out=artifacts/rando-benchmarks-local.json
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
import math
import os
import platform
import statistics
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import Any


REPO = Path(__file__).resolve().parents[2]
DEFAULT_MANIFEST = REPO / "tests" / "rando_benchmarks" / "manifest.yaml"
HEX_DIGITS = frozenset("0123456789abcdefABCDEF")
# Independent of provisional performance budgets: this only bounds a wedged
# child process or an accidentally pathological seed. Non-gating scenarios may
# exceed their provisional budget without failing, but still fail on correctness
# errors or this deliberately generous infrastructure timeout.
SAMPLE_SAFETY_TIMEOUT_S = 300


def resolve_repo_path(path: Path) -> Path:
    return path.resolve() if path.is_absolute() else (REPO / path).resolve()


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


def load_yaml(path: Path) -> dict[str, Any]:
    if not path.exists():
        raise ValueError(f"manifest not found: {path}")
    try:
        import yaml  # type: ignore
    except ImportError as exc:
        raise ValueError(
            "PyYAML not installed (pip install -r requirements.txt)"
        ) from exc
    with path.open("r", encoding="utf-8") as fp:
        data = yaml.safe_load(fp) or {}
    if not isinstance(data, dict):
        raise ValueError(f"manifest root must be a mapping: {path}")
    return data


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as fp:
        for block in iter(lambda: fp.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def git_provenance() -> dict[str, Any]:
    def run_git(*args: str) -> str | None:
        proc = subprocess.run(
            ["git", *args], cwd=REPO, text=True, capture_output=True,
            check=False,
        )
        return proc.stdout.strip() if proc.returncode == 0 else None

    status = run_git("status", "--porcelain")
    return {
        "head": run_git("rev-parse", "HEAD"),
        "dirty": bool(status) if status is not None else None,
    }


def binary_provenance(binary: Path) -> dict[str, Any]:
    stat = binary.stat()
    return {
        "path": str(binary),
        "size": stat.st_size,
        "mtime_ns": stat.st_mtime_ns,
        "sha256": sha256_file(binary),
    }


def write_json_atomic(path: Path | None, report: dict[str, Any]) -> None:
    if path is None:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(path.name + ".tmp")
    tmp.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n",
                   encoding="utf-8")
    os.replace(tmp, path)


def is_sha256(value: Any) -> bool:
    return (isinstance(value, str) and len(value) == 64 and
            all(ch in HEX_DIGITS for ch in value))


def validate_and_resolve(
    benchmark_manifest: Path,
    corpus_override: Path | None,
) -> tuple[dict[str, Any], Path, dict[str, Any], list[dict[str, Any]]]:
    benchmark = load_yaml(benchmark_manifest)
    errors: list[str] = []
    if benchmark.get("format_version") != 1:
        errors.append("benchmark format_version must be 1")

    corpus_value = benchmark.get("corpus_manifest")
    if corpus_override is not None:
        corpus_path = resolve_repo_path(corpus_override)
    elif isinstance(corpus_value, str) and corpus_value:
        corpus_path = resolve_repo_path(Path(corpus_value))
    else:
        corpus_path = REPO / "tests" / "rando_corpus" / "manifest.yaml"
        errors.append("benchmark manifest is missing corpus_manifest")
    corpus = load_yaml(corpus_path)

    benchmark_version = benchmark.get("generator_version")
    corpus_version = corpus.get("generator_version")
    if isinstance(benchmark_version, bool) or not isinstance(benchmark_version, int):
        errors.append("benchmark generator_version must be an integer")
    if benchmark_version != corpus_version:
        errors.append(
            "benchmark generator_version "
            f"{benchmark_version!r} != corpus generator_version {corpus_version!r}"
        )

    corpus_entries = corpus.get("entries", [])
    if not isinstance(corpus_entries, list):
        errors.append("corpus entries must be a list")
        corpus_entries = []
    by_label: dict[str, dict[str, Any]] = {}
    duplicate_corpus_labels: set[str] = set()
    for entry in corpus_entries:
        if not isinstance(entry, dict):
            continue
        label = entry.get("label")
        if not isinstance(label, str) or not label:
            continue
        if label in by_label:
            duplicate_corpus_labels.add(label)
        else:
            by_label[label] = entry
    if duplicate_corpus_labels:
        errors.append(
            "corpus labels referenced by benchmarks must be unique; duplicates: "
            + ", ".join(sorted(duplicate_corpus_labels))
        )

    scenarios = benchmark.get("scenarios", [])
    if not isinstance(scenarios, list) or not scenarios:
        errors.append("benchmark scenarios must be a non-empty list")
        scenarios = []

    names: set[str] = set()
    referenced_labels: set[str] = set()
    resolved: list[dict[str, Any]] = []
    for idx, raw in enumerate(scenarios):
        prefix = f"scenario {idx}"
        if not isinstance(raw, dict):
            errors.append(f"{prefix}: must be a mapping")
            continue
        name = raw.get("name")
        if not isinstance(name, str) or not name:
            errors.append(f"{prefix}: missing non-empty name")
            name = f"scenario-{idx}"
        elif name in names:
            errors.append(f"{prefix}: duplicate name {name!r}")
        names.add(name)

        suite = raw.get("suite")
        if suite not in ("public", "local"):
            errors.append(f"{prefix} {name}: suite must be public or local")
        statistic = raw.get("statistic")
        if statistic not in ("median", "max"):
            errors.append(f"{prefix} {name}: statistic must be median or max")
        budget_ms = raw.get("budget_ms")
        if (isinstance(budget_ms, bool) or
                not isinstance(budget_ms, (int, float)) or budget_ms <= 0):
            errors.append(f"{prefix} {name}: budget_ms must be positive")
            budget_ms = 1
        gating = raw.get("gating")
        if not isinstance(gating, bool):
            errors.append(f"{prefix} {name}: gating must be explicitly true or false")
            gating = False

        samples = raw.get("samples")
        if not isinstance(samples, list) or not samples:
            errors.append(f"{prefix} {name}: samples must be a non-empty list")
            samples = []
        elif statistic == "median" and len(samples) < 3:
            errors.append(
                f"{prefix} {name}: a median gate needs at least three samples"
            )
        resolved_samples: list[dict[str, Any]] = []
        for sample_idx, sample in enumerate(samples):
            sample_prefix = f"{prefix} {name} sample {sample_idx}"
            if not isinstance(sample, dict):
                errors.append(f"{sample_prefix}: must be a mapping")
                continue
            label = sample.get("corpus_label")
            expected = sample.get("expected_digest")
            if not isinstance(label, str) or not label:
                errors.append(f"{sample_prefix}: missing corpus_label")
                continue
            if label in referenced_labels:
                errors.append(
                    f"{sample_prefix}: corpus label {label!r} is referenced more than once"
                )
            referenced_labels.add(label)
            if not is_sha256(expected):
                errors.append(f"{sample_prefix}: expected_digest must be SHA-256 hex")
            entry = by_label.get(label)
            if entry is None:
                errors.append(f"{sample_prefix}: corpus label {label!r} not found")
                continue
            corpus_digest = entry.get("expected_digest")
            if expected != corpus_digest:
                errors.append(
                    f"{sample_prefix}: pinned digest {expected!r} does not match "
                    f"corpus digest {corpus_digest!r}"
                )

            # A public benchmark must remain runnable in ROM-free CI. Import the
            # corpus runner's canonical skip predicates so this check cannot
            # drift from the workflow's assetless contract.
            if suite == "public":
                from run_rando_corpus import (  # local script module
                    entry_needs_local_pot_registry,
                    entry_needs_local_soul_rooms,
                    entry_uses_enemy_drop_checks,
                    entry_uses_terrain_shuffle,
                )
                needs_local = []
                if entry_needs_local_pot_registry(entry):
                    needs_local.append("pot registry")
                if entry_uses_enemy_drop_checks(entry):
                    needs_local.append("enemy-drop registry")
                if entry_needs_local_soul_rooms(entry):
                    needs_local.append("soul-room registry")
                if entry_uses_terrain_shuffle(entry):
                    needs_local.append("terrain registry")
                if needs_local:
                    errors.append(
                        f"{sample_prefix}: public sample requires local "
                        + ", ".join(needs_local)
                    )
            resolved_samples.append({
                "corpus_label": label,
                "expected_digest": expected,
                "entry": entry,
            })

        resolved.append({
            "name": name,
            "suite": suite,
            "statistic": statistic,
            "budget_ms": float(budget_ms),
            "gating": gating,
            "samples": resolved_samples,
        })

    if errors:
        raise ValueError("benchmark manifest validation failed:\n  - " +
                         "\n  - ".join(errors))
    return benchmark, corpus_path, corpus, resolved


def setting_text(value: Any) -> str:
    if isinstance(value, bool):
        return "true" if value else "false"
    return str(value)


def scenario_timeout_seconds() -> int:
    return SAMPLE_SAFETY_TIMEOUT_S


def run_sample(
    binary: Path,
    sample: dict[str, Any],
    timeout_s: int,
    warmup: bool = False,
) -> dict[str, Any]:
    entry = sample["entry"]
    settings = entry.get("settings", {}) or {}
    seed = str(entry.get("seed", entry.get("seed_u64", "")))
    result: dict[str, Any] = {
        "corpus_label": sample["corpus_label"],
        "seed": seed,
        "expected_digest": sample["expected_digest"],
        "timeout_s": timeout_s,
        "warmup": warmup,
        "status": "running",
    }
    settings_csv = ",".join(
        f"{key}={setting_text(value)}" for key, value in settings.items()
    )
    with tempfile.TemporaryDirectory() as td:
        out_json = Path(td) / "benchmark.json"
        command = [
            str(binary), "--generate-seed",
            f"--settings={settings_csv}",
            f"--seed={seed}",
            f"--out-spoiler={out_json}",
            "--rando-work-counters",
        ]
        started = time.perf_counter()
        try:
            proc = subprocess.run(
                command, cwd=REPO, check=False, capture_output=True,
                timeout=timeout_s,
            )
        except subprocess.TimeoutExpired as exc:
            result["external_wall_ms"] = round(
                (time.perf_counter() - started) * 1000.0, 3
            )
            result["status"] = "failed"
            result["error"] = f"generator timed out after {timeout_s}s"
            if exc.stderr:
                result["stderr"] = exc.stderr.decode(errors="replace")[-4000:]
            return result
        result["external_wall_ms"] = round(
            (time.perf_counter() - started) * 1000.0, 3
        )
        stderr_text = proc.stderr.decode(errors="replace")
        if proc.returncode != 0:
            result["status"] = "failed"
            result["error"] = f"generator exited {proc.returncode}"
            result["stderr"] = stderr_text[-4000:]
            return result
        work_prefix = "rando_work_counters: "
        work_line = next(
            (line for line in stderr_text.splitlines()
             if line.startswith(work_prefix)),
            None,
        )
        if work_line is None:
            result["status"] = "failed"
            result["error"] = "generator did not emit requested work counters"
            result["stderr"] = stderr_text[-4000:]
            return result
        try:
            work = {
                key: int(value)
                for key, value in (
                    token.split("=", 1)
                    for token in work_line[len(work_prefix):].split()
                )
            }
            required_work = {
                "placement_attempts",
                "reachability_expansion_passes",
                "door_generation_attempts",
            }
            if set(work) != required_work or any(value < 0 for value in work.values()):
                raise ValueError("unexpected counter fields or values")
        except (TypeError, ValueError) as exc:
            result["status"] = "failed"
            result["error"] = f"malformed work-counter output: {exc}"
            result["stderr"] = stderr_text[-4000:]
            return result
        result["work_counters"] = work
        if not out_json.exists():
            result["status"] = "failed"
            result["error"] = "generator did not write spoiler JSON"
            return result
        try:
            spoiler = json.loads(out_json.read_text(encoding="utf-8"))
        except Exception as exc:
            result["status"] = "failed"
            result["error"] = f"spoiler JSON is unparseable: {exc}"
            return result
        meta = spoiler.get("meta", {})
        actual_digest = meta.get("placement_digest_hex")
        result["actual_digest"] = actual_digest
        result["generation_wall_clock_ms"] = meta.get("generation_wall_clock_ms")
        if actual_digest != sample["expected_digest"]:
            result["status"] = "failed"
            result["error"] = (
                "placement digest mismatch: expected "
                f"{sample['expected_digest']}, got {actual_digest}"
            )
            return result
        if not isinstance(result["generation_wall_clock_ms"], (int, float)):
            result["status"] = "failed"
            result["error"] = "spoiler meta.generation_wall_clock_ms is missing"
            return result
        result["status"] = "passed"
        return result


def percentile_95(values: list[float]) -> float:
    ordered = sorted(values)
    return ordered[math.ceil(0.95 * len(ordered)) - 1]


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=find_binary_default())
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument("--corpus-manifest", type=Path, default=None,
                        help="override the corpus manifest referenced by the benchmark manifest")
    parser.add_argument("--suite", choices=("public", "local"), default="public")
    parser.add_argument("--scenario", action="append", default=[],
                        help="run only the named scenario within the selected suite; "
                             "repeatable (default: every scenario in the suite)")
    parser.add_argument("--json-out", type=Path, default=None,
                        help="optional machine-readable report path; updated after every sample")
    parser.add_argument("--schema-only", action="store_true",
                        help="validate benchmark/corpus references without launching a binary")
    parser.add_argument("--quiet", action="store_true")
    args = parser.parse_args(argv)

    benchmark_manifest = resolve_repo_path(args.manifest)
    json_out = resolve_repo_path(args.json_out) if args.json_out else None
    try:
        benchmark, corpus_path, corpus, scenarios = validate_and_resolve(
            benchmark_manifest, args.corpus_manifest
        )
    except ValueError as exc:
        print(f"run_rando_benchmarks: {exc}", file=sys.stderr)
        return 2

    selected = [scenario for scenario in scenarios if scenario["suite"] == args.suite]
    if args.scenario:
        requested = set(args.scenario)
        selected = [scenario for scenario in selected
                    if scenario["name"] in requested]
        found = {scenario["name"] for scenario in selected}
        missing = sorted(requested - found)
        if missing:
            print(
                f"run_rando_benchmarks: scenario(s) not found in suite "
                f"{args.suite!r}: {', '.join(missing)}",
                file=sys.stderr,
            )
            return 2
    if not selected:
        print(f"run_rando_benchmarks: suite {args.suite!r} has no scenarios",
              file=sys.stderr)
        return 2
    if args.schema_only:
        if not args.quiet:
            refs = sum(len(scenario["samples"]) for scenario in scenarios)
            print(
                "run_rando_benchmarks: schema OK -- "
                f"{len(scenarios)} scenarios, {refs} unique corpus references, "
                f"generator version {corpus.get('generator_version')}"
            )
        return 0

    binary = resolve_repo_path(args.binary)
    if not binary.exists() or not binary.is_file():
        print(
            f"run_rando_benchmarks: binary {binary} not found. "
            "Build it first or pass --binary=<path>.",
            file=sys.stderr,
        )
        return 2

    started_at = datetime.now(timezone.utc).isoformat()
    report: dict[str, Any] = {
        "format_version": 1,
        "tool": "run_rando_benchmarks.py",
        "status": "running",
        "started_at_utc": started_at,
        "suite": args.suite,
        "generator_version": benchmark["generator_version"],
        "benchmark_manifest": {
            "path": str(benchmark_manifest),
            "sha256": sha256_file(benchmark_manifest),
        },
        "corpus_manifest": {
            "path": str(corpus_path),
            "sha256": sha256_file(corpus_path),
        },
        "binary": binary_provenance(binary),
        "git": git_provenance(),
        "platform": {
            "system": platform.system(),
            "release": platform.release(),
            "machine": platform.machine(),
            "python": platform.python_version(),
        },
        "warmup": None,
        "scenarios": [],
    }
    write_json_atomic(json_out, report)

    if not args.quiet:
        print(
            f"run_rando_benchmarks: suite={args.suite} "
            f"generator_version={benchmark['generator_version']}"
        )
        print(f"  binary={binary}")

    # One suite warmup only. It primes executable/file-system caches without
    # doubling every long-running complex scenario; each sample is a fresh
    # process, so per-scenario in-process warmup would provide no benefit.
    warmup_scenario = selected[0]
    warmup_sample = warmup_scenario["samples"][0]
    warmup = run_sample(
        binary, warmup_sample,
        scenario_timeout_seconds(),
        warmup=True,
    )
    report["warmup"] = warmup
    write_json_atomic(json_out, report)
    if warmup["status"] != "passed":
        report["status"] = "failed"
        report["error"] = f"suite warmup failed: {warmup.get('error', 'unknown error')}"
        report["completed_at_utc"] = datetime.now(timezone.utc).isoformat()
        write_json_atomic(json_out, report)
        print(f"FAIL: {report['error']}", file=sys.stderr)
        return 1

    failures = 0
    for scenario in selected:
        scenario_report: dict[str, Any] = {
            "name": scenario["name"],
            "statistic": scenario["statistic"],
            "budget_ms": scenario["budget_ms"],
            "gating": scenario["gating"],
            "timeout_s": scenario_timeout_seconds(),
            "status": "running",
            "samples": [],
        }
        report["scenarios"].append(scenario_report)
        write_json_atomic(json_out, report)
        if not args.quiet:
            mode = "gate" if scenario["gating"] else "telemetry"
            print(
                f"  {scenario['name']} [{mode}]: {scenario['statistic']} <= "
                f"{scenario['budget_ms']:.0f} ms "
                f"({len(scenario['samples'])} sample(s))"
            )

        for sample in scenario["samples"]:
            sample_result = run_sample(
                binary, sample, scenario_report["timeout_s"]
            )
            scenario_report["samples"].append(sample_result)
            write_json_atomic(json_out, report)
            if not args.quiet:
                if sample_result["status"] == "passed":
                    print(
                        f"    {sample_result['corpus_label']}: "
                        f"{sample_result['external_wall_ms']:.1f} ms external, "
                        f"{sample_result['generation_wall_clock_ms']} ms generator"
                    )
                else:
                    print(
                        f"    {sample_result['corpus_label']}: FAIL -- "
                        f"{sample_result.get('error', 'unknown error')}"
                    )

        passed_samples = [
            sample for sample in scenario_report["samples"]
            if sample["status"] == "passed"
        ]
        values = [float(sample["external_wall_ms"]) for sample in passed_samples]
        if len(passed_samples) != len(scenario_report["samples"]):
            scenario_report["status"] = "failed"
            scenario_report["error"] = "one or more samples failed"
            failures += 1
        else:
            score = (statistics.median(values)
                     if scenario["statistic"] == "median" else max(values))
            scenario_report["measured_ms"] = round(score, 3)
            scenario_report["median_ms"] = round(statistics.median(values), 3)
            scenario_report["p95_ms"] = round(percentile_95(values), 3)
            scenario_report["max_ms"] = round(max(values), 3)
            if score > scenario["budget_ms"] and scenario["gating"]:
                scenario_report["status"] = "failed"
                scenario_report["error"] = (
                    f"{scenario['statistic']} {score:.1f} ms exceeds "
                    f"budget {scenario['budget_ms']:.1f} ms"
                )
                failures += 1
            elif not scenario["gating"]:
                scenario_report["status"] = "observed"
                scenario_report["calibration"] = (
                    "telemetry-only until three clean target-environment runs are recorded"
                )
                if score > scenario["budget_ms"]:
                    scenario_report["warning"] = (
                        f"provisional {scenario['statistic']} {score:.1f} ms exceeds "
                        f"provisional budget {scenario['budget_ms']:.1f} ms"
                    )
            else:
                scenario_report["status"] = "passed"
        write_json_atomic(json_out, report)

    report["status"] = "failed" if failures else "passed"
    report["completed_at_utc"] = datetime.now(timezone.utc).isoformat()
    report["summary"] = {
        "scenarios": len(selected),
        "failed": failures,
        "passed": sum(s["status"] == "passed" for s in report["scenarios"]),
        "observed": sum(s["status"] == "observed" for s in report["scenarios"]),
    }
    write_json_atomic(json_out, report)
    if failures:
        print(f"run_rando_benchmarks: FAIL -- {failures} scenario(s) failed")
        return 1
    print(f"run_rando_benchmarks: PASS -- {len(selected)} scenario(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
