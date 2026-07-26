#!/usr/bin/env python3
"""Validate the binary-backed Hints v2 spoiler contract.

The in-process hint self-tests own planner and runtime lifecycle invariants.
This guard exercises the public ``--generate-seed`` boundary so the additive
JSON/text spoiler schema, delivery topology, mode identity, and exclusion of
mutable discovery/payment state cannot silently drift.
"""
from __future__ import annotations

import argparse
import collections
import hashlib
import json
import re
import struct
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Any


SETTINGS_BASE = (
    "mode.state=open,goal=triforce-hunt,"
    "pieces_required=20,pieces_placed=30"
)
SEED = "0x5"
GENERATION_TIMEOUT_SECONDS = 120

LEGACY_ALGORITHM_VERSION = 1
LEGACY_TEXT_SCHEMA_VERSION = 1
CURRENT_ALGORITHM_VERSION = 2
CURRENT_TEXT_SCHEMA_VERSION = 2
LEGACY_OFF_DIGEST = (
    "0159944abdcdc41a9929b9fcae775f6e0ee15279da17a9308bbd80583731ee91"
)
CURRENT_OFF_DIGEST = (
    "aa6ae4f9a8b9a71066c3cae7f22df1683a6a38e08c2758af5e6e100eec82cf8b"
)

MIX_IDS = {
    "variety": 0,
    "important": 1,
    "difficult": 2,
    "world-info": 3,
    "not-applicable": 0,
}
TILE_COVERAGE_IDS = {
    15: 0,
    0: 1,
    5: 2,
    10: 3,
}


@dataclass(frozen=True)
class PolicyCase:
    label: str
    settings: str
    profile: str
    tile_count: int
    paid_depth: int
    mix: str
    mode: int = 1
    npc_reward_reveal: bool = False
    require_full: bool = True


# Reviewed public-CLI matrix: every named profile, coverage, paid depth, and
# mix appears, along with component-before-profile precedence and the canonical
# empty-tuple collapse to Off. Balanced is generated twice separately below.
POLICY_CASES = (
    PolicyCase("balanced-a", "hints=balanced", "balanced", 15, 3, "variety"),
    PolicyCase("balanced-b", "hints=balanced", "balanced", 15, 3, "variety"),
    PolicyCase(
        "balanced-npc-preview",
        "hints=balanced,hint_npc_reward_reveal=true",
        "balanced",
        15,
        3,
        "variety",
        npc_reward_reveal=True,
    ),
    PolicyCase("off", "hints=off", "off", 0, 0, "not-applicable", mode=0),
    PolicyCase(
        "off-npc-preview",
        "hints=off,hint_npc_reward_reveal=true",
        "off",
        0,
        0,
        "not-applicable",
        mode=0,
        npc_reward_reveal=True,
    ),
    PolicyCase("sparse", "hints=sparse", "sparse", 5, 1, "variety"),
    PolicyCase("direct", "hints=direct", "direct", 15, 3, "important"),
    PolicyCase(
        "custom-ten-difficult",
        "hints=balanced,hint_tiles=many,hint_paid=2,hint_mix=difficult",
        "custom",
        10,
        2,
        "difficult",
    ),
    PolicyCase(
        "paid-only-world-info",
        "hints=balanced,hint_tiles=none,hint_paid=3,hint_mix=world-info",
        "custom",
        0,
        3,
        "world-info",
    ),
    PolicyCase(
        "tile-only-important",
        "hints=balanced,hint_tiles=all,hint_paid=0,hint_mix=important",
        "custom",
        15,
        0,
        "important",
    ),
    PolicyCase(
        "components-before-profile",
        "hint_paid=2,hint_mix=world-info,hints=sparse",
        "custom",
        5,
        2,
        "world-info",
    ),
    PolicyCase(
        "empty-normalizes-off",
        "hints=balanced,hint_mix=difficult,hint_tiles=none,hint_paid=0",
        "off",
        0,
        0,
        "not-applicable",
        mode=0,
    ),
)

PAID_SOURCES = {
    17: ("fork_storyteller", 528),
    18: ("fork_fortune_teller_kakariko", 529),
    19: ("fork_fortune_teller_dark_world", 530),
}
TILE_SOURCES = {
    1: "telepathic_tile_eastern_palace",
    2: "telepathic_tile_tower_of_hera_floor_4",
    3: "telepathic_tile_spectacle_rock",
    4: "telepathic_tile_swamp_entrance",
    5: "telepathic_tile_thieves_town_upstairs",
    6: "telepathic_tile_misery_mire",
    7: "telepathic_tile_palace_of_darkness",
    8: "telepathic_tile_desert_bonk_torch_room",
    9: "telepathic_tile_castle_tower",
    10: "telepathic_tile_ice_large_room",
    11: "telepathic_tile_turtle_rock",
    12: "telepathic_tile_ice_entrace",  # Stable upstream typo.
    13: "telepathic_tile_ice_stalfos_knights_room",
    14: "telepathic_tile_tower_of_hera_entrance",
    15: "telepathic_tile_south_east_darkworld_cave",
}
FACT_KIND_IDS = {
    "priority_item": 1,
    "major_item": 2,
    "high_friction_location": 3,
    "goal": 4,
    "active_setting": 5,
    "region_value": 6,
    "useful": 7,
    "flavor": 8,
}
TEMPLATE_IDS = {
    "exact_placement": 1,
    "goal": 2,
    "active_setting": 3,
    "region_value": 4,
    "flavor": 5,
}
FACT_TEMPLATE = {
    "priority_item": "exact_placement",
    "major_item": "exact_placement",
    "high_friction_location": "exact_placement",
    "goal": "goal",
    "active_setting": "active_setting",
    "region_value": "region_value",
    "useful": "exact_placement",
    "flavor": "flavor",
}
COMPAT_ROW_KEYS = {"npc", "dialogue_id", "text", "compatibility"}
FORBIDDEN_STATE_KEY_FRAGMENTS = (
    "discover",
    "resolve",
    "checked",
    "cursor",
    "pending",
    "latch",
    "epoch",
    "presentation",
    "paid_selection",
    "selected_hint",
    "selected_fact",
    "current_hint",
    "current_fact",
    "hint_state",
    "hint_runtime",
    "paid_state",
    "armed",
    "exhausted",
    "encoded",
    "dialogue_flags",
)
FORBIDDEN_STATE_TEXT_ROOTS = (
    "discover",
    "resolve",
    "checked",
    "cursor",
    "pending",
    "latch",
    "epoch",
    "presentation",
    "armed",
)

