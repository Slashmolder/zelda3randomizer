#!/usr/bin/env python3
"""Generate the enemy-shuffle constraint tables from the Enemizer (MIT) source.

Parses C:\\src\\Enemizer SpriteRequirement.cs + SpriteConstants.cs and emits C
initializers for src/rando/shuffle_enemies.c:
  - kSheetNeed[256]   per-type bit7=KNOWN | bits3..0 = needs subgroup slot 3..0
  - kEsBossTypes[]    boss/secondary ids (<=0xF2)
  - kEnemyTable rows  randomizable enemies + their REQUIRED sheets (ALL slots) + flags
  - per-slot reshuffle pools (dungeon: a single-slot killable+key enemy exists;
    overworld: any randomizable enemy exists)

We parse the SOURCE, not a hand summary — re-transcription dropped sheet 68 from
Walking Zora (the "hybrid zora+buzzblob" bug) and 0x16/0xBC from the pin set.

Run:  python assets/scripts/gen_enemy_shuffle_tables.py [ENEMIZER_DIR]
Paste the printed blocks into shuffle_enemies.c (they are structural sprite-id /
sheet-id facts — allowed inline; not extracted ROM data).
"""
import re, sys, os

ENZ = sys.argv[1] if len(sys.argv) > 1 else r"C:\src\Enemizer"
REQ = os.path.join(ENZ, "EnemizerLibrary", "EnemyRandomizer", "SpriteRequirement.cs")
CON = os.path.join(ENZ, "EnemizerLibrary", "EnemyRandomizer", "SpriteConstants.cs")

# Enemizer's per-position candidate-sheet whitelists (PotentialSubsetN). DISJOINT.
WHITELIST = {
    0: [22, 31, 47, 14],
    1: [44, 30, 32],
    2: [12, 18, 23, 24, 28, 46, 34, 35, 39, 40, 38, 41, 36, 37, 42],
    3: [17, 16, 27, 20, 82, 83],
}

def strip_comments(text):
    # remove // line comments and /* */ blocks (no // inside string literals here)
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return "\n".join(re.sub(r"//.*$", "", ln) for ln in text.splitlines())

# --- id map from SpriteConstants.cs ---
con = strip_comments(open(CON, encoding="utf-8", errors="ignore").read())
idmap = {}
for m in re.finditer(r"(\w+)\s*=\s*(0x[0-9A-Fa-f]+)", con):
    idmap[m.group(1)] = int(m.group(2), 16)

# --- parse each SpriteRequirements.Add( ... ); block (paren-balanced) ---
req = strip_comments(open(REQ, encoding="utf-8", errors="ignore").read())
blocks = []
i = 0
needle = "SpriteRequirements.Add("
while True:
    j = req.find(needle, i)
    if j < 0:
        break
    k = j + len(needle)
    depth = 1
    while k < len(req) and depth:
        if req[k] == "(":
            depth += 1
        elif req[k] == ")":
            depth -= 1
        k += 1
    blocks.append(req[j:k])
    i = k

FLAG_SET = ("SetBoss", "SetNPC", "SetIsObject", "SetObject", "SetOverlord",
            "SetNeverUse", "SetNeverUseDungeon", "SetNeverUseOverworld",
            "SetDoNotRandomize", "SetKillable", "SetCannotHaveKey", "SetWaterSprite")

# id -> dict(flags set, subs {0:[..],...}, name)  (last write wins for dup ids like 0xBB)
ents = {}
for b in blocks:
    nm = re.search(r"SpriteConstants\.(\w+)", b)
    if not nm:
        continue
    name = nm.group(1)
    if name not in idmap:
        continue
    sid = idmap[name]
    flags = set(f for f in FLAG_SET if re.search(r"\." + f + r"\s*\(", b))
    subs = {0: [], 1: [], 2: [], 3: []}
    for sm in re.finditer(r"AddSubgroup([0-3])\s*\(([^)]*)\)", b):
        pos = int(sm.group(1))
        ids = [int(x) for x in re.findall(r"\d+", sm.group(2))]
        subs[pos] = ids
    grp = re.search(r"AddGroup\s*\(([^)]*)\)", b)
    e = ents.setdefault(sid, {"name": name, "flags": set(), "subs": {0: [], 1: [], 2: [], 3: []}, "group": None})
    e["flags"] |= flags
    for p in range(4):
        if subs[p]:
            e["subs"][p] = subs[p]  # last non-empty wins (handles 0xBB variants)
    if grp:
        e["group"] = int(re.findall(r"\d+", grp.group(1))[0]) if re.findall(r"\d+", grp.group(1)) else None

