#!/usr/bin/env python3
"""Audit static enemies for enemy-check tiers.

This is intentionally an audit generator, not the final registry. It scans local
ROM-derived sprite assets, reuses the curated enemy-shuffle constraint table as
the first-pass safety oracle, and reports whether candidate sets have stable
runtime identity and location-capacity headroom.
"""
from __future__ import annotations

import argparse
import difflib
import hashlib
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path

import yaml

SCRIPT_DIR = Path(__file__).resolve().parent
REPO = SCRIPT_DIR.parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

from gen_enemy_drop_tables import (  # noqa: E402
    collect_forced_drops,
    parse_u16le_array,
    read_assets,
    sprite_entries,
)

DEFAULT_ASSETS = REPO / "zelda3_assets.dat"
DEFAULT_OUT = REPO / "assets" / "rando" / "enemy_check_candidates.audit.yaml"
SHUFFLE_ENEMIES_C = REPO / "src" / "rando" / "shuffle_enemies.c"
LOCATION_IDS_H = REPO / "src" / "rando" / "location_ids.h"
RANDO_LOGIC_H = REPO / "src" / "rando" / "rando_logic.h"
POT_REGISTRY = REPO / "assets" / "rando" / "pots.gen.yaml"
ENEMY_CHECK_REGISTRY = REPO / "assets" / "rando" / "enemy_checks.gen.yaml"
ENTRANCE_REGISTRY = REPO / "assets" / "rando" / "entrance_registry.yaml"
ENEMY_CHECK_BASE_ID = 1400


def die(msg: str) -> None:
    print(f"audit_enemy_check_candidates: ERROR: {msg}", file=sys.stderr)
    raise SystemExit(2)


def parse_enemy_constraints(path: Path) -> dict[int, dict]:
    text = path.read_text(encoding="utf-8")
    pattern = re.compile(
        r"\[0x([0-9A-Fa-f]{2})\]\s*=\s*\{\s*\(([^)]*)\)\s*,\s*\{([^}]*)\}\s*\}\s*,\s*//\s*(.*)"
    )
    out: dict[int, dict] = {}
    for m in pattern.finditer(text):
        sid = int(m[1], 16)
        flags = set(re.findall(r"ESF_[A-Z_]+", m[2]))
        sheets = [int(x) for x in re.findall(r"\d+", m[3])]
        out[sid] = {
            "name": m[4].strip(),
            "flags": sorted(flags),
            "sheets": sheets,
        }
    if not out:
        die(f"{path}: could not parse kEnemyTable rows")
    return out


def exclusion_reason(info: dict | None, *, allow_cannot_key: bool,
                     allow_flying: bool) -> str | None:
    if info is None or "ESF_RANDOMIZABLE" not in info["flags"]:
        return "not_enemy_shuffle_randomizable"
    if "ESF_KILLABLE" not in info["flags"]:
        return "not_killable"
    if not allow_cannot_key and "ESF_CANNOT_KEY" in info["flags"]:
        return "cannot_have_key_class"
    if not allow_flying and "ESF_FLYING" in info["flags"]:
        return "flying_class"
    return None


def source_name(info: dict | None, typ: int) -> str:
    if info is None:
        return f"Sprite 0x{typ:02X}"
    return info.get("name") or f"Sprite 0x{typ:02X}"


def forced_drop_source_set(assets: dict[str, bytes]) -> set[tuple[int, int]]:
    small, big = collect_forced_drops(assets)
    return {(int(r["room"]), int(r["source_slot"])) for r in small + big}


