#!/usr/bin/env python3
"""Derive each cave-entrance entry's UPSTREAM (ALTTPR) region from the PHP source.

An entry's `region:` in entrance_registry.yaml is the SOURCE region for
cave-entrance shuffle: whatever interior the shuffle puts behind that door has
its locations rebound to it. `Entrance_SelfCheck` (2) cross-validates that
region against the entry's own locations — but ONLY for entries that HAVE
locations. The location-less entries had no automated verification at all, which
is the gap this closes.

ALTTPR declares a `Shop`, `Shop\\TakeAny` or `Shop\\Upgrade` for most cave doors,
inside the PHP file of the region that door belongs to, and its 5th constructor
argument is the door id — `PreviousOverworldDoor`, i.e. our door row + 1. So the
region membership of a door is DERIVABLE from upstream rather than guessed from
an overworld screen, which is the method design D9 used and which is not
reliable for doors on a region boundary (see that change's note).

This is a ONE-SHOT authoring helper plus a drift check, NOT a build step: it
needs the ALTTPR checkout as a sibling directory. The derived values are
committed into the registry as `upstream_regions:`, and the CI-side assertion
that `region:` agrees with them lives in gen_entrance_door_rows.py --check,
which needs no PHP.

    python assets/scripts/gen_entrance_upstream_regions.py            # report
    python assets/scripts/gen_entrance_upstream_regions.py --emit     # yaml fragment
    python assets/scripts/gen_entrance_upstream_regions.py --check    # drift vs committed
"""
from __future__ import annotations

import argparse
import glob
import os
import re
import sys

import yaml

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
# ONE definition of "can upstream decide this entry's region", owned by the
# CI-side script (which must run without an upstream checkout). Importing rather
# than restating it keeps this report and the gate from ever disagreeing.
from gen_entrance_door_rows import is_pinnable  # noqa: E402
REG = os.path.join(HERE, "..", "rando", "entrance_registry.yaml")
SUBPATH = os.path.join("alttp_vt_randomizer", "app", "Region", "Standard")


def default_upstream() -> str:
    """Nearest `alttp_vt_randomizer` sibling, walking up from this file.

    A plain checkout has it one level above the repo root, but a git WORKTREE
    lives at <repo>/.claude/worktrees/<name>/, so a fixed number of `..` hops
    lands in the wrong place. Walk instead of counting.
    """
    d = HERE
    for _ in range(8):
        d = os.path.dirname(d)
        cand = os.path.join(d, SUBPATH)
        if os.path.isdir(cand):
            return cand
    return os.path.join(HERE, "..", "..", "..", SUBPATH)  # for the error message

# new Shop("Name", config, palette, room, door, $this, ...) — also Shop\TakeAny
# and Shop\Upgrade. Room and door are the 4th and 5th arguments.
SHOP_RE = re.compile(
    r'new\s+Shop(?:\\\w+)?\(\s*"([^"]+)"\s*,\s*0x[0-9A-Fa-f]+\s*,\s*0x[0-9A-Fa-f]+\s*,'
    r'\s*(0x[0-9A-Fa-f]+)\s*,\s*(0x[0-9A-Fa-f]+)\s*,')


def upstream_doors(root: str) -> dict[int, list[dict]]:
    """door id -> [{region, shop, src}] from every Standard region file."""
    out: dict[int, list[dict]] = {}
    for path in sorted(glob.glob(os.path.join(root, "**", "*.php"), recursive=True)):
        rel = os.path.relpath(path, root).replace(os.sep, "/")
        # app/Region/Standard/DarkWorld/DeathMountain/East.php -> DarkWorld_DeathMountain_East
        region = rel[:-4].replace("/", "_")
        text = open(path, encoding="utf-8", errors="replace").read()
        for m in SHOP_RE.finditer(text):
            door = int(m.group(3), 16)
            line = text[:m.start()].count("\n") + 1
            out.setdefault(door, []).append({
                "region": region,
                "shop": m.group(1),
                "src": f"app/Region/Standard/{rel}:{line}",
            })
    return out


def derive(interiors, doors) -> dict[int, list[dict]]:
    """entry index -> the upstream declarations covering its door rows."""
    found = {}
    for idx, it in enumerate(interiors):
        rows = it.get("door_rows") or []
        hits = []
        for r in sorted(rows):
            for h in doors.get(r + 1, []):
                hits.append(dict(h, door=r + 1))
        found[idx] = hits
    return found


