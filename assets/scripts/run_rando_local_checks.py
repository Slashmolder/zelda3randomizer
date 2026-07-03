#!/usr/bin/env python3
"""Local randomizer pre-merge checks.

This runner is intentionally local-only: it includes checks that need a built
binary plus ROM-derived/extracted artifacts that public CI cannot have. In
particular, it refreshes the gitignored pot ground-truth dump from the binary,
regenerates the local pot registries, and regenerates rando codegen from them.

Usage:
  python assets/scripts/run_rando_local_checks.py
  python assets/scripts/run_rando_local_checks.py --binary=bin/x64-Release/zelda3.exe
  python assets/scripts/run_rando_local_checks.py --skip-corpus
  python assets/scripts/run_rando_local_checks.py --prepare-only
"""
from __future__ import annotations

import argparse
import hashlib
import re
import subprocess
import sys
from pathlib import Path

import yaml


REPO = Path(__file__).resolve().parents[2]
POT_DUMP = REPO / "assets" / "rando" / "pot_dump.gen.txt"
CHEST_TABLE = REPO / "assets" / "rando" / "chest_table.gen.bin"
DEFAULT_TMP = REPO / "tmp" / "local-rando-checks"
POT_REGISTRY = REPO / "assets" / "rando" / "pots.gen.yaml"
POT_KEY_DEPTH = REPO / "assets" / "rando" / "pot_key_depth.gen.yaml"
ENEMY_DROP_REGISTRY = REPO / "assets" / "rando" / "enemy_drops.gen.yaml"
ENEMY_CHECK_REGISTRY = REPO / "assets" / "rando" / "enemy_checks.gen.yaml"
ITEM_REGISTRY = REPO / "assets" / "rando" / "item_registry.yaml"
POT_NONPOT_DROP_COUNTS = REPO / "src" / "rando" / "pot_nonpot_drop_counts.h"
CODEGEN_OUTPUTS = [
    REPO / "src" / "rando" / "logic_data.c",
    REPO / "src" / "rando" / "location_ids.h",
    REPO / "src" / "rando" / "item_ids.h",
    REPO / "src" / "rando" / "chest_lookup.h",
    REPO / "src" / "rando" / "pot_lookup.h",
    REPO / "src" / "rando" / "enemy_drop_lookup.h",
    REPO / "src" / "rando" / "enemy_check_lookup.h",
    POT_NONPOT_DROP_COUNTS,
    REPO / "src" / "rando" / "icon_atlas.h",
    REPO / "src" / "rando" / "direct_grant_icons.h",
]


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


def snapshot(paths: list[Path]) -> dict[Path, bytes | None]:
    out: dict[Path, bytes | None] = {}
    for path in paths:
        if path.exists():
            out[path] = hashlib.sha256(path.read_bytes()).digest()
        else:
            out[path] = None
    return out


def refresh_pot_codegen(binary: Path, tmp: Path) -> int:
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
        ("gen_pot_tables",
         [sys.executable, "assets/scripts/gen_pot_tables.py"]),
        ("gen_pot_tables --check",
         [sys.executable, "assets/scripts/gen_pot_tables.py", "--check"]),
        ("gen_pot_tables --check-overrides",
         [sys.executable, "assets/scripts/gen_pot_tables.py", "--check-overrides"]),
        ("dump key-depth ground truth",
         [str(binary), "--dump-key-depth", str(key_depth)]),
        ("gen_pot_key_depth",
         [sys.executable, "assets/scripts/gen_pot_key_depth.py",
          "--dump", str(key_depth)]),
        ("gen_pot_key_depth --check",
         [sys.executable, "assets/scripts/gen_pot_key_depth.py",
          "--dump", str(key_depth), "--check"]),
        ("gen_enemy_drop_tables",
         [sys.executable, "assets/scripts/gen_enemy_drop_tables.py",
          "--key-depth", str(key_depth)]),
        ("gen_enemy_drop_tables --check",
         [sys.executable, "assets/scripts/gen_enemy_drop_tables.py",
          "--key-depth", str(key_depth), "--check"]),
        ("gen_enemy_check_tables",
         [sys.executable, "assets/scripts/gen_enemy_check_tables.py",
          "--key-depth", str(key_depth)]),
        ("gen_enemy_check_tables --check",
         [sys.executable, "assets/scripts/gen_enemy_check_tables.py",
          "--key-depth", str(key_depth), "--check"]),
        ("rando_logic_gen --strict",
         [sys.executable, "assets/rando_logic_gen.py", "--strict"]),
        ("audit_enemy_check_candidates",
         [sys.executable, "assets/scripts/audit_enemy_check_candidates.py",
          "--include-rows", "--allow-cannot-key", "--allow-flying"]),
        ("audit_enemy_check_candidates --check",
         [sys.executable, "assets/scripts/audit_enemy_check_candidates.py",
          "--include-rows", "--allow-cannot-key", "--allow-flying", "--check"]),
    ]

    for label, cmd in checks:
        rc = run(label, cmd)
        if rc:
            return rc
    return 0


