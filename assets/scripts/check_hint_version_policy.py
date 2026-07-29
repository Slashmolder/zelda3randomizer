#!/usr/bin/env python3
"""Conservative Git-diff policy for Hints compatibility versions.

A slot persists only `{algorithm_version, text_schema_version, plan_digest}`
(`RandoHintPersistedState`); the plan itself is rebuilt at load from the seed's
settings and placements. That split is what makes two tiers of protection
necessary, and it is what decides which tier a diff belongs to.

**Semantics tier — must advance BOTH hint axes.** The generated metadata lock
covers declarative selection and text inputs, but runtime C can also change
selection order, digest semantics, or render templates. Every committed or
working-tree change to rando_hints.c/.h, the hint metadata/contract files, and
hint-specific rando_logic_gen.py hunks must therefore advance BOTH the plan
algorithm and text-schema versions relative to its immediate baseline. Rendered
strings are deliberately NOT part of plan identity (`hint_plan_compute_digest`
hashes the semantic payload only), so a naming or template change is invisible
to the digest — the text-schema axis is the only thing that catches it.

**Inputs tier — must advance kGeneratorVersion.** Entrance-region bindings and
region ids are inputs the plan builder consumes, not a way of interpreting a
persisted plan. `hint_effective_region` folds
`Rando_GetEntranceRegionOverride` into `RandoHintFact.region_id`, which IS
hashed into the persisted digest, so rebinding a region makes an old slot's
digest mismatch — the accurate diagnosis. Advancing the hint axes here would
assert a schema change that did not happen and would downgrade that slot from
`DigestMismatch` to a wrong `UnsupportedAlgorithm`.

The digest is the real protection; the generator bump is how a stale slot gets
surfaced at all. Be precise about that, because the two differ: an
entrance/door-shuffle slot HARD-FAILS its layout digest gate on drift, but a
PLAIN slot only emits a one-time version-drift warning and loads its embedded
placement as-is (`Rando_ActivateSlot`, rando.c). Either way the hint plan is
rebuilt and re-digested, so a rebound region reports `DigestMismatch` rather
than a false "unsupported schema".

The inputs tier is checked across the whole base..head range, matching
`check_generator_version.py`'s shipped-delta granularity: a placement-neutral
commit inside a branch that bumps once is legitimate (83d51c016d, 133b0663 are
both exactly that), and the regression corpus — not a diff heuristic — is what
proves whether a change actually moved placement. Consequence worth knowing:
the tier only demands SOME bump inside the range, so its strength depends on
the base being tight. CI fetches origin/main, so the range is the PR's own
commits; a stale local origin/main widens the range and weakens the check
proportionally.

Both tiers are evaluated independently — a diff touching hint runtime AND
region bindings owes both. This intentionally over-bumps intertwined runtime
edits. A false negative would let an old sidecar/snapshot identity reconstruct
different facts or text.
"""
from __future__ import annotations

import argparse
import re
import subprocess
from pathlib import Path


REPO = Path(__file__).resolve().parents[2]
HINT_HEADER = "src/rando/rando_hints.h"
GENERATOR_HEADER = "src/rando/rando.h"
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
GENERATOR_PATTERN = re.compile(r"^\s*#define\s+kGeneratorVersion\s+(\d+)u?", re.MULTILINE)

# Diff tiers. See the module docstring for why these are protected differently.
TIER_SEMANTICS = "semantics"  # owes both hint compatibility axes
TIER_INPUTS = "inputs"        # owes a kGeneratorVersion advance


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


