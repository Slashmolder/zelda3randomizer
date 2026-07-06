#!/usr/bin/env python3
"""Validate zelda3_assets.dat against the compiled asset header signature."""
from __future__ import annotations

import argparse
import re
import struct
import sys
from pathlib import Path


REPO = Path(__file__).resolve().parents[2]
DEFAULT_ASSETS = REPO / "zelda3_assets.dat"
DEFAULT_HEADER = REPO / "src" / "assets.h"
SIGNATURE_LEN = 48


class AssetSignatureError(Exception):
    pass


def _parse_assets_header(path: Path) -> tuple[bytes, int]:
    text = path.read_text(encoding="utf-8")
    count_match = re.search(r"^\s*kNumberOfAssets\s*=\s*(\d+)", text, re.MULTILINE)
    sig_match = re.search(r"^#define\s+kAssets_Sig\s+(.+)$", text, re.MULTILINE)
    if count_match is None:
        raise AssetSignatureError(f"{path}: missing kNumberOfAssets")
    if sig_match is None:
        raise AssetSignatureError(f"{path}: missing kAssets_Sig")

    try:
        sig = bytes(int(part.strip(), 0) for part in sig_match.group(1).split(","))
    except ValueError as exc:
        raise AssetSignatureError(f"{path}: malformed kAssets_Sig") from exc
    if len(sig) != SIGNATURE_LEN:
        raise AssetSignatureError(
            f"{path}: kAssets_Sig is {len(sig)} bytes, expected {SIGNATURE_LEN}")
    return sig, int(count_match.group(1))


def check_assets_signature(asset_file: Path = DEFAULT_ASSETS,
                           assets_header: Path = DEFAULT_HEADER) -> None:
    if not asset_file.is_file():
        raise AssetSignatureError(f"{asset_file}: missing zelda3_assets.dat")

    expected_sig, expected_count = _parse_assets_header(assets_header)
    data = asset_file.read_bytes()
    min_len = SIGNATURE_LEN + 32 + 8 + expected_count * 4
    if len(data) < min_len:
        raise AssetSignatureError(
            f"{asset_file}: too small for {expected_count} assets")
    if data[:SIGNATURE_LEN] != expected_sig:
        got = data[:16].decode("ascii", errors="replace").rstrip("\0")
        want = expected_sig[:16].decode("ascii", errors="replace").rstrip("\0")
        raise AssetSignatureError(
            f"{asset_file}: asset signature {got!r} does not match compiled "
            f"{want!r}; rerun `python assets/restool.py --extract-from-rom`")
    count = struct.unpack_from("<I", data, 80)[0]
    if count != expected_count:
        raise AssetSignatureError(
            f"{asset_file}: asset count {count} does not match compiled "
            f"{expected_count}; rerun `python assets/restool.py --extract-from-rom`")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--asset-file", type=Path, default=DEFAULT_ASSETS,
                        help="path to zelda3_assets.dat")
    parser.add_argument("--assets-header", type=Path, default=DEFAULT_HEADER,
                        help="path to generated src/assets.h")
    parser.add_argument("--quiet", action="store_true",
                        help="suppress the success message")
    args = parser.parse_args(argv)

    try:
        check_assets_signature(args.asset_file, args.assets_header)
    except AssetSignatureError as exc:
        print(f"check_assets_signature: ERROR: {exc}", file=sys.stderr)
        return 1
    if not args.quiet:
        print(f"check_assets_signature: OK {args.asset_file}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
