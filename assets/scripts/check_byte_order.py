#!/usr/bin/env python3
"""Byte-order pin grep for src/rando/ (tasks.md §1.0c).

Per ``randomizer-core / Byte-order pin``: all on-disk multi-byte fields
(share strings, settings hashes, slot headers, snapshot tail) SHALL be
little-endian-serialized explicitly. ``htobe*``/``be*toh``/``__builtin_bswap``
are forbidden inside ``src/rando/``.

Empty ``src/rando/`` is a clean pass — the guard activates the moment the
first randomizer source file lands.

Usage:
  python assets/scripts/check_byte_order.py
  python assets/scripts/check_byte_order.py --quiet
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

RANDO_DIR = Path("src/rando")

FORBIDDEN = [
    (r"\bhtobe(16|32|64)\b", "htobe* — explicit LE serialization required instead"),
    (r"\bhtole(16|32|64)\b", "htole* — use plain shifts; macro is non-portable across OSes"),
    (r"\bbe(16|32|64)toh\b", "be*toh — explicit LE serialization required instead"),
    (r"\ble(16|32|64)toh\b", "le*toh — use plain shifts; macro is non-portable across OSes"),
    (r"\b__builtin_bswap(16|32|64)\b", "__builtin_bswap* — explicit byte assembly required"),
    (r"\bntohs\b|\bntohl\b|\bhtons\b|\bhtonl\b", "BSD network-byte-order macros — forbidden"),
]


def strip_comments(line: str) -> str:
    idx = line.find("//")
    if idx >= 0:
        line = line[:idx]
    return line


def scan_file(path: Path) -> list[tuple[int, str, str]]:
    violations: list[tuple[int, str, str]] = []
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except OSError as exc:
        print(f"warning: cannot read {path}: {exc}", file=sys.stderr)
        return violations
    for lineno, raw_line in enumerate(text.splitlines(), start=1):
        cleaned = strip_comments(raw_line)
        for pattern, reason in FORBIDDEN:
            if re.search(pattern, cleaned):
                violations.append((lineno, raw_line.rstrip(), reason))
    return violations


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--quiet", action="store_true")
    parser.add_argument("--root", type=Path, default=RANDO_DIR)
    args = parser.parse_args(argv)

    if not args.root.exists():
        if not args.quiet:
            print(f"check_byte_order: {args.root} does not exist yet — clean pass (A0 scaffold).")
        return 0

    files = sorted(p for p in args.root.rglob("*") if p.suffix in (".c", ".h"))
    if not files:
        if not args.quiet:
            print(f"check_byte_order: no .c/.h files under {args.root} — clean pass.")
        return 0

    total = 0
    for path in files:
        for lineno, line, reason in scan_file(path):
            total += 1
            if not args.quiet:
                print(f"{path}:{lineno}: {reason}")
                print(f"  > {line}")

    if total:
        if not args.quiet:
            print(f"\ncheck_byte_order: {total} violation(s) in src/rando/.")
            print("Per randomizer-core / Byte-order pin, these macros are forbidden.")
        return 1

    if not args.quiet:
        print(f"check_byte_order: {len(files)} file(s) scanned, no violations.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
