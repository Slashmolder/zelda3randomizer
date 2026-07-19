#!/usr/bin/env python3
"""Guard GitHub Actions' delegation to the central validation runner.

The runner owns the source/runtime gate lists. CI must invoke its authoritative
``ci`` phases with the exact required orchestration arguments on every platform;
checking duplicated command-name substrings would allow comments, ``echo``, or
wrong flags to produce a false green.
"""
from __future__ import annotations

import ast
import shlex
import sys
from pathlib import Path

import yaml

REPO = Path(__file__).resolve().parents[2]
RUNNER = REPO / "assets" / "scripts" / "run_rando_validation.py"
WORKFLOW = REPO / ".github" / "workflows" / "rando_ci.yaml"
RUNNER_PREFIX = ("python", "assets/scripts/run_rando_validation.py", "ci")

REQUIRED_STEPS = (
    (
        "rando-source-guards",
        "Run authoritative CI source profile (PR)",
        frozenset(("--phase", "--build-mode", "--base-sha", "--head-sha",
                   "--timings-json")),
        {
            "--phase": "source", "--build-mode": "prebuilt",
            "--base-sha": "${{ github.event.pull_request.base.sha }}",
            "--head-sha": "${{ github.event.pull_request.head.sha }}",
            "--timings-json": "artifacts/rando-validation-source.json",
        },
        None,
    ),
    (
        "rando-source-guards",
        "Run authoritative CI source profile (push)",
        frozenset(("--phase", "--build-mode", "--base-sha", "--head-sha",
                   "--timings-json")),
        {
            "--phase": "source", "--build-mode": "prebuilt",
            "--base-sha": "$base", "--head-sha": "$head",
            "--timings-json": "artifacts/rando-validation-source.json",
        },
        None,
    ),
    (
        "rando-determinism",
        "Run authoritative CI runtime profile",
        frozenset(("--phase", "--build-mode", "--binary", "--object-dir",
                   "--timings-json", "--artifacts-dir")),
        {
            "--phase": "runtime", "--build-mode": "prebuilt",
            "--binary": "${{ matrix.binary }}",
            "--object-dir": "${{ matrix.object_dir }}",
            "--timings-json": "artifacts/rando-validation-runtime-${{ matrix.name }}.json",
            "--artifacts-dir": "artifacts/runtime-${{ matrix.name }}",
        },
        "bash",
    ),
)


def runner_has_authoritative_dispatch() -> bool:
    """Require ci() to call the owned source and runtime orchestrators."""
    source = RUNNER.read_text(encoding="utf-8")
    tree = ast.parse(source, filename=str(RUNNER))
    ci_node = next(
        (node for node in tree.body
         if isinstance(node, ast.FunctionDef) and node.name == "ci"),
        None,
    )
    if ci_node is None:
        return False
    calls = {
        node.func.id
        for node in ast.walk(ci_node)
        if isinstance(node, ast.Call) and isinstance(node.func, ast.Name)
    }
    return {"assert_assetless_inputs", "run_source", "run_build",
            "runtime_common"}.issubset(calls)


def logical_commands(script: str) -> list[str]:
    """Join POSIX backslash continuations and return executable shell lines."""
    commands: list[str] = []
    pending = ""
    for raw in script.splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        pending = f"{pending} {line}".strip()
        if pending.endswith("\\"):
            pending = pending[:-1].rstrip()
            continue
        commands.append(pending)
        pending = ""
    if pending:
        commands.append(pending)
    return commands


def runner_options(script: str) -> list[dict[str, str]]:
    invocations: list[dict[str, str]] = []
    for command in logical_commands(script):
        try:
            tokens = shlex.split(command, posix=True)
        except ValueError as exc:
            raise RuntimeError(f"cannot parse workflow command {command!r}: {exc}") from exc
        if tuple(tokens[:len(RUNNER_PREFIX)]) != RUNNER_PREFIX:
            continue
        args = tokens[len(RUNNER_PREFIX):]
        options: dict[str, str] = {}
        index = 0
        while index < len(args):
            token = args[index]
            if not token.startswith("--"):
                raise RuntimeError(f"unexpected runner argument {token!r} in {command!r}")
            if "=" in token:
                name, value = token.split("=", 1)
            else:
                if index + 1 >= len(args) or args[index + 1].startswith("--"):
                    raise RuntimeError(f"runner option {token!r} has no value in {command!r}")
                name, value = token, args[index + 1]
                index += 1
            if name in options:
                raise RuntimeError(f"duplicate runner option {name!r} in {command!r}")
            options[name] = value
            index += 1
        invocations.append(options)
    return invocations


def workflow_steps() -> dict[tuple[str, str], dict]:
    data = yaml.safe_load(WORKFLOW.read_text(encoding="utf-8"))
    if not isinstance(data, dict) or not isinstance(data.get("jobs"), dict):
        raise RuntimeError("workflow does not contain a jobs mapping")
    found: dict[tuple[str, str], dict] = {}
    for job_name, job in data["jobs"].items():
        if not isinstance(job, dict):
            continue
        for step in job.get("steps", []):
            if isinstance(step, dict) and isinstance(step.get("name"), str):
                found[(job_name, step["name"])] = step
    return found


def main() -> int:
    try:
        if not runner_has_authoritative_dispatch():
            raise RuntimeError("runner ci() does not own both source and runtime dispatch")
        steps = workflow_steps()
        failures: list[str] = []
        for job, name, exact_options, fixed_values, required_shell in REQUIRED_STEPS:
            step = steps.get((job, name))
            if step is None or not isinstance(step.get("run"), str):
                failures.append(f"{job}/{name}: missing runnable step")
                continue
            if required_shell is not None and step.get("shell") != required_shell:
                failures.append(
                    f"{job}/{name}: shell {step.get('shell')!r} != {required_shell!r}"
                )
            invocations = runner_options(step["run"])
            if len(invocations) != 1:
                failures.append(
                    f"{job}/{name}: expected exactly one central-runner invocation, "
                    f"found {len(invocations)}"
                )
                continue
            options = invocations[0]
            if frozenset(options) != exact_options:
                failures.append(
                    f"{job}/{name}: option set {sorted(options)} != {sorted(exact_options)}"
                )
                continue
            wrong = {key: (options.get(key), value) for key, value in fixed_values.items()
                     if options.get(key) != value}
            if wrong:
                failures.append(f"{job}/{name}: wrong fixed option values {wrong}")
            if any(not value for value in options.values()):
                failures.append(f"{job}/{name}: empty runner option value")
    except (OSError, SyntaxError, yaml.YAMLError, RuntimeError) as exc:
        print(f"check_validation_contract: cannot inspect orchestration: {exc}", file=sys.stderr)
        return 1

    if failures:
        print("check_validation_contract: workflow/runner delegation drift detected:",
              file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1

    print("check_validation_contract: exact source/runtime central-runner delegation OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
