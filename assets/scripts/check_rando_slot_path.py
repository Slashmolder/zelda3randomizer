#!/usr/bin/env python3
"""Slot-path generation guard.

The regression corpus (`run_rando_corpus.py`) and `check_rando_invariants.py`
both drive the headless **CLI** generator (`--generate-seed`). But the PC native
settings window AND the in-game settings screen call a *different* function —
`Rando_GenerateSlot` (`src/rando/rando_generate.c`) — which additionally builds
and persists a sidecar slot (settings blob, share string, world_state header,
placement table). That path historically had **no automated test**; a
file-select render regression once passed the corpus + selftest + four audit
rounds and was caught only by playtest, precisely because nothing exercised the
slot path (see CLAUDE.md).

This guard closes that gap. For each matrix cell it runs the binary's
`--generate-slot` mode (which calls `Rando_GenerateSlot`, reads the persisted
slot back, and prints a one-line JSON result) and asserts:

  1. **accept/reject** matches the expected accessibility-tier outcome,
  2. **round-trip** — the persisted slot deserializes with the right
     `placement_digest`, `world_state`, and `slot_kind`,
  3. **parity** — for accepted cells, the slot-path placement digest equals the
     CLI path's (`--generate-seed`) digest for the same (settings, seed), so the
     UI path provably did not diverge from the corpus-tested path.

It deliberately includes a hard-pool standard Ganonhunt seed that the placer can
make goal-completable, but not 100%-inventory: it must REJECT under
`accessibility=items` and ACCEPT under `accessibility=none` ("beatable only").
That pair exercises the three-way accessibility gate end-to-end through the real
UI path.

`Rando_GenerateSlot` writes `saves/sram.dat` + the sidecar + a spoiler relative
to the CWD, so every cell runs in its own scratch temp directory (with `saves/`
and `spoilers/` pre-created) to avoid clobbering anything.

Usage:
  python assets/scripts/check_rando_slot_path.py
  python assets/scripts/check_rando_slot_path.py --binary=bin/x64-Release/zelda3.exe
"""
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent.parent
DEFAULT_BINARY = REPO / "zelda3"
GEN_TIMEOUT_S = 120


# (label, settings_csv, seed, expect_ok, check_parity, expect_world_state)
# - expect_ok:        slot generation should succeed (True) or be refused (False)
# - check_parity:     also run --generate-seed and require an identical digest
#                     (only meaningful for accepted, attempt-0-success cells)
# - expect_world_state: persisted slot header world_state (0=open,1=standard,
#                     2=inverted,3=retro per WorldState enum); None to skip
MATRIX = [
    ("open-fg-items",       "mode.state=open,goal=fast_ganon,accessibility=items",     "0x77", True,  True,  0),
    ("open-fg-locations",   "mode.state=open,goal=fast_ganon,accessibility=locations", "0x77", True,  True,  0),
    ("open-fg-beatable",    "mode.state=open,goal=fast_ganon,accessibility=none",      "0x77", True,  True,  0),
    ("open-fg-beatable-alias", "mode.state=open,goal=fast_ganon,accessibility=beatable","0x77", True, True,  0),
    ("standard-fg",         "mode.state=standard,goal=fast_ganon",                     "0x3",  True,  True,  1),
    ("inverted-fg",         "mode.state=inverted,goal=fast_ganon",                     "0x50", True,  True,  2),
    ("open-completionist",  "mode.state=open,goal=completionist",                      "0x7",  True,  True,  0),
    # The three-way accessibility gate, end-to-end through the slot path:
    # this hard-pool Ganonhunt seed has unreachable placements under strict
    # inventory, but the goal is still completable in beatable-only mode.
    ("std-ganonhunt-hard-items-REJECT", "mode.state=standard,goal=ganonhunt,item_pool=hard,accessibility=items", "3", False, False, None),
    ("std-ganonhunt-hard-beatable-OK",  "mode.state=standard,goal=ganonhunt,item_pool=hard,accessibility=none",  "3", True,  True,  1),
]