def collect_dungeon_candidates(assets: dict[str, bytes], constraints: dict[int, dict],
                               forced_sources: set[tuple[int, int]],
                               *, allow_cannot_key: bool,
                               allow_flying: bool,
                               extra_source_constraints: dict[int, dict] | None = None,
                               ) -> tuple[list[dict], Counter]:
    try:
        sprites = assets["kDungeonSprites"]
        offsets = parse_u16le_array(assets["kDungeonSpriteOffs"])
    except KeyError as e:
        die(f"missing asset {e.args[0]} in zelda3_assets.dat")

    rows: list[dict] = []
    excluded: Counter = Counter()
    for room in range(len(offsets)):
        runtime_slot = 0
        for _list_index, y, x, typ in sprite_entries(sprites, offsets, room):
            if typ == 0xE4:
                continue
            if x >= 0xE0:
                excluded["overlord_or_control"] += 1
                continue
            slot = runtime_slot
            runtime_slot += 1
            if (room, slot) in forced_sources:
                excluded["existing_forced_key_drop_check"] += 1
                continue
            # The enemy-shuffle table is the conservative default oracle, but
            # an all-tier check may safely model a static source that is not a
            # safe shuffle replacement. Callers must opt those source types in
            # with their own reviewed constraint/name metadata.
            extra_constraints = extra_source_constraints or {}
            info = extra_constraints.get(typ, constraints.get(typ))
            reason = (
                None if typ in extra_constraints else
                exclusion_reason(info, allow_cannot_key=allow_cannot_key,
                                 allow_flying=allow_flying)
            )
            if reason is not None:
                excluded[reason] += 1
                continue
            rows.append({
                "domain": "dungeon",
                "room": room,
                "source_slot": slot,
                "source_type": typ,
                "source_name": source_name(info, typ),
                "source_y": y,
                "source_x": x,
                "runtime_identity": f"dungeon:0x{room:03X}:{slot}",
            })
    return rows, excluded


def overworld_block(y: int, x: int) -> int:
    r2 = (y >> 4) << 2
    r6 = (x >> 4) + r2
    r5 = (x & 0x0F) | ((y << 4) & 0xFF)
    return r5 | (r6 << 8)


def iter_overworld_entries(sprites: bytes, offsets: list[int]):
    for offset_index, off in enumerate(offsets):
        if off >= len(sprites):
            die(f"overworld offset index {offset_index} points outside kOverworldSprites")
        stage = offset_index // 144
        area = offset_index % 144
        pos = off
        slot = 0
        while pos < len(sprites):
            y = sprites[pos]
            if y == 0xFF:
                break
            if pos + 2 >= len(sprites):
                die(f"overworld offset index {offset_index} sprite list is truncated")
            x = sprites[pos + 1]
            typ = sprites[pos + 2]
            yield stage, area, slot, y, x, typ
            slot += 1
            pos += 3
        else:
            die(f"overworld offset index {offset_index} sprite list missing 0xff terminator")


def collect_overworld_candidates(assets: dict[str, bytes], constraints: dict[int, dict],
                                 *, allow_cannot_key: bool,
                                 allow_flying: bool) -> tuple[list[dict], list[dict], Counter]:
    try:
        sprites = assets["kOverworldSprites"]
        offsets = parse_u16le_array(assets["kOverworldSpriteOffs"])
    except KeyError as e:
        die(f"missing asset {e.args[0]} in zelda3_assets.dat")

    raw_rows: list[dict] = []
    identity_rows: list[dict] = []
    excluded: Counter = Counter()
    for stage, area, slot, y, x, typ in iter_overworld_entries(sprites, offsets):
        if typ == 0xF4:
            excluded["sprite_count_marker"] += 1
            continue
        if typ >= 0xF3:
            excluded["overlord_or_control"] += 1
            continue
        info = constraints.get(typ)
        reason = exclusion_reason(info, allow_cannot_key=allow_cannot_key,
                                  allow_flying=allow_flying)
        block = overworld_block(y, x)
        row = {
            "domain": "overworld",
            "stage": stage,
            "area": area,
            "source_slot": slot,
            "block": block,
            "source_type": typ,
            "source_name": source_name(info, typ),
            "source_y": y,
            "source_x": x,
            "runtime_identity": f"overworld:{stage}:0x{area:02X}:{slot}:0x{block:04X}",
        }
        identity_rows.append(row)
        if reason is not None:
            excluded[reason] += 1
            continue
        raw_rows.append(row)

    by_runtime_key: dict[tuple[int, int, int], list[dict]] = defaultdict(list)
    for row in identity_rows:
        by_runtime_key[(int(row["stage"]), int(row["area"]), int(row["block"]))].append(row)

    collision_groups = []
    blocked_keys = set()
    for key, hits in sorted(by_runtime_key.items()):
        distinct_sources = {
            (int(r["source_slot"]), int(r["source_type"]))
            for r in hits
        }
        if len(distinct_sources) <= 1:
            continue
        blocked_keys.add(key)
        collision_groups.append({
            "stage": key[0],
            "area": key[1],
            "block": key[2],
            "count": len(hits),
            "sources": [
                {
                    "source_slot": int(r["source_slot"]),
                    "source_type": int(r["source_type"]),
                    "source_name": r["source_name"],
                }
                for r in hits[:8]
            ],
        })

    rows = [
        r for r in raw_rows
        if (int(r["stage"]), int(r["area"]), int(r["block"])) not in blocked_keys
    ]
    excluded["overworld_runtime_identity_collision"] += len(raw_rows) - len(rows)
    return rows, collision_groups, excluded