def generator_version_at(ref: str | None) -> int | None:
    """kGeneratorVersion at ``ref`` (None = working tree).

    Returns None ONLY when the header genuinely does not exist there — a
    baseline predating the constant, which is the one case the inputs tier may
    skip. Everything else fails CLOSED. ``file_at`` swallows git errors into an
    empty string, so a naive search would treat an unreadable base (a shallow
    CI clone whose base blob was never fetched) as "predates the constant" and
    silently pass the check. ``check_generator_version.py`` raises in the
    analogous case; match it.
    """
    if ref is None:
        disk = REPO / GENERATOR_HEADER
        if not disk.exists():
            return None
        source = disk.read_text(encoding="utf-8")
    else:
        listing = subprocess.run(
            ["git", "ls-tree", "-r", "--name-only", ref, "--", GENERATOR_HEADER],
            cwd=REPO,
            text=True,
            encoding="utf-8",
            capture_output=True,
            check=False,
        )
        if listing.returncode != 0:
            detail = listing.stderr.strip() or f"exit {listing.returncode}"
            raise RuntimeError(
                f"cannot inspect {GENERATOR_HEADER} at {ref!r}: {detail}"
            )
        present = {
            line.strip().replace("\\", "/")
            for line in listing.stdout.splitlines()
            if line.strip()
        }
        if GENERATOR_HEADER not in present:
            return None
        source = file_at(ref, GENERATOR_HEADER)
    match = GENERATOR_PATTERN.search(source)
    if match is None:
        where = "the working tree" if ref is None else repr(ref)
        raise RuntimeError(
            f"{GENERATOR_HEADER} exists at {where} but defines no "
            "kGeneratorVersion"
        )
    return int(match.group(1))


def generator_version_advances(before: int, after: int) -> bool:
    return after > before


def patch_tiers(patch: str, names: set[str]) -> set[str]:
    """Classify a diff into the compatibility tiers it owes.

    Tiers are independent, not ranked: a diff that touches hint runtime AND
    region bindings owes both a hint-axis advance and a generator bump.
    """
    tiers: set[str] = set()
    if names & RUNTIME_PATHS:
        tiers.add(TIER_SEMANTICS)
    if names & HINT_METADATA_PATHS:
        tiers.add(TIER_SEMANTICS)
    if CODEGEN_PATH in names and HINT_CODEGEN_PATTERN.search(patch):
        tiers.add(TIER_SEMANTICS)
    # Region bindings are plan INPUTS. They move RandoHintFact.region_id, which
    # the persisted digest hashes, so an old slot mismatches accurately without
    # any schema change. The file trigger stays unconditional (unlike the
    # codegen path's content match): a kGeneratorVersion advance is already
    # mandatory for every src/rando/**/*.c edit under check_generator_version,
    # so demanding it here costs nothing and cannot be evaded by a region
    # rebinding that happens not to spell "region" in its diff text.
    if REGION_LOCK_PATH in names or ENTRANCE_REGION_PATH in names:
        tiers.add(TIER_INPUTS)
    if (
        any(
            name.startswith("assets/rando/")
            and name.endswith((".yaml", ".yml"))
            for name in names
        )
        and REGION_LOGIC_PATTERN.search(patch)
    ):
        tiers.add(TIER_INPUTS)
    return tiers


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


def require_generator_advance(
    label: str,
    before_ref: str,
    after_ref: str | None,
) -> bool:
    """Enforce the inputs tier. Returns True when a comparison actually ran."""
    before = generator_version_at(before_ref)
    if before is None:
        # Predates the constant; nothing to compare against. Mirrors
        # check_generator_version.py's "treat as new file" base handling.
        return False
    after = generator_version_at(after_ref)
    if after is None:
        raise RuntimeError(
            f"{label} rebinds the entrance/region inputs Hints consume, but "
            f"{GENERATOR_HEADER} no longer defines kGeneratorVersion"
        )
    if not generator_version_advances(before, after):
        raise RuntimeError(
            f"{label} rebinds the entrance/region inputs Hints consume but "
            f"kGeneratorVersion does not advance: {before}->{after}. Region "
            "bindings enter the persisted plan digest, not the plan algorithm "
            "or text schema, so the generator bump (not a hint-axis bump) is "
            "what marks the older slot stale. Bump kGeneratorVersion in "
            f"{GENERATOR_HEADER}."
        )
    return True


