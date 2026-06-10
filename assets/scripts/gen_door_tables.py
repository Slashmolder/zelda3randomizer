#!/usr/bin/env python3
"""gen_door_tables.py — door-shuffle static topology codegen.

Builds the ALttPDoorRandomizer reference's vanilla world IN-PROCESS (imports the
reference's own modules and runs its creators), harvests the dungeon door/region/
rule data, and emits gitignored C tables consumed by src/rando/door_runtime.c,
shuffle_doors.c, door_keylogic.c and the logic oracle.

Reference: a checkout of ALttPDoorRandomizer (MIT) — see --ref. We execute its
declaration code rather than text-parsing it, so the harvest cannot drift from
the reference's own semantics. Rules are captured by monkeypatching the rule
primitives (set_rule / add_rule / ...) to record each lambda's SOURCE at call
time — composition (add_lamp_requirement etc.) is recorded as an ordered op
list per spot, which sidesteps closure opacity.

Outputs (all gitignored; build fails without them via check_door_tables.py):
  src/rando/door_tables.gen.h / .c
  assets/rando/door_tables.gen.json   (debug/inspection intermediate)

Committed inputs:
  assets/rando/door_registry.yaml       (frozen door-stub ids; created by --init-registry)
  assets/rando/door_portals.yaml        (per-portal access gates, hand-curated)
  assets/rando/door_rules_overrides.yaml (hand-curated rules the extractor can't parse)

Usage:
  python assets/scripts/gen_door_tables.py --ref C:/src/ALttPDoorRandomizer [--dump-stats]
"""

import argparse
import inspect
import json
import os
import random
import re
import sys

REPO = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))

# ---------------------------------------------------------------------------
# Phase A: build the reference vanilla world and harvest.
# ---------------------------------------------------------------------------

# The dungeons the fork shuffles + their fork dungeon ids (ALTTPR order used by
# the rando: see src/rando/ location/dungeon tables). HC + Swamp are pinned at
# MVP but still harvested (the catalog is complete; pins are a generation-time
# policy).
REF_DUNGEONS = [
    # (reference name, fork enum name)
    ('Hyrule Castle', 'HyruleCastle'),
    ('Eastern Palace', 'EasternPalace'),
    ('Desert Palace', 'DesertPalace'),
    ('Tower of Hera', 'TowerOfHera'),
    ('Agahnims Tower', 'CastleTower'),
    ('Palace of Darkness', 'PalaceOfDarkness'),
    ('Swamp Palace', 'SwampPalace'),
    ('Skull Woods', 'SkullWoods'),
    ('Thieves Town', 'ThievesTown'),
    ('Ice Palace', 'IcePalace'),
    ('Misery Mire', 'MiseryMire'),
    ('Turtle Rock', 'TurtleRock'),
    ('Ganons Tower', 'GanonsTower'),
]


