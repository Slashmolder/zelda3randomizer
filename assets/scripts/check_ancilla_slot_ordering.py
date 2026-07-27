#!/usr/bin/env python3
"""Reject delivering a randomizer grant while the delivering ancilla still owns
its slot.

The vanilla item receipt allocates from the same fixed pool the delivering
ancilla sits in (`Ancilla_AllocInit` scans slots 0..4). If the caller has not
released its own slot first, a busy pool silently degrades `Link_ReceiveItem`
to its quiet fallback: no receipt, so no fanfare, no rising-crystal cutscene,
and no `submodule_index = 0x18` -- which is what warps the player out of a
sealed boss room. The grant still reports Accepted with the item banked and the
location marked checked, so there is nothing left to retry.

That shipped once (boss prizes, fixed in a5ef5eb0) and was still present at two
more sites (tablet, flute spot) when this guard was written -- the same "fix the
reported instance, miss its siblings" pattern that hid a 5-of-7 crystal-bit
permutation for months. So it is a guard, not a comment.

Rule: within a window after `ancilla_type[<var>] = 0;`-style release, any call
to a grant-delivery API is fine. A delivery call that appears with NO preceding
release of the same index in the enclosing block is reported.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
TARGETS = [REPO / "src" / "ancilla.c"]

DELIVERY = re.compile(
    r"\b(Rando_CommitPreparedGrant|Rando_GrantLocation|Rando_GrantBossPrizeReceipt)\s*\(")
RELEASE = re.compile(r"\bancilla_type\[\s*(\w+)\s*\]\s*=\s*0\s*;")
# The delivering ancilla index is conventionally `k` in this file.
INDEX = "k"
LOOKBACK = 14
ESCAPE = "slot-ordering: allow"


def main() -> int:
    violations: list[str] = []
    for path in TARGETS:
        rel = path.relative_to(REPO).as_posix()
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
        for i, line in enumerate(lines):
            if not DELIVERY.search(line):
                continue
            if ESCAPE in line or (i and ESCAPE in lines[i - 1]):
                continue
            # Self-checks and helpers that are not inside an ancilla handler
            # never own a slot; they pass trivially because no `k` release is
            # expected -- so require the release only when the enclosing window
            # actually manipulates ancilla_type[k].
            window = lines[max(0, i - LOOKBACK):i]
            touches_slot = any(f"ancilla_type[{INDEX}]" in w for w in window)
            if not touches_slot:
                continue
            released = any(
                RELEASE.search(w) and RELEASE.search(w).group(1) == INDEX
                for w in window)
            if not released:
                violations.append(
                    f"{rel}:{i + 1}: delivers a grant while ancilla_type[{INDEX}] "
                    f"is still set")

    if violations:
        print("check_ancilla_slot_ordering: a grant is delivered from an ancilla "
              "that still owns its slot:", file=sys.stderr)
        for v in violations:
            print(f"  {v}", file=sys.stderr)
        print("  Fix: release ancilla_type[k] before the call and restore it "
              "when delivery did not happen, or annotate with "
              "`// slot-ordering: allow <reason>`.", file=sys.stderr)
        return 1
    print("check_ancilla_slot_ordering: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
