#!/usr/bin/env python3
"""Guard GNU Make's compiler-generated header dependency wiring.

The source check is assetless and non-mutating. ``--behavioral`` additionally
requires an already-built Unix tree, parses compiler-emitted ``.d`` files to
derive the exact consumer set for one ordinary and one generated header, and
uses GNU Make's ``-W`` (pretend-new prerequisite) to prove exactly those objects
rebuild while unrelated objects stay unchanged. It also proves the real
``clean_obj`` recipe against disposable artifacts in a scratch directory; the
caller's completed build is never removed.
"""
from __future__ import annotations

import argparse
import os
import posixpath
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
MAKEFILE = REPO / "Makefile"


def fail(message: str) -> int:
    print(f"check_make_depfiles: {message}", file=sys.stderr)
    return 1


def source_check() -> int:
    try:
        text = MAKEFILE.read_text(encoding="utf-8")
    except OSError as exc:
        return fail(f"cannot read {MAKEFILE}: {exc}")

    required = {
        "C recipe emits depfiles": r"%\.o\s*:\s*%\.c[\s\S]*?\$\(CC\)[^\n]*-MMD[^\n]*-MP[^\n]*-MF",
        "C++ recipe emits depfiles": r"%\.o\s*:\s*%\.cpp[\s\S]*?\$\(CXX\)[^\n]*-MMD[^\n]*-MP[^\n]*-MF",
        "depfile list covers C and C++ objects": r"^DEPS\s*:?=.*\$\(OBJS:\.o=\.d\).*\$\(CPP_OBJS:\.o=\.d\)",
        "depfiles are included": r"^-include\s+\$\(DEPS\)",
    }
    missing = [label for label, pattern in required.items()
               if not re.search(pattern, text, re.MULTILINE)]

    clean = re.search(r"^clean_obj\s*:(.*?)(?=^[A-Za-z0-9_.$()/%-]+\s*:|\Z)",
                      text, re.MULTILINE | re.DOTALL)
    if clean is None or "$(DEPS)" not in clean.group(1):
        missing.append("clean_obj removes $(DEPS)")

    generated_gate = re.search(
        r"^\$\(OBJS\)\s+\$\(CPP_OBJS\)\s*:\s*\|[^\n]*\$\(RANDO_GEN_OUTPUTS\)",
        text, re.MULTILINE)
    if generated_gate is None:
        missing.append("generated outputs remain order-only object presence gates")

    if missing:
        return fail("wiring incomplete:\n  - " + "\n  - ".join(missing))
    print("check_make_depfiles: source wiring OK")
    return 0


def build_artifact_states() -> dict[Path, tuple[int, int]]:
    paths = list((REPO / "src").rglob("*.o"))
    paths += list((REPO / "snes").rglob("*.o"))
    paths += list((REPO / "third_party").rglob("*.o"))
    paths += list((REPO / "src").rglob("*.d"))
    paths += list((REPO / "snes").rglob("*.d"))
    paths += list((REPO / "third_party").rglob("*.d"))
    binary = REPO / "zelda3"
    if binary.exists():
        paths.append(binary)
    return {p: (p.stat().st_mtime_ns, p.stat().st_size) for p in paths}


def object_states() -> dict[Path, tuple[int, int]]:
    return {path: state for path, state in build_artifact_states().items()
            if path.suffix == ".o"}


