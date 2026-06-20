# Design — entrance shuffle (Phase C)

Authored from the `pc-entrance-spike` investigation + playtest (2026-05-29). This
document captures the design decisions; the proposal carries the *why* and scope.

## 0. The one-line model

Entrance shuffle is **one permutation π** (overworld door → interior) that drives
**two independent halves**:

```
                    π  (door → interior), per seed
                   ╱                                ╲
        RUNTIME half                          LOGIC half
   walking through door A                 placer/tracker must know
   physically lands Link in B            door A's region reaches B
                   │                                │
   kOverworld_Entrance_Id overlay        per-seed alternate EDGE table
   (g_asset_ptrs[126] shadow copy)       (Inverted's kRandoEdges_* pattern,
   — SPIKE-PROVEN ✅                       but regenerated from π each seed)
```

Both halves are fed by the same π computed once in `Rando_GenerateSlot`
(`rando_generate.c:138`) — the single generation choke point.

## 1. Key finding: the `RegionRemap` scaffold is the WRONG abstraction (retire it)

Phase A scaffolded `RegionRemap_Lookup` / `Rando_SetRegionRemap` /
`Rando_ResetRegionRemap` (`rando_logic.c:321-335`) as the intended Phase C entrance
hook. Two problems found this session:

1. **It is dead code.** Zero callers in `src/`; identity in every shipped seed.
   The proposal's premise that "Phase B #4a Inverted activated RegionRemap, making
   it production-grade" is **false**. Inverted shipped via a *static alternate edge
   table* (`kRandoEdges_Inverted[]`, `rando_logic.h:226`) plus a per-screen visual
   tile overlay (`inverted_maps_apply.c`) — it never touched the overlay.

2. **It models the wrong thing.** `eval_region_reachable` (`rando_logic.c:139`)
   reads a u16 operand the header documents as `region_id`, passes it through
   `RegionRemap_Lookup`, then indexes the reachability bitset *as a region*. The
   operand is a region, used as a region. But entrance shuffle does not want to
   remap a region lookup — it wants to **rewire which interior-region a door-edge
   terminates at**. In identity mode nobody noticed because `Lookup(x) == x`.

**Decision:** retire `RegionRemap` (delete the indirection; it sits in the hot
`eval_region_reachable` path that 10+ live `OP_REGION_REACHABLE` predicates traverse
every reachability iteration — if it were ever populated with a non-identity table it
would *corrupt* those predicates, so "harmless dead code" understates it: delete, do
not leave dormant). This supersedes the change's original `randomizer-logic` ADDED
requirement, which is rewritten accordingly.

## 2. Logic half — TWO mechanisms, matching the two exit classes

> **CORRECTION (2026-05-29, fresh-eyes review):** an earlier draft of this section
> claimed "interiors are first-class regions; a door is an edge into the interior
> region; entrance shuffle = rewrite that edge." **That is true for dungeons but
> FALSE for caves.** Verified against source — the graph is 31 regions; caves are NOT
> regions. "Ice Rod Cave" / "Aginah's Cave" are **locations** (`type: Chest`,
> `location_registry.yaml:274-279`) bound to an *overworld* region
> (`LightWorld_South`); there is **no cave-interior region and no door-edge into a
> cave**. Dungeons ARE regions with real inbound door-edges
> (`LightWorld_NorthEast → EasternPalace` / `→ PalaceOfDarkness`, in
> `logic_parts/*.yaml`; 67 edges total across all logic files). So the logic half
> needs **two mechanisms**, one per exit class.

### 2a. Caves — per-seed location-region reassignment (NOT edges)
A cave's chest is reachable iff its bound overworld region is reachable; there is no
door traversal in the graph. So shuffling cave entrances on the logic side means:
**each cave-location's *effective region* becomes the overworld region of whichever
door now leads to it.** Two consequences:
- A cave swap **within the same overworld region is a logic no-op** (both reachable
  iff that region is reachable) — only cross-region swaps move reachability.
