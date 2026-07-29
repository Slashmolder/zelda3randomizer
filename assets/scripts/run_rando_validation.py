#!/usr/bin/env python3
"""Cross-platform randomizer validation entry point.

Profiles are fixed contracts rather than best-effort collections:

``quick``
    Incremental (or explicitly prebuilt) build, focused source guards, config
    and grant self-test groups, the logic microbenchmark, and a short default
    generator benchmark.
``ci``
    Public assetless CI contract.  Supports ``--phase=source`` and
    ``--phase=runtime`` so workflow jobs can use a prebuilt matrix binary
    without rebuilding it.  The runtime phase is clean-build by default.
``full``
    Bootstrap and verify every local artifact, build once, refresh all local
    registries (including terrain), rebuild mandatorily, then run every local
    source/runtime/corpus/performance gate.

Every requested prerequisite fails closed.  Use ``--build-mode=prebuilt`` only
when the caller intentionally supplies an already-built binary/object tree.
The ci source and full profiles automatically resolve the generator-version
range from the merge-base of ``origin/main`` (or ``main``) to ``HEAD``; both
SHAs may be supplied explicitly when those refs are unavailable.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time
from dataclasses import asdict, dataclass
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
SCRIPTS = REPO / "assets" / "scripts"
PYTHON = sys.executable

ASSETLESS_INPUTS = (
    "assets/rando/chest_table.gen.bin",
    "assets/rando/pot_dump.gen.txt",
    "assets/rando/pots.gen.yaml",
    "assets/rando/pot_key_depth.gen.yaml",
    "assets/rando/enemy_drops.gen.yaml",
    "assets/rando/enemy_checks.gen.yaml",
    "assets/rando/soul_rooms.gen.yaml",
    "assets/rando/terrain_dump.gen.txt",
    "assets/rando/terrain.gen.yaml",
    "assets/rando/bonk.gen.yaml",
    "assets/rando/ow_graph.gen.yaml",
)


@dataclass
class StepResult:
    label: str
    command: list[str]
    seconds: float
    returncode: int


def default_binary() -> Path:
    # Match the build command's authoritative output.  Do not auto-select a
    # stale root-level zelda3.exe after MSBuild produced bin/x64-Release.
    return (REPO / "bin" / "x64-Release" / "zelda3.exe"
            if os.name == "nt" else REPO / "zelda3")


class Runner:
    def __init__(self, timings_path: Path) -> None:
        self.results: list[StepResult] = []
        self.timings_path = timings_path

    def write_timings(self, profile: str, status: str) -> None:
        self.timings_path.parent.mkdir(parents=True, exist_ok=True)
        payload = {
            "profile": profile,
            "status": status,
            "platform": sys.platform,
            "python": sys.version.split()[0],
            "steps": [asdict(result) for result in self.results],
            "total_seconds": round(sum(r.seconds for r in self.results), 6),
        }
        self.timings_path.write_text(json.dumps(payload, indent=2) + "\n",
                                     encoding="utf-8")

    def run(self, label: str, command: list[str], *, env: dict[str, str] | None = None) -> None:
        print(f"\n==> {label}", flush=True)
        print(subprocess.list2cmdline(command), flush=True)
        started = time.monotonic()
        try:
            proc = subprocess.run(command, cwd=REPO, env=env)
            returncode = proc.returncode
        except OSError as exc:
            print(f"run_rando_validation: could not launch {command[0]}: {exc}",
                  file=sys.stderr)
            returncode = 127
        seconds = time.monotonic() - started
        self.results.append(StepResult(label, command, round(seconds, 6), returncode))
        if returncode:
            raise RuntimeError(f"{label} failed with exit {returncode}")


def script(name: str, *args: str) -> list[str]:
    path = SCRIPTS / name
    if not path.is_file():
        raise RuntimeError(f"required validation script is missing: {path.relative_to(REPO)}")
    return [PYTHON, str(path.relative_to(REPO)), *args]


def find_msbuild() -> str | None:
    """Find MSBuild from PATH or a normal Visual Studio installation."""
    on_path = shutil.which("msbuild")
    if on_path is not None:
        return on_path

    vswhere_candidates: list[Path] = []
    vswhere_on_path = shutil.which("vswhere")
    if vswhere_on_path:
        vswhere_candidates.append(Path(vswhere_on_path))
    program_files_x86 = os.environ.get("ProgramFiles(x86)")
    if program_files_x86:
        vswhere_candidates.append(
            Path(program_files_x86) / "Microsoft Visual Studio" / "Installer" /
            "vswhere.exe"
        )
    for vswhere in vswhere_candidates:
        if not vswhere.is_file():
            continue
        try:
            proc = subprocess.run(
                [str(vswhere), "-latest", "-products", "*",
                 "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
                 "-find", r"MSBuild\**\Bin\MSBuild.exe"],
                capture_output=True, text=True, check=False,
            )
        except OSError:
            continue
        if proc.returncode == 0:
            for line in proc.stdout.splitlines():
                candidate = Path(line.strip())
                if candidate.is_file():
                    return str(candidate)
    return None


def build_command(clean: bool, jobs: int) -> list[list[str]]:
    if os.name == "nt":
        msbuild = find_msbuild()
        if msbuild is None:
            raise RuntimeError(
                "MSBuild not found on PATH or through Visual Studio's vswhere; "
                "install the Desktop development with C++ workload or use "
                "--build-mode=prebuilt"
            )
        common = [msbuild, "Zelda3.sln", "/m", "/p:Configuration=Release", "/p:Platform=x64"]
        return ([common + ["/t:Clean"]] if clean else []) + [common]
    make = shutil.which("make")
    if make is None:
        raise RuntimeError("GNU Make not found")
    return ([[make, "clean_obj"]] if clean else []) + [[make, f"-j{jobs}", "zelda3"]]


def run_build(runner: Runner, mode: str, jobs: int, label: str) -> None:
    if mode == "prebuilt":
        return
    for index, command in enumerate(build_command(mode == "clean", jobs), start=1):
        runner.run(f"{label} ({index})", command)


def require_binary(binary: Path) -> Path:
    binary = binary.resolve()
    if not binary.is_file():
        raise RuntimeError(f"required binary not found: {binary}")
    return binary


def git_commit(ref: str) -> str | None:
    """Resolve ``ref`` to a commit without accepting missing/ambiguous input."""
    try:
        proc = subprocess.run(
            ["git", "rev-parse", "--verify", f"{ref}^{{commit}}"],
            cwd=REPO,
            capture_output=True,
            text=True,
            check=False,
        )
    except OSError as exc:
        raise RuntimeError(f"cannot run git while resolving {ref!r}: {exc}") from exc
    if proc.returncode != 0:
        return None
    resolved = proc.stdout.strip()
    return resolved if re.fullmatch(r"[0-9a-fA-F]{40,64}", resolved) else None


def resolve_generator_version_range(
    kind: str, base_sha: str | None, head_sha: str | None
) -> tuple[str, str]:
    """Resolve the mandatory ci/full generator-version diff range."""
    if bool(base_sha) != bool(head_sha):
        raise RuntimeError("--base-sha and --head-sha must be supplied together")
    if base_sha and head_sha:
        base = git_commit(base_sha)
        head = git_commit(head_sha)
        if base is None or head is None:
            bad = base_sha if base is None else head_sha
            raise RuntimeError(
                f"generator-version diff ref {bad!r} is not an available commit"
            )
        return base, head

    head = git_commit("HEAD")
    if head is None:
        raise RuntimeError("cannot resolve HEAD for the generator-version diff guard")
    for base_ref in ("origin/main", "main"):
        if git_commit(base_ref) is None:
            continue
        proc = subprocess.run(
            ["git", "merge-base", base_ref, head],
            cwd=REPO,
            capture_output=True,
            text=True,
            check=False,
        )
        base = proc.stdout.strip()
        if proc.returncode == 0 and re.fullmatch(r"[0-9a-fA-F]{40,64}", base):
            print(
                "run_rando_validation: generator-version diff range "
                f"{base[:12]}..{head[:12]} (merge-base {base_ref})"
            )
            return base, head
    raise RuntimeError(
        f"{kind} profile cannot resolve a generator-version base: fetch "
        "origin/main or provide --base-sha and --head-sha explicitly"
    )


def source_steps(kind: str, base_sha: str | None, head_sha: str | None) -> list[tuple[str, list[str]]]:
    smoke = [
        ("git diff --check", ["git", "diff", "--check"]),
        ("Make depfile source guard", script("check_make_depfiles.py")),
        ("grant consumer source guard", script("check_grant_consumers.py")),
        ("knowledge consumer source guard", script("check_knowledge_consumers.py")),
        ("ancilla slot-ordering guard", script("check_ancilla_slot_ordering.py") + ["--selftest"]),
        ("workflow/runner validation contract", script("check_validation_contract.py")),
        ("codegen wiring", script("check_codegen_wiring.py")),
        ("hint metadata drift guard", script("check_hint_metadata.py")),
        ("hint compatibility version policy", script("check_hint_version_policy.py")),
        ("corpus version sync", script("check_corpus_version_sync.py")),
        ("placer determinism source guard", script("check_placer_determinism.py", "--source-only")),
    ]
    if kind == "quick":
        return smoke

    full = [
        ("no embedded data", script("check_no_embedded_data.py")),
        ("determinism source guard", script("check_determinism.py")),
        ("door table guard", script("check_door_tables.py")),
        ("pot override source guard", [PYTHON, "assets/scripts/gen_pot_tables.py", "--check-overrides-static"]),
        ("byte-order guard", script("check_byte_order.py")),
        ("grant audit guard", script("check_audit_guard.py", "--strict")),
        *smoke[1:],
        ("logic override guard", script("check_logic_overrides.py")),
        ("corpus schema", [PYTHON, "assets/scripts/run_rando_corpus.py", "--schema-only"]),
        ("benchmark manifest schema",
         [PYTHON, "assets/scripts/run_rando_benchmarks.py", "--schema-only"]),
        ("strict rando codegen", [PYTHON, "assets/rando_logic_gen.py", "--strict"]),
    ]
    resolved_base, resolved_head = resolve_generator_version_range(
        kind, base_sha, head_sha
    )
    full.append(("generator version diff guard",
                 script("check_generator_version.py", "--base-sha", resolved_base,
                        "--head-sha", resolved_head)))
    return [("git diff --check", ["git", "diff", "--check"]), *full]


def run_source(runner: Runner, kind: str, base_sha: str | None, head_sha: str | None) -> None:
    for label, command in source_steps(kind, base_sha, head_sha):
        runner.run(label, command)


def assert_assetless_inputs() -> None:
    present = [rel for rel in ASSETLESS_INPUTS if (REPO / rel).exists()]
    if present:
        raise RuntimeError("ci profile requires an assetless checkout; remove/move ignored local inputs:\n  "
                           + "\n  ".join(present))


def macro_value(path: str, name: str) -> int:
    source = REPO / path
    if not source.is_file():
        raise RuntimeError(f"assetless registry header missing after build: {path}")
    match = re.search(rf"^#define\s+{re.escape(name)}\s+(\d+)\b",
                      source.read_text(encoding="utf-8"), re.MULTILINE)
    if not match:
        raise RuntimeError(f"{path} does not define {name}")
    return int(match.group(1))


def assert_assetless_counts() -> None:
    counts = (
        ("src/rando/chest_lookup.h", "kRandoChestLookup_COUNT"),
        ("src/rando/pot_lookup.h", "kRandoPotRegistryCount"),
        ("src/rando/terrain_lookup.h", "kRandoTerrainRegistryCount"),
        ("src/rando/bonk_lookup.h", "kRandoBonkRegistryCount"),
        ("src/rando/enemy_drop_lookup.h", "kRandoEnemyDropLookup_COUNT"),
        ("src/rando/enemy_check_lookup.h", "kRandoEnemyCheckRegistryCount"),
    )
    nonzero = [(name, macro_value(path, name)) for path, name in counts
               if macro_value(path, name) != 0]
    logic = (REPO / "src/rando/logic_data.c").read_text(encoding="utf-8")
    souls = re.search(r"const\s+uint8\s+kRandoSoulRoomsBaked\s*=\s*(\d+)\s*;", logic)
    if souls is None:
        raise RuntimeError("logic_data.c does not define kRandoSoulRoomsBaked")
    if int(souls.group(1)) != 0:
        nonzero.append(("kRandoSoulRoomsBaked", int(souls.group(1))))
    ow_graph = re.search(r"const\s+uint8\s+kRandoOwGraphPresent\s*=\s*(\d+)\s*;",
                         logic)
    if ow_graph is None:
        raise RuntimeError("logic_data.c does not define kRandoOwGraphPresent")
    if int(ow_graph.group(1)) != 0:
        nonzero.append(("kRandoOwGraphPresent", int(ow_graph.group(1))))
    if nonzero:
        raise RuntimeError("assetless registry counts are nonzero: "
                           + ", ".join(f"{name}={value}" for name, value in nonzero))
    print("run_rando_validation: assetless registry counts OK")


def runtime_common(runner: Runner, binary: Path, object_dir: Path,
                   *, corpus_local: bool, corpus_timings: Path) -> None:
    env = os.environ.copy()
    env["DISPLAY"] = ""
    runner.run("link-symbol artifact guard",
               script("check_link_symbols.py", "--object-dir", str(object_dir),
                      "--require-objects"))
    runner.run("all randomizer self-tests", [str(binary), "--rando-selftest"], env=env)
    runner.run("dedicated grant self-test alias",
               [str(binary), "--rando-grant-check"], env=env)
    runner.run("hint spoiler contract",
               script("check_hint_spoiler.py", "--binary", str(binary)))
    runner.run("legacy hint race-reveal contract",
               script("check_hint_legacy_reveal.py", "--binary", str(binary)))
    runner.run("logic microbenchmark",
               [str(binary), "--rando-bench-logic", "--bench-iters=5000"], env=env)
    runner.run("door self-test", [str(binary), "--door-selftest"], env=env)
    runner.run("initialization + snapshot WRAM ownership",
               script("check_init_order.py", "--binary", str(binary)))
    # Shop identity is keyed on the overworld door; this walks every real door
    # row through the resolver and fails on an unreachable or room-aliased shop
    # slot. Asset-driven, so it self-skips on an assetless checkout.
    runner.run("shopsanity door-identity walk",
               [str(binary), "--rando-shop-doorwalk", "--allow-missing-assets"],
               env=env)
    # A cave interior's declared region is the SOURCE region entrance shuffle
    # rebinds through, but Entrance_SelfCheck only cross-validates it for entries
    # that HAVE locations. The location-less ones are pinned instead to the
    # committed `upstream_regions`, which this also enforces. Asset-driven,
    # self-skips. (Re-deriving those values from the ALTTPR PHP is
    # gen_entrance_upstream_regions.py --check, which needs a sibling checkout
    # and so is NOT wired here — run it when touching the registry.)
    runner.run("cave-interior region world consistency + upstream pin",
               script("gen_entrance_door_rows.py", "--check", "--allow-missing-assets"))
    corpus_args = ["--binary", str(binary), "--timings-json", str(corpus_timings)]
    if not corpus_local:
        corpus_args += ["--skip-pot-shuffle", "--skip-enemy-drop-checks",
                        "--skip-enemy-souls", "--skip-terrain-shuffle",
                        "--skip-ow-warp"]
    runner.run("regression corpus", script("run_rando_corpus.py", *corpus_args))
    slot_args = ["--binary", str(binary)]
    if not corpus_local:
        slot_args.append("--allow-missing-pot-registry")
        slot_args.append("--allow-missing-ow-graph")
    runner.run("slot-path matrix", script("check_rando_slot_path.py", *slot_args))
    runner.run("randomizer invariant sweep",
               script("check_rando_invariants.py", "--binary", str(binary)))
    runner.run("placer determinism runtime canary",
               script("check_placer_determinism.py", "--binary", str(binary)))


def quick(runner: Runner, args: argparse.Namespace) -> None:
    run_build(runner, args.build_mode, args.jobs, "incremental build")
    binary = require_binary(args.binary)
    run_source(runner, "quick", args.base_sha, args.head_sha)
    env = os.environ.copy()
    env["DISPLAY"] = ""
    runner.run("config self-test group", [str(binary), "--rando-selftest=config"], env=env)
    runner.run("grant self-test alias", [str(binary), "--rando-grant-check"], env=env)
    runner.run("logic microbenchmark",
               [str(binary), "--rando-bench-logic", "--bench-iters=5000"], env=env)
    runner.run("short default benchmark",
               script("run_rando_benchmarks.py", "--binary", str(binary),
                      "--suite=public", "--scenario=default-generation",
                      "--json-out", str(args.artifacts_dir / "benchmarks-quick.json")))


def ci(runner: Runner, args: argparse.Namespace) -> None:
    assert_assetless_inputs()
    if args.phase in ("all", "source"):
        run_source(runner, "ci", args.base_sha, args.head_sha)
        assert_assetless_counts()
    if args.phase in ("all", "runtime"):
        run_build(runner, args.build_mode, args.jobs, "CI build")
        binary = require_binary(args.binary)
        assert_assetless_counts()
        runtime_common(runner, binary, args.object_dir,
                       corpus_local=False,
                       corpus_timings=args.artifacts_dir / "corpus-ci-public.json")
        # The CI wall-time contract is Linux-only; macOS/Windows still run the
        # cross-platform digest and logic-microbenchmark gates above.
        if sys.platform.startswith("linux"):
            runner.run("public benchmark suite",
                       script("run_rando_benchmarks.py", "--binary", str(binary),
                              "--suite=public", "--json-out",
                              str(args.artifacts_dir / "benchmarks-ci-public.json")))
        if sys.platform.startswith("linux"):
            runner.run("Make depfile behavioral guard",
                       script("check_make_depfiles.py", "--behavioral"))


def full(runner: Runner, args: argparse.Namespace) -> None:
    if args.phase != "all":
        raise RuntimeError("full profile does not support partial --phase runs")
    if args.build_mode == "prebuilt":
        raise RuntimeError("full profile requires its mandatory two-pass rebuild")
    runner.run("worktree artifact bootstrap", script("setup_worktree.py"))
    run_build(runner, args.build_mode, args.jobs, "bootstrap build")
    binary = require_binary(args.binary)
    runner.run("refresh local artifact/codegen set",
               script("run_rando_local_checks.py", "--binary", str(binary),
                      "--prepare-only"))
    # Mandatory second pass: generated registries may have changed after the
    # first binary was linked.  Incremental is deliberate; depfiles select the
    # exact consumers while preserving the two-pass correctness contract.
    run_build(runner, "incremental", args.jobs, "mandatory post-prepare rebuild")
    binary = require_binary(args.binary)
    runner.run("verify complete local artifact set",
               script("setup_worktree.py", "--verify", "--require-all-artifacts"))
    run_source(runner, "full", args.base_sha, args.head_sha)
    runtime_common(runner, binary, args.object_dir,
                   corpus_local=True,
                   corpus_timings=args.artifacts_dir / "corpus-full-local.json")
    runner.run("public benchmark suite",
               script("run_rando_benchmarks.py", "--binary", str(binary),
                      "--suite=public", "--json-out",
                      str(args.artifacts_dir / "benchmarks-full-public.json")))
    runner.run("local benchmark suite",
               script("run_rando_benchmarks.py", "--binary", str(binary),
                      "--suite=local", "--json-out",
                      str(args.artifacts_dir / "benchmarks-full-local.json")))
    if os.name != "nt":
        runner.run("Make depfile behavioral guard",
                   script("check_make_depfiles.py", "--behavioral"))


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("profile", choices=("quick", "ci", "full"))
    parser.add_argument("--phase", choices=("all", "source", "runtime"), default="all",
                        help="CI job split; quick/full require all")
    parser.add_argument("--build-mode", choices=("incremental", "clean", "prebuilt"),
                        help="default: quick=incremental, ci/full=clean")
    parser.add_argument("--binary", type=Path, default=default_binary())
    parser.add_argument("--object-dir", type=Path,
                        default=Path("obj/x64-Release" if os.name == "nt" else "."))
    parser.add_argument("--jobs", type=int, default=max(1, min(8, os.cpu_count() or 1)))
    parser.add_argument(
        "--base-sha",
        help="override the ci/full generator-version merge-base (requires --head-sha)",
    )
    parser.add_argument(
        "--head-sha",
        help="override the ci/full generator-version head (requires --base-sha)",
    )
    parser.add_argument("--timings-json", type=Path,
                        default=REPO / "tmp" / "rando-validation-timings.json")
    parser.add_argument("--artifacts-dir", type=Path,
                        default=REPO / "tmp" / "rando-validation",
                        help="corpus/benchmark partial JSON output directory")
    args = parser.parse_args(argv)
    if args.profile != "ci" and args.phase != "all":
        parser.error("--phase is only supported by the ci profile")
    if args.build_mode is None:
        args.build_mode = "incremental" if args.profile == "quick" else "clean"
    args.binary = args.binary if args.binary.is_absolute() else REPO / args.binary
    args.object_dir = args.object_dir if args.object_dir.is_absolute() else REPO / args.object_dir
    args.timings_json = (args.timings_json if args.timings_json.is_absolute()
                         else REPO / args.timings_json)
    args.artifacts_dir = (args.artifacts_dir if args.artifacts_dir.is_absolute()
                          else REPO / args.artifacts_dir)
    args.artifacts_dir.mkdir(parents=True, exist_ok=True)
    return args


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    runner = Runner(args.timings_json)
    status = "failed"
    try:
        {"quick": quick, "ci": ci, "full": full}[args.profile](runner, args)
        status = "passed"
        print(f"\nrun_rando_validation: PASS ({args.profile})")
        return 0
    except RuntimeError as exc:
        print(f"\nrun_rando_validation: FAIL ({args.profile}): {exc}", file=sys.stderr)
        return 1
    finally:
        runner.write_timings(args.profile, status)
        print(f"run_rando_validation: timings {runner.timings_path}")


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
