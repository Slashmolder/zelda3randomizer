#!/usr/bin/env python3
"""Mirror zelda3.smc, zelda3_assets.dat, and zelda3.ini into the current git worktree.

The project's ROM (`zelda3.smc` / `zelda3.sfc`) and the extracted asset blob
(`zelda3_assets.dat`) are gitignored, so freshly-created git worktrees lack
them. A worktree agent that runs `--generate-seed` or other asset-loading
paths will hit `Die("Failed to read zelda3_assets.dat ...")` and on Windows
Release builds pop a modal dialog on whoever's desktop the process happens
to land on.

We also mirror `zelda3.ini` (optional, best-effort). The exe searches parent
directories for `zelda3.ini`, so a worktree run with no local ini falls back
to the *main checkout's* ini and writes its F12 state dumps + SRAM saves into
the main checkout root — polluting it and risking save collisions. A local ini
keeps dumps/saves inside the worktree. Missing-source-ini is non-fatal: the exe
still runs via the parent-dir fallback.

Run this script after creating a new worktree (or in an agent's setup
step) to mirror the ROM + assets (+ ini) from the main worktree. The script is
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
INI_NAME = "zelda3.ini"  # optional, best-effort (keeps dumps/saves in the worktree)
# Generated/gitignored ROM-extracted chest table, read by rando_logic_gen.py at
# codegen time to build src/rando/chest_lookup.h (the (room,ordinal)->LOC map the
# runtime chest dispatch uses). If it is ABSENT when the codegen runs, chest_lookup.h
# is emitted EMPTY and *every chest grants its vanilla item* — a silent, very
# confusing playtest failure (placement is correct, but no chest resolves to its
# placed item). Mirror it so worktree builds dispatch chests correctly.
CHEST_TABLE_REL = os.path.join("assets", "rando", "chest_table.gen.bin")


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
    have_ini = (cwd / INI_NAME).is_file()
    have_chest = (cwd / CHEST_TABLE_REL).is_file()

    if args.verify:
        if have_rom and have_assets:
            print("setup_worktree: OK (rom + assets present)")
            return 0
        if not have_rom:
            print(f"setup_worktree: MISSING {ROM_NAMES} in {cwd}")
        if not have_assets:
            print(f"setup_worktree: MISSING {ASSETS_NAME} in {cwd}")
        return 1

    if have_rom and have_assets and have_ini and have_chest:
        print("setup_worktree: nothing to do (rom + assets + ini + chest table already present)")
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
        # The ini is optional; only a missing ROM/assets is fatal.
        if have_rom and have_assets:
            print("setup_worktree: rom + assets present; could not locate main "
                  "worktree to mirror optional zelda3.ini (skipping -- the exe "
                  "falls back to a parent-dir ini).", file=sys.stderr)
            return 0
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

    # Optional: mirror zelda3.ini so F12 dumps + SRAM saves stay in the worktree
    # instead of falling back to the main checkout's ini (and its directory).
    # Best-effort: a missing source ini is non-fatal.
    if not have_ini:
        src_ini = source / INI_NAME
        if src_ini.is_file():
            dst_ini = cwd / INI_NAME
            print(f"setup_worktree: copy {src_ini} -> {dst_ini}")
            shutil.copy2(src_ini, dst_ini)
        else:
            print(f"setup_worktree: source has no {INI_NAME} (optional) -- skipping; "
                  f"the exe will fall back to a parent-dir ini.", file=sys.stderr)

    # Mirror the generated chest table so the worktree's rando codegen can build a
    # populated chest_lookup.h. Without it, chest_lookup.h is emitted EMPTY and
    # every chest grants vanilla (silent runtime breakage). Best-effort: a missing
    # source table is non-fatal here but loudly warned, since the symptom is
    # baffling at playtest time.
    if not have_chest:
        src_chest = source / CHEST_TABLE_REL
        if src_chest.is_file():
            dst_chest = cwd / CHEST_TABLE_REL
            dst_chest.parent.mkdir(parents=True, exist_ok=True)
            print(f"setup_worktree: copy {src_chest} -> {dst_chest}")
            shutil.copy2(src_chest, dst_chest)
        else:
            print(f"setup_worktree: WARNING source has no {CHEST_TABLE_REL} -- "
                  f"worktree chest_lookup.h will be EMPTY and ALL CHESTS will grant "
                  f"their vanilla item. Run `python assets/restool.py --extract-from-rom` "
                  f"in {source} to generate it, then re-run this script + rebuild.",
                  file=sys.stderr)

    print("setup_worktree: done.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
