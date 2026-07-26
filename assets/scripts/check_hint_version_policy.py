#!/usr/bin/env python3
"""Conservative Git-diff policy for Hints compatibility versions.

The generated metadata lock covers declarative selection and text inputs, but
runtime C can also change selection order, digest semantics, or render
templates. Every committed or working-tree change to rando_hints.c/.h (and
hint-specific rando_logic_gen.py hunks) must therefore advance BOTH the plan
algorithm and text-schema versions relative to its immediate baseline.

This intentionally over-bumps intertwined runtime edits. A false negative would
let an old sidecar/snapshot identity reconstruct different facts or text.
"""
from __future__ import annotations

import argparse
import re
import subprocess
from pathlib import Path


REPO = Path(__file__).resolve().parents[2]
HINT_HEADER = "src/rando/rando_hints.h"
RUNTIME_PATHS = {
    "src/rando/rando_hints.c",
    HINT_HEADER,
}
CODEGEN_PATH = "assets/rando_logic_gen.py"
HINT_METADATA_PATHS = {
    "assets/rando/hint_metadata.yaml",
    "assets/rando/hint_metadata.lock.json",
    "assets/rando/hint_registry_contract.json",
}
REGION_LOCK_PATH = "assets/rando/region_ids.lock.json"
ENTRANCE_REGION_PATH = "src/rando/shuffle_entrance.c"
HINT_CODEGEN_PATTERN = re.compile(
    r"hint_metadata|HintMetadata|GeneratedHint|_hint_|emit_hint_metadata",
    re.IGNORECASE,
)
REGION_LOGIC_PATTERN = re.compile(
    r"^[+-].*(?:\bregion\s*:|region_override|world_state_filter)",
    re.IGNORECASE | re.MULTILINE,
)


def git(*args: str, check: bool = True) -> str:
    proc = subprocess.run(
        ["git", *args],
        cwd=REPO,
        text=True,
        encoding="utf-8",
        capture_output=True,
        check=False,
    )
    if check and proc.returncode != 0:
        raise RuntimeError(
            f"git {' '.join(args)} failed: {proc.stderr.strip()}"
        )
    return proc.stdout


def commit_exists(ref: str) -> bool:
    return (
        subprocess.run(
            ["git", "cat-file", "-e", f"{ref}^{{commit}}"],
            cwd=REPO,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=False,
        ).returncode
        == 0
    )


def resolve_base(explicit: str | None) -> str:
    if explicit:
        if not commit_exists(explicit):
            raise RuntimeError(f"base commit {explicit!r} does not exist")
        return git("rev-parse", explicit).strip()
    head = git("rev-parse", "HEAD").strip()
    for ref in ("origin/main", "main"):
        if not commit_exists(ref):
            continue
        if (
            subprocess.run(
                ["git", "merge-base", "--is-ancestor", ref, head],
                cwd=REPO,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                check=False,
            ).returncode
            == 0
        ):
            return git("rev-parse", ref).strip()
    for ref in ("main", "origin/main"):
        if not commit_exists(ref):
            continue
        proc = subprocess.run(
            ["git", "merge-base", ref, head],
            cwd=REPO,
            text=True,
            encoding="utf-8",
            capture_output=True,
            check=False,
        )
        base = proc.stdout.strip()
        if proc.returncode == 0 and base:
            return base
    raise RuntimeError(
        "cannot resolve a Hints version-policy base; fetch main/origin/main "
        "or pass --base-sha"
    )


def file_at(ref: str | None, path: str) -> str:
    if ref is None:
        disk = REPO / path
        return disk.read_text(encoding="utf-8") if disk.exists() else ""
    proc = subprocess.run(
        ["git", "show", f"{ref}:{path}"],
        cwd=REPO,
        text=True,
        encoding="utf-8",
        capture_output=True,
        check=False,
    )
    return proc.stdout if proc.returncode == 0 else ""


