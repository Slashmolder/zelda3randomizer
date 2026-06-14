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

Outputs — COMMITTED reference-derived artifacts (CI and fresh clones lack the
reference checkout, so they cannot regenerate; check_door_tables.py guards
count/registry consistency):
  src/rando/door_tables.gen.h / .c
  assets/rando/door_predicates.gen.json (vm-pred manifest for rando_logic_gen.py)
  assets/rando/door_tables.gen.json     (debug/inspection intermediate; gitignored)

Committed inputs:
  assets/rando/door_registry.yaml       (frozen door-stub ids; created by --init-registry)
  assets/rando/door_portals.yaml        (per-portal access gates, hand-curated)
(The translator currently parses every harvested rule — 0 errors — so no
hand-curated overrides file exists; if a future reference update introduces an
unparseable rule, it ships as IMPASSABLE until curated.)

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
    """Strip the call-site wrapper, keep the lambda/def body text."""
    src = ' '.join(src.split())
    # `def name(state): return BODY` rules (e.g. hidden_pits_rule)
    m = re.match(r'def \w+\(state\): return (.*)$', src)
    if m:
        return m.group(1).strip()
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
    # Spiral staircases are intensity-1 pool stubs exactly like
    # default_door_connections (DoorShuffle.create_door_spoiler shuffles
    # [Normal, SpiralStairs] at intensity 1); wire their vanilla pairings so
    # the harvest captures the vanilla edges + partners for the pool.
    for a, b in DS.spiral_staircases:
        DS.connect_two_way(world, a, b, P)

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
            'crystal_switch': bool(getattr(r, 'crystal_switch', False)),
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

    # Wiring provenance: which pre-connect list wired each door (classifies the
    # shuffle pool: default/one_way-wired Normal+Spiral doors are the
    # intensity-1 stubs; everything else is static).
    import DoorShuffle as DS
    wiring = {}
    for a, b in DS.default_door_connections:
        wiring[a] = wiring[b] = 'default'
    for a, b in DS.default_one_way_connections:
        wiring.setdefault(a, 'one_way')
        wiring.setdefault(b, 'one_way')
    for a, b in DS.spiral_staircases:
        wiring[a] = wiring[b] = 'spiral'
    for a, b in DS.interior_doors:
        wiring[a] = wiring[b] = 'interior'
    for a, _ in DS.logical_connections:
        wiring[a] = 'logical'
    for a, _ in DS.falldown_pits:
        wiring[a] = 'falldown'
    for a, _ in DS.dungeon_warps:
        wiring[a] = 'warp'
    for a, b in DS.open_edges:
        wiring[a] = wiring[b] = 'open'
    for a, b in DS.straight_staircases:
        wiring[a] = wiring[b] = 'straight'
    for a, b in DS.ladders:
        wiring[a] = wiring[b] = 'ladder'
    for d in doors:
        d['wiring'] = wiring.get(d['name'])
    vanilla_pairs = {
        'two_way': list(DS.default_door_connections) + list(DS.spiral_staircases),
        'one_way': list(DS.default_one_way_connections),
    }

    # Stitcher-side static data (required paths, portals, dead-end allowances).
    from source.dungeon import DungeonStitcher as StitchMod
    from BaseClasses import flooded_keys as flooded_keys_map
    import DungeonGenerator as DGMod
    stitcher = {
        'flooded_keys': dict(flooded_keys_map),
        'boss_path_checks': list(StitchMod.boss_path_checks),
        'drop_path_checks': list(StitchMod.drop_path_checks),
        'dungeon_boss_sectors': {k: list(v) for k, v in DGMod.dungeon_boss_sectors.items()},
        'default_dungeon_entrances': {k: list(v) for k, v in DGMod.default_dungeon_entrances.items()},
        'drop_entrances': {k: list(v) for k, v in DGMod.drop_entrances.items()},
        'dungeon_dead_end_allowance': dict(DGMod.dungeon_dead_end_allowance),
    }

    # RoomData enum values (= the ROM door-list byte codes the C engine reads).
    from RoomData import DoorKind, Position
    door_kind_values = {m.name: m.value for m in DoorKind}
    position_values = {m.name: m.value for m in Position}

    return {
        'dungeons': dungeons,
        'regions': regions,
        'doors': doors,
        'edges': edges,
        'locations': locations,
        'rooms': rooms,
        'paired_doors': paired,
        'key_drop_data': key_drops,
        'vanilla_pairs': vanilla_pairs,
        'stitcher': stitcher,
        'door_kind_values': door_kind_values,
        'position_values': position_values,
    }


# ---------------------------------------------------------------------------
# Phase B1: rule translation — harvested rule descriptors -> door-rule IR.
#
# IR node forms (tuples):
#   ('true',) ('false',)
#   ('vm', '<fork DSL string>')        — pure item/macro term, compiled into
#                                        kRandoPredicateStream by rando_logic_gen
#   ('event', '<event name>')          — oracle-internal monotone event
#   ('creach', '<region name>', 'blue'|'orange') — per-crystal-state reach query
#   ('and', [..]) ('or', [..]) ('not', x)
#
# Every leaf the translator cannot map is a hard ERROR (listed for curation in
# door_rules_overrides.yaml) — unparsed rules ship as IMPASSABLE, never as free.
# ---------------------------------------------------------------------------

import ast as pyast

# Reference item name -> fork DSL term for plain `state.has('X', player)`.
HAS_ITEM_MAP = {
    'Hammer': 'HAS_ITEM(Hammer)',
    'Hookshot': 'HAS_ITEM(Hookshot)',
    'Lamp': 'HAS_ITEM(Lamp)',
    'Fire Rod': 'HAS_ITEM(FireRod)',
    'Ice Rod': 'HAS_ITEM(IceRod)',
    'Cane of Somaria': 'HAS_ITEM(CaneOfSomaria)',
    'Cane of Byrna': 'HAS_ITEM(CaneOfByrna)',
    'Cape': 'HAS_ITEM(Cape)',
    'Flippers': 'HAS_ITEM(Flippers)',
    'Magic Powder': 'HAS_ITEM(MagicPowder)',
    'Mushroom': 'HAS_ITEM(Mushroom)',
    'Blue Boomerang': 'HAS_ITEM(BlueBoomerang)',
    'Red Boomerang': 'HAS_ITEM(RedBoomerang)',
    'Moon Pearl': 'HAS_ITEM(MoonPearl)',
    'Bow': 'CanShootArrowsL1()',
    # macros.yaml's level-2 archery macro is CanShootSilvers (CanShootArrowsL2
    # does not exist — an unknown macro makes rando_logic_gen compile the WHOLE
    # predicate to constant FALSE, walling off the affected door edges).
    'Silver Arrows': 'CanShootSilvers()',
    'Big Key (Ganons Tower)': 'HAS_ITEM(BigKey_GanonsTower)',
    'Pegasus Boots': 'HAS_ITEM(Boots)',
    # kill-rule sword tiers (any-sword disjunctions simplify via A|(A&B) fold)
    'Fighter Sword': 'HasSword(1)',
    'Master Sword': 'HasSword2()',
    'Tempered Sword': 'HasSword3()',
    'Golden Sword': 'HasSword(4)',
}

# Events granted by event locations inside dungeons (monotone unlocks). The
# Swamp ones are listed for completeness; Swamp is pinned at MVP and the
# translator asserts NEGATED event tests occur only there.
KNOWN_EVENTS = {
    'Trench 1 Filled', 'Trench 2 Filled', 'Drained Swamp', 'Open Floodgate',
    'Hidden Pits', 'Convenient Block', 'Shining Light', 'Maiden Rescued',
    'Maiden Unmasked', 'Attic Cracked Floor',
}

