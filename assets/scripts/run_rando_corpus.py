#!/usr/bin/env python3
"""Regression-corpus runner (tasks.md §1.0a, §12.1-12.3).

Reads the corpus manifest at ``tests/rando_corpus/manifest.yaml``, regenerates
every (settings, seed_u64) pair via the binary's ``--generate-seed`` mode in
batch form (one binary launch per platform), and diffs the resulting placement
table SHA-256 digest against the expected value recorded in the manifest.

**A0 status (scaffold)**: until task 1.6a lands ``--generate-seed`` and task
12.2 fills the corpus, this script:
  - Validates manifest schema (catches manifest-format bugs early)
  - Reports counts (manifest entries vs. expected digests)
  - Exits clean on an empty or absent manifest

**Activation**: once the binary supports ``--generate-seed --manifest=<yaml>``
and the manifest has entries, the script invokes the binary, captures the JSON
spoiler digests, and diffs against expected.

Usage (A0):
  python assets/scripts/run_rando_corpus.py

Usage (activated):
  python assets/scripts/run_rando_corpus.py --binary=./zelda3
  python assets/scripts/run_rando_corpus.py --binary=./zelda3 --skip-pot-shuffle
  python assets/scripts/run_rando_corpus.py --binary=./zelda3 --skip-pot-shuffle --skip-enemy-drop-checks --skip-enemy-souls --skip-terrain-shuffle --skip-ow-warp
"""
from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass, field
from datetime import datetime, timezone
import hashlib
import json
import os
import platform
import subprocess
import sys
import tempfile
import time
import zlib
from pathlib import Path

MANIFEST = Path("tests/rando_corpus/manifest.yaml")
REPO = Path(__file__).resolve().parents[2]
ENEMY_DROP_REGISTRY = REPO / "assets" / "rando" / "enemy_drops.gen.yaml"
ENEMY_CHECK_REGISTRY = REPO / "assets" / "rando" / "enemy_checks.gen.yaml"
GEN_ENEMY_DROP = REPO / "assets" / "scripts" / "gen_enemy_drop_tables.py"
GEN_ENEMY_CHECK = REPO / "assets" / "scripts" / "gen_enemy_check_tables.py"
RANDO_LOGIC_GEN = REPO / "assets" / "rando_logic_gen.py"
ENEMY_COMPILED_ARTIFACTS = (
    REPO / "src" / "rando" / "logic_data.c",
    REPO / "src" / "rando" / "enemy_drop_lookup.h",
    REPO / "src" / "rando" / "enemy_check_lookup.h",
)


def load_manifest(path: Path) -> dict:
    if not path.exists():
        return {"entries": [], "generator_version": None}
    try:
        import yaml  # type: ignore
    except ImportError:
        # In CI we install pyyaml; locally users can run the build pipeline
        # which depends on it (per requirements.txt).
        print("error: PyYAML not installed (pip install -r requirements.txt).")
        sys.exit(2)
    with path.open("r", encoding="utf-8") as fp:
        data = yaml.safe_load(fp) or {}
    if "entries" not in data:
        data["entries"] = []
    return data


def validate_entry(entry: dict, idx: int) -> list[str]:
    errors = []
    if "settings" not in entry:
        errors.append(f"entry {idx}: missing 'settings'")
    # Canonical key is `seed` per randomizer-core / CLI manifest schema;
    # accept legacy `seed_u64` for backward compatibility.
    if "seed" not in entry and "seed_u64" not in entry:
        errors.append(f"entry {idx}: missing 'seed' (quoted uint64 string)")
    if "expected_digest" not in entry:
        errors.append(f"entry {idx}: missing 'expected_digest' (SHA-256 of placement table)")
    elif not isinstance(entry["expected_digest"], str) or len(entry["expected_digest"]) != 64:
        errors.append(f"entry {idx}: 'expected_digest' must be a 64-char hex string")
    # expected_sphere_digest is optional; when present, validate format.
    if "expected_sphere_digest" in entry:
        sd = entry["expected_sphere_digest"]
        if not isinstance(sd, str) or len(sd) != 64:
            errors.append(f"entry {idx}: 'expected_sphere_digest' must be a 64-char hex string")
    return errors