class RuleRecorder:
    """Records (spot_kind, spot_name, op, rule_descriptor) for every rule call.

    A rule descriptor is {'src': <lambda body text>, 'closure': {name: descr}} where
    closure values are reprs for plain values, region/location names for objects, and
    nested descriptors for closed-over functions (or_rule/and_rule combinators etc.).
    """

    def __init__(self):
        self.entries = {}  # (kind, name) -> list of (op, descriptor)

    def describe(self, rule, depth=0):
        if depth > 16:
            return {'src': '<depth-limit>', 'closure': {}}
        try:
            src = normalize_lambda(inspect.getsource(rule))
        except (OSError, TypeError):
            src = '<unrecoverable>'
        closure = {}
        code = getattr(rule, '__code__', None)
        cells = getattr(rule, '__closure__', None)
        if code is not None and cells:
            for name, cell in zip(code.co_freevars, cells):
                try:
                    val = cell.cell_contents
                except ValueError:
                    closure[name] = '<empty-cell>'
                    continue
                if callable(val) and getattr(val, '__code__', None) is not None:
                    closure[name] = self.describe(val, depth + 1)
                elif hasattr(val, 'name') and not isinstance(val, type):
                    closure[name] = f'<obj:{type(val).__name__}:{val.name}>'
                elif isinstance(val, (bool, int, str, float)) or val is None:
                    closure[name] = repr(val)
                else:
                    closure[name] = f'<{type(val).__name__}>'
        return {'src': src, 'closure': closure}

    def describe_rule_object(self, rule, depth=0):
        """Structured serialization of a source/logic/Rule.py Rule AST."""
        if depth > 12:
            return {'rule_type': '<depth-limit>'}
        principal = rule.principal
        if hasattr(principal, 'name') and not isinstance(principal, (str, type)):
            principal = f'<obj:{type(principal).__name__}:{principal.name}>'
        elif hasattr(principal, 'defeat_rule'):
            principal = f'<defeat:{principal.defeat_rule}>'
        elif not isinstance(principal, (bool, int, str, float, tuple, type(None))):
            principal = f'<{type(principal).__name__}>'
        def plain(v):
            if isinstance(v, (bool, int, str, float, type(None))):
                return v
            if isinstance(v, (list, tuple)):
                return [plain(x) for x in v]
            if hasattr(v, 'name'):
                return f'<obj:{type(v).__name__}:{v.name}>'
            return f'<{type(v).__name__}>'

        out = {
            'rule_type': rule.rule_type.name,
            'principal': plain(principal),
            'count': rule.count,
            'barrier': rule.barrier.name if rule.barrier is not None else None,
            'flag': plain(rule.flag),
            'locations': [plain(l) for l in rule.locations],
        }
        if rule.sub_rules:
            out['sub_rules'] = [self.describe_rule_object(r, depth + 1) for r in rule.sub_rules]
        return out

    def record(self, spot, op, rule):
        kind = type(spot).__name__  # Entrance / Location
        key = (kind, spot.name)
        if hasattr(rule, 'rule_type') and hasattr(rule, 'sub_rules'):
            self.entries.setdefault(key, []).append((op, {'ast': self.describe_rule_object(rule)}))
        else:
            self.entries.setdefault(key, []).append((op, self.describe(rule)))


def normalize_lambda(src):
    """Strip the call-site wrapper, keep the lambda body text."""
    src = ' '.join(src.split())
    i = src.find('lambda state:')
    if i < 0:
        return src
    body = src[i + len('lambda state:'):]
    # Trim a trailing call-paren run that belonged to the enclosing call.
    depth = 0
    out = []
    for ch in body:
        if ch in '([{':
            depth += 1
        elif ch in ')]}':
            if depth == 0:
                break
            depth -= 1
        out.append(ch)
    return ''.join(out).strip().rstrip(',').strip()


