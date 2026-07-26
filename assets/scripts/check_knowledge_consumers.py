#!/usr/bin/env python3
"""Reject new full-knowledge consumers outside the audited allowlist.

tracker-player-knowledge invariant (openspec randomizer-player-knowledge): a
player-facing surface may only present information the player could know at
that moment. The knowledge-limited live view (Rando_GetLiveReachability) is
the ONLY reachability a display surface may consume; the full-knowledge flood
and the raw per-seed assignment getters are reserved for generation, gameplay
delivery, the spoiler writer, and selfchecks. This guard fails the build when
a file outside the audited allowlist calls one of those APIs, so a future
tracker/panel/export reaching for full knowledge is a build break, not a
silent spoiler (the check_grant_consumers.py pattern, file granularity).

Escape hatch for an intentional exception: put `knowledge-guard: allow
<reason>` in a comment on the call line or the line above it.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import re
import sys

REPO = Path(__file__).resolve().parents[2]
SOURCE_ROOT = REPO / "src"
ESCAPE = "knowledge-guard: allow"

# symbol -> set of repo-relative files (POSIX separators) allowed to CALL it.
# Every entry is justified by the tracker-player-knowledge audit matrix
# (design.md): generation/placer, gameplay delivery at the site the player is
# standing in, spoiler/reveal, persistence plumbing, or selfchecks. Extending
# an allowlist requires a written justification in the change that does it.
GUARDS: dict[str, set[str]] = {
    # Full-knowledge reachability floods — generation/selftest only. Display
    # surfaces consume Rando_GetLiveReachability (knowledge-limited) instead.
    "Logic_ComputeReachabilityFullKnowledge": {
        "src/main.c",                       # CLI generation + accessibility sweeps
        "src/rando/rando.c",                # generation entries + selfchecks + live bridge internals
        "src/rando/rando_logic.c",          # definition + logic selfchecks
        "src/rando/rando_placement.c",      # assumed fill + accessibility gates
    },
    "Logic_ExpandReachability": {
        "src/rando/rando_logic.c",          # definition
        "src/rando/rando_placement.c",      # fill-time incremental expansion
    },
    # Reachability_Snapshot copies whatever is in the SHARED g_reachability
    # buffer — after any generation/goal/selftest call that is the FULL
    # knowledge flood. Only the live bridge (which snapshots its own masked
    # result) may call it.
    "Reachability_Snapshot": {
        "src/rando/rando_logic.c",          # definition
        "src/rando/rando.c",                # live bridge: snapshots the masked result
    },
    # Raw per-seed topology layouts. Handing these to a display surface leaks
    # the whole mapping regardless of the flood gating.
    "Rando_ActiveOwWarpLayout": {
        "src/rando/rando.c",                # store definition + activation
        "src/rando/shuffle_ow_warp.c",      # runtime warp execution (gameplay delivery)
    },
    "Chains_RuntimeGetSession": {
        "src/rando/chains_runtime.c",       # definition
        "src/rando/rando_snapshot_tail.c",  # session persistence
    },
    # Raw per-seed assignment getters — the true (hidden) assignments.
    "Rando_GetDungeonPrizeAssignment": {
        "src/rando/rando.c",                # store definition + activation/selfchecks
        "src/rando/rando_logic.c",          # predicate-VM context (generation + live flood input)
        "src/rando/rando_placement.c",      # placer prize handling (generation)
        "src/dungeon.c",                    # falling-prize sprite (fires after the boss kill)
        "src/rando/rando_window/tracker_windows.cpp",  # prize column: "?"-gated until obtained (audited)
    },
    "Rando_GetMedallionAssignment": {
        "src/rando/rando.c",
        "src/rando/rando_logic.c",
        "src/rando/rando_generate.c",       # slot generation (spoiler fields)
        "src/ancilla.c",                    # medallion-cast gameplay gates at the entrance
        "src/main.c",                       # spoiler build
        "src/rando/medallion_icons.c",      # deliberate in-world reveal at the entrance
    },
    "Rando_GetBossAssignment": {
        "src/rando/rando.c",
        "src/rando/rando_logic.c",
        "src/rando/rando_placement.c",      # placer boss-kill modelling (generation)
    },
    # Raw connection accessors. Rando_EntranceConnection and DoorRt_GetLink
    # return true topology; today's consumers gate on the discovery bits
    # (auto_tracker exports discovered connections only) — a new consumer must
    # prove the same.
    "Rando_EntranceConnection": {
        "src/rando/rando.c",                # definition
        "src/rando/auto_tracker.c",         # discovery-gated export (audited)
    },
    "DoorRt_GetLink": {
        "src/rando/door_runtime.c",         # definition + runtime redirects
        "src/rando/auto_tracker.c",         # discovery-gated export (audited)
    },
    # Placed-item names — the item-identity channel. Existing consumers are
    # race-gated displays, receipt/delivery text at the location itself, or
    # spoiler surfaces (all audited); a new caller must be classified.
    "Rando_GetItemName": {
        "src/rando/rando.c",                # receipt/soul/trap text at delivery
        "src/rando/rando_dialogue.c",       # delivery messages at the location
        "src/rando/rando_hints.c",          # hint text (race-gated at display)
        "src/rando/rando_spoiler.c",        # spoiler writer (intentional)
        "src/rando/rando_generate.c",       # generation logs/spoiler
        "src/rando/rando_placement.c",      # placer diagnostics (generation)
        "src/rando/customizer.c",           # pre-generation pool editor UI
        "src/rando/seed_shape.c",           # pre-generation pool summary
        "src/main.c",                       # CLI spoiler/debug output
        "src/rando/rando_window/tracker_windows.cpp",   # race-gated "Show items (spoiler)" (audited)
        "src/rando/rando_window/rando_reach_panel.cpp", # race-gated item column (audited)
        "src/rando/rando_window/rando_window.cpp",      # race-gated spoiler tab (audited)
        "src/rando/rando_window/panels_selftest.cpp",   # selfcheck
    },
}

CALL_RE = {sym: re.compile(r"\b" + re.escape(sym) + r"\s*\(") for sym in GUARDS}

# Words that can legally precede a CALL, so a line starting with one is never a
# prototype no matter how type-like it looks (`return Foo(x);`).
_CALL_LEAD_KEYWORDS = {
    "return", "if", "while", "for", "switch", "case", "else", "do", "sizeof",
    "and", "or", "not",
}
# A function DECLARATION (prototype) mentions the symbol without consuming its
# result: `uint16 Rando_Foo(const X *s);`. Scanning headers is worthwhile (an
# inline helper in a .h can consume a guarded API), but every header that
# DECLARES one of these would otherwise be a false positive, so recognise and
# skip the prototype shape: the line ends the statement, and everything before
# the symbol is a return type — identifiers/`*`/qualifiers only, with no
# operator and no call-leading keyword.
_DECL_PREFIX_RE = re.compile(r"^[\w \t*]+$")


def is_declaration(line: str, sym: str) -> bool:
    line = line.split("//", 1)[0]  # drop a trailing line comment
    head, _, tail = line.partition(sym)
    tail = tail.rstrip()
    # `;` ends a one-line prototype; `,` is a prototype whose parameter list
    # wraps to the next line. Either is only reached when the prefix below is a
    # bare return type, so a wrapped CALL (`x = foo(a,`) still falls through.
    if not (tail.endswith(";") or tail.endswith(",")):
        return False  # a call used as an expression, not a prototype
    head = head.strip()
    if not head or not _DECL_PREFIX_RE.match(head):
        return False  # an operator (=, &&, (, ...) precedes it => it is a call
    last_word = head.split()[-1].lstrip("*")
    return last_word not in _CALL_LEAD_KEYWORDS


def iter_source_files():
    # Headers included: an inline helper in a .h can consume a guarded API just
    # as easily as a .c, and the file-granular allowlist covers it identically.
    for ext in ("*.c", "*.cpp", "*.h", "*.hpp"):
        yield from SOURCE_ROOT.rglob(ext)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--list", action="store_true",
                        help="list every guarded call site with its verdict")
    args = parser.parse_args()

    violations: list[str] = []
    for path in sorted(iter_source_files()):
        rel = path.relative_to(REPO).as_posix()
        if rel.startswith("third_party/"):
            continue
        # Generated at build time from the logic YAMLs (gitignored) — its
        # content is codegen output on the generation side, not a hand-written
        # consumer this guard could usefully police.
        if rel == "src/rando/logic_data.c":
            continue
        try:
            lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
        except OSError as e:
            print(f"check_knowledge_consumers: cannot read {rel}: {e}", file=sys.stderr)
            return 1
        for i, line in enumerate(lines):
            for sym, rx in CALL_RE.items():
                if not rx.search(line):
                    continue
                if is_declaration(line, sym):
                    continue  # prototype, not a consumer
                allowed = rel in GUARDS[sym]
                escaped = ESCAPE in line or (i > 0 and ESCAPE in lines[i - 1])
                if args.list:
                    verdict = "allow" if allowed else ("escape" if escaped else "VIOLATION")
                    print(f"{verdict:9} {sym:40} {rel}:{i + 1}")
                if not allowed and not escaped:
                    violations.append(f"{rel}:{i + 1}: {sym} outside the audited allowlist")

    if violations:
        print("check_knowledge_consumers: full-knowledge API consumed outside the "
              "audited allowlist (player-knowledge invariant, see "
              "openspec/specs/randomizer-player-knowledge):", file=sys.stderr)
        for v in violations:
            print(f"  {v}", file=sys.stderr)
        print("  Fix: consume Rando_GetLiveReachability / a discovery-gated view, "
              "or add `knowledge-guard: allow <reason>` with justification.",
              file=sys.stderr)
        return 1
    print("check_knowledge_consumers: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
