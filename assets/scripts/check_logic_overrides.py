#!/usr/bin/env python3
"""Logic-graph silent-override guard.

The rando logic codegen (``assets/rando_logic_gen.py``) merges the logic graph
from many YAML files with a **"last file wins"** policy at the base
(non-world-state) level:

    logic.yaml  →  logic_parts/*.yaml (sorted)        [base level]
                   logic_parts/inverted/**            [Inverted overrides]

For a location predicate, macro, or region declared in more than one base-level
file, the LAST file silently overrides the earlier ones — no warning. That
silent override has bitten this project twice (see CLAUDE.md):

  * King Zora et al. lost their ``RescuedZelda`` gate when a logic_parts
    duplicate dropped the ``region:`` binding.
  * Eastern Palace Boss/Prize had *weaker* predicates in logic_parts than in
    logic.yaml, and the placer routed through EP without arrows or lamp.

The codegen's ``--strict`` mode warns on ``region: 0xFFFF``, but it does NOT
catch the general "a duplicate declaration changed the predicate" case. This
guard does.

What it flags (fails CI):
  * A **location** id declared in ≥2 base-level files where the effective tuple
    (region, can_reach, can_place, always_allow, type, vanilla_item) DIFFERS
    between declarations — i.e. a later file silently changed the predicate or
    the vanilla mapping/type the codegen emits.
  * A **macro** name redefined across base-level files (including
    ``macros.yaml``, the lowest-priority macro source the inline macros override)
    with a different body/params.
  * A **region** id redeclared across base-level files with a different
    (parent, dungeon, world_state_filter) binding (the King-Zora "stub overwrote
    the real binding" case, plus a silently-dropped world-state gate).

Intentional overrides are allowlisted below with a reason. Today the only
intentional set is the Eastern Palace locations, where logic_parts/01
deliberately supersedes logic.yaml with the corrected dark-room-nav predicates.

Harmless *identical* duplicates (same predicate declared twice) are reported as
an informational count only — they don't fail, but they are redundant cruft.

This is a pure-source guard: no build, no assets, no ROM. PyYAML only.

Usage:
  python assets/scripts/check_logic_overrides.py [--verbose]
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

try:
    import yaml  # type: ignore
except ImportError:
    print("check_logic_overrides: PyYAML not installed (pip install pyyaml).")
    sys.exit(2)

RANDO_ASSETS = Path("assets/rando")
LOGIC_YAML = RANDO_ASSETS / "logic.yaml"
LOGIC_PARTS = RANDO_ASSETS / "logic_parts"
# macros.yaml is the canonical home of the named macros and is loaded at LOWEST
# priority by the codegen (`{**macros, **logic_macros}` in rando_logic_gen.py),
# so an inline macro can silently override it — include it in the macro scan.
MACROS_YAML = RANDO_ASSETS / "macros.yaml"

# ---------------------------------------------------------------------------
# Intentional, reviewed overrides. A base-level duplicate whose predicate
# legitimately supersedes an earlier file goes here with a reason. Anything not
# listed that changes a predicate across base files fails the guard.
# ---------------------------------------------------------------------------
# Eastern Palace: the whole EP block in logic.yaml is superseded seed data —
# logic_parts/01_eastern_palace.yaml is the authoritative copy and intentionally
# overrides it (the dark-room-nav `(HAS_ITEM(Lamp) OR CanDarkRoomNav())` form on
# the Lamp gates, the Compass/Map `always_allow` on the Big Chest, and the
# `vanilla_item` mappings). See CLAUDE.md "item.require.Lamp dark-room pattern"
# and the EP weaker-predicate regression note. All 7 EP locations differ.
_EP = "EP authoritative copy in logic_parts/01 supersedes logic.yaml seed data"
INTENTIONAL_LOCATION_OVERRIDES = {
    "Eastern Palace - Big Chest": _EP,
    "Eastern Palace - Big Key Chest": _EP,
    "Eastern Palace - Boss": _EP,
    "Eastern Palace - Cannonball Chest": _EP,
    "Eastern Palace - Compass Chest": _EP,
    "Eastern Palace - Map Chest": _EP,
    "Eastern Palace - Prize": _EP,
}
INTENTIONAL_MACRO_OVERRIDES: dict[str, str] = {}
INTENTIONAL_REGION_OVERRIDES: dict[str, str] = {}


def _norm(pred) -> str:
    """Normalize a predicate string for comparison (whitespace is not
    semantically significant — the codegen parses these into an AST)."""
    return " ".join(str(pred).split())


def _is_base_level(path: Path) -> bool:
    """Mirror ``rando_logic_gen._world_state_id_for_path``: a file under a
    world-state subdir (currently ``inverted/``) routes to an override map, not
    the base graph. Everything else is base level. Keep this in sync with the
    codegen if a new world-state subdir (e.g. ``retro/``) is added."""
    return "inverted" not in path.parts


def base_level_files() -> list[Path]:
    files: list[Path] = []
    # macros.yaml first: it's the lowest-priority macro source (no locations /
    # regions / edges), so listing it ahead of the logic files reflects the
    # codegen's "inline macros override macros.yaml" precedence.
    if MACROS_YAML.exists():
        files.append(MACROS_YAML)
    if LOGIC_YAML.exists():
        files.append(LOGIC_YAML)
    if LOGIC_PARTS.is_dir():
        files += [p for p in sorted(LOGIC_PARTS.rglob("*.yaml")) if _is_base_level(p)]
    return files


def load(path: Path) -> dict:
    return yaml.safe_load(path.read_text(encoding="utf-8")) or {}


def loc_predicate(raw: dict) -> tuple:
    # Defaults must match rando_logic_gen._merge_logic_doc exactly. Includes
    # type + vanilla_item: both flow into the emitted location struct
    # (type_id / vanilla_id), so a silent change to either is a real override.
    return (
        _norm(raw.get("region", "")),
        _norm(raw.get("can_reach", "TRUE()")),
        _norm(raw.get("can_place", "TRUE()")),
        _norm(raw.get("always_allow", "FALSE()")),
        _norm(raw.get("type", "Chest")),
        _norm(raw.get("vanilla_item", "")),
    )


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--verbose", action="store_true",
                        help="also list harmless identical duplicates")
    args = parser.parse_args(argv)

    files = base_level_files()
    if not files:
        print("check_logic_overrides: no base-level logic YAML found — skipping.")
        return 0

    # id -> list[(rel_path, value)]
    locs: dict[str, list[tuple[str, tuple]]] = {}
    macros: dict[str, list[tuple[str, tuple]]] = {}
    regions: dict[str, list[tuple[str, tuple]]] = {}

    for f in files:
        rel = f.relative_to(RANDO_ASSETS).as_posix()
        doc = load(f)
        for raw in (doc.get("locations") or []):
            key = raw.get("id")
            if key is None:
                continue
            locs.setdefault(key, []).append((rel, loc_predicate(raw)))
        for raw in (doc.get("macros") or []):
            key = raw.get("name")
            if key is None:
                continue
            val = (tuple(raw.get("parameters") or []), _norm(raw.get("body", "")))
            macros.setdefault(key, []).append((rel, val))
        for raw in (doc.get("regions") or []):
            key = raw.get("id")
            if key is None:
                continue
            # parent/dungeon = connectivity binding (King-Zora loss); the
            # world_state_filter (as a tuple for hashability) catches a silently
            # dropped world-state gate.
            val = (raw.get("parent"), raw.get("dungeon"),
                   tuple(raw.get("world_state_filter") or []))
            regions.setdefault(key, []).append((rel, val))

    conflicts: list[str] = []
    identical_dupes = 0

    def check(kind: str, table: dict, allowlist: dict[str, str]) -> None:
        nonlocal identical_dupes
        for key, occ in sorted(table.items()):
            if len(occ) < 2:
                continue
            distinct = {v for _, v in occ}
            if len(distinct) < 2:
                identical_dupes += 1
                if args.verbose:
                    fileset = ", ".join(sorted({f for f, _ in occ}))
                    print(f"  info: {kind} '{key}' declared identically in {len(occ)} files ({fileset})")
                continue
            if key in allowlist:
                fileset = ", ".join(f for f, _ in occ)
                print(f"  allowed: {kind} '{key}' overridden across [{fileset}] — {allowlist[key]}")
                continue
            lines = [f"CONFLICT: {kind} '{key}' has DIFFERING declarations across base-level files:"]
            for f, v in occ:
                lines.append(f"    {f}: {v}")
            conflicts.append("\n".join(lines))

    check("location", locs, INTENTIONAL_LOCATION_OVERRIDES)
    check("macro", macros, INTENTIONAL_MACRO_OVERRIDES)
    check("region", regions, INTENTIONAL_REGION_OVERRIDES)

    if conflicts:
        print("check_logic_overrides: FAIL — silent base-level override(s) detected.\n")
        for c in conflicts:
            print(c + "\n")
        print(
            "A later base-level logic file silently overrode an earlier predicate.\n"
            "If this is intentional, add the id to the matching allowlist in\n"
            "assets/scripts/check_logic_overrides.py with a one-line reason.\n"
            "Otherwise, reconcile the duplicate so a single file owns the predicate."
        )
        return 1

    print(
        f"check_logic_overrides: OK — scanned {len(files)} base-level files; "
        f"no un-allowlisted overrides. ({identical_dupes} harmless identical "
        f"duplicate id(s); run with --verbose to list.)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
