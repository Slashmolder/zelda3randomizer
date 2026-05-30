# Apply-time implementation plan — entrance shuffle (Phase C)

Authored 2026-05-30 from a source-grounded sweep (three Explore agents over
settings/save, logic graph, runtime door redirect) + the playtested
`pc-entrance-spike` code (`SpikeEntrance_Install/Teardown` in that worktree's
`rando.c`). This doc pins the apply-time decisions that `tasks.md` Stage 0
deferred, with file:line grounding. The runtime engine is **default-off**, so
every non-entrance-shuffle seed stays byte-identical (corpus invariant).

> I cannot playtest (the user does). The runtime door-redirect + coupling half is
> the only part no automated check covers (`CLAUDE.md`: "playtest is the only
> reliable net"). It is implemented carefully mirroring the merged Retro TakeAny
> idiom and the playtested spike; §"Playtest checklist" is the hand-off.

---

## Grounded facts (verified this session)

| Fact | Source |
|---|---|
| `kOverworld_Entrance_Id` = `g_asset_ptrs[126]`, `uint8[]` door-slot→entrance-id | `src/assets.h:254` |
| Entry hook: `which_entrance = kOverworld_Entrance_Id[lx]`; TakeAny redirect right after | `src/overworld.c:3342-3356` |
| `which_entrance` is u8 at `g_ram+0x10E` | `src/variables.h:160` |
| Interior load keys *everything* off `which_entrance` (room, scroll, player, camera, music…) | `src/dungeon.c:8463-8499` |
| Caves (room `[0x100,0x180)` excl `0x104`) exit via `LoadCachedEntranceProperties()` keyed by cached `*_exit` shadow vars | `src/overworld.c:1791-1792, 1836-1872` |
| Dungeons / `0x104` / `≥0x180` exit via room-keyed SEARCH `do k--; while(kExitDataRooms[k]!=room)` — **no floor** (silent OOB) | `src/overworld.c:1795-1816` |
| Slot lifecycle install/teardown points | `Rando_ActivateSidecarSlot` / `Rando_DeactivateSlot`, `src/rando/rando.c` |
| TakeAny captures source door before redirect: `g_rando_takeany_door_id = lx+1` | `src/overworld.c:3349-3356` |
| `RegionRemap_Lookup` has 1 caller (`eval_region_reachable`), is identity; `Rando_SetRegionRemap`/`Rando_ResetRegionRemap` have **0 callers** | `src/rando/rando_logic.c:315-332, 139-154` |
| `region_override` per-location reassignment exists, keyed by world_state via `Rando_FindPredicateOverride` | `rando_logic.c:469-477`, `rando_spoiler.c:647` |
| `kRandoEdges_Inverted[]` alternate edge table walked when `world_state==Inverted` | `rando_logic.c:432-444`, `logic_data.c:2285` |
| Canonical layout: content `out[0..24]`, zero-pad `out[25..27]`, `kSettingsCanonicalLen=28` | `rando_settings.c:92-131` |
| Default canonical bytes + hash pinned in `Settings_SelfCheck` | `rando_settings.c:217-245` |
| Slot header 80 bytes; Phase B hints used the **additive reserved-byte** pattern (`settings_ext_present`@65, world_state@68, reserved[10]@70) | `rando_save.h:50-130`, `rando_save.c:108-146` |
| Slot on-disk = header + flat placement table + checked bitmap; size via `RandoSave_SlotOnDiskSize` | `rando_save.c:186-249` |
| `kGeneratorVersion = 37u`; bump-trigger globs include `src/rando/**` + `assets/rando/**` | `rando.h:17`, `check_generator_version.py` |
| CSV parser: `KEY_` enum (currently ≤22) + 32-bit `seen` bitmask | `rando_settings.c:526-553` |

---

## Stage 0 decisions (final)

### 0.1 — Retire `RegionRemap` (minimal)
`eval_region_reachable` (`rando_logic.c:139`) reads a u16 operand the header
documents as a region id, then passes it through identity `RegionRemap_Lookup`.
**Decision:** delete the indirection — use the operand directly as `region_id` —
and delete `RegionRemap_Lookup`, `Rando_SetRegionRemap`, `Rando_ResetRegionRemap`,
the two file-static globals, and their `rando_logic.h` decls. Keep
`OP_REGION_REACHABLE` and `eval_region_reachable` (10+ live predicates use the
opcode). This is the design §1 "delete, do not leave dormant" path; it's a no-op
on reachability (Lookup was identity) so corpus stays byte-identical. Verified 0
callers to Set/Reset before deleting.

### 0.2 — Entrance data home: `assets/rando/entrance_registry.yaml` + generated table
A new YAML registry (built by the data agent this session) enumerates each cave
**interior**: `interior_id`, room(s), entrance-id(s), vanilla `region`, member
`locations: [ids]`. A codegen step (extend `assets/rando_logic_gen.py` or a small
`gen_entrance_registry.py`) emits `src/rando/entrance_data.c` (`kRandoCaveInteriors[]`).
Dungeon door-edge marking (Stage 2) extends the same registry with a `dungeon:`
section tagging the static edges that are door-edges. Chosen over a scattered
YAML `door:` marker so the classification is in one auditable file.

### 0.3 — Save representation: header reserved bytes + regenerate π at load (NOT a TLV)
**Revised after plan review (H4/M1).** The slot file format has **no TLV-skip
infrastructure** — `RandoSave_DeserializeSlot` reads header + flat table + bitmap
and returns `SlotOnDiskSize` exactly, with no trailing loop or magic; the TLV-skip
machinery lives in the *different* `rando_snapshot_tail.c` format. Worse, three
multi-slot packers sum `SlotOnDiskSize` to lay 3 slots back-to-back, so a
variable tail would corrupt slot offsets. A TLV is therefore net-new format work,
not a free append. **Decision:** persist π the cheap, robust way — store two bytes
in the existing header `reserved[10]` tail (@70 `entrance_axes`, @71
`entrance_attempt`) and **regenerate π at slot-activate** from `(seed [from
share_string], entrance_axes, attempt)`. π for a given attempt is a pure function
of the deterministically-enumerated pool + seed + attempt (no RNG/clock per
`check_determinism`), and the restore path already relies on deterministic pool
enumeration, so this is fully reproducible (M1). This mirrors the Phase B hints
"regenerate from a 3-byte settings extension in the reserved tail" precedent
(`settings_ext_present`@65) exactly — no slot-size change, no packer hazard, no
corpus on-disk-size coupling. The stubbed `TAIL_ENTRANCE_MAP` TLV is **not built**;
the save spec is reconciled to the as-built reserved-byte+regen approach. When
`entrance_axes == 0` the bytes are 0 and the slot is byte-identical to a Phase B
slot (older binaries already ignore the reserved tail).

### 0.4 — Settings: pack 5 axes into the existing pad byte `out[25]` (NO size bump)
Add 5 `uint8` fields to `RandoSettings` (`shuffle_cave_entrances`,
`shuffle_dungeon_entrances`, `coupled`, `cross_category`, `decoupled`). Serialize
them **bit-packed into `out[25]`** (was zero pad):
```
out[25] bit0 shuffle_cave_entrances
        bit1 shuffle_dungeon_entrances
        bit2 coupled
        bit3 cross_category
        bit4 decoupled
```
`apply_derived_rules` normalizes `coupled=cross_category=decoupled=0` whenever
`!(shuffle_cave||shuffle_dungeon)` so the **default (no shuffle) packs to 0x00** →
`kExpectedCanonical`/`kExpectedHash` unchanged, `kSettingsCanonicalLen` stays 28,
**no `[[canonical-size-coupling]]` cascade**. `coupled` defaults to 1 in the struct
(on when shuffle enabled) but normalizes to 0 when no shuffle. `decoupled` implies
`!coupled` (normalize: decoupled⇒coupled=0). `kGeneratorVersion` 37→38; corpus
digests verified byte-identical (only the manifest version field bumps).
CSV keys: `shuffle_cave_entrances`, `shuffle_dungeon_entrances`, `coupled`,
`cross_category`, `decoupled` (+ `entrance_preset` sugar). 5 new `KEY_` enums
(total ≤28, under the 32-bit `seen` limit).

### 0.5 — Presets are UI sugar over axes (no stored enum)
Simple/Restricted/Crossed/Insanity/Custom are realized by the picker writing the
axis bits (table in design §5a). No extra canonical byte — the axes ARE the state.
A preset is *derived* for display by matching the axis combination.

---

## Stage 1 — coupled cave shuffle (the vertical slice)

New module `src/rando/shuffle_entrance.{c,h}`.

### Permutation engine (runtime-table-driven, no hardcoded ids)
- `Entrance_EnumerateCavePool(out_pool)` — scan live `kOverworld_Entrance_Id`
  (size `g_asset_sizes[126]`); for each door-slot whose entrance-id's
  `kEntranceData_rooms[id]` is a cave room, record (door_slot, entrance_id). Group
  by interior (room) so multi-entrance interiors move as a unit. Mirrors the
  spike's `SpikeEntrance_IsCave`.
- `Entrance_ComputeCavePermutation(pool, settings, seed, attempt) → assignment[]`
  — seeded Fisher–Yates over the pool (deterministic RNG, no `rand`/`time` per
  `check_determinism`). Coupled ⇒ a single permutation of interiors over slots.
- The logic-side region map is derived from the SAME assignment using the
  registry's per-interior vanilla region (closed form: slot k's region = pool
  interior k's vanilla region; interior now at slot k inherits region k).

### Logic half — per-seed cave region override (NOT edges)
- Add a per-seed mutable override consulted by `Logic_ComputeReachability` *after*
  the static `Rando_FindPredicateOverride`, taking precedence: a small
  `g_entrance_region_override[location_id]` (0xFFFF = none), populated from the
  assignment at generation/activation. Reuses the exact `effective_region`
  mechanism at `rando_logic.c:469-477`.
- Closed form (registry-confirmed): for each cave-door pool slot k (fixed region
  R_k = the location_registry region of slot k's vanilla cave-location), the
  interior now assigned to slot k gets `region_override = R_k` for all its member
  locations. R_k is exactly the "overworld-area region" location_registry already
  stores for cave locations, so the model is self-consistent with the placer.
- `entrance_axes==0` ⇒ array all-0xFFFF ⇒ identity ⇒ byte-identical reachability.
- **Inverted scope (review H2):** under `world_state==Inverted`, cave locations
  ALREADY carry a static `region_override` from `Rando_FindPredicateOverride`, and
  the registry's regions are Standard-layout — so a per-seed override would clobber
  the Inverted region and risk a softlocking seed. **Stage 1 disables entrance
  shuffle when `world_state==Inverted`** (the generator treats the axes as off +
  the picker greys them out), documented as a known limitation. Inverted⊗entrance
  needs an Inverted-layout registry (future).

### Generation — goal-reachable reject-and-retry
- In `Rando_GenerateSlot` (`rando_generate.c`) / `Place_AssumedFill` harness: draw
  π, populate the cave region override, run placement, `Goal_IsCompletable`; on
  fail redraw (bounded attempts, store accepted `attempt` index only for spoiler).
  Coupled caves are ≈always solvable (design §4).

### Runtime half — door overlay (coupling is AUTOMATIC for caves)
**Revised after plan review (H1) + source read of `Dungeon_LoadEntrance`.**
`Dungeon_LoadEntrance` (`dungeon.c:8373-8397`) caches the `*_exit` shadow vars
from the **live overworld position** (the SOURCE door A) at entry, *before* the
interior loads from `which_entrance` (`dungeon.c:8447+`); the interior load never
touches `*_exit`. So on cave exit, `LoadCachedEntranceProperties`
(`overworld.c:1836`) restores source door A — **the overlay alone yields COUPLED
return for caves**. This matches the design's *conclusion* (coupled = baseline,
decoupled = Stage 4) even though the design's *mechanism* note (§3b: "cache the
source door's props") had the caching backwards. The spike doc's "decoupled" label
was an unverified prediction. So Stage 1 runtime = **just install the overlay** —
no hand-written `*_exit` caching (that would fight the engine's own cache, review
H1). **#1 playtest item:** confirm exit returns to the SOURCE door; if it does NOT,
the documented fallback is a source-door exit override (deferred, not built blind).
- **Door overlay** (`Entrance_InstallOverlay`/`Teardown`): owned shadow of
  `kOverworld_Entrance_Id`, repoint `g_asset_ptrs[126]`; install in
  `Rando_ActivateSidecarSlot` (after hints block), restore in
  `Rando_DeactivateSlot` (before placement teardown). Exactly the spike's
  mechanism, productionized (owns lifetime; restores original pointer).
- **Pool guard (review M4):** `Entrance_EnumerateCavePool` asserts every pooled
  interior's room stays in the cached-exit class `[0x100,0x180)\{0x104}`, so a
  shuffled cave can never fall into the room-keyed exit SEARCH. Additionally
  harden that search's missing floor (`overworld.c:1795` `do k--` has no `k>0`
  guard — silent OOB) with a one-line floor now; it protects every later stage.
