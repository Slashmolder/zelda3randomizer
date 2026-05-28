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
VARIABLES_H = Path("src/variables.h")
AUDIT_MD = Path("openspec/changes/add-randomizer-support/audit.md")
EXEMPTION_COMMENT = "rando-exempt:"

# Functions that resolve a pointer from a dispatch table (e.g.,
# `kMemoryLocationToGiveItemTo[j]`) and then write through `*p`. Any
# unflagged `*p[?]\s*[|&^+-]?=` write inside a function whose body
# mentions one of these names is suspicious because the regex-based
# direct-cell match above can't catch indirect writes. See memory note
# `audit_guard_indirect_writes` and §7.7 of add-randomizer-support
# audit (the pendant double-grant in misc.c:739).
INDIRECT_DISPATCH_TABLES = [
    "kMemoryLocationToGiveItemTo",
    "kValueToGiveItemTo",
]

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
    "link_bigkey",
    "link_dungeon_map",
    "link_compass",
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

# Raw-offset writes (e.g. ``g_ram[0xF374] |= 1`` or ``*(uint8*)(g_ram+0xF374) = 1``)
# bypass the symbol-name regex above. Parsed from variables.h at scan time so
# the offsets stay in lockstep with the C-side macro definitions.
# Cluster-audit (post-e9f20ad) memory note `audit_guard_indirect_writes`.
DEFINE_RE = re.compile(
    r"^\s*#define\s+([a-zA-Z_]\w*)\s+\(\s*\*\s*\(\s*u?int\d+_?\s*\*\s*\)\s*\(\s*g_ram\s*\+\s*0x([0-9a-fA-F]+)\s*\)\s*\)"
)


def parse_tracked_offsets() -> dict[int, str]:
    """Read variables.h and return {offset: symbol} for each TRACKED_CELLS macro.

    The symbol-name regex catches plain assignments like ``link_item_bow = 5``
    but misses raw-address writes through the same offset (e.g. callers that
    spell out ``g_ram+0xF340``). Building a separate offset → symbol map lets
    the raw-write regex below cover both shapes uniformly.
    """
    if not VARIABLES_H.exists():
        return {}
    tracked = set(TRACKED_CELLS) | {"link_num_arrows"}
    out: dict[int, str] = {}
    for line in VARIABLES_H.read_text(encoding="utf-8", errors="replace").splitlines():
        m = DEFINE_RE.match(line)
        if not m:
            continue
        name = m.group(1)
        if name not in tracked:
            continue
        out[int(m.group(2), 16)] = name
    return out


def build_raw_write_regex(offsets: dict[int, str]) -> re.Pattern[str] | None:
    """Build a regex matching raw writes to any tracked offset.

    Patterns matched (case-insensitive on the hex part):
        g_ram[0xNNNN] = ...
        g_ram[0xNNNN] |= ...
        *(uint8*)(g_ram + 0xNNNN) = ...
        *(g_ram + 0xNNNN) = ...
    """
    if not offsets:
        return None
    # Build the offset alternation. Each offset is matched in hex (with or
    # without 0x prefix tolerance — but DEFINE_RE forces 0x so we mirror).
    hex_alt = "|".join(f"{off:X}" for off in sorted(offsets, reverse=True))
    # Two write shapes: bracket-indexed g_ram[0xNNNN] and pointer-deref
    # ``*(<cast>)(g_ram + 0xNNNN)``. Both followed by an assignment op.
    pattern = (
        r"g_ram\s*(?:\[\s*0x(?:" + hex_alt + r")\s*\]"
        r"|\+\s*0x(?:" + hex_alt + r")\s*\))"
        r"\s*(?:=(?!=)|\+=|-=|\|=|&=|\^=)"
    )
    return re.compile(pattern, re.IGNORECASE)

# Dispatch funnel patterns — these are "blessed" writes the audit knows about.
DISPATCH_CONTEXT_PATTERNS = [
    re.compile(r"Rando_OnLocationCheck\s*\("),
    re.compile(r"Link_ReceiveItem\s*\("),
    re.compile(r"AncillaAdd_ItemReceipt\s*\("),
]


def is_exempted(file_lines: list[str], lineno_zero_based: int) -> bool:
    """Check if the line carries an explicit ``// rando-exempt:`` comment.

    Accepts the marker on the same line OR within the preceding contiguous
    comment block of up to 12 lines (audit rationales often span several
    sentences). The walk-back stops at any blank or non-comment line, so the
    marker has to be in a comment block that's contiguous with the write.
    """
    line = file_lines[lineno_zero_based]
    if EXEMPTION_COMMENT in line:
        return True
    # Walk backward through contiguous comment lines (// ...) up to 12 lines.
    for delta in range(1, 13):
        idx = lineno_zero_based - delta
        if idx < 0:
            break
        prev = file_lines[idx].lstrip()
        if not prev.startswith("//"):
            # Allow whitespace-only line as a step between comment and code,
            # but stop at non-comment non-blank.
            if prev == "":
                continue
            break
        if EXEMPTION_COMMENT in prev:
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


