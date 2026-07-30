#!/usr/bin/env python3
"""Regression guard for generator-156 Hints v1 race-spoiler reveal.

The checked-in ZRSR fixture was produced by the signed generator-156 binary at
commit 4b360270.  A current binary must be able to reveal that exact legacy
container with the frozen Hints 1/1 planner and serializer while continuing to
refuse unrelated generator versions, legacy-reserved hint-policy bits, and the
NPC reward-preview bit that generator 156 did not define.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path
from typing import Any


REPO = Path(__file__).resolve().parents[2]
FIXTURE = (
    REPO
    / "tests"
    / "rando_fixtures"
    / "hints"
    / "legacy_gen156_hints_v1_race.zrsr"
)

ZRSR_SIZE = 141
GENERATOR_OFFSET = 4
STAMP_OFFSET = 6
STAMP_SIZE = 32
SHARE_LENGTH_OFFSET = 38
SHARE_OFFSET = 42
SHARE_CAPACITY = 64
SETTINGS_OFFSET = 106
SETTINGS_SIZE = 31
CRC_OFFSET = 137

LEGACY_GENERATOR_VERSION = 156
OLDER_UNSUPPORTED_GENERATOR_VERSION = 155
FORMER_CONFIGURABLE_GENERATOR_VERSION = 157
LEGACY_ALGORITHM_VERSION = 1
LEGACY_TEXT_SCHEMA_VERSION = 1

# Canonical byte [28] bits 5-7 were refused by generator 156.  Hints v2 uses
# bit 5 as part of tile coverage, so accepting it while replaying the frozen
# 1/1 plan would silently reinterpret a crossed legacy/current container.
# The SEVEN configurable-hint policy bits generator 156 did not define. The
# reveal path refuses a legacy container that sets ANY of them (the `legacy_156`
# gate in rando.c, whose own comment says "those seven bits"), because accepting
# one would let a CRC-correct foreign artifact smuggle configurable policy into
# the frozen 1/1 builder.
#
# Driven as an exhaustive table rather than a few hand-picked mutations: this
# guard first exercised one bit, then four, while claiming to cover the refusal.
# Every canon[29] and canon[30] bit — the whole hint-mix-high and paid-depth
# axes — had no negative test at all, so a build that started accepting either
# would have passed. Keep this table in lockstep with that gate; a bit added
# there and not here is silently unguarded.
LEGACY_REFUSED_BITS = (
    (SETTINGS_OFFSET + 25, 1 << 7, "npc_reward_preview"),
    (SETTINGS_OFFSET + 28, 1 << 5, "tile_coverage_bit0"),
    (SETTINGS_OFFSET + 28, 1 << 6, "tile_coverage_bit1"),
    (SETTINGS_OFFSET + 28, 1 << 7, "hint_mix_low"),
    (SETTINGS_OFFSET + 29, 1 << 7, "hint_mix_high"),
    (SETTINGS_OFFSET + 30, 1 << 6, "paid_depth_bit0"),
    (SETTINGS_OFFSET + 30, 1 << 7, "paid_depth_bit1"),
)

EXPECTED_FIXTURE_SHA256 = (
    "737017237f941bc15a0514d39ed44814d9c11353fbce5c5cdd5d41e3374ff8ec"
)
EXPECTED_STAMP_SHA256 = (
    "eee8a4a1188694ebd781080968c8228997855012bcc4102ad7cd1649b09f2dcc"
)
EXPECTED_SHARE_STRING = "LJJFGU44PZQR6NUMQAMM3CGHZO2QI2GBOIA67TNLPBLDIEXUXM"
EXPECTED_CANONICAL_HEX = (
    "01010707000100000000000000000001010014001e00010000000000000000"
)
EXPECTED_PLACEMENT_DIGEST = (
    "69a249373c77c4e76ad9a2a874ac068c877a4d541ca60094d0362317da18c0e4"
)
EXPECTED_SPHERE_DIGEST = (
    "381e363cbc3c2bd64efecbe60949413660de3bdb96a4a3d0b46726c0cb4f4c0b"
)
EXPECTED_PLAN = {
    "algorithm_version": LEGACY_ALGORITHM_VERSION,
    "text_schema_version": LEGACY_TEXT_SCHEMA_VERSION,
    "digest": "bf3904d8fe0efce68de7499c7346d70b9b0e67a9dd3581e56be47c03d76e7d4d",
    "fact_count": 24,
    "primary_count": 18,
    "reserve_count": 6,
}

# Complete semantic/text projection of the 24 frozen delivery rows.  The raw
# spoiler stamp already pins every JSON byte; these rows make a hint-specific
# failure actionable instead of reporting only a whole-file hash mismatch.
# Tuple fields:
#   source_id, fact_id, fact_kind_id, template_id, template_parameter,
#   primary, tile_index, queue_index, location_id, item_id, region_id, text
EXPECTED_HINT_ROWS = (
    (1, 0, 1, 1, 0, True, 0, None, 171, 3, 18, "Glove is at Graveyard Ledge"),
    (2, 1, 1, 1, 0, True, 1, None, 180, 4, 19, "Bow Upgrade is at Ice Rod Cave"),
    (3, 2, 2, 1, 0, True, 2, None, 125, 65, 11, "GT Small Key is at Ganon's Tower - Tile Room"),
    (4, 3, 2, 1, 0, True, 3, None, 121, 35, 11, "Cape is at Ganon's Tower - Big Chest"),
    (5, 4, 2, 1, 0, True, 4, None, 37, 58, 24, "PoD Small Key is at Palace of Darkness - The Arena - Ledge"),
    (6, 5, 2, 1, 0, True, 5, None, 12, 34, 9, "Cane of Byrna is at Eastern Palace - Big Chest"),
    (7, 6, 3, 1, 0, True, 6, None, 208, 43, 3, "Bottle is at Catfish"),
    (8, 7, 4, 2, 1, True, 7, None, None, None, None, "The seed goal is Fast Ganon"),
    (9, 8, 6, 4, 12, True, 8, None, None, None, 18, "Light World - North West contains 12 useful items"),
    (10, 9, 6, 4, 9, True, 9, None, None, None, 11, "Ganon's Tower - Lobby contains 9 useful items"),
    (11, 10, 7, 1, 0, True, 10, None, 33, 57, 13, "Castle Tower Small Key is at Castle Tower - Dark Maze"),
    (12, 11, 7, 1, 0, True, 11, None, 64, 82, 25, "Skull Map is at Skull Woods - Map Chest"),
    (13, 12, 7, 1, 0, True, 12, None, 161, 19, 18, "Hookshot is at Blind's Hideout - Left"),
    (14, 13, 8, 5, 1, True, 13, None, None, None, None, "Fortune favors exploration"),
    (15, 14, 2, 1, 0, True, 14, None, 154, 31, 18, "Net is at Chicken House"),
    (19, 15, 2, 1, 0, True, None, 0, 94, 63, 23, "Mire Small Key is at Misery Mire - Spike Chest"),
    (18, 16, 2, 1, 0, True, None, 0, 179, 0, 19, "Sword is at Mini Moldorm Cave - Far Right"),
    (17, 17, 2, 1, 0, True, None, 0, 107, 64, 30, "Turtle Rock Small Key is at Turtle Rock - Eye Bridge - Top Right"),
    (17, 18, 7, 1, 0, False, None, 1, 34, 123, 13, "Agahnim is at Agahnim"),
    (18, 19, 2, 1, 0, False, None, 1, 17, 112, 31, "Red Pendant is at Eastern Palace - Prize"),
    (19, 20, 2, 1, 0, False, None, 1, 163, 42, 18, "Quarter Magic is at Blind's Hideout - Far Left"),
    (17, 21, 2, 1, 0, False, None, 2, 176, 33, 19, "Cane of Somaria is at Mini Moldorm Cave - Far Left"),
    (18, 22, 2, 1, 0, False, None, 2, 191, 26, 19, "Ether is at Flute Spot"),
    (19, 23, 7, 1, 0, False, None, 2, 6, 124, 12, "Escape Map is at Hyrule Castle - Map Chest"),
)


class CheckFailure(RuntimeError):
    """A concise, expected validator failure."""


def fail(message: str) -> None:
    raise CheckFailure(message)


def sha256_hex(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def disk_crc(data: bytes) -> int:
    return struct.unpack_from("<I", data, CRC_OFFSET)[0]


def computed_crc(data: bytes) -> int:
    return zlib.crc32(data[:CRC_OFFSET]) & 0xFFFFFFFF


def refresh_crc(data: bytearray) -> None:
    struct.pack_into("<I", data, CRC_OFFSET, computed_crc(data))


def validate_fixture(data: bytes) -> None:
    if len(data) != ZRSR_SIZE:
        fail(f"fixture length is {len(data)}, expected {ZRSR_SIZE}")
    if data[:4] != b"ZRSR":
        fail("fixture magic is not ZRSR")
    if sha256_hex(data) != EXPECTED_FIXTURE_SHA256:
        fail("fixture SHA-256 differs from the signed-commit provenance pin")
    if disk_crc(data) != computed_crc(data):
        fail("fixture CRC32 is invalid")

    generator = struct.unpack_from("<H", data, GENERATOR_OFFSET)[0]
    if generator != LEGACY_GENERATOR_VERSION:
        fail(
            f"fixture generator is {generator}, expected "
            f"{LEGACY_GENERATOR_VERSION}"
        )
    stamp = data[STAMP_OFFSET : STAMP_OFFSET + STAMP_SIZE].hex()
    if stamp != EXPECTED_STAMP_SHA256:
        fail("fixture embedded spoiler stamp differs from the provenance pin")

    share_length = struct.unpack_from("<I", data, SHARE_LENGTH_OFFSET)[0]
    if share_length > SHARE_CAPACITY:
        fail(f"fixture share-string length {share_length} exceeds capacity")
    share = data[SHARE_OFFSET : SHARE_OFFSET + share_length].decode("ascii")
    if share != EXPECTED_SHARE_STRING:
        fail("fixture share string differs from the provenance pin")

    canonical = data[SETTINGS_OFFSET : SETTINGS_OFFSET + SETTINGS_SIZE]
    if canonical.hex() != EXPECTED_CANONICAL_HEX:
        fail("fixture canonical settings differ from the provenance pin")
    for offset, bit, name in LEGACY_REFUSED_BITS:
        if data[offset] & bit:
            fail(f"legacy fixture unexpectedly sets the {name} policy bit "
                 f"(canon[{offset - SETTINGS_OFFSET}] bit {bit.bit_length() - 1}) "
                 f"— it would be refused, so the success case could not run")


def invoke_reveal(
    binary: Path, target: Path, cwd: Path, timeout_seconds: int
) -> subprocess.CompletedProcess[str]:
    try:
        return subprocess.run(
            [str(binary), f"--reveal-spoiler={target}"],
            cwd=cwd,
            check=False,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=timeout_seconds,
        )
    except (OSError, subprocess.SubprocessError) as exc:
        fail(f"could not invoke reveal binary: {exc}")
    raise AssertionError("unreachable")


def scratch_paths(target: Path) -> tuple[Path, Path]:
    return (
        Path(str(target) + ".reveal-tmp"),
        Path(str(target) + ".partial"),
    )


def assert_no_scratch(target: Path, label: str) -> None:
    leaked = [str(path.name) for path in scratch_paths(target) if path.exists()]
    if leaked:
        fail(f"{label}: reveal leaked scratch artifacts: {', '.join(leaked)}")


def assert_failure_case(
    binary: Path,
    root: Path,
    label: str,
    contents: bytes,
    result_code: int,
    description: str,
    timeout_seconds: int,
) -> None:
    case_dir = root / label
    case_dir.mkdir()
    target = case_dir / f"{label}.json"
    target.write_bytes(contents)

    result = invoke_reveal(binary, target, case_dir, timeout_seconds)
    expected_line = f"--reveal-spoiler: FAILED ({result_code}): {description}"
    if result.returncode != 1:
        fail(
            f"{label}: reveal returned {result.returncode}, expected 1\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    if expected_line not in result.stderr:
        fail(
            f"{label}: missing exact failure result {expected_line!r}\n"
            f"stderr:\n{result.stderr}"
        )
    if target.read_bytes() != contents:
        fail(f"{label}: refused reveal modified the copied suppressed bytes")
    if target.with_suffix(".txt").exists():
        fail(f"{label}: refused reveal unexpectedly wrote a text spoiler")
    assert_no_scratch(target, label)


def hint_row_projection(row: Any, index: int) -> tuple[Any, ...]:
    if not isinstance(row, dict):
        fail(f"hints[{index}] is not an object")
    return (
        row.get("source_id"),
        row.get("fact_id"),
        row.get("fact_kind_id"),
        row.get("template_id"),
        row.get("template_parameter"),
        row.get("primary"),
        row.get("tile_index"),
        row.get("queue_index"),
        row.get("location_id"),
        row.get("item_id"),
        row.get("region_id"),
        row.get("text"),
    )


def validate_revealed_json(contents: bytes, embedded_stamp: bytes) -> None:
    revealed_digest = hashlib.sha256(contents).digest()
    if revealed_digest != embedded_stamp:
        fail(
            "revealed JSON SHA-256 does not match the fixture's embedded stamp: "
            f"{revealed_digest.hex()} != {embedded_stamp.hex()}"
        )
    try:
        data = json.loads(contents.decode("utf-8"))
    except (UnicodeError, json.JSONDecodeError) as exc:
        fail(f"revealed spoiler is not valid UTF-8 JSON: {exc}")
    if not isinstance(data, dict):
        fail("revealed spoiler root is not an object")

    meta = data.get("meta")
    if not isinstance(meta, dict):
        fail("revealed spoiler has no meta object")
    if meta.get("generator_version") != LEGACY_GENERATOR_VERSION:
        fail("revealed meta.generator_version is not the legacy version 156")
    if meta.get("share_string") != EXPECTED_SHARE_STRING:
        fail("revealed meta.share_string differs from the fixture")
    if meta.get("placement_digest_hex") != EXPECTED_PLACEMENT_DIGEST:
        fail("revealed placement digest differs from the frozen fixture")
    if meta.get("sphere_digest") != EXPECTED_SPHERE_DIGEST:
        fail("revealed sphere digest differs from the frozen fixture")
    if meta.get("hints_count") != len(EXPECTED_HINT_ROWS):
        fail("revealed meta.hints_count differs from the frozen row count")

    settings = data.get("settings")
    if not isinstance(settings, dict):
        fail("revealed spoiler has no settings object")
    if "hint_npc_reward_reveal" in settings:
        fail("revealed generator-156 JSON contains the current preview field")
    if "shopsanity" in settings:
        fail("revealed generator-156 JSON contains the later shopsanity echo")

    plan = data.get("hint_plan")
    if plan != EXPECTED_PLAN:
        fail(
            "revealed legacy hint-plan identity differs from the frozen 1/1 "
            f"contract:\nexpected {EXPECTED_PLAN!r}\ngot      {plan!r}"
        )

    hints = data.get("hints")
    if not isinstance(hints, list):
        fail("revealed spoiler has no hints array")
    projected = tuple(
        hint_row_projection(row, index) for index, row in enumerate(hints)
    )
    if projected != EXPECTED_HINT_ROWS:
        mismatch = next(
            (
                index
                for index, pair in enumerate(
                    zip(projected, EXPECTED_HINT_ROWS)
                )
                if pair[0] != pair[1]
            ),
            min(len(projected), len(EXPECTED_HINT_ROWS)),
        )
        got = projected[mismatch] if mismatch < len(projected) else "<missing>"
        want = (
            EXPECTED_HINT_ROWS[mismatch]
            if mismatch < len(EXPECTED_HINT_ROWS)
            else "<no extra row>"
        )
        fail(
            f"revealed legacy hint rows differ at index {mismatch}:\n"
            f"expected {want!r}\ngot      {got!r}"
        )


def assert_success_case(
    binary: Path, root: Path, fixture: bytes, timeout_seconds: int
) -> None:
    case_dir = root / "success"
    case_dir.mkdir()
    target = case_dir / "legacy_gen156_revealed.json"
    target.write_bytes(fixture)

    result = invoke_reveal(binary, target, case_dir, timeout_seconds)
    if result.returncode != 0:
        fail(
            f"legacy reveal returned {result.returncode}, expected 0\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    if "--reveal-spoiler: OK at " not in result.stderr:
        fail(f"legacy reveal did not report success:\n{result.stderr}")
    assert_no_scratch(target, "success")

    revealed = target.read_bytes()
    embedded_stamp = fixture[STAMP_OFFSET : STAMP_OFFSET + STAMP_SIZE]
    validate_revealed_json(revealed, embedded_stamp)
    text_path = target.with_suffix(".txt")
    if not text_path.is_file() or text_path.stat().st_size == 0:
        fail("legacy reveal did not write its non-empty text companion")
    try:
        text = text_path.read_text(encoding="utf-8")
    except (OSError, UnicodeError) as exc:
        fail(f"legacy reveal text is not valid UTF-8: {exc}")
    if "NPC reward previews:" in text:
        fail("revealed generator-156 text contains the current preview field")


def resolve_binary(argument: Path) -> Path:
    path = argument if argument.is_absolute() else REPO / argument
    try:
        path = path.resolve(strict=True)
    except OSError as exc:
        fail(f"binary does not exist: {path} ({exc})")
    if not path.is_file():
        fail(f"binary is not a file: {path}")
    return path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--binary",
        type=Path,
        default=Path("bin/x64-Release/zelda3.exe"),
        help="current zelda3 binary (default: bin/x64-Release/zelda3.exe)",
    )
    parser.add_argument(
        "--fixture",
        type=Path,
        default=FIXTURE,
        help="override the checked-in generator-156 ZRSR fixture",
    )
    parser.add_argument(
        "--timeout",
        type=int,
        default=120,
        help="per-reveal timeout in seconds (default: 120)",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        binary = resolve_binary(args.binary)
        fixture_path = (
            args.fixture
            if args.fixture.is_absolute()
            else REPO / args.fixture
        )
        fixture = fixture_path.read_bytes()
        validate_fixture(fixture)
        if args.timeout <= 0:
            fail("--timeout must be positive")

        older_version_mutation = bytearray(fixture)
        struct.pack_into(
            "<H",
            older_version_mutation,
            GENERATOR_OFFSET,
            OLDER_UNSUPPORTED_GENERATOR_VERSION,
        )
        refresh_crc(older_version_mutation)

        former_configurable_mutation = bytearray(fixture)
        struct.pack_into(
            "<H",
            former_configurable_mutation,
            GENERATOR_OFFSET,
            FORMER_CONFIGURABLE_GENERATOR_VERSION,
        )
        refresh_crc(former_configurable_mutation)

        policy_mutations = []
        for offset, bit, name in LEGACY_REFUSED_BITS:
            mutation = bytearray(fixture)
            mutation[offset] |= bit
            refresh_crc(mutation)
            policy_mutations.append((name, bytes(mutation)))

        # Every other mutation here repairs the CRC, which means all four of
        # them are refused BEFORE the reveal ever regenerates a placement --
        # so the stamp comparison itself, the check that actually detects a
        # doctored spoiler body, had no negative test at all. Corrupt the
        # RECORDED stamp instead: CRC, share string, generator version and
        # policy bits all stay valid, the regeneration runs and succeeds, and
        # the only thing left to disagree is the stamp.
        stamp_mutation = bytearray(fixture)
        stamp_mutation[STAMP_OFFSET] ^= 0x01
        refresh_crc(stamp_mutation)
        if bytes(stamp_mutation) == fixture:
            fail("stamp mutation did not change the fixture")
        if disk_crc(bytes(stamp_mutation)) != computed_crc(bytes(stamp_mutation)):
            fail("stamp mutation left an invalid CRC — it would be refused as "
                 "CrcMismatch and never reach the stamp comparison")

        # TemporaryDirectory creates a unique, closed path instead of holding
        # NamedTemporaryFile handles across the child process.  That is
        # important on Windows, where an open handle prevents the C reveal path
        # from atomically replacing its copied target.
        with tempfile.TemporaryDirectory(
            prefix="z3r-legacy-hint-reveal-"
        ) as temp:
            root = Path(temp)
            assert_failure_case(
                binary,
                root,
                "older_version_mismatch",
                bytes(older_version_mutation),
                5,
                "Suppressed file was produced by a different generator version.",
                args.timeout,
            )
            assert_failure_case(
                binary,
                root,
                "former_configurable_version_mismatch",
                bytes(former_configurable_mutation),
                5,
                "Suppressed file was produced by a different generator version.",
                args.timeout,
            )
            for name, mutation in policy_mutations:
                assert_failure_case(
                    binary,
                    root,
                    f"policy_{name}_refused",
                    mutation,
                    8,
                    "Embedded settings are corrupt.",
                    args.timeout,
                )
            assert_failure_case(
                binary,
                root,
                "stamp_mismatch",
                bytes(stamp_mutation),
                6,
                "Stamp mismatch — regenerated placement does not match the "
                "recorded stamp.",
                args.timeout,
            )
            assert_success_case(binary, root, fixture, args.timeout)

        print("OK: generator-156 Hints 1/1 race reveal compatibility")
        print(f"  fixture: {fixture_path}")
        print(f"  fixture SHA-256: {EXPECTED_FIXTURE_SHA256}")
        print(f"  revealed/stamp SHA-256: {EXPECTED_STAMP_SHA256}")
        print(f"  hint-plan digest: {EXPECTED_PLAN['digest']}")
        print(
            f"  refusal guards: generators 155 and 157 => VersionMismatch; "
            f"all {len(LEGACY_REFUSED_BITS)} undefined policy bits "
            f"({', '.join(n for _, _, n in LEGACY_REFUSED_BITS)}) => "
            f"SettingsCorrupt; doctored stamp => StampMismatch"
        )
        return 0
    except (CheckFailure, OSError) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