HEX_DIGEST_RE = re.compile(r"^[0-9a-f]{64}$")
TEXT_PLAN_RE = re.compile(
    r"^  Plan: algorithm (\d+), text schema (\d+), digest ([0-9a-f]{64})$"
)
TEXT_COUNTS_RE = re.compile(r"^  Facts: (\d+) primary, (\d+) reserve$")
TEXT_POLICY_RE = re.compile(
    r"^  Policy: profile (\S+), tiles (\d+), paid depth (\d+), mix (\S+)$"
)
TEXT_SOURCES_RE = re.compile(
    r"^  Sources: (\d+) total, (\d+) tile, (\d+) paid services, "
    r"(\d+) paid facts$"
)
TEXT_NPC_REWARD_PREVIEW_RE = re.compile(
    r"^NPC reward previews: (yes|no) "
    r"\(Original/US rich dialogue only\)$"
)
TEXT_FACT_RE = re.compile(
    r"^  \[(\d{2})\] (\S+)\s+(primary|reserve) "
    r"(tile|queue) (\d+) \| "
    r"(\S+)/(\S+)/template:(\S+) parameter:(\d+)(.*)$"
)
TEXT_COMPAT_RE = re.compile(r"^  \[compat\] (\S+)\s+(.+)$")
TEXT_TARGET_RE = re.compile(r" (location|item|region):(\d+)")


class ContractError(RuntimeError):
    """A human-readable spoiler contract failure."""


def fail(message: str) -> None:
    raise ContractError(message)


def duplicate_rejecting_object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            fail(f"JSON contains duplicate key {key!r}")
        result[key] = value
    return result


def require_type(value: Any, expected: type, path: str) -> None:
    # ``bool`` subclasses ``int`` in Python; spoiler primitive types are exact.
    if type(value) is not expected:
        fail(f"{path} must be {expected.__name__}, got {type(value).__name__}")


def require_int(mapping: dict[str, Any], key: str, path: str,
                maximum: int | None = None) -> int:
    if key not in mapping:
        fail(f"{path} is missing required key {key!r}")
    value = mapping[key]
    require_type(value, int, f"{path}.{key}")
    if value < 0:
        fail(f"{path}.{key} must be non-negative, got {value}")
    if maximum is not None and value > maximum:
        fail(f"{path}.{key} must not exceed {maximum}, got {value}")
    return value


def require_str(mapping: dict[str, Any], key: str, path: str) -> str:
    if key not in mapping:
        fail(f"{path} is missing required key {key!r}")
    value = mapping[key]
    require_type(value, str, f"{path}.{key}")
    return value


def require_bool(mapping: dict[str, Any], key: str, path: str) -> bool:
    if key not in mapping:
        fail(f"{path} is missing required key {key!r}")
    value = mapping[key]
    require_type(value, bool, f"{path}.{key}")
    return value


def require_digest(value: Any, path: str) -> str:
    require_type(value, str, path)
    if HEX_DIGEST_RE.fullmatch(value) is None:
        fail(f"{path} must be a lowercase 64-digit hexadecimal digest")
    return value


def scan_state_keys(value: Any, path: str) -> None:
    if isinstance(value, dict):
        for key, child in value.items():
            lowered = key.lower()
            term = next((token for token in FORBIDDEN_STATE_KEY_FRAGMENTS
                         if token in lowered), None)
            if term is not None:
                fail(f"{path} leaks mutable state through key {key!r} "
                     f"(matched {term!r})")
            scan_state_keys(child, f"{path}.{key}")
    elif isinstance(value, list):
        for index, child in enumerate(value):
            scan_state_keys(child, f"{path}[{index}]")


def scan_state_text(text: str, path: str) -> None:
    lowered = text.lower()
    for term in FORBIDDEN_STATE_TEXT_ROOTS:
        if re.search(rf"\b{re.escape(term)}[a-z_]*\b", lowered):
            fail(f"{path} leaks mutable hint state term {term!r}")


def load_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(
            path.read_text(encoding="utf-8"),
            object_pairs_hook=duplicate_rejecting_object,
        )
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        fail(f"cannot parse {path.name}: {exc}")
    require_type(value, dict, path.name)
    return value


