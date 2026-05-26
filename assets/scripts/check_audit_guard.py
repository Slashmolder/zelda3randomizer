#!/usr/bin/env python3
"""Audit guard (tasks.md §1.0g, §6.10).

Rejects new writes to inventory-state cells (``link_item_*``, ``link_bottle_info[*]``,
``link_has_crystals``, etc.) outside the documented dispatch path or the
exemption list in ``openspec/changes/add-randomizer-support/audit.md``.

The guard is the long-term enforcement for the Phase 0 audit's discipline:
once §6 instrumentation lands, every grant-site write goes through
``Rando_OnLocationCheck`` (or carries an explicit exemption comment). Any new
write that doesn't match either pattern is a regression.

**A0 status (scaffold)**: ``audit.md`` exists but acceptance checks 0.8a-e
aren't all ticked yet. Until they are, the guard cannot consult a stable
exemption list, so it runs in **report-only mode** (logs new writes but
returns zero). When 0.8e ticks (§6 unblocked), flip ``--strict`` on in the
CI workflow.

Usage:
  python assets/scripts/check_audit_guard.py             # report-only
  python assets/scripts/check_audit_guard.py --strict    # fail on violation
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

SRC_DIR = Path("src")
AUDIT_MD = Path("openspec/changes/add-randomizer-support/audit.md")
EXEMPTION_COMMENT = "rando-exempt:"

# Cells the audit tracks. Mirrors audit.md §0.1.1 dispatch-table cells.
TRACKED_CELLS = [
    "link_item_bow",
    "link_item_boomerang",
    "link_item_hookshot",
    "link_item_bombs",
    "link_item_mushroom",
    "link_item_fire_rod",
    "link_item_ice_rod",
    "link_item_bombos_medallion",
    "link_item_ether_medallion",
    "link_item_quake_medallion",
    "link_item_torch",
    "link_item_hammer",
    "link_item_flute",
    "link_item_bug_net",
    "link_item_book_of_mudora",
    "link_item_cane_somaria",
    "link_item_cane_byrna",
    "link_item_cape",
    "link_item_mirror",
    "link_item_gloves",
    "link_item_boots",
    "link_item_flippers",
    "link_item_moon_pearl",
    "link_sword_type",
    "link_shield_type",
    "link_armor",
    "link_has_crystals",
    "link_which_pendants",
    "link_num_keys",
    "link_heart_pieces",
    "link_health_capacity",
    "link_magic_consumption",
    "link_bomb_upgrades",
    "link_arrow_upgrades",
    # link_bottle_info is an array; matched separately below.
]

WRITE_RE = re.compile(
    # ``=(?!=)`` excludes ``==`` and the left half of ``===``.
    # Plain ``=`` matches assignment; compound assigns (+=, etc.) match
    # directly since they don't collide with comparators.
    r"\b(" + "|".join(TRACKED_CELLS) + r")\s*(?:=(?!=)|\+=|-=|\|=|&=|\^=|\+\+|--)"
)
BOTTLE_WRITE_RE = re.compile(r"\blink_bottle_info\s*\[[^\]]+\]\s*(?:=(?!=)|\+=|-=|\|=|&=|\^=)")

# Dispatch funnel patterns — these are "blessed" writes the audit knows about.
DISPATCH_CONTEXT_PATTERNS = [
    re.compile(r"Rando_OnLocationCheck\s*\("),
    re.compile(r"Link_ReceiveItem\s*\("),
    re.compile(r"AncillaAdd_ItemReceipt\s*\("),
]


def is_exempted(file_lines: list[str], lineno_zero_based: int) -> bool:
    """Check if the line carries an explicit ``// rando-exempt:`` comment."""
    line = file_lines[lineno_zero_based]
    if EXEMPTION_COMMENT in line:
        return True
    # Also honor the comment on the preceding line (more common style)
    if lineno_zero_based > 0 and EXEMPTION_COMMENT in file_lines[lineno_zero_based - 1]:
        return True
    return False


def is_in_dispatch_context(file_lines: list[str], lineno_zero_based: int) -> bool:
    """Check if the surrounding ~12 lines mention a dispatch funnel call."""
    start = max(0, lineno_zero_based - 12)
    end = min(len(file_lines), lineno_zero_based + 4)
    window = "\n".join(file_lines[start:end])
    return any(p.search(window) for p in DISPATCH_CONTEXT_PATTERNS)


def is_consumption_or_arithmetic(line: str) -> bool:
    """Heuristic: ``cell -= n`` / ``cell--`` are consumption, not grants."""
    return bool(re.search(r"\b(?:link_item_bombs|link_num_arrows)\s*(?:--|-=)", line))


def scan_file(path: Path) -> list[tuple[int, str, str]]:
    """Return [(lineno, line, reason)] for each tracked write site found."""
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except OSError as exc:
        print(f"warning: cannot read {path}: {exc}", file=sys.stderr)
        return []
    lines = text.splitlines()
    hits: list[tuple[int, str, str]] = []
    for idx, line in enumerate(lines):
        if is_consumption_or_arithmetic(line):
            continue
        match = WRITE_RE.search(line) or BOTTLE_WRITE_RE.search(line)
        if not match:
            continue
        if is_exempted(lines, idx):
            continue
        if is_in_dispatch_context(lines, idx):
            continue
        cell = match.group(1) if match.re is WRITE_RE else "link_bottle_info[*]"
        hits.append((idx + 1, line.rstrip(), f"write to {cell} outside dispatch / exemption"))
    return hits


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--strict", action="store_true", help="fail on violation (audit.md 0.8e must have ticked)")
    parser.add_argument("--quiet", action="store_true")
    args = parser.parse_args(argv)

    if not SRC_DIR.exists():
        print(f"check_audit_guard: {SRC_DIR} not found.")
        return 1

    if not AUDIT_MD.exists():
        print(f"check_audit_guard: {AUDIT_MD} missing — cannot consult exemption list.")
        return 1 if args.strict else 0

    files = sorted(SRC_DIR.glob("*.c"))
    total = 0
    for path in files:
        for lineno, line, reason in scan_file(path):
            total += 1
            if not args.quiet:
                tag = "[error]" if args.strict else "[report-only]"
                print(f"{tag} {path}:{lineno}: {reason}")
                print(f"  > {line}")

    if total:
        if not args.quiet:
            print(
                f"\ncheck_audit_guard: {total} non-exempt write(s) to tracked cells.\n"
                "  Per tasks.md §6.10, every grant-site write SHALL flow through Rando_OnLocationCheck\n"
                "  or carry an explicit ``// rando-exempt: <reason>`` comment per audit.md."
            )
        return 1 if args.strict else 0

    if not args.quiet:
        print(f"check_audit_guard: {len(files)} file(s) scanned, no non-exempt writes.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
