#!/usr/bin/env python3
"""Initialization + vanilla snapshot WRAM ownership guard (tasks.md §1.0d, §1.2).

Captures the freshly-built binary's post-``ZeldaInitialize`` WRAM in vanilla
mode, then restores each chapter savestate's base snapshot without stepping
frames. Both complete randomizer-owned WRAM ranges must remain zero.

The C binary emits a lossless hex dump plus a convenience ``ok=`` value. This
driver independently parses the inclusive ownership bounds from
``src/features.h``, requires an exact-length dump, decodes it, and checks every
byte itself. A stale or incomplete C-side ``ok=1`` therefore cannot green the
guard.

Usage:
  python assets/scripts/check_init_order.py           # fail closed + ownership check
  python assets/scripts/check_init_order.py --allow-missing-prerequisites
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

FEATURES_H = Path("src/features.h")
SAVES_REF = Path("saves/ref")

OWNED_BEGIN_NAME = "kRam_RandoOwnedBegin"
OWNED_END_NAME = "kRam_RandoOwnedEnd"
FIRST_OWNED_CELL = "kRam_Features1"
LAST_OWNED_CELL = "kRam_RandoTakeAnyDoor"


def discover_rando_owned_range() -> tuple[int, int] | None:
    """Independently parse the inclusive randomizer WRAM ownership bounds."""
    if not FEATURES_H.exists():
        return None
    text = FEATURES_H.read_text(encoding="utf-8", errors="replace")
    values: dict[str, int] = {}
    for name in (
        OWNED_BEGIN_NAME,
        OWNED_END_NAME,
        FIRST_OWNED_CELL,
        LAST_OWNED_CELL,
    ):
        matches = re.findall(
            rf"\b{re.escape(name)}\s*=\s*(0x[0-9a-fA-F]+)\b", text
        )
        if len(matches) != 1:
            return None
        values[name] = int(matches[0], 16)
    begin, end = values[OWNED_BEGIN_NAME], values[OWNED_END_NAME]
    if begin != values[FIRST_OWNED_CELL] or end != values[LAST_OWNED_CELL]:
        return None
    return (begin, end) if begin <= end else None


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
    KNOWN_KEYS = (
        "owned_begin=", "owned_end=", "initializer_bytes=", "snapshot_bytes=",
        "initializer_ok=", "snapshot_ok=", "ok=",
    )
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
            if k in out:
                return None
            out[k] = v
    return out


def run_one_chapter(
    binary: Path, savestate: Path, owned_range: tuple[int, int]
) -> tuple[bool, str]:
    """Validate the emitted initialization and restored-snapshot WRAM dumps."""
    import subprocess
    try:
        proc = subprocess.run(
            [str(binary), f"--vanilla-ram-check={savestate}"],
            capture_output=True,
            text=True,
            check=False,
        )
    except OSError as exc:
        return (False, f"unable to launch binary: {exc}")
    line = next(
        (candidate for candidate in proc.stdout.splitlines()
         if candidate.startswith("ram_check ")),
        "",
    )
    parsed = parse_ram_check_line(line)
    if not parsed:
        detail = proc.stderr.strip() or proc.stdout.strip()
        return (False, f"unparseable output (exit {proc.returncode}): {detail!r}")

    begin, end = owned_range
    try:
        emitted_begin = int(parsed["owned_begin"], 0)
        emitted_end = int(parsed["owned_end"], 0)
    except (KeyError, ValueError):
        return (False, f"missing/malformed ownership bounds: {line}")
    if (emitted_begin, emitted_end) != owned_range:
        return (
            False,
            f"range mismatch: source=0x{begin:03x}-0x{end:03x}; {line}",
        )

    expected_hex_len = (end - begin + 1) * 2
    decoded: dict[str, tuple[bool, list[tuple[int, int]]]] = {}
    for state in ("initializer", "snapshot"):
        raw_hex = parsed.get(f"{state}_bytes", "")
        if (len(raw_hex) != expected_hex_len or
                not re.fullmatch(r"[0-9a-fA-F]+", raw_hex)):
            return (
                False,
                f"{state} byte dump length/format mismatch "
                f"(expected {expected_hex_len} hex chars): {line}",
            )
        raw = bytes.fromhex(raw_hex)
        nonzero = [(begin + index, value) for index, value in enumerate(raw) if value]
        decoded[state] = (not nonzero, nonzero)

    independently_ok = all(state_ok for state_ok, _ in decoded.values())
    for state, (state_ok, _) in decoded.items():
        reported = parsed.get(f"{state}_ok") == "1"
        if parsed.get(f"{state}_ok") not in ("0", "1") or reported != state_ok:
            return (False, f"binary {state}_ok= disagrees with decoded bytes: {line}")
    reported_ok = parsed.get("ok") == "1"
    if parsed.get("ok") not in ("0", "1") or reported_ok != independently_ok:
        return (False, f"binary ok= disagrees with decoded byte ranges: {line}")
    expected_exit = 0 if independently_ok else 1
    if proc.returncode != expected_exit:
        return (
            False,
            f"binary exit {proc.returncode}, expected {expected_exit} for decoded bytes: {line}",
        )
    for state, (_, nonzero) in decoded.items():
        if nonzero:
            cells = ", ".join(f"0x{addr:03x}=0x{value:02x}" for addr, value in nonzero)
            return (False, f"non-zero {state} randomizer-owned WRAM ({cells}): {line}")
    return (True, line)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--strict",
        action="store_true",
        help="deprecated compatibility flag; fail-closed is now the default",
    )
    parser.add_argument(
        "--allow-missing-prerequisites",
        action="store_true",
        help="explicitly skip when savestates, RAM cells, or binary are absent",
    )
    parser.add_argument(
        "--binary",
        default="./bin/x64-Release/zelda3.exe",
        help="path to the built zelda3 binary",
    )
    args = parser.parse_args(argv)

    owned_range = discover_rando_owned_range()
    chapters = sorted(SAVES_REF.glob("Chapter*.sav")) if SAVES_REF.exists() else []

    if not chapters:
        print(f"check_init_order: no Chapter*.sav files under {SAVES_REF}.")
        return 0 if args.allow_missing_prerequisites else 2

    if owned_range is None:
        print(
            "check_init_order: missing, duplicate, or invalid randomizer WRAM "
            "ownership bounds in src/features.h."
        )
        return 0 if args.allow_missing_prerequisites else 2

    # Resolve to an absolute path: `Path("./zelda3")` stringifies back to
    # "zelda3" (pathlib strips the leading "./"), and subprocess would then
    # PATH-search for it — which fails on Linux/macOS where cwd isn't on PATH
    # (FileNotFoundError: 'zelda3'). An absolute path executes the built binary
    # directly. resolve(strict=False) is a no-op-safe even if it doesn't exist.
    binary = Path(args.binary).resolve()
    if not binary.is_file():
        print(f"check_init_order: binary not found at {binary}")
        return 0 if args.allow_missing_prerequisites else 2

    failures = 0
    for sav in chapters:
        ok, line = run_one_chapter(binary, sav, owned_range)
        marker = "OK  " if ok else "FAIL"
        print(f"  [{marker}] {line}")
        if not ok:
            failures += 1

    if failures:
        print(
            f"\ncheck_init_order: {failures} initialization/snapshot check(s) violated the "
            f"randomizer-owned WRAM contract.\n"
            f"  ZeldaInitialize or a committed vanilla reference snapshot contains\n"
            f"  data in addresses claimed for randomizer state. Investigate the collision."
        )
        return 1

    print(
        f"\ncheck_init_order: {len(chapters)} initialization/snapshot checks clean — every byte\n"
        f"  in 0x{owned_range[0]:03x}-0x{owned_range[1]:03x} was zero after "
        "ZeldaInitialize and vanilla restore (no frames stepped)."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