def parse_define(path: Path, name: str) -> int:
    if not path.exists():
        die(f"missing {path}; run codegen/build first")
    m = re.search(rf"#define\s+{re.escape(name)}\s+(\d+)",
                  path.read_text(encoding="utf-8", errors="replace"))
    if not m:
        die(f"{path}: missing #define {name}")
    return int(m[1])


def load_pot_room_predicates(path: Path) -> dict[int, list[dict]]:
    if not path.exists():
        return {}
    doc = yaml.safe_load(path.read_text(encoding="utf-8")) or {}
    out: dict[int, list[dict]] = defaultdict(list)
    seen = set()
    for raw in doc.get("pots", []) or []:
        if "room" not in raw or "can_reach" not in raw:
            continue
        room = int(raw["room"])
        key = (room, raw.get("region"), raw.get("can_reach"))
        if key in seen:
            continue
        seen.add(key)
        out[room].append({
            "region": raw.get("region"),
            "can_reach": raw.get("can_reach"),
        })
    return dict(out)


def load_region_only_pot_rooms(path: Path) -> dict[int, str]:
    """Rooms whose pot rows prove region membership with no extra local gate."""
    if not path.exists():
        return {}
    doc = yaml.safe_load(path.read_text(encoding="utf-8")) or {}
    by_room: dict[int, list[dict]] = defaultdict(list)
    for raw in doc.get("pots", []) or []:
        if "room" not in raw or "region" not in raw:
            continue
        by_room[int(raw["room"])].append(raw)

    out = {}
    for room, rows in by_room.items():
        if not rows or any("can_reach" in r for r in rows):
            continue
        regions = {str(r["region"]) for r in rows if r.get("region")}
        if len(regions) == 1:
            out[room] = next(iter(regions))
    return out


def load_entry_room_predicates(path: Path) -> dict[int, dict]:
    if not path.exists():
        return {}
    doc = yaml.safe_load(path.read_text(encoding="utf-8")) or {}
    out = {}
    for raw in doc.get("dungeons", []) or []:
        entry_region = raw.get("entry_region")
        if not entry_region:
            continue
        for room in raw.get("rooms", []) or []:
            out[int(room)] = {
                "entry_region": str(entry_region),
                "dungeon_id": raw.get("dungeon_id"),
            }
    return out


def load_enemy_check_registry_summary(path: Path) -> dict:
    if not path.exists():
        return {
            "source_available": False,
            "candidate_count": 0,
            "emitted_count": 0,
            "emitted_dungeon_count": 0,
            "emitted_underworld_all_tier_count": 0,
            "emitted_overworld_count": 0,
            "emitted_boss_count": 0,
            "emitted_scripted_spawn_count": 0,
            "out_of_scope_no_key_depth_count": 0,
            "max_loc_plus_one": 0,
            "emitted_dungeon_keys": set(),
        }
    doc = yaml.safe_load(path.read_text(encoding="utf-8")) or {}
    summary = doc.get("summary", {}) or {}
    rows = doc.get("enemy_checks", []) or []
    max_loc_plus_one = 0
    emitted_dungeon_keys = set()
    for raw in rows:
        if "id" not in raw:
            continue
        max_loc_plus_one = max(max_loc_plus_one, int(raw["id"]) + 1)
        if raw.get("domain") == "dungeon":
            emitted_dungeon_keys.add((int(raw["room"]), int(raw["source_slot"])))
    emitted_count = int(summary.get("emitted_count", len(rows)))
    emitted_dungeon_count = int(summary.get("emitted_dungeon_count", emitted_count))
    emitted_underworld_all_tier_count = int(
        summary.get("emitted_underworld_all_tier_count", 0))
    emitted_overworld_count = int(summary.get("emitted_overworld_count", 0))
    emitted_boss_count = int(summary.get("emitted_boss_count", 0))
    emitted_scripted_spawn_count = int(summary.get("emitted_scripted_spawn_count", 0))
    return {
        "source_available": True,
        "candidate_count": int(summary.get("candidate_count", len(rows))),
        "emitted_count": emitted_count,
        "emitted_dungeon_count": emitted_dungeon_count,
        "emitted_underworld_all_tier_count": emitted_underworld_all_tier_count,
        "emitted_overworld_count": emitted_overworld_count,
        "emitted_boss_count": emitted_boss_count,
        "emitted_scripted_spawn_count": emitted_scripted_spawn_count,
        "out_of_scope_no_key_depth_count": int(summary.get("out_of_scope_no_key_depth_count", 0)),
        "max_loc_plus_one": max_loc_plus_one,
        "emitted_dungeon_keys": emitted_dungeon_keys,
    }


