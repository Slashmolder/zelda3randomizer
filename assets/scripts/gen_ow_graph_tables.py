#!/usr/bin/env python3
"""gen_ow_graph_tables.py — extract the overworld screen-component graph from
the community OW randomizer (aerinon/ALttPDoorRandomizer, branch
OverworldShuffleDev, MIT) and emit assets/rando/ow_graph.gen.yaml for
rando_logic_gen.py.

Per openspec/changes/add-rando-ow-warp-shuffle design D2: everything is parsed
from upstream source via `git show` + ast literal extraction — never executed,
never hand-transcribed. The emitted YAML is gitignored (generated artifact).

Data extracted (upstream file: structure):
  OWEdges.py:            OWTileRegions (component name -> screen, bidict),
                         create_owedges rows (edge name -> screen; for
                         edge->region binding via Regions.py exits)
  OverworldShuffle.py:   default_connections (vanilla edge wiring),
                         mandatory_connections (intra-screen links),
                         one_way_ledges (landing -> {drop sources}),
                         isolated_regions (walk-in-less components),
                         ow_connections (teleporter hosting),
                         default_whirlpool_connections
  Regions.py:            create_lw_region/create_dw_region rows
                         (region -> exits list [binds edges/connections],
                          terrain=Terrain.Water flag)
  source/overworld/FluteShuffle.py: flute_data (candidate -> LW region +
                         arrival tableau + minimap icon columns)
Fork-side input:
  assets/scripts/gen_terrain_tables.py: SCREEN_REGIONS (screen -> (zone, tag,
  note)) — the COMPLETE 80-screen map from the terrain feature; the
  enemy-check OVERWORLD_REGIONS only covers screens with enemy candidates.

Runtime-edge model emitted for rando_logic_gen (design D1 as refined during
implementation — see the D1 reconciliation note in design.md):
  comp->zone: unconditional, EVERY component (standing on a component implies
              zone egress by walking or dropping; asserted below via the
              egress check)
  zone->comp: ONLY for the 8 whirlpool water components (HAS_ITEM(Flippers))
              — general zone->component membership is deliberately absent:
              ledges like Bumper Cave Ledge are walked onto from a DIFFERENT
              zone across screen edges, so "zone grants component" is wrong
              for exactly the components that matter, and nothing in warp
              shuffle needs the direction (spots/drops/portals point AT
              components; walk-reachable teleporters keep their existing
              hand-written zone edges).
  drops:      directed source-component -> landing-component
  portals:    teleporter-hosting component -> opposite-world zone (predicate
              resolved by rando_logic_gen from the fork's zone edges,
              CanFly-stripped per design D1)
Cross-zone component ADJACENCY is deliberately NOT a runtime edge class: some
upstream intra/inter-screen links are item-gated only by source comments
(#hammer/#mitts), so ungated adjacency edges would bypass the hand-written
zone-edge predicates. Adjacency is emitted only for the sector flood and the
quotient cross-check (witness set).
"""

import argparse
import ast
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
OUT_PATH = REPO_ROOT / "assets" / "rando" / "ow_graph.gen.yaml"
FORK_ZONES_PATH = REPO_ROOT / "assets" / "scripts" / "gen_terrain_tables.py"

PSEUDO_SCREEN_MIN = 0x80
VANILLA_FLUTE_SLOTS = 8
WHIRLPOOL_COUNT = 8


def die(msg):
    print(f"gen_ow_graph_tables: FATAL: {msg}", file=sys.stderr)
    sys.exit(2)


def git_show(upstream, ref, path):
    r = subprocess.run(["git", "-C", str(upstream), "show", f"{ref}:{path}"],
                       capture_output=True, text=True)
    if r.returncode != 0:
        die(f"git show {ref}:{path} failed: {r.stderr.strip()}")
    return r.stdout


# ---------------------------------------------------------------- ast helpers