def _setting_truthy(v) -> bool:
    if isinstance(v, str):
        return v.lower() not in ("", "0", "off", "false", "none", "vanilla")
    return bool(v)


def entry_needs_local_pot_registry(entry: dict) -> bool:
    """True when this corpus row can activate pot locations at generation time.

    Public CI runs without the ROM-derived local pot registry, so it must skip
    real pot-shuffle rows. Rows whose settings normalize pot_shuffle off should
    still run there; the cave-forced-off corpus entries exist specifically to
    lock that normalization.
    """
    settings = entry.get("settings", {}) or {}
    if not _setting_truthy(settings.get("pot_shuffle", "off")):
        return False
    state = str(settings.get("mode.state", "open")).lower()
    cave_shuffle = _setting_truthy(settings.get("shuffle_cave_entrances", False))
    if cave_shuffle and state in ("open", "standard"):
        return False
    return True


def entry_uses_enemy_drop_checks(entry: dict) -> bool:
    settings = entry.get("settings", {}) or {}
    v = settings.get("enemy_drop_checks", "off")
    if isinstance(v, str):
        return v.lower() not in ("", "0", "off", "false", "none")
    return bool(v)


def entry_uses_terrain_shuffle(entry: dict) -> bool:
    """True when grass_shuffle, rock_shuffle, or bonk_shuffle is active.

    Public CI runs without the ROM-derived local registries (terrain.gen.yaml,
    bonk.gen.yaml), so it must skip these rows — the activation guard fails
    such a slot closed, and generation aborts, on a registry-less build.
    add-rando-grass-rock-shuffle; bonk_shuffle is the same terrain-family
    axis with the same local-artifact dependency (add-rando-bonk-sanity —
    review P1: CI previously ran the four bonk rows against an EMPTY
    registry).
    """
    settings = entry.get("settings", {}) or {}
    for axis in ("grass_shuffle", "rock_shuffle", "bonk_shuffle"):
        if _setting_truthy(settings.get(axis, "off")):
            return True
    return False


def entry_uses_ow_warp(entry: dict) -> bool:
    """True when a flute-spot or whirlpool warp axis is active.

    Public CI builds without the gitignored OW screen-component graph
    (assets/rando/ow_graph.gen.yaml → baked kRandoOwGraph* tables), and
    OwWarp_Compute refuses warp-axis generation fail-closed when the tables
    are absent/empty (add-rando-ow-warp-shuffle). Skip these rows on
    assetless runs; local checks run the full corpus.
    """
    settings = entry.get("settings", {}) or {}
    flute = settings.get("flute_shuffle", "off")
    if isinstance(flute, str):
        if flute.lower() not in ("", "0", "off", "false", "none"):
            return True
    elif bool(flute):
        return True
    return _setting_truthy(settings.get("whirlpool_shuffle", False))


def entry_needs_local_soul_rooms(entry: dict) -> bool:
    """True when this row needs the gitignored kill-room soul data.

    Public CI builds without assets/rando/soul_rooms.gen.yaml. The generator
    intentionally refuses enemies-tier souls (souls_shuffle=all) without those
    ROM-derived wraps, but door shuffle normalizes souls off and must still run
    to prove the degrade behavior.
    """
    settings = entry.get("settings", {}) or {}
    souls = str(settings.get("souls_shuffle", "off")).lower()
    if souls != "all":
        return False
    if _setting_truthy(settings.get("door_shuffle", "vanilla")):
        return False
    return True


def corpus_uses_enemy_drop_checks(entries: list[dict]) -> bool:
    return any(entry_uses_enemy_drop_checks(entry) for entry in entries)


def _print_captured_output(proc: subprocess.CompletedProcess[str]) -> None:
    if proc.stdout:
        print(proc.stdout.rstrip())
    if proc.stderr:
        print(proc.stderr.rstrip(), file=sys.stderr)