- **Compose with TakeAny**: the overlay changes which interior
  `kOverworld_Entrance_Id[lx]` names; TakeAny's host-room redirect then runs on top
  (Retro-only). They compose because the overlay only changes the table the hook
  reads — TakeAny still reads the (now permuted) value and may further-redirect.

### Settings / save / spoiler / UI
- Wire `shuffle_cave_entrances` + `coupled` into the generator (other axes off).
- `TAIL_ENTRANCE_MAP` TLV write/read (0.3).
- Spoiler `entrance_mapping` section (JSON + text) between `placements` and
  `sphere_data` (`rando_spoiler.c:264`): door→interior list, marked coupled.
- Native settings window (`rando_window/game_config_panels.cpp`): cave-shuffle +
  coupled toggles. Switch in-game picker if applicable (PC compiles it out).

### Verify
- `kGeneratorVersion` 37→38; corpus regen → confirm default digests byte-identical.
- `--rando-selftest` (+ new `Entrance_SelfCheck`), corpus green, audit-guard /
  determinism / codegen / init-order / generator-version checks.
- **Playtest** (user): see §Playtest checklist.
- Fresh-eyes audit agent before declaring Stage 1 done.

---

## Stage 2 — dungeon entrances (room-keyed exit class)

- Bound the OOB exit search first (`overworld.c:1795` `do k--` has no floor) before
  remapping the exit class — a shuffled room absent from `kExitDataRooms`
  underflows. Add a floor + fallback.
