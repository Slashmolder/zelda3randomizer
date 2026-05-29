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

The real entrance-shuffle *runtime* is in the asm repo `C:/src/z3randomizer`
(`entrances.asm`, `tables.asm` StartingAreaExitTable / ExtraHole tables,
`doorframefixes.asm`), NOT the PHP (`EntranceRandomizer.php` shells out to Python;
its "mt_rand" docstring is stale). Translation discipline per `audit.md §0.10`:
read the asm, not the comments.
