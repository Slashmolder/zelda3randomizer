#!/usr/bin/env python3
"""kGeneratorVersion PR-gate (tasks.md §1.0e, §13.6).

If a PR touches any of the listed paths but does NOT bump ``kGeneratorVersion``
in ``src/rando/rando.h``, fail the CI job. Per ``tasks.md §13.6``: any change
that affects placement output requires a version bump so the regression corpus
gets regenerated and slots written by the old version surface the expected
informational warning.

Bump-trigger paths (per tasks.md §13.6 — extend as new modules land):
  - src/rando/*.c, src/rando/*.h
  - assets/rando/**.yaml          (logic.yaml, location_registry.yaml, etc.)
  - assets/rando_logic_gen.py and local ROM-artifact generators
  - any generated file: src/rando/logic_data.c, src/rando/location_ids.h, etc.

**A0 status (scaffold)**: ``kGeneratorVersion`` doesn't exist yet (lands in
task 1.1). Until it does, the script exits clean. The script is wired into CI
now so the gate is live from the moment the constant lands.

Usage (CI invocation):
  python assets/scripts/check_generator_version.py --base-sha=<sha> --head-sha=<sha>

The base/head shas let it diff the constant's declared value between the PR's
merge base and tip. Without git context (local invocation), it just verifies
the constant is present and well-formed.
"""
from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

VERSION_HEADER_CANDIDATES = [
    Path("src/rando/rando.h"),
    Path("src/rando/rando_version.h"),
]
VERSION_PATTERN = re.compile(r"#define\s+kGeneratorVersion\s+(\d+)")

# Paths whose modification requires a kGeneratorVersion bump.
BUMP_TRIGGER_GLOBS = [
    "src/rando/**/*.c",
    "src/rando/**/*.h",
    "assets/rando/**/*.yaml",
    "assets/rando_logic_gen.py",
    "assets/scripts/gen_pot_tables.py",
    "assets/scripts/gen_pot_key_depth.py",
    "assets/scripts/gen_enemy_drop_tables.py",
    "assets/scripts/gen_enemy_check_tables.py",
    "assets/scripts/gen_soul_room_tables.py",
]


def find_version_header() -> Path | None:
    for candidate in VERSION_HEADER_CANDIDATES:
        if candidate.exists():
            return candidate
    return None


def read_version(header: Path) -> int | None:
    text = header.read_text(encoding="utf-8", errors="replace")
    match = VERSION_PATTERN.search(text)
    return int(match.group(1)) if match else None


def read_version_at(header_relpath: str, sha: str) -> int | None:
    """Read kGeneratorVersion as it was at a given git SHA. Returns None on missing."""
    # `git show <sha>:<path>` always uses repository-style forward slashes.
    # Path.__str__ emits backslashes on Windows, which made this gate silently
    # treat the existing header as absent and skip the required bump check.
    header_relpath = header_relpath.replace("\\", "/")
    try:
        out = subprocess.run(
            ["git", "show", f"{sha}:{header_relpath}"],
            capture_output=True,
            text=True,
            check=False,
        )
    except FileNotFoundError:
        return None
    if out.returncode != 0:
        return None
    match = VERSION_PATTERN.search(out.stdout)
    return int(match.group(1)) if match else None


def changed_files(base_sha: str, head_sha: str) -> list[str]:
    out = subprocess.run(
        ["git", "diff", "--name-only", f"{base_sha}..{head_sha}"],
        capture_output=True,
        text=True,
        check=False,
    )
    if out.returncode != 0:
        return []
    return [line.strip() for line in out.stdout.splitlines() if line.strip()]