- Single-entrance dungeons first (EP↔PoD): one door-edge rewrite each. Per-seed
  **edge overlay** (mirror `kRandoEdges_Inverted` selection): build a per-seed edge
  array = base graph with each dungeon door-edge's `to_region` rewritten per π;
  internal edges + event gates fixed. Registry's `dungeon:` section marks door-edges.
- Multi-entrance dungeons (HC ~3 doors, SW ~4) move as a unit / consistently.
- Link's House (`0x104`) folded into the room-keyed class.
- Prize/medallion gates stay tied to the dungeon, not the door (watch the
  `[[prize-shuffle-bit-gates]]` class).

## Stage 3 — cross_category
- Widen the permutation pool to mix caves↔dungeons (generator-side); reuse the
  Stage 1+2 runtime. Cross-class coupling needs both exit mechanisms wired.

## Stage 4 — decoupled / Insanity (optional, may split)
- Per-endpoint independent shuffle (implies `!coupled`). Constrained-construction
  retry (random π rarely leaves the goal reachable). One-way spoiler/tracker.

## Presets
- Picker bundles axes into Simple/Restricted/Crossed/Insanity/Custom (UI-only).

---

## Cross-cutting
- Backward-load: a `generator_version=37` slot loads on 38 with an info warning, no
  regen; an entrance TLV from a newer build is skipped by length.