def self_check() -> None:
    for path in HINT_METADATA_PATHS | RUNTIME_PATHS:
        if patch_tiers("", {path}) != {TIER_SEMANTICS}:
            raise RuntimeError(
                f"version-policy selfcheck lost the semantics tier for {path}"
            )
    # Region bindings owe the generator bump, NOT the hint axes. If either of
    # these flips back to the semantics tier, no entrance change can ever pass.
    for path in (ENTRANCE_REGION_PATH, REGION_LOCK_PATH):
        if patch_tiers("", {path}) != {TIER_INPUTS}:
            raise RuntimeError(
                f"version-policy selfcheck lost the inputs tier for {path}"
            )
    region_yaml = {"assets/rando/logic_parts/00_probe.yaml"}
    if patch_tiers("+  region: LightWorld_South\n", region_yaml) != {TIER_INPUTS}:
        raise RuntimeError(
            "version-policy selfcheck lost the inputs tier for region yaml"
        )
    if patch_tiers("+  # unrelated\n", region_yaml):
        raise RuntimeError(
            "version-policy selfcheck flagged a region-free yaml diff"
        )
    if patch_tiers("+  # unrelated\n", {CODEGEN_PATH}):
        raise RuntimeError(
            "version-policy selfcheck flagged a hint-free codegen diff"
        )
    if patch_tiers("+emit_hint_metadata()\n", {CODEGEN_PATH}) != {TIER_SEMANTICS}:
        raise RuntimeError(
            "version-policy selfcheck lost the semantics tier for hint codegen"
        )
    # Tiers are independent: a combined diff owes both.
    if patch_tiers("", {"src/rando/rando_hints.c", ENTRANCE_REGION_PATH}) != {
        TIER_SEMANTICS,
        TIER_INPUTS,
    }:
        raise RuntimeError(
            "version-policy selfcheck collapsed a combined-tier diff"
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
    if (
        not generator_version_advances(163, 164)
        or generator_version_advances(164, 164)
        or generator_version_advances(164, 163)
    ):
        raise RuntimeError(
            "version-policy selfcheck failed the kGeneratorVersion advance rule"
        )
    # The inputs tier must fail CLOSED on a base it cannot read. Otherwise a
    # shallow clone missing the base blob reads as "predates the constant" and
    # skips the check while still reporting success.
    try:
        generator_version_at("0" * 40)
    except RuntimeError:
        pass
    else:
        raise RuntimeError(
            "version-policy selfcheck: an unreadable base did not fail closed"
        )


def check_commits(base: str, head: str) -> tuple[int, list[str]]:
    """Enforce the semantics tier per commit; report inputs-tier commits.

    The inputs tier is deliberately NOT enforced here. Only the base..head
    delta ever ships, so a placement-neutral intermediate commit inside a
    branch that bumps kGeneratorVersion once is legitimate — the same
    granularity check_generator_version.py uses.
    """
    checked = 0
    inputs_commits: list[str] = []
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
        tiers = patch_tiers(patch, names)
        if TIER_INPUTS in tiers:
            inputs_commits.append(commit)
        if TIER_SEMANTICS not in tiers:
            continue
        # The selected durable base can predate Hints v2. Do not retroactively
        # reject historical hint edits made before either compatibility axis
        # existed; enforcement begins with the commit that introduces them.
        if versions_at(parent) == (0, 0) and versions_at(commit) == (0, 0):
            continue
        require_both_axes(f"commit {commit[:12]}", parent, commit)
        checked += 1
    return checked, inputs_commits


def check_worktree(head: str) -> tuple[int, bool]:
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
    tiers = patch_tiers(patch, names)
    if TIER_SEMANTICS not in tiers:
        return 0, TIER_INPUTS in tiers
    require_both_axes("working tree", head, None)
    return 1, TIER_INPUTS in tiers


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--base-sha")
    args = parser.parse_args()
    self_check()
    base = resolve_base(args.base_sha)
    head = git("rev-parse", "HEAD").strip()
    semantics, inputs_commits = check_commits(base, head)
    worktree_semantics, worktree_inputs = check_worktree(head)
    semantics += worktree_semantics

    inputs = len(inputs_commits) + (1 if worktree_inputs else 0)
    inputs_checked = False
    if inputs:
        # Compare the whole shipped delta. The working tree is the real tip
        # when it carries the rebinding, otherwise HEAD is.
        tip = None if worktree_inputs else head
        where = "working tree" if worktree_inputs else f"range {base[:12]}..{head[:12]}"
        inputs_checked = require_generator_advance(where, base, tip)

    if not inputs:
        inputs_note = "0 entrance/region-input diff(s)"
    elif inputs_checked:
        inputs_note = (
            f"{inputs} entrance/region-input diff(s) advanced kGeneratorVersion"
        )
    else:
        # Do not claim an advance that was never compared.
        inputs_note = (
            f"{inputs} entrance/region-input diff(s) NOT version-checked "
            "(base predates kGeneratorVersion)"
        )
    print(
        "check_hint_version_policy: "
        f"{semantics} semantics diff(s) advanced both compatibility axes; "
        f"{inputs_note} (base {base[:12]})"
    )


if __name__ == "__main__":
    main()