def run_generation(binary: Path, case: PolicyCase,
                   output_dir: Path) -> tuple[dict[str, Any], str]:
    case_dir = output_dir / case.label
    case_dir.mkdir()
    json_path = case_dir / f"{case.label}.json"
    text_path = case_dir / f"{case.label}.txt"
    command = [
        str(binary),
        "--generate-seed",
        f"--settings={SETTINGS_BASE},{case.settings}",
        f"--seed={SEED}",
        f"--out-spoiler={json_path}",
        "--budget-seconds=0",
    ]
    try:
        result = subprocess.run(
            command,
            cwd=case_dir,
            check=False,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=GENERATION_TIMEOUT_SECONDS,
        )
    except (OSError, subprocess.SubprocessError) as exc:
        fail(f"{case.label}: generator invocation failed: {exc}")
    if result.returncode != 0:
        fail(
            f"{case.label}: generator exited {result.returncode}\n"
            f"stdout:\n{result.stdout[-2000:]}\n"
            f"stderr:\n{result.stderr[-2000:]}"
        )
    if not json_path.is_file() or not text_path.is_file():
        fail(f"{case.label}: generator did not write both JSON and text spoilers")
    data = load_json(json_path)
    try:
        text = text_path.read_text(encoding="utf-8")
    except (OSError, UnicodeError) as exc:
        fail(f"cannot read {text_path.name}: {exc}")
    return data, text


def validate_plan(data: dict[str, Any], case: PolicyCase) -> dict[str, Any]:
    label = case.label
    plan = data.get("hint_plan")
    require_type(plan, dict, f"{label}.hint_plan")
    expected_keys = {
        "algorithm_version",
        "text_schema_version",
        "digest",
        "fact_count",
        "primary_count",
        "reserve_count",
        "profile",
        "tile_count",
        "paid_depth",
        "mix",
        "source_count",
        "tile_source_count",
        "paid_source_count",
        "paid_fact_count",
    }
    if set(plan) != expected_keys:
        fail(f"{label}.hint_plan keys differ from the configurable contract: "
             f"{sorted(plan)}")
    if require_int(
        plan, "algorithm_version", f"{label}.hint_plan", 0xFFFF
    ) != CURRENT_ALGORITHM_VERSION:
        fail(f"{label}: unexpected hint algorithm version")
    if require_int(
        plan, "text_schema_version", f"{label}.hint_plan", 0xFFFF
    ) != CURRENT_TEXT_SCHEMA_VERSION:
        fail(f"{label}: unexpected hint text schema version")
    require_digest(plan.get("digest"), f"{label}.hint_plan.digest")
    primary = require_int(
        plan, "primary_count", f"{label}.hint_plan", 24
    )
    reserve = require_int(
        plan, "reserve_count", f"{label}.hint_plan", 24
    )
    fact_count = require_int(
        plan, "fact_count", f"{label}.hint_plan", 24
    )
    if fact_count != primary + reserve:
        fail(f"{label}: fact_count does not equal primary_count + reserve_count")
    if require_str(plan, "profile", f"{label}.hint_plan") != case.profile:
        fail(f"{label}: hint profile is not normalized as requested")
    if require_int(
        plan, "tile_count", f"{label}.hint_plan", 15
    ) != case.tile_count:
        fail(f"{label}: requested tile coverage is not serialized")
    if require_int(
        plan, "paid_depth", f"{label}.hint_plan", 3
    ) != case.paid_depth:
        fail(f"{label}: requested paid depth is not serialized")
    if require_str(plan, "mix", f"{label}.hint_plan") != case.mix:
        fail(f"{label}: requested hint mix is not serialized")
    require_int(plan, "source_count", f"{label}.hint_plan", 18)
    require_int(plan, "tile_source_count", f"{label}.hint_plan", 15)
    require_int(plan, "paid_source_count", f"{label}.hint_plan", 3)
    require_int(plan, "paid_fact_count", f"{label}.hint_plan", 9)
    return plan


def validate_settings_policy(data: dict[str, Any], case: PolicyCase) -> None:
    label = case.label
    settings = data.get("settings")
    require_type(settings, dict, f"{label}.settings")
    if require_int(settings, "hints", f"{label}.settings", 1) != case.mode:
        fail(f"{label}: settings.hints does not match the requested mode")
    expected_coverage = (
        TILE_COVERAGE_IDS[15]
        if case.mode == 0
        else TILE_COVERAGE_IDS[case.tile_count]
    )
    expected_mix = 0 if case.mode == 0 else MIX_IDS[case.mix]
    expected_paid = 3 if case.mode == 0 else case.paid_depth
    if require_int(
        settings, "hint_tile_coverage", f"{label}.settings", 3
    ) != expected_coverage:
        fail(f"{label}: settings hint tile policy is not canonical")
    if require_int(
        settings, "hint_mix", f"{label}.settings", 3
    ) != expected_mix:
        fail(f"{label}: settings hint mix is not canonical")
    if require_int(
        settings, "hint_paid_depth", f"{label}.settings", 3
    ) != expected_paid:
        fail(f"{label}: settings hint paid depth is not canonical")
    if require_bool(
        settings,
        "hint_npc_reward_reveal",
        f"{label}.settings",
    ) != case.npc_reward_reveal:
        fail(f"{label}: NPC reward-preview setting does not match the request")