def is_boss(e):     return "SetBoss" in e["flags"]
def is_npc(e):      return "SetNPC" in e["flags"]
def is_object(e):   return "SetIsObject" in e["flags"] or "SetObject" in e["flags"]
def is_overlord(e): return "SetOverlord" in e["flags"]
def dnr(e):         return "SetDoNotRandomize" in e["flags"] or is_boss(e) or is_npc(e) or is_object(e) or is_overlord(e)
def never_dun(e):   return "SetNeverUseDungeon" in e["flags"] or "SetNeverUse" in e["flags"]
def never_ow(e):    return "SetNeverUseOverworld" in e["flags"] or "SetNeverUse" in e["flags"]
def killable(e):    return "SetKillable" in e["flags"]
def cannot_key(e):  return "SetCannotHaveKey" in e["flags"] or "SetWaterSprite" in e["flags"]
def water(e):       return "SetWaterSprite" in e["flags"]

# Vanilla per-(slot,sheet) frequency from kSpriteTilesets[144][4] in load_gfx.c —
# used to pick the most-common sheet for an OR-within-position requirement (so a
# soldier's [13,73] resolves to 73, the common one, maximizing admission).
def load_tileset_freq():
    txt = open(os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(__file__))),
                            "src", "load_gfx.c"), encoding="utf-8", errors="ignore").read()
    m = re.search(r"kSpriteTilesets\[144\]\[4\]\s*=\s*\{(.*?)\n\};", txt, re.S)
    freq = {0: {}, 1: {}, 2: {}, 3: {}}
    if m:
        for row in re.findall(r"\{([^}]*)\}", m.group(1)):
            vals = [int(x) for x in re.findall(r"\d+", row)]
            if len(vals) == 4:
                for j, s in enumerate(vals):
                    if s:
                        freq[j][s] = freq[j].get(s, 0) + 1
    return freq
FREQ = load_tileset_freq()

# Required sheet per position: across-position is AND (keep each non-empty slot);
# within-position is OR -> pick the sheet most common in that slot in vanilla.
def req_sheets(e):
    out = []
    for p in range(4):
        ids = e["subs"][p]
        if ids:
            best = max(ids, key=lambda s: FREQ[p].get(s, 0))
            out.append((p, best))
    return out  # list of (pos, sheet)

# Flyers: Enemizer has no setter; the overworld flyers that matter for the
# DontUseFlyingSprites rooms. Keep the MVP's curated set.
FLYERS = {0x00, 0x01, 0x19}

# Non-killable enemies we still allow to spawn IN (overworld variety) — curated
# so we don't randomly spawn traps/hazards (sparks, rollers, guruguru, winders,
# spike traps, antifairies). Standard "creature" enemies only.
EXTRA_NONKILL = {0x00, 0x01, 0x19, 0x26, 0x9B}  # Raven, Vulture, Poe, Hardhat, Wizzrobe

def randomizable(sid, e):
    if sid > 0xF2:
        return False
    if dnr(e):
        return False
    if never_dun(e) and never_ow(e):
        return False
    if not req_sheets(e):
        return False
    if killable(e):
        return True
    return sid in EXTRA_NONKILL

# ---- emit kSheetNeed[256] (pin detection) + boss set ----
need = {}
for sid in range(0xF3):
    e = ents.get(sid)
    if e is None:
        need[sid] = 0  # unknown -> not KNOWN bit -> pins ALL slots at runtime
        continue
    mask = 0x80
    for p in range(4):
        if e["subs"][p]:
            mask |= (1 << p)
    # AddGroup(n) entries (the 8 village NPCs: SweepingLady/RunningMan/... use
    # whole-group sheets with NO AddSubgroupN) would otherwise pin nothing and get
    # reshuffled into garbage (fresh-eyes audit MED-HIGH). Conservatively pin ALL
    # slots so any area holding one is ineligible.
    if e["group"] is not None:
        mask |= 0x0F
    need[sid] = mask
boss_ids = sorted(sid for sid, e in ents.items() if sid <= 0xF2 and is_boss(e))

# ---- enemy table rows ----
ESF = {"K": "ESF_KILLABLE", "NK": "ESF_CANNOT_KEY", "W": "ESF_WATER",
       "ND": "ESF_NEVER_DUNGEON", "NO": "ESF_NEVER_OVERWORLD", "FLY": "ESF_FLYING"}