- Append-only registry check if ids grow.
- `docs/randomizer.md` + README status checklist.

## Playtest checklist (hand-off — the only reliable net)
1. Default seed (no entrance shuffle): everything byte-identical, plays as today.
2. `shuffle_cave_entrances=true` (Open or Standard): enter a recognizable cave → a
   DIFFERENT interior loads, geometrically correct (Link spawns right, camera ok),
   walkable; **exit returns to the SAME overworld door** (coupled — the #1
   unverified seam; if it exits at the WRONG door the engine is decoupled and the
   doc fallback in §Runtime applies).
3. **Multi-door interiors (audit M2 — highest-value check):** the tavern
   (`0x42/0x43/0x44`), snitch house (`0x3E/0x3F`), and ice-rod/good-bee
   (`0x56/0x84`) each have 2–3 overworld doors. Enter the shuffled seed through
   EACH door of such a cave and confirm (a) both land in the SAME permuted
   interior and (b) each exits back to its own source door. The logic models these
   as one region per interior; if a cave's two doors are actually in different
   reachability regions, a check could be mis-gated.
4. Save + quit inside a shuffled cave, reload: same π restored (header @70/@71 →
   regenerated π); exit still returns to source door.
5. Completability: reach the goal on a shuffled seed; the spoiler `entrance_mapping`
   should match the doors you actually walk through.
6. **Stage 2 dungeons** (`shuffle_dungeon_entrances=true`, Open/Standard): the 6
   single-entrance dungeons (PoD, Swamp, Thieves' Town, Ice Palace, Tower of Hera,
   Agahnim's Tower) shuffle among themselves. Enter one's overworld door → a
   DIFFERENT dungeon loads (spoiler `dungeon_entrance_mapping`). **Coupled exit is
   the #1 dungeon check:** beat/leave the loaded dungeon and confirm you return to
   the door you ENTERED (not the loaded dungeon's vanilla door) — especially test
   entering a land dungeon's door that now loads Ice Palace, then exiting: you must
   NOT be stranded on Ice Palace's lake. Also: enter, then mirror/save-quit/die, and
   confirm you don't end up at a wrong/stale door (the coupling global is consumed on
   normal exit; abnormal exits are the known-incomplete edge cases). Goal reachable.