def validate_text_preview_setting(text: str, case: PolicyCase) -> None:
    matches = [
        match
        for line in text.splitlines()
        if (match := TEXT_NPC_REWARD_PREVIEW_RE.fullmatch(line)) is not None
    ]
    if len(matches) != 1:
        fail(
            f"{case.label}.txt must contain exactly one NPC reward-preview "
            f"setting line, found {len(matches)}"
        )
    actual = matches[0].group(1) == "yes"
    if actual != case.npc_reward_reveal:
        fail(f"{case.label}.txt NPC reward-preview setting does not mirror JSON")


def validate_meta(data: dict[str, Any], expected_hint_rows: int,
                  label: str) -> dict[str, Any]:
    meta = data.get("meta")
    require_type(meta, dict, f"{label}.meta")
    if require_int(meta, "hints_count", f"{label}.meta") != expected_hint_rows:
        fail(f"{label}.meta.hints_count does not mirror hints[]")
    require_digest(meta.get("placement_digest_hex"),
                   f"{label}.meta.placement_digest_hex")
    require_digest(meta.get("sphere_digest"), f"{label}.meta.sphere_digest")
    return meta


def target_values(row: dict[str, Any], path: str) -> dict[str, int]:
    targets: dict[str, int] = {}
    for key in ("location_id", "item_id", "region_id"):
        if key in row:
            targets[key] = require_int(row, key, path, 0xFFFE)
    return targets


def validate_delivery_row(row: Any, index: int, label: str) -> dict[str, Any]:
    path = f"{label}.hints[{index}]"
    require_type(row, dict, path)
    for key in (
        "npc",
        "text",
        "fact_kind",
        "precision",
        "template",
        "source",
        "source_kind",
    ):
        require_str(row, key, path)
    for key in (
        "dialogue_id",
        "fact_id",
        "fact_kind_id",
        "template_id",
        "template_parameter",
        "source_id",
    ):
        require_int(
            row,
            key,
            path,
            0xFFFF if key in ("dialogue_id", "template_parameter") else 0xFF,
        )
    require_bool(row, "primary", path)

    if row["fact_id"] != index:
        fail(f"{path}.fact_id must preserve fact order ({index})")
    if row["npc"] != row["source"]:
        fail(f"{path}: legacy npc and typed source identities disagree")
    if not row["text"]:
        fail(f"{path}.text must not be empty")

    fact_kind = row["fact_kind"]
    template = row["template"]
    expected_kind_id = FACT_KIND_IDS.get(fact_kind)
    if expected_kind_id is None or row["fact_kind_id"] != expected_kind_id:
        fail(f"{path}: fact_kind and fact_kind_id disagree")
    expected_template_id = TEMPLATE_IDS.get(template)
    if expected_template_id is None or row["template_id"] != expected_template_id:
        fail(f"{path}: template and template_id disagree")
    if FACT_TEMPLATE.get(fact_kind) != template:
        fail(f"{path}: fact kind uses an incompatible template")

    precision = row["precision"]
    targets = target_values(row, path)
    if precision == "exact":
        if template != "exact_placement":
            fail(f"{path}: exact precision requires exact_placement template")
        if set(targets) != {"location_id", "item_id", "region_id"}:
            fail(f"{path}: exact fact requires location, item, and region targets")
        if row["template_parameter"] != 0:
            fail(f"{path}: exact facts require template_parameter 0")
    elif precision == "positive_summary":
        if "location_id" in targets or "item_id" in targets:
            fail(f"{path}: summary fact must not expose exact location/item targets")
        if template == "region_value":
            if set(targets) != {"region_id"}:
                fail(f"{path}: region_value summary requires only region_id")
        elif targets:
            fail(f"{path}: non-region summary has unexpected target IDs")
        parameter = row["template_parameter"]
        if template == "goal" and parameter > 6:
            fail(f"{path}: goal parameter is outside the stable goal enum")
        if template == "active_setting" and not 1 <= parameter <= 10:
            fail(f"{path}: active-setting parameter is outside its stable enum")
        if template == "region_value" and parameter < 2:
            fail(f"{path}: region-value parameter must count at least two items")
        if template == "flavor" and parameter > 3:
            fail(f"{path}: flavor parameter is outside the stable v1 table")
    else:
        fail(f"{path}: unsupported precision {precision!r}")

    source_kind = row["source_kind"]
    source_id = row["source_id"]
    if source_kind == "tile":
        tile_index = require_int(row, "tile_index", path, 14)
        if "queue_index" in row:
            fail(f"{path}: tile row contains queue_index")
        if source_id != tile_index + 1:
            fail(f"{path}: tile source_id does not match tile_index")
        if TILE_SOURCES.get(source_id) != row["source"]:
            fail(f"{path}: tile source string and stable source_id disagree")
        if row["dialogue_id"] != 512 + tile_index:
            fail(f"{path}: tile dialogue_id does not match tile_index")
    elif source_kind == "paid":
        require_int(row, "queue_index", path, 2)
        if "tile_index" in row:
            fail(f"{path}: paid row contains tile_index")
        expected = PAID_SOURCES.get(source_id)
        if expected is None:
            fail(f"{path}: unknown paid source_id {source_id}")
        if (row["source"], row["dialogue_id"]) != expected:
            fail(f"{path}: paid source legacy/typed identity disagrees")
    else:
        fail(f"{path}: unsupported source_kind {source_kind!r}")
    return row


