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

The matrix exercises all three accessibility values through accepted slots. It also
includes the explicit `enemy_drop_checks=all` + `accessibility=locations`
incompatibility: all-tier overworld checks currently include missable pre/post-Aga
windows, so both slot and CLI generation must reject that combination. This keeps a
stable end-to-end refusal canary without relying on a particular seed remaining
unreachable after legitimate logic improvements.

`Rando_GenerateSlot` writes `saves/sram.dat` + the sidecar + a spoiler relative
to the CWD, so every cell runs in its own scratch temp directory (with `saves/`
and `spoilers/` pre-created) to avoid clobbering anything.

Usage:
  python assets/scripts/check_rando_slot_path.py
  python assets/scripts/check_rando_slot_path.py --binary=bin/x64-Release/zelda3.exe
  python assets/scripts/check_rando_slot_path.py --jobs=1
  python assets/scripts/check_rando_slot_path.py --allow-missing-binary
  python assets/scripts/check_rando_slot_path.py --allow-missing-pot-registry

The last mode is an assetless refusal test, not a skip: both slot and CLI pot
generation must reject specifically because the registry is missing.
"""
from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent.parent
GEN_TIMEOUT_S = 120


def find_binary_default() -> Path:
    """Return the built binary path most likely to run on this host."""
    windows_candidates = [
        REPO / "bin" / "x64-Release" / "zelda3.exe",
        REPO / "zelda3.exe",
    ]
    posix_candidates = [
        REPO / "zelda3",
        REPO / "bin" / "x64-Release" / "zelda3.exe",
    ]
    candidates = windows_candidates if os.name == "nt" else posix_candidates
    for candidate in candidates:
        if candidate.exists():
            return candidate
    return candidates[0]


DEFAULT_BINARY = find_binary_default()


def default_jobs() -> int:
    return max(1, min(8, os.cpu_count() or 1))


def local_pot_registry_available() -> bool:
    """Return whether this checkout's generated pot registry has real rows."""
    header = REPO / "src" / "rando" / "pot_lookup.h"
    try:
        text = header.read_text(encoding="utf-8")
    except OSError:
        return False
    for macro in ("kRandoPotRegistryCount", "kRandoPotLookup_COUNT"):
        match = re.search(rf"^#define\s+{macro}\s+(\d+)\b", text, re.MULTILINE)
        if match:
            return int(match.group(1)) > 0
    return False


def local_ow_graph_available() -> bool:
    """Return whether this checkout's baked OW warp graph has real tables."""
    source = REPO / "src" / "rando" / "logic_data.c"
    try:
        text = source.read_text(encoding="utf-8")
    except OSError:
        return False
    match = re.search(r"const\s+uint8\s+kRandoOwGraphPresent\s*=\s*(\d+)\s*;",
                      text)
    return bool(match) and int(match.group(1)) != 0


def local_enemy_registry_available() -> bool:
    """Return whether this checkout's generated enemy-drop registry has rows.

    Without it the generator refuses any enemy_drop_checks seed for the
    REGISTRY-ABSENT reason, which pre-empts every semantic refusal downstream of
    it -- so a cell asserting one of those reasons has to be gated on this the
    same way the pot/owgraph cells are.
    """
    header = REPO / "src" / "rando" / "enemy_drop_lookup.h"
    try:
        text = header.read_text(encoding="utf-8")
    except OSError:
        return False
    for macro in ("kRandoEnemyDropRegistryCount", "kRandoEnemyDropLookup_COUNT"):
        match = re.search(rf"^#define\s+{macro}\s+(\d+)\b", text, re.MULTILINE)
        if match:
            return int(match.group(1)) > 0
    return False