rows = []
enemy_sheets_by_slot = {0: {}, 1: {}, 2: {}, 3: {}}  # slot -> {sheet -> [single-slot killable+key?]}
sheet_has_any = {0: set(), 1: set(), 2: set(), 3: set()}
for sid in sorted(s for s in ents if randomizable(s, ents[s])):
    e = ents[sid]
    rs = req_sheets(e)
    assert len(rs) <= 3, f"{e['name']} needs {len(rs)} slots (>3)"
    flagbits = []
    if killable(e): flagbits.append("K")
    if cannot_key(e): flagbits.append("NK")
    if water(e): flagbits.append("W")
    if never_dun(e) and not never_ow(e): flagbits.append("ND")
    if never_ow(e) and not never_dun(e): flagbits.append("NO")
    if sid in FLYERS: flagbits.append("FLY")
    sheets = [s for (_p, s) in rs]
    flagexpr = " | ".join(ESF[f] for f in flagbits) if flagbits else "0"
    rows.append((sid, e["name"], flagexpr, sheets))
    # pool bookkeeping
    for (p, s) in rs:
        sheet_has_any[p].add(s)
    if len(rs) == 1 and killable(e) and not cannot_key(e):
        p, s = rs[0]
        enemy_sheets_by_slot[p].setdefault(s, []).append(sid)

# Which positions each sheet is used in by randomizable enemies (for the
# disjointness invariant the position-unaware picker depends on).
sheet_positions = {}
for sid, name, flagexpr, sheets in rows:
    e = ents[sid]
    for (p, s) in req_sheets(e):
        sheet_positions.setdefault(s, set()).add(p)

# ---- per-slot pools ----
def dpool(p):  # dungeon: whitelist sheet with a single-slot killable+key enemy
    return [s for s in WHITELIST[p] if s in enemy_sheets_by_slot[p]]
def opool(p):  # overworld: whitelist sheet with ANY randomizable enemy in slot p
    return [s for s in WHITELIST[p] if s in sheet_has_any[p]]

# Number of subgroup slots the runtime reshuffles (must match ES_RESHUFFLE_SLOTS
# in shuffle_enemies.c). Slots 0,1,2 are the enemy slots; slot 3 is mostly objects
# (it pins often) but carries a few enemies (Armos/Tektite 16, Buzzblob 17, Pikit 27).
RESHUFFLE_SLOTS = 4

# DISJOINTNESS INVARIANT: a sheet we ever load into slot p must be used by enemies
# ONLY in slot p (else the position-unaware "sheet loaded" test mis-admits an
# enemy that needs it elsewhere -> garbage). Assert it for every pool member.
for p in range(RESHUFFLE_SLOTS):
    for s in set(dpool(p)) | set(opool(p)):
        used = sheet_positions.get(s, {p})
        assert used == {p}, f"AMBIGUOUS pool sheet {s} for slot {p}: enemies use it in {used}"

# ---------- print ----------
def fmt_ids(ids, per=13):
    out, line = [], []
    for x in ids:
        line.append(f"0x{x:02X}")
        if len(line) == per:
            out.append(", ".join(line)); line = []
    if line: out.append(", ".join(line))
    return ",\n  ".join(out)

print(f"// === GENERATED by gen_enemy_shuffle_tables.py (Enemizer MIT source) ===")
print(f"// randomizable enemies: {len(rows)}   bosses<=0xF2: {len(boss_ids)}")
print()
print("// --- kSheetNeed[256]: bit7 KNOWN | bits3..0 needs subgroup slot 3..0 ---")
print("static const uint8 kSheetNeed[256] = {")
items = [f"[0x{sid:02X}]=0x{need[sid]:02X}" for sid in range(0xF3) if need[sid]]
for i in range(0, len(items), 8):
    print("  " + ", ".join(items[i:i+8]) + ",")
print("};")
print()
print("static const uint8 kEsBossTypes[] = {\n  " + fmt_ids(boss_ids) + "\n};")
print()
print("static const EnemyConstraint kEnemyTable[ES_TABLE_LEN] = {")
for sid, name, flagexpr, sheets in rows:
    sh = ", ".join(str(s) for s in sheets)
    print(f"  [0x{sid:02X}] = {{ (ESF_RANDOMIZABLE | {flagexpr}), {{ {sh} }} }},  // {name}")
print("};")
print()
for p in range(RESHUFFLE_SLOTS):
    print(f"static const uint8 kEsDungeonPool{p}[] = {{ {', '.join(str(s) for s in dpool(p))} }};")
    print(f"static const uint8 kEsOverworldPool{p}[] = {{ {', '.join(str(s) for s in opool(p))} }};")
print("// counts:  dungeon = {" + ", ".join(str(len(dpool(p))) for p in range(RESHUFFLE_SLOTS)) +
      "}   overworld = {" + ", ".join(str(len(opool(p))) for p in range(RESHUFFLE_SLOTS)) + "}")
print()
print("// pool sanity (dungeon pools must be non-empty so key rooms stay fillable):")
for p in range(RESHUFFLE_SLOTS):
    print(f"//   slot {p}: dungeon={dpool(p)}  overworld={opool(p)}")