def _run_preflight_step(label: str, cmd: list[str], timeout: int) -> bool:
    try:
        proc = subprocess.run(
            cmd, cwd=str(REPO), text=True, capture_output=True,
            timeout=timeout,
        )
    except subprocess.TimeoutExpired as exc:
        print(f"run_rando_corpus: enemy-registry preflight timed out: {label}")
        if exc.stdout:
            print(str(exc.stdout).rstrip())
        if exc.stderr:
            print(str(exc.stderr).rstrip(), file=sys.stderr)
        return False
    if proc.returncode == 0:
        return True
    print(f"run_rando_corpus: enemy-registry preflight failed: {label}")
    print(f"  command: {' '.join(cmd)}")
    _print_captured_output(proc)
    return False


def verify_enemy_drop_registries(binary: Path, timeout: int) -> bool:
    """Fail early when full enemy-drop corpus rows would use stale local data.

    The enemy-drop/check registries are gitignored ROM-derived inputs. If they
    predate generator logic, the corpus can report digest drift even though the
    tracked manifest is correct. Before running non-skipped enemy-drop rows,
    validate those registries against a fresh key-depth dump from the same
    binary, regenerate tracked C/H outputs with write-if-changed semantics, and
    make sure the binary was rebuilt after any output that actually changed.
    """
    missing = [p for p in (ENEMY_DROP_REGISTRY, ENEMY_CHECK_REGISTRY)
               if not p.exists()]
    if missing:
        print("run_rando_corpus: local enemy-drop registry artifact(s) missing:")
        for p in missing:
            print(f"  missing: {p.relative_to(REPO)}")
        print("  Run: python assets/scripts/run_rando_local_checks.py "
              "--binary=<built zelda3.exe> --prepare-only")
        print("  Or pass --skip-enemy-drop-checks to run the assetless subset.")
        return False

    with tempfile.TemporaryDirectory() as td:
        key_depth = Path(td) / "key_depth.txt"
        steps = [
            ("dump key-depth ground truth",
             [str(binary), "--dump-key-depth", str(key_depth)]),
            ("gen_enemy_drop_tables --check",
             [sys.executable, str(GEN_ENEMY_DROP),
              "--key-depth", str(key_depth), "--check"]),
            ("gen_enemy_check_tables --check",
             [sys.executable, str(GEN_ENEMY_CHECK),
              "--key-depth", str(key_depth), "--check"]),
            ("rando_logic_gen --strict",
             [sys.executable, str(RANDO_LOGIC_GEN), "--strict"]),
        ]
        for label, cmd in steps:
            if not _run_preflight_step(label, cmd, timeout):
                print("  Refresh with: python assets/scripts/run_rando_local_checks.py "
                      "--binary=<built zelda3.exe> --prepare-only")
                print("  Then rebuild the binary before rerunning the full corpus.")
                return False

    try:
        binary_mtime = binary.stat().st_mtime
    except OSError as exc:
        print(f"run_rando_corpus: could not stat binary {binary}: {exc}")
        return False
    newer = [
        p for p in ENEMY_COMPILED_ARTIFACTS
        if p.exists() and p.stat().st_mtime > binary_mtime
    ]
    if newer:
        print("run_rando_corpus: binary is older than changed enemy-drop "
              "compiled artifact(s); rebuild before running the full corpus.")
        for p in newer:
            print(f"  newer than binary: {p.relative_to(REPO)}")
        return False

    return True


@dataclass
class CorpusResult:
    idx: int
    label: str
    lines: list[str] = field(default_factory=list)
    failed: bool = False
    skipped: bool = False
    skip_reasons: list[str] = field(default_factory=list)
    elapsed_ms: float = 0.0


def _emit_result(result: CorpusResult) -> tuple[int, int]:
    for line in result.lines:
        print(line)
    return (1 if result.failed else 0, 1 if result.skipped else 0)


