#!/usr/bin/env python3
"""Corpus-version sync guard.

Asserts that the regression corpus manifest's ``generator_version`` matches the
authoritative ``kGeneratorVersion`` ``#define`` in ``src/rando/rando.h``.

Why this guard exists
---------------------
``bump_rando_corpus.py`` rewrites the manifest's ``generator_version`` whenever
it regenerates digests, and the two values are *meant* to march in lock-step:
the corpus digests are only valid for the generator version that produced them.
But nothing enforced the match, so a placement-affecting (or version-bumping)
change that happened to be digest-neutral under default settings — the
documented "inert-change exception", where ``kGeneratorVersion`` advances but no
corpus digest changes — could leave the manifest pinned to the *old* version
with no CI failure. That silently weakens the corpus's provenance contract: a
reader can no longer trust that ``generator_version`` names the build the
digests came from.

This is a pure-source guard: no build, no assets, no ROM. It only reads two
text files, so it runs in the cheap ``rando-source-guards`` CI job.

Usage:
  python assets/scripts/check_corpus_version_sync.py
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

RANDO_H = Path("src/rando/rando.h")
MANIFEST = Path("tests/rando_corpus/manifest.yaml")

# `#define kGeneratorVersion 53u  // ...`  — tolerate an optional u/U suffix.
_DEFINE_RE = re.compile(r"#define\s+kGeneratorVersion\s+(\d+)[uU]?\b")
# `generator_version: 53  # ...`
_MANIFEST_RE = re.compile(r"^\s*generator_version\s*:\s*(\d+)\b", re.MULTILINE)


def _read(path: Path) -> str | None:
    if not path.exists():
        return None
    return path.read_text(encoding="utf-8")


def main(argv: list[str]) -> int:
    rando_h = _read(RANDO_H)
    manifest = _read(MANIFEST)

    if rando_h is None:
        print(f"check_corpus_version_sync: {RANDO_H} not found; cannot verify "
              "the generator/corpus provenance contract.", file=sys.stderr)
        return 2
    if manifest is None:
        print(f"check_corpus_version_sync: {MANIFEST} not found; cannot verify "
              "the generator/corpus provenance contract.", file=sys.stderr)
        return 2

    define_m = _DEFINE_RE.search(rando_h)
    if not define_m:
        print(
            f"check_corpus_version_sync: could not find a "
            f"`#define kGeneratorVersion <n>` in {RANDO_H}."
        )
        return 1
    manifest_m = _MANIFEST_RE.search(manifest)
    if not manifest_m:
        print(
            f"check_corpus_version_sync: could not find a "
            f"`generator_version: <n>` key in {MANIFEST}."
        )
        return 1

    code_ver = int(define_m.group(1))
    manifest_ver = int(manifest_m.group(1))

    if code_ver != manifest_ver:
        print(
            "check_corpus_version_sync: MISMATCH\n"
            f"  {RANDO_H} kGeneratorVersion = {code_ver}\n"
            f"  {MANIFEST} generator_version = {manifest_ver}\n"
            "  These must match. If you bumped kGeneratorVersion, re-run\n"
            "  `python assets/scripts/bump_rando_corpus.py --apply` (or update the\n"
            "  manifest's generator_version by hand for a digest-neutral bump)."
        )
        return 1

    print(f"check_corpus_version_sync: OK (generator_version == kGeneratorVersion == {code_ver}).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
