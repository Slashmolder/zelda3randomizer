#!/usr/bin/env python3
"""Mirror zelda3.smc and zelda3_assets.dat into the current git worktree.

The project's ROM (`zelda3.smc` / `zelda3.sfc`) and the extracted asset blob
(`zelda3_assets.dat`) are gitignored, so freshly-created git worktrees lack
them. A worktree agent that runs `--generate-seed` or other asset-loading
paths will hit `Die("Failed to read zelda3_assets.dat ...")` and on Windows
Release builds pop a modal dialog on whoever's desktop the process happens
to land on.

Run this script after creating a new worktree (or in an agent's setup
step) to mirror the ROM + assets from the main worktree. The script is
idempotent: if files already exist locally it does nothing.

Usage:
    python assets/scripts/setup_worktree.py              # auto-detect main worktree
    python assets/scripts/setup_worktree.py --from PATH  # explicit source dir
    python assets/scripts/setup_worktree.py --verify     # check, don't copy

Resolution order for the source:
    1. --from PATH command-line override
    2. ZELDA3_MAIN_WORKTREE environment variable
    3. `git worktree list` -- pick the entry without a worktree-specific
       subdirectory marker (i.e., the main checkout)

Exits 0 if assets are present (or were successfully mirrored), 1 otherwise.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROM_NAMES = ("zelda3.sfc", "zelda3.smc")
ASSETS_NAME = "zelda3_assets.dat"


def find_main_worktree() -> Path | None:
    """Run `git worktree list --porcelain` and return the main worktree's path.

    Heuristic: the main worktree is the one whose path does NOT contain
    `.claude/worktrees/` (those are the agent-isolation worktrees this
    script is meant to populate).
    """
    try:
        out = subprocess.check_output(
            ["git", "worktree", "list", "--porcelain"],
            text=True,
        )
    except (FileNotFoundError, subprocess.CalledProcessError) as exc:
        print(f"setup_worktree: git worktree list failed: {exc}", file=sys.stderr)
        return None

    candidates: list[Path] = []
    for line in out.splitlines():
        if line.startswith("worktree "):
            candidates.append(Path(line[len("worktree "):].strip()))

    # Filter out agent-isolation worktrees.
    main_candidates = [
        p for p in candidates
        if ".claude/worktrees" not in p.as_posix()
        and ".claude\\worktrees" not in str(p)
    ]
    if not main_candidates:
        return None
    # Prefer the shortest path — if multiple main-style worktrees exist,
    # the canonical checkout will typically be the shortest.
    main_candidates.sort(key=lambda p: len(p.as_posix()))
    return main_candidates[0]


def find_existing_rom(d: Path) -> Path | None:
    for name in ROM_NAMES:
        p = d / name
        if p.is_file():
            return p
    return None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--from", dest="source", default=None,
                        help="explicit source directory (overrides auto-detect)")
    parser.add_argument("--verify", action="store_true",
                        help="check whether assets are present, don't copy")
    args = parser.parse_args()

    cwd = Path.cwd().resolve()

    have_rom = find_existing_rom(cwd) is not None
    have_assets = (cwd / ASSETS_NAME).is_file()

    if args.verify:
        if have_rom and have_assets:
            print("setup_worktree: OK (rom + assets present)")
            return 0
        if not have_rom:
            print(f"setup_worktree: MISSING {ROM_NAMES} in {cwd}")
        if not have_assets:
            print(f"setup_worktree: MISSING {ASSETS_NAME} in {cwd}")
        return 1

    if have_rom and have_assets:
        print("setup_worktree: nothing to do (rom + assets already present)")
        return 0

    # Resolve the source.
    source: Path | None = None
    if args.source:
        source = Path(args.source).resolve()
    elif "ZELDA3_MAIN_WORKTREE" in os.environ:
        source = Path(os.environ["ZELDA3_MAIN_WORKTREE"]).resolve()
    else:
        source = find_main_worktree()

    if source is None:
        print("setup_worktree: could not locate main worktree. Pass --from PATH,",
              file=sys.stderr)
        print("                set ZELDA3_MAIN_WORKTREE, or ensure git knows about",
              file=sys.stderr)
        print("                the main checkout.", file=sys.stderr)
        return 1

    if source.resolve() == cwd:
        print(f"setup_worktree: source == cwd ({source}); nothing to mirror.",
              file=sys.stderr)
        return 1

    print(f"setup_worktree: source = {source}")

    if not have_rom:
        src_rom = find_existing_rom(source)
        if src_rom is None:
            print(f"setup_worktree: source has no ROM ({ROM_NAMES}) -- cannot mirror.",
                  file=sys.stderr)
            print(f"                Place a copy in {source} first, or pass --from.",
                  file=sys.stderr)
            return 1
        dst_rom = cwd / src_rom.name
        print(f"setup_worktree: copy {src_rom} -> {dst_rom}")
        shutil.copy2(src_rom, dst_rom)

    if not have_assets:
        src_assets = source / ASSETS_NAME
        if not src_assets.is_file():
            print(f"setup_worktree: source has no {ASSETS_NAME} -- cannot mirror.",
                  file=sys.stderr)
            print(f"                Run `python assets/restool.py --extract-from-rom`",
                  file=sys.stderr)
            print(f"                in {source} first, or pass --from to a populated dir.",
                  file=sys.stderr)
            return 1
        dst_assets = cwd / ASSETS_NAME
        print(f"setup_worktree: copy {src_assets} -> {dst_assets}")
        shutil.copy2(src_assets, dst_assets)

    print("setup_worktree: done.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
