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
    if "seed_u64" not in entry:
        errors.append(f"entry {idx}: missing 'seed_u64'")
    if "expected_digest" not in entry:
        errors.append(f"entry {idx}: missing 'expected_digest' (SHA-256 of placement table)")
    elif not isinstance(entry["expected_digest"], str) or len(entry["expected_digest"]) != 64:
        errors.append(f"entry {idx}: 'expected_digest' must be a 64-char hex string")
    return errors


def run_activated(binary: Path, manifest: dict) -> int:
    if not binary.exists():
        print(f"run_rando_corpus: binary {binary} not found. Build first.")
        return 1
    # The binary's batch form writes N spoiler JSONs to a directory.
    # We then read each, recompute its placement digest, compare to expected.
    # TODO: implement once 1.6a / 12.2 land.
    print(f"run_rando_corpus: activated path TODO — manifest has {len(manifest['entries'])} entries.")
    print(f"  Binary: {binary}")
    print("  Implementation pending tasks 1.6a (--generate-seed --manifest) and 12.2 (corpus content).")
    return 0


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=Path("./zelda3"))
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