def parse_module(source, label):
    try:
        return ast.parse(source)
    except SyntaxError as e:
        die(f"cannot parse {label}: {e}")


def top_level_assign(tree, name, label):
    for node in tree.body:
        if isinstance(node, ast.Assign) and len(node.targets) == 1 and \
           isinstance(node.targets[0], ast.Name) and node.targets[0].id == name:
            return node.value
    die(f"{label}: no top-level assignment `{name}`")


def literal(node, label):
    """literal_eval with bidict(...)/set unwrapping."""
    if isinstance(node, ast.Call) and isinstance(node.func, ast.Name) and \
       node.func.id == "bidict":
        if len(node.args) != 1:
            die(f"{label}: bidict() with != 1 arg")
        node = node.args[0]
    try:
        return ast.literal_eval(node)
    except (ValueError, SyntaxError) as e:
        die(f"{label}: not literal-evaluable: {e}")


def extract_literal(tree, name, label):
    return literal(top_level_assign(tree, name, label), f"{label}.{name}")


def call_rows(tree, func_names):
    """Yield every Call node anywhere in the tree whose callee name matches."""
    for node in ast.walk(tree):
        if isinstance(node, ast.Call):
            f = node.func
            callee = f.id if isinstance(f, ast.Name) else (
                f.attr if isinstance(f, ast.Attribute) else None)
            if callee in func_names:
                yield callee, node


def const(node, default=None):
    if node is None:
        return default
    try:
        return ast.literal_eval(node)
    except (ValueError, SyntaxError):
        return default


# ---------------------------------------------------------------- extraction

def extract_components(owedges_tree):
    """OWTileRegions: component name -> screen id. Pseudo screens split out."""
    raw = extract_literal(owedges_tree, "OWTileRegions", "OWEdges.py")
    comps, pseudo = {}, {}
    for name, screen in raw.items():
        if not isinstance(name, str) or not isinstance(screen, int):
            die(f"OWTileRegions entry not (str, int): {name!r}: {screen!r}")
        (pseudo if screen >= PSEUDO_SCREEN_MIN else comps)[name] = screen
    if not comps:
        die("OWTileRegions produced zero non-pseudo components")
    return comps, pseudo


def extract_edges(owedges_tree):
    """create_owedge(player, name, owID, ...) rows -> edge name -> screen."""
    edges = {}
    for _, call in call_rows(owedges_tree, {"create_owedge"}):
        if len(call.args) < 3:
            continue
        name = const(call.args[1])
        screen = const(call.args[2])
        if isinstance(name, str) and isinstance(screen, int):
            if name in edges:
                die(f"duplicate edge declaration: {name}")
            edges[name] = screen
    if not edges:
        die("no create_owedge rows found")
    return edges


def extract_regions(regions_tree):
    """create_lw_region/create_dw_region -> name -> (exits[], is_water)."""
    out = {}
    for callee, call in call_rows(regions_tree,
                                  {"create_lw_region", "create_dw_region"}):
        if len(call.args) < 2:
            continue
        name = const(call.args[1])
        if not isinstance(name, str):
            continue
        exits = const(call.args[3], []) if len(call.args) > 3 else []
        if exits is None:
            exits = []
        is_water = False
        # terrain is either the 6th positional or a keyword; it is an
        # Attribute (Terrain.Water) so literal_eval fails -> inspect the ast.
        tnode = call.args[5] if len(call.args) > 5 else None
        for kw in call.keywords:
            if kw.arg == "terrain":
                tnode = kw.value
        if isinstance(tnode, ast.Attribute) and tnode.attr == "Water":
            is_water = True
        if name in out:
            die(f"duplicate region declaration: {name}")
        out[name] = (list(exits) if isinstance(exits, list) else [], is_water)
    if not out:
        die("no create_*_region rows found")
    return out


