#!/usr/bin/env python3
"""Focused Hints v2 metadata/codegen drift guard.

This is intentionally fast enough for the source-validation profile. The full
``rando_logic_gen.py --strict`` invocation repeats the same validation after
merging optional ROM-derived pot/enemy/terrain/bonk locations.
"""
from __future__ import annotations

import sys
from pathlib import Path

import yaml


REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "assets"))

import rando_logic_gen as codegen  # noqa: E402


def fail(message: str) -> None:
    raise SystemExit(f"check_hint_metadata: {message}")


def main() -> None:
    assets = REPO / "assets" / "rando"
    items = codegen.load_items(assets / "item_registry.yaml")
    locations = codegen.load_locations(assets / "location_registry.yaml")
    (
        regions,
        edges,
        _location_predicates,
        _macros,
        world_state_overrides,
        _world_state_edges,
    ) = codegen.load_logic(assets / "logic.yaml")
    contract_regions = dict(regions)
    contract_region_ids = codegen._region_ids_for_codegen(contract_regions)
    ow_regions, _ow_edges, _ow_meta = codegen.load_ow_graph(
        assets / "ow_graph.gen.yaml", regions, edges
    )
    for region_id, region in ow_regions.items():
        if region_id in regions:
            fail(f"overworld region {region_id!r} collides with base logic")
        regions[region_id] = region
    region_ids = codegen._region_ids_for_codegen(regions)
    registry_contract = codegen.load_hint_registry_contract(
        assets / "hint_registry_contract.json"
    )
    metadata = codegen.load_hint_metadata(
        assets / "hint_metadata.yaml",
        items,
        locations,
        contract_locations=locations,
        regions=regions,
        region_ids=region_ids,
        contract_regions=contract_regions,
        contract_region_ids=contract_region_ids,
        contract_world_state_overrides=world_state_overrides,
        registry_contract=registry_contract,
    )
    codegen.validate_hint_metadata_lock(
        assets / "hint_metadata.lock.json",
        metadata,
        codegen.hint_runtime_algorithm_version(
            REPO / "src" / "rando" / "rando_hints.h"
        ),
        codegen.hint_runtime_text_schema_version(
            REPO / "src" / "rando" / "rando_hints.h"
        ),
    )
    if len(metadata.items) != len(items):
        fail("generated item metadata does not cover the item registry")
    if len(metadata.locations) != len(locations):
        fail("generated location metadata does not cover the location registry")
    if len(metadata.regions) != len(regions):
        fail("generated region metadata does not cover all shipped regions")

    # Exercise the public/assetless contract without renaming the user's local
    # ignored artifacts. Exact base-only inputs may consume approved OWC aliases
    # through the locked shipped superset; a partially present graph must fail.
    base_metadata = codegen.load_hint_metadata(
        assets / "hint_metadata.yaml",
        items,
        locations,
        contract_locations=locations,
        regions=contract_regions,
        region_ids=contract_region_ids,
        contract_regions=contract_regions,
        contract_region_ids=contract_region_ids,
        contract_world_state_overrides=world_state_overrides,
        registry_contract=registry_contract,
    )
    if len(base_metadata.regions) != registry_contract["base_region_count"]:
        fail("exact assetless region contract did not stay base-only")
    first_ow_region = next(iter(ow_regions.items()), None)
    if first_ow_region is not None:
        partial_regions = dict(contract_regions)
        partial_regions[first_ow_region[0]] = first_ow_region[1]
        partial_region_ids = codegen._region_ids_for_codegen(partial_regions)
        try:
            codegen.load_hint_metadata(
                assets / "hint_metadata.yaml",
                items,
                locations,
                contract_locations=locations,
                regions=partial_regions,
                region_ids=partial_region_ids,
                contract_regions=contract_regions,
                contract_region_ids=contract_region_ids,
                contract_world_state_overrides=world_state_overrides,
                registry_contract=registry_contract,
            )
        except RuntimeError as exc:
            if "region_aliases references unknown regions" not in str(exc):
                fail(f"partial-region rejection failed unexpectedly: {exc}")
        else:
            fail("partial region artifact set was accepted")
        try:
            codegen.validate_hint_registry_contract(
                assets / "hint_registry_contract.json",
                locations,
                partial_regions,
                partial_region_ids,
                world_state_overrides,
                base_location_count=len(locations),
                base_region_count=len(contract_region_ids),
            )
        except RuntimeError as exc:
            if "drift/partial artifact set" not in str(exc):
                fail(f"partial registry rejection failed unexpectedly: {exc}")
        else:
            fail("partial merged-registry contract was accepted")

    doc = yaml.safe_load(
        (assets / "hint_metadata.yaml").read_text(encoding="utf-8")
    )
    explicit = doc["location_aliases"]
    base = doc["location_base_aliases"]
    compact_52 = codegen._hint_compact_location_name(
        "DarkWorld_NorthWest Bush S52 P05AE", explicit, base
    )
    compact_54 = codegen._hint_compact_location_name(
        "DarkWorld_NorthWest Bush S54 P05AE", explicit, base
    )
    if compact_52 != "DWNWB S52 P05AE":
        fail(f"S52 coordinate compacted unexpectedly as {compact_52!r}")
    if compact_54 != "DWNWB S54 P05AE":
        fail(f"S54 coordinate compacted unexpectedly as {compact_54!r}")

    hookshot = metadata.items[items["Hookshot"].id].qualified_name
    exact_52 = f"{hookshot} at {compact_52}"
    exact_54 = f"{hookshot} at {compact_54}"
    if exact_52 == exact_54:
        fail("final exact rendered identity collides for S52/S54")

    script_child = codegen._hint_compact_location_name(
        "Enemy Check - Room 0x0A8 Script 04 Child 3 - Red Stalfos",
        explicit,
        base,
    )
    if script_child != "Enemy R0A8 X04 C3":
        fail(f"script/child coordinate compacted unexpectedly as {script_child!r}")

    # Runtime C must consume the generated layer, not quietly grow a second
    # registry switch/table that bypasses codegen validation.
    runtime = (REPO / "src" / "rando" / "rando_hints.c").read_text(
        encoding="utf-8"
    )
    if '#include "hint_metadata.h"' not in runtime:
        fail("rando_hints.c does not include the generated metadata artifact")
    forbidden = {
        "kHintLocationAliases": "hand-written location alias table",
        "kHintDungeonAliases": "hand-written dungeon alias table",
        "kHighFrictionHintLocations": "hand-written high-friction table",
        "case ITEM_ProgressiveGlove:": "hand-written priority-item switch",
    }
    for needle, label in forbidden.items():
        if needle in runtime:
            fail(f"{label} reappeared in rando_hints.c")

    print(
        "check_hint_metadata: "
        f"{len(metadata.items)} items, {len(metadata.locations)} tracked "
        f"locations, {len(metadata.regions)} regions; "
        "classification/alias/final-encoded-text + assetless/partial guards OK"
    )


if __name__ == "__main__":
    main()