def build_reference_world(ref_path):
    sys.path.insert(0, ref_path)
    # The reference imports relative to its own root; run from there.
    os.chdir(ref_path)

    from CLI import parse_cli
    from BaseClasses import World
    import Rules

    args = parse_cli([])
    P = 1
    random.seed(0)
    world = World(args.multi, args.shuffle, args.door_shuffle, args.logic, args.mode,
                  args.swords, args.difficulty, args.item_functionality, args.timer,
                  args.progressive, args.goal, args.algorithm, args.accessibility,
                  args.shuffleganon, args.custom, args.customitemarray, args.hints,
                  'none')
    # Mirror Main.py's args->world copies for everything the creators/rules read.
    PASSTHRU = ['boots_hint', 'remote_items', 'mapshuffle', 'compassshuffle', 'keyshuffle',
                'bigkeyshuffle', 'bombbag', 'flute_mode', 'bow_mode', 'door_type_mode',
                'trap_door_mode', 'key_logic_algorithm', 'decoupledoors', 'door_self_loops',
                'experimental', 'dungeon_counters', 'shopsanity', 'dropshuffle', 'pottery',
                'mixed_travel', 'standardize_palettes', 'shufflelinks', 'shuffletavern',
                'skullwoods', 'linked_drops', 'pseudoboots', 'mirrorscroll', 'overworld_map',
                'take_any', 'restrict_boss_items', 'collection_rate', 'colorizepots',
                'aga_randomness', 'money_balance']
    for name in PASSTHRU:
        setattr(world, name, getattr(args, name).copy())
    world.potshuffle = args.shufflepots.copy()
    world.open_pyramid = args.openpyramid.copy()
    world.boss_shuffle = args.shufflebosses.copy()
    world.enemy_shuffle = args.shuffleenemies.copy()
    world.enemy_health = args.enemy_health.copy()
    world.enemy_damage = args.enemy_damage.copy()
    world.any_enemy_logic = args.any_enemy_logic.copy()
    world.beemizer = {P: str(args.beemizer[P])}
    world.crystals_needed_for_ganon = {P: 7}
    world.crystals_needed_for_gt = {P: 7}
    world.crystals_ganon_orig = args.crystals_ganon.copy()
    world.crystals_gt_orig = args.crystals_gt.copy()
    world.treasure_hunt_count = {}
    world.treasure_hunt_total = {}
    world.intensity = {P: 1}
    world.customizer = None
    world.owShuffle = getattr(args, 'ow_shuffle', {P: 'vanilla'}).copy() \
        if hasattr(args, 'ow_shuffle') else {P: 'vanilla'}
    world.seed = 0

    from Regions import create_regions, create_dungeon_regions, create_shops
    from Doors import create_doors
    from RoomData import create_rooms
    from Dungeons import create_dungeons
    import DoorShuffle as DS

    create_regions(world, P)
    create_dungeon_regions(world, P)
    create_shops(world, P)
    create_doors(world, P)
    create_rooms(world, P)
    create_dungeons(world, P)

    # Rules.drop_rules/pot_rules walk the underworld enemy table (no-op while
    # dropshuffle/pottery are 'none', but the table must exist).
    from source.rom.DataTables import init_data_tables
    from source.enemizer.DamageTables import DamageTable
    world.damage_table[P] = DamageTable()
    world.data_tables[P] = init_data_tables(world, P)

    # Vanilla door wiring = link_doors_prep minus vanilla_key_logic (which needs
    # linked overworld portals the harvest doesn't require). Intensity-1 set.
    for exit_name, region_name in DS.logical_connections:
        DS.connect_simple_door(world, exit_name, region_name, P)
    for a, b in DS.interior_doors:
        DS.connect_interior_doors(a, b, world, P)
    for exit_name, region_name in DS.falldown_pits:
        DS.connect_simple_door(world, exit_name, region_name, P)
    for exit_name, region_name in DS.dungeon_warps:
        DS.connect_simple_door(world, exit_name, region_name, P)
    for a, b in DS.open_edges:
        DS.connect_two_way(world, a, b, P)
    for a, b in DS.straight_staircases:
        DS.connect_two_way(world, a, b, P)
    for a, b in DS.ladders:
        DS.connect_two_way(world, a, b, P)
    for a, b in DS.default_door_connections:
        DS.connect_two_way(world, a, b, P)
    for a, b in DS.default_one_way_connections:
        DS.connect_one_way(world, a, b, P)

    # --- Rule capture: patch the primitives, then run set_rules. -----------
    recorder = RuleRecorder()
    orig_set_rule = Rules.set_rule
    orig_add_rule_new = Rules.add_rule_new

    in_add_rule_new = [False]

    def patched_set_rule(spot, rule):
        recorder.record(spot, 'set', rule)
        orig_set_rule(spot, rule)

    def patched_add_rule_new(spot, rule, combine='and'):
        recorder.record(spot, combine, rule)
        in_add_rule_new[0] = True
        try:
            orig_add_rule_new(spot, rule, combine)  # internally calls add_rule(rule_lambda)
        finally:
            in_add_rule_new[0] = False

    def patched_add_rule(spot, rule, combine='and'):
        if not in_add_rule_new[0]:
            recorder.record(spot, combine, rule)
        # original add_rule body
        old_rule = spot.access_rule
        if combine == 'or':
            spot.access_rule = lambda state: rule(state) or old_rule(state)
        else:
            spot.access_rule = lambda state: rule(state) and old_rule(state)

    Rules.set_rule = patched_set_rule
    Rules.add_rule = patched_add_rule
    Rules.add_rule_new = patched_add_rule_new

    # Overworld-linkage-dependent passes are no-op'd: they need link_overworld/
    # link_entrances (not run) and attach NO dungeon-interior rules. The oracle
    # models interior traversal only — bunny/Pearl gating remains with the
    # fork's existing dungeon-entry edges.
    Rules.set_big_bomb_rules = lambda world, player: None
    Rules.set_inverted_big_bomb_rules = lambda world, player: None
    Rules.set_bunny_rules = lambda world, player, inverted: None

    world.key_logic[P] = {}  # vanilla_key_logic skipped; rules pass tolerates empty
    Rules.set_rules(world, P)

    Rules.set_rule = orig_set_rule
    Rules.add_rule_new = orig_add_rule_new

    return world, P, recorder