- The mechanism already exists: `region_override` (`rando_logic.c:477`,
  `rando_placement.c:530`, `rando_spoiler.c:647`) — a per-location region
  reassignment, today keyed by `world_state` (Inverted uses it). Entrance shuffle
  needs the **same field driven per-seed by π** instead of by world_state. This is
  *simpler* than the edge-overlay I originally described, not harder — Stage 1's
  logic is "reassign cave-location regions per π," reusing a shipped, tested path.

### 2b. Dungeons — per-seed edge overlay (Stage 2)
Dungeons ARE regions with inbound door-edges, so here the edge-overlay is correct:
`Logic_ComputeReachability` (`rando_logic.c:378`) runs a fixed-point expansion over a
static `RandoEdgeDef[]` graph; Inverted already proved the graph can be swapped for
an alternate static table (`kRandoEdges_Inverted[]`). At generation, build a per-seed
edge array = base graph with each **dungeon door-edge's** `to_region` rewritten to
π(door)'s dungeon region; internal dungeon edges and event gates stay fixed.

### 2c. Common
`entrance_shuffle == none` (all axes off) ⇒ no region_override-from-π and base edge
graph ⇒ byte-identical reachability to today (corpus invariant preserved). Open
question deferred to apply-time: whether the dungeon door-edge classification lives
in YAML (a `door:` marker) or a generated side-table.

## 3. Runtime half — door overlay + coupling

### 3a. Entry redirect (spike-proven)
`kOverworld_Entrance_Id` (`g_asset_ptrs[126]`, door-slot → entrance-id) is the clean
permutation point. The entry hook (`overworld.c:3342`, `which_entrance =
kOverworld_Entrance_Id[lx]`) then drives the whole interior load
(`dungeon.c:8448 kEntranceData_rooms[which_entrance]` + parallel per-entrance spawn
records). Install = build a shadow copy permuted by π, repoint `g_asset_ptrs[126]`;
teardown restores the original. Hooked to slot lifecycle (`Rando_ActivateSidecarSlot`
/ `Rando_DeactivateSlot`; reference by symbol — line numbers drift). NB: `which_entrance`
is a **u8** (`g_ram+0x10E`), so any entrance-id permutation must stay ≤255.

NB: **entrance-id is not 1:1 with room** (ids 0x42/0x43/0x44 all → room 0x103).
Permute at **interior granularity**, not raw entrance-id — group entrance-ids by
their interior before shuffling.

