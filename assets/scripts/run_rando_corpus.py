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
"""
from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
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


def run_activated(binary: Path, manifest: dict) -> int:
    if not binary.exists():
        print(f"run_rando_corpus: binary {binary} not found. Build first.")
        return 1
    import json
    import subprocess
    import tempfile

    failures = 0
    for idx, entry in enumerate(manifest["entries"]):
        settings = entry.get("settings", {})
        seed = str(entry.get("seed", entry.get("seed_u64", "")))
        expected = entry.get("expected_digest", "")
        expected_sphere = entry.get("expected_sphere_digest", "")
        label = entry.get("label", f"entry-{idx}")
        settings_csv = ",".join(f"{k}={v}" for k, v in settings.items())

        with tempfile.TemporaryDirectory() as td:
            out_json = Path(td) / "out.json"
            try:
                subprocess.run(
                    [str(binary), "--generate-seed",
                     f"--settings={settings_csv}",
                     f"--seed={seed}",
                     f"--out-spoiler={out_json}"],
                    check=True, capture_output=True, timeout=60,
                )
            except (subprocess.CalledProcessError, subprocess.TimeoutExpired) as e:
                print(f"  FAIL [{idx}] {label}: generator error: {e}")
                failures += 1
                continue
            if not out_json.exists():
                print(f"  FAIL [{idx}] {label}: spoiler not written")
                failures += 1
                continue
            # Phase B Slice 6 §7.4 — race-mode entries emit a 134-byte
            # suppressed binary (magic ZRSR). Verify by reading the file,
            # checking the magic + CRC32, then invoking --reveal-spoiler
            # on a sibling copy to confirm the stamp matches the
            # regenerated placement. The original file is left intact.
            head = out_json.read_bytes()[:4]
            if head == b"ZRSR":
                buf = out_json.read_bytes()
                if len(buf) != 138:
                    print(f"  FAIL [{idx}] {label}: ZRSR file size {len(buf)} != 138")
                    failures += 1
                    continue
                # Validate CRC32 (LE u32 at offset 134 over bytes 0..133).
                # (kGenVer 14 §66 grew the file 134→138 to absorb the wider
                # canonical settings layout.)
                import zlib
                disk_crc = int.from_bytes(buf[134:138], "little")
                calc_crc = zlib.crc32(buf[:134]) & 0xffffffff
                if disk_crc != calc_crc:
                    print(f"  FAIL [{idx}] {label}: ZRSR CRC mismatch "
                          f"(disk {disk_crc:#x} != calc {calc_crc:#x})")
                    failures += 1
                    continue
                # Reveal round-trip: copy → reveal → confirm exit 0.
                reveal_path = Path(td) / "reveal_target.json"
                reveal_path.write_bytes(buf)
                try:
                    subprocess.run(
                        [str(binary), f"--reveal-spoiler={reveal_path}"],
                        check=True, capture_output=True, timeout=60,
                    )
                except (subprocess.CalledProcessError, subprocess.TimeoutExpired) as e:
                    print(f"  FAIL [{idx}] {label}: reveal failed: {e}")
                    failures += 1
                    continue
                # On success the file has been overwritten with full JSON.
                # Confirm we can parse it and the placement_digest is sane.
                try:
                    revealed = json.loads(reveal_path.read_text(encoding="utf-8"))
                except Exception as exc:
                    print(f"  FAIL [{idx}] {label}: revealed JSON unparseable: {exc}")
                    failures += 1
                    continue
                got_pd = revealed.get("meta", {}).get("placement_digest_hex", "")
                got_sphere_pd = revealed.get("meta", {}).get("sphere_digest", "")
                if expected and got_pd != expected:
                    print(f"  FAIL [{idx}] {label}: revealed placement_digest "
                          f"mismatch: expected {expected[:16]}, got {got_pd[:16]}")
                    failures += 1
                    continue
                # Cluster-audit M2 (of e9f20ad audit): also check sphere
                # digest. Previously skipped in the ZRSR sub-path, leaving
                # the manifest's expected_sphere_digest for race-mode entries
                # unenforced.
                if expected_sphere and got_sphere_pd != expected_sphere:
                    print(f"  FAIL [{idx}] {label}: revealed sphere_digest "
                          f"mismatch: expected {expected_sphere[:16]}, "
                          f"got {got_sphere_pd[:16]}")
                    failures += 1
                    continue
                print(f"  OK   [{idx}] {label}: ZRSR roundtrip OK "
                      f"(placement_digest {got_pd[:16] if got_pd else 'unchecked'}...)")
                continue
            spoiler = json.loads(out_json.read_text(encoding="utf-8"))
            got = spoiler.get("meta", {}).get("placement_digest_hex", "")
            got_sphere = spoiler.get("meta", {}).get("sphere_digest", "")

        if got != expected:
            print(f"  FAIL [{idx}] {label}: placement_digest mismatch")
            print(f"    expected {expected}")
            print(f"    got      {got}")
            failures += 1
        elif expected_sphere and got_sphere != expected_sphere:
            print(f"  FAIL [{idx}] {label}: sphere_digest mismatch (placement OK)")
            print(f"    expected sphere {expected_sphere}")
            print(f"    got sphere      {got_sphere}")
            failures += 1
        else:
            print(f"  OK   [{idx}] {label}: {got[:16]}...")

    if failures:
        print(f"\nrun_rando_corpus: {failures} of {len(manifest['entries'])} entries FAILED.")
        return 1
    print(f"\nrun_rando_corpus: all {len(manifest['entries'])} entries OK.")
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
    args = parser.parse_args(argv)

    data = load_manifest(args.manifest)
    entries = data.get("entries", [])

    if not args.quiet:
        print(f"run_rando_corpus: manifest {args.manifest}")
        print(f"  generator_version: {data.get('generator_version', '(unset)')}")
        print(f"  entries: {len(entries)}")

    # Schema check (catches manifest-format bugs early).
    all_errors: list[str] = []
    for idx, entry in enumerate(entries):
        all_errors.extend(validate_entry(entry, idx))
    if all_errors:
        for err in all_errors:
            print(f"  schema error: {err}")
        return 1

    if not entries:
        if not args.quiet:
            print("run_rando_corpus: empty manifest — A0 scaffold pass.")
        return 0

    return run_activated(args.binary, data)


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