def check_pot_nonpot_drop_counts() -> int:
    """Verify generated C free-grant rows match pot_key_depth.gen.yaml.

    This is intentionally local-only: both inputs are gitignored products of the
    ROM-backed pot workflow. It avoids a second hand-maintained truth table while
    still catching stale or partially regenerated codegen outputs.
    """
    for path, how in [
        (POT_KEY_DEPTH, "Run local pot prepare to regenerate pot_key_depth.gen.yaml."),
        (POT_NONPOT_DROP_COUNTS, "Run assets/rando_logic_gen.py after pot key-depth generation."),
        (ITEM_REGISTRY, "assets/rando/item_registry.yaml is required for item-id ordering."),
    ]:
        rc = require_file(path, how)
        if rc:
            return rc

    items_doc = yaml.safe_load(ITEM_REGISTRY.read_text(encoding="utf-8"))
    item_ids = {raw["name"]: int(raw["id"]) for raw in items_doc.get("items", [])}

    depth_doc = yaml.safe_load(POT_KEY_DEPTH.read_text(encoding="utf-8"))
    rows = depth_doc.get("nonpot_drops")
    if rows is None:
        print("run_rando_local_checks: pot_key_depth.gen.yaml is missing nonpot_drops.")
        print("Run assets/scripts/gen_pot_key_depth.py, then assets/rando_logic_gen.py.")
        return 2

    try:
        expected = sorted(
            [(raw["item"], int(raw["count"])) for raw in rows],
            key=lambda row: item_ids[row[0]],
        )
    except KeyError as e:
        print(f"run_rando_local_checks: unknown nonpot_drops item {e!s}.")
        return 2

    text = POT_NONPOT_DROP_COUNTS.read_text(encoding="utf-8", errors="replace")
    actual = []
    for m in re.finditer(r"\{\s*ITEM_([A-Za-z0-9_]+)\s*,\s*(\d+)\s*\}", text):
        actual.append((m.group(1), int(m.group(2))))

    if actual != expected:
        print("run_rando_local_checks: pot_nonpot_drop_counts.h does not match pot_key_depth.gen.yaml.")
        print(f"  expected: {expected}")
        print(f"  actual:   {actual}")
        print("Regenerate with assets/rando_logic_gen.py after gen_pot_key_depth.py.")
        return 1

    print("\n==> pot nonpot drop-count codegen guard", flush=True)
    print(f"matched {len(actual)} generated free-grant row(s)", flush=True)
    return 0


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
    parser.add_argument("--prepare-only", action="store_true",
                        help="Refresh local pot artifacts and rando codegen, then stop.")
    parser.add_argument("--skip-prepare", action="store_true",
                        help="Assume local pot artifacts/codegen are already fresh.")
    args = parser.parse_args(argv)

    binary = resolve_repo_path(args.binary)
    tmp = resolve_repo_path(args.tmp)

    if not binary.exists():
        print(f"run_rando_local_checks: binary {binary} not found. "
              f"Build first or pass --binary.")
        return 2

    tracked_codegen = [
        POT_REGISTRY,
        POT_KEY_DEPTH,
        ENEMY_DROP_REGISTRY,
        ENEMY_CHECK_REGISTRY,
    ] + CODEGEN_OUTPUTS
    if not args.skip_prepare:
        before = snapshot(tracked_codegen)
        rc = refresh_pot_codegen(binary, tmp)
        if rc:
            return rc
        rc = check_pot_nonpot_drop_counts()
        if rc:
            return rc
        if args.prepare_only:
            print("\nrun_rando_local_checks: local rando codegen prepared", flush=True)
            return 0
        after = snapshot(tracked_codegen)
        if before != after:
            print(
                "\nrun_rando_local_checks: local rando registries/codegen changed. "
                "Rebuild the binary and rerun this script with --skip-prepare, "
                "or use `make rando-local-checks` on a Make build.",
                flush=True,
            )
            return 3
    else:
        rc = check_pot_nonpot_drop_counts()
        if rc:
            return rc

    checks = []

    if not args.skip_selftest:
        checks.append(("rando self-test", [str(binary), "--rando-selftest"]))
    checks.append(("slot-path generation guard",
                   [sys.executable, "assets/scripts/check_rando_slot_path.py",
                    "--binary", str(binary)]))
    checks.append(("rando invariant sweep",
                   [sys.executable, "assets/scripts/check_rando_invariants.py",
                    "--binary", str(binary)]))
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