def dungeon_predicate_coverage(dungeon_rows: list[dict],
                               pot_predicates: dict[int, list[dict]],
                               region_only_pot_rooms: dict[int, str],
                               entry_room_predicates: dict[int, dict],
                               emitted_dungeon_keys: set[tuple[int, int]]) -> dict:
    rooms = sorted({int(r["room"]) for r in dungeon_rows})
    pot_covered = [room for room in rooms if room in pot_predicates]
    pot_region_covered = [room for room in rooms if room in region_only_pot_rooms]
    entry_covered = [room for room in rooms if room in entry_room_predicates]
    covered = sorted(set(pot_covered) | set(pot_region_covered) | set(entry_covered))
    uncovered = [room for room in rooms if room not in set(covered)]
    pot_covered_set = set(pot_covered)
    pot_region_covered_set = set(pot_region_covered)
    entry_covered_set = set(entry_covered)
    covered_set = set(covered)
    reviewed_uncovered_rows = [
        r for r in dungeon_rows
        if int(r["room"]) not in covered_set and
        (int(r["room"]), int(r["source_slot"])) in emitted_dungeon_keys
    ]
    still_uncovered_rows = [
        r for r in dungeon_rows
        if int(r["room"]) not in covered_set and
        (int(r["room"]), int(r["source_slot"])) not in emitted_dungeon_keys
    ]
    reviewed_uncovered_rooms = sorted({int(r["room"]) for r in reviewed_uncovered_rows})
    still_uncovered_rooms = sorted({int(r["room"]) for r in still_uncovered_rows})
    return {
        "pot_predicate_source": POT_REGISTRY.relative_to(REPO).as_posix(),
        "pot_predicate_source_available": POT_REGISTRY.exists(),
        "entry_room_source": ENTRANCE_REGISTRY.relative_to(REPO).as_posix(),
        "entry_room_source_available": ENTRANCE_REGISTRY.exists(),
        "candidate_rooms": len(rooms),
        "rooms_with_pot_predicate": len(pot_covered),
        "rooms_with_region_only_pot_predicate": len(pot_region_covered),
        "rooms_with_entry_room_predicate": len(entry_covered),
        "rooms_with_any_conservative_predicate": len(covered),
        "rooms_without_any_conservative_predicate": len(uncovered),
        "candidates_with_pot_predicate": sum(1 for r in dungeon_rows if int(r["room"]) in pot_covered_set),
        "candidates_with_region_only_pot_predicate": sum(
            1 for r in dungeon_rows if int(r["room"]) in pot_region_covered_set),
        "candidates_with_entry_room_predicate": sum(
            1 for r in dungeon_rows if int(r["room"]) in entry_covered_set),
        "candidates_with_any_conservative_predicate": sum(
            1 for r in dungeon_rows if int(r["room"]) in covered_set),
        "candidates_without_any_conservative_predicate": sum(
            1 for r in dungeon_rows if int(r["room"]) not in covered_set),
        "reviewed_rescued_rooms_without_conservative_predicate": len(reviewed_uncovered_rooms),
        "reviewed_rescued_candidates_without_conservative_predicate": len(reviewed_uncovered_rows),
        "still_excluded_rooms_without_conservative_predicate": len(still_uncovered_rooms),
        "still_excluded_candidates_without_conservative_predicate": len(still_uncovered_rows),
        "sample_uncovered_rooms": [f"0x{room:03X}" for room in uncovered[:32]],
        "sample_reviewed_rescued_rooms": [
            f"0x{room:03X}" for room in reviewed_uncovered_rooms[:32]],
        "sample_still_excluded_rooms": [
            f"0x{room:03X}" for room in still_uncovered_rooms[:32]],
    }