# Zero-arg macro calls: reference CollectionState method -> fork DSL.
MACRO_MAP = {
    'can_use_bombs': 'CanBombThings()',
    'can_shoot_arrows': 'CanShootArrowsL1()',
    'has_Boots': 'HAS_ITEM(Boots)',
    'has_fire_source': '(HAS_ITEM(Lamp) OR HAS_ITEM(FireRod))',
    'can_lift_rocks': 'CanLiftRocks()',
    'can_lift_heavy_rocks': 'CanLiftDarkRocks()',
    'can_melt_things': 'CanMeltThings(world)',
    'has_Mirror': 'HAS_ITEM(MagicMirror)',
    'has_sword': 'HasSword(1)',
    'has_beam_sword': 'HasSword2()',
    'has_blunt_weapon': '(HasSword(1) OR HAS_ITEM(Hammer))',
    # DR can_hit_crystal (BaseClasses.py:1211): bombs|arrows|blunt|boomerangs|
    # hookshot|fire|ice|somaria|byrna.
    'can_hit_crystal': ('(CanBombThings() OR CanShootArrowsL1() OR HasSword(1) OR '
                        'HAS_ITEM(Hammer) OR HAS_ITEM(BlueBoomerang) OR '
                        'HAS_ITEM(RedBoomerang) OR HAS_ITEM(Hookshot) OR '
                        'HAS_ITEM(FireRod) OR HAS_ITEM(IceRod) OR '
                        'HAS_ITEM(CaneOfSomaria) OR HAS_ITEM(CaneOfByrna))'),
    # DR can_avoid_lasers (BaseClasses.py:1280); fork TR idiom includes Byrna.
    'can_avoid_lasers': '(CanBlockLasers() OR HAS_ITEM(Cape) OR HAS_ITEM(CaneOfByrna))',
    # DR can_hit_crystal_through_barrier (BaseClasses.py:1223): bombs|arrows|
    # boomerangs|fire|ice|somaria (NO blunt/hookshot/byrna).
    'can_hit_crystal_through_barrier': ('(CanBombThings() OR CanShootArrowsL1() OR '
                                        'HAS_ITEM(BlueBoomerang) OR HAS_ITEM(RedBoomerang) OR '
                                        'HAS_ITEM(FireRod) OR HAS_ITEM(IceRod) OR '
                                        'HAS_ITEM(CaneOfSomaria))'),
}

# DR small-key names -> fork item enum names (vanilla key-logic at-location
# rules attach these on a few HC chests; HC is pinned but tables stay complete).
SM_KEY_MAP = {
    'Small Key (Escape)': 'SmallKey_HyruleCastleEscape',
    'Small Key (Eastern Palace)': 'SmallKey_EasternPalace',
    'Small Key (Desert Palace)': 'SmallKey_DesertPalace',
    'Small Key (Tower of Hera)': 'SmallKey_TowerOfHera',
    'Small Key (Agahnims Tower)': 'SmallKey_HyruleCastleTower',
    'Small Key (Palace of Darkness)': 'SmallKey_PalaceOfDarkness',
    'Small Key (Swamp Palace)': 'SmallKey_SwampPalace',
    'Small Key (Skull Woods)': 'SmallKey_SkullWoods',
    'Small Key (Thieves Town)': 'SmallKey_ThievesTown',
    'Small Key (Ice Palace)': 'SmallKey_IcePalace',
    'Small Key (Misery Mire)': 'SmallKey_MiseryMire',
    'Small Key (Turtle Rock)': 'SmallKey_TurtleRock',
    'Small Key (Ganons Tower)': 'SmallKey_GanonsTower',
}


class RuleError(Exception):
    pass


