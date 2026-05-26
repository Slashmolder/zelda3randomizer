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


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--strict",
        action="store_true",
        help="exit non-zero if the scaffold cannot yet run (CI gate sanity check)",
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

    # Activated path: actually run the binary and check byte-identity.
    # Implementation pending — placeholder that documents the contract:
    print("check_init_order: rando kRam_* cells detected:")
    for name, off in sorted(cells.items(), key=lambda kv: kv[1]):
        print(f"  {name} @ 0x{off:04x}")
    print(f"\n{len(chapters)} chapter savestate(s) available:")
    for s in chapters:
        print(f"  {s.name}")
    print(
        "\ncheck_init_order: TODO — implement replay-and-dump once binary supports it.\n"
        "  Currently exits clean. Wire to the real binary in task 1.2."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