def build_doc(args) -> dict:
    assets_path = args.assets if args.assets.is_absolute() else REPO / args.assets
    if not assets_path.exists():
        die(f"missing {assets_path}; build/extract assets first")
    assets = read_assets(assets_path)
    constraints = parse_enemy_constraints(SHUFFLE_ENEMIES_C)
    forced_sources = forced_drop_source_set(assets)

    dungeon_rows, dungeon_excluded = collect_dungeon_candidates(
        assets, constraints, forced_sources,
        allow_cannot_key=args.allow_cannot_key,
        allow_flying=args.allow_flying,
    )
    overworld_rows, ow_collisions, overworld_excluded = collect_overworld_candidates(
        assets, constraints,
        allow_cannot_key=args.allow_cannot_key,
        allow_flying=args.allow_flying,
    )

    loc_count = parse_define(LOCATION_IDS_H, "LOC__COUNT")
    capacity = parse_define(RANDO_LOGIC_H, "kRandoLocationCapacity")
    pot_predicates = load_pot_room_predicates(POT_REGISTRY)
    region_only_pot_rooms = load_region_only_pot_rooms(POT_REGISTRY)
    entry_room_predicates = load_entry_room_predicates(ENTRANCE_REGISTRY)
    emitted_registry = load_enemy_check_registry_summary(ENEMY_CHECK_REGISTRY)
    predicate_coverage = dungeon_predicate_coverage(
        dungeon_rows, pot_predicates, region_only_pot_rooms, entry_room_predicates,
        emitted_registry["emitted_dungeon_keys"])
    emitted_dungeon_count = emitted_registry["emitted_dungeon_count"] or len(dungeon_rows)
    emitted_count = emitted_registry["emitted_count"]
    base_without_enemy_registry = loc_count
    if emitted_registry["source_available"] and emitted_count <= loc_count:
        base_without_enemy_registry = loc_count - emitted_count
    enemy_base = max(base_without_enemy_registry, ENEMY_CHECK_BASE_ID)
    mvp_projected = enemy_base + emitted_dungeon_count
    underworld_all_tier_count = emitted_registry["emitted_underworld_all_tier_count"]
    eligible_total = emitted_dungeon_count + underworld_all_tier_count + len(overworld_rows)
    raw_overworld_total = len(overworld_rows) + overworld_excluded["overworld_runtime_identity_collision"]
    projected = enemy_base + eligible_total
    if emitted_registry["source_available"]:
        projected = max(projected, emitted_registry["max_loc_plus_one"])

    doc = {
        "format_version": 1,
        "_generated_by": "assets/scripts/audit_enemy_check_candidates.py (do not hand-edit)",
        "source": {
            "assets": assets_path.name,
            "sha256": hashlib.sha256(assets_path.read_bytes()).hexdigest(),
            "constraint_table": SHUFFLE_ENEMIES_C.relative_to(REPO).as_posix(),
        },
        "policy": {
            "eligible_source_type": [
                "ESF_RANDOMIZABLE",
                "ESF_KILLABLE",
            ],
            "excluded_source_type_flags": [
                flag for flag, allowed in (
                    ("ESF_CANNOT_KEY", args.allow_cannot_key),
                    ("ESF_FLYING", args.allow_flying),
                ) if not allowed
            ],
            "excluded_sources": [
                "existing forced-key enemy-drop checks",
                "overlords/control markers",
                "overworld candidates whose runtime (stage, area, block) identity collides within one active sprite list",
            ],
            "runtime_identity": {
                "dungeon": "(dungeon_room_index, sprite_N source slot)",
                "overworld": "(sram_progress_indicator-derived active stage, overworld_area_index, source slot, sprite_N_word block)",
            },
        },
        "summary": {
            "current_loc_count": loc_count,
            "current_capacity": capacity,
            "current_headroom": capacity - loc_count,
            "recommended_mvp": {
                "scope": "dungeon_only",
                "eligible_candidates": emitted_dungeon_count,
                "registry": ENEMY_CHECK_REGISTRY.relative_to(REPO).as_posix(),
                "registry_available": emitted_registry["source_available"],
                "out_of_scope_no_key_depth": emitted_registry["out_of_scope_no_key_depth_count"],
                "projected_loc_count": mvp_projected,
                "fits_current_capacity": mvp_projected <= capacity,
            },
            "raw_candidates": {
                "dungeon": len(dungeon_rows),
                "overworld_before_identity_collision_filter": raw_overworld_total,
                "total_before_identity_collision_filter": len(dungeon_rows) + raw_overworld_total,
            },
            "eligible_candidates": {
                "dungeon": emitted_dungeon_count,
                "underworld_all_tier": underworld_all_tier_count,
                "overworld": len(overworld_rows),
                "total": eligible_total,
            },
            "registry_emitted": {
                "available": emitted_registry["source_available"],
                "candidate_count": emitted_registry["candidate_count"],
                "total": emitted_registry["emitted_count"],
                "dungeon": emitted_dungeon_count,
                "underworld_all_tier": underworld_all_tier_count,
                "overworld": emitted_registry["emitted_overworld_count"],
                "boss": emitted_registry["emitted_boss_count"],
                "scripted_spawn": emitted_registry["emitted_scripted_spawn_count"],
            },
            "projected_loc_count": projected,
            "fits_current_capacity": projected <= capacity,
            "minimum_capacity_for_projected_set": projected,
            "dungeon_room_predicate_coverage": predicate_coverage,
        },
        "excluded_counts": {
            "dungeon": dict(sorted(dungeon_excluded.items())),
            "overworld": dict(sorted(overworld_excluded.items())),
        },
        "overworld_runtime_identity_collisions": {
            "count": len(ow_collisions),
            "groups": ow_collisions[:args.max_collision_groups],
        },
    }
    if args.include_rows:
        doc["eligible_rows"] = {
            "dungeon": dungeon_rows,
            "overworld": overworld_rows,
        }
    return doc


