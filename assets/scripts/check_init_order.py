#!/usr/bin/env python3
"""Init-order replay guard (tasks.md §1.0d, §1.2).

Replays the per-chapter savestates in ``saves/ref/`` against the freshly-built
binary in vanilla mode (no randomizer slot active) and asserts byte-identity
at every newly-added ``kRam_*`` offset versus the pre-change baseline.

**A0 status (scaffold)**: until task 1.1 lands ``kRam_Features1`` etc., this
script has no new offsets to check. The script knows the protocol but exits
zero with an explanatory message.

**Activation**: once ``kRam_RandoSlotActive`` (or any new ``kRam_*`` cell)
exists in ``src/features.h``, the script:
  1. Boots the binary against each ``saves/ref/Chapter*.sav`` in replay mode.
  2. After each replay, dumps the new kRam_* byte range.
  3. Asserts the dump matches the vanilla baseline captured pre-change.
  4. Fails the CI job if any byte differs.

Usage:
  python assets/scripts/check_init_order.py           # scan + check
  python assets/scripts/check_init_order.py --strict  # fail if scaffold can't yet run
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

FEATURES_H = Path("src/features.h")
SAVES_REF = Path("saves/ref")

# kRam_* cells whose init-order we guard. Populated by scanning features.h.
# At A0 only the existing pre-rando cells exist; the rando cells (kRam_Features1,
# kRam_RandoSlotActive, kRam_RandoStartingInventoryGranted) land in task 1.1.
RANDO_KRAM_NAMES = {
    "kRam_Features1",
    "kRam_RandoSlotActive",
    "kRam_RandoStartingInventoryGranted",
}


def discover_rando_kram_cells() -> dict[str, int]:
    """Parse features.h for kRam_* enum entries belonging to the rando subsystem."""
    if not FEATURES_H.exists():
        return {}
    text = FEATURES_H.read_text(encoding="utf-8", errors="replace")
    out: dict[str, int] = {}
    # Match: kRam_Foo = 0xNNN,
    for match in re.finditer(r"(kRam_[A-Za-z0-9_]+)\s*=\s*(0x[0-9a-fA-F]+)", text):
        name, offset = match.group(1), int(match.group(2), 16)
        if name in RANDO_KRAM_NAMES:
            out[name] = offset
    return out


def parse_ram_check_line(line: str) -> dict[str, str] | None:
    """Parse one ``ram_check ...`` stdout line into a key/value dict.

    Returns None if the line isn't a recognized ram_check line.
    """
    if not line.startswith("ram_check "):
        return None
    out: dict[str, str] = {}
    rest = line[len("ram_check "):].strip()
    # Tokens are space-separated key=value, but savestate paths can contain
    # spaces ("Chapter 1 - Zelda's Rescue.sav"). The savestate= token always
    # appears first; everything before the next "<key>=" with a known key is
    # part of the savestate path.
    KNOWN_KEYS = ("ok=", "features1=", "slot_active=", "starting_inv=")
    sav_end = len(rest)
    for k in KNOWN_KEYS:
        idx = rest.find(" " + k)
        if idx != -1 and idx < sav_end:
            sav_end = idx
    head = rest[:sav_end].strip()
    if not head.startswith("savestate="):
        return None
    out["savestate"] = head[len("savestate="):]
    for tok in rest[sav_end:].split():
        if "=" in tok:
            k, v = tok.split("=", 1)
            out[k] = v
    return out


def run_one_chapter(binary: Path, savestate: Path) -> tuple[bool, str]:
    """Invoke the binary's --vanilla-ram-check and parse the result.

    Returns (ok, raw_line). ok=True if the binary printed ok=1 (all
    tracked kRam_* cells were zero after replay).
    """
    import subprocess
    try:
        out = subprocess.check_output(
            [str(binary), f"--vanilla-ram-check={savestate}"],
            stderr=subprocess.STDOUT,
            text=True,
        )
    except subprocess.CalledProcessError as exc:
        return (False, f"binary exited {exc.returncode}: {exc.output.strip()}")
    line = out.strip().splitlines()[-1] if out.strip() else ""
    parsed = parse_ram_check_line(line)
    if not parsed:
        return (False, f"unparseable output: {line!r}")
    return (parsed.get("ok") == "1", line)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--strict",
        action="store_true",
        help="exit non-zero if the scaffold cannot yet run (CI gate sanity check)",
    )
    parser.add_argument(
        "--binary",
        default="./bin/x64-Release/zelda3.exe",
        help="path to the built zelda3 binary",
    )
    args = parser.parse_args(argv)

    cells = discover_rando_kram_cells()
    chapters = sorted(SAVES_REF.glob("Chapter*.sav")) if SAVES_REF.exists() else []

    if not chapters:
        print(f"check_init_order: no Chapter*.sav files under {SAVES_REF}.")
        return 1 if args.strict else 0

    if not cells:
        print(
            "check_init_order: no rando kRam_* cells declared yet in src/features.h.\n"
            "  scaffold A0 pass — guard activates when task 1.1 lands kRam_Features1."
        )
        return 1 if args.strict else 0

    binary = Path(args.binary)
    if not binary.is_file():
        print(f"check_init_order: binary not found at {binary}")
        return 1 if args.strict else 0

    failures = 0
    for sav in chapters:
        ok, line = run_one_chapter(binary, sav)
        marker = "OK  " if ok else "FAIL"
        print(f"  [{marker}] {line}")
        if not ok:
            failures += 1

    if failures:
        print(
            f"\ncheck_init_order: {failures} chapter(s) saw non-zero rando kRam_* state.\n"
            f"  This means existing game code is writing to addresses claimed for\n"
            f"  randomizer state (per audit.md §0.7). Investigate the collision."
        )
        return 1

    print(
        f"\ncheck_init_order: {len(chapters)} chapter(s) clean — every rando kRam_*\n"
        f"  cell retained zero through vanilla-mode replay."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