def emit_fragment(interiors, found) -> None:
    print("# --- upstream_regions fragment (registry order, ALL entries) ---")
    for idx, it in enumerate(interiors):
        hits = found[idx]
        print(f"  # {idx:2d} {it['interior_id']}")
        if not hits:
            print("    upstream_regions: []   # upstream declares no shop/take-any at this door")
            continue
        print("    upstream_regions:")
        for h in hits:
            print(f"      - {{ region: {h['region']}, door: 0x{h['door']:02X}, "
                  f"shop: \"{h['shop']}\", src: \"{h['src']}\" }}")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--upstream", default=None)
    ap.add_argument("--emit", action="store_true", help="print the yaml fragment")
    ap.add_argument("--check", action="store_true",
                    help="verify the committed upstream_regions still match the PHP")
    ap.add_argument("--allow-missing-upstream", action="store_true",
                    help="skip (exit 0) when the ALTTPR checkout is absent")
    args = ap.parse_args()
    if args.upstream is None:
        args.upstream = default_upstream()

    if not os.path.isdir(args.upstream):
        msg = f"gen_entrance_upstream_regions: ALTTPR checkout not found at {args.upstream}"
        if args.allow_missing_upstream:
            print(msg + " — SKIPPED")
            return 0
        sys.exit(msg + " (clone it as a sibling directory, or pass --upstream)")

    with open(REG, "r", encoding="utf-8") as f:
        interiors = yaml.safe_load(f)["interiors"]
    doors = upstream_doors(args.upstream)
    found = derive(interiors, doors)

    if args.emit:
        emit_fragment(interiors, found)
        return 0

    if args.check:
        bad = 0
        for idx, it in enumerate(interiors):
            committed = it.get("upstream_regions")
            if committed is None:
                print(f"  entry {idx:2d} {it['interior_id']:<36} has no upstream_regions; "
                      f"re-run with --emit and paste", file=sys.stderr)
                bad += 1
                continue
            want = [{"region": h["region"], "door": h["door"], "shop": h["shop"],
                     "src": h["src"]} for h in found[idx]]
            got = [{"region": c["region"], "door": c["door"], "shop": c["shop"],
                    "src": c["src"]} for c in committed]
            if want != got:
                print(f"  entry {idx:2d} {it['interior_id']:<36} upstream drift:",
                      file=sys.stderr)
                print(f"        committed {got}", file=sys.stderr)
                print(f"        upstream  {want}", file=sys.stderr)
                bad += 1
        if bad:
            print(f"gen_entrance_upstream_regions: {bad} entr(y/ies) drifted from upstream.",
                  file=sys.stderr)
            return 1
        print(f"gen_entrance_upstream_regions: OK — all {len(interiors)} entries' "
              f"upstream_regions match the ALTTPR source.")
        return 0

    # Default: human report. `pinnable` is the only sound verdict class — see
    # is_pinnable() for why partial or split coverage cannot decide anything.
    print("     entry  interior                            cover  declared                        upstream")
    for idx, it in enumerate(interiors):
        hits = found[idx]
        rows = it.get("door_rows") or []
        covered = {h["door"] - 1 for h in hits}
        regions = sorted({h["region"] for h in hits})
        declared = it.get("region")
        pin = is_pinnable(rows, hits)
        mark = "!" if (pin and declared != regions[0]) else " "
        locs = "L" if it.get("locations") else " "
        tag = "PIN" if pin else ("---" if not regions else "~~~")
        print(f"{mark}{locs} {tag} {idx:3d}  {it['interior_id']:<34} "
              f"{len(covered)}/{len(rows)}  {str(declared):<30} "
              f"{', '.join(regions) if regions else '(none upstream)'}")
    print()
    print("  PIN = every door has an upstream declaration AND they agree -> verifiable")
    print("  ~~~ = partial or split coverage -> upstream cannot decide (see is_pinnable)")
    print("  --- = no upstream declaration for any door")
    print("  !   = PINNABLE and the declared region disagrees -> real candidate")
    print("  L   = entry has locations, so Entrance_SelfCheck (2) already verifies it")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