def harvest(world, P, recorder):
    """Normalize the reference world into a plain-dict model."""
    from BaseClasses import DoorType, RegionType, Direction, CrystalBarrier

    dungeon_by_region = {}
    dungeons = []
    for di, (ref_name, fork_name) in enumerate(REF_DUNGEONS):
        d = world.get_dungeon(ref_name, P)
        region_names = sorted(r if isinstance(r, str) else r.name for r in d.regions)
        dungeons.append({
            'index': di,
            'ref_name': ref_name,
            'fork_name': fork_name,
            'alttpr_id': d.dungeon_id if hasattr(d, 'dungeon_id') else None,
            'chest_small_keys': len(d.small_keys),
            'has_big_key': d.big_key is not None,
            'regions': region_names,
        })
        for rn in region_names:
            dungeon_by_region[rn] = di

    # Regions (dungeon type only, restricted to our 13 dungeons' membership).
    regions = []
    region_index = {}
    for r in sorted(world.regions, key=lambda r: r.name):
        if r.type != RegionType.Dungeon:
            continue
        if r.name not in dungeon_by_region:
            continue  # e.g. unreferenced helper regions
        region_index[r.name] = len(regions)
        regions.append({
            'name': r.name,
            'dungeon': dungeon_by_region[r.name],
            'locations': [l.name for l in r.locations],
        })

    # Doors.
    DT = DoorType
    doors = []
    door_index = {}
    for d in sorted(world.doors, key=lambda d: d.name):
        ent_region = d.entrance.parent_region.name if d.entrance is not None else None
        if ent_region is not None and ent_region not in region_index:
            continue  # door outside our dungeons (shouldn't happen)
        door_index[d.name] = len(doors)
        doors.append({
            'name': d.name,
            'type': d.type.name,
            'direction': d.direction.name if d.direction is not None else None,
            'room': d.roomIndex,
            'door_index': d.doorIndex,
            'layer': d.layer,
            'pos': d.doorListPos,
            'toggle': bool(d.toggle),
            'trap_flag': d.trapFlag,
            'blocked': bool(d.blocked),
            'trapped': bool(d.trapped),
            'stonewall': bool(d.stonewall),
            'small_key': bool(d.smallKey),
            'big_key': bool(d.bigKey),
            'ugly': bool(d.ugly),
            'dead_end': bool(d.deadEnd),
            'passage': bool(d.passage),
            'quadrant': d.quadrant,
            'shift_x': d.shiftX,
            'shift_y': d.shiftY,
            'zero_hz_cam': bool(d.zeroHzCam),
            'zero_vt_cam': bool(d.zeroVtCam),
            'crystal': d.crystal.name if d.crystal is not None else 'Null',
            'req_event': d.req_event,
            'entrance_flag': bool(d.entranceFlag),
            'portal_able': bool(d.portalAble),
            'region': ent_region,
            'dungeon': dungeon_by_region.get(ent_region),
            'dest_region': (d.dest.name if d.dest is not None and hasattr(d.dest, 'entrances')
                            else None),
            'dest_door': (d.dest.name if d.dest is not None and hasattr(d.dest, 'roomIndex')
                          else None),
        })

    # Region edges: every entrance of a dungeon region with a connection.
    edges = []
    for rname in sorted(region_index):
        r = world.get_region(rname, P)
        for ent in r.exits:
            if ent.connected_region is None:
                continue
            if ent.connected_region.name not in region_index:
                continue
            key = ('Entrance', ent.name)
            rules = recorder.entries.get(key, [])
            edges.append({
                'name': ent.name,
                'from': rname,
                'to': ent.connected_region.name,
                'door': ent.door.name if getattr(ent, 'door', None) is not None else None,
                'rules': rules,
            })

    # Locations within dungeon regions (+ their captured rules).
    locations = []
    for rname in sorted(region_index):
        r = world.get_region(rname, P)
        for loc in r.locations:
            key = ('Location', loc.name)
            rules = recorder.entries.get(key, [])
            locations.append({
                'name': loc.name,
                'region': rname,
                'forced_item': loc.forced_item.name if getattr(loc, 'forced_item', None) else None,
                'event': bool(getattr(loc, 'event', False)),
                'rules': rules,
            })

    # Rooms (door lists for the kind overlay) + paired doors.
    rooms = []
    for room in sorted(world.rooms, key=lambda r: r.index):
        rooms.append({
            'index': room.index,
            'door_list': [(pos.name if hasattr(pos, 'name') else int(pos),
                           kind.name if hasattr(kind, 'name') else int(kind))
                          for (pos, kind) in room.doorList],
        })
    paired = [{'a': pd.door_a, 'b': pd.door_b, 'pair': bool(pd.pair)}
              for pd in world.paired_doors[P]]

    # Key drop data (drop/pot keys per location name -> our drop-key table).
    import Regions as RegionsMod
    key_drops = {}
    if hasattr(RegionsMod, 'key_drop_data'):
        key_drops = {k: list(v) if isinstance(v, (list, tuple)) else v
                     for k, v in RegionsMod.key_drop_data.items()}

    return {
        'dungeons': dungeons,
        'regions': regions,
        'doors': doors,
        'edges': edges,
        'locations': locations,
        'rooms': rooms,
        'paired_doors': paired,
        'key_drop_data': key_drops,
    }