# (label, settings_csv, seed, expect_ok, check_parity, expect_world_state,
#  required_local_artifact, expected_refusal_reason)
# - expect_ok:        slot generation should succeed (True) or be refused (False)
# - check_parity:     also run --generate-seed and require an identical digest
#                     (only meaningful for accepted, attempt-0-success cells)
# - expect_world_state: persisted slot header world_state (0=open,1=standard,
#                     2=inverted,3=retro per WorldState enum); None to skip
# - required_local_artifact: None, "pots", "owgraph", or "enemy". Public CI has
#                     no ROM-derived pot registry, no OW warp graph and no
#                     enemy-drop registry; with the matching --allow-missing-*
#                     flag, such cells become fail-closed refusal tests: BOTH
#                     slot and CLI paths must reject specifically because that
#                     artifact is absent. Tag every cell whose settings TOUCH a
#                     local artifact, not just the ones whose feature it is --
#                     the artifact-absent refusal pre-empts whatever semantic
#                     reason the cell would otherwise assert.
# - expected_refusal_reason: stable semantic reason required from BOTH paths for
#                     an intentionally rejected cell; None for accepted cells.
MATRIX = [
    ("open-fg-items",       "mode.state=open,goal=fast_ganon,accessibility=items",     "0x77", True,  True,  0, None, None),
    ("open-fg-locations",   "mode.state=open,goal=fast_ganon,accessibility=locations", "0x77", True,  True,  0, None, None),
    ("open-fg-beatable",    "mode.state=open,goal=fast_ganon,accessibility=none",      "0x77", True,  True,  0, None, None),
    ("open-fg-beatable-alias", "mode.state=open,goal=fast_ganon,accessibility=beatable","0x77", True, True,  0, None, None),
    ("standard-fg",         "mode.state=standard,goal=fast_ganon",                     "0x3",  True,  True,  1, None, None),
    ("inverted-fg",         "mode.state=inverted,goal=fast_ganon",                     "0x50", True,  True,  2, None, None),
    ("open-completionist",  "mode.state=open,goal=completionist",                      "0x7",  True,  True,  0, None, None),
    ("door-pot-keys-open",  "mode.state=open,goal=fast_ganon,door_shuffle=basic,pot_shuffle=keys", "0x504f54", True, True, 0, "pots", None),
    ("door-pot-all-items-open", "mode.state=open,goal=fast_ganon,door_shuffle=basic,pot_shuffle=all,accessibility=items", "0x504f55", True, True, 0, "pots", None),
    # OW warp axes ride the sidecar ext v12 identity (ow_attempt/ow_digest24)
    # through persistence + digest parity; assetless CI asserts the semantic
    # fail-closed refusal instead (graph tables absent).
    ("warp-both-open",      "mode.state=open,goal=fast_ganon,flute_shuffle=random,whirlpool_shuffle=true", "0x159", True, True, 0, "owgraph", None),
    # Stable slot/CLI rejection: all-tier overworld checks include missable
    # pre/post-Aga windows, so 100%-locations is intentionally unsupported.
    # Tagged "enemy": assetlessly the REGISTRY-ABSENT refusal fires first and
    # pre-empts this semantic one, so without the tag the cell asserted a reason
    # public CI structurally cannot produce.
    ("all-enemy-locations-REJECT", "mode.state=open,goal=fast_ganon,dungeon_items.small_keys=wild,enemy_drop_checks=all,accessibility=locations", "0x12", False, False, None, "enemy", "unsupported-enemy-locations"),
    # Seed reselected 3 -> 1 (2026-07-16): regenerating the stale Jul-7
    # gitignored artifacts (the room-0x099 big-key self-lock gates + the
    # CanKillHceThings alignment) tipped seed 3 from feasible to the
    # documented marginal hunt-goal-at-items-tier refusal (seed 8 also
    # refuses; 1/2/4-7/9-11 pass — verified by sweep on main-identical code,
    # A/B-proven pre-existing, NOT a shopsanity regression). Both cells stay
    # paired on one seed so the items/beatable tiers compare like-for-like.
    ("std-ganonhunt-hard-items-OK",     "mode.state=standard,goal=ganonhunt,item_pool=hard,accessibility=items", "1", True, True, 1, None, None),
    ("std-ganonhunt-hard-beatable-OK",  "mode.state=standard,goal=ganonhunt,item_pool=hard,accessibility=none",  "1", True,  True,  1, None, None),
]