def extract_flute(flute_tree):
    """flute_data: owid -> row. Row = ([LW region, DW region], Slot, VRAM,
    BGY, BGX, LinkY, LinkX, CamY, CamX, Unk1, Unk2, IconY, IconX [, Alt...]).
    Alt columns (tile-swap variants) are ignored at v1 (Inverted is out of
    scope)."""
    raw = extract_literal(flute_tree, "flute_data", "FluteShuffle.py")
    tableau_keys = ["slot", "vram", "bg_y", "bg_x", "link_y", "link_x",
                    "cam_y", "cam_x", "unk1", "unk2", "icon_y", "icon_x"]
    out = {}
    for owid, row in raw.items():
        if not isinstance(owid, int) or not isinstance(row, tuple):
            die(f"flute_data[{owid!r}]: unexpected shape")
        regions, rest = row[0], row[1:]
        if not (isinstance(regions, list) and len(regions) >= 1 and
                isinstance(regions[0], str)):
            die(f"flute_data[{owid:#04x}]: region list malformed")
        if len(rest) < len(tableau_keys):
            die(f"flute_data[{owid:#04x}]: {len(rest)} cols < "
                f"{len(tableau_keys)} required")
        entry = {"screen": owid, "lw_region": regions[0]}
        for k, v in zip(tableau_keys, rest):
            if not isinstance(v, int):
                die(f"flute_data[{owid:#04x}].{k}: non-int {v!r}")
            entry[k] = v
        out[owid] = entry
    if not out:
        die("flute_data empty")
    return out


def extract_forced_flute(flute_source):
    """The Desert/Mire guarantee: upstream force-includes screen 0x30. Locate
    the forced set from source rather than hardcoding: accept any int literal
    in a line containing 'force' and 'flute' context; fail if none found so an
    upstream change surfaces loudly."""
    forced = set()
    for line in flute_source.splitlines():
        low = line.lower()
        if "0x30" in low and ("force" in low or "guarantee" in low):
            forced.add(0x30)
    if not forced:
        die("could not locate the forced Desert/Mire flute guarantee in "
            "FluteShuffle.py — upstream shape changed; re-ground the port")
    return sorted(forced)


def resolve_pairs(pairs, edge_screen, region_exits, comps, label):
    """Map (nameA, nameB) pairs to component pairs. Names may be edge names
    (bound to a region via Regions.py exits) or exit/connection names (bound
    the same way). Pairs where either side doesn't resolve to a non-pseudo
    component are returned in `skipped` for reporting."""
    owner = {}
    for region, (exits, _w) in region_exits.items():
        for e in exits:
            if e in owner and owner[e] != region:
                die(f"exit `{e}` claimed by both {owner[e]} and {region}")
            owner[e] = region
    resolved, skipped = [], []
    for a, b in pairs:
        ra = owner.get(a, a if a in comps else None)
        rb = owner.get(b, b if b in comps else None)
        if ra in comps and rb in comps:
            resolved.append((ra, rb, a, b))
        else:
            skipped.append((a, b))
    if not resolved:
        die(f"{label}: nothing resolved")
    return resolved, skipped, owner


def flood_sectors(comps, undirected, drops, water_names):
    """Reproduce upstream build_sectors' topological flood (design D2): flood
    over undirected adjacency + drops treated one-way, no portals/mirror/
    flute; then drop Water components (no-starting-Flippers branch)."""
    nbrs = {c: set() for c in comps}
    for a, b in undirected:
        nbrs[a].add(b)
        nbrs[b].add(a)
    for src, dst in drops:
        nbrs[src].add(dst)  # one-way: source reaches landing
    sectors, seen = [], set()
    for start in sorted(comps):
        if start in seen:
            continue
        stack, sector = [start], set()
        while stack:
            c = stack.pop()
            if c in sector:
                continue
            sector.add(c)
            stack.extend(n for n in nbrs[c] if n not in sector)
        # one-way drops make this an over-merge in one direction only; the
        # upstream flood has the same property (build_accessible_region_list
        # walks exits, which include drops).
        seen |= sector
        land = sorted(c for c in sector if c not in water_names)
        if land:
            sectors.append(land)
    return sectors