def versions_at(ref: str | None) -> tuple[int, int]:
    source = file_at(ref, HINT_HEADER)

    def value(macro: str) -> int:
        match = re.search(
            rf"^\s*#define\s+{re.escape(macro)}\s+(\d+)u?\s*$",
            source,
            flags=re.MULTILINE,
        )
        # The pre-Hints-v2 baseline has neither axis; treat that as version 0.
        return int(match.group(1)) if match else 0

    return (
        value("kRandoHintPlanAlgorithmVersion"),
        value("kRandoHintTextSchemaVersion"),
    )


def patch_is_hint_sensitive(patch: str, names: set[str]) -> bool:
    if names & RUNTIME_PATHS:
        return True
    if names & HINT_METADATA_PATHS:
        return True
    if CODEGEN_PATH in names and HINT_CODEGEN_PATTERN.search(patch):
        return True
    if REGION_LOCK_PATH in names or ENTRANCE_REGION_PATH in names:
        return True
    if (
        any(
            name.startswith("assets/rando/")
            and name.endswith((".yaml", ".yml"))
            for name in names
        )
        and REGION_LOGIC_PATTERN.search(patch)
    ):
        return True
    return False


def require_both_axes(
    label: str,
    before_ref: str | None,
    after_ref: str | None,
) -> None:
    before = versions_at(before_ref)
    after = versions_at(after_ref)
    if not both_axes_advance(before, after):
        raise RuntimeError(
            f"{label} changes Hints runtime/codegen semantics but does not "
            "advance both compatibility axes: "
            f"algorithm {before[0]}->{after[0]}, text {before[1]}->{after[1]}"
        )


def both_axes_advance(
    before: tuple[int, int],
    after: tuple[int, int],
) -> bool:
    return after[0] > before[0] and after[1] > before[1]


def self_check() -> None:
    for path in HINT_METADATA_PATHS:
        if not patch_is_hint_sensitive("", {path}):
            raise RuntimeError(
                f"version-policy selfcheck missed metadata path {path}"
            )
    if (
        not both_axes_advance((1, 1), (2, 2))
        or both_axes_advance((1, 1), (1, 2))
        or both_axes_advance((1, 1), (2, 1))
        or both_axes_advance((2, 2), (1, 3))
    ):
        raise RuntimeError(
            "version-policy selfcheck failed strict monotonic-axis rules"
        )


def check_commits(base: str, head: str) -> int:
    checked = 0
    commits = git("rev-list", "--reverse", f"{base}..{head}").splitlines()
    for commit in commits:
        parents = git("rev-list", "--parents", "-n", "1", commit).split()
        parent = parents[1] if len(parents) > 1 else None
        names = set(
            git("diff-tree", "--no-commit-id", "--name-only", "-r", commit)
            .replace("\\", "/")
            .splitlines()
        )
        patch = git("show", "--format=", "--unified=0", commit)
        if not patch_is_hint_sensitive(patch, names):
            continue
        # The selected durable base can predate Hints v2. Do not retroactively
        # reject historical hint edits made before either compatibility axis
        # existed; enforcement begins with the commit that introduces them.
        if versions_at(parent) == (0, 0) and versions_at(commit) == (0, 0):
            continue
        require_both_axes(f"commit {commit[:12]}", parent, commit)
        checked += 1
    return checked


def check_worktree(head: str) -> int:
    names = set(
        git("diff", "--name-only", "HEAD", "--")
        .replace("\\", "/")
        .splitlines()
    )
    names.update(
        git("ls-files", "--others", "--exclude-standard", "--")
        .replace("\\", "/")
        .splitlines()
    )
    patch = git("diff", "--unified=0", "HEAD", "--")
    if not patch_is_hint_sensitive(patch, names):
        return 0
    require_both_axes("working tree", head, None)
    return 1


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--base-sha")
    args = parser.parse_args()
    self_check()
    base = resolve_base(args.base_sha)
    head = git("rev-parse", "HEAD").strip()
    checks = check_commits(base, head) + check_worktree(head)
    print(
        "check_hint_version_policy: "
        f"{checks} hint-sensitive diff(s) advanced both compatibility axes "
        f"(base {base[:12]})"
    )


if __name__ == "__main__":
    main()