def run_slot(binary: Path, settings: str, seed: str) -> dict | None:
    """Run --generate-slot in a scratch CWD; return the parsed JSON (or None)."""
    with tempfile.TemporaryDirectory() as td:
        (Path(td) / "saves").mkdir()
        (Path(td) / "spoilers").mkdir()
        try:
            proc = subprocess.run(
                [str(binary), "--generate-slot", f"--settings={settings}", f"--seed={seed}"],
                cwd=td, capture_output=True, text=True, timeout=GEN_TIMEOUT_S,
            )
        except OSError as e:
            print(f"  failed to launch {binary}: {e}", file=sys.stderr)
            return None
        except subprocess.TimeoutExpired:
            print(f"  slot generation timed out after {GEN_TIMEOUT_S}s", file=sys.stderr)
            return None
        # The binary prints exactly one JSON line on stdout regardless of exit
        # code (ok=false carries the error). exit 0 == accepted, 1 == refused.
        line = next((l for l in proc.stdout.splitlines() if l.startswith("{")), None)
        if line is None:
            print(f"  no JSON on stdout (exit {proc.returncode}); stderr tail:\n"
                  f"    {proc.stderr.strip()[-300:]}", file=sys.stderr)
            return None
        try:
            result = json.loads(line)
        except json.JSONDecodeError as exc:
            print(f"  malformed JSON on stdout: {exc}", file=sys.stderr)
            return None
        if not isinstance(result, dict):
            print("  slot JSON result is not an object", file=sys.stderr)
            return None
        # Private harness metadata used to prove semantic refusal. The public
        # JSON error is intentionally user-facing and may only say
        # "placement failed"; stderr carries BuildItemPool's precise reason.
        result["_returncode"] = proc.returncode
        result["_stderr"] = proc.stderr
        return result


def semantic_refusal(stderr: str) -> str | None:
    """Classify only stable, intentional generator refusals."""
    if (
        "pot_shuffle requested" in stderr
        and "pots.gen.yaml" in stderr
        and "rebuild before generating pot-shuffle seeds" in stderr
    ):
        return "missing-pot-registry"
    if (
        "enemy_drop_checks=all" in stderr
        and "accessibility=locations" in stderr
        and ("unsupported" in stderr or "not supported" in stderr)
    ):
        return "unsupported-enemy-locations"
    if (
        "OW graph tables are absent/empty" in stderr
        and "refusing" in stderr
    ):
        return "missing-ow-graph"
    if (
        "enemy_drop_checks requires the generated" in stderr
        and "enemy-drop registry" in stderr
    ):
        return "missing-enemy-registry"
    return None