def recompute_plan_digest(plan: dict[str, Any],
                          rows: list[dict[str, Any]], mode: int) -> str:
    """Reproduce ``hint_plan_compute_digest`` from public typed spoiler rows."""
    primary_by_npc = [0xFF] * 21
    paid_queue = [[0xFF] * 3 for _ in range(3)]
    paid_count = [0] * 3

    for row in rows:
        source_id = row["source_id"]
        if row["source_kind"] == "tile":
            if primary_by_npc[source_id] != 0xFF:
                fail(f"plan digest input repeats tile source {source_id}")
            primary_by_npc[source_id] = row["fact_id"]
        else:
            source = source_id - 17
            queue = row["queue_index"]
            if paid_queue[source][queue] != 0xFF:
                fail(f"plan digest input repeats paid source {source_id} "
                     f"queue {queue}")
            paid_queue[source][queue] = row["fact_id"]
            paid_count[source] = max(paid_count[source], queue + 1)
            if queue == 0:
                primary_by_npc[source_id] = row["fact_id"]

    algorithm = plan["algorithm_version"]
    text_schema = plan["text_schema_version"]
    if (
        algorithm == LEGACY_ALGORITHM_VERSION
        and text_schema == LEGACY_TEXT_SCHEMA_VERSION
    ):
        encoded = bytearray(b"Z3R-HINT-PLAN")
    elif (
        algorithm == CURRENT_ALGORITHM_VERSION
        and text_schema == CURRENT_TEXT_SCHEMA_VERSION
    ):
        encoded = bytearray(b"Z3R-HINT-PLAN-V2")
    else:
        fail(
            "plan digest input uses an unsupported/crossed "
            f"version pair {algorithm}/{text_schema}"
        )
    encoded += struct.pack("<HHB", algorithm, text_schema, mode)
    if algorithm == CURRENT_ALGORITHM_VERSION:
        mix_id = MIX_IDS.get(plan["mix"])
        if mix_id is None:
            fail(f"plan digest input has unknown mix {plan['mix']!r}")
        encoded += bytes(
            [plan["tile_count"], mix_id, plan["paid_depth"]]
        )
    encoded += bytes(
        [
            plan["fact_count"],
            plan["primary_count"],
            plan["reserve_count"],
        ]
    )
    for row in rows:
        encoded += struct.pack(
            "<BBBBHHHH",
            row["fact_id"],
            row["fact_kind_id"],
            row["template_id"],
            int(row["primary"]),
            row.get("location_id", 0xFFFF),
            row.get("item_id", 0xFFFF),
            row.get("region_id", 0xFFFF),
            row["template_parameter"],
        )
    encoded += bytes(primary_by_npc)
    for source in range(3):
        encoded += bytes([paid_count[source], *paid_queue[source]])
    return hashlib.sha256(encoded).hexdigest()


def validate_compatibility_row(row: Any, path: str) -> dict[str, Any]:
    require_type(row, dict, path)
    if set(row) != COMPAT_ROW_KEYS:
        fail(
            f"{path}: Murahdahla must remain a separate exact four-field "
            "compatibility record"
        )
    if (
        require_str(row, "npc", path) != "murahdahla"
        or require_int(row, "dialogue_id", path) != 527
        or require_str(row, "compatibility", path) != "murahdahla"
    ):
        fail(f"{path}: invalid Murahdahla compatibility identity")
    if not require_str(row, "text", path):
        fail(f"{path}: Murahdahla compatibility text must not be empty")
    return row


def semantic_identity(row: dict[str, Any]) -> tuple[Any, ...]:
    return (
        row["fact_kind_id"],
        row["template_id"],
        row["template_parameter"],
        row.get("location_id"),
        row.get("item_id"),
        row.get("region_id"),
        row["text"],
    )


def assignment_identity(row: dict[str, Any]) -> tuple[int, str, int]:
    delivery_index = (
        row["queue_index"]
        if row["source_kind"] == "paid"
        else row["tile_index"]
    )
    return row["source_id"], row["source_kind"], delivery_index