class RuleTranslator:
    def __init__(self, model):
        self.model = model
        self.region_names = {r['name'] for r in model['regions']}
        self.region_dungeon = {r['name']: r['dungeon'] for r in model['regions']}
        self.dungeon_fork = {d['index']: d['fork_name'] for d in model['dungeons']}
        self.errors = []   # (spot, body, message)

    # -- descriptor entry points ------------------------------------------

    def translate_entry_list(self, spot, entries, spot_dungeon):
        """Compose the ordered (op, descriptor) list into one IR tree."""
        cur = ('true',)
        for op, descr in entries:
            ir = self.translate_descr(spot, descr, spot_dungeon)
            if op == 'set':
                cur = ir
            elif op == 'or':
                cur = ('or', [cur, ir])
            else:  # 'and'
                cur = ('and', [cur, ir])
        return simplify_ir(cur)

    def translate_descr(self, spot, descr, spot_dungeon):
        try:
            if 'ast' in descr:
                return self.translate_rule_ast(descr['ast'])
            return self.translate_lambda(spot, descr, spot_dungeon)
        except RuleError as e:
            self.errors.append((spot, json.dumps(descr)[:200], str(e)))
            return ('false',)

    # -- structured Rule objects (challenge/kill rules) --------------------

    def translate_rule_ast(self, node):
        t = node['rule_type']
        if t == 'Static':
            return ('true',) if node['principal'] else ('false',)
        if t == 'Conjunction':
            return simplify_ir(('and', [self.translate_rule_ast(s) for s in node.get('sub_rules', [])]))
        if t == 'Disjunction':
            return simplify_ir(('or', [self.translate_rule_ast(s) for s in node.get('sub_rules', [])]))
        if t == 'Negate':
            return ('not', self.translate_rule_ast(node['sub_rules'][0]))
        if t == 'Item':
            name = node['principal']
            if name in HAS_ITEM_MAP and node['count'] == 1:
                return ('vm', HAS_ITEM_MAP[name])
            raise RuleError(f'unmapped AST item {name} x{node["count"]}')
        if t == 'ExtendMagic':
            magic = node['principal'] if isinstance(node['principal'], int) else 16
            return ('vm', f'CanExtendMagic(world, {max(1, magic // 8)})')
        raise RuleError(f'unmapped AST rule_type {t}')

    # -- lambda descriptors -------------------------------------------------

    def translate_lambda(self, spot, descr, spot_dungeon, depth=0):
        src = descr['src']
        closure = descr['closure']
        if src in ('<unrecoverable>', '<depth-limit>'):
            raise RuleError(src)
        try:
            tree = pyast.parse(src, mode='eval').body
        except SyntaxError as e:
            raise RuleError(f'unparseable body: {src!r} ({e})')
        return self.expr(spot, tree, closure, spot_dungeon, depth)

    def expr(self, spot, node, closure, spot_dungeon, depth):
        if isinstance(node, pyast.BoolOp):
            op = 'and' if isinstance(node.op, pyast.And) else 'or'
            return simplify_ir((op, [self.expr(spot, v, closure, spot_dungeon, depth)
                                     for v in node.values]))
        if isinstance(node, pyast.UnaryOp) and isinstance(node.op, pyast.Not):
            inner = self.expr(spot, node.operand, closure, spot_dungeon, depth)
            if inner[0] == 'event':
                dgn = self.dungeon_fork.get(spot_dungeon, '?')
                if dgn not in ('SwampPalace',):
                    raise RuleError(f'negated event {inner[1]!r} outside Swamp ({dgn})')
            return ('not', inner)
        if isinstance(node, (pyast.Constant,)):
            if node.value is True:
                return ('true',)
            if node.value is False:
                return ('false',)
            raise RuleError(f'constant {node.value!r}')
        if isinstance(node, pyast.Name):
            # closure variable: bool literal or nested rule descriptor
            val = closure.get(node.id)
            if isinstance(val, dict):
                if depth > 24:
                    raise RuleError('closure depth')
                return self.translate_lambda(spot, val, spot_dungeon, depth + 1)
            if val == 'True':
                return ('true',)
            if val == 'False':
                return ('false',)
            raise RuleError(f'unresolved name {node.id} = {val!r}')
        if isinstance(node, pyast.Call):
            return self.call(spot, node, closure, spot_dungeon, depth)
        if isinstance(node, pyast.Attribute):
            # state.world.can_take_damage — fork has no OHKO mode at MVP
            if unparse(node) == 'state.world.can_take_damage':
                return ('true',)
            raise RuleError(f'attribute {unparse(node)!r}')
        if isinstance(node, pyast.Subscript):
            if unparse(node).startswith('state.world.free_lamp_cone'):
                return ('vm', 'CanDarkRoomNav()')
            raise RuleError(f'subscript {unparse(node)!r}')
        raise RuleError(f'expr {unparse(node)!r}')

    def call(self, spot, node, closure, spot_dungeon, depth):
        text = unparse(node)
        fn = node.func
        fn_text = unparse(fn)

        # rule1(state) / rule2(state): nested closure lambdas
        if isinstance(fn, pyast.Name):
            val = closure.get(fn.id)
            if isinstance(val, dict):
                if depth > 24:
                    raise RuleError('closure depth')
                return self.translate_lambda(spot, val, spot_dungeon, depth + 1)
            raise RuleError(f'call to unresolved {fn.id}')

        # boss defeat forms
        if fn_text.endswith('.can_defeat'):
            owner = unparse(fn.value)
            if (owner == 'location.parent_region.dungeon.boss'
                    or re.match(r"world\.get_location\(.*\)\.parent_region\.dungeon\.boss$", owner)):
                dgn = self.dungeon_fork.get(spot_dungeon)
                if dgn is None:
                    raise RuleError('boss rule outside dungeon')
                if dgn == 'CastleTower':
                    # Agahnim 1 — fork idiom (12_hyrule_castle_tower.yaml "Agahnim")
                    return ('vm', '(HasSword(1) OR (OP_MODEWEAPONS_EQ(swordless) AND '
                                  '(HAS_ITEM(Hammer) OR HAS_ITEM(BugCatchingNet))))')
                return ('vm', f'CanKillBoss({dgn})')
            m = re.match(r"world\.get_region\('([^']+)', player\)\.dungeon\.bosses\['(\w+)'\]", owner)
            if m:
                slot = m.group(2)
                if slot == 'bottom':
                    return ('vm', 'CanKillArmosKnights()')
                if slot == 'middle':
                    return ('vm', 'CanKillLanmolas(world)')
                if slot == 'top':
                    return ('vm', 'CanKillMoldorm()')
                raise RuleError(f'GT boss slot {slot}')
            raise RuleError(f'boss form {owner!r}')

        if not fn_text.startswith('state.'):
            raise RuleError(f'call {text!r}')
        meth = fn_text[len('state.'):]

        if meth == 'has':
            name = self.const_arg(node.args[0], closure)
            count = 1
            if len(node.args) >= 3:
                count = self.const_arg(node.args[2], closure)
            if name in KNOWN_EVENTS:
                if count != 1:
                    raise RuleError(f'event with count {name}')
                return ('event', name)
            if name in HAS_ITEM_MAP and count == 1:
                return ('vm', HAS_ITEM_MAP[name])
            raise RuleError(f'has({name!r}, count={count})')

        if meth in ('can_reach_blue', 'can_reach_orange'):
            m = re.match(r"world\.get_region\('([^']+)', player\)", unparse(node.args[0]))
            if not m or m.group(1) not in self.region_names:
                raise RuleError(f'creach region in {text!r}')
            return ('creach', m.group(1), 'blue' if meth == 'can_reach_blue' else 'orange')

        if meth == 'can_reach':
            # state.can_reach('R', 'Region', player) — any-crystal-state reach
            rname = self.const_arg(node.args[0], closure)
            if len(node.args) >= 2 and self.const_arg(node.args[1], closure) != 'Region':
                raise RuleError(f'can_reach non-region {text!r}')
            if rname not in self.region_names:
                raise RuleError(f'can_reach unknown region {rname!r}')
            return ('reach', rname)

        if meth == 'has_sm_key':
            key = self.const_arg(node.args[0], closure)
            if key not in SM_KEY_MAP:
                raise RuleError(f'has_sm_key {key!r}')
            return ('vm', f'HAS_AMOUNT({SM_KEY_MAP[key]}, 1)')

        if meth == 'has_hearts':
            # Fork stance (31_misery_mire.yaml Spike Chest): cantTakeDamage
            # defaults false -> hearts checks collapse to TRUE.
            return ('true',)

        if meth == 'can_extend_magic':
            magic = 16
            for a in node.args[1:]:
                v = self.const_arg(a, closure, allow_fail=True)
                if isinstance(v, int):
                    magic = v
                    break
            bars = max(1, magic // 8)
            return ('vm', f'CanExtendMagic(world, {bars})')

        if meth == 'can_kill_most_things':
            enemies = 5
            for a in node.args[1:]:
                v = self.const_arg(a, closure, allow_fail=True)
                if isinstance(v, int):
                    enemies = v
            return ('vm', f'CanKillMostThings(world, {enemies})')

        if meth in MACRO_MAP:
            return ('vm', MACRO_MAP[meth])

        raise RuleError(f'method {meth!r} in {text!r}')

    def const_arg(self, node, closure, allow_fail=False):
        if isinstance(node, pyast.Constant):
            return node.value
        if isinstance(node, pyast.Name):
            val = closure.get(node.id)
            if isinstance(val, str):
                try:
                    return pyast.literal_eval(val)
                except (ValueError, SyntaxError):
                    pass
        if allow_fail:
            return None
        raise RuleError(f'non-const arg {unparse(node)!r}')


def unparse(node):
    try:
        return pyast.unparse(node)
    except AttributeError:  # py<3.9
        import io
        return pyast.dump(node)


def simplify_ir(node):
    """Flatten nests, drop true/false identities, fold A OR (A AND B) -> A."""
    kind = node[0]
    if kind in ('and', 'or'):
        flat = []
        for c in node[1]:
            c = simplify_ir(c)
            if c[0] == kind:
                flat.extend(c[1])
            else:
                flat.append(c)
        # identity / absorbing elements
        if kind == 'and':
            flat = [c for c in flat if c != ('true',)]
            if any(c == ('false',) for c in flat):
                return ('false',)
            if not flat:
                return ('true',)
        else:
            flat = [c for c in flat if c != ('false',)]
            if any(c == ('true',) for c in flat):
                return ('true',)
            if not flat:
                return ('false',)
        # dedup + absorption: A OR (A AND ...) -> A
        uniq = []
        for c in flat:
            if c not in uniq:
                uniq.append(c)
        if kind == 'or':
            atoms = [c for c in uniq if c[0] not in ('and',)]
            uniq = [c for c in uniq
                    if not (c[0] == 'and' and any(a in c[1] for a in atoms))]
        if len(uniq) == 1:
            return uniq[0]
        return (kind, uniq)
    if kind == 'not':
        return ('not', simplify_ir(node[1]))
    return node


def fold_vm(node):
    """Collapse pure-vm subtrees into single DSL strings."""
    kind = node[0]
    if kind in ('and', 'or'):
        children = [fold_vm(c) for c in node[1]]
        if all(c[0] == 'vm' for c in children):
            j = ' AND ' if kind == 'and' else ' OR '
            return ('vm', '(' + j.join(c[1] for c in children) + ')')
        return (kind, children)
    if kind == 'not':
        inner = fold_vm(node[1])
        if inner[0] == 'vm':
            return ('vm', f'(NOT {inner[1]})')
        return ('not', inner)
    return node


# ---------------------------------------------------------------------------
# Phase B2: table building + C emission.
# ---------------------------------------------------------------------------

DIR_ENUM = {'North': 0, 'South': 1, 'West': 2, 'East': 3, 'Up': 4, 'Down': 5, None: 0xFF}
TYPE_ENUM = {'Normal': 0, 'SpiralStairs': 1, 'StraightStairs': 2, 'Ladder': 3,
             'Open': 4, 'Hole': 5, 'Warp': 6, 'Interior': 7, 'Logical': 8}
CRYSTAL_ENUM = {'Null': 0, 'Blue': 1, 'Orange': 2, 'Either': 3}
EVENT_ORDER = ['Trench 1 Filled', 'Trench 2 Filled', 'Drained Swamp', 'Open Floodgate',
               'Hidden Pits', 'Convenient Block', 'Shining Light', 'Maiden Rescued',
               'Maiden Unmasked', 'Attic Cracked Floor']
# event name -> granting reference location
EVENT_GRANT_LOC = {
    'Trench 1 Filled': 'Trench 1 Switch',
    'Trench 2 Filled': 'Trench 2 Switch',
    'Drained Swamp': 'Swamp Drain',
    'Hidden Pits': 'Skull Star Tile',
    'Convenient Block': 'Ice Block Drop',
    'Shining Light': 'Attic Cracked Floor',
    'Maiden Rescued': 'Suspicious Maiden',
    'Maiden Unmasked': 'Revealing Light',
    'Attic Cracked Floor': 'Attic Cracked Floor',
}
# Events granted OUTSIDE the shuffled dungeons ('Floodgate' lives at the Dam,
# a cave region): no in-dungeon granting row exists. Emitted with region
# 0xFFFF — the explorer treats their Event leaves as externally-supplied
# (lenient: true; the Stage-3 oracle binds them to fork logic).
EXTERNAL_EVENTS = {'Open Floodgate'}
# reference location name -> fork location name where normalization fails
LOC_NAME_FIX = {
    "Hyrule Castle - Zelda's Chest": "Hyrule Castle - Zelda's Cell",
    # DR renamed ALTTPR's Moldorm Chest (the post-Moldorm-2 chest).
    "Ganons Tower - Validation Chest": "Ganon's Tower - Moldorm Chest",
}

# Vanilla NON-POSITIONAL Normal connections = the engine's teleport doors
# (the `(link_tile_below & 0xcf) == 0x89` branch reading
# dung_hdr_travel_destinations[3/4]) — discovered by the positional
# cross-check; currently Hyrule Castle's 1F<->2F hall doors only. These stay
# Normal-typed in the pool model but carry kDoorTblFlag_VanillaTeleport so the
# runtime hooks them at the teleport read site instead of the positional line.
VANILLA_TELEPORT_PAIRS = {
    ('Hyrule Castle East Hall W', 'Hyrule Castle Back Hall E'),
    ('Hyrule Castle West Hall E', 'Hyrule Castle Back Hall W'),
}

# Doors whose two halves vanilla-connect across a palace-index boundary may
# carry the bit-1 (cur_palace_index_x2) transition toggle; basic shuffle keeps
# doors within one reference dungeon, and Rando_DoorArrive leaves the palace
# index unchanged, so any pool door pair crossing such a boundary must fail
# codegen. The check uses the per-room palace mapping derivable from dungeon
# membership: all rooms referenced by one reference-dungeon's doors must agree.


def norm_loc(s):
    s = s.lower().replace("'", '').replace('-', ' ')
    return re.sub(r'\s+', ' ', s).strip()


class TableBuilder:
    def __init__(self, model, repo):
        self.model = model
        self.repo = repo
        self.problems = []

        import yaml
        with open(os.path.join(repo, 'assets', 'rando', 'location_registry.yaml')) as f:
            locreg = yaml.safe_load(f)
        self.fork_locs = {l['name']: l['id'] for l in locreg['locations'] if not l.get('deprecated')}
        self.fork_locs_norm = {norm_loc(n): i for n, i in self.fork_locs.items()}
        with open(os.path.join(repo, 'assets', 'rando', 'item_registry.yaml')) as f:
            itemreg = yaml.safe_load(f)
        self.fork_items = {i['name']: i['id'] for i in itemreg['items']}

    def problem(self, msg):
        self.problems.append(msg)

    def build(self):
        m = self.model
        # ---- regions sorted by (dungeon, name) => contiguous per-dungeon ----
        regions = sorted(m['regions'], key=lambda r: (r['dungeon'], r['name']))
        self.region_id = {r['name']: i for i, r in enumerate(regions)}
        self.regions = regions

        # ---- doors: physical dungeon doors, sorted by name (frozen ids) ----
        doors = [d for d in sorted(m['doors'], key=lambda d: d['name'])
                 if d['region'] is not None]
        self.door_id = {d['name']: i for i, d in enumerate(doors)}
        self.doors = doors

        # rooms join: (room, doorListPos) -> (pos_byte, kind)
        room_doorlists = {}
        for room in m['rooms']:
            entries = []
            for pos_name, kind_name in room['door_list']:
                entries.append((m['position_values'][pos_name], m['door_kind_values'][kind_name]))
            room_doorlists[room['index']] = entries
        self.room_doorlists = room_doorlists

        for d in doors:
            d['id'] = self.door_id[d['name']]
            d['pos_byte'] = 0xFF
            d['kind'] = 0xFF
            if d['pos'] is not None and d['pos'] >= 0:
                entries = room_doorlists.get(d['room'])
                if entries is None or d['pos'] >= len(entries):
                    self.problem(f"door {d['name']}: doorListPos {d['pos']} out of range for room {d['room']:#x}")
                else:
                    d['pos_byte'], d['kind'] = entries[d['pos']]

        # ---- pool membership + vanilla pairs ----
        pool_types = ('Normal', 'SpiralStairs')
        pair_of = {}
        one_way_pairs = []
        for a, b in m['vanilla_pairs']['two_way']:
            if a in self.door_id and b in self.door_id:
                pair_of[a] = b
                pair_of[b] = a
        for a, b in m['vanilla_pairs']['one_way']:
            if a in self.door_id and b in self.door_id:
                one_way_pairs.append((a, b))
        self.vanilla_two_way = [(a, b) for a, b in m['vanilla_pairs']['two_way']
                                if a in self.door_id and b in self.door_id]
        self.vanilla_one_way = one_way_pairs
        for d in doors:
            d['in_pool'] = (d['type'] in pool_types
                            and (d['name'] in pair_of
                                 or any(d['name'] in p for p in one_way_pairs)))

        # ---- verification: vanilla connectivity is positional -------------
        DELTA = {('East', 'West'): 1, ('West', 'East'): -1,
                 ('South', 'North'): 0x10, ('North', 'South'): -0x10}
        for a, b in self.vanilla_two_way:
            da, db = doors[self.door_id[a]], doors[self.door_id[b]]
            if (a, b) in VANILLA_TELEPORT_PAIRS or (b, a) in VANILLA_TELEPORT_PAIRS:
                da['vanilla_teleport'] = db['vanilla_teleport'] = True
            elif da['type'] == 'Normal' and db['type'] == 'Normal':
                key = (da['direction'], db['direction'])
                if key not in DELTA:
                    self.problem(f'vanilla pair {a}<->{b}: directions {key} not opposite')
                elif db['room'] - da['room'] != DELTA[key]:
                    self.problem(f'vanilla pair {a}<->{b}: rooms {da["room"]:#x}->{db["room"]:#x} '
                                 f'not positional for {key}')
            if da['room'] >= 0x100 or db['room'] >= 0x100:
                self.problem(f'vanilla pair {a}<->{b}: room >= 0x100')

        # ---- rules ----------------------------------------------------------
        tr = RuleTranslator(m)
        self.vm_preds = []        # ordered unique DSL strings
        self.vm_index = {}
        self.rule_blob = bytearray()
        self.rule_offsets = {}    # ir-key -> offset
        region_dgn = {r['name']: r['dungeon'] for r in m['regions']}

        def vm_idx(dsl):
            if dsl not in self.vm_index:
                self.vm_index[dsl] = len(self.vm_preds)
                self.vm_preds.append(dsl)
            return self.vm_index[dsl]

        def encode(ir, out):
            k = ir[0]
            if k == 'true':
                out.append(0)
            elif k == 'false':
                out.append(1)
            elif k in ('and', 'or'):
                out.append(2 if k == 'and' else 3)
                out.append(len(ir[1]))
                for c in ir[1]:
                    encode(c, out)
            elif k == 'not':
                out.append(4)
                encode(ir[1], out)
            elif k == 'vm':
                i = vm_idx(ir[1])
                out.extend((5, i & 0xFF, i >> 8))
            elif k == 'event':
                out.extend((6, EVENT_ORDER.index(ir[1])))
            elif k == 'creach':
                rid = self.region_id[ir[1]]
                out.extend((7, rid & 0xFF, rid >> 8, 0 if ir[2] == 'blue' else 1))
            elif k == 'reach':
                rid = self.region_id[ir[1]]
                out.extend((8, rid & 0xFF, rid >> 8))
            else:
                raise RuleError(f'encode {k}')

        def rule_ref(spot, entries, dungeon, extra_irs=None):
            ir = tr.translate_entry_list(spot, entries, dungeon) if entries else ('true',)
            if extra_irs:
                ir = simplify_ir(('and', [ir] + list(extra_irs)))
            ir = fold_vm(ir)
            if ir == ('true',):
                return 0xFFFF  # always true
            key = repr(ir)
            if key not in self.rule_offsets:
                off = len(self.rule_blob)
                buf = bytearray()
                encode(ir, buf)
                self.rule_blob.extend(buf)
                self.rule_offsets[key] = off
            return self.rule_offsets[key]

        # ---- edges ---------------------------------------------------------
        # door.req_event is the stitcher's own event gate (ExplorationState
        # event_doors); it is NOT part of the recorded access rules, so AND the
        # mapped event into the edge rule (event names differ from the granting
        # location names that req_event uses).
        REQ_EVENT_TO_EVENT = {
            'Trench 1 Switch': 'Trench 1 Filled',
            'Trench 2 Switch': 'Trench 2 Filled',
            'Swamp Drain': 'Drained Swamp',
        }
        edges = []
        for e in m['edges']:
            dgn = region_dgn[e['from']]
            door = e.get('door')
            door_id = self.door_id.get(door, 0xFFFF) if door else 0xFFFF
            cls = 'static'
            extra = None
            if door and door in self.door_id:
                dd = self.doors[self.door_id[door]]
                if dd['in_pool']:
                    cls = 'pool'
                if dd.get('req_event'):
                    ev = REQ_EVENT_TO_EVENT.get(dd['req_event'])
                    if ev is None:
                        self.problem(f"door {door}: unmapped req_event {dd['req_event']!r}")
                    else:
                        extra = [('event', ev)]
            edges.append({
                'name': e['name'],
                'from': self.region_id[e['from']],
                'to': self.region_id[e['to']],
                'rule': rule_ref(e['name'], e['rules'], dgn, extra),
                'door': door_id,
                'cls': cls,
            })
        self.edges = edges

        # ---- locations / events / drop keys --------------------------------
        # ExplorationState.flooded_key_check: a trench event fires only once
        # its flooded pot-key LOCATION was found — model as AND Reach(that
        # location's region) on the event rule.
        flooded = m['stitcher'].get('flooded_keys', {})
        loc_region_name = {l['name']: l['region'] for l in m['locations']}
        locations, events, drop_keys = [], {}, []
        for l in m['locations']:
            dgn = region_dgn[l['region']]
            name = LOC_NAME_FIX.get(l['name'], l['name'])
            extra = None
            if l['name'] in flooded:
                fl = flooded[l['name']]
                if fl in loc_region_name:
                    extra = [('reach', loc_region_name[fl])]
                else:
                    self.problem(f"flooded key location {fl!r} (for {l['name']}) not found")
            rule = rule_ref(l['name'], l['rules'], dgn, extra)
            fork_id = self.fork_locs.get(name)
            if fork_id is None:
                fork_id = self.fork_locs_norm.get(norm_loc(name))
            forced = l.get('forced_item') or ''
            if fork_id is not None:
                locations.append({'name': name, 'fork_id': fork_id,
                                  'region': self.region_id[l['region']], 'rule': rule})
            elif forced in EVENT_ORDER or l['name'] in EVENT_GRANT_LOC.values():
                # event location: grants `forced` (or the event mapped to it)
                ev = forced if forced in EVENT_ORDER else None
                if ev is None:
                    ev = next((k for k, v in EVENT_GRANT_LOC.items() if v == l['name'])
                              , None)
                if ev is None:
                    self.problem(f'event location {l["name"]}: no event mapping')
                else:
                    events[ev] = {'region': self.region_id[l['region']], 'rule': rule}
            elif forced.startswith('Small Key'):
                drop_keys.append({'name': l['name'], 'dungeon': dgn,
                                  'region': self.region_id[l['region']], 'rule': rule})
            elif forced.startswith('Big Key'):
                # HC Big Key Drop — fork has no such location; HC is pinned and
                # big-key drops aren't modeled (BK comes from the fork pool).
                pass
            elif l['name'] in ('Agahnim 1', 'Agahnim 2', "Frozen Lake Ledge?"):
                pass  # fork models these via its own Prize_Event locations
            else:
                self.problem(f'location {l["name"]!r}: no fork id, not event/drop (forced={forced!r})')
        # multi-grant events: 'Attic Cracked Floor' grants 'Shining Light' too —
        # resolved by aliasing below if missing.
        for ev in EVENT_ORDER:
            if ev in events:
                continue
            if ev in EXTERNAL_EVENTS:
                events[ev] = {'region': 0xFFFF, 'rule': 0xFFFF}
                continue
            grant = EVENT_GRANT_LOC.get(ev)
            if grant and any(l['name'] == grant for l in m['locations']):
                # granted by a location that mapped to another event or fork id
                src = next(l for l in m['locations'] if l['name'] == grant)
                events[ev] = {'region': self.region_id[src['region']],
                              'rule': rule_ref(src['name'], src['rules'],
                                               region_dgn[src['region']])}
            else:
                self.problem(f'event {ev!r}: no granting location found')
        self.locations = locations
        self.events = events
        self.drop_keys = drop_keys
        self.tr_errors = tr.errors

        # ---- dungeons -------------------------------------------------------
        dgns = []
        for d in m['dungeons']:
            rng = [i for i, r in enumerate(regions) if r['dungeon'] == d['index']]
            sk = f"SmallKey_{d['fork_name']}" if d['fork_name'] != 'HyruleCastle' else 'SmallKey_HyruleCastleEscape'
            if d['fork_name'] == 'CastleTower':
                sk = 'SmallKey_HyruleCastleTower'
            bk = f"BigKey_{d['fork_name']}"
            dgns.append({
                'ref_name': d['ref_name'],
                'fork_name': d['fork_name'],
                'chest_small_keys': d['chest_small_keys'],
                'has_big_key': d['has_big_key'],
                'region_first': min(rng) if rng else 0,
                'region_count': len(rng),
                'small_key_item': self.fork_items.get(sk, 0xFFFF),
                'big_key_item': self.fork_items.get(bk, 0xFFFF) if d['has_big_key'] else 0xFFFF,
            })
            if rng and (max(rng) - min(rng) + 1) != len(rng):
                self.problem(f'dungeon {d["ref_name"]}: regions not contiguous')
            if d['has_big_key'] and dgns[-1]['big_key_item'] == 0xFFFF:
                self.problem(f'dungeon {d["ref_name"]}: fork big key item {bk} not found')
            if dgns[-1]['small_key_item'] == 0xFFFF:
                self.problem(f'dungeon {d["ref_name"]}: fork small key item {sk} not found')
        self.dungeons = dgns

        # ---- required paths -------------------------------------------------
        st = m['stitcher']
        paths = []
        for rname in st['boss_path_checks']:
            if rname in self.region_id:
                paths.append({'dungeon': region_dgn[rname], 'region': self.region_id[rname],
                              'kind': 'boss'})
        for rname in st['drop_path_checks']:
            if rname in self.region_id:
                paths.append({'dungeon': region_dgn[rname], 'region': self.region_id[rname],
                              'kind': 'drop'})
        # TT basic-mode attic-window path (determine_paths_for_dungeon)
        if 'Thieves Attic Window' in self.region_id:
            paths.append({'dungeon': region_dgn['Thieves Attic Window'],
                          'region': self.region_id['Thieves Attic Window'], 'kind': 'boss'})
        self.paths = paths

        # ---- portals (entrance lobbies + drop arrivals) ---------------------
        ref_dgn_index = {d['ref_name']: d['index'] for d in m['dungeons']}
        portals = []
        for dname, lobbies in st['default_dungeon_entrances'].items():
            di = ref_dgn_index.get(dname)
            if di is None:
                continue
            for rname in lobbies:
                if rname not in self.region_id:
                    self.problem(f'portal lobby {rname!r} ({dname}): unknown region')
                    continue
                portals.append({'dungeon': di, 'region': self.region_id[rname],
                                'is_drop': 0, 'name': rname})
        for dname, drops in st['drop_entrances'].items():
            di = ref_dgn_index.get(dname)
            if di is None:
                continue
            for rname in drops:
                if rname not in self.region_id:
                    self.problem(f'drop entrance {rname!r} ({dname}): unknown region')
                    continue
                portals.append({'dungeon': di, 'region': self.region_id[rname],
                                'is_drop': 1, 'name': rname})
        self.portals = portals
        # C consumers (Door_AllPortalOrigins / Door_ExploreStaged in
        # shuffle_doors.c) append portal origins into fixed
        # uint16[kDoorGen_MaxOrigins] arrays — the generator is the spec for
        # that bound, so enforce it here instead of overflowing at runtime.
        # Count the population the STATIC consumer actually appends:
        # Door_AllPortalOrigins walks only the !is_drop rows (is_drop==1 rows
        # mark which regions are drop arrivals; they are never appended as
        # origins themselves). Door_ExploreStaged can additionally re-append
        # staged lobbies at runtime, but that append is bounds-guarded
        # fail-closed on the C side, so this static check covers the static
        # population only — conservatively, since staged lobbies are drawn
        # from regions Door_InitialOrigins first excluded.
        kDoorGen_MaxOrigins = 12  # door_keylogic.h, grow in lockstep
        per_dungeon = {}
        for p in portals:
            if p['is_drop']:
                continue
            per_dungeon[p['dungeon']] = per_dungeon.get(p['dungeon'], 0) + 1
        for di, cnt in sorted(per_dungeon.items()):
            if cnt > kDoorGen_MaxOrigins:
                self.problem(f'dungeon {di}: {cnt} entrance-portal rows exceed '
                             f'kDoorGen_MaxOrigins={kDoorGen_MaxOrigins} '
                             f'(door_keylogic.h must grow in lockstep)')

        # ---- room door lists (kind overlay) + paired doors ------------------
        self.rooms_sorted = sorted(room_doorlists.items())
        self.paired = [(self.door_id[p['a']], self.door_id[p['b']], 1 if p['pair'] else 0)
                       for p in m['paired_doors']
                       if p['a'] in self.door_id and p['b'] in self.door_id]
        return self


def check_registry(builder, registry_path, init):
    """door_registry.yaml freezes door name -> id (digest stability across builds).

    Returns a list of problems (empty = consistent). With init=True (or no file
    yet) the registry is (re)written from the current build instead.
    """
    import yaml
    current = {d['name']: i for i, d in enumerate(builder.doors)}
    if init or not os.path.isfile(registry_path):
        with open(registry_path, 'w', newline='\n', encoding='utf-8') as f:
            f.write('# door_registry.yaml — FROZEN door-stub ids for door shuffle.\n'
                    '# Generated by gen_door_tables.py --init-registry; ids are\n'
                    '# append-only (the per-seed door-layout digest hashes door ids, so\n'
                    '# a re-numbering invalidates every door-shuffle save).\n'
                    'format_version: 1\ndoors:\n')
            for name, i in sorted(current.items(), key=lambda kv: kv[1]):
                f.write(f'  - {{ id: {i}, name: "{name}" }}\n')
        return []
    with open(registry_path, encoding='utf-8') as f:
        reg = yaml.safe_load(f)
    frozen = {e['name']: e['id'] for e in reg['doors']}
    problems = []
    for name, i in frozen.items():
        if current.get(name) != i:
            problems.append(f'registry drift: {name!r} frozen id {i}, current {current.get(name)}')
    for name in current:
        if name not in frozen:
            problems.append(f'registry missing new door {name!r} (append it explicitly)')
    return problems


def emit_c(builder, out_h, out_c, out_preds):
    H = []
    C = []
    doors, regions = builder.doors, builder.regions

    name_blob = []
    name_off = {}

    def s_off(s):
        if s not in name_off:
            name_off[s] = sum(len(x) + 1 for x in name_blob)
            name_blob.append(s)
        return name_off[s]

    H.append('// door_tables.gen.h — GENERATED by assets/scripts/gen_door_tables.py. Do not edit.\n')
    H.append('#pragma once\n#include "../types.h"\n\n')
    H.append('enum {  // DoorTbl door types\n'
             '  kDoorTblType_Normal, kDoorTblType_SpiralStairs, kDoorTblType_StraightStairs,\n'
             '  kDoorTblType_Ladder, kDoorTblType_Open, kDoorTblType_Hole, kDoorTblType_Warp,\n'
             '  kDoorTblType_Interior, kDoorTblType_Logical,\n};\n')
    H.append('enum {  // DoorTbl directions\n'
             '  kDoorTblDir_North, kDoorTblDir_South, kDoorTblDir_West, kDoorTblDir_East,\n'
             '  kDoorTblDir_Up, kDoorTblDir_Down,\n};\n')
    H.append('enum {  // DoorTblDoor flags\n'
             '  kDoorTblFlag_Toggle = 0x1, kDoorTblFlag_Blocked = 0x2,\n'
             '  kDoorTblFlag_Trapped = 0x4, kDoorTblFlag_Stonewall = 0x8,\n'
             '  kDoorTblFlag_VanillaSmallKey = 0x10, kDoorTblFlag_VanillaBigKey = 0x20,\n'
             '  kDoorTblFlag_Ugly = 0x40, kDoorTblFlag_DeadEnd = 0x80,\n'
             '  kDoorTblFlag_Portal = 0x100, kDoorTblFlag_InPool = 0x200,\n'
             '  kDoorTblFlag_CrystalBlue = 0x400, kDoorTblFlag_CrystalOrange = 0x800,\n'
             '  kDoorTblFlag_VanillaTeleport = 0x1000,\n};\n')
    H.append('enum {  // DoorTblEvent ids\n')
    for i, ev in enumerate(EVENT_ORDER):
        H.append(f'  kDoorEvent_{re.sub(r"[^A-Za-z0-9]", "", ev)} = {i},\n')
    H.append('};\n')
    H.append('enum {  // door-rule bytecode ops (kDoorTblRuleBlob)\n'
             '  kDoorRuleOp_True, kDoorRuleOp_False, kDoorRuleOp_And, kDoorRuleOp_Or,\n'
             '  kDoorRuleOp_Not, kDoorRuleOp_Vm, kDoorRuleOp_Event, kDoorRuleOp_CReach,\n'
             '  kDoorRuleOp_Reach,\n};\n\n')

    H.append('typedef struct DoorTblDoor {\n'
             '  uint16 name_off;\n  uint16 region;          // kDoorTblRegions index\n'
             '  uint16 vanilla_partner;  // door id, 0xFFFF none\n  uint16 flags;\n'
             '  uint8 room;\n  uint8 type;\n  uint8 direction;\n'
             '  uint8 door_index;        // 0..2 within the edge\n  uint8 layer;\n'
             '  uint8 pos;               // doorListPos, 0xFF none\n'
             '  uint8 pos_byte;          // RoomData Position code (door-list low byte)\n'
             '  uint8 kind;              // RoomData DoorKind code (door-list high byte)\n'
             '  uint8 trap_flag;\n  uint8 quadrant;\n  int8 shift_x;\n  int8 shift_y;\n'
             '  uint8 dungeon;\n} DoorTblDoor;\n\n')
    H.append('typedef struct DoorTblRegion {\n'
             '  uint16 name_off;\n  uint8 dungeon;\n  uint8 flags;  // 1 = crystal switch present\n'
             '} DoorTblRegion;\n\n')
    H.append('typedef struct DoorTblEdge {\n'
             '  uint16 from_region;\n  uint16 to_region;\n'
             '  uint16 rule;   // offset into kDoorTblRuleBlob, 0xFFFF = always\n'
             '  uint16 door;   // door id this edge crosses, 0xFFFF = logical\n'
             '  uint8 is_pool; // 1 = vanilla connection of a shuffleable stub\n'
             '} DoorTblEdge;\n\n')
    H.append('typedef struct DoorTblPair {\n  uint16 a, b;\n  uint8 one_way;\n} DoorTblPair;\n\n')
    H.append('typedef struct DoorTblLocation {\n'
             '  uint16 fork_loc_id;\n  uint16 region;\n  uint16 rule;\n} DoorTblLocation;\n\n')
    H.append('typedef struct DoorTblEvent {\n'
             '  uint8 event_id;\n  uint16 region;\n  uint16 rule;\n} DoorTblEvent;\n\n')
    H.append('typedef struct DoorTblDropKey {\n'
             '  uint8 dungeon;\n  uint16 region;\n  uint16 rule;\n} DoorTblDropKey;\n\n')
    H.append('typedef struct DoorTblDungeon {\n'
             '  uint16 name_off;\n  uint8 chest_small_keys;\n  uint8 has_big_key;\n'
             '  uint16 region_first;\n  uint16 region_count;\n'
             '  uint16 small_key_item;\n  uint16 big_key_item;\n} DoorTblDungeon;\n\n')
    H.append('typedef struct DoorTblPath {\n'
             '  uint8 dungeon;\n  uint8 kind;  // 0 = boss/required, 1 = drop-exit\n'
             '  uint16 region;\n} DoorTblPath;\n\n')
    H.append('typedef struct DoorTblPortal {\n'
             '  uint8 dungeon;\n  uint8 is_drop;  // 1 = hole/drop arrival, not a walk-in lobby\n'
             '  uint16 region;\n} DoorTblPortal;\n\n')
    H.append('typedef struct DoorTblRoomDoor {\n  uint8 pos_byte;\n  uint8 kind;\n} DoorTblRoomDoor;\n')
    H.append('typedef struct DoorTblRoom {\n'
             '  uint8 room;\n  uint8 count;\n  uint16 first;  // into kDoorTblRoomDoors\n'
             '} DoorTblRoom;\n\n')
    H.append('typedef struct DoorTblPairedKind {\n  uint16 a, b;\n  uint8 pair;\n} DoorTblPairedKind;\n\n')

    counts = {
        'kDoorTbl_DoorCount': len(doors),
        'kDoorTbl_RegionCount': len(regions),
        'kDoorTbl_EdgeCount': len(builder.edges),
        'kDoorTbl_PairCount': len(builder.vanilla_two_way) + len(builder.vanilla_one_way),
        'kDoorTbl_LocationCount': len(builder.locations),
        'kDoorTbl_EventCount': len(builder.events),
        'kDoorTbl_DropKeyCount': len(builder.drop_keys),
        'kDoorTbl_DungeonCount': len(builder.dungeons),
        'kDoorTbl_PathCount': len(builder.paths),
        'kDoorTbl_PortalCount': len(builder.portals),
        'kDoorTbl_RoomCount': len(builder.rooms_sorted),
        'kDoorTbl_PairedKindCount': len(builder.paired),
        'kDoorTbl_VmPredCount': len(builder.vm_preds),
        'kDoorTbl_RuleBlobSize': len(builder.rule_blob),
    }
    for k, v in counts.items():
        H.append(f'#define {k} {v}\n')
    H.append('\nextern const char kDoorTblNames[];\n'
             'extern const DoorTblDoor kDoorTblDoors[kDoorTbl_DoorCount];\n'
             'extern const DoorTblRegion kDoorTblRegions[kDoorTbl_RegionCount];\n'
             'extern const DoorTblEdge kDoorTblEdges[kDoorTbl_EdgeCount];\n'
             'extern const DoorTblPair kDoorTblVanillaPairs[kDoorTbl_PairCount];\n'
             'extern const DoorTblLocation kDoorTblLocations[kDoorTbl_LocationCount];\n'
             'extern const DoorTblEvent kDoorTblEvents[kDoorTbl_EventCount];\n'
             'extern const DoorTblDropKey kDoorTblDropKeys[kDoorTbl_DropKeyCount];\n'
             'extern const DoorTblDungeon kDoorTblDungeons[kDoorTbl_DungeonCount];\n'
             'extern const DoorTblPath kDoorTblPaths[kDoorTbl_PathCount];\n'
             'extern const DoorTblPortal kDoorTblPortals[kDoorTbl_PortalCount];\n'
             'extern const DoorTblRoom kDoorTblRooms[kDoorTbl_RoomCount];\n'
             'extern const DoorTblRoomDoor kDoorTblRoomDoors[];\n'
             'extern const DoorTblPairedKind kDoorTblPairedKinds[kDoorTbl_PairedKindCount];\n'
             'extern const uint8 kDoorTblRuleBlob[kDoorTbl_RuleBlobSize];\n')

    # ------------------------------------------------------------------ C --
    C.append('// door_tables.gen.c — GENERATED by assets/scripts/gen_door_tables.py. Do not edit.\n')
    C.append('// embedded-data-guard: allow — generated from the ALttPDoorRandomizer\n'
             '// reference (MIT) by gen_door_tables.py; committed, regenerate via the script.\n')
    C.append('#include "door_tables.gen.h"\n\n')

    rows = []
    for d in doors:
        flags = ((1 if d['toggle'] else 0)
                 | (2 if d['blocked'] else 0)
                 | (4 if d['trapped'] else 0)
                 | (8 if d['stonewall'] else 0)
                 | (0x10 if d['small_key'] else 0)
                 | (0x20 if d['big_key'] else 0)
                 | (0x40 if d['ugly'] else 0)
                 | (0x80 if d['dead_end'] else 0)
                 | (0x100 if d['portal_able'] else 0)
                 | (0x200 if d['in_pool'] else 0)
                 | (0x400 if d['crystal'] in ('Blue', 'Either') else 0)
                 | (0x800 if d['crystal'] in ('Orange', 'Either') else 0)
                 | (0x1000 if d.get('vanilla_teleport') else 0))
        partner = 0xFFFF
        if d.get('dest_door') and d['dest_door'] in builder.door_id:
            partner = builder.door_id[d['dest_door']]
        rows.append('  {%d,%d,%d,0x%x,0x%x,%d,%d,%d,%d,%d,0x%x,0x%x,0x%x,%d,%d,%d,%d}, // %s' % (
            s_off(d['name']), builder.region_id[d['region']], partner, flags,
            d['room'] if d['room'] is not None and d['room'] >= 0 else 0xFF,
            TYPE_ENUM[d['type']], DIR_ENUM[d['direction']],
            max(0, d['door_index']) if d['door_index'] is not None else 0,
            max(0, d['layer']) if d['layer'] is not None else 0,
            d['pos'] if d['pos'] is not None and d['pos'] >= 0 else 0xFF,
            d['pos_byte'], d['kind'], d['trap_flag'] or 0,
            d['quadrant'] if d['quadrant'] is not None else 0,
            clamp_i8(d['shift_x']), clamp_i8(d['shift_y']), d['dungeon'], d['name']))
    C.append('const DoorTblDoor kDoorTblDoors[kDoorTbl_DoorCount] = {\n' + '\n'.join(rows) + '\n};\n\n')

    rows = ['  {%d,%d,%d}, // %s' % (s_off(r['name']), r['dungeon'],
                                     1 if r['crystal_switch'] else 0, r['name'])
            for r in regions]
    C.append('const DoorTblRegion kDoorTblRegions[kDoorTbl_RegionCount] = {\n' + '\n'.join(rows) + '\n};\n\n')

    rows = ['  {%d,%d,0x%x,%d,%d}, // %s' % (e['from'], e['to'], e['rule'], e['door'],
                                             1 if e['cls'] == 'pool' else 0, e['name'])
            for e in builder.edges]
    C.append('const DoorTblEdge kDoorTblEdges[kDoorTbl_EdgeCount] = {\n' + '\n'.join(rows) + '\n};\n\n')

    rows = []
    for a, b in builder.vanilla_two_way:
        rows.append('  {%d,%d,0}, // %s <-> %s' % (builder.door_id[a], builder.door_id[b], a, b))
    for a, b in builder.vanilla_one_way:
        rows.append('  {%d,%d,1}, // %s -> %s' % (builder.door_id[a], builder.door_id[b], a, b))
    C.append('const DoorTblPair kDoorTblVanillaPairs[kDoorTbl_PairCount] = {\n' + '\n'.join(rows) + '\n};\n\n')

    rows = ['  {%d,%d,0x%x}, // %s' % (l['fork_id'], l['region'], l['rule'], l['name'])
            for l in sorted(builder.locations, key=lambda l: l['fork_id'])]
    C.append('const DoorTblLocation kDoorTblLocations[kDoorTbl_LocationCount] = {\n' + '\n'.join(rows) + '\n};\n\n')

    rows = ['  {%d,%d,0x%x}, // %s' % (EVENT_ORDER.index(ev), info['region'], info['rule'], ev)
            for ev, info in sorted(builder.events.items(), key=lambda kv: EVENT_ORDER.index(kv[0]))]
    C.append('const DoorTblEvent kDoorTblEvents[kDoorTbl_EventCount] = {\n' + '\n'.join(rows) + '\n};\n\n')

    rows = ['  {%d,%d,0x%x}, // %s' % (k['dungeon'], k['region'], k['rule'], k['name'])
            for k in builder.drop_keys]
    C.append('const DoorTblDropKey kDoorTblDropKeys[kDoorTbl_DropKeyCount] = {\n' + '\n'.join(rows) + '\n};\n\n')

    rows = ['  {%d,%d,%d,%d,%d,%d,%d}, // %s' % (
        s_off(d['ref_name']), d['chest_small_keys'], 1 if d['has_big_key'] else 0,
        d['region_first'], d['region_count'], d['small_key_item'], d['big_key_item'],
        d['ref_name']) for d in builder.dungeons]
    C.append('const DoorTblDungeon kDoorTblDungeons[kDoorTbl_DungeonCount] = {\n' + '\n'.join(rows) + '\n};\n\n')

    rows = ['  {%d,%d,%d},' % (p['dungeon'], 0 if p['kind'] == 'boss' else 1, p['region'])
            for p in builder.paths]
    C.append('const DoorTblPath kDoorTblPaths[kDoorTbl_PathCount] = {\n' + '\n'.join(rows) + '\n};\n\n')

    rows = ['  {%d,%d,%d}, // %s' % (p['dungeon'], p['is_drop'], p['region'], p['name'])
            for p in builder.portals]
    C.append('const DoorTblPortal kDoorTblPortals[kDoorTbl_PortalCount] = {\n' + '\n'.join(rows) + '\n};\n\n')

    room_rows, rd_rows = [], []
    for room, entries in builder.rooms_sorted:
        room_rows.append('  {0x%x,%d,%d},' % (room, len(entries), len(rd_rows)))
        for pos_byte, kind in entries:
            rd_rows.append('  {0x%x,0x%x},' % (pos_byte, kind))
    C.append('const DoorTblRoom kDoorTblRooms[kDoorTbl_RoomCount] = {\n' + '\n'.join(room_rows) + '\n};\n\n')
    C.append('const DoorTblRoomDoor kDoorTblRoomDoors[] = {\n' + '\n'.join(rd_rows) + '\n};\n\n')

    rows = ['  {%d,%d,%d},' % (a, b, p) for a, b, p in builder.paired]
    C.append('const DoorTblPairedKind kDoorTblPairedKinds[kDoorTbl_PairedKindCount] = {\n' + '\n'.join(rows) + '\n};\n\n')

    blob = ', '.join(str(b) for b in builder.rule_blob)
    C.append('const uint8 kDoorTblRuleBlob[kDoorTbl_RuleBlobSize] = { ' + blob + ' };\n\n')

    esc = ''.join(s.replace('\\', '\\\\').replace('"', '\\"') + '\\0' for s in name_blob)
    C.append('const char kDoorTblNames[] = "' + esc + '";\n')

    with open(out_h, 'w', newline='\n', encoding='utf-8') as f:
        f.write(''.join(H))
    with open(out_c, 'w', newline='\n', encoding='utf-8') as f:
        f.write(''.join(C))
    with open(out_preds, 'w', newline='\n', encoding='utf-8') as f:
        json.dump({
            'format_version': 1,
            # Unique fork-DSL strings referenced by kDoorRuleOp_Vm leaves;
            # rando_logic_gen.py compiles them into kRandoPredicateStream and
            # emits kDoorVmPreds[{off,len}] in matching order.
            'predicates': builder.vm_preds,
            # Fork locations under door-shuffle control: rando_logic_gen wraps
            # each location's can_reach as
            #   (NOT DOORS_ACTIVE(d) AND vanilla) OR
            #   (DOORS_ACTIVE(d) AND DOORS_LOC_REACHABLE(loc))
            'locations': [{'fork_id': l['fork_id'],
                           'dungeon': builder.regions[l['region']]['dungeon']}
                          for l in sorted(builder.locations, key=lambda l: l['fork_id'])],
            # Portal lobbies (door-table region ids) for the oracle's seeding
            # gates; joined with the hand-curated door_portals.yaml by name.
            'portals': [{'name': p['name'], 'dungeon': p['dungeon'],
                         'door_region': p['region'], 'is_drop': p['is_drop']}
                        for p in builder.portals],
            'dungeons': [d['fork_name'] for d in builder.dungeons],
        }, f, indent=1)


def clamp_i8(v):
    v = int(v)
    return max(-128, min(127, v if v < 128 else v - 256)) if v > 127 else v


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
    ap.add_argument('--check-rules', action='store_true')
    ap.add_argument('--init-registry', action='store_true')
    ap.add_argument('--no-emit', action='store_true')
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
    with open(args.out_json, 'w', newline='\n', encoding='utf-8') as f:
        json.dump(model, f, indent=1, sort_keys=True)
    print(f'gen_door_tables: wrote {args.out_json}')

    if args.dump_stats:
        dump_stats(model)

    if args.check_rules:
        tr = RuleTranslator(model)
        n_vm = n_mixed = 0
        region_dgn = {r['name']: r['dungeon'] for r in model['regions']}
        for e in model['edges'] + model['locations'] :
            dgn = region_dgn.get(e.get('from') or e.get('region'))
            ir = tr.translate_entry_list(e['name'], e['rules'], dgn)
            ir = fold_vm(ir)
            if ir[0] == 'vm':
                n_vm += 1
            elif ir[0] not in ('true', 'false'):
                n_mixed += 1
        print(f'rule check: vm-only={n_vm} mixed={n_mixed} errors={len(tr.errors)}')
        for spot, body, msg in tr.errors[:30]:
            print(f'  ERR {spot}: {msg}\n      {body[:160]}')
        return 1 if tr.errors else 0

    if args.no_emit:
        return 0

    builder = TableBuilder(model, REPO).build()
    problems = list(builder.problems)
    for spot, body, msg in builder.tr_errors:
        problems.append(f'rule error at {spot}: {msg}')
    registry_path = os.path.join(REPO, 'assets', 'rando', 'door_registry.yaml')
    problems += check_registry(builder, registry_path, args.init_registry)
    if problems:
        print(f'gen_door_tables: {len(problems)} problem(s):', file=sys.stderr)
        for p in problems[:40]:
            print('  ' + p, file=sys.stderr)
        return 1

    out_h = os.path.join(REPO, 'src', 'rando', 'door_tables.gen.h')
    out_c = os.path.join(REPO, 'src', 'rando', 'door_tables.gen.c')
    out_preds = os.path.join(REPO, 'assets', 'rando', 'door_predicates.gen.json')
    emit_c(builder, out_h, out_c, out_preds)
    print(f'gen_door_tables: emitted {out_c}')
    print(f'  doors={len(builder.doors)} regions={len(builder.regions)} '
          f'edges={len(builder.edges)} locations={len(builder.locations)} '
          f'events={len(builder.events)} drop_keys={len(builder.drop_keys)} '
          f'vm_preds={len(builder.vm_preds)} rule_blob={len(builder.rule_blob)}B')
    return 0


if __name__ == '__main__':
    sys.exit(main())
