# Design — door shuffle

Authored 2026-06-09 from four parallel research agents over the reference
(`C:\src\ALttPDoorRandomizer`), the zelda3 dungeon runtime (`src/dungeon.c`), and the
fork's rando infrastructure; **revised the same day after a three-agent fresh-eyes pass
that found a logic-half showstopper and a cluster of runtime control-flow corrections**
(see §12 for the review changelog). The proposal carries the *why* and scope; this
captures the *how* and every load-bearing hazard.

> **Reference convention:** code is anchored by **function/symbol name**, per CLAUDE.md
> ("line refs rot fast"). Any `dungeon.c` / `rando_logic.c` line numbers below are
> research-time snapshots for the implementer's first grep, not durable anchors — the
> symbol is the anchor. **Counts** (door-stub totals, region counts) are research-agent
> figures and MUST be grep-confirmed against `Doors.py`/`RoomData.py` before archive
> (the spec-rot lesson).

## 0. The one-line model

Door shuffle is **one per-seed door matching D** (a pairing of dungeon door-stubs) that
drives the same **two independent halves** entrance shuffle established, but at ~20× the
surface area:

```
                      D  (door-stub ↔ door-stub), per seed
                     ╱                                      ╲
          RUNTIME half                                 LOGIC half
   walking through door A physically              placer/key-prover must know
   lands Link in room/edge B                      D's reachability + per-door key
                     │                            thresholds, room-granular
   sparse redirect consulted INSIDE the            │
   quadrant-gate of the transition fns            static OP_DOORS_* ops querying a
   (generalizes the vanilla teleport-door)        reachability ORACLE = the same
                                                  explorer the stitcher/prover use
                                                  (Arch-2, §4 — as built)
```

Both halves are fed by the same `D` computed once during generation
(`Rando_GenerateSlot`) — the entrance-shuffle pattern. The novelty is entirely in *how
big and how structured* each half is.

## 1. Why this is not "entrance shuffle, again"

Entrance shuffle redirects **one** table (`kOverworld_Entrance_Id`, 57 entrances); its
logic half is a per-location region reassignment + a single dungeon-edge `to_region`
rewrite. Door shuffle is categorically harder:

1. **There is no table to overlay** for normal doors — the engine moves one cell on a
   16-wide room grid (`dungeon_room_index += 1` / `-= 0x10` inside the supertile-boundary
   branch of `Dungeon_StartInterRoomTrans_Right`/`…_Left`/`…_Up`/`…_Down`). We must
   *create* the indirection.
2. **The logic graph has no intra-dungeon structure** — a whole dungeon is one region;
   keys are count predicates baked for the vanilla layout. Door shuffle invalidates both
   per seed.
3. **Beatability requires a softlock prover** — every reachable key-door ordering must
   avoid spending the last key on a dead end (an exponential proof,
   `KeyDoorShuffle.validate_key_layout`).

## 2. Runtime half — the door-redirect layer (`randomizer-door-runtime`)

### 2a. Transition taxonomy (positional vs table-driven)

| Transition | C symbol | Vanilla destination | Difficulty |
|---|---|---|---|
| Normal edge door | `Dungeon_StartInterRoomTrans_{Right,Left,Up,Down}` | **positional** `room ± 1` / `± 0x10` (inside the quadrant-gate) | **HARD** |
| Teleport door (vanilla) | same fns, the `(link_tile_below & 0xcf)==0x89/0x8e` branch | header `dung_hdr_travel_destinations[3/4]` → `Dungeon_AdjustForTeleportDoors` | EASY (arbitrary-room already) |
| Spiral / straight stair | `Dungeon_DetectStaircase` → submodule 6 / 18 / 19 | header `dung_hdr_travel_destinations[(idx&3)+1]` | EASY (table byte) |
| Hole / pit fall | two sites in `LinkState_Pits` (`player.c` — both must be hooked) | header `dung_hdr_travel_destinations[0]` | EASY |
| Open-edge / ladder / straight-stair-as-door | edge path / `Module07_11_StraightInterroomStairs` | positional / header | MED (intensity-2 only) |

**Committed scope (Milestone B) shuffles only Normal + Spiral** (intensity 1). So the
hard case is the four `Dungeon_StartInterRoomTrans_*` functions; spirals are a
header-byte override (with the aliasing caveat in §2d).

**As built, only the intensity-1 sites are hooked at all.** The four
`Dungeon_StartInterRoomTrans_*` supertile-boundary branches consult
`Rando_DoorTransOverride(dir)`, and the spiral header-dest read in
`Dungeon_DetectStaircase` goes through `Rando_DoorSpiralDest(room, slot, attr,
vanilla_byte)`. Hole/pit, vanilla-teleport-door, and straight-stair sites are
untouched (intensity-2+ follow-on). The "EASY (table byte)" rows above remain true
but unexercised. **Codegen discovery:** the positional vanilla-connectivity
cross-check (Stage 0) found that Hyrule Castle's two 1F↔2F hall door pairs —
Normal-typed in the reference model — are the *engine's* `0x89` teleport doors
(the `dung_hdr_travel_destinations[3/4]` branch). They carry
`kDoorTblFlag_VanillaTeleport` in the catalog (the exit resolver skips them) and
HC is pinned (§ pins), so no committed-scope layout can touch them.

### 2b. The sparse-override model + the control-flow contract

Add a per-seed lookup with an explicit "no override" sentinel so the *vanilla path is
untouched by default*:

```c
// door_runtime.h
#define RANDO_DOOR_NO_OVERRIDE 0xFFFF
typedef struct { uint16 dest_room; uint8 dest_edge; uint8 dest_slot;
                 uint8 dest_layer; uint8 flags; } RandoDoorDest;
const RandoDoorDest* Rando_DoorRedirect(uint16 room, uint8 edge, uint8 slot); // NULL = unshuffled/off
```

**Control-flow contract (the §12 review correction — get this exactly right or repeat
the Dark-Chapel branch-tracing loss).** The four `Dungeon_StartInterRoomTrans_*`
functions are **quadrant state machines**, not `room±1` one-liners. A 2×2-supertile room
has 4 quadrants; the room-index only changes **inside the supertile-boundary branch**
(`if (!link_quadrant_x)` for Right, `if (link_quadrant_x)` for Left, the
`link_quadrant_y` gates for Up/Down) — a quadrant 0→1 crossing is an *internal* scroll
with **no** room change. Therefore:

1. **Hook placement:** the redirect MUST sit *inside* that supertile-boundary branch,
   **replacing the `dungeon_room_index ± 1/± 0x10` line** — never at function entry (else
   it fires ~4× per room).