def validate_case(
    data: dict[str, Any], case: PolicyCase
) -> tuple[dict[str, Any], list[dict[str, Any]], list[dict[str, Any]]]:
    label = case.label
    scan_state_keys(data, label)
    validate_settings_policy(data, case)
    plan = validate_plan(data, case)
    hints = data.get("hints")
    require_type(hints, list, f"{label}.hints")
    validate_meta(data, len(hints), label)

    fact_count = plan["fact_count"]
    expected_hint_rows = fact_count + (1 if case.mode else 0)
    if len(hints) != expected_hint_rows:
        fail(
            f"{label}.hints has {len(hints)} rows; expected {fact_count} "
            f"delivery rows plus {1 if case.mode else 0} compatibility row"
        )
    raw_rows = hints[:fact_count]
    rows = [
        validate_delivery_row(row, index, label)
        for index, row in enumerate(raw_rows)
    ]
    if case.mode:
        validate_compatibility_row(
            hints[-1], f"{label}.hints[{len(hints) - 1}]"
        )
    elif hints:
        fail(f"{label}: normalized Off mode must emit no hint rows")

    if [row["primary"] for row in rows] != (
        [True] * plan["primary_count"]
        + [False] * plan["reserve_count"]
    ):
        fail(f"{label}: primary/reserve facts are not in canonical order")

    tile_rows = [row for row in rows if row["source_kind"] == "tile"]
    paid_rows = [row for row in rows if row["source_kind"] == "paid"]
    tile_indices = [row["tile_index"] for row in tile_rows]
    if len(set(tile_indices)) != len(tile_indices):
        fail(f"{label}: a physical tile source is assigned more than once")
    if len(tile_rows) > case.tile_count:
        fail(f"{label}: retained tile assignments exceed requested coverage")
    if any(not row["primary"] for row in tile_rows):
        fail(f"{label}: tile assignments must be primary facts")
    if any(row["precision"] != "exact" for row in paid_rows):
        fail(f"{label}: every paid fact must remain exact")
    if any(row["queue_index"] >= case.paid_depth for row in paid_rows):
        fail(f"{label}: paid row exceeds the requested queue depth")

    paid_topology = collections.Counter(
        (row["source_id"], row["queue_index"]) for row in paid_rows
    )
    if any(count != 1 for count in paid_topology.values()):
        fail(f"{label}: a paid queue position is assigned more than once")
    paid_sources = 0
    for source_id in PAID_SOURCES:
        queues = sorted(
            row["queue_index"]
            for row in paid_rows
            if row["source_id"] == source_id
        )
        if queues:
            paid_sources += 1
        if queues != list(range(len(queues))):
            fail(f"{label}: paid source {source_id} does not form a prefix")
    if any(row["primary"] != (row["queue_index"] == 0) for row in paid_rows):
        fail(f"{label}: paid queue heads must be primary and reserves must follow")

    # Algorithm 2 compacts tile primaries first, then paid heads in service
    # order, then reserves in queue-major/service-major order.
    if rows[:len(tile_rows)] != tile_rows:
        fail(f"{label}: tile facts are not the canonical compact prefix")
    paid_assignment_order = [
        (row["queue_index"], row["source_id"]) for row in paid_rows
    ]
    if paid_assignment_order != sorted(paid_assignment_order):
        fail(f"{label}: paid facts are not in canonical queue/service order")

    tile_count = len(tile_rows)
    paid_fact_count = len(paid_rows)
    source_count = tile_count + paid_sources
    expected_counts = {
        "fact_count": tile_count + paid_fact_count,
        "primary_count": tile_count + paid_sources,
        "reserve_count": paid_fact_count - paid_sources,
        "source_count": source_count,
        "tile_source_count": tile_count,
        "paid_source_count": paid_sources,
        "paid_fact_count": paid_fact_count,
    }
    for key, expected in expected_counts.items():
        if plan[key] != expected:
            fail(
                f"{label}.hint_plan.{key}={plan[key]} does not match "
                f"derived topology {expected}"
            )

    if case.require_full and case.mode:
        if tile_count != case.tile_count:
            fail(
                f"{label}: rich fixture underfilled requested tile coverage "
                f"({tile_count}/{case.tile_count})"
            )
        for source_id in PAID_SOURCES:
            depth = sum(
                row["source_id"] == source_id for row in paid_rows
            )
            if depth != case.paid_depth:
                fail(
                    f"{label}: rich fixture underfilled paid source "
                    f"{source_id} ({depth}/{case.paid_depth})"
                )

    semantic_ids = [
        (
            row["template_id"],
            row["template_parameter"],
            row.get("location_id"),
            row.get("item_id"),
            row.get("region_id"),
        )
        for row in rows
    ]
    if len(set(semantic_ids)) != len(semantic_ids):
        fail(f"{label}: duplicate semantic hint facts escaped planning")

    for row in rows:
        lowered = row["text"].lower()
        for unsupported in (
            "way of the hero",
            "required for",
            "barren",
            "foolish",
        ):
            if unsupported in lowered:
                fail(
                    f"{label}: unsupported necessity/barren claim "
                    f"{unsupported!r} entered a hint"
                )

    computed_digest = recompute_plan_digest(plan, rows, case.mode)
    if computed_digest != plan["digest"]:
        fail(f"{label}: advertised digest does not match typed facts/assignments")
    if case.mode == 0 and plan["digest"] != CURRENT_OFF_DIGEST:
        fail(
            f"{label}: current Off empty-plan digest changed without a "
            "compatibility-version update"
        )
    return plan, hints, rows


def validate_digest_version_canaries() -> None:
    legacy = {
        "algorithm_version": LEGACY_ALGORITHM_VERSION,
        "text_schema_version": LEGACY_TEXT_SCHEMA_VERSION,
        "fact_count": 0,
        "primary_count": 0,
        "reserve_count": 0,
        # Deliberately nonsensical policy: algorithm 1 must ignore these bytes.
        "tile_count": 5,
        "paid_depth": 1,
        "mix": "difficult",
    }
    if recompute_plan_digest(legacy, [], 0) != LEGACY_OFF_DIGEST:
        fail("independent algorithm-1 empty-plan digest canary changed")
    current = {
        "algorithm_version": CURRENT_ALGORITHM_VERSION,
        "text_schema_version": CURRENT_TEXT_SCHEMA_VERSION,
        "fact_count": 0,
        "primary_count": 0,
        "reserve_count": 0,
        "tile_count": 0,
        "paid_depth": 0,
        "mix": "not-applicable",
    }
    if recompute_plan_digest(current, [], 0) != CURRENT_OFF_DIGEST:
        fail("independent algorithm-2 empty-plan digest canary changed")