def render_yaml(doc: dict) -> str:
    return yaml.safe_dump(doc, sort_keys=False, allow_unicode=False, width=1000) + "\n"


def write_lf(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="\n") as f:
        f.write(text)


def check_fresh(path: Path, expected: str) -> int:
    have = path.read_text(encoding="utf-8", errors="replace") if path.exists() else ""
    if have == expected:
        print(f"audit_enemy_check_candidates: OK {path} is fresh", file=sys.stderr)
        return 0
    print(f"audit_enemy_check_candidates: ERROR: {path} is stale; run "
          f"`python assets/scripts/audit_enemy_check_candidates.py`.",
          file=sys.stderr)
    for line in difflib.unified_diff(
            have.splitlines(), expected.splitlines(),
            fromfile=str(path),
            tofile=f"{path} (regenerated)",
            lineterm=""):
        print(line, file=sys.stderr)
    return 1


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--assets", type=Path, default=DEFAULT_ASSETS,
                    help="Path to zelda3_assets.dat")
    ap.add_argument("--out", type=Path, default=DEFAULT_OUT,
                    help="Output YAML path")
    ap.add_argument("--include-rows", action="store_true",
                    help="Include every eligible candidate row in the YAML")
    ap.add_argument("--allow-cannot-key", action="store_true",
                    help="Do not exclude ESF_CANNOT_KEY classes")
    ap.add_argument("--allow-flying", action="store_true",
                    help="Do not exclude ESF_FLYING classes")
    ap.add_argument("--max-collision-groups", type=int, default=50,
                    help="Maximum overworld identity collision groups to print")
    ap.add_argument("--check", action="store_true",
                    help="Fail if the output is stale instead of writing it")
    args = ap.parse_args(argv)

    out_path = args.out if args.out.is_absolute() else REPO / args.out
    doc = build_doc(args)
    text = render_yaml(doc)
    summary = doc["summary"]
    print(
        "audit_enemy_check_candidates: "
        f"{summary['eligible_candidates']['dungeon']} dungeon + "
        f"{summary['eligible_candidates']['underworld_all_tier']} underworld all-tier + "
        f"{summary['eligible_candidates']['overworld']} overworld raw-audit candidates "
        f"({summary['eligible_candidates']['total']} total); "
        f"registry emitted {summary['registry_emitted']['total']} checks; "
        f"MVP LOC__COUNT {summary['recommended_mvp']['projected_loc_count']} / "
        f"{summary['current_capacity']}; "
        f"projected LOC__COUNT {summary['projected_loc_count']} / "
        f"{summary['current_capacity']}",
        file=sys.stderr,
    )
    if args.check:
        return check_fresh(out_path, text)
    write_lf(out_path, text)
    print(f"audit_enemy_check_candidates: wrote {out_path}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