def _run_one_entry_impl(binary: Path, idx: int, entry: dict,
                        skip_pot_shuffle: bool,
                        skip_enemy_drop_checks: bool,
                        skip_enemy_souls: bool,
                        skip_terrain_shuffle: bool,
                        skip_ow_warp: bool,
                        timeout: int) -> CorpusResult:
    settings = entry.get("settings", {})
    seed = str(entry.get("seed", entry.get("seed_u64", "")))
    expected = entry.get("expected_digest", "")
    expected_sphere = entry.get("expected_sphere_digest", "")
    label = entry.get("label", f"entry-{idx}")
    result = CorpusResult(idx=idx, label=label)
    skip_reasons = []
    if skip_pot_shuffle and entry_needs_local_pot_registry(entry):
        skip_reasons.append("pot_shuffle")
    if skip_enemy_drop_checks and entry_uses_enemy_drop_checks(entry):
        skip_reasons.append("enemy_drop_checks")
    if skip_enemy_souls and entry_needs_local_soul_rooms(entry):
        skip_reasons.append("souls_shuffle=all")
    if skip_terrain_shuffle and entry_uses_terrain_shuffle(entry):
        skip_reasons.append("terrain_shuffle")
    if skip_ow_warp and entry_uses_ow_warp(entry):
        skip_reasons.append("ow_warp")
    if skip_reasons:
        result.lines.append(
            f"  SKIP [{idx}] {label}: {'+'.join(skip_reasons)} entry "
            f"(local ROM-derived data required)"
        )
        result.skipped = True
        result.skip_reasons = skip_reasons
        return result
    settings_csv = ",".join(f"{k}={v}" for k, v in settings.items())

    with tempfile.TemporaryDirectory() as td:
        out_json = Path(td) / "out.json"
        try:
            subprocess.run(
                [str(binary), "--generate-seed",
                 f"--settings={settings_csv}",
                 f"--seed={seed}",
                 f"--out-spoiler={out_json}"],
                check=True, capture_output=True, timeout=timeout,
            )
        except (subprocess.CalledProcessError, subprocess.TimeoutExpired) as e:
            result.lines.append(f"  FAIL [{idx}] {label}: generator error: {e}")
            result.failed = True
            return result
        if not out_json.exists():
            result.lines.append(f"  FAIL [{idx}] {label}: spoiler not written")
            result.failed = True
            return result
        # Phase B Slice 6 §7.4 — race-mode entries emit a fixed-size
        # suppressed binary (magic ZRSR). Verify by reading the file,
        # checking the magic + CRC32, then invoking --reveal-spoiler
        # on a sibling copy to confirm the stamp matches the
        # regenerated placement. The original file is left intact.
        buf = out_json.read_bytes()
        if buf[:4] == b"ZRSR":
            if len(buf) != 141:
                result.lines.append(
                    f"  FAIL [{idx}] {label}: ZRSR file size {len(buf)} != 141"
                )
                result.failed = True
                return result
            # Validate CRC32 (LE u32 at offset 137 over bytes 0..136).
            # Keep this in lockstep with rando_spoiler.h constants.
            disk_crc = int.from_bytes(buf[137:141], "little")
            calc_crc = zlib.crc32(buf[:137]) & 0xffffffff
            if disk_crc != calc_crc:
                result.lines.append(
                    f"  FAIL [{idx}] {label}: ZRSR CRC mismatch "
                    f"(disk {disk_crc:#x} != calc {calc_crc:#x})"
                )
                result.failed = True
                return result
            # Reveal round-trip: copy → reveal → confirm exit 0.
            reveal_path = Path(td) / "reveal_target.json"
            reveal_path.write_bytes(buf)
            try:
                subprocess.run(
                    [str(binary), f"--reveal-spoiler={reveal_path}"],
                    check=True, capture_output=True, timeout=timeout,
                )
            except (subprocess.CalledProcessError, subprocess.TimeoutExpired) as e:
                result.lines.append(f"  FAIL [{idx}] {label}: reveal failed: {e}")
                result.failed = True
                return result
            # On success the file has been overwritten with full JSON.
            # Confirm we can parse it and the placement_digest is sane.
            try:
                revealed = json.loads(reveal_path.read_text(encoding="utf-8"))
            except Exception as exc:
                result.lines.append(
                    f"  FAIL [{idx}] {label}: revealed JSON unparseable: {exc}"
                )
                result.failed = True
                return result
            got_pd = revealed.get("meta", {}).get("placement_digest_hex", "")
            got_sphere_pd = revealed.get("meta", {}).get("sphere_digest", "")
            if expected and got_pd != expected:
                result.lines.append(
                    f"  FAIL [{idx}] {label}: revealed placement_digest "
                    f"mismatch: expected {expected[:16]}, got {got_pd[:16]}"
                )
                result.failed = True
                return result
            # also check sphere
            # digest. Previously skipped in the ZRSR sub-path, leaving
            # the manifest's expected_sphere_digest for race-mode entries
            # unenforced.
            if expected_sphere and got_sphere_pd != expected_sphere:
                result.lines.append(
                    f"  FAIL [{idx}] {label}: revealed sphere_digest "
                    f"mismatch: expected {expected_sphere[:16]}, "
                    f"got {got_sphere_pd[:16]}"
                )
                result.failed = True
                return result
            result.lines.append(
                f"  OK   [{idx}] {label}: ZRSR roundtrip OK "
                f"(placement_digest {got_pd[:16] if got_pd else 'unchecked'}...)"
            )
            return result
        try:
            spoiler = json.loads(buf.decode("utf-8"))
        except Exception as exc:
            result.lines.append(f"  FAIL [{idx}] {label}: spoiler JSON unparseable: {exc}")
            result.failed = True
            return result
        got = spoiler.get("meta", {}).get("placement_digest_hex", "")
        got_sphere = spoiler.get("meta", {}).get("sphere_digest", "")

    if got != expected:
        result.lines.append(f"  FAIL [{idx}] {label}: placement_digest mismatch")
        result.lines.append(f"    expected {expected}")
        result.lines.append(f"    got      {got}")
        result.failed = True
    elif expected_sphere and got_sphere != expected_sphere:
        result.lines.append(f"  FAIL [{idx}] {label}: sphere_digest mismatch (placement OK)")
        result.lines.append(f"    expected sphere {expected_sphere}")
        result.lines.append(f"    got sphere      {got_sphere}")
        result.failed = True
    else:
        result.lines.append(f"  OK   [{idx}] {label}: {got[:16]}...")
    return result