def dump_stats(model):
    from collections import Counter
    doors = model['doors']
    print(f"dungeons: {len(model['dungeons'])}")
    print(f"regions:  {len(model['regions'])}")
    print(f"doors:    {len(doors)}")
    print('  by type:', dict(Counter(d['type'] for d in doors)))
    print(f"edges:    {len(model['edges'])}")
    ruled = [e for e in model['edges'] if e['rules']]
    print(f"  with rules: {len(ruled)}")
    print(f"locations: {len(model['locations'])}")
    lruled = [l for l in model['locations'] if l['rules']]
    print(f"  with rules: {len(lruled)}")
    print(f"rooms: {len(model['rooms'])}, paired_doors: {len(model['paired_doors'])}")
    print(f"key_drop_data entries: {len(model['key_drop_data'])}")
    # Distinct rule bodies, most common first — drives the vocabulary compiler.
    bodies = Counter()
    unrec = []

    def walk(descr, spot):
        if 'ast' in descr:
            bodies[f"AST:{descr['ast']['rule_type']}"] += 1
            return
        bodies[descr['src']] += 1
        if descr['src'] == '<unrecoverable>':
            unrec.append((spot, descr))
        for v in descr['closure'].values():
            if isinstance(v, dict):
                walk(v, spot)

    for e in model['edges'] + model['locations']:
        for op, descr in e['rules']:
            walk(descr, e['name'])
    print(f"distinct rule bodies: {len(bodies)}")
    for src, n in bodies.most_common(40):
        print(f"  {n:4d}  {src[:140]}")
    print(f"unrecoverable: {len(unrec)}")
    for spot, descr in unrec[:15]:
        print(f"  {spot}: closure={list(descr['closure'].keys())}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--ref', default=os.environ.get('DOOR_RANDO_REF',
                    os.path.join(REPO, '..', 'ALttPDoorRandomizer')))
    ap.add_argument('--dump-stats', action='store_true')
    ap.add_argument('--out-json', default=os.path.join(REPO, 'assets', 'rando',
                                                       'door_tables.gen.json'))
    args = ap.parse_args()

    ref = os.path.abspath(args.ref)
    if not os.path.isfile(os.path.join(ref, 'DoorShuffle.py')):
        print(f'gen_door_tables: reference not found at {ref}', file=sys.stderr)
        return 2

    world, P, recorder = build_reference_world(ref)
    model = harvest(world, P, recorder)

    os.makedirs(os.path.dirname(args.out_json), exist_ok=True)
    with open(args.out_json, 'w', newline='\n') as f:
        json.dump(model, f, indent=1, sort_keys=True)
    print(f'gen_door_tables: wrote {args.out_json}')

    if args.dump_stats:
        dump_stats(model)
    return 0


if __name__ == '__main__':
    sys.exit(main())