def matches_glob(path: str, pattern: str) -> bool:
    # Translate the glob to a regex with PROPER recursive ``**`` semantics.
    # ``**/`` matches zero or more leading path segments, so ``src/rando/**/*.c``
    # matches BOTH ``src/rando/rando.c`` (top level) AND ``src/rando/sub/x.c``;
    # ``*`` matches within a single segment; bare ``**`` matches anything.
    # The previous ``**/``->``*/`` fnmatch collapse required a literal
    # intermediate ``/`` and therefore SILENTLY EXCLUDED every top-level file —
    # logic.yaml, rando_placement.c, rando.h (the file defining the version
    # itself) — defeating the bump gate for the overwhelming majority of
    # placement-affecting edits.
    import re as _re
    regex, i = "", 0
    while i < len(pattern):
        if pattern.startswith("**/", i):
            regex += r"(?:[^/]+/)*"
            i += 3
        elif pattern.startswith("**", i):
            regex += r".*"
            i += 2
        elif pattern[i] == "*":
            regex += r"[^/]*"
            i += 1
        elif pattern[i] == "?":
            regex += r"[^/]"
            i += 1
        else:
            regex += _re.escape(pattern[i])
            i += 1
    return _re.fullmatch(regex, path) is not None


# Self-test the glob matcher at import (cheap) so a future refactor of the
# recursive translation can't silently re-break top-level coverage.
assert matches_glob("src/rando/rando_placement.c", "src/rando/**/*.c")
assert matches_glob("src/rando/rando.h", "src/rando/**/*.h")
assert matches_glob("assets/rando/logic.yaml", "assets/rando/**/*.yaml")
assert matches_glob("assets/rando/logic_parts/01_eastern_palace.yaml", "assets/rando/**/*.yaml")
assert matches_glob("assets/rando_logic_gen.py", "assets/rando_logic_gen.py")
assert matches_glob("assets/scripts/gen_pot_tables.py", "assets/scripts/gen_pot_tables.py")
assert matches_glob("assets/scripts/gen_pot_key_depth.py", "assets/scripts/gen_pot_key_depth.py")
assert matches_glob("assets/scripts/gen_enemy_check_tables.py", "assets/scripts/gen_enemy_check_tables.py")
assert matches_glob("src/rando/rando_window/rando_window.h", "src/rando/**/*.h")
assert not matches_glob("src/rando/rando.c", "src/rando/**/*.h")
assert not matches_glob("src/main.c", "src/rando/**/*.c")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base-sha", default=None)
    parser.add_argument("--head-sha", default="HEAD")
    parser.add_argument("--quiet", action="store_true")
    args = parser.parse_args(argv)

    header = find_version_header()
    if header is None:
        if not args.quiet:
            print(
                "check_generator_version: kGeneratorVersion header not found.\n"
                "  Expected one of: " + ", ".join(str(p) for p in VERSION_HEADER_CANDIDATES) + "\n"
                "  A0 scaffold pass — gate activates when task 1.1 lands the header."
            )
        return 0

    head_version = read_version(header)
    if head_version is None:
        print(f"check_generator_version: {header} present but kGeneratorVersion not defined.")
        return 1

    if not args.quiet:
        print(f"check_generator_version: kGeneratorVersion = {head_version} (header: {header})")

    if not args.base_sha:
        # Local-invocation mode: presence + well-formedness check only.
        return 0

    base_version = read_version_at(str(header), args.base_sha)
    if base_version is None:
        if not args.quiet:
            print(f"check_generator_version: header didn't exist at base sha {args.base_sha[:8]}; treating as new file.")
        return 0

    changed = changed_files(args.base_sha, args.head_sha)
    triggered_by = [p for p in changed if any(matches_glob(p, g) for g in BUMP_TRIGGER_GLOBS)]
    if not triggered_by:
        if not args.quiet:
            print("check_generator_version: no bump-trigger paths in PR diff.")
        return 0

    if head_version <= base_version:
        print(f"check_generator_version: PR touches bump-trigger paths but kGeneratorVersion did not advance.")
        print(f"  base ({args.base_sha[:8]}): {base_version}")
        print(f"  head ({args.head_sha[:8]}): {head_version}")
        print(f"  Trigger paths in this diff ({len(triggered_by)}):")
        for path in triggered_by[:20]:
            print(f"    {path}")
        if len(triggered_by) > 20:
            print(f"    ... and {len(triggered_by) - 20} more")
        print("\nPer tasks.md §13.6, bump kGeneratorVersion in this PR.")
        return 1

    if not args.quiet:
        print(f"check_generator_version: bump OK ({base_version} -> {head_version}).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