def run_slot(binary: Path, settings: str, seed: str) -> dict | None:
    """Run --generate-slot in a scratch CWD; return the parsed JSON (or None)."""
    with tempfile.TemporaryDirectory() as td:
        (Path(td) / "saves").mkdir()
        (Path(td) / "spoilers").mkdir()
        proc = subprocess.run(
            [str(binary), "--generate-slot", f"--settings={settings}", f"--seed={seed}"],
            cwd=td, capture_output=True, text=True, timeout=GEN_TIMEOUT_S,
        )
        # The binary prints exactly one JSON line on stdout regardless of exit
        # code (ok=false carries the error). exit 0 == accepted, 1 == refused.
        line = next((l for l in proc.stdout.splitlines() if l.startswith("{")), None)
        if line is None:
            print(f"  no JSON on stdout (exit {proc.returncode}); stderr tail:\n"
                  f"    {proc.stderr.strip()[-300:]}", file=sys.stderr)
            return None
        return json.loads(line)


def run_seed_digest(binary: Path, settings: str, seed: str) -> str | None:
    """Run the CLI --generate-seed path; return its placement_digest_hex."""
    with tempfile.TemporaryDirectory() as td:
        out_json = Path(td) / "out.json"
        try:
            subprocess.run(
                [str(binary), "--generate-seed", f"--settings={settings}",
                 f"--seed={seed}", f"--out-spoiler={out_json}"],
                cwd=td, check=True, capture_output=True, timeout=GEN_TIMEOUT_S,
            )
        except subprocess.CalledProcessError:
            return None
        if not out_json.exists():
            return None
        return json.loads(out_json.read_text(encoding="utf-8")).get("meta", {}).get("placement_digest_hex")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=DEFAULT_BINARY)
    args = parser.parse_args(argv)

    if not args.binary.exists():
        # Match run_rando_corpus.py: missing binary is a skip, not a hard fail,
        # so a docs-only / ROM-less checkout doesn't break.
        print(f"check_rando_slot_path: binary {args.binary} not found - skipping.")
        return 0
    args.binary = args.binary.resolve()

    print(f"check_rando_slot_path: binary {args.binary}")
    failures = 0
    for label, settings, seed, expect_ok, check_parity, expect_ws in MATRIX:
        res = run_slot(args.binary, settings, seed)
        if res is None:
            print(f"  FAIL [{label}]: no result")
            failures += 1
            continue

        ok = bool(res.get("ok"))
        if ok != expect_ok:
            print(f"  FAIL [{label}]: ok={ok}, expected {expect_ok} "
                  f"(error={res.get('error','')!r})")
            failures += 1
            continue

        if not ok:
            # Correctly refused. Confirm the CLI path refuses too (consistency).
            cli = run_seed_digest(args.binary, settings, seed)
            if cli is not None:
                print(f"  FAIL [{label}]: slot path refused but CLI path accepted "
                      f"(digest {cli[:8]}...) - paths disagree")
                failures += 1
            else:
                print(f"  OK   [{label}]: refused by both slot and CLI paths (as expected)")
            continue

        if not res.get("roundtrip_ok"):
            print(f"  FAIL [{label}]: slot did not round-trip "
                  f"(world_state={res.get('world_state')}, slot_kind={res.get('slot_kind')})")
            failures += 1
            continue

        if expect_ws is not None and res.get("world_state") != expect_ws:
            print(f"  FAIL [{label}]: world_state={res.get('world_state')}, expected {expect_ws}")
            failures += 1
            continue

        if check_parity:
            cli_digest = run_seed_digest(args.binary, settings, seed)
            if cli_digest is None:
                print(f"  FAIL [{label}]: CLI path failed to generate (parity check)")
                failures += 1
                continue
            if cli_digest != res.get("placement_digest_hex"):
                print(f"  FAIL [{label}]: digest parity mismatch\n"
                      f"        slot: {res.get('placement_digest_hex')}\n"
                      f"        cli : {cli_digest}")
                failures += 1
                continue

        print(f"  OK   [{label}]: {res.get('placement_digest_hex','')[:16]}... "
              f"(ws={res.get('world_state')}, roundtrip+parity)")

    n = len(MATRIX)
    if failures:
        print(f"\ncheck_rando_slot_path: {failures} of {n} cells FAILED.")
        return 1
    print(f"\ncheck_rando_slot_path: all {n} cells OK.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