def _run_one_entry(binary: Path, idx: int, entry: dict,
                   skip_pot_shuffle: bool,
                   skip_enemy_drop_checks: bool,
                   skip_enemy_souls: bool,
                   skip_terrain_shuffle: bool,
                   skip_ow_warp: bool,
                   timeout: int) -> CorpusResult:
    """Run and time one row inside its worker, including reveal/verification.

    Timing here deliberately excludes executor queue time. Under parallel corpus
    execution it still includes CPU/I/O contention, so it is telemetry rather
    than a cross-runner aggregate performance gate.
    """
    label = entry.get("label", f"entry-{idx}")
    started = time.perf_counter()
    try:
        result = _run_one_entry_impl(
            binary, idx, entry, skip_pot_shuffle, skip_enemy_drop_checks,
            skip_enemy_souls, skip_terrain_shuffle, skip_ow_warp, timeout,
        )
    except Exception as exc:
        result = CorpusResult(
            idx=idx,
            label=label,
            failed=True,
            lines=[f"  FAIL [{idx}] {label}: runner error: {exc}"],
        )
    result.elapsed_ms = (time.perf_counter() - started) * 1000.0
    return result


def _sha256_file(path: Path) -> str | None:
    if not path.exists() or not path.is_file():
        return None
    digest = hashlib.sha256()
    with path.open("rb") as fp:
        for block in iter(lambda: fp.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _git_provenance() -> dict:
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


def _binary_provenance(binary: Path) -> dict:
    if not binary.exists() or not binary.is_file():
        return {"path": str(binary), "exists": False}
    stat = binary.stat()
    return {
        "path": str(binary),
        "exists": True,
        "size": stat.st_size,
        "mtime_ns": stat.st_mtime_ns,
        "sha256": _sha256_file(binary),
    }


def _result_to_json(result: CorpusResult) -> dict:
    status = "skipped" if result.skipped else "failed" if result.failed else "passed"
    return {
        "index": result.idx,
        "label": result.label,
        "status": status,
        "elapsed_ms": round(result.elapsed_ms, 3),
        "skip_reasons": list(result.skip_reasons),
        "messages": list(result.lines),
    }


def _write_json_atomic(path: Path | None, report: dict) -> None:
    if path is None:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(path.name + ".tmp")
    tmp.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n",
                   encoding="utf-8")
    os.replace(tmp, path)


