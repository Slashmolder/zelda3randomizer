#!/usr/bin/env python3
"""Codegen-wiring guard (tasks.md §1.6b).

Generated files SHALL be listed identically across every build system: Makefile,
``Zelda3.vcxproj`` pre-build steps, and ``src/platform/switch/Makefile``.

The recurring failure mode in multi-build-system projects: a contributor adds
``src/rando/logic_data.c`` to the Makefile but forgets MSVC, which then fails
opaquely on Windows CI. This guard enumerates the *expected* set of generated
files and asserts each one is referenced in every build system.

**A0 status (scaffold)**: no generated files exist yet (logic_data.c, etc.
land in Phase A1). The script knows the protocol and exits clean until at
least one generated file is declared.

Usage:
  python assets/scripts/check_codegen_wiring.py
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

# Files emitted by the rando codegen pipeline. Sources of truth:
#   - src/rando/logic_data.c        (from assets/rando/logic.yaml — task 3.5)
#   - src/rando/location_ids.h      (from assets/rando/location_registry.yaml — task 3.1)
#   - src/rando/item_ids.h          (from assets/rando/item_registry.yaml — task 3.2)
#   - src/rando/chest_lookup.h      (from assets/chest_data.py + location_registry.yaml — task 6.3)
#   - src/rando/icon_atlas.h        (from assets/rando/icon_atlas.yaml — task 9.4b)
#
# Note: src/rando/vanilla_assets_hash.h is NOT in this list. It's hand-
# maintained (the dump tool overwrites it) and pulled in via #include, so
# the build systems don't need to reference it explicitly.
EXPECTED_GENERATED = [
    "src/rando/logic_data.c",
    "src/rando/location_ids.h",
    "src/rando/item_ids.h",
    "src/rando/chest_lookup.h",
    "src/rando/icon_atlas.h",
    "src/rando/direct_grant_icons.h",
]

BUILD_SYSTEM_FILES = [
    Path("Makefile"),
    Path("Zelda3.vcxproj"),
    Path("src/platform/switch/Makefile"),
]


def file_mentions(path: Path, needle: str) -> bool:
    if not path.exists():
        return False
    text = path.read_text(encoding="utf-8", errors="replace")
    # Match either the full path or just the basename — different build systems
    # phrase references differently.
    return needle in text or Path(needle).name in text


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--quiet", action="store_true")
    args = parser.parse_args(argv)

    # If no generated files have been declared yet, there's nothing to check.
    if not any(Path(f).exists() for f in EXPECTED_GENERATED):
        # Also accept if the assets/rando/ pipeline hasn't been set up — that
        # means codegen hasn't started yet.
        if not args.quiet:
            print(
                "check_codegen_wiring: no generated rando files exist yet.\n"
                "  scaffold A0 pass — guard activates when first generated file lands."
            )
        return 0

    missing: list[tuple[str, list[Path]]] = []
    for gen in EXPECTED_GENERATED:
        if not Path(gen).exists():
            continue  # only check declared-and-present files
        missing_in = [bs for bs in BUILD_SYSTEM_FILES if not file_mentions(bs, gen)]
        if missing_in:
            missing.append((gen, missing_in))

    if missing:
        for gen, build_systems in missing:
            print(f"check_codegen_wiring: {gen} not referenced in:")
            for bs in build_systems:
                print(f"  - {bs}")
        print(
            "\nPer tasks.md §1.6b, every codegen-generated file SHALL be listed in\n"
            "Makefile, Zelda3.vcxproj, and src/platform/switch/Makefile (parity)."
        )
        return 1

    # --- Codegen INPUT-recursion guard ----------------------------------------
    # The logic graph is assembled from assets/rando/logic_parts/**/*.yaml (incl.
    # logic_parts/inverted/**). If a build system declares its codegen
    # prerequisites with a NON-recursive `assets/rando/*.yaml` glob only, editing
    # a logic_parts file will NOT retrigger codegen on an incremental build — it
    # silently ships a stale logic_data.c. (Fixed once; this guards the regress.)
    # A build system passes if it covers the subtree either literally
    # ("logic_parts") or via a recursive `find assets/rando` over all yaml.
    if Path("assets/rando/logic_parts").is_dir():
        import re
        recursive_find = re.compile(r"find\s+\S*assets[\\/]rando\b")

        def strip_comments(path: Path, text: str) -> str:
            # Inspect build *code*, not prose — a comment mentioning "logic_parts"
            # must not mask a non-recursive glob in the actual recipe.
            if path.suffix == ".vcxproj":
                return re.sub(r"<!--.*?-->", "", text, flags=re.S)
            # Makefiles (incl. the Switch Makefile, no suffix): drop #-comment lines.
            return "\n".join(l for l in text.splitlines()
                             if not l.lstrip().startswith("#"))

        not_recursive = []
        for bs in BUILD_SYSTEM_FILES:
            if not bs.exists():
                continue
            text = strip_comments(bs, bs.read_text(encoding="utf-8", errors="replace"))
            if "logic_parts" in text or recursive_find.search(text):
                continue
            not_recursive.append(bs)
        if not_recursive:
            for bs in not_recursive:
                print(f"check_codegen_wiring: {bs} does not declare the "
                      f"logic_parts/** subtree as a codegen prerequisite.")
            print(
                "\nThe rando codegen reads assets/rando/logic_parts/**/*.yaml, but the\n"
                "above build system(s) wire codegen against a non-recursive glob, so a\n"
                "logic_parts edit won't retrigger codegen (stale logic_data.c).\n"
                "Use a recursive form: `$(shell find assets/rando -name '*.yaml')` for\n"
                "make, or `assets\\rando\\logic_parts\\**\\*.yaml` in the vcxproj Inputs."
            )
            return 1

    if not args.quiet:
        present = [f for f in EXPECTED_GENERATED if Path(f).exists()]
        print(f"check_codegen_wiring: {len(present)} generated file(s) wired across all build systems.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
