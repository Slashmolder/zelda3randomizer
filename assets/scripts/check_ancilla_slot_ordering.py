#!/usr/bin/env python3
r"""Reject delivering a randomizer grant while the delivering ancilla still owns
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
permutation. So it is a guard, not a comment.

Rule: a delivery call inside an ancilla handler (a function taking `int <idx>`)
must be preceded, within a short window, by a release of an ancilla slot. The
window matters: an unrelated `ancilla_type[k] = 0` elsewhere in the same handler
must not bless the call.

Pinned by its own self-test: `--selftest` re-runs the analysis over
`a5ef5eb0^:src/ancilla.c`, the revision that contained the original softlock,
and fails unless that revision is REJECTED. Two earlier versions of this guard
silently matched nothing -- once from an over-tight gate, once because a `\b`
became a literal backspace byte -- so "it prints OK" is not evidence.
"""
from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
TARGETS = [REPO / "src" / "ancilla.c"]

DELIVERY = re.compile(
    r"\b(Rando_CommitPreparedGrant|Rando_GrantLocation|Rando_GrantBossPrizeReceipt)\s*\(")
RELEASE = re.compile(r"\bancilla_type\s*\[\s*\w+\s*\]\s*=\s*0\s*;")
HANDLER = re.compile(r"^\w[\w \t*]*\(\s*int\s+(\w+)\s*[,)]")
LOOKBACK = 14
ESCAPE = "slot-ordering: allow"

# A silently non-matching regex reports a clean tree forever. Pin the patterns
# against known-good inputs at import time.
assert DELIVERY.search("  x = Rando_GrantLocation(a, b);")
assert RELEASE.search("    ancilla_type[k] = 0;")
assert HANDLER.search("void Ancilla36_Flute(int k) {")


def function_start(lines, idx):
    """Line index of the enclosing function's first line (after a column-0 `}`)."""
    for i in range(idx, -1, -1):
        if lines[i].startswith("}"):
            return i + 1
    return 0


def scan(lines, rel):
    out = []
    for i, line in enumerate(lines):
        if not DELIVERY.search(line):
            continue
        if ESCAPE in line or (i and ESCAPE in lines[i - 1]):
            continue
        start = function_start(lines, i)
        # Only handlers that own an ancilla slot are in scope; self-checks and
        # free helpers cannot strand one. strip() matters: the line after a
        # column-0 `}` is usually blank, and an unstripped join makes the `^`
        # anchor fail for every site.
        header = " ".join(lines[start:start + 3]).strip()
        if not HANDLER.search(header):
            continue
        window = lines[max(start, i - LOOKBACK):i]
        if not any(RELEASE.search(w) for w in window):
            out.append("%s:%d: delivers a grant without releasing this "
                       "ancilla's slot first" % (rel, i + 1))
    return out


def selftest():
    """The guard must REJECT the revision that contained the original bug."""
    ref = "a5ef5eb0^:src/ancilla.c"
    try:
        blob = subprocess.run(["git", "show", ref], cwd=str(REPO), check=True,
                              capture_output=True, text=True, errors="replace")
    except (subprocess.CalledProcessError, FileNotFoundError) as e:
        print("check_ancilla_slot_ordering: selftest SKIP (%s unavailable: %s)"
              % (ref, e))
        return 0
    hits = scan(blob.stdout.splitlines(), ref)
    if not hits:
        print("check_ancilla_slot_ordering: SELFTEST FAILED - the guard accepts "
              "%s, which contains the boss-prize softlock it exists to catch. "
              "The guard is not doing anything." % ref, file=sys.stderr)
        return 1
    print("check_ancilla_slot_ordering: selftest OK (rejects %s, %d site(s))"
          % (ref, len(hits)))
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--selftest", action="store_true",
                    help="prove the guard rejects the known-bad revision")
    args = ap.parse_args()

    if args.selftest and selftest() != 0:
        return 1

    violations = []
    for path in TARGETS:
        rel = path.relative_to(REPO).as_posix()
        violations += scan(
            path.read_text(encoding="utf-8", errors="replace").splitlines(), rel)

    if violations:
        print("check_ancilla_slot_ordering: a grant is delivered from an ancilla "
              "that still owns its slot:", file=sys.stderr)
        for v in violations:
            print("  " + v, file=sys.stderr)
        print("  Fix: release ancilla_type[<idx>] before the call and restore it "
              "when delivery did not happen, or annotate with "
              "`// slot-ordering: allow <reason>`.", file=sys.stderr)
        return 1
    print("check_ancilla_slot_ordering: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