def _print_slowest(results: list[CorpusResult]) -> None:
    attempted = [result for result in results if not result.skipped]
    if not attempted:
        return
    slowest = sorted(attempted, key=lambda result: result.elapsed_ms,
                     reverse=True)[:10]
    print("\nrun_rando_corpus: slowest rows (worker elapsed; telemetry only)")
    for result in slowest:
        status = "FAIL" if result.failed else "OK"
        print(f"  {result.elapsed_ms:10.1f} ms  {status:4s}  "
              f"[{result.idx}] {result.label}")


def run_activated(binary: Path, manifest: dict, skip_pot_shuffle: bool = False,
                  skip_enemy_drop_checks: bool = False,
                  skip_enemy_souls: bool = False,
                  skip_terrain_shuffle: bool = False,
                  skip_ow_warp: bool = False, jobs: int = 1,
                  timeout: int = 60, timings_json: Path | None = None,
                  manifest_path: Path = MANIFEST) -> int:
    # Resolve to an absolute path: `Path("./zelda3")` stringifies back to
    # "zelda3" (pathlib strips the leading "./"), so subprocess would PATH-search
    # for it and fail on Linux/macOS (cwd isn't on PATH). An absolute path runs
    # the built binary directly.
    binary = binary.resolve()
    manifest_path = manifest_path.resolve()
    if timings_json is not None:
        timings_json = timings_json.resolve()
    started_at = datetime.now(timezone.utc).isoformat()
    started_perf = time.perf_counter()
    results_by_index: list[CorpusResult | None] = [None] * len(manifest["entries"])
    report = {
        "format_version": 1,
        "tool": "run_rando_corpus.py",
        "status": "running",
        "started_at_utc": started_at,
        "generator_version": manifest.get("generator_version"),
        "manifest": {
            "path": str(manifest_path),
            "sha256": _sha256_file(manifest_path),
        },
        "binary": _binary_provenance(binary),
        "git": _git_provenance(),
        "platform": {
            "system": platform.system(),
            "release": platform.release(),
            "machine": platform.machine(),
            "python": platform.python_version(),
        },
        "execution": {
            "jobs": max(1, jobs),
            "timeout_s": max(1, timeout),
            "skip_pot_shuffle": skip_pot_shuffle,
            "skip_enemy_drop_checks": skip_enemy_drop_checks,
            "skip_enemy_souls": skip_enemy_souls,
            "skip_terrain_shuffle": skip_terrain_shuffle,
            "skip_ow_warp": skip_ow_warp,
        },
        "results": [],
    }

    def persist(status: str = "running", error: str | None = None,
                completed: bool = False) -> None:
        completed_results = [result for result in results_by_index if result is not None]
        report["status"] = status
        report["results"] = [_result_to_json(result) for result in completed_results]
        report["summary"] = {
            "total_manifest_entries": len(results_by_index),
            "completed": len(completed_results),
            "passed": sum(not result.failed and not result.skipped
                          for result in completed_results),
            "failed": sum(result.failed for result in completed_results),
            "skipped": sum(result.skipped for result in completed_results),
            "elapsed_ms": round((time.perf_counter() - started_perf) * 1000.0, 3),
        }
        attempted = [result for result in completed_results if not result.skipped]
        report["slowest"] = [
            _result_to_json(result)
            for result in sorted(attempted, key=lambda item: item.elapsed_ms,
                                 reverse=True)[:10]
        ]
        if error is not None:
            report["error"] = error
        else:
            report.pop("error", None)
        if completed:
            report["completed_at_utc"] = datetime.now(timezone.utc).isoformat()
        _write_json_atomic(timings_json, report)

    persist()
    if not binary.exists():
        error = f"binary {binary} not found. Build first."
        print(f"run_rando_corpus: {error}")
        persist("failed", error, completed=True)
        return 1

    entries = manifest["entries"]
    jobs = max(1, jobs)
    timeout = max(1, timeout)
    failures = 0
    skipped = 0

    if not skip_enemy_drop_checks and corpus_uses_enemy_drop_checks(entries):
        print("run_rando_corpus: checking local enemy-drop registries")
        if not verify_enemy_drop_registries(binary, max(timeout, 120)):
            persist("failed", "local enemy-drop registry preflight failed",
                    completed=True)
            return 1

    if jobs == 1:
        for idx, entry in enumerate(entries):
            result = _run_one_entry(binary, idx, entry, skip_pot_shuffle,
                                    skip_enemy_drop_checks, skip_enemy_souls,
                                    skip_terrain_shuffle, skip_ow_warp, timeout)
            results_by_index[idx] = result
            failed_delta, skipped_delta = _emit_result(result)
            failures += failed_delta
            skipped += skipped_delta
            persist()
    else:
        pending: dict[int, CorpusResult] = {}
        next_to_emit = 0
        with ThreadPoolExecutor(max_workers=jobs) as executor:
            future_to_idx = {
                executor.submit(_run_one_entry, binary, idx, entry,
                                skip_pot_shuffle, skip_enemy_drop_checks,
                                skip_enemy_souls, skip_terrain_shuffle,
                                skip_ow_warp, timeout): idx
                for idx, entry in enumerate(entries)
            }
            for future in as_completed(future_to_idx):
                idx = future_to_idx[future]
                try:
                    result = future.result()
                except Exception as exc:
                    label = entries[idx].get("label", f"entry-{idx}")
                    result = CorpusResult(
                        idx=idx,
                        label=label,
                        failed=True,
                        lines=[f"  FAIL [{idx}] {label}: runner error: {exc}"],
                    )
                pending[idx] = result
                results_by_index[idx] = result
                persist()
                while next_to_emit in pending:
                    result = pending.pop(next_to_emit)
                    failed_delta, skipped_delta = _emit_result(result)
                    failures += failed_delta
                    skipped += skipped_delta
                    next_to_emit += 1

    completed_results = [result for result in results_by_index if result is not None]
    _print_slowest(completed_results)
    if failures:
        print(f"\nrun_rando_corpus: {failures} of {len(entries) - skipped} "
              f"run entries FAILED ({skipped} skipped).")
        persist("failed", f"{failures} corpus row(s) failed", completed=True)
        return 1
    if skipped:
        print(f"\nrun_rando_corpus: all {len(entries) - skipped} run entries OK "
              f"({skipped} local-registry entries skipped).")
    else:
        print(f"\nrun_rando_corpus: all {len(entries)} entries OK.")
    persist("passed", completed=True)
    return 0