def parse_text_hints(text: str, label: str) -> dict[str, Any]:
    lines = text.splitlines()
    try:
        start = lines.index("Hints:")
    except ValueError:
        fail(f"{label}.txt is missing the Hints section")
    if start + 5 >= len(lines) or lines[start + 1] != "------":
        fail(f"{label}.txt has a malformed Hints heading")

    plan_match = TEXT_PLAN_RE.fullmatch(lines[start + 2])
    counts_match = TEXT_COUNTS_RE.fullmatch(lines[start + 3])
    policy_match = TEXT_POLICY_RE.fullmatch(lines[start + 4])
    sources_match = TEXT_SOURCES_RE.fullmatch(lines[start + 5])
    if (
        plan_match is None
        or counts_match is None
        or policy_match is None
        or sources_match is None
    ):
        fail(f"{label}.txt has malformed plan/count/policy/source headers")
    parsed: dict[str, Any] = {
        "algorithm_version": int(plan_match.group(1)),
        "text_schema_version": int(plan_match.group(2)),
        "digest": plan_match.group(3),
        "primary_count": int(counts_match.group(1)),
        "reserve_count": int(counts_match.group(2)),
        "profile": policy_match.group(1),
        "tile_count": int(policy_match.group(2)),
        "paid_depth": int(policy_match.group(3)),
        "mix": policy_match.group(4),
        "source_count": int(sources_match.group(1)),
        "tile_source_count": int(sources_match.group(2)),
        "paid_source_count": int(sources_match.group(3)),
        "paid_fact_count": int(sources_match.group(4)),
        "rows": [],
        "compat": None,
    }

    index = start + 6
    while index < len(lines):
        line = lines[index]
        if not line:
            index += 1
            continue
        compat_match = TEXT_COMPAT_RE.fullmatch(line)
        if compat_match is not None:
            if parsed["compat"] is not None:
                fail(f"{label}.txt contains multiple compatibility rows")
            parsed["compat"] = {
                "npc": compat_match.group(1),
                "text": compat_match.group(2),
            }
            index += 1
            continue
        fact_match = TEXT_FACT_RE.fullmatch(line)
        if fact_match is None:
            fail(f"{label}.txt has unexpected Hints line: {line!r}")
        if index + 1 >= len(lines) or not lines[index + 1].startswith("       "):
            fail(f"{label}.txt fact row lacks its indented rendered text")

        tail = fact_match.group(10)
        target_pairs = TEXT_TARGET_RE.findall(tail)
        if "".join(f" {key}:{value}" for key, value in target_pairs) != tail:
            fail(f"{label}.txt fact row has malformed target metadata: {tail!r}")
        targets: dict[str, int] = {}
        for key, value in target_pairs:
            if key in targets:
                fail(f"{label}.txt fact row repeats {key!r}")
            targets[key] = int(value)
        parsed["rows"].append({
            "fact_id": int(fact_match.group(1)),
            "source": fact_match.group(2),
            "primary": fact_match.group(3) == "primary",
            "delivery_kind": fact_match.group(4),
            "delivery_index": int(fact_match.group(5)),
            "fact_kind": fact_match.group(6),
            "precision": fact_match.group(7),
            "template": fact_match.group(8),
            "template_parameter": int(fact_match.group(9)),
            "targets": targets,
            "text": lines[index + 1][7:],
        })
        index += 2

    scan_state_text("\n".join(lines[start:]), f"{label}.txt Hints section")
    return parsed


def validate_text_mirror(text: str, plan: dict[str, Any],
                         hints: list[dict[str, Any]], label: str) -> None:
    parsed = parse_text_hints(text, label)
    for key in (
        "algorithm_version",
        "text_schema_version",
        "digest",
        "primary_count",
        "reserve_count",
        "profile",
        "tile_count",
        "paid_depth",
        "mix",
        "source_count",
        "tile_source_count",
        "paid_source_count",
        "paid_fact_count",
    ):
        if parsed[key] != plan[key]:
            fail(f"{label}.txt {key} does not mirror JSON hint_plan")

    delivery_rows = [
        row for row in hints if row.get("compatibility") != "murahdahla"
    ]
    if len(parsed["rows"]) != len(delivery_rows):
        fail(f"{label}.txt delivery row count does not mirror JSON")
    for text_row, json_row in zip(parsed["rows"], delivery_rows):
        expected_targets = {
            key[:-3]: json_row[key]
            for key in ("location_id", "item_id", "region_id")
            if key in json_row
        }
        expected = {
            "fact_id": json_row["fact_id"],
            "source": json_row["source"],
            "primary": json_row["primary"],
            "delivery_kind": (
                "queue" if json_row["source_kind"] == "paid" else "tile"
            ),
            "delivery_index": json_row.get(
                "queue_index", json_row.get("tile_index")
            ),
            "fact_kind": json_row["fact_kind"],
            "precision": json_row["precision"],
            "template": json_row["template"],
            "template_parameter": json_row["template_parameter"],
            "targets": expected_targets,
            "text": json_row["text"],
        }
        if text_row != expected:
            fail(f"{label}.txt fact {json_row['fact_id']} does not mirror JSON")

    compat_rows = [
        row for row in hints if row.get("compatibility") == "murahdahla"
    ]
    if compat_rows:
        expected_compat = {
            "npc": compat_rows[0]["npc"],
            "text": compat_rows[0]["text"],
        }
        if parsed["compat"] != expected_compat:
            fail(f"{label}.txt Murahdahla row does not mirror JSON")
    elif parsed["compat"] is not None:
        fail(f"{label}.txt contains a compatibility row absent from JSON")


