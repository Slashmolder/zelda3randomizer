#!/usr/bin/env python3
"""gen_bonk_tables.py — enumerate the placed overworld BONK-ITEM sprites
(add-rando-bonk-sanity) and emit the gitignored registry the rando codegen
consumes (assets/rando/bonk.gen.yaml).

Population: sprite types 0x79 (dormant bee hive) and 0xAC (apple tree) —
exactly the types SpritePrep_OverworldBonkItem preps dormant; woken by
Entity_ApplyRumbleToSprites (Boots dash or Quake rumble). The classic bonk-prize
ABSORBABLE family (0xD8-0xE3, incl. bonk fairies) is deliberately out of
scope for v1 (design.md D8).

Ground truth is the packed OW sprite asset the engine loads
(kOverworldSprites / kOverworldSpriteOffs, 3 stages x 144 areas — the
sprite-phase axis selected by sram_progress_indicator via
GetOverworldSpritePtr). An object registers only if its (area, block, type)
is IDENTICAL across stages 1 AND 2 — the collectable window (stage 0 is the
pre-rescue escape phase, logic-unreachable for bonk checks; the strict
three-stage rule measured ZERO survivors). Stage-variant objects within the
window are excluded (the missable-check rule: an object absent post-Agahnim
would become mid-game-uncollectable). `block` is the
engine's own position key: the r5/r6 packing Overworld_LoadSprites computes
from the record bytes and stores sprites under in
sprite_where_in_overworld[] — the same key the runtime hook recovers from
sprite_N_word[k] at wake time, and the same key the OW enemy checks use.

IDs: base 2816 (0xB00), inside the free 2634..3071 gap BELOW the terrain
family — terrain (3072..) must remain the id-sorted registry's contiguous
SUFFIX (the kRandoNonTerrainLocationsCount reachability fast-path bounds
there when both terrain axes are off; a bonk row after terrain would be
skipped by that bound). The plan's provisional 7168 base was corrected at
implementation for this invariant.

Bounds tripwire: grouped count must land in [1, 200] (there is deliberately
NO comparison against ALTTPR's dead setOverworldBonkPrizes table — those 62
addresses target the absorbable family, not this population).
"""

from __future__ import annotations

import struct
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
ASSETS_DAT = REPO / "zelda3_assets.dat"
OUT_YAML = REPO / "assets" / "rando" / "bonk.gen.yaml"
OVERRIDES_YAML = REPO / "assets" / "rando" / "bonk_logic_overrides.yaml"

sys.path.insert(0, str(Path(__file__).resolve().parent))
from gen_enemy_check_tables import OVERWORLD_REGIONS  # noqa: E402

BONK_TYPES = {0x79: "Bee", 0xAC: "Apple"}
ID_BASE = 2816   # 0xB00 — below terrain's 3072 base (suffix invariant above)
ID_MAX = 3071    # stay inside the free gap
STAGES = 3
AREAS = 144


def die(msg: str) -> None:
    sys.exit(f"gen_bonk_tables: ERROR: {msg}")


def read_assets(path: Path) -> dict[str, bytes]:
    data = path.read_bytes()
    if len(data) < 88:
        die(f"{path} is too small to be zelda3_assets.dat")
    if not (data.startswith(b"Zelda3_v0") or data.startswith(b"Zelda3_v1")):
        die(f"{path} does not start with a known Zelda3 asset signature")
    count, key_len = struct.unpack_from("<II", data, 80)
    sizes_off = 88
    keys_off = sizes_off + 4 * count
    keys_end = keys_off + key_len
    if keys_end > len(data):
        die(f"{path} has a truncated asset key table")
    sizes = list(struct.unpack_from("<" + "I" * count, data, sizes_off))
    keys = [k.decode("utf-8") for k in data[keys_off:keys_end].split(b"\0") if k]
    if len(keys) != count:
        die(f"{path} key count mismatch")
    off = keys_end
    out: dict[str, bytes] = {}
    for name, size in zip(keys, sizes):
        off = (off + 3) & ~3
        end = off + size
        if end > len(data):
            die(f"{path} asset {name} is truncated")
        out[name] = data[off:end]
        off = end
    return out


