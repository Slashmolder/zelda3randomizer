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
  python assets/scripts/run_rando_corpus.py --binary=./zelda3 --skip-pot-shuffle --skip-enemy-drop-checks
"""
from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass, field
import json
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

MANIFEST = Path("tests/rando_corpus/manifest.yaml")


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


@dataclass
class CorpusResult:
    idx: int
    label: str
    lines: list[str] = field(default_factory=list)
    failed: bool = False
    skipped: bool = False


def _emit_result(result: CorpusResult) -> tuple[int, int]:
    for line in result.lines:
        print(line)
    return (1 if result.failed else 0, 1 if result.skipped else 0)


def _run_one_entry(binary: Path, idx: int, entry: dict,
                   skip_pot_shuffle: bool,
                   skip_enemy_drop_checks: bool,
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
    if skip_reasons:
        result.lines.append(
            f"  SKIP [{idx}] {label}: {'+'.join(skip_reasons)} entry "
            f"(local ROM-derived registry required)"
        )
        result.skipped = True
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
            if len(buf) != 139:
                result.lines.append(
                    f"  FAIL [{idx}] {label}: ZRSR file size {len(buf)} != 139"
                )
                result.failed = True
                return result
            # Validate CRC32 (LE u32 at offset 135 over bytes 0..134).
            # Keep this in lockstep with rando_spoiler.h constants.
            disk_crc = int.from_bytes(buf[135:139], "little")
            calc_crc = zlib.crc32(buf[:135]) & 0xffffffff
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


def run_activated(binary: Path, manifest: dict, skip_pot_shuffle: bool = False,
                  skip_enemy_drop_checks: bool = False, jobs: int = 1,
                  timeout: int = 60) -> int:
    # Resolve to an absolute path: `Path("./zelda3")` stringifies back to
    # "zelda3" (pathlib strips the leading "./"), so subprocess would PATH-search
    # for it and fail on Linux/macOS (cwd isn't on PATH). An absolute path runs
    # the built binary directly.
    binary = binary.resolve()
    if not binary.exists():
        print(f"run_rando_corpus: binary {binary} not found. Build first.")
        return 1

    entries = manifest["entries"]
    jobs = max(1, jobs)
    timeout = max(1, timeout)
    failures = 0
    skipped = 0

    if jobs == 1:
        for idx, entry in enumerate(entries):
            result = _run_one_entry(binary, idx, entry, skip_pot_shuffle,
                                    skip_enemy_drop_checks, timeout)
            failed_delta, skipped_delta = _emit_result(result)
            failures += failed_delta
            skipped += skipped_delta
    else:
        pending: dict[int, CorpusResult] = {}
        next_to_emit = 0
        with ThreadPoolExecutor(max_workers=jobs) as executor:
            future_to_idx = {
                executor.submit(_run_one_entry, binary, idx, entry,
                                skip_pot_shuffle, skip_enemy_drop_checks,
                                timeout): idx
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
                while next_to_emit in pending:
                    result = pending.pop(next_to_emit)
                    failed_delta, skipped_delta = _emit_result(result)
                    failures += failed_delta
                    skipped += skipped_delta
                    next_to_emit += 1

    if failures:
        print(f"\nrun_rando_corpus: {failures} of {len(entries) - skipped} "
              f"run entries FAILED ({skipped} skipped).")
        return 1
    if skipped:
        print(f"\nrun_rando_corpus: all {len(entries) - skipped} run entries OK "
              f"({skipped} local-registry entries skipped).")
    else:
        print(f"\nrun_rando_corpus: all {len(entries)} entries OK.")
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
    parser.add_argument("--jobs", type=int, default=1,
                        help="number of corpus entries to run concurrently "
                             "(default: 1).")
    parser.add_argument("--timeout", type=int, default=120,
                        help="per-entry generator timeout in seconds "
                             "(default: 120 -- legit hard door-layout seeds "
                             "run ~55s; 60s flaked under contention).")
    args = parser.parse_args(argv)

    data = load_manifest(args.manifest)
    entries = data.get("entries", [])

    if not args.quiet:
        print(f"run_rando_corpus: manifest {args.manifest}")
        print(f"  generator_version: {data.get('generator_version', '(unset)')}")
        print(f"  entries: {len(entries)}")
        if args.jobs != 1:
            print(f"  jobs: {max(1, args.jobs)}")
        if args.timeout != 60:
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
                         args.skip_enemy_drop_checks, args.jobs, args.timeout)


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