def validate_nested_prefix(
    smaller_rows: list[dict[str, Any]],
    larger_rows: list[dict[str, Any]],
    label: str,
) -> None:
    larger = {
        assignment_identity(row): semantic_identity(row)
        for row in larger_rows
    }
    for row in smaller_rows:
        assignment = assignment_identity(row)
        if assignment not in larger:
            fail(f"{label}: retained assignment {assignment} disappeared")
        if semantic_identity(row) != larger[assignment]:
            fail(f"{label}: retained assignment {assignment} rerolled its fact")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--binary",
        required=True,
        type=Path,
        help="Randomizer executable to exercise",
    )
    args = parser.parse_args()
    binary = args.binary
    if not binary.is_absolute():
        binary = (Path.cwd() / binary).resolve()
    if not binary.is_file():
        print(f"check_hint_spoiler: binary not found: {binary}", file=sys.stderr)
        return 2

    try:
        validate_digest_version_canaries()
        with tempfile.TemporaryDirectory(prefix="rando-hint-spoiler-") as temp:
            output_dir = Path(temp)
            results: dict[
                str,
                tuple[
                    PolicyCase,
                    dict[str, Any],
                    str,
                    dict[str, Any],
                    list[dict[str, Any]],
                    list[dict[str, Any]],
                ],
            ] = {}
            for case in POLICY_CASES:
                data, text = run_generation(binary, case, output_dir)
                plan, hints, rows = validate_case(data, case)
                validate_text_mirror(text, plan, hints, case.label)
                validate_text_preview_setting(text, case)
                results[case.label] = (
                    case,
                    data,
                    text,
                    plan,
                    hints,
                    rows,
                )

            balanced_a = results["balanced-a"]
            balanced_b = results["balanced-b"]
            if balanced_a[3] != balanced_b[3] or balanced_a[4] != balanced_b[4]:
                fail("Balanced hint_plan/hints are not deterministic across runs")
            balanced_npc_preview = results["balanced-npc-preview"]
            if (
                balanced_a[3] != balanced_npc_preview[3]
                or balanced_a[4] != balanced_npc_preview[4]
                or balanced_a[5] != balanced_npc_preview[5]
            ):
                fail(
                    "NPC reward preview changed the non-empty Balanced hint "
                    "plan or delivery rows"
                )

            off = results["off"]
            off_npc_preview = results["off-npc-preview"]
            normalized_off = results["empty-normalizes-off"]
            if off[3] != normalized_off[3] or off[4] != normalized_off[4]:
                fail("empty custom tuple does not normalize to canonical Off")
            if off[3] != off_npc_preview[3] or off[4] != off_npc_preview[4]:
                fail(
                    "NPC reward preview changed the independent Off hint "
                    "plan or delivery rows"
                )
            off_canon = bytes.fromhex(off[1]["settings"]["canonical_hex"])
            preview_canon = bytes.fromhex(
                off_npc_preview[1]["settings"]["canonical_hex"]
            )
            expected_preview_canon = bytearray(off_canon)
            expected_preview_canon[25] |= 0x80
            if preview_canon != bytes(expected_preview_canon):
                fail(
                    "NPC reward preview did not change only canonical "
                    "settings byte [25] bit 7"
                )
            for key in (
                "settings_hash_hex",
                "settings_hash_full_hex",
                "share_string",
            ):
                if off[1]["meta"][key] == off_npc_preview[1]["meta"][key]:
                    fail(
                        f"NPC reward preview did not change settings identity "
                        f"field {key}"
                    )

            baseline_meta = balanced_a[1]["meta"]
            for key in ("placement_digest_hex", "sphere_digest"):
                for case, data, _text, _plan, _hints, _rows in results.values():
                    if data["meta"][key] != baseline_meta[key]:
                        fail(
                            f"{case.label}: hint policy changed placement "
                            f"identity field {key}"
                        )

            validate_nested_prefix(
                results["sparse"][5],
                results["balanced-a"][5],
                "Sparse -> Balanced nested prefix",
            )

            # Algorithm-2 identity is policy-bound. Equal normalized policies
            # (the two Balanced and two Off forms) must match; every distinct
            # tuple must carry a distinct digest even if scarcity could have
            # left two retained decks otherwise equal.
            digest_by_policy: dict[tuple[str, int, int, str], str] = {}
            for case, _data, _text, plan, _hints, _rows in results.values():
                policy = (
                    case.profile,
                    case.tile_count,
                    case.paid_depth,
                    case.mix,
                )
                previous = digest_by_policy.setdefault(policy, plan["digest"])
                if previous != plan["digest"]:
                    fail(f"{case.label}: equal normalized policy changed digest")
            distinct_digests = set(digest_by_policy.values())
            if len(distinct_digests) != len(digest_by_policy):
                fail("distinct normalized policies collided in plan identity")
    except ContractError as exc:
        print(f"check_hint_spoiler: FAIL: {exc}", file=sys.stderr)
        return 1

    print(
        "check_hint_spoiler: OK "
        "(12-run profile/policy matrix, independent NPC reward previews, "
        "variable tile/paid topology, "
        "v1/v2 independent digests, nested Variety prefixes, JSON/text mirror, "
        "canonical Off, no mutable state)"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