def find_binary_default() -> Path:
    """Look for the built zelda3 binary in common locations.

    Per `build_commands` memory: the MSBuild Release/x64 artifact lands at
    `bin/x64-Release/zelda3.exe`. Plain `make` lands at `./zelda3` (Linux/macOS).
    Returns the first one that exists; falls back to `./zelda3` (legacy
    default) if neither is found, so the error message still surfaces.
    """
    candidates = [
        Path("bin") / "x64-Release" / "zelda3.exe",  # MSBuild Release x64
        Path("./zelda3.exe"),                         # Repo-root copy (Windows)
        Path("./zelda3"),                             # Linux/macOS make target
    ]
    for c in candidates:
        if c.exists():
            return c
    return Path("./zelda3")  # legacy default; error path will print it


def default_jobs() -> int:
    return max(1, min(8, os.cpu_count() or 1))


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=find_binary_default())
    parser.add_argument("--manifest", type=Path, default=MANIFEST)
    parser.add_argument("--quiet", action="store_true")
    parser.add_argument("--schema-only", action="store_true",
                        help="validate the manifest schema and exit 0 without "
                             "launching the binary. Used by the no-build "
                             "source-guards CI job; the determinism job runs the "
                             "full corpus with --binary.")
    parser.add_argument("--skip-pot-shuffle", action="store_true",
                        help="skip entries that request pot_shuffle. Public CI "
                             "uses this when the local ROM-derived pot registry "
                             "is absent; local checks run the full corpus.")
    parser.add_argument("--skip-enemy-drop-checks", action="store_true",
                        help="skip entries that request enemy_drop_checks. Public "
                             "CI uses this when the local ROM-derived enemy "
                             "registries are absent; local checks run the full "
                             "corpus.")
    parser.add_argument("--skip-enemy-souls", action="store_true",
                        help="skip entries that require souls_shuffle=all. Public "
                             "CI uses this when the local ROM-derived "
                             "soul_rooms.gen.yaml data is absent; local checks "
                             "run the full corpus.")
    parser.add_argument("--skip-terrain-shuffle", action="store_true",
                        help="skip entries that request grass_shuffle/rock_shuffle/bonk_shuffle. "
                             "Public CI uses this when the local ROM-derived "
                             "terrain registry (terrain.gen.yaml) is absent; local "
                             "checks run the full corpus.")
    parser.add_argument("--skip-ow-warp", action="store_true",
                        help="skip entries that request flute_shuffle/whirlpool_shuffle. "
                             "Public CI uses this when the gitignored OW graph "
                             "(ow_graph.gen.yaml) is absent — the generator refuses "
                             "warp axes fail-closed without it; local checks run "
                             "the full corpus.")
    parser.add_argument("--jobs", type=int, default=default_jobs(),
                        help="number of corpus entries to run concurrently "
                             "(default: min(8, CPU count)).")
    parser.add_argument("--timeout", type=int, default=120,
                        help="per-entry generator timeout in seconds "
                             "(default: 120 -- legit hard door-layout seeds "
                             "run ~55s; 60s flaked under contention).")
    parser.add_argument("--timings-json", type=Path, default=None,
                        help="optional per-row timing/provenance JSON path; "
                             "updated after each completed row so failures "
                             "leave a useful partial report")
    args = parser.parse_args(argv)

    data = load_manifest(args.manifest)
    entries = data.get("entries", [])

    if not args.quiet:
        print(f"run_rando_corpus: manifest {args.manifest}")
        print(f"  generator_version: {data.get('generator_version', '(unset)')}")
        print(f"  entries: {len(entries)}")
        if args.jobs != 1:
            print(f"  jobs: {max(1, args.jobs)}")
        if args.timeout != 120:
            print(f"  timeout: {max(1, args.timeout)}s")

    # Schema check (catches manifest-format bugs early).
    all_errors: list[str] = []
    for idx, entry in enumerate(entries):
        all_errors.extend(validate_entry(entry, idx))
    if all_errors:
        for err in all_errors:
            print(f"  schema error: {err}")
        return 1

    # Schema-only mode: the manifest validated cleanly above; stop before the
    # binary-dependent run. Lets the no-build source-guards job gate manifest
    # format without a compiled binary present.
    if args.schema_only:
        if not args.quiet:
            print("run_rando_corpus: --schema-only — manifest schema OK, "
                  "skipping binary run.")
        return 0

    if not entries:
        if not args.quiet:
            print("run_rando_corpus: empty manifest — A0 scaffold pass.")
        return 0

    return run_activated(args.binary, data, args.skip_pot_shuffle,
                         args.skip_enemy_drop_checks, args.skip_enemy_souls,
                         args.skip_terrain_shuffle, args.skip_ow_warp,
                         args.jobs, args.timeout,
                         args.timings_json, args.manifest)


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