def run_make(make: str, args: list[str], *, cwd: Path = REPO) -> tuple[int, str]:
    proc = subprocess.run([make, *args], cwd=cwd, text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    return proc.returncode, proc.stdout


def depfile_consumers(header: str) -> tuple[set[Path], set[str]]:
    """Return consumers and every raw prerequisite spelling of ``header``.

    C++ sources below ``src/rando/rando_window`` can spell the same header as
    ``src/rando/rando_window/../rando.h``. GNU Make's ``-W`` matches the raw
    prerequisite name, even though both spellings stat the same file, so the
    behavioral probe must force every spelling represented in the depfiles.
    """
    wanted = posixpath.normpath(header.replace("\\", "/"))
    depfiles = list((REPO / "src").rglob("*.d"))
    depfiles += list((REPO / "snes").rglob("*.d"))
    depfiles += list((REPO / "third_party").rglob("*.d"))
    consumers: set[Path] = set()
    raw_spellings: set[str] = set()
    for depfile in depfiles:
        try:
            text = depfile.read_text(encoding="utf-8", errors="replace")
        except OSError as exc:
            raise RuntimeError(f"cannot read {depfile.relative_to(REPO)}: {exc}") from exc
        # GCC/Clang escape physical line breaks in the first dependency rule.
        logical = text.replace("\\\r\n", " ").replace("\\\n", " ")
        first_rule = logical.splitlines()[0] if logical.splitlines() else ""
        targets, separator, dependencies = first_rule.partition(":")
        if not separator:
            raise RuntimeError(f"malformed depfile first rule: {depfile.relative_to(REPO)}")
        matching_spellings = {
            token for token in dependencies.split()
            if posixpath.normpath(token.replace("\\", "/")) == wanted
        }
        if not matching_spellings:
            continue
        raw_spellings.update(matching_spellings)
        for target in targets.split():
            target = posixpath.normpath(target.replace("\\", "/"))
            if target.endswith(".o"):
                consumers.add(REPO / Path(target))
    return consumers, raw_spellings


def assert_rebuild(make: str, header: str, label: str, jobs: int) -> int:
    try:
        expected, raw_spellings = depfile_consumers(header)
    except RuntimeError as exc:
        return fail(str(exc))
    if not expected:
        return fail(f"no depfile names {label} {header}")
    missing = sorted(path for path in expected if not path.is_file())
    if missing:
        return fail(
            f"{label} depfiles name missing consumer objects: "
            + ", ".join(str(path.relative_to(REPO)) for path in missing)
        )

    before = object_states()
    print(
        f"check_make_depfiles: forcing {label} {header} via "
        f"{len(raw_spellings)} depfile spelling(s)",
        flush=True,
    )
    force_args: list[str] = []
    for spelling in sorted(raw_spellings):
        force_args.extend(("-W", spelling))
    rc, output = run_make(make, [f"-j{max(1, jobs)}", *force_args, "zelda3"])
    if rc:
        print(output, file=sys.stderr)
        return fail(f"{label} rebuild failed (exit {rc})")
    after = object_states()
    rebuilt = {path for path, state in after.items()
               if path not in before or before[path] != state}
    if rebuilt != expected:
        missing_rebuilds = sorted(expected - rebuilt)
        unrelated_rebuilds = sorted(rebuilt - expected)
        details: list[str] = []
        if missing_rebuilds:
            details.append("expected but unchanged: " + ", ".join(
                str(path.relative_to(REPO)) for path in missing_rebuilds))
        if unrelated_rebuilds:
            details.append("unrelated but rebuilt: " + ", ".join(
                str(path.relative_to(REPO)) for path in unrelated_rebuilds))
        return fail(f"{label} consumer-set mismatch; " + "; ".join(details))
    print(f"check_make_depfiles: {label} rebuilt exactly {len(rebuilt)} consumer(s)")
    return 0


def assert_clean_recipe_in_scratch(make: str) -> int:
    """Run Makefile's clean_obj recipe against disposable command-line targets."""
    live_before = build_artifact_states()
    with tempfile.TemporaryDirectory(prefix="z3r-depfile-clean-") as temp_dir:
        scratch = Path(temp_dir)
        (scratch / "assets" / "rando").mkdir(parents=True)
        relpaths = (
            "probe/c-object.o",
            "probe/cpp-object.o",
            "probe/c-object.d",
            "probe/cpp-object.d",
            "probe/zelda3",
        )
        for relpath in relpaths:
            path = scratch / relpath
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"" if path.suffix == ".d" else b"clean_obj probe\n")
        args = [
            "-f", str(MAKEFILE), "clean_obj",
            "OBJS=probe/c-object.o",
            "CPP_OBJS=probe/cpp-object.o",
            "DEPS=probe/c-object.d probe/cpp-object.d",
            "TARGET_EXEC=probe/zelda3",
        ]
        print("check_make_depfiles: checking clean_obj in scratch", flush=True)
        rc, output = run_make(make, args, cwd=scratch)
        if rc:
            print(output, file=sys.stderr)
            return fail(f"scratch clean_obj failed (exit {rc})")
        leftovers = [relpath for relpath in relpaths if (scratch / relpath).exists()]
        if leftovers:
            return fail("scratch clean_obj left build artifacts: " + ", ".join(leftovers))

    if build_artifact_states() != live_before:
        return fail("scratch clean_obj unexpectedly changed the caller's build tree")
    print("check_make_depfiles: scratch clean_obj removed objects, depfiles, and binary")
    return 0


def behavioral_check(make: str, jobs: int) -> int:
    if os.name == "nt":
        return fail("--behavioral requires a Unix/WSL GNU Make build")
    if shutil.which(make) is None:
        return fail(f"GNU Make executable {make!r} not found")
    if not (REPO / "zelda3").is_file():
        return fail("--behavioral requires an already-built ./zelda3")
    depfiles = list((REPO / "src").rglob("*.d"))
    if not depfiles:
        return fail("--behavioral requires compiler-emitted src/**/*.d files; rebuild first")

    # Converge an older but complete developer build before taking the exact
    # consumer snapshot. Source edits made after that build may legitimately
    # rebuild unrelated objects here; they must not be misattributed to -W.
    print("check_make_depfiles: synchronizing incremental baseline", flush=True)
    rc, output = run_make(make, [f"-j{max(1, jobs)}", "zelda3"])
    if rc:
        print(output, file=sys.stderr)
        return fail(f"baseline incremental build failed (exit {rc})")

    if assert_rebuild(make, "src/rando/rando.h", "ordinary header", jobs):
        return 1
    if assert_rebuild(make, "src/rando/item_ids.h", "generated header", jobs):
        return 1

    before = object_states()
    print("check_make_depfiles: checking unchanged codegen/no-op build", flush=True)
    rc, output = run_make(make, ["zelda3"])
    if rc:
        print(output, file=sys.stderr)
        return fail(f"no-op build failed (exit {rc})")
    after = object_states()
    changed = sorted(str(p.relative_to(REPO)) for p, stamp in after.items()
                     if p not in before or before[p] != stamp)
    if changed:
        return fail("unchanged codegen/no-op build recompiled objects: " + ", ".join(changed))
    print("check_make_depfiles: unchanged codegen/no-op build rebuilt nothing")

    if assert_clean_recipe_in_scratch(make):
        return 1
    print("check_make_depfiles: exact header/no-op/scratch-clean behavior OK")
    return 0


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--behavioral", action="store_true",
                        help="verify a completed Unix build without cleaning it")
    parser.add_argument("--make", default="make", help="GNU Make executable")
    parser.add_argument("--jobs", type=int, default=2,
                        help="parallelism for forced consumer rebuilds (default 2)")
    args = parser.parse_args(argv)
    rc = source_check()
    if rc or not args.behavioral:
        return rc
    return behavioral_check(args.make, args.jobs)


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
