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
HANDLER = re.compile(r"^\w[\w \t*]*\(\s*int\s+(\w+)\s*[,)]")
SIG_START = re.compile(r"^\w[\w \t*]*\(")
LOOKBACK = 14
ESCAPE = "slot-ordering: allow"


def release_re(var):
    """Release of THIS handler's own slot.

    Deliberately keyed to the handler's index parameter. A bare
    `ancilla_type[<anything>] = 0` used to count, which meant the retire-other-
    icons loops (`for i ... ancilla_type[i] = 0;` in AncillaAdd_RandoIconReceipt
    and misc.c) blessed a delivery that never released the caller's own slot --
    the guard would have accepted the exact bug it exists to catch, as long as
    some unrelated slot was cleared nearby.
    """
    return re.compile(r"\bancilla_type\s*\[\s*%s\s*\]\s*=\s*0\s*;" % re.escape(var))


# A silently non-matching regex reports a clean tree forever. Pin the patterns
# against known-good inputs at import time.
assert DELIVERY.search("  x = Rando_GrantLocation(a, b);")
assert release_re("k").search("    ancilla_type[k] = 0;")
assert not release_re("k").search("    ancilla_type[i] = 0;")
assert HANDLER.search("void Ancilla36_Flute(int k) {")


def function_start(lines, idx):
    """Line index of the enclosing function's first line (after a column-0 `}`)."""
    for i in range(idx, -1, -1):
        if lines[i].startswith("}"):
            return i + 1
    return 0


def handler_index_var(lines, start, idx):
    """The enclosing function's ancilla-index parameter, or None if not a handler.

    Scans forward from the start of the enclosing function for the first
    column-0 signature. The previous version instead joined the THREE lines
    after the last column-0 `}` and matched against that, so any handler whose
    signature is preceded by a comment block -- which in this file is most of
    them -- never matched and was skipped entirely. That is a false negative in
    the direction that matters: the guard silently stopped covering the sites it
    was written for.

    Signatures also WRAP. `Ancilla29_CommitStoredRandoGrant(` puts its `int k`
    on the following line, so a single-line match missed it and that site --
    a real handler that delivers a grant -- was out of scope entirely. Join
    forward to the opening brace before matching.
    """
    for i in range(start, idx + 1):
        if not SIG_START.match(lines[i]):
            continue
        sig = []
        for j in range(i, min(i + 4, len(lines))):
            sig.append(lines[j])
            if "{" in lines[j] or ";" in lines[j]:
                break
        m = HANDLER.match(" ".join(sig))
        if m:
            return m.group(1)
    return None


def scan(lines, rel):
    out = []
    for i, line in enumerate(lines):
        if not DELIVERY.search(line):
            continue
        if ESCAPE in line or (i and ESCAPE in lines[i - 1]):
            continue
        start = function_start(lines, i)
        # Only handlers that own an ancilla slot are in scope; self-checks and
        # free helpers cannot strand one.
        var = handler_index_var(lines, start, i)
        if var is None:
            continue
        window = lines[max(start, i - LOOKBACK):i]
        rel_re = release_re(var)
        if not any(rel_re.search(w) for w in window):
            out.append("%s:%d: delivers a grant without releasing this "
                       "ancilla's slot (ancilla_type[%s] = 0) first"
                       % (rel, i + 1, var))
    return out


# Synthetic fixtures for the two false negatives found by audit. Each is a
# handler that MUST be rejected; each was ACCEPTED by the pre-fix guard.
FIXTURES = [
    (
        "comment block above the signature",
        """}

// A long explanatory comment about this handler, of the kind this file is
// full of. Three or more lines of it is enough that the old header window
// (the three lines after the previous column-0 close brace) never saw the
// signature at all, so the handler fell out of scope silently.
void Ancilla99_Example(int k) {
  Rando_GrantLocation(loc, item, code, pres, 0, 0);
}
""",
    ),
    (
        "wraps its signature across lines",
        """}
static bool Ancilla96_Example(
    int k, RandoGrantPresentation presentation) {
  Rando_CommitPreparedGrant(&token, presentation, 3, 0);
}
""",
    ),
    (
        "releases some OTHER slot, not its own",
        """}
void Ancilla98_Example(int k) {
  for (int i = 0; i < 5; i++) {
    if (ancilla_type[i] == kAncillaType_RandoIconReceipt)
      ancilla_type[i] = 0;
  }
  Rando_GrantLocation(loc, item, code, pres, 0, 0);
}
""",
    ),
]


def fixture_selftest():
    bad = 0
    for name, src in FIXTURES:
        if not scan(src.splitlines(), "<fixture>"):
            print("check_ancilla_slot_ordering: SELFTEST FAILED - the guard "
                  "accepts a handler that %s." % name, file=sys.stderr)
            bad += 1
    # And the compliant shape must still pass, or the guard is just noise.
    ok_src = """}
void Ancilla97_Example(int k) {
  ancilla_type[k] = 0;
  Rando_GrantLocation(loc, item, code, pres, 0, 0);
}
"""
    if scan(ok_src.splitlines(), "<fixture>"):
        print("check_ancilla_slot_ordering: SELFTEST FAILED - the guard rejects "
              "a handler that DOES release its own slot first.", file=sys.stderr)
        bad += 1
    if bad:
        return 1
    print("check_ancilla_slot_ordering: fixture selftest OK (%d false-negative "
          "shapes rejected, compliant shape accepted)" % len(FIXTURES))
    return 0


def selftest():
    """The guard must REJECT the revision that contained the original bug."""
    if fixture_selftest() != 0:
        return 1
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