2. **No bare `return`:** after the room-index change the function consumes
   `room_transitioning_flags` (bit 0 → toggle `link_is_on_lower_level`; bit 1 → toggle
   `cur_palace_index_x2`/floor) and then **clears** it. The hook MUST fall through to (or
   replicate) that consume-and-clear + the `submodule_index = 2` scroll handoff. A bare
   `return` strands a stale `room_transitioning_flags` that corrupts the *next*
   transition — structurally identical to the catalogued Dark-Chapel `death_var4`
   stale-flag bug.
3. **Spiral-stair bookkeeping (two call sites):** the vanilla path calls
   `Dungeon_AdjustAfterSpiralStairs` when `(uint8)dungeon_room_index !=
   (uint8)dungeon_room_index2` — and there is a **second** call site,
   `Dungeon_InitializeRoomFromSpecial` (the staircase submodule 18/19 load), which
   repositions Link by the same `(dest&0xf)-(prev&0xf)` grid delta after the destination
   room loads. A non-adjacent spiral redirect perturbs **both**. The hook/`Rando_DoorArrive`
   MUST set `dungeon_room_index2`/`dungeon_room_index_prev` so *both* `AdjustAfterSpiralStairs`
   calls are zero-delta no-ops and `Rando_DoorArrive` is the sole position authority.
4. **Place the hook AFTER the legitimate early returns.** The Up/Down functions early-return
   for the overworld-exit tile (`link_tile_below == 0x8e` → `Dung_HandleExitToOverworld`) and
   the `dungeon_room_index == 0` credits branch. The redirect replaces the `± 0x10` line
   *below* those guards — a dungeon top-edge overworld exit must still work.
5. **Milestone-A identity is only meaningful if the all-`NO_OVERRIDE` path flows through the
   REWRITTEN branch** (sets the room index via the new code, consumes+clears the flags,
   manages `index2`/`_prev`), NOT a preserved vanilla fast-path (`if (NO_OVERRIDE) { /*
   original code */ }`). A duplicated vanilla arm makes the RAM-compare gate hollow — it
   would prove the untouched code is untouched. The identity table must exercise the same
   branch a real redirect does, differing only in the destination value.

**Identity invariant:** Milestone A ships this layer with `Rando_DoorRedirect` returning
`NULL` everywhere. With the flag clear *and* the table empty the diff is zero — but the
genuine correctness gate is the **flag-ON, all-`NO_OVERRIDE`, RAM-compare run** (§10): only
that path actually executes the hooks. The off-path corpus check merely proves the
dead-when-off code is inert.

**As built (Stage 1).** The hook shape is `if (!Rando_DoorTransOverride(dir))
dungeon_room_index--;` (resp. `++` / `±= 0x10`) — the override guards the vanilla
positional line *inside* the supertile-boundary branch, after the early returns,
exactly per points 1/4. Two as-built refinements to points 2/5:

- **Override-arm toggle semantics:** when the override fires, `DoorRt_Arrive` has
  already applied room index, Link/camera/bounds, quadrant, and layer; the caller
  then *skips* the `room_transitioning_flags` layer/palace toggle block via the
  self-clearing latch `Rando_DoorTransConsumedToggles()` — the toggles encode the
  POSITIONAL partner's relationship, meaningless for the shuffled pair. The
  function's unconditional `room_transitioning_flags = 0` at the tail still runs
  (no stale flag, honoring point 2). The **destination door record is the layer
  authority**; `cur_palace_index_x2` is left untouched (basic never crosses a
  palace boundary — codegen fails any pool pair that would).
- **Identity-gate nuance (deviation from point 5, recorded deliberately):** the
  all-`NO_OVERRIDE` path executes the full exit-door *resolver* (room index lookup,
  slot matching) and then runs the *vanilla* room-index line — the room-index write
  is NOT routed through a rewritten common arm. Point 5's "identity must exercise
  the rewritten branch" is therefore satisfied for the resolver/hook machinery but
  not for the destination-application code (`DoorRt_Arrive`), which by definition
  diverges from the ROM and can only be playtest-verified. The flag-ON RAM-compare
  gate proves the hooks + resolver are transparent; arrival correctness is on the
  playtest matrix.

`DoorRt_Arrive` also sets `dungeon_room_index2`/`_prev` to the arrival edge's
*virtual positional neighbor* so the next room load resyncs them and both
`Dungeon_AdjustAfterSpiralStairs` sites see a zero delta (point 3), and resyncs the
tagalong y-high bytes.

### 2c. Arbitrary-room arrival (`Rando_DoorArrive`)

The vanilla teleport-door routine `Dungeon_AdjustForTeleportDoors` already repositions
Link + camera from a destination room's grid coords. Two corrections from review:

