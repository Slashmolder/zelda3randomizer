#!/usr/bin/env python3
"""Fail the build if a large inline data blob is committed.

Generated/extracted data belongs in gitignored artifacts, not inlined into
source as hex. Flags any run of adjacent pure-hex string literals (joined by
concatenation glue) reaching HEX_RUN_THRESHOLD chars — above a SHA-256 (64),
below a real data blob. Allow a true positive with an `embedded-data-guard:
allow <reason>` comment on or just above the line.

Usage: python assets/scripts/check_no_embedded_data.py [--root DIR]
"""
from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

# 64 bytes. Above any single SHA-256 (64 hex) / asset hash; far below a real
# inline data blob.
HEX_RUN_THRESHOLD = 128

ALLOW_MARKER = "embedded-data-guard: allow"

# String literals: "..." or '...' (no escapes expected inside hex blobs).
_LITERAL_RE = re.compile(r"""(['"])((?:(?!\1).)*)\1""")
_HEX_RE = re.compile(r"\A[0-9a-fA-F]+\Z")
# Characters allowed *between* two adjacent hex literals for them to count as
# one concatenated blob (Python implicit/`+` concat, C "" "" concat, etc.).
_GLUE_RE = re.compile(r"\A[\s+(),\\]*\Z")

# Only scan textual source/data files. Binary/asset blobs are gitignored anyway.
_TEXT_SUFFIXES = {
    ".py", ".c", ".h", ".cpp", ".cc", ".hpp", ".inc",
    ".yaml", ".yml", ".json", ".txt", ".md", ".cfg", ".ini",
}


def _tracked_files(root: Path) -> list[Path]:
    out = subprocess.run(
        ["git", "ls-files", "-z"],
        cwd=root, capture_output=True, text=True, check=True,
    ).stdout
    return [root / p for p in out.split("\0") if p]


def _line_of(text: str, pos: int) -> int:
    return text.count("\n", 0, pos) + 1


def scan_file(path: Path) -> list[tuple[int, int]]:
    """Return [(line, hex_len)] for each suspected inline data blob."""
    try:
        text = path.read_text(encoding="utf-8", errors="strict")
    except (UnicodeDecodeError, OSError):
        return []  # not UTF-8 text — skip
    lines = text.splitlines()

    hits: list[tuple[int, int]] = []
    run_len = 0
    run_start_pos = 0
    prev_end = -1
    for m in _LITERAL_RE.finditer(text):
        content = m.group(2)
        is_hex = len(content) >= 8 and len(content) % 2 == 0 and bool(_HEX_RE.match(content))
        if is_hex and run_len > 0 and _GLUE_RE.match(text[prev_end:m.start()]):
            run_len += len(content)          # extend current concatenated run
        elif is_hex:
            run_len = len(content)           # start a new run
            run_start_pos = m.start()
        else:
            run_len = 0                      # non-hex literal breaks the run
        prev_end = m.end()

        if run_len >= HEX_RUN_THRESHOLD:
            ln = _line_of(text, run_start_pos)
            # Honor an allow marker on the run's first line or up to 3 lines
            # above it.
            window = "\n".join(lines[max(0, ln - 4):ln])
            if ALLOW_MARKER not in window:
                hits.append((ln, run_len))
            run_len = 0  # report once per run
    return hits


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--root", default=".", help="repo root (default: cwd)")
    args = ap.parse_args(argv)
    root = Path(args.root).resolve()
    self_path = Path(__file__).resolve()

    violations: list[tuple[Path, int, int]] = []
    for f in _tracked_files(root):
        if f.suffix.lower() not in _TEXT_SUFFIXES:
            continue
        if f.resolve() == self_path:
            continue  # this file documents the pattern in prose
        for ln, hex_len in scan_file(f):
            violations.append((f, ln, hex_len))

    if violations:
        print("check_no_embedded_data: suspected inline data blob(s) committed:")
        for f, ln, hex_len in violations:
            try:
                rel = f.relative_to(root)
            except ValueError:
                rel = f
            print(f"  {rel}:{ln}  — {hex_len} hex chars ({hex_len // 2} bytes) "
                  f"of concatenated hex string literals")
        print(
            "\nGenerated/extracted data belongs in a gitignored build artifact, "
            "read at runtime/codegen (see assets/chest_data.py), not inlined into "
            "source. If this data genuinely belongs in source, add an "
            f"'{ALLOW_MARKER} <reason>' comment on/above the line.")
        return 1

    print("check_no_embedded_data: clean — no inline data blobs found.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