def run_seed_digest(binary: Path, settings: str, seed: str) -> dict[str, str | None]:
    """Run the CLI path and distinguish acceptance, expected refusal, and error."""
    with tempfile.TemporaryDirectory() as td:
        out_json = Path(td) / "out.json"
        try:
            proc = subprocess.run(
                [str(binary), "--generate-seed", f"--settings={settings}",
                 f"--seed={seed}", f"--out-spoiler={out_json}"],
                cwd=td, check=False, capture_output=True, text=True,
                timeout=GEN_TIMEOUT_S,
            )
        except OSError as exc:
            return {"status": "error", "digest": None,
                    "detail": f"launch failed: {exc}"}
        except subprocess.TimeoutExpired:
            return {"status": "error", "digest": None,
                    "detail": f"timed out after {GEN_TIMEOUT_S}s"}

        stderr_tail = proc.stderr.strip()[-500:]
        refusal = semantic_refusal(proc.stderr)
        if proc.returncode == 1 and refusal is not None:
            return {"status": "rejected", "digest": None,
                    "reason": refusal, "detail": stderr_tail}
        if proc.returncode != 0:
            return {"status": "error", "digest": None,
                    "detail": f"unexpected exit {proc.returncode}: {stderr_tail}"}
        if not out_json.exists():
            return {"status": "error", "digest": None,
                    "detail": "exit 0 without spoiler JSON"}
        try:
            digest = json.loads(out_json.read_text(encoding="utf-8")).get(
                "meta", {}).get("placement_digest_hex")
        except (OSError, json.JSONDecodeError) as exc:
            return {"status": "error", "digest": None,
                    "detail": f"unreadable spoiler JSON: {exc}"}
        if not (isinstance(digest, str) and
                re.fullmatch(r"[0-9a-fA-F]{64}", digest)):
            return {"status": "error", "digest": None,
                    "detail": f"missing/malformed placement digest: {digest!r}"}
        return {"status": "accepted", "digest": digest, "detail": stderr_tail}


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=DEFAULT_BINARY)
    parser.add_argument("--jobs", type=int, default=default_jobs(),
                        help="parallel matrix cells (default min(8, CPU count); "
                             "use 1 for serial)")
    parser.add_argument(
        "--allow-missing-binary", action="store_true",
        help="explicitly skip the runtime matrix when the binary is absent",
    )
    parser.add_argument(
        "--allow-missing-pot-registry", action="store_true",
        help="when the local registry is absent, require both slot and CLI "
             "pot-shuffle paths to refuse specifically for that reason",
    )
    parser.add_argument(
        "--allow-missing-ow-graph", action="store_true",
        help="when the baked OW warp graph is absent, require both slot and "
             "CLI warp-axis paths to refuse specifically for that reason",
    )
    parser.add_argument(
        "--allow-missing-enemy-registry", action="store_true",
        help="when the generated enemy-drop registry is absent, require both "
             "slot and CLI enemy-drop paths to refuse specifically for that "
             "reason",
    )
    args = parser.parse_args(argv)

    if not args.binary.is_file():
        if args.allow_missing_binary:
            print(f"check_rando_slot_path: binary {args.binary} not found - "
                  "explicitly skipped (--allow-missing-binary).")
            return 0
        print(f"check_rando_slot_path: binary {args.binary} not found; the "
              "requested runtime check cannot run. Build it or pass "
              "--allow-missing-binary explicitly.", file=sys.stderr)
        return 2
    args.binary = args.binary.resolve()

    print(f"check_rando_slot_path: binary {args.binary}")
    print(f"check_rando_slot_path: jobs {max(1, args.jobs)}")
    pot_registry_present = local_pot_registry_available()
    if not pot_registry_present and not args.allow_missing_pot_registry:
        print("check_rando_slot_path: local pot registry is absent; the requested "
              "matrix includes pot-shuffle cells. Prepare local artifacts or "
              "pass --allow-missing-pot-registry explicitly (public assetless CI).",
              file=sys.stderr)
        return 2
    ow_graph_present = local_ow_graph_available()
    if not ow_graph_present and not args.allow_missing_ow_graph:
        print("check_rando_slot_path: baked OW warp graph is absent; the requested "
              "matrix includes warp-axis cells. Prepare local artifacts or "
              "pass --allow-missing-ow-graph explicitly (public assetless CI).",
              file=sys.stderr)
        return 2
    enemy_registry_present = local_enemy_registry_available()
    if not enemy_registry_present and not args.allow_missing_enemy_registry:
        print("check_rando_slot_path: generated enemy-drop registry is absent; the "
              "requested matrix includes enemy-drop cells. Prepare local artifacts "
              "or pass --allow-missing-enemy-registry explicitly (public assetless "
              "CI).", file=sys.stderr)
        return 2
    # required_local_artifact -> (present?, semantic refusal reason, label)
    artifact_state = {
        "pots": (pot_registry_present, "missing-pot-registry", "pot registry"),
        "owgraph": (ow_graph_present, "missing-ow-graph", "OW warp graph"),
        "enemy": (enemy_registry_present, "missing-enemy-registry",
                  "enemy-drop registry"),
    }
    jobs = max(1, args.jobs)

    def run_cell(cell):
        (label, settings, seed, expect_ok, check_parity, expect_ws,
         required_artifact, expected_refusal_reason) = cell
        lines: list[str] = []
        res = run_slot(args.binary, settings, seed)
        if res is None:
            lines.append(f"  FAIL [{label}]: no result")
            return lines, True

        raw_ok = res.get("ok")
        if not isinstance(raw_ok, bool):
            lines.append(
                f"  FAIL [{label}]: slot JSON ok must be boolean, got {raw_ok!r}"
            )
            return lines, True
        ok = raw_ok
        if required_artifact is not None and not artifact_state[required_artifact][0]:
            _, refusal_reason, artifact_label = artifact_state[required_artifact]
            slot_reason = semantic_refusal(
                f"{res.get('_stderr', '')}\n{res.get('error', '')}"
            )
            slot_refused = (
                not ok
                and res.get("_returncode") == 1
                and slot_reason == refusal_reason
            )
            cli = run_seed_digest(args.binary, settings, seed)
            cli_refused = (
                cli.get("status") == "rejected"
                and cli.get("reason") == refusal_reason
            )
            if slot_refused and cli_refused:
                lines.append(
                    f"  OK   [{label}]: missing {artifact_label} refused by both slot and CLI paths"
                )
                return lines, False
            if ok:
                lines.append(
                    f"  FAIL [{label}]: slot path accepted this cell without the {artifact_label}"
                )
            else:
                lines.append(
                    f"  FAIL [{label}]: slot path did not produce the semantic {refusal_reason} "
                    f"refusal (exit={res.get('_returncode')}, reason={slot_reason!r}, "
                    f"error={res.get('error', '')!r})"
                )
            if cli.get("status") == "accepted":
                lines.append(
                    f"        CLI path also accepted (digest {str(cli.get('digest'))[:8]}...)"
                )
            elif not cli_refused:
                lines.append(
                    f"        CLI path did not produce the semantic {refusal_reason} refusal "
                    f"(status={cli.get('status')}, reason={cli.get('reason')!r}, "
                    f"detail={cli.get('detail')})"
                )
            return lines, True
        if ok != expect_ok:
            lines.append(f"  FAIL [{label}]: ok={ok}, expected {expect_ok} "
                         f"(error={res.get('error','')!r})")
            return lines, True

        if not ok:
            # Correctly refused only if BOTH paths use the matrix's stable,
            # intentional reason and the slot path returns the refusal exit.
            if expected_refusal_reason is None:
                lines.append(f"  FAIL [{label}]: rejected cell has no expected semantic reason")
                return lines, True
            slot_reason = semantic_refusal(
                f"{res.get('_stderr', '')}\n{res.get('error', '')}"
            )
            if (res.get("_returncode") != 1 or
                    slot_reason != expected_refusal_reason):
                lines.append(
                    f"  FAIL [{label}]: slot path did not produce expected refusal "
                    f"(exit={res.get('_returncode')}, reason={slot_reason!r}, "
                    f"expected={expected_refusal_reason!r})"
                )
                return lines, True
            cli = run_seed_digest(args.binary, settings, seed)
            if (cli.get("status") == "rejected" and
                    cli.get("reason") == expected_refusal_reason):
                lines.append(
                    f"  OK   [{label}]: both paths refused with "
                    f"{expected_refusal_reason} (as expected)"
                )
                return lines, False
            lines.append(
                f"  FAIL [{label}]: CLI path did not produce expected refusal "
                f"(status={cli.get('status')}, reason={cli.get('reason')!r}, "
                f"expected={expected_refusal_reason!r}, detail={cli.get('detail')})"
            )
            return lines, True

        if res.get("_returncode") != 0:
            lines.append(
                f"  FAIL [{label}]: slot JSON reported ok=true with exit "
                f"{res.get('_returncode')}"
            )
            return lines, True

        if res.get("roundtrip_ok") is not True:
            lines.append(f"  FAIL [{label}]: slot did not round-trip "
                         f"(world_state={res.get('world_state')}, "
                         f"slot_kind={res.get('slot_kind')})")
            return lines, True

        if expect_ws is not None and res.get("world_state") != expect_ws:
            lines.append(f"  FAIL [{label}]: world_state={res.get('world_state')}, expected {expect_ws}")
            return lines, True

        if check_parity:
            cli = run_seed_digest(args.binary, settings, seed)
            if cli["status"] != "accepted":
                lines.append(f"  FAIL [{label}]: CLI path failed to generate "
                             f"(parity check: {cli['detail']})")
                return lines, True
            cli_digest = cli["digest"]
            if cli_digest != res.get("placement_digest_hex"):
                lines.append(f"  FAIL [{label}]: digest parity mismatch")
                lines.append(f"        slot: {res.get('placement_digest_hex')}")
                lines.append(f"        cli : {cli_digest}")
                return lines, True

        lines.append(f"  OK   [{label}]: {res.get('placement_digest_hex','')[:16]}... "
                     f"(ws={res.get('world_state')}, roundtrip+parity)")
        return lines, False

    results = {}
    if jobs == 1:
        for idx, cell in enumerate(MATRIX):
            results[idx] = run_cell(cell)
    else:
        with ThreadPoolExecutor(max_workers=jobs) as executor:
            future_to_idx = {executor.submit(run_cell, cell): idx
                             for idx, cell in enumerate(MATRIX)}
            for future in as_completed(future_to_idx):
                idx = future_to_idx[future]
                try:
                    results[idx] = future.result()
                except Exception as e:
                    label = MATRIX[idx][0]
                    results[idx] = ([f"  FAIL [{label}]: runner error: {e!r}"], True)

    failures = 0
    for idx in range(len(MATRIX)):
        lines, failed = results[idx]
        for line in lines:
            print(line)
        failures += 1 if failed else 0

    n = len(MATRIX)
    if failures:
        print(f"\ncheck_rando_slot_path: {failures} of {n} run cells FAILED.")
        return 1
    print(f"\ncheck_rando_slot_path: all {n} cells OK.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
