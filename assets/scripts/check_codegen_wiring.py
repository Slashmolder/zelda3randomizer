#!/usr/bin/env python3
"""Codegen-wiring guard (tasks.md §1.6b).

Generated files SHALL be listed identically across every build system: Makefile,
``Zelda3.vcxproj`` pre-build steps, and ``src/platform/switch/Makefile``.

The recurring failure mode in multi-build-system projects: a contributor adds
``src/rando/logic_data.c`` to the Makefile but forgets MSVC, which then fails
opaquely on Windows CI. This guard enumerates the *expected* set of generated
files and asserts each one is referenced in every build system.

All checks are pure text greps over the three build files — they do NOT need
the (gitignored) generated files to exist, so the guard is fully live on a
fresh checkout / CI runner where codegen has not run yet. (It originally
skipped itself when no generated file existed — a scaffold gate from before
codegen landed — which meant the CI source-guards job, which never runs
codegen, silently skipped every check.)

Usage:
  python assets/scripts/check_codegen_wiring.py
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

# Files emitted by the rando codegen pipeline. Sources of truth:
#   - src/rando/logic_data.c        (from assets/rando/logic.yaml — task 3.5)
#   - src/rando/location_ids.h      (from assets/rando/location_registry.yaml — task 3.1)
#   - src/rando/item_ids.h          (from assets/rando/item_registry.yaml — task 3.2)
#   - src/rando/chest_lookup.h      (from assets/chest_data.py + location_registry.yaml — task 6.3)
#   - src/rando/pot_lookup.h        (from local assets/rando/pots.gen.yaml — pot room/pos lookup)
#   - src/rando/enemy_drop_lookup.h (from local enemy_drops.gen.yaml — forced enemy key drops)
#   - src/rando/enemy_check_lookup.h (from local enemy_checks.gen.yaml — ordinary enemy checks)
#   - src/rando/pot_nonpot_drop_counts.h (from local pot_key_depth.gen.yaml — nonpot key free-grants)
#   - src/rando/icon_atlas.h        (from assets/rando/icon_atlas.yaml — task 9.4b)
#   - src/rando/direct_grant_icons.h (from assets/rando/direct_grant_icons.yaml — task 9.x)
#
# Note: src/rando/vanilla_assets_hash.h is NOT in this list. It's hand-
# maintained (the dump tool overwrites it) and pulled in via #include, so
# the build systems don't need to reference it explicitly.
EXPECTED_GENERATED = [
    "src/rando/logic_data.c",
    "src/rando/location_ids.h",
    "src/rando/item_ids.h",
    "src/rando/chest_lookup.h",
    "src/rando/pot_lookup.h",
    "src/rando/terrain_lookup.h",  # add-rando-grass-rock-shuffle
    "src/rando/enemy_drop_lookup.h",
    "src/rando/enemy_check_lookup.h",
    "src/rando/pot_nonpot_drop_counts.h",
    "src/rando/icon_atlas.h",
    "src/rando/direct_grant_icons.h",
]

BUILD_SYSTEM_FILES = [
    Path("Makefile"),
    # Lowercase to match the git-tracked filename: on a case-sensitive FS (Linux
    # CI) `Path("Zelda3.vcxproj").exists()` is False, so file_mentions() returned
    # False for every generated file and the guard failed spuriously.
    Path("zelda3.vcxproj"),
    Path("src/platform/switch/Makefile"),
]


def strip_comments(path: Path, text: str) -> str:
    """Inspect build *code*, not prose — a comment mentioning a generated file
    must not mask its absence from the actual rule/recipe/item group."""
    if path.suffix == ".vcxproj":
        return re.sub(r"<!--.*?-->", "", text, flags=re.S)
    # Makefiles (incl. the Switch Makefile, no suffix): drop #-comment lines.
    return "\n".join(l for l in text.splitlines()
                     if not l.lstrip().startswith("#"))


def file_mentions(path: Path, needle: str) -> bool:
    if not path.exists():
        return False
    text = strip_comments(path, path.read_text(encoding="utf-8", errors="replace"))
    # Normalize separators (the vcxproj uses backslashes) and require at least
    # the immediate parent dir: "rando/logic_data.c". Build systems reference
    # these files with a path prefix (src/rando/..., $(SRC_DIR)/rando/...,
    # src\rando\...), while prose/echo strings tend to use the bare basename —
    # a bare-basename mention (e.g. the Makefile recipe's @echo listing) must
    # NOT satisfy parity, or a file dropped from the real rule goes unnoticed.
    text = text.replace("\\", "/")
    needle = needle.replace("\\", "/")
    dir_qualified = "/".join(needle.split("/")[-2:])
    return needle in text or dir_qualified in text


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--quiet", action="store_true")
    args = parser.parse_args(argv)

    missing: list[tuple[str, list[Path]]] = []
    for gen in EXPECTED_GENERATED:
        # Deliberately no existence check: these are gitignored codegen
        # OUTPUTS, absent on a fresh checkout (and in the no-build CI job).
        # file_mentions() is a text grep over the build files, which is all
        # the parity contract needs.
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
        # A comment mentioning "logic_parts" must not mask a non-recursive glob
        # in the actual recipe — hence strip_comments (shared with file_mentions).
        recursive_find = re.compile(r"find\s+\S*assets[\\/]rando\b")

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

    # --- Local artifact deletion guard ---------------------------------------
    # Gitignored pot artifacts are optional inputs. If codegen runs with them
    # present, then they are later deleted, a purely dependency-driven incremental
    # build can consider stale pot-enabled outputs up to date because the missing
    # files disappear from the input set. Guard the build-system contract that
    # codegen is invoked every build; rando_logic_gen.py itself avoids rewriting
    # unchanged outputs, so this does not churn mtimes in the no-op case.
    missing_force = []
    for bs in [Path("Makefile"), Path("src/platform/switch/Makefile")]:
        if not bs.exists():
            continue
        text = strip_comments(bs, bs.read_text(encoding="utf-8", errors="replace"))
        if "rando-codegen-force" not in text:
            missing_force.append(bs)
    vcx = Path("zelda3.vcxproj")
    if vcx.exists():
        text = strip_comments(vcx, vcx.read_text(encoding="utf-8", errors="replace"))
        m = re.search(r'<Target\s+Name="RandoCodegen"[^>]*>.*?</Target>', text, flags=re.S)
        if not m or re.search(r'\b(Inputs|Outputs)=', m.group(0)):
            missing_force.append(vcx)
    if missing_force:
        for bs in missing_force:
            print(f"check_codegen_wiring: {bs} can skip codegen when local pot "
                  f"artifact presence changes.")
        print(
            "\nThe gitignored pot YAMLs may be added or deleted outside git. Build\n"
            "systems must invoke rando_logic_gen.py every build so stale pot-enabled\n"
            "generated outputs are replaced by assetless empty tables when those\n"
            "local artifacts disappear."
        )
        return 1

    if not args.quiet:
        print(f"check_codegen_wiring: {len(EXPECTED_GENERATED)} generated file(s) wired "
              f"across all build systems (+ logic_parts recursion, pot artifact deletion guard).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