### 3b. Coupling (enter A → exit A)
ALTTPR's default entrance shuffle is **coupled**. The engine makes this cheap for
the cave class: cave exits restore Link from entry-time-cached `*_exit` shadow vars
via `LoadCachedEntranceProperties` (`overworld.c:1836`). So coupling = capture the
*source* door at entry and cache *its* exit properties (not the loaded interior's).

This is the **exact** door-capture-before-redirect pattern the just-merged Retro
TakeAny already ships: `overworld.c:3349` captures `g_rando_takeany_door_id` before
the redirect "destroys the source identity." Coupled caves reuse that idiom.

**Decision:** coupled is the **baseline**. Decoupled becomes a property of Insanity
mode (Stage 4), not a separate axis.

### 3c. The cave / dungeon fault line (drives staging)
Two exit classes:

| Class | Rooms | Return mechanism | Difficulty |
|---|---|---|---|
| **Cave / single interior** | 0x100–0x17F excl 0x104 | entry-cached `*_exit` (LoadCachedEntranceProperties) | easy — Stage 1 |
| **Dungeon / Link's House / special areas** | < 0x100, 0x104, **and ≥ 0x180** | room-keyed exit-table SEARCH (`kExitDataRooms`) | hard — Stage 2 |

**Review correction:** the search branch is the `else` of `room != 0x104 && room <
0x180 && room >= 0x100`, so it catches **room ≥ 0x180 too** (special-area exits, e.g.
`kSpecialSwitchArea_Exit = {0x180,0x181,0x182,0x189}`) — not just dungeons + Link's
House. Also the search is a hardcoded `int k = 79; do k--;` with **no floor**: a
shuffled room absent from `kExitDataRooms` underflows the array (silent OOB). Stage 2
must bound this search before remapping the exit class.

**Sub-fault-line inside dungeons** (drives Stage 2's own ordering):
- **Single-entrance dungeons** (Eastern Palace, Palace of Darkness, Desert?, etc.) —
  one door, one interior. Swapping EP↔PoD is logically *one edge-overlay rewrite*,
  same machinery as caves. **This is the low-risk dungeon case** (user's EP→PoD
  example) and should land first in Stage 2.
- **Multi-entrance dungeons** (Hyrule Castle ~3 doors, Skull Woods ~4) — their doors
  must stay mutually consistent (all of a dungeon's entrances move as a unit, or are
  individually shuffled only in the wilder modes). Lands after single-entrance.

## 4. Generation: goal-reachable retry

π must keep the goal reachable. `Place_AssumedFill` (`rando_placement.c:629`)
already runs a bounded-retry loop and `Goal_IsCompletable` (`rando_placement.h:72`)
already does the reachability check. Two viable structures:

- **Reject-and-retry** (Stage 1 choice): draw π, build the per-seed edge graph, run
  placement, check `Goal_IsCompletable`; on failure redraw π (bounded attempts).
  Simple; fine while π's success rate is high (coupled caves ≈ always solvable).
- **Constrained construction** (Stage 4 need): build π incrementally, never closing
  off the goal. Required for Insanity, where random π rarely leaves the goal
  reachable. Deferred.

## 5. Settings model — composable toggles, modes as presets

**Correction (supersedes earlier drafts):** there is NO existing `entrance_shuffle`
enum or field. `RandoSettings` (`rando_settings.h`) has no entrance axis; the only
artifact is a disabled UI row stub (`kRow_EntranceShuffle_Disabled`,
`select_file.c:2510`). The original proposal's "Phase A reserved this axis ordinally"
claim is **false** — nothing is reserved. (Verified 2026-05-29.)

### 5a. Composable axes (decision 2026-05-29)
Rather than ALTTPR's single monolithic enum (none/simple/restricted/crossed/
insanity), entrance shuffle exposes **independent boolean axes** — the user chooses
what to swap:

```
  shuffle_cave_entrances    : on/off
  shuffle_dungeon_entrances : on/off
  coupled                   : on/off   (DEFAULT ON — enter A ⇒ exit A)
  cross_category            : on/off   (caves↔dungeons may mix)
  decoupled / per-endpoint  : on/off   (the "Insanity" spice; implies !coupled)
```

The 4 famous ALTTPR modes become **presets** over these axes (so we keep the
recognizable names AND gain a "Custom" mode for free):

| Preset | caves | dungeons | coupled | cross | decoupled |
|---|---|---|---|---|---|
| Simple | ✓ | ✓ | ✓ | ✗ | ✗ |
| Restricted | ✓ | ✓ | ✓ | ✗ (within-category) | ✗ |
| Crossed | ✓ | ✓ | ✓ | ✓ | ✗ |
| Insanity | ✓ | ✓ | ✗ | ✓ | ✓ |
| Custom | user-chosen | | | | |

Each axis maps to exactly one staging step (§6), so a stage ships a *usable toggle*,
not a half-built mode. Tradeoff accepted: a few more settings bits (trivial) and
share-string divergence from alttpr.com (already true — we are a separate generator).

### 5b. Canonical serialization (corpus impact — NOT free)
Today `Settings_CanonicalSerialize` writes content in `out[0..24]` and zero-pad in
`out[25..27]` (`rando_settings.c:101-129`; `kSettingsCanonicalLen = 28`). New axes
append after `out[24]`.

**Honest accounting (corrected after review):** the "byte-identical for free" framing
only holds for **≤3 axes** (they fit the 3 pad bytes; pad was already 0 and axes
default to 0/off, so the default hash is unchanged). We have **5 axes**, so realistic
outcome is a `kSettingsCanonicalLen` bump (28→32) — which per
`[[canonical-size-coupling]]` touches **4 coupled sites** (the `_Static_assert`, ZRSR
file size, CRC offsets, corpus-runner constants), NOT a one-line append. The default
*hash* can still be preserved if the new bytes are appended as 0-default and the size
grows by zeroed bytes, but that must be verified against the corpus, not assumed.
`kGeneratorVersion` bumps regardless. (Apply-time: consider packing the 5 bools into
ONE byte to stay within the 3 pad bytes and dodge the size bump entirely.)

### 5c. Save TLV + spoiler
- **Save TLV**: π is per-seed state that must survive save/load. Phase A reserved a
  TLV chain after the checked-bitmap (`randomizer-save / Forward-compat reserve`).
  Define `TAIL_ENTRANCE_MAP` carrying π (or the seed+settings to regenerate it
  deterministically — cheaper on bytes; decide at apply-time).
- **Spoiler**: `entrance_mapping` section (door → interior) in JSON + text.

## 6. Staging (one change, staged tasks)

Each stage lights up **one composable axis** (§5a) — a stage ships a real, usable
toggle, not a half-built mode.

```
STAGE 1  shuffle_caves        Coupled cave-shuffle. Full vertical slice: π engine
         (coupled)            + per-seed edge overlay + door overlay + coupling +
                              goal-reachable retry + settings axes + picker + save
                              TLV + spoiler. Retires ALL hard engine risk. Shippable.
STAGE 2  shuffle_dungeons     Room-keyed exit class. Single-entrance dungeons FIRST
         (coupled)            (EP↔PoD — low risk), then multi-entrance consistency +
                              Link's House. Reuses the Stage 1 engine.
STAGE 3  cross_category       caves↔dungeons may mix (the "Crossed" feel). Mostly
                              generator-side pool widening; little new runtime.
STAGE 4  decoupled           Per-endpoint independent shuffle (the "Insanity"
                              spice) + constrained-construction retry. Long pole;
                              optional / could split out.
PRESETS  (any stage on)       Once the relevant axes exist, bundle them into the
                              named ALTTPR modes (Simple/Restricted/Crossed/Insanity)
                              + Custom. Cheap once the primitives ship.
```

The engine is built **once** in Stage 1; later stages flip on additional axes. The 4
ALTTPR modes are *presets over the axes*, not 4 separate features.

## 7. Coordination with parallel Retro work

Shared seams (sequence at merge, don't author blind):
- `kGeneratorVersion` + corpus bump — whoever lands second rebases + regenerates.
- Canonical settings serialization — **append-only**; entrance_shuffle appends, so
  Retro digests stay byte-identical.
- `overworld.c` entry hook (`~3342`) — **TakeAny now occupies this**. The entrance
  redirect and the TakeAny redirect both rewrite `which_entrance` here; they must
  compose (entrance shuffle picks the interior; TakeAny's host-room redirect is a
  Retro-only further redirect). Apply-time ordering decision.

## 8. ALTTPR provenance note

The real entrance-shuffle *runtime* is in the `z3randomizer` asm checkout
(`entrances.asm`, `tables.asm` StartingAreaExitTable / ExtraHole tables,
`doorframefixes.asm`), NOT the PHP (`EntranceRandomizer.php` shells out to Python;
its "mt_rand" docstring is stale). Translation discipline per `audit.md §0.10`:
read the asm, not the comments.

## 9. Cross-category (Crossed) engine design — for the next build step

**Status (2026-05-30):** the two logic PRIMITIVES are implemented + tested
(byte-identical when inactive): `Rando_AddEntranceEdge` (case 3) and
`Rando_SetEntranceRegionOverridePred` (case 4) in `rando_logic.c`. The
combined-pool ENGINE that drives them is designed below but not yet built.

**Key finding:** every dungeon door is item-gated (Moon Pearl / Flippers / Book /
Mirror / Hammer / crystals / RescuedZelda-in-Standard — verified against
`logic_parts/*.yaml`). So there is NO "non-gated dungeon" subset to mix freely;
real cross-category needs the cave-behind-dungeon case to inherit the door's
predicate. **The full-reachability gate does NOT protect this** (it evaluates the
MODEL, so a too-permissive model passes a runtime-broken seed) — the model must be
*exactly* right, which is why this needs careful implementation + playtest.

### The combined permutation π over {cave interiors} ∪ {single-edge dungeons}
Exclude multi-source dungeons (Turtle Rock, Ganon's Tower — two inbound door-edges
⇒ ambiguous predicate to inherit) from the cross pool; they stay within the
dungeon-only sub-pool. Each pool endpoint has a door = (overworld region R,
predicate P). Cave door: P = TRUE. Dungeon door: P = its door-edge predicate
(offset/len from the kRandoEdges entry whose `to_region` is the dungeon entry).

For door-of-X → endpoint Y (X = the door's owner), the four cases:
1. **cave→cave**: `Rando_SetEntranceRegionOverride(Y.locs, R_X)`. [exists]
2. **dungeon→dungeon**: `Rando_SetEntranceEdgeOverride(X_entry, Y_entry)` (remap
   X's door-edge to Y's entry; keeps X's predicate). [exists]
3. **cave→dungeon**: `Rando_AddEntranceEdge(R_X, Y_entry, 0,0)` (unconditional —
   cave-door access = being in R_X) AND **remove Y's original door-edge** so Y
   isn't still reachable via its vanilla spot: `Rando_SetEntranceEdgeOverride(
   Y_entry, kVoidRegion=63)` (redirects Y's original edge to an unreachable void
   region; the added edge uses Y_entry directly, bypassing the override). [edge-add exists; the void-removal uses the existing edge-override with region 63 — safe: 63 < kReachabilityMaxRegions(256), no locations/edges bind to it]
4. **dungeon→cave**: `Rando_SetEntranceRegionOverridePred(Y.locs, R_X, P_X.off,
   P_X.len)` — the cave inherits X's door predicate. [exists]

Invariant: π is a bijection, so each dungeon entry region is the override key
exactly once (value = what's behind THAT dungeon's door: another dungeon's entry,
or the void if a cave is behind it) and gets exactly one new inbound (a remapped
edge from a dungeon source, or an added edge from a cave source). No double-set.

### Runtime
- **Door overlay**: unify the two passes — for each door slot, write the target
  endpoint's representative entrance-id (cave OR dungeon; the id sets are
  disjoint so a single combined map works).
- **Cross-class exit coupling**: today's coupling handles 3 of 4 cases already
  (cave-source→anything uses the cached `*_exit`; dungeon-source→dungeon uses the
  room-keyed search override; dungeon-source→cave loads a cave-room ⇒ cached
  branch ⇒ uses cached source dungeon pos ⇒ coupled). The ONE new case is
  **cave-source → dungeon loaded**: the loaded room is <0x100 ⇒ the search branch
  runs, ignoring the cached source cave pos ⇒ decoupled. FIX: set a flag at the
  entry hook when the SOURCE door is a cave (cached-class) and force
  `LoadCachedEntranceProperties()` at the exit regardless of the loaded room
  (the cached `*_exit` already holds the source cave-door overworld position).

### Generation
Combined retry: compute the combined π, apply the 4-case overrides, require FULL
reachability (the existing gate), accept first π where Goal_IsCompletable. Store
under the existing `cross_category` axis (canonical bit 3). Corpus entry to lock it.

**Status (2026-05-30):** §9 is BUILT, tested, committed. Runtime door overlay
(`Entrance_BuildCrossOverlay`), 4-case dispatch (`Entrance_ApplyCrossOverrides`),
and the cave→dungeon exit flag (`g_rando_entrance_force_cached` +
`Rando_EntranceForceCachedExit`) all land. Corpus `c-entrance-cross-open-fast-ganon`
locks it; 62 entries green. Default-off so corpus/selftest stay byte-identical.
Task 3.3 (playtest + audit) is the remaining gate — cross is NOT model-gate-proof.

## 10. Decoupled (Insanity) engine design — for the next build step

**Status (2026-05-30):** NOT built. This section is the design + a load-bearing
asset finding that gates the whole stage. Read before building.

### What decoupled means
Coupled: enter door A ⇒ exit returns you to A. Decoupled ("Insanity"): the exit
side is an INDEPENDENT permutation — leaving interior I drops you at some overworld
door D unrelated to where you entered. Doors become **one-way edges**. Implies
`!coupled`; `apply_derived_rules` already clears coupled when decoupled is set.

### Logic model (the easy half)
The reachability graph is already directed, so decoupled is naturally expressible:
for each (interior I, exit-destination door D) pair, add a directed edge
`region(I) → region(interior-behind-D)`. No coupling assumption. The existing
`Rando_AddEntranceEdge` primitive carries this. Two independent permutations
(entrance π_in : door→interior, exit π_out : interior→door) instead of one bijection
and its inverse. Cross-category composes (the pools already unify in §9).

### Generation (the medium half)
A *random* π_out almost never leaves the goal reachable (one-way doors strand whole
regions), so reject-and-retry (§4) is too sparse. Needs **constrained construction**
(§4, Stage 4 bullet): build π_out incrementally, assigning each exit only to a
destination that keeps every already-placed region reachable — assumed-fill applied
to exits rather than items. The full-reachability gate still backstops the result.

### Runtime (the HARD half — the actual blocker)
To arrive at overworld door D, the engine must set the overworld arrival state
(area index, link x/y, camera + scroll, screen index — the `*_exit` shadow-var set).
Where those come from per destination class:

- **Dungeon / special destinations** (rooms < 0x100, 0x104, ≥ 0x180): a STATIC table
  exists. `kExitData_*` (asset ptrs 130–142: `ScreenIndex, Rooms, Map16LoadSrcOff,
  ScrollX/Y, XCoord, YCoord, CameraXScroll, CameraYScroll, NormalDoor, FancyDoor`)
  is keyed by interior room. The vanilla room-keyed search (`do k--; while(
  kExitDataRooms[k]!=room)`) already loads a full arrival state from it. So to arrive
  at dungeon/special door D: search `kExitData` for the room of D's vanilla interior
  and load that entry. **Buildable today** — it's the §9 dungeon-exit machinery with
  the search key set to the *destination's* room instead of the source's.

- **Cave destinations** (rooms 0x100–0x17F excl 0x104): **NO static arrival table
  exists.** Cave exits use `LoadCachedEntranceProperties`, which reads `*_exit` —
  the overworld position CACHED LIVE at entry (`Dungeon_LoadEntrance` dungeon.c:8373).
  There is no asset giving a cave door's overworld coordinates ahead of time; the
  cave-door overworld position is implicit in the trigger tile and only materializes
  when the player physically stands on it. Decoupled needs to drop the player at an
  *arbitrary* cave door they did NOT enter from, so the live-cache trick cannot
  supply it.

**DECISION (user, 2026-05-30): build FULL support — the new cave-arrival asset.**

### Research resolution (2026-05-30): the engine ALWAYS uses static seed tables
A research pass over both this codebase and the ALTTPR asm (`z3randomizer`
@ dcb0a2b) resolved the open question of "store explicit state vs recompute from
(area, pos)":

- The vanilla exit (`LoadOverworldFromDungeon` else-branch, `overworld.c:1814-1836`)
  loads ~10 SEED fields from `kExitData_*[k]` — `ScrollX/Y, link x/y,
  Map16LoadSrcOff, CameraX/YScroll, ScreenIndex, NormalDoor, FancyDoor, Unk1, Unk3`
  — then `Overworld_LoadNewScreenProperties()` RECOMPUTES only the camera
  *boundaries* from `overworld_area_index`. So the engine needs the seeds; the
  boundaries are derived.
- Even flute/bird-travel landings use a stored seed table
  (`Overworld_LoadBirdTravelPos`, `overworld.c:2013` → `kBirdTravel_*`). Discrete
  arrivals are NEVER pure recompute-from-position in this engine.
- ALTTPR's `StartingAreaExitTable` (`tables.asm:1052`, 20-byte rows) stores the same
  near-full seed set explicitly. Confirmed: that revision is COUPLED-ONLY (grep
  decouple/insanity = 0 hits), so there's no decoupled mechanism to port — we build
  it. But the row shape is the right template, and it equals our `*_exit` shadow set.

⇒ **A static per-cave-door arrival seed table IS required** (same ~10 fields as
`kExitData`, keyed by cave entrance-id). Pure recompute is not an option.

### The asset: `kCaveExitData_*` (cave arrival seed table)
Mirror the `kExitData_*` field set, keyed by cave entrance-id (the 57 cave doors).
Fields per entry (the cached `*_exit` shadow-var set restored by
`LoadCachedEntranceProperties`, `overworld.c:~1860`): `overworld_area_index,
TM/VRAM tilemap index, BG H/V scroll, link x/y, camera x/y scroll-low,
map16_load_src_off, ow_entrance_value (NormalDoor), big_rock (FancyDoor), unk1,
unk3, tile-theme indices`. New `g_asset_ptrs[143..]` slots + `assets.h` readers.

**Population = a CAPTURE pass, not hand-authoring or ROM extraction.** The seed
values are a deterministic function of (area, door-tile pos) THROUGH the overworld
scroll computation — not stored in the ROM for caves and not cheaply re-derivable in
Python. So populate by reusing the engine: a one-shot capture mode walks each cave
door slot, drives the overworld load + entry caching (the exact `Dungeon_LoadEntrance`
path), reads the resulting `*_exit` set, and emits the table. Baked into
`zelda3_assets.dat` via `compile_resources.py` (and a `restool.py` flag). This
guarantees byte-correct arrival because it IS the runtime computation. Verified by
playtest (visit each captured door, confirm clean arrival).

### Runtime exit redirect
At `LoadOverworldFromDungeon`, a DECOUPLED interior's exit must arrive at the
independently-assigned destination door (π_out), not the cached source. New branch:
look up the destination door's class — dungeon/special ⇒ key the existing room
search on the destination's room (already supported); cave ⇒ load the seeds from
`kCaveExitData_*[dest_entrance_id]` (the new asset) directly into the arrival vars
(same writes as the else-branch, sourced from the cave table). A per-seed
`g_rando_decoupled_exit[interior]` map (built at slot-load from π_out) drives it.

### Logic model — one-way edges
Decoupled = two independent permutations: π_in (door→interior, the existing door
overlay) and π_out (interior→door). Reachability is directed: location reachability
inside I depends only on π_in (you enter I via its door); π_out controls where you
emerge, which can open or strand overworld regions. So per interior I add:
`overworld_region(door with π_in=I) → I_entry` (gated by the door predicate; this is
the entry side, shared with coupled) AND `I_exit_region → overworld_region(π_out(I))`
(unconditional — you can always walk out). Coupled is the special case π_out=π_in⁻¹
where the exit edge returns whence you came (adds no new reach). The existing
`Rando_AddEntranceEdge` (predicate-carrying) expresses both. Cross-category composes.

### Generation — constrained construction
A random π_out almost never keeps everything reachable (one-way doors strand whole
regions), so reject-and-retry is too sparse. Build π_out incrementally: assumed-fill
over EXITS — repeatedly pick an unplaced interior's exit and assign it to a
destination that (with assumed access to all unplaced items) keeps the frontier
growing, never closing the goal off. The full-reachability gate (§2.2a) backstops the
final result. Deterministic in (seed, attempt) like the other pools.

### Phasing (each milestone independently testable where possible)
- **D.1 Logic one-way edges** — π_out edges + the directed model. Headless-testable
  (corpus digests with a decoupled seed). No asset needed.
- **D.2 Constrained-construction generation** — the exit assumed-fill + retry. Headless
  (corpus + reachability gate). The algorithmic centerpiece.
- **D.3 Cave-arrival asset + capture tool** — the new `kCaveExitData_*` table +
  populate pass + `assets.h` reader + `.dat` bump. Playtest-verified.
- **D.4 Runtime exit redirect** — wire π_out → arrival (dungeon via room search, cave
  via the new table). Playtest-verified.
- **D.5 Spoiler/tracker** for one-way doors; UI: enable the Insanity preset.

Settings bit `kEntranceAxis_Decoupled` already exists + normalizes (`decoupled`
implies `!coupled`). Default-off ⇒ corpus byte-identical until a decoupled seed is
added.