def parse_u16le_array(b: bytes) -> list[int]:
    return list(struct.unpack_from("<" + "H" * (len(b) // 2), b))


def record_block(y: int, x: int) -> int:
    # Overworld_LoadSprites (src/sprite.c): r2=(y>>4)<<2; r6=(x>>4)+r2;
    # r5=(x&0xf)|(uint8)(y<<4); block=r5|r6<<8.
    r2 = (y >> 4) << 2
    r6 = (x >> 4) + r2
    r5 = (x & 0xF) | ((y << 4) & 0xF0)
    return r5 | (r6 << 8)


def fnv1a32(items) -> int:
    h = 0x811C9DC5
    for it in items:
        for v in it:
            for byte in int(v).to_bytes(4, "little"):
                h = ((h ^ byte) * 0x01000193) & 0xFFFFFFFF
    return h


def load_overrides() -> dict:
    if not OVERRIDES_YAML.exists():
        return {}
    import yaml
    d = yaml.safe_load(OVERRIDES_YAML.read_text(encoding="utf-8")) or {}
    return d.get("areas", {}) or {}


def main() -> int:
    if not ASSETS_DAT.exists():
        die(f"missing {ASSETS_DAT}; extract assets first")
    assets = read_assets(ASSETS_DAT)
    sprites = assets["kOverworldSprites"]
    offs = parse_u16le_array(assets["kOverworldSpriteOffs"])
    if len(offs) != STAGES * AREAS:
        die(f"kOverworldSpriteOffs has {len(offs)} entries, expected {STAGES * AREAS}")

    # per-stage sets of (area, block, type)
    per_stage: list[set] = [set() for _ in range(STAGES)]
    per_stage_counts = [0, 0, 0]
    for idx, off in enumerate(offs):
        stage, area = idx // AREAS, idx % AREAS
        pos = off
        while pos + 2 < len(sprites):
            y = sprites[pos]
            if y == 0xFF:
                break
            x, typ = sprites[pos + 1], sprites[pos + 2]
            pos += 3
            if typ not in BONK_TYPES:
                continue
            key = (area, record_block(y, x), typ)
            if key in per_stage[stage]:
                die(f"duplicate bonk object {key} within stage {stage}")
            per_stage[stage].add(key)
            per_stage_counts[stage] += 1

    # Identity window = stages 1 AND 2 (post-escape and post-Agahnim).
    # An object in stage 1 but not 2 would be MISSABLE post-Agahnim ->
    # excluded; stage-2-only objects are excluded conservatively. The
    # original three-stage rule measured ZERO survivors — stage tables
    # genuinely differ — so the rule was refined to the collectable window
    # (as-built note in the change's design.md).
    #
    # Stage-0 presence GATES the predicate, it does not define identity:
    # the original assumption "stage 0 (pre-rescue Standard) is
    # logic-unreachable for every bonk check" was FALSE — Hyrule Castle
    # Escape is start-reachable in Standard, so a stage-0-ABSENT row there
    # (the HCE apple tree spawns only post-rescue) was logic-reachable
    # before its sprite could exist; Lamp placed on it = a deterministic
    # Zelda-rescue hardlock (external review P1, reproducer: Standard
    # bonk_shuffle=all, Boots@Uncle + Lamp@2820, seed 1). Rows absent from
    # stage 0 now carry HAS_ITEM(RescuedZelda) — granted at start in
    # Open/Retro/Inverted, earned via the escort in Standard — matching
    # exactly when the sprite can spawn.
    grouped = per_stage[1] & per_stage[2]
    excluded = (per_stage[1] | per_stage[2]) - grouped
    n = len(grouped)
    print(f"gen_bonk_tables: per-stage counts {per_stage_counts}, "
          f"stage-1+2-identical {n}, stage-variant excluded {len(excluded)}, "
          f"stage-0-only ignored {len(per_stage[0] - per_stage[1] - per_stage[2])}")
    for key in sorted(excluded):
        print(f"  excluded (stage-variant): area 0x{key[0]:02X} block 0x{key[1]:04X} type 0x{key[2]:02X}")
    if not (1 <= n <= 200):
        die(f"grouped count {n} outside the [1,200] bounds tripwire")
    if ID_BASE + n - 1 > ID_MAX:
        die(f"id range overflows the below-terrain gap ({ID_BASE}..{ID_MAX})")

    overrides = load_overrides()
    rows = []
    for i, (area, block, typ) in enumerate(sorted(grouped)):
        ov = overrides.get(area, {}) if isinstance(overrides.get(area, {}), dict) else {}
        if ov.get("exclude"):
            continue
        region = ov.get("region") or OVERWORLD_REGIONS.get(area)
        if not region:
            die(f"area 0x{area:02X} has no region in OVERWORLD_REGIONS or "
                f"bonk_logic_overrides.yaml — review required")
        world_gate = "TerrainWorldGateDW()" if 0x40 <= area < 0x80 else "TerrainWorldGateLW()"
        pred = f"({world_gate}) AND CanBonk()"
        if (area, block, typ) not in per_stage[0]:
            # Sprite absent pre-rescue: gate on the escort (see the stage-0
            # comment above). Stage-0-present rows spawn from the start and
            # need no gate.
            pred += " AND HAS_ITEM(RescuedZelda)"
        rows.append({
            "id": ID_BASE + i,
            "name": f"{region} Bonk{BONK_TYPES[typ]} A{area:02X} B{block:04X}",
            "area": area,
            "block": block,
            "type": typ,
            "region": region,
            "can_reach": pred,
        })

    digest = fnv1a32((r["area"], r["block"], r["type"], r["id"]) for r in rows)
    with OUT_YAML.open("w", encoding="utf-8", newline="\n") as f:
        f.write("# GENERATED by assets/scripts/gen_bonk_tables.py — do not edit.\n")
        f.write("# Placed OW bonk-item sprites (0x79 bee hive / 0xAC apple tree)\n")
        f.write("# whose (area, block, type) is identical across stages 1 AND 2 —\n")
        f.write("# the collectable window; the strict three-stage rule measured\n")
        f.write("# ZERO survivors and was refined (see the script docstring, which\n")
        f.write("# also covers the id-gap + suffix-invariant rationale).\n")
        f.write("# content_stamp is this script's own regen fingerprint; it is NOT\n")
        f.write("# kRandoBonkRegistryDigest (that guard value is computed over\n")
        f.write("# (area, block, loc_id) by rando_logic_gen.py's _bonk_registry_digest\n")
        f.write("# and nothing parses this header).\n")
        f.write(f"content_stamp: 0x{digest:08X}\n")
        f.write(f"count: {len(rows)}\n")
        f.write("locations:\n")
        for r in rows:
            f.write(f"- {{id: {r['id']}, name: '{r['name']}', area: 0x{r['area']:02X}, "
                    f"block: 0x{r['block']:04X}, type: 0x{r['type']:02X}, "
                    f"region: {r['region']}, can_reach: '{r['can_reach']}'}}\n")
    print(f"gen_bonk_tables: wrote {len(rows)} bonk locations "
          f"(ids {ID_BASE}..{ID_BASE + len(rows) - 1}, digest 0x{digest:08X}) -> {OUT_YAML}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
