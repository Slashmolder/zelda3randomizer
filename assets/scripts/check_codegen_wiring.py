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
    "src/rando/bonk_lookup.h",  # add-rando-bonk-sanity
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

# Normal-invocation inputs consumed by assets/rando_logic_gen.py. Unlike Make
# and Switch (which force-run codegen), MSBuild uses these as its incremental
# correctness boundary. Keep the two sets exact: an omission ships stale
# generated code, while an extra/missing optional entry breaks incremental or
# add/delete invalidation behavior.
MSBUILD_REQUIRED_CODEGEN_INPUTS = {
    "assets/rando/op_registry.yaml",
    "assets/rando/item_registry.yaml",
    "assets/rando/location_registry.yaml",
    "assets/rando/icon_atlas.yaml",
    "assets/rando/direct_grant_icons.yaml",
    "assets/rando/logic.yaml",
    "assets/rando/macros.yaml",
    "assets/rando/npc_souls.yaml",
    "assets/rando/entrance_registry.yaml",
    "assets/rando/door_predicates.gen.json",
    "assets/rando/door_portals.yaml",
    "assets/rando/logic_parts/**/*.yaml",
    "assets/rando_logic_gen.py",
    "assets/chest_data.py",
}

MSBUILD_OPTIONAL_CODEGEN_INPUTS = {
    "assets/rando/chest_table.gen.bin",
    "assets/rando/pots.gen.yaml",
    "assets/rando/pot_key_depth.gen.yaml",
    "assets/rando/terrain.gen.yaml",
    "assets/rando/bonk.gen.yaml",  # add-rando-bonk-sanity (review F1)
    "assets/rando/enemy_drops.gen.yaml",
    "assets/rando/enemy_checks.gen.yaml",
    "assets/rando/soul_rooms.gen.yaml",
    "assets/rando/ow_graph.gen.yaml",  # add-rando-ow-warp-shuffle
}

MSBUILD_PRESENCE_CODEGEN_INPUTS = {
    "assets/rando/logic.schema.yaml",
    "zelda3_assets.dat",
}


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


def vcx_item_includes(text: str, item_name: str) -> set[str]:
    """Return normalized Include values for one MSBuild item type."""
    values = re.findall(
        rf'<{re.escape(item_name)}\b[^>]*\bInclude="([^"]+)"', text
    )
    return {value.replace("\\", "/").lower() for value in values}