- **Arrival edge is model-guaranteed opposite the exit edge** (the matching connects
  North↔South / East↔West, per the reference's `hanger`/`opposite_h_type` map). So the
  destination room scrolls in normally — **redirected normal doors keep the smooth scroll;
  they do NOT need to become fade/cut transitions.** The real job of `Rando_DoorArrive` is
  the **slot/perpendicular-axis correction**: a room can have several doors on one edge at
  different slots, and the teleport routine keeps Link's *same intra-room offset*, which
  would land him off-axis (into a wall) at the partner's slot. `Rando_DoorArrive` sets
  Link's coordinate on the door's perpendicular axis from the partner slot's tilemap
  position (`kDoorPositionToTilemapOffs_*`) + an inward step, sets `link_quadrant_x/y`,
  `link_is_on_lower_level` from `dest_layer`, then runs `Dungeon_AdjustForRoomLayout`.
- **16-bit destinations:** `Dungeon_AdjustForTeleportDoors` takes a `uint8 room` and the
  whole `dung_hdr_travel_destinations[]` mechanism is byte-wide, but `dungeon_room_index`
  is `uint16` and dungeon rooms range to `< 0x140`. `Rando_DoorArrive` MUST take a
  `uint16` room and MUST NOT route an arbitrary destination through the `uint8`
  travel-dest bytes (they silently wrap at 0x100). *Open question (§9):* confirm no
  committed-scope (intensity-1, single-dungeon) target is ≥ 0x100; if any is, the uint16
  path is mandatory for MVP, not a follow-on.

This is the single most delicate runtime routine and the #1 playtest target.

### 2d. Spiral stairs — one authority, not two (review correction)

A spiral stair (tile attr 0x38/0x39) does **both**: `Dungeon_DetectStaircase` calls
`Dungeon_StartInterRoomTrans_Up/Down` *and then* unconditionally overwrites
`BYTE(dungeon_room_index) = dung_hdr_travel_destinations[(which_staircase_index & 3) + 1]`.
So the §2b hook (inside `…Trans_Up/Down`) and a header-byte override would **both fire and
fight** for the same transition. Resolution:

- The §2b `…Trans_*` hook MUST be a **no-op in staircase context** (gate it on
  "this is a normal edge door," e.g. the `link_tile_below` door-tile test, not a staircase).
- The **header-dest override is the SOLE spiral authority**, keyed on `(room,
  which_staircase_index & 3)` — overriding only that specific slot, never a blanket array
  overwrite (slots 3/4 alias the teleport-door bytes).
- The override site writes `BYTE(dungeon_room_index)` — **uint8**, so it cannot reach a
  destination ≥ 0x100. The spiral redirect must therefore write the full **uint16**
  `dungeon_room_index` (bypassing the `BYTE(...)` store) if any spiral target is high-plane
  (§9.4 must answer this for the spiral dest set, not just teleport doors).
- The spiral **slot key** is `which_staircase_index & 3`, derived at runtime from the tile
  attribute — NOT from a door-stub id. The codegen door-registry MUST map each spiral stub
  to its runtime staircase slot so generator and runtime agree (§2e/§3a).

**As built:** `Dungeon_DetectStaircase` brackets its `…Trans_Up/Down` calls with
`Rando_DoorStaircaseContext(true/false)` (the edge-door hook no-ops inside), and the
header read becomes `BYTE(dungeon_room_index) = Rando_DoorSpiralDest(prev_room, slot,
attr, dung_hdr_travel_destinations[slot+1])` — keyed (room, slot, attr) at the read
site, never a blanket header overwrite; straight/water-stair attrs return the vanilla
byte. A chosen redirect arms a **pending intra-room fixup** applied by
`Rando_DoorSpiralFixup()` from `Dungeon_InitializeRoomFromSpecial` *after* the vanilla
grid-granular adjust: it locates the destination staircase tile in the freshly loaded
room (attr 0x38/0x39 with the slot marker on the row below) and applies the intra-room
delta to Link/camera/quadrant, setting the layer from the dest door record (vanilla
spiral pairs share their intra-room offset; shuffled ones need not). The uint8
`BYTE(dungeon_room_index)` store stands — codegen asserts every cataloged door's room
is `< 0x100` (§9.4 resolved), so a uint16 path was unnecessary.

### 2e. Door-stub identity (the engine assigns none)

Door records carry only `{edge, position-slot, kind}` — no id. We codegen a **stable
door-stub id** = a canonical index over `(room, edge, slot, layer)`, matching the
reference's `Door.name` catalog so generator and runtime agree. `door_registry.yaml` pins
these ids (mirrors `entrance_registry.yaml`); they MUST be frozen across generator
versions or old slots break.

### 2f. Environment coherence — the boss-shuffle lesson, mostly handled by construction

Because a redirect sets `dungeon_room_index` to the destination **before** the room load
(`Module07_02_*` → `Dungeon_LoadRoom` → `Dungeon_LoadHeader`), the destination room's own
header drives palette (`palette_main_indoors` / `kDungPalinfos[hdr[1]]`), sprite gfx
(`hdr[3]`), the effect byte (`dung_hdr_collision_2` = `hdr[4]`: ice/lava/flood/freeze),
collision, lights-out, BG2 — and the scroll uploads the destination's full tilemap. So
**the boss-shuffle "sprites-not-environment" trap does NOT apply to a room-index-level
redirect** (review-verified). Remaining cross-environment hazards, all deferred to the
crossed follow-on: `cur_palace_index_x2` + floor/plane on cross-dungeon/floor redirects
(basic never crosses dungeons ⇒ MVP safe); room-state-gated spawns (Blind, the
Kholdstare/Trinexx boss-env-pinned rooms boss shuffle already pins — basic intensity-1
never relocates boss rooms).

### 2g. Shutter / adjacency / door-open-bit hazards (4 neighbors, not one)

`Dungeon_LoadHeader` peeks **all four** positional neighbors (`room−1, room+1, room−16,
room+16`) via `Dungeon_CheckAdjacentRoomsForOpenDoors` / `Dungeon_LoadAdjacentRoomDoors`
on **every** room load, matching the loaded room's door-tilemap slots against the
neighbor's door list to decide which shutters render open. Under redirection the logical
neighbor differs, so: (a) all four edges' adjacency peeks must resolve the **redirected**
neighbor, and (b) when partner doors differ in slot the tilemap-slot match can silently
fail. Door-open bits live in `save_dung_info[room]` nibble `0xF000`, keyed by the physical
room (unchanged). This is the most likely "shutter won't open / key door re-locks" runtime
bug and is a named Milestone-B audit item.

**As built this hazard grew into a first-class committed work item — the Stage-1b
door-KIND overlay** (see the `randomizer-door-runtime` delta and §13 R2/R3): three raw
door-list reader seams consult a per-(room, doorListPos) kind override; the engine's
4-stateful-doors-per-supertile limit forces a pos<4 **both-halves** position-swap
constraint on key-door candidates; paired key-door halves need an explicit partner
open-bit mirror plus suppression of the physical-neighbor propagation scan; and the
reference's Skull Pinball trap→Normal mutation rides along. In flight at spec-update
time, alongside the Stage-2 stitcher/prover.

## 3. Generation half — the door-shuffle generator (`randomizer-shuffles`)

Ported from the reference with **our own format + RNG salt** (no seed-parity needed).

### 3a. Static topology (codegen, not per-seed)

`gen_door_tables.py` parses `Doors.py` + `RoomData.py` + `Dungeons.py` and emits C tables
(gitignored, regenerated). **Precedent: `assets/scripts/gen_entrance_table.py` /
`gen_inverted_maps.py`** (the in-tree codegen pattern — *not* `gen_enemy_shuffle_tables.py`,
which lives on an unmerged sibling branch). Tables: door-stub catalog
(`{id,room,edge,slot,layer,DoorType,default_partner_id,DoorKind}`); per-room env +
`dungeon_id` + region membership (the reference's room-granular `dungeon_regions`); vanilla
`door_type_counts` per dungeon; required-path targets (`boss_path_checks` etc.);
crystal-barrier (blue/orange) annotations.

### 3b. The stitcher (per-seed connectivity)

Port the **live** stitcher entry point `generate_dungeon` (in
`source/dungeon/DungeonStitcher.py`) — *not* `generate_dungeon_find_proposal_old` (the
deprecated DFS in `DungeonGenerator.py`), and beware `DungeonGenerator.generate_dungeon` is
a stale same-named sibling. Phases: `convert_to_sectors` (a `DoorShuffle.py` helper the
pipeline calls — flood connected regions, collect outstanding stubs) → `create_random_proposal` (bucket stubs by hook
N/S/E/W/Stairs, match each hanger to an opposite-hook hook from a different piece, avoid
self-loops) → loop: `explore_proposal` (BFS) → `check_valid`; on failure `modify_proposal`
(swap one *visited* door's target with an *unvisited* matching-hook door to pull in an
orphaned section; re-randomize after 10 local repairs). Cap iterations; raise on
exhaustion. Reachability primitive = a port of `ExplorationState` with dual
`visited_blue`/`visited_orange` crystal-barrier states (both are lists in the reference ⇒
deterministic).

**Polarity caveat (review correction):** `basic` (single-dungeon pool) skips the
cross-dungeon *distribution* loop (`create_dungeon_builders` →
`assign_polarized_sectors`/`polarity_step_3`/`parallel_full_neutralization`), which is the
single hardest engine and is crossed/partitioned-only. But `simple_dungeon_builder` still
constructs `GlobalPolarity` + calls `assign_sector` per sector — so the basic path must
either port those (likely trivial for a single sector-list) or prove them no-ops. The
claim is "basic skips the distribution engine," not "basic has zero polarity code."

### 3c. The key-door softlock prover (`door_keylogic.c`)

Port `KeyDoorShuffle.analyze_dungeon` + `validate_key_layout`: build `KeyCounter` states
(closure over "which doors are open"), prove via memoized worst-case DFS that no reachable
state spends the last available key/location on a dead end (`invalid_self_locking_key`);
on failure drop the key-door count and retry. Output per dungeon: the chosen key-door set +
a `DoorRules` per door = the worst-case minimum small keys that must be held to open it
safely, plus `bk_restricted` (locations the big key may NOT be placed at).

**Scope correction (review):** "MVP = `partial` key logic only" is **not** a port-scope
reducer. `partial`/`strict`/`dangerous` diverge only at the *runtime-eval* side
(`partial` adds `number = min(worst_case, small_key_num)`); `analyze_dungeon` produces the
*same* `door_rules`/`bk_restricted` for all three. So the MVP still needs the full
`analyze_dungeon` + `validate_key_layout` + `KeyCounter` closure machinery, including the
**std-mode Hyrule-Castle special-casing** woven through `validate_key_layout` and the
**`bk_restricted` → big-key placement ban** (§4). The MVP simplification is *which eval
threshold the logic uses* (worst-case, which is safe — see §4), not *which prover code is
ported*.

### 3d. Door-type assignment (MVP = `original`)

`door_type_mode = original` keeps vanilla per-dungeon counts and relocates the existing
small/big-key doors onto the new connections. The `DoorKind` byte (SmallKey/BigKey) lives
in the per-room door record parsed by `RoomDraw_DrawAllObjects`. **Open question (§9):**
mutate via a regenerated `kDungeonRoom` asset vs. a runtime overlay of the door-list parse
— leaning runtime overlay (keeps asset vanilla, diff seed-scoped).

### 3e. Determinism

All door RNG salts off the seed with a door-specific constant. `budget_seconds = 0` always
(the `Place_AssumedFill` determinism rule). The stitcher/prover are iteration-bounded, not
time-bounded. **Cross-platform hazard (review):** the reference uses Python dict/set
iteration in spots (and defensively `sort`s before hot RNG draws). The C port MUST feed
every random selection from an **explicitly id-sorted list** (the codegen door-stub id
order is the natural anchor) and MUST NOT iterate a hash-ordered container to feed RNG or
output — or `placement_digest` diverges across platforms.

### 3f. What the MVP deliberately omits

Skipped for Milestone B, designed in §8: the cross-dungeon polarity/distribution engine;
intensity 2/3; door-type big/all/chaos + trap shuffle/removal; decouple; wild/universal
keys; pottery/key-drop pools.

## 4. Logic half — the door-shuffle reachability oracle (`randomizer-logic`) — AS BUILT (Arch-2)

### 4a. Why the region graph can't express it (evidence)

`logic_parts/20_palace_of_darkness.yaml`: the dungeon is **one** region; all locations set
`region: PalaceOfDarkness`; key gating is `HAS_AMOUNT(SmallKey_PalaceOfDarkness, N)` baked
for the *vanilla* layout. `Logic_ComputeReachability` is a generic fixed-point over
`(region_bitset, EdgeDef[])` with **no op for "door open" or "room reachable within
dungeon."** A shuffled layout changes both *which* locations are reachable and *how many
keys* gate each — inexpressible today.

### 4b. The infeasible approach we are NOT taking (recorded so it isn't re-proposed)

The pre-review draft proposed injecting an *arbitrary per-seed per-location predicate*
through "the existing predicate-override seam." **Two fresh-eyes agents independently
proved this infeasible**, and I verified it:
- `Rando_FindPredicateOverride` / `RandoLocationPredOverride` store
  `can_reach_offset`/`length` — **offsets into the frozen static `kRandoPredicateStream`
  bytecode blob**, keyed by `world_state`. There is no runtime/per-seed predicate
  *synthesis* path; the VM `Cursor` always anchors to the static stream.
- Even entrance shuffle's per-seed predicate override (`g_entrance_override_pred_off/len`)
  only *selects a pre-existing static offset* — it cannot emit new bytecode.
- A single per-location scalar `(item gates) AND HAS_AMOUNT(SmallKey, N)` also cannot
  faithfully encode the reference's per-**door**, stateful, placement-state-dependent
  `DoorRules` (the `AllowSmall` self-locking exemption, the `needed_keys_w_bk` vs
  `_wo_bk` two-valued thresholds, the `bk_restricted` placement ban).

### 4c. The chosen approach — AS BUILT: Arch-2, a reachability ORACLE behind two static ops

The pre-implementation plan (§4c-original, summarized below for the record) was a
**static-region port**: add the reference's ~579 per-room regions to the fork's region
table, bump `kReachabilityMaxRegions` past 256, wire layout `D` as per-seed door-edges
(a door-owned clone of entrance shuffle's added-edge machinery), and select per-door key
thresholds from a precompiled `HAS_AMOUNT(SmallKey_X, k)` offset family. The
implementation plan's adversarial review (round 1) replaced it with **Arch-2**, and that
is what shipped. Why the static port lost:

- **Two-model drift.** The stitcher and key prover already need a faithful C explorer
  over the door graph (crystal states, events, required paths) to generate and validate
  layouts. The static port would have *re-expressed the same connectivity a second time*
  in the fork's region/edge/predicate vocabulary — two models of one dungeon that can
  silently disagree (the exact generator-vs-logic desync class the placer cannot catch).
  Arch-2 makes the generator's explorer the *single* model and lets the logic query it.
- **Statefulness the flat fixed-point can't express.** The reference's dungeon
  reachability is stateful per exploration: dual blue/orange **crystal-switch** states
  (a region can be reachable in one barrier state only), monotone **events**
  (trench fills, Thieves' Town maiden states, Attic Cracked Floor) granted *during* the
  flood, and Swamp-class water toggles. `Logic_ComputeReachability`'s flat region bitset
  has no per-state dimension; encoding blue/orange × event combinations as static
  regions/edges would explode the graph well past the 579 baseline. The explorer already
  carries exactly this state.
- **Key economy is a *flood property*, not an inventory predicate.** Drop keys (§1c of
  the plan) count toward a door's threshold only when their room is *reached in the
  current exploration state* — `HAS_AMOUNT` offsets can't see that. The oracle computes
  `held = chest-key counts + reachable drop-key rooms` inside the flood, symmetric with
  the prover.

**The shipped mechanism** (`rando_logic.c` + `shuffle_doors.h` + `rando_logic_gen.py` +
`op_registry.yaml` ids 20/21):

- **Two STATIC predicate-VM ops** — `OP_DOORS_ACTIVE(dungeon_u8)` and
  `OP_DOORS_LOC_REACHABLE(loc_u16)` — evaluated against per-seed C state (the installed
  `DoorShuffleLayout`), the `OP_MEDALLION_OPENS` / `OP_CAN_KILL_BOSS` precedent. §4b's
  no-bytecode-synthesis rule is honored: the stream stays static; only the *state* the
  ops consult is per-seed.
- **Codegen wrap** (`rando_logic_gen.py`, fed by `door_predicates.gen.json`): every
  door-controlled location's `can_reach` becomes
  `(NOT DOORS_ACTIVE(d) AND vanilla) OR (DOORS_ACTIVE(d) AND DOORS_LOC_REACHABLE(loc))`.
  Inactive ⇒ exactly the vanilla bytes evaluate ⇒ corpus byte-identical (3-way regen
  proof). `DOORS_ACTIVE` is per-dungeon off the layout's `shuffled_mask`, so **pinned
  dungeons (HC, Swamp) evaluate pure vanilla** even with a layout installed.
- **The oracle = `DoorExplore_Run`** — the SAME crystal-aware explorer the stitcher and
  prover use, with Vm rule leaves bound to the compiled fork predicate stream
  (`kDoorVmPreds`; pure item/macro terms, never `OP_DOORS_*`, so no recursion —
  `g_in_door_oracle` asserts it) and key-door edges gated by the prover's worst-case
  thresholds plus the drop-key economy.
- **Portal seeding (the round-1 BLOCKER fix):** the fork's region graph carries ~one
  region per dungeon, so the oracle cannot infer *which portals* of a multi-portal
  dungeon (Desert, Skull, TR ledges, Tower) are independently enterable. The committed
  table `assets/rando/door_portals.yaml` → `kDoorPortalGates` maps each reference portal
  lobby to the **fork region** gating it + an optional static-stream predicate; the
  oracle seeds its flood from portals whose gate holds under the *live region bitset*.
  Gates are monotone (regions+items only); a too-strong gate only under-seeds
  (conservative-safe). The SAME table feeds the generator's portal analysis, so the two
  sides cannot disagree about enterability.
- **Memoization:** the flood is cached per dungeon and invalidated every
  `Logic_ComputeReachability` fixed-point pass (counts are fixed per call; the region
  bitset only grows between passes, so a stale-by-one-pass cache can only
  *under*-estimate, and the zero-change exit pass evaluates with exact inputs —
  monotone, converges).
- `bk_restricted` ships as a real **big-key placement ban** in the placer
  (`DoorShuffle_BkRestricted` next to `dungeon_mode_accepts_item`), exactly as the
  original §4c demanded.

What the rewrite made moot: NO 579-region YAML port, NO `kReachabilityMaxRegions` bump
(the cap stays 256; door regions live in the door tables' own namespace), NO per-seed
door-edge arrays or region overrides (the entrance/door namespace-collision risk
evaporated — the modes are mutually exclusive anyway, §4d), NO precompiled threshold
family, and no Switch reachability-budget re-measure (the oracle floods ≤13 dungeons
per pass, memoized).

**Worst-case N is safe.** Using the prover's worst-case threshold (skipping the
`AllowSmall` / two-valued relaxations) can only ever *over*-estimate keys-needed, which
makes the placer *more* conservative — it never certifies a location reachable with fewer
keys than reality, so it never ships an unbeatable seed. The relaxations (which let the
placer be *less* conservative) are a later optimization, not a correctness requirement.
`bk_restricted`, by contrast, is **not** a relaxation — placing the big key behind its own
big-key door is unbeatable — so it ships in the MVP as a real placement ban.

### 4d. Containment + key modes + world states (MVP pins, as built)

`dungeon_mode_accepts_item` gives whole-dungeon **containment** ("this small key lands in
its own dungeon"), *not* per-location reachability — that is the oracle's job. The pins
live in `apply_derived_rules` (`rando_settings.c`), normalizing (not refusing) so the
`settings_hash` matches the actually-generated seed — the entrance-axis convention:

- door shuffle ⇒ force `dungeon_small_keys_mode == Dungeon` AND
  `dungeon_big_keys_mode == Dungeon` (containment + the `bk_restricted` ban together);
- honored only on **Open/Standard** world states (Inverted has its own logic tree;
  **Retro** degenerates thresholds — `eval_has_amount` collapses any
  `HAS_AMOUNT(SmallKey_X, N)` to `GenericKey >= 1`, flattening per-door key math; the
  reference guards universal keys the same way) and only under **NoGlitches** logic
  (the oracle models no glitch traversal);
- **mutually exclusive with entrance shuffle** — door shuffle yields to an explicit
  entrance-shuffle request (both redirect dungeon topology; the portal-gate table
  assumes vanilla lobby reachability);
- dungeon pins: **Hyrule Castle in ALL world states** + **Swamp Palace**
  (`kDoorShuffle_MvpDungeonMask`, `shuffle_doors.h`) — see the plan pins P4'/P5;
  Thieves' Town is deliberately NOT pinned (the maiden/attic events are ported as
  explorer events).

`Wild`/`Universal` keys × door shuffle (tracking `outside_keys`) stays a follow-on.

## 5. Settings model + canonical serialization (`randomizer-core`)

Full-feature axes total ~10 bits (`doorShuffle` 2, `intensity` 2, `door_type_mode` 2,
`trap_door_mode` 2, `decoupledoors` 1, `door_self_loops` 1) — exceeding the single free pad
byte `[27]`. **Committed scope (MVP)** needs only `doorShuffle` (2 bits, vanilla/basic) +
`intensity` pinned to 1, which fits `[27]` ⇒ `kSettingsCanonicalLen` stays 28, no size
cascade, default `settings_hash` byte-identical (the enemy-shuffle `[26]` precedent —
verified: `[25]`=entrance axes, `[26]`=enemy_shuffle, `[27]`=reserved-zero). The follow-on
axes grow the layout to `[28]` and trigger the 4-site `kSettingsCanonicalLen` cascade
(`rando_settings.h`, `rando_spoiler.{h,c}` CRC + 138 const, `rando_save.c`, `main.c`, +
corpus `126/130/134/138` constants), guarded by `_Static_assert`s. MVP pins into `[27]`
precisely to defer that cascade. `apply_derived_rules` coerces incompatible combos (door
shuffle ⇒ force in-dungeon small + big keys) on the private copy before serialization, so
defaults still pack to zero. (The deserializer already ignores `[27]`; note the *stale*
`rando_settings.h` comment claiming it rejects non-zero pad bytes should be corrected when
`[27]` is claimed.) **The baseline `randomizer-core` "Settings canonical serialization
order" spec requirement is stale** (it predates the `[25]`/`[26]` axes); this
change therefore adds an ADDED requirement rather than restating the drifted order (§ specs).

**As built:** `door_shuffle ∈ {vanilla, basic}` in canonical `[27]` **bits 0-1**
(`kDoorShuffleAxis_Mask`); `intensity` is pinned to 1 and not serialized at all. CSV key
`door_shuffle`. `Settings_EffectiveDoorShuffle` reports the normalized value by reading
canonical byte `[27]` post-serialization (definitionally post-`apply_derived_rules`).
The native settings window exposes a "Door shuffle" checkbox mapping to vanilla/basic.

## 6. Save / regen / version-locking (`randomizer-save`)

Do **not** serialize the door layout — regenerate it from `(base_seed, settings,
door_attempt)` at load (the entrance-permutation precedent). The placement table is already
serialized (items fixed); the physical door redirect + per-door key thresholds are
recomputed deterministically. Add a `door_attempt` byte at the **first reserved byte of the
4-byte sidecar tail** (the `reserved[4]` after `prize_attempt`; @73–75 are already
boomerang/bow/prize_attempt). `door_attempt` consumes 1 of the 4 remaining tail bytes.

**Version-drift is BLOCKING for door-shuffle slots (review correction, re-rated HIGH).**
For entrance shuffle, drift means "wrong overworld area" (still traversable), so the
existing drift *warning* is non-blocking and load proceeds. For door shuffle, a drifted
interior layout can make the **certified-beatable serialized placement unbeatable** (the
placer put the big key behind N key-doors; a regenerated layout needs N+1). **Note the
plumbing reality:** `Rando_ActivateSidecarSlot` is `void` (no refuse path today) and the
drift check is advisory-only. The **primary route is a persisted layout digest**: store a
hash of the generated door layout in the sidecar, recompute it from the regenerated layout
at activation, and **hard-fail (refuse the slot) on mismatch** — this is a small,
self-contained add (a tail field + a compare) rather than threading a new failure return
through the activation API. (Refusing purely on `generator_version` mismatch is the cruder
alternative; the digest catches the actual divergence, including same-version pool drift.) `kGeneratorVersion` bumps; the exact number is **re-grepped at
commit** (the version-drift rule) — `62` is illustrative and is already contended by
concurrent unmerged branches (sheet-reshuffle/customizer at 62, major-glitch close-out at
64), so this will likely land higher.

**As built:** the sidecar header claims the whole `reserved[4]` tail — `door_attempt`
at **@76** plus a **24-bit layout digest at @77-79** (3 bytes LE; the low 24 bits of
`DoorShuffle_LayoutDigest` over pairings + key doors + thresholds + `bk_restricted`),
both zero on vanilla-door slots and for pre-field writers (`rando_save.{c,h}`).
`Rando_ActivateSidecarSlot` regenerates the layout from the share-string seed +
settings + `door_attempt` BEFORE installing any slot state, recomputes the digest, and
**hard-fails on mismatch or generation failure** — `Rando_DeactivateSlot()` + a stderr
diagnostic, the slot refused for this session but the file untouched (non-destructive;
still loadable by the build that wrote it). The plan's fancier pre-activation
select-file classifier seam (an "incompatible" render kind cached in the sidecar
classifier) was NOT built — the activation-time refusal is the smaller seam and covers
the correctness requirement; the UI polish can follow if drift refusals turn out to be
user-visible in practice.

## 7. Milestones (the staged plan)

- **A — Foundations & identity (committed).** Codegen topology; sparse-override runtime
  layer + hooks (with the §2b control-flow contract) behind `kFeatures1_DoorShuffleActive`;
  generalized `Rando_DoorArrive`; identity table. **Real gate: flag-ON, all-`NO_OVERRIDE`
  RAM-compare clean** (the off-path corpus check only proves dead-when-off code is inert).
  No randomness yet.
- **B — Basic MVP (committed).** `doorShuffle=basic`, intensity 1, `original` door types,
  in-dungeon small+big keys; stitcher + key-prover + the §4c door-graph logic;
  settings/save/UI/version/corpus. **Gate: hand-playtest redirected dungeons — beatable,
  no softlock, correct shutters/keys/arrival.**
- **C — Intensity 2/3 (follow-on change).** Open-edges, straight-stairs, ladders;
  lobby/portal shuffle (couples with entrance shuffle's portal layer).
- **D — Door-type/trap/decouple (follow-on change).** big/all/chaos counts, trap-door
  shuffle + removal, decoupled one-way doors, self-loops, strict/dangerous key eval, the
  `AllowSmall`/two-valued key-rule relaxations.
- **E — Crossed / partitioned (follow-on change).** The polarity/sector-distribution
  engine + cross-dungeon environment porting (palette standardization, the boss-env pin
  set, `cur_palace_index_x2`/floor coherence) + wild/universal keys. The hardest
  runtime+gen surface; inherits every boss-shuffle environment lesson.

## 8. Full-feature surface (design coverage for follow-ons)

- **Intensity:** L1 = Normal+Spiral; L2 += Open/StraightStairs/Ladder; L3 += lobby/portal
  shuffle. Excluded categories are pre-connected (vanilla-wired) so the stitcher ignores
  them.
- **Pools:** basic = per-dungeon; partitioned = 3 fixed groups (LW / early-DW / late-DW);
  crossed = one all-dungeon pool, rebuilt into builders by the polarity engine.
- **Door types:** `DoorKind` byte — SmallKey/BigKey/Bombable/Dashable/Trap. big mode
  unlocks vertical big-key doors; chaos perturbs counts.
- **Trap doors:** `trap_door_mode` vanilla/optional/boss/oneway; the Mire cutscene trap is
  always left to force fire.
- **Decouple:** one-way half-edges; `connect_one_way` + the anti-degenerate-cycle guard.

## 9. Open design questions — ALL RESOLVED (as built)

1. **§4c spike** — SUPERSEDED by Arch-2 (§4c as built): the static-region +
   precompiled-threshold mechanism was replaced wholesale by the oracle in the
   implementation plan's round-1 review, so the "does the fixed-point reproduce the
   prover's sphere" question dissolved — the oracle IS the prover's explorer. The
   remaining oracle≡stitcher agreement check is a `--door-selftest` gate, not a design
   question. The infeasible arbitrary-predicate path stays recorded in §4b.
2. **`Rando_DoorArrive` validation** — built as `DoorRt_Arrive` (+ the spiral fixup);
   correctness rides the flag-ON identity gate for the hook machinery and the playtest
   matrix for the arrival math itself (§2b as-built note). No offline harness was built.
3. **Door-type byte application** — RESOLVED: **runtime door-list overlay** (the
   Stage-1b door-KIND overlay, §2g/`randomizer-door-runtime`); the `kDungeonRoom` asset
   stays vanilla and the diff stays seed-scoped.
4. **16-bit destinations** — RESOLVED: `gen_door_tables.py` fails codegen if any
   cataloged vanilla pair's room is `≥ 0x100`, so every committed-scope destination fits
   the byte-wide paths (the spiral `BYTE(...)` store stands).
5. **Door-stub id freeze** — RESOLVED: `assets/rando/door_registry.yaml` (committed)
   pins append-only ids; the layout digest hashes door ids, so a re-numbering would
   invalidate every door-shuffle save — `check_door_tables.py` enforces registry/header
   agreement.
6. **Shutter/door-open sync** — RESOLVED into the named Stage-1b kind-overlay scope
   (partner open-bit mirror + neighbor-propagation suppression, §2g as-built note).
7. **Artifact lifecycle** — RESOLVED: the door tables are **COMMITTED**
   reference-derived artifacts (`src/rando/door_tables.gen.{c,h}` +
   `assets/rando/door_predicates.gen.json`), because regeneration needs a local
   ALttPDoorRandomizer checkout that CI and fresh clones lack. `check_door_tables.py`
   (wired into `rando_ci.yaml`) guards *consistency* — missing/empty files, header
   counts vs data vs the frozen registry, predicate-manifest count vs
   `kDoorTbl_VmPredCount` — rather than regeneration. (The debug intermediate
   `door_tables.gen.json` stays gitignored.)

## 10. Verification & the fails-open trap

- **Milestone A real gate = flag-ON RAM-compare.** Routing every transition through an
  all-`NO_OVERRIDE` table with the flag ON, side-by-side against the ROM, is the only check
  that *executes* the hooks and proves zero divergence. The corpus byte-identical check
  proves only that the dead-when-off code path didn't perturb generation.
- **Milestones B+ have no automated net** — corpus + `--rando-selftest` cover only headless
  *generation*, never the runtime redirect or the key-prover's end-to-end grant. Playtest is
  the only reliable verification; the playtest loop is the real bottleneck.
- **Digest-neutrality must be corpus-proven, not claimed.** Per CLAUDE.md ("validate any
  placement-affecting change with a CORPUS REGEN, not a code-review claim of
  digest-neutral"), the "`doorShuffle==vanilla` byte-identical" and "default `settings_hash`
  unchanged" claims are acceptance gates verified by a **3-way regen** (fresh `main` vs the
  change, codegen forced), not by inspection.
- **Fails-open hazard:** the codegen'd door table is a gitignored artifact (like
  `chest_table.gen.bin`). If absent at build it MUST fail the build, not silently emit an
  empty table (every door would fall through to vanilla and *look* fine while the feature is
  dead). Add `check_door_tables.py` (the existing fails-open-guard precedent), extend
  `setup_worktree.py`'s mirror allowlist if committed-but-gitignored, and add a
  `RandoGenerate_SelfCheck` non-empty assertion.

## 11. Risk register

| Risk | Severity | Mitigation |
|---|---|---|
| §4c logic re-architecture larger than scoped / mis-models reachability | HIGH | spike one dungeon's generation before committing B; worst-case-N is provably safe; reuse shipped per-seed primitives |
| Porting ~579 room regions overruns `kReachabilityMaxRegions = 256` (silently ⇒ regions read unreachable) | HIGH | bump the cap (uint16 ids, no static-assert couples it); re-prove off-path byte-identical + re-measure Switch budget after (§4c) |
| Spiral redirect double-acts (`…Trans` hook vs header overwrite) / 2nd `AdjustAfterSpiralStairs` site | HIGH | §2d makes the header override the sole spiral authority + §2b no-ops in staircase context; zero-delta both adjust sites (§2b.3) |
| Door↔entrance region-override namespace collision | MED | separate `g_door_region_override[]` or mutual-exclusion guard (§4c) |
| Door shuffle × Retro/Wild/Universal collapses per-door thresholds | MED | coerce/refuse those key/world modes in MVP (§4d) |
| `Rando_DoorArrive` mis-lands Link / camera | HIGH | offline spawn harness (§9.2); flag-ON RAM-compare gate; per-edge/layer playtest matrix |
| Hook placed at fn entry not inside the quadrant-gate / strands `room_transitioning_flags` | HIGH | the §2b control-flow contract; replace the `room±1` line, fall through to consume+clear |
| Key-prover port bug ⇒ certified-but-unbeatable seed | HIGH | full `analyze_dungeon`/`validate_key_layout` port + `bk_restricted` ban; playtest |
| Version-drift loads an unbeatable door-shuffle slot | HIGH | drift is **blocking** for door slots, or a persisted layout digest hard-fails (§6) |
| Shutter/door-open desync (§2g) | MED | redirect all 4 adjacency peeks; explicit audit item |
| Codegen table fails open (empty) | MED | build guard + self-check + `setup_worktree.py` mirror (§10) |
| Cross-platform digest drift from hash-ordered iteration | MED | id-sorted lists feed all RNG; no set-iteration into output (§3e) |
| `kSettingsCanonicalLen` cascade when follow-on axes land | LOW | MVP pins into `[27]`; cascade is a known 4-site checklist |
| Crossed-mode environment porting (deferred) | HIGH (follow-on E) | reuse boss-shuffle pin set + palette standardization; out of MVP |

## 12. Review changelog (2026-06-09 fresh-eyes pass, 3 agents)

- **Logic SHOWSTOPPER (2 agents, independently):** the "reuse the predicate-override seam"
  plan was infeasible — the seam carries static stream offsets, not per-seed predicates,
  and a per-location scalar can't encode the reference's per-door `DoorRules`. → §4 fully
  rewritten to the per-room-region + per-seed-door-graph + precompiled-threshold mechanism
  (§4c), with the infeasible path recorded (§4b). `bk_restricted` promoted to an MVP
  placement ban.
- **Runtime control-flow (agent 1):** the four `…InterRoomTrans` fns are quadrant state
  machines; hook must sit inside the supertile-boundary branch, consume+clear
  `room_transitioning_flags`, manage `dungeon_room_index2`/`_prev`. → §2b control-flow
  contract added. `Rando_DoorArrive` takes uint16 (travel-dest bytes are uint8); arrival
  edge is opposite ⇒ scroll preserved (no fade); spiral header override aliases dest[3/4];
  4-neighbor shutter peek. → §2c/§2d/§2g.
- **Process (agent 3):** wrong precedent (`gen_enemy_shuffle_tables.py` → `gen_entrance_table.py`);
  version-drift made blocking for door slots (re-rated HIGH); Milestone A real gate is the
  flag-ON RAM-compare, not the corpus; line-number anchors → symbol anchors + research-time
  disclaimer; save-tail offset corrected to `@76` reserved; `setup_worktree.py` mirror +
  artifact-lifecycle open question; digest-neutrality must be corpus-regen-proven.
- **Confirmed correct:** sparse-override runtime model; environment-loads-from-dest-header
  (boss-shuffle trap does not apply); the `[25]/[26]/[27]` settings layout + MVP fit;
  `@76 reserved[4]` exists; the per-seed region/edge/added-edge primitives exist (just
  capped + entrance-owned); no week/day estimates.

**Second pass (3 agents → 2 re-reviewers; the architecture validated, mechanical refinements only):**
- **Logic §4c verdict: SOUND + faithful** — the predicate/edge/threshold machinery is feasible
  (per-seed added-edges carry a static-stream `pred_off`; `OP_HAS_AMOUNT` exists; the Cursor
  anchors to any offset) and worst-case-N matches the reference's actual `has_sm_key(N)`
  per-door eval (conservative-safe, confirmed against `Rules.py`/`KeyDoorShuffle.py`). New
  item folded: the **region-cap bump** (~579 room regions > 256 cap), a **separate
  region-override array**, and the **Retro threshold collapse**.
- **Runtime verdict: SOUND, two spiral-specific fixes folded** — the spiral double-path (§2d
  sole-authority) and the second `AdjustAfterSpiralStairs` site (§2b.3); plus hook-after-
  early-returns (§2b.4), the identity-must-exercise-the-rewritten-branch subtlety (§2b.5),
  and uint16 spiral writes (§2d).
- **Save: drift hard-fail via layout digest** named the primary route (the `void`
  `Rando_ActivateSidecarSlot` has no refuse path; the digest compare is the smaller add) (§6).
- **Confirmed:** the four-normal-door story, `@76`, `[27]` fit (deserializer already ignores
  the pad byte), `setup_worktree.py` mirror precedent, the codegen-precedent citation, and
  the ADDED-vs-MODIFIED choices all check out. No residual showstopper; the plan is
  implementation-ready for Milestone A.

## 13. Implementation changelog (the 4-round plan review + as-built deviations)

Between this design and the code, a separate implementation plan went through **four
adversarial review rounds**; the surviving decisions are what shipped. Summary:

- **Round 1** — endorsed **Arch-2** (the oracle, §4c as built) over the §4c static-region
  port, and found the **portal-seeding BLOCKER**: the oracle had no way to know which
  portals of a multi-portal dungeon are enterable. Fix = the committed
  `door_portals.yaml` gate table, shared by generator and oracle. Also: per-dungeon
  `DOORS_ACTIVE` (pinned dungeons evaluate pure vanilla), oracle-returns-false with no
  layout, the drop-key economy (`held = chest keys + reachable drop-key rooms`,
  symmetric prover/oracle), and the memo-per-fixed-point-pass monotonicity argument.
- **Round 2** — surfaced the **door-KIND overlay** as first-class committed scope (a
  relocated key door must be physically real — N1) and a cluster of N-findings folded
  into the plan: the live-stitcher entry point (`DungeonStitcher.generate_dungeon`; the
  reviewer's claim it didn't exist was REFUTED with evidence — the deprecated
  `DungeonGenerator` sibling had been read instead), `RoomData.py` position bytes for
  slot derivation, the palace-index codegen guard, both generation seams
  (slot + headless) named, the non-destructive refusal seam, big-key doors NOT relocated
  in `original` mode, and the Desert Back intensity-1 waiver.
- **Round 3** — hardened the kind overlay: there are **three** raw door-list reader
  seams (not one); the **stateful pos<4 constraint** (door-open state persists only for
  door-list slots 0-3 — relocated key doors need the reference's position-swap); the
  **partner open-bit mirror + neighbor-propagation suppression** (double-spend re-lock /
  unpaid pre-open); deterministic integer-only `ncr` sampling for
  `find_valid_combination`; and the digest-verdict caching note for the refusal seam.
- **Round 4** — sign-off with conditional edits: the pos<4 swap feasibility is evaluated
  **per-half** (BOTH halves of a pair must land at pos<4 — a half left at pos≥4 is
  force-opened at load and voids the mirror).

**As-built deviations from this design** (each argued through the review, none silent):

1. §4c static-region port → **Arch-2 oracle** (two static ops + `DoorExplore_Run`); the
   region-cap bump, per-seed door-edges, threshold family, and separate region-override
   array all became moot (§4c as built).
2. The door tables are **committed**, not gitignored-and-regenerated (§9.7) — CI has no
   reference checkout; `check_door_tables.py` guards consistency.
3. Hole/teleport/straight-stair sites are **not hooked** (intensity-1 scope literalism);
   HC's two vanilla-teleport Normal-typed pairs are flagged + pinned away (§2a as-built).
4. The identity gate exercises the resolver, not a rewritten destination-application arm
   (§2b as-built note — the deliberate softening of §2b.5).
5. Slot-drift refusal lives at **activation** (`Rando_ActivateSidecarSlot` hard-fail),
   not a pre-activation select-file classifier render kind (§6 as-built).
6. Spiral plane: `cur_staircase_plane` still reads the SOURCE header at the transition;
   the destination layer is applied by the post-load fixup from the dest door record
   (§2d as-built) — up↔up pairs and source-derived floor cosmetics accepted as designed.
7. MVP pins (§4d as built): Open/Standard + NoGlitches only, entrance-shuffle mutual
   exclusion (door yields), in-dungeon small+big keys forced, HC pinned in ALL world
   states (not just standard), Swamp pinned. Normalization is silent
   (`apply_derived_rules`), not refuse-with-note.

**Status at spec-update time (2026-06-09):** Stage 0 (codegen + committed tables +
guard), Stage 1 (runtime redirect + hooks), Stage 3 (ops/oracle/portal gates/location
wrap + `bk_restricted` ban), and Stage 4 (settings axis, sidecar @76-79, generation in
both pipelines, activation regen + hard-fail, UI checkbox, spoiler section,
`--door-selftest` CLI) are in the tree. **In flight:** Stage 2 (the stitcher + key
prover behind the `shuffle_doors.h` contract — `shuffle_doors.c` is a stub whose
`DoorShuffle_Generate` returns false, so a `basic` request currently exhausts its
attempts and generation fails cleanly) and Stage 1b (the door-KIND overlay). Open:
`kGeneratorVersion` bump + corpus seed + 3-way regen, the flag-ON RAM-compare identity
run, the playtest matrix, and `docs/randomizer.md`.