def fnv1a(data):
    h = 0x811C9DC5
    for b in data:
        h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
    return h


# ---------------------------------------------------------------- main

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--upstream", default=str(REPO_ROOT.parent.parent /
                                              "ALttPDoorRandomizer"))
    ap.add_argument("--ref", default="origin/OverworldShuffleDev")
    ap.add_argument("--out", default=str(OUT_PATH))
    args = ap.parse_args()

    upstream = Path(args.upstream)
    # The default above resolves from the worktree; fall back to the sibling
    # of the MAIN checkout when the worktree nests under .claude/worktrees.
    if not (upstream / "OWEdges.py").exists() and \
       not subprocess.run(["git", "-C", str(upstream), "rev-parse"],
                          capture_output=True).returncode == 0:
        cand = Path("C:/src/ALttPDoorRandomizer")
        if cand.exists():
            upstream = cand
        else:
            die(f"upstream checkout not found at {args.upstream}")

    ref_hash = subprocess.run(
        ["git", "-C", str(upstream), "rev-parse", args.ref],
        capture_output=True, text=True)
    if ref_hash.returncode != 0:
        die(f"cannot resolve {args.ref} in {upstream}")
    ref_hash = ref_hash.stdout.strip()

    src_owedges = git_show(upstream, args.ref, "OWEdges.py")
    src_shuffle = git_show(upstream, args.ref, "OverworldShuffle.py")
    src_regions = git_show(upstream, args.ref, "Regions.py")
    src_flute = git_show(upstream, args.ref, "source/overworld/FluteShuffle.py")

    t_owedges = parse_module(src_owedges, "OWEdges.py")
    t_shuffle = parse_module(src_shuffle, "OverworldShuffle.py")
    t_regions = parse_module(src_regions, "Regions.py")
    t_flute = parse_module(src_flute, "FluteShuffle.py")

    comps, pseudo = extract_components(t_owedges)
    edge_screen = extract_edges(t_owedges)
    region_exits = extract_regions(t_regions)
    flute = extract_flute(t_flute)
    forced_flute = extract_forced_flute(src_flute)

    default_conns = extract_literal(t_shuffle, "default_connections",
                                    "OverworldShuffle.py")
    mandatory = extract_literal(t_shuffle, "mandatory_connections",
                                "OverworldShuffle.py")
    one_way = extract_literal(t_shuffle, "one_way_ledges",
                              "OverworldShuffle.py")
    isolated = extract_literal(t_shuffle, "isolated_regions",
                               "OverworldShuffle.py")
    ow_conns = extract_literal(t_shuffle, "ow_connections",
                               "OverworldShuffle.py")
    whirl_raw = extract_literal(t_shuffle, "default_whirlpool_connections",
                                "OverworldShuffle.py")

    # Fork zone map (screen -> zone id string), ast-parsed like the rest.
    # SCREEN_REGIONS values are (zone, source_tag, note) tuples.
    t_fork = parse_module(FORK_ZONES_PATH.read_text(encoding="utf-8"),
                          str(FORK_ZONES_PATH))
    screen_regions = extract_literal(t_fork, "SCREEN_REGIONS",
                                     str(FORK_ZONES_PATH))
    zone_of_screen = {s: v[0] for s, v in screen_regions.items()}

    # --- component -> zone ---------------------------------------------------
    zone_of = {}
    missing_zone = []
    for name, screen in comps.items():
        z = zone_of_screen.get(screen)
        if z is None:
            missing_zone.append((name, screen))
        else:
            zone_of[name] = z
    if missing_zone:
        die(f"{len(missing_zone)} components on screens with no fork zone "
            f"binding, e.g. {missing_zone[:4]}")

    # --- adjacency (witness/sector use only; NOT runtime edges) -------------
    adj, adj_skipped, exit_owner = resolve_pairs(
        [(a, b) for a, b in default_conns], edge_screen, region_exits, comps,
        "default_connections")
    mand, mand_skipped, _ = resolve_pairs(
        [(a, b) for a, b in mandatory], edge_screen, region_exits, comps,
        "mandatory_connections")
    undirected = sorted({tuple(sorted((ra, rb)))
                         for ra, rb, _a, _b in adj + mand if ra != rb})
    cross_zone = sorted(p for p in undirected
                        if zone_of[p[0]] != zone_of[p[1]])
    intra_zone = sorted(p for p in undirected
                        if zone_of[p[0]] == zone_of[p[1]])

    # --- drops (directed, runtime) ------------------------------------------
    drops = []
    for landing, sources in one_way.items():
        if landing not in comps:
            continue  # pseudo/water-of-pseudo landings excluded with screens
        for src in sources:
            if src in comps:
                drops.append((src, landing))
    drops.sort()

    # --- isolation flag + egress assert -------------------------------------
    # upstream_isolated is informational (upstream's flute ignore-flood set);
    # it does NOT mean "no adjacency" — ledges span screens (Bumper Cave
    # Ledge is walked onto from Death Mountain across a screen edge).
    touched = {c for p in undirected for c in p}
    drop_sources = {src for src, _d in drops}
    upstream_isolated = sorted(c for c in comps if c in set(isolated))

    # --- ow_connections: directed links + teleporters ------------------------
    # ow_connections carries approaches/drops/bridges/leaves AND teleporters
    # as (connection name, target region) rows. Non-teleporter rows are
    # egress evidence + sector-flood data (never runtime edges — some are
    # comment-gated upstream); teleporter rows feed the portal class.
    ow_links = []           # (host_comp, target_comp) directed
    ow_links_unresolved = 0
    all_teleporters = []
    for screen, groups in ow_conns.items():
        if not isinstance(groups, tuple):
            die(f"ow_connections[{screen:#04x}] not a tuple")
        for group in groups:
            for name, target in group:
                host = exit_owner.get(name)
                is_tele = "Teleporter" in name
                if host is None or host not in comps or target not in comps:
                    if is_tele:
                        die(f"teleporter `{name}` host/target unresolvable "
                            f"(host={host!r}, target={target!r})")
                    ow_links_unresolved += 1
                    continue
                ow_links.append((host, target))
                if is_tele:
                    all_teleporters.append({
                        "name": name, "host": host,
                        "host_screen": comps[host],
                        "host_zone": zone_of[host],
                        "target_component": target,
                        "target_zone": zone_of[target],
                    })
    if not all_teleporters:
        die("no teleporters extracted")

    # --- whirlpools ----------------------------------------------------------
    whirlpools = []
    for pair in whirl_raw:
        (sa, na, ra), (sb, nb, rb) = pair
        for s, n, r in ((sa, na, ra), (sb, nb, rb)):
            if r not in comps:
                die(f"whirlpool water region `{r}` not a component")
        whirlpools.append({
            "a": {"screen": sa, "name": na, "component": ra,
                  "zone": zone_of[ra]},
            "b": {"screen": sb, "name": nb, "component": rb,
                  "zone": zone_of[rb]},
        })
    lw_wp = [w for w in whirlpools
             if w["a"]["screen"] < 0x40 and w["b"]["screen"] < 0x40]
    dw_wp = [w for w in whirlpools
             if w["a"]["screen"] >= 0x40 and w["b"]["screen"] >= 0x40]

    # --- flute candidates ----------------------------------------------------
    flute_out = []
    for owid in sorted(flute):
        e = flute[owid]
        if owid >= 0x40:
            die(f"flute candidate {owid:#04x} not a LW screen")
        if e["lw_region"] not in comps:
            die(f"flute candidate {owid:#04x} region `{e['lw_region']}` "
                f"not a component")
        e = dict(e)
        e["component"] = e.pop("lw_region")
        e["zone"] = zone_of[e["component"]]
        e["forced"] = owid in forced_flute
        e["upstream_isolated"] = e["component"] in set(upstream_isolated)
        flute_out.append(e)

    # vanilla flute spots: upstream default_flute_connections
    vanilla_flute = extract_literal(t_flute, "default_flute_connections",
                                    "FluteShuffle.py") \
        if any(isinstance(n, ast.Assign) and
               getattr(n.targets[0], "id", None) == "default_flute_connections"
               for n in t_flute.body) else None
    if vanilla_flute is None:
        die("default_flute_connections not found in FluteShuffle.py")
    vanilla_flute = sorted(vanilla_flute)

    # --- water terrain set ---------------------------------------------------
    water_names = sorted(n for n, (_e, w) in region_exits.items()
                         if w and n in comps)

    # --- sectors -------------------------------------------------------------
    # The upstream flood walks region exits INCLUDING the ow_connections
    # links but EXCLUDING portals/mirror/flute (build_sectors' contract) —
    # teleporter links must not merge LW/DW sectors.
    tele_names = {t["name"] for t in all_teleporters}
    tele_pairs = {(t["host"], t["target_component"]) for t in all_teleporters}
    ow_links_nontele = [p for p in ow_links if p not in tele_pairs]
    sectors = flood_sectors(comps, undirected, drops + ow_links_nontele,
                            set(water_names))
    sector_of = {}
    for si, sec in enumerate(sectors):
        for c in sec:
            sector_of[c] = si

    # --- balanced-mode conflict relation (upstream getIgnored port) ----------
    # EXACT port of FluteShuffle.py getIgnored (round-2 audit LOW-1 — the
    # first cut was a 1-hop approximation): from the spot's region, follow
    # region exits recursively, but a step to screen ts is allowed ONLY when
    # ts is the BASE screen, ts is the CURRENT frame's screen, or the
    # current region itself sits on the base screen (i.e. base screen +
    # regions one screen out + same-screen closure at each frame) — PLUS
    # unbounded recursion up one-way-ledge SOURCE chains (the upstream
    # table maps landing -> {sources}), each with its own screen frame.
    # Candidates conflict iff their floods overlap (the accumulated-set
    # pairwise reduction).
    same_screen = {}
    for n, s in comps.items():
        same_screen.setdefault(s, set()).add(n)
    out_nbrs = {c: set() for c in comps}   # region "exits": adjacency both
    for a, b in undirected:                # ways + directed drops/ow-links
        out_nbrs[a].add(b)
        out_nbrs[b].add(a)
    for s_, d_ in drops + ow_links_nontele:
        out_nbrs[s_].add(d_)
    ledge_sources = {}
    for landing, sources in one_way.items():
        if landing in comps:
            ledge_sources[landing] = [s for s in sources if s in comps]

    def _flood(start):
        base = comps[start]
        seen = {start}
        stack = [(start, base)]
        while stack:
            r, owid = stack.pop()
            for t in (out_nbrs[r] | same_screen[comps[r]]):
                if t in seen:
                    continue
                ts = comps[t]
                if ts == base or ts == owid or comps[r] == base:
                    seen.add(t)
                    stack.append((t, ts))
            for src in ledge_sources.get(r, ()):
                if src not in seen:
                    seen.add(src)
                    stack.append((src, comps[src]))
        return seen

    nbhd = {c: _flood(c) for c in comps}
    cand_list = sorted(flute)  # candidate screens
    conflicts = {}
    for i in cand_list:
        ci = flute[i]["lw_region"]
        conflicts[i] = sorted(
            j for j in cand_list
            if j != i and nbhd[ci] & nbhd[flute[j]["lw_region"]])

    # --- self-asserts (design D2) -------------------------------------------
    # Egress: comp->zone is emitted ONLY for components with some outdoor
    # egress (undirected adjacency, a drop out, an ow_connections outbound
    # link, a teleporter, or whirlpool travel). The remainder — ledges whose
    # only exit is a cave entrance, enclosed waters — become STUBS: no
    # comp->zone edge (logic-inert), and no warp target may land on one.
    tele_hosts = {t["host"] for t in all_teleporters}
    wp_comps = {w[s]["component"] for w in whirlpools for s in ("a", "b")}
    ow_out = {h for h, _t in ow_links}
    stubs = sorted(c for c in comps
                   if c not in touched and c not in drop_sources and
                   c not in ow_out and c not in tele_hosts and
                   c not in wp_comps)
    stub_set = set(stubs)
    landable = ({e["component"] for e in flute_out} | wp_comps |
                {dst for _s, dst in drops} |
                {t["target_component"] for t in all_teleporters
                 if t["host"] in {e["component"] for e in flute_out}})
    bad_stubs = sorted(stub_set & landable)
    if bad_stubs:
        die(f"warp-landable components have no egress: {bad_stubs}")

    # Portal edges only for the class they exist for (design D1): teleporters
    # hosted on flute-candidate components (flute-critical walk-in-less
    # ledges). Walk-reachable teleporters keep their hand-written zone edges;
    # a generic host->target-ZONE edge would over-grant for enclosed targets
    # (e.g. an island teleporter must not grant its island's whole zone).
    cand_comps = {e["component"] for e in flute_out}
    teleporters = sorted((t for t in all_teleporters
                          if t["host"] in cand_comps),
                         key=lambda t: (t["host_screen"], t["name"]))
    for t in teleporters:
        if t["target_component"] in stub_set:
            die(f"portal target `{t['target_component']}` is an egress-less "
                f"stub — portal edge would grant an unleavable spot's zone")
    if len(whirlpools) * 2 != WHIRLPOOL_COUNT:
        die(f"whirlpool count {len(whirlpools)*2} != {WHIRLPOOL_COUNT}")
    if len(lw_wp) != 3 or len(dw_wp) != 1:
        die(f"whirlpool world split LW pairs={len(lw_wp)} DW pairs={len(dw_wp)}"
            f" != 3/1")
    if len(vanilla_flute) != VANILLA_FLUTE_SLOTS:
        die(f"vanilla flute spots {len(vanilla_flute)} != 8")
    for owid in vanilla_flute:
        if owid not in flute:
            die(f"vanilla flute spot {owid:#04x} missing from flute_data")
    for owid in forced_flute:
        if owid not in flute:
            die(f"forced flute screen {owid:#04x} missing from flute_data")
    for e in flute_out:
        if e["component"] not in sector_of:
            die(f"flute candidate 0x{e['screen']:02x} component "
                f"{e['component']!r} is outside every sector")
    covered = {s for s in comps.values()}
    all_lw_dw = {s for s in zone_of_screen if s < PSEUDO_SCREEN_MIN}
    uncovered = sorted(all_lw_dw - covered)
    if uncovered:
        die(f"screens with no component: {[hex(s) for s in uncovered]}")
    dupes = len(comps) != len(set(comps))
    if dupes:
        die("component names not unique")

    # --- emit ----------------------------------------------------------------
    lines = []
    w = lines.append
    w("# GENERATED by assets/scripts/gen_ow_graph_tables.py — do not edit.")
    w(f"# upstream: {args.ref} @ {ref_hash}")
    w("format_version: 1")
    w("components:")
    for name in sorted(comps):
        w(f"  - name: {name!r}")
        w(f"    screen: 0x{comps[name]:02x}")
        w(f"    zone: {zone_of[name]}")
        w(f"    water: {str(name in water_names).lower()}")
        w(f"    upstream_isolated: {str(name in set(upstream_isolated)).lower()}")
        w(f"    stub: {str(name in stub_set).lower()}")
    w("drops:")
    for src, dst in drops:
        w(f"  - [{src!r}, {dst!r}]")
    w("adjacency_intra_zone:")
    for a, b in intra_zone:
        w(f"  - [{a!r}, {b!r}]")
    w("adjacency_cross_zone:  # witness-only; never runtime edges (see header)")
    for a, b in cross_zone:
        w(f"  - [{a!r}, {b!r}]")
    w("teleporters:")
    for t in teleporters:
        w(f"  - name: {t['name']!r}")
        w(f"    host: {t['host']!r}")
        w(f"    host_screen: 0x{t['host_screen']:02x}")
        w(f"    host_zone: {t['host_zone']}")
        w(f"    target_component: {t['target_component']!r}")
        w(f"    target_zone: {t['target_zone']}")
    w("whirlpools:")
    for wp in whirlpools:
        for side in ("a", "b"):
            s = wp[side]
            w(f"  - screen: 0x{s['screen']:02x}" if side == "a"
              else f"    partner_screen: 0x{s['screen']:02x}")
            if side == "a":
                w(f"    name: {s['name']!r}")
                w(f"    component: {s['component']!r}")
                w(f"    zone: {s['zone']}")
            else:
                w(f"    partner_name: {s['name']!r}")
                w(f"    partner_component: {s['component']!r}")
                w(f"    partner_zone: {s['zone']}")
    w("flute_candidates:")
    for e in flute_out:
        w(f"  - screen: 0x{e['screen']:02x}")
        w(f"    component: {e['component']!r}")
        w(f"    zone: {e['zone']}")
        w(f"    forced: {str(e['forced']).lower()}")
        w(f"    upstream_isolated: {str(e['upstream_isolated']).lower()}")
        w(f"    sector: {sector_of[e['component']]}")
        w(f"    conflicts: [{', '.join(f'0x{c:02x}' for c in conflicts[e['screen']])}]")
        for k in ("slot", "vram", "bg_y", "bg_x", "link_y", "link_x",
                  "cam_y", "cam_x", "unk1", "unk2", "icon_y", "icon_x"):
            w(f"    {k}: 0x{e[k] & 0xFFFF:04x}")
    w(f"vanilla_flute_screens: [{', '.join(f'0x{s:02x}' for s in vanilla_flute)}]")
    w("sectors:")
    for s in sectors:
        w(f"  - [{', '.join(repr(c) for c in s)}]")
    w("skipped:")
    w(f"  default_connections: {len(adj_skipped)}")
    w(f"  mandatory_connections: {len(mand_skipped)}")

    body = "\n".join(lines) + "\n"
    digest = fnv1a(body.encode("utf-8"))
    body += f"identity_digest: 0x{digest:08x}\n"
    with open(args.out, "w", encoding="utf-8", newline="\n") as f:
        f.write(body)

    print(f"gen_ow_graph_tables: {len(comps)} components "
          f"({len(upstream_isolated)} upstream-isolated, {len(water_names)} "
          f"water, {len(stubs)} stubs), {len(drops)} drops, "
          f"{len(ow_links)} ow-links (+{ow_links_unresolved} unresolved), "
          f"{len(intra_zone)}+{len(cross_zone)} intra/cross-zone adjacency, "
          f"{len(teleporters)}/{len(all_teleporters)} portal/all teleporters, "
          f"{len(whirlpools)} whirlpool pairs, {len(flute_out)} flute "
          f"candidates ({len(forced_flute)} forced), {len(sectors)} sectors, "
          f"skipped {len(adj_skipped)}/{len(mand_skipped)} conn pairs")
    if stubs:
        print(f"gen_ow_graph_tables: stubs (no comp->zone edge): {stubs}")
    print(f"gen_ow_graph_tables: wrote {args.out} (digest 0x{digest:08x}, "
          f"upstream {ref_hash[:12]})")


if __name__ == "__main__":
    main()