def vcx_conditioned_items(text: str, item_name: str) -> dict[str, str]:
    """Return normalized Include -> Condition for simple MSBuild items."""
    records: dict[str, str] = {}
    for attrs in re.findall(rf'<{re.escape(item_name)}\b([^>]*)>', text):
        include = re.search(r'\bInclude="([^"]+)"', attrs)
        condition = re.search(r'\bCondition="([^"]+)"', attrs)
        if include:
            path = include.group(1).replace("\\", "/").lower()
            records[path] = (condition.group(1).replace("\\", "/").lower()
                             if condition else "")
    return records


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

    # --- MSBuild input completeness guard -----------------------------------
    # The incremental RandoCodegen stamp makes this list load-bearing. Assert
    # the exact required and presence-aware optional sets, rather than merely
    # checking that the stamp mechanism exists.
    vcx = Path("zelda3.vcxproj")
    if vcx.exists():
        vcx_text = strip_comments(
            vcx, vcx.read_text(encoding="utf-8", errors="replace")
        )
        required_all = vcx_item_includes(vcx_text, "RandoCodegenInput")
        promotes_optional = "@(randocodegenoptionalinput)" in required_all
        promotes_presence = "@(randocodegenpresenceinput)" in required_all
        required = set(required_all)
        required.discard("@(randocodegenoptionalinput)")
        required.discard("@(randocodegenpresenceinput)")
        optional = vcx_item_includes(vcx_text, "RandoCodegenOptionalInput")
        presence = vcx_item_includes(vcx_text, "RandoCodegenPresenceInput")
        expected_required = {p.lower() for p in MSBUILD_REQUIRED_CODEGEN_INPUTS}
        expected_optional = {p.lower() for p in MSBUILD_OPTIONAL_CODEGEN_INPUTS}
        expected_presence = {p.lower() for p in MSBUILD_PRESENCE_CODEGEN_INPUTS}
        missing_required = sorted(expected_required - required)
        extra_required = sorted(required - expected_required)
        missing_optional = sorted(expected_optional - optional)
        extra_optional = sorted(optional - expected_optional)
        missing_presence = sorted(expected_presence - presence)
        extra_presence = sorted(presence - expected_presence)
        bad_conditions: list[str] = []
        for item_name, paths in (
            ("RandoCodegenOptionalInput", expected_optional),
            ("RandoCodegenPresenceInput", expected_presence),
        ):
            records = vcx_conditioned_items(vcx_text, item_name)
            for path in paths:
                if records.get(path) != f"exists('{path}')":
                    bad_conditions.append(path)
        if (missing_required or extra_required or missing_optional or extra_optional or
                missing_presence or extra_presence or bad_conditions or
                not promotes_optional or promotes_presence):
            for label, paths in (
                ("missing required", missing_required),
                ("unexpected required", extra_required),
                ("missing presence-aware optional", missing_optional),
                ("unexpected presence-aware optional", extra_optional),
                ("missing presence-only", missing_presence),
                ("unexpected presence-only", extra_presence),
                ("missing/mismatched Exists condition", sorted(bad_conditions)),
            ):
                for path in paths:
                    print(f"check_codegen_wiring: MSBuild {label} input: {path}")
            if not promotes_optional:
                print("check_codegen_wiring: MSBuild optional inputs do not flow "
                      "into RandoCodegenInput")
            if promotes_presence:
                print("check_codegen_wiring: MSBuild presence-only probes must not "
                      "flow into direct timestamp inputs")
            print(
                "\nThe MSBuild RandoCodegen stamp must declare the exact normal-"
                "invocation\nrequired, optional-content, and presence-only sets "
                "consumed by\nassets/rando_logic_gen.py."
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
    # Gitignored registries are optional inputs. If codegen runs with one present,
    # then it is later deleted, a naive dependency-driven incremental build can
    # consider stale generated outputs up to date because the missing file simply
    # disappears from the input set. Make keeps the simple force-run contract.
    # MSBuild may either force-run or use the presence-aware stamp contract below.
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
        force_run = bool(m and not re.search(r'\b(Inputs|Outputs)=', m.group(0)))
        incremental_contract = [
            'RandoCodegenOptionalInput',
            'RandoCodegenPresenceInput',
            'Name="RandoCodegenState"',
            'rando_codegen_inputs.txt',
            'Lines="version=3;@(RandoCodegenOptionalInput);@(RandoCodegenPresenceInput)"',
            'WriteOnlyWhenDifferent="true"',
            '_MissingRandoCodegenOutput',
            '<Delete Files="$(IntDir)rando_codegen.stamp"',
            'Inputs="@(RandoCodegenInput);$(IntDir)rando_codegen_inputs.txt"',
            'Outputs="$(IntDir)rando_codegen.stamp"',
            '<Touch Files="$(IntDir)rando_codegen.stamp" AlwaysCreate="true"',
        ] + [p.replace("/", "\\") for p in sorted(
            MSBUILD_OPTIONAL_CODEGEN_INPUTS | MSBUILD_PRESENCE_CODEGEN_INPUTS)]
        presence_aware = bool(m and all(token in text for token in incremental_contract))
        if not force_run and not presence_aware:
            missing_force.append(vcx)
    if missing_force:
        for bs in missing_force:
            print(f"check_codegen_wiring: {bs} can skip codegen when a local "
                  f"artifact's presence changes.")
        print(
            "\nGitignored registries/assets may be added or deleted outside git. Build\n"
            "systems must either invoke rando_logic_gen.py every build or maintain a\n"
            "presence-aware stamp that invalidates on optional-file addition/deletion\n"
            "and whenever a generated output is missing."
        )
        return 1

    if not args.quiet:
        print(f"check_codegen_wiring: {len(EXPECTED_GENERATED)} generated file(s) wired "
              f"across all build systems (+ exact MSBuild inputs, logic_parts recursion, "
              f"local-artifact deletion guard).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