def scan_file(path: Path, raw_re: re.Pattern[str] | None) -> list[tuple[int, str, str]]:
    """Return [(lineno, line, reason)] for each tracked write site found.

    Three classes of write are caught:
      1. Symbol-name writes (``WRITE_RE``): ``link_item_bow = ...`` etc.
      2. Bottle-array writes (``BOTTLE_WRITE_RE``): ``link_bottle_info[k] = ...``.
      3. Raw-offset writes (``raw_re``, built from variables.h at startup):
         ``g_ram[0xF340] |= ...`` or ``*(uint8*)(g_ram+0xF340) = ...``.
         Catches the indirect-write failure mode the §7.7 pendant
         double-grant bug exposed.
    """
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
        sym_match = WRITE_RE.search(line)
        bottle_match = BOTTLE_WRITE_RE.search(line) if not sym_match else None
        raw_match = raw_re.search(line) if (raw_re is not None and not sym_match and not bottle_match) else None
        match = sym_match or bottle_match or raw_match
        if not match:
            continue
        if is_exempted(lines, idx):
            continue
        if is_in_dispatch_context(lines, idx):
            continue
        if sym_match:
            cell = sym_match.group(1)
        elif bottle_match:
            cell = "link_bottle_info[*]"
        else:
            cell = "raw g_ram offset"
        hits.append((idx + 1, line.rstrip(), f"write to {cell} outside dispatch / exemption"))
    return hits


def scan_indirect_dispatch_warnings(files: list[Path]) -> list[tuple[Path, str]]:
    """List files that reference an indirect-write dispatch table.

    The regex-based scan above misses writes through pointers like
    ``*p = ...`` where ``p`` came from ``kMemoryLocationToGiveItemTo[j]``.
    Flag those files as "manual audit needed" — auditor confirms every
    ``*p`` write in the function carries either a dispatch context or
    an exemption comment.
    """
    out: list[tuple[Path, str]] = []
    for path in files:
        try:
            text = path.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        for name in INDIRECT_DISPATCH_TABLES:
            if name in text:
                out.append((path, name))
                break
    return out


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

    tracked_offsets = parse_tracked_offsets()
    raw_re = build_raw_write_regex(tracked_offsets)
    if not args.quiet and not tracked_offsets:
        print("check_audit_guard: warning — could not parse variables.h; raw-offset scan disabled.")

    # Recursive scan picks up `src/platform/*/.c` and any future
    # subdirectory writers. `src/rando/*.c` is excluded because its
    # entire purpose IS the dispatch path — those files own
    # `Rando_OnLocationCheck` and the per-class direct-grant helpers
    # (`prize_item_direct_grant`, `magic_upgrade_direct_grant`, etc.).
    # The guard's job is to catch vanilla-side writes that DIDN'T
    # route through rando, not to police rando's own implementation.
    # Indirect-dispatch advisory still surfaces `src/rando/rando.c`
    # for its `kValueToGiveItemTo` reference, so the auditor's eyeball
    # path still covers it.
    # Audit-of-audit LOW-1 of phase-b (e9f20ad cluster-audit follow-on).
    all_c = sorted(SRC_DIR.rglob("*.c"))
    files = [p for p in all_c if "rando" not in p.parts]
    total = 0
    for path in files:
        for lineno, line, reason in scan_file(path, raw_re):
            total += 1
            if not args.quiet:
                tag = "[error]" if args.strict else "[report-only]"
                print(f"{tag} {path}:{lineno}: {reason}")
                print(f"  > {line}")

    # Indirect-dispatch advisory — not a hard error, but flags files
    # the auditor must eyeball for ``*p`` writes through dispatch
    # tables that the regex above can't catch.
    # Pass the full file set (including src/rando) so rando-side
    # references to the dispatch tables also surface in the advisory.
    indirect_files = scan_indirect_dispatch_warnings(all_c)
    if indirect_files and not args.quiet:
        print()
        print("check_audit_guard: indirect-dispatch-table references detected.")
        print("  The regex scan above catches direct symbol-name + raw-offset writes,")
        print("  but `*p = ...` writes via these dispatch-table pointers need manual")
        print("  eyeball. See memory note `audit_guard_indirect_writes` and §7.7 of")
        print("  add-randomizer-support audit (misc.c:739 pendant double-grant).")
        for path, tbl in indirect_files:
            print(f"  [advisory] {path}: references {tbl}")

    if total:
        if not args.quiet:
            print(
                f"\ncheck_audit_guard: {total} non-exempt write(s) to tracked cells.\n"
                "  Per tasks.md §6.10, every grant-site write SHALL flow through Rando_OnLocationCheck\n"
                "  or carry an explicit ``// rando-exempt: <reason>`` comment per audit.md."
            )
        return 1 if args.strict else 0

    if not args.quiet:
        n_off = len(tracked_offsets)
        suffix = f" ({n_off} tracked offsets parsed from variables.h)" if n_off else ""
        print(f"check_audit_guard: {len(files)} file(s) scanned, no non-exempt writes{suffix}.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
