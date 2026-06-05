# Tasks — entrance shuffle (Phase C)

Staged per `design.md §6`. The engine is built once in Stage 1; later stages widen
the pool + exit class. Each stage is independently playtestable. The change archives
when Stage 1 (minimum) is shippable; Stages 2–4 may complete here or split into
follow-on changes.

> **Archive readiness (2026-06-02):** NOT archive-ready as-is. Stages 1-3 are built and
> owner-playtested, but `specs/randomizer-shuffles/spec.md:9,21` commits an **Insanity
> mode SHALL + scenario** that is NOT built (Stage 4 deferred, plus Skull Woods §2.3 and
> Link's House §2.4). Archiving as-is would write an unbuilt mode into the canonical
> `randomizer-shuffles` spec. Before archiving: carve the Insanity scenario (and any
> other deferred-stage SHALLs) into a follow-on change so the delta matches what ships.
> Also at archive-time reconcile the pre-existing canonical `randomizer-logic` requirement
> "Per-seed RegionRemap overlay for entrance shuffle" — this change's design retires
> RegionRemap in favor of the per-seed logic edge overlay, so the delta must MODIFY/replace
> that baseline requirement rather than leave the stale RegionRemap mechanism in canon.
> (Owner chose to keep this active on 2026-06-02.)

> Provenance discipline: runtime facts from the asm repo `C:/src/z3randomizer`
> (`entrances.asm`, `tables.asm`, `doorframefixes.asm`), placement/logic facts from
> `../alttp_vt_randomizer`. Read source, not comments (per `design.md §8`).

## Stage 0 — groundwork / decisions (no runtime change)

- [x] 0.1 Retire `RegionRemap`: confirmed zero install callers (`Rando_Set/Reset`)
      + identity-only `RegionRemap_Lookup`; deleted all three + the two file-statics
      + header decls + the `eval_region_reachable` indirection (operand is used
      directly as `region_id`). Updated `op_registry.yaml` OP_REGION_REACHABLE
      semantics comment. Corpus byte-identical (was a no-op). See `apply_plan.md` 0.1.
- [x] 0.2 Door-edge classification home: new `assets/rando/entrance_registry.yaml`
      (38 cave interiors, all 57 entrance-ids) is the data home; the per-interior
      `region` + member `locations` drive the cave region-override. Dungeon
      door-edge marking (Stage 2) extends the same file. (See `apply_plan.md` 0.2.)
- [x] 0.3 Save representation: **revised** — NOT a TLV (the slot format has no
      TLV-skip infra; review H4). Store `entrance_axes`@70 + `entrance_attempt`@71
      in the header reserved tail and regenerate π at slot-activate from
      (seed, axes, attempt). Mirrors the Phase B hints reserved-byte+regen precedent.
      (See `apply_plan.md` 0.3.)
- [x] 0.4 Added the 5 composable entrance axes to `RandoSettings` + bit-PACKED them
      into the existing zero-pad byte `out[25]` (NO `kSettingsCanonicalLen` bump,
      NO canonical-size-coupling cascade). `apply_derived_rules` normalizes
      coupled/cross/decoupled→0 when no shuffle active ⇒ default byte 0x00 ⇒ corpus
      byte-identical. CSV keys + deserialize + Settings_SelfCheck round-trip added.
      `kGeneratorVersion` 37→38. (See `apply_plan.md` 0.4.)
- [x] 0.5 Presets (Simple/Restricted/Crossed/Insanity/Custom) are **UI sugar over
      the axes** (no stored enum) — decided in `apply_plan.md` 0.5; picker wiring in
      Stage 1.13 / Presets P.1.

## Stage 1 — functional: coupled cave-shuffle, one mode (the vertical slice)

### Permutation engine
- [x] 1.1 Enumerate cave/single-interior entrances; grouped by interior in the
      static `kCaveInteriors[]` table (38 interiors / 57 ids; entrance-id NOT 1:1
      with room). `shuffle_entrance.c`.
- [x] 1.2 `shuffle_entrance.{c,h}`: `Entrance_ComputePermutation` — seeded
      Fisher–Yates (xoshiro via rando_rng) over the cave pool.
- [x] 1.3 Coupled pairing = a single bijection (baseline).

### Logic half  (caves = REGION reassignment, NOT edges — design §2a)
- [x] 1.4 Per-seed cave-location region override driven by π. New
      `Rando_{Begin,Set,Clear,Get}EntranceRegionOverride` in `rando_logic.c`,
      consulted in the location loop AFTER the static override (Open/Standard
      carry none, so no clobber). Closed form: interior now behind door i inherits
      i's vanilla region. Registry↔logic regions cross-validated in the self-check.
- [x] 1.5 Inactive (axes off) ⇒ override inactive ⇒ byte-identical reachability
      (corpus 57/57 byte-identical for the default seeds).
- [x] 1.6 Goal-reachable reject-and-retry around `Place_AssumedFill` /
      `Goal_IsCompletable` in both generation paths (rando_generate.c + CLI).

### Runtime half
- [x] 1.7 Door overlay: owned shadow of `kOverworld_Entrance_Id`, repoint
      `g_asset_ptrs[126]`; install/teardown on slot lifecycle (rando.c).
- [x] 1.8 Coupling: **automatic for caves** — `Dungeon_LoadEntrance` caches the
      SOURCE overworld position at entry (review H1; supersedes the design §3b
      manual-cache plan). No hand-written `*_exit` caching (would fight the engine).
- [x] 1.9 Composes with TakeAny: the overlay only changes the table the entry hook
      reads; TakeAny's host-room redirect (Retro-only) runs on top. (Retro is out of
      Stage 1 scope, so no live interaction yet — verified by `Entrance_IsActive`.)

### Settings / save / spoiler / UI
- [x] 1.10 `shuffle_cave_entrances` + `coupled` wired into the generator (Open/
      Standard only via `Entrance_IsActive`); other axes stay off until their stage.
- [x] 1.11 Save: header `entrance_axes`@70 + `entrance_attempt`@71 + regenerate π at
      load (NOT the stubbed TLV — slot format has no skip infra; see 0.3 / review H4).
- [x] 1.12 Spoiler `entrance_mapping` section (JSON + text).
- [x] 1.13 Native settings window: live cave-shuffle toggle + coupled indicator
      (Open/Standard gated). In-game Switch picker: deferred (PC compiles it out;
      Stage 1 is PC-first).

### Verify
- [x] 1.14 `kGeneratorVersion` 37→38; corpus regen; default digests byte-identical.
- [x] 1.15 `--rando-selftest` (+ `Entrance_SelfCheck`) + corpus(57, incl. 2 entrance
      entries) + determinism/audit-guard/codegen/gen-version checks all green.
- [x] 1.16 **Playtest CONFIRMED** (USER, 2026-05-30): cave shuffle works and is
      coupled (enter shuffled cave → different interior → exit returns to the SOURCE
      door). Remaining checklist nice-to-haves (multi-door caves per audit M2,
      save/load π round-trip) not separately re-confirmed but the core seam is good.
- [x] 1.17 Fresh-eyes audit pass — DONE. 1 HIGH (override leak on total placement
      failure) + 2 MED (Inverted/Retro hash bit; unclamped public loop) + 3 LOW all
      fixed (commit dc55487); core closed-form / coupling / save-regen confirmed
      correct. M2 (multi-door region split) flagged as the top playtest item.

## Stage 2 — `shuffle_dungeon_entrances` (room-keyed exit class)

Single-entrance dungeons FIRST (EP↔PoD — low risk), then multi-entrance.

> Grounded exit facts (read this session): dungeon/special/`0x104` exits go through
> the room-keyed SEARCH `int k = 79; do k--; while (kExitDataRooms[k] != room);`
> at `overworld.c:1795-1796` (the `else` of `room != 0x104 && room < 0x180 &&
> room >= 0x100`, so it also catches `room >= 0x180` special areas). The search has
> **no floor** — a shuffled room absent from `kExitDataRooms` underflows (silent
> OOB). Stage 1 caves provably never hit this (Entrance_SelfCheck asserts pooled
> rooms ∈ [0x100,0x180)\{0x104}), so the floor guard is deferred to here.

- [x] 2.0 **Hardened the exit-search floor** (`overworld.c:1796`) — `k > 0`. Vanilla
      OOB can't occur (every vanilla room is in `kExitDataRooms`), so RAM-compare is
      unaffected; defends shuffled exits + every later stage.
- [x] 2.1 Room-keyed exit COUPLING: the entry hook captures the SOURCE dungeon's
      room (`Rando_EntranceCoupledExitRoom`, `g_rando_entrance_exit_room`); the exit
      search keys on it so the player returns to the entered door (avoids stranding,
      e.g. Ice Palace lake without flippers). Caves auto-couple; dungeons don't, so
      this is required. Runtime — playtest-pending.
- [x] 2.2 **Dungeon pool = 10 of 12** (`kDungeons[]`): the 6 clean single-entrance
      (PoD, Swamp, Thieves' Town, Ice Palace, Tower of Hera, Hyrule Castle Tower)
      + Misery Mire (medallion in door predicate) + Eastern Palace (its real entry
      is the Lobby region; the "2nd region" is empty/vestigial) + Desert Palace &
      Turtle Rock (MAIN door only — the extra "contained" doors stay vanilla; the
      dungeon stays reachable via them, logic treats that as a conservative extra).
      Shared door overlay (cave + dungeon passes on disjoint id sets).
- [x] 2.2a **Full-reachability gate** — the entrance retry rejects any π that
      strands placements (not just goal-incompletable ones), in both generation
      paths. Caught + fixed a real bug where GT's crystal-gate circularity produced
      a seed with 9 unreachable locations.
- [x] 2.3a **Ganon's Tower** as an ADVANCED OPT-IN (`shuffle_ganons_tower_entrance`,
      default off) → 11th dungeon when on. Its crystal-tower gate travels with the
      door, but the full-reachability retry gate (2.2a) rejects the circular
      permutations and finds a clean one (verified: 12/12 fully-reachable at
      crystals.tower=7 in testing; never ships an unreachable seed). User can lower
      crystals.tower if a seed won't generate.
- [ ] 2.3 **Deferred**: Skull Woods only (truly many separate entrances — needs a
      multi-entrance move-as-a-unit mechanism). Documented in `shuffle_entrance.c`
      + `entrance_registry.yaml`.
- [ ] 2.4 Link's House (room 0x104) special-case — not needed for the 6 (deferred).
- [x] 2.5 Logic: per-seed **dungeon EDGE overlay** (`Rando_*EntranceEdgeOverride`)
      remaps each door-edge's `to_region` per π, keyed by the (unique) entry region;
      the door's access predicate stays put. Self-check cross-validates exactly-one
      inbound edge per entry region (drift guard). Prize/medallion gates: the 6
      clean dungeons are non-medallion; prizes stay tied to the dungeon region (only
      the door-edge moves), so `[[prize-shuffle-bit-gates]]` is unaffected.
- [x] 2.6 Save: dungeon bit in `entrance_axes` + shared `entrance_attempt`;
      `Entrance_RuntimeInstall` regenerates both pools at slot-load.
- [x] 2.7 Playtest CONFIRMED (USER, 2026-05-30): dungeon shuffle works and the
      coupled EXIT is correct — entering Eastern Palace's randomized door loads a
      different dungeon and leaving returns Link to EP's own door (not the loaded
      dungeon's), so no stranding. Fresh-eyes audit DONE earlier (1 HIGH + 1 LOW
      fixed; direction-agreement / no-collision / save-regen / no-double-remap
      verified). The highest-risk seam is now validated.
- [x] 2.8 **FIXED — medallion gate travels with the SPOT** (audit 2026-05-30; user
      decision: "the requirement should be tied to the entrance not the dungeon").
      Was: the logic attached `OP_MEDALLION_OPENS` to the INTERIOR `Entrance→Lobby`
      edge while `Entrance_ApplyEdgeOverrides` remapped the APPROACH edges, so after a
      swap the model evaluated the LOADED interior's gate but runtime keeps the
      barrier at the source spot → permissive softlock when MM/TR is the SOURCE.
      FIX (kGenVer 44): the dungeon edge-override (Stage 2 + cross Stage 3) now keys
      on each dungeon's **interior (lobby) region** — the `to_region` of its gated
      door edge — via `interior_region_name` / `dungeon_override_key()` in
      `shuffle_entrance.c`. So MM/TR's medallion-bearing `Entrance→Lobby` edge is what
      gets remapped, keeping the medallion predicate tied to the source spot; the
      approach edges (and the `Entrance` waypoint) stay put. For the 8 single-region
      dungeons interior == entry, so behavior + digests are unchanged; only the 4
      MM/TR-involving corpus seeds changed (regenerated). Cross-pool eligibility still
      keys on `entry_region_name`, so TR stays excluded from cross (2 approach edges) —
      no pool-composition change. `Entrance_SelfCheck` now also validates each
      interior region resolves, id < 64, and is a door-edge `to_region`. **Runtime
      match still wants a playtest** (model↔runtime is not gate-protected): confirm a
      seed where TR/MM's door leads to a non-medallion dungeon still demands the
      medallion at the spot, and a non-medallion dungeon's door leading to TR/MM is
      free at its own spot. See [[entrance-shuffle-medallion-gate-mismatch]].

## Stage 3 — `cross_category` ("Crossed" feel)

- [x] 3.0 **Logic primitives built + tested** (`design.md §9`): `Rando_AddEntranceEdge`
      (dungeon-behind-cave) + `Rando_SetEntranceRegionOverridePred` (cave-behind-
      gated-dungeon, inherits the door predicate). Byte-identical when inactive.
      Finding: ALL dungeon doors are item-gated, so cross needs predicate inheritance.
- [x] 3.1 **Combined-pool engine** (`design.md §9`, built): unified π over caves +
      single-edge dungeons (exclude TR/GT — multi-source); 4-case dispatch
      (cave→cave region-override; dungeon→dungeon edge-override; cave→dungeon
      edge-add + void-region edge-removal; dungeon→cave predicate-carrying override);
      unified door overlay (`Entrance_BuildCrossOverlay`); cave-source→dungeon exit
      flag (`g_rando_entrance_force_cached` + `Rando_EntranceForceCachedExit`).
- [x] 3.2 Generation wiring (cross retry supersedes cave/dun, full-reachability gate)
      + corpus (`c-entrance-cross-open-fast-ganon`, 62 entries) + UI (Crossed preset
      LIVE, cross-category checkbox LIVE). Runtime install branches cross vs
      cave/dun in `Entrance_RuntimeInstall`; exit coupling in `LoadOverworldFromDungeon`.
- [x] 3.3 Playtest + fresh-eyes audit. *(Owner playtest-confirmed 2026-06-04 for the built modes — Simple/Restricted/Crossed + coupled. Insanity/decoupled is carved to `add-rando-entrance-shuffle-insanity`, runtime-blocked on the cave-arrival fork.)* NOTE: cross-category correctness is NOT
      gate-protected (the gate evaluates the model; a too-permissive model ships a
      runtime softlock) — needs careful build + playtest of model↔runtime match.
      All four cross directions verified against `Dungeon_LoadEntrance`'s
      unconditional `*_exit` cache (caching is interior-driven, not door-driven, so
      a cave-door→dungeon load still caches the source overworld position).

## Stage 4 — `decoupled` / per-endpoint ("Insanity") — FULL support (user 2026-05-30)

Design resolved in `design.md §10`: the engine always uses static seed tables for
discrete arrivals (verified: kExitData, kBirdTravel, ALTTPR StartingAreaExitTable),
so a NEW per-cave-door arrival seed table (`kCaveExitData_*`) is required, populated
by a capture pass (reuse the engine — the seeds aren't ROM-stored for caves).

- [x] D.1 **Logic one-way edges** (caves) — the net hole→exit-hole map is itself a
      uniform permutation (π_out∘π_in), generated directly (`Entrance_ComputeDecoupledExit`,
      distinct salt). `Entrance_ApplyDecoupledExitEdges` adds unconditional one-way
      warps `region(hole i) → region(exit[i])` via `Rando_AddEntranceEdge`, composing
      on top of any entry/dungeon/cross edge set (Begins only if none active —
      `Rando_EntranceEdgeOverridesActive`). Entry locations still via the existing cave
      override. Wired into BOTH generation paths (main.c corpus + rando_generate.c slot
      — though the slot/runtime stays gated off until D.4). Self-check + corpus entry
      `c-entrance-decoupled-caves-open-fast-ganon` (63 entries). HEADLESS-verified.
- [x] D.2 **Generation: reject-and-retry SUFFICES for caves** — empirically 10/10 test
      seeds generate fully-reachable within the 64-attempt cap (cave doors are dead-end
      item rooms over a walkable overworld, so one-way cave warps rarely strand the
      goal; the design's "random π_out almost never works" was overcautious for caves).
      The full-reachability gate (§2.2a) backstops. Constrained-construction was
      flagged as maybe-needed for dungeon-decoupled — but it ALSO suffices there (D.7,
      7/7 seeds), because one-way dungeon exits only ADD overworld traversal. NOTE:
      decoupled normalizes off without cave OR dungeon shuffle (hash stability).
- [x] D.4 **Runtime exit redirect — BUILT** (productionizes the validated spike
      recipe). At slot-load `Entrance_RuntimeInstall` reconstructs `es.decoupled`,
      regenerates `net` (entered-interior→emerge-interior) via `Entrance_Compute-
      DecoupledExit`, applies the one-way edges (tracker), and arms the runtime
      arrival capture/replay (`g_decoupled_*` + `g_cave_arrival[]` in rando.c).
      Hooks: entry (`Rando_DecoupledSetEnteredDoor`, overworld.c) records the entered
      cave interior; capture (`Rando_DecoupledCaptureArrival`, dungeon.c after the
      *_exit cache) snapshots that door's arrival block + world flags (rejects the
      degenerate startup capture); exit (`Rando_DecoupledReplaceArrival`,
      LoadOverworldFromDungeon cached branch) replays `net[entered]`'s arrival
      (block + `is_in_dark_world`/`savegame_is_darkworld` + target room/door-settings
      for the Y-adjust). **Coupled fallback** when the target isn't captured yet, so
      it never strands. Default-off; corpus 63/63 + selftest + guards green.
- [x] D.3 **Static cave-arrival table (baked)** — captured via the walkabout tooling
      (`--dump-cave-doors` locator + per-entry persisted capture; `Rando_Capture-
      ArrivalForBake`) and committed as `src/rando/cave_arrival_baked.h`
      (`kCaveArrivalBaked[38]`, all 38 captured; interior 37 heart_piece_cave_3 turned
      out to be a normal WALK-IN cave at dark screen 0x22, not a drop cave). The runtime preloads it
      (`Rando_LoadArrivalCaptureIfNeeded`) so every door is one-way from launch with
      no on-disk capture; a live `cave_arrival_capture.bin` overlays it for dev
      re-captures. Runtime-only data → placement digests unchanged, no kGenVer bump,
      corpus stays 63/63. Capture persists across restarts (binary sidecar) after a
      data-loss bug (in-memory table + game-kill rebuilds) was fixed.
- [x] D.5 UI: **Insanity preset + Decoupled checkbox LIVE** (caves-only; gated on cave
      shuffle; clears coupled). Spoiler `decoupled_exit` section already emitted (D.1).
      Tracker one-way-door display deferred.
- [x] D.6 Playtest (caves) + fresh-eyes audit DONE. User playtest-confirmed decoupled
      caves working + fun (2026-05-30) after fixing a cross-world stuck-bunny bug (the
      cached exit path skipped the mirror-warp's form re-eval; commit 344b8ca). The
      3-reviewer audit found + fixed 1 HIGH (decoupled exit edges accumulating across
      retries) + 1 MED-HIGH (g_decoupled_entered not consumed-at-top) + MEDs.
- [x] D.7 **Dungeon decoupled — BUILT** (Insanity for dungeons; one-way dungeon EXITS).
      No arrival asset needed: the runtime reuses the static kExitData room-keyed exit
      search, retargeting `kDungeons[net'[loaded]].room` instead of the source room
      (`Rando_EntranceDungeonDecoupledExitRoom`, overworld.c entry hook overrides the
      coupled room). net' = independent permutation over the dungeon pool (`Entrance_
      ComputeDungeonDecoupledExit`, distinct salt). **Logic is coupled-equivalent — NO
      decoupled exit edges** (fresh-eyes audit HIGH: a `lobby(D)→entry_region(net'[D])`
      edge wrongly granted gated INTERIOR access because for single-region dungeons
      entry_region_name IS the lobby). One-way exits only change the emerge spot on the
      connected overworld, so coupled reachability is correct + conservative. **Cross-
      world exits** sync world flags + bunny form in the room-keyed search branch (audit
      HIGH, same class as the cave fix 344b8ca). Composes on the dungeon ENTRY shuffle;
      scoped to non-Crossed; GT participates with its opt-in. Normalization keeps
      `decoupled` with cave OR dungeon shuffle. Self-check + JSON/text spoiler + corpus
      entry `c-entrance-dungeon-decoupled-open-fast-ganon` (64). Existing 63 digests
      byte-identical → no kGenVer bump. PLAYTEST-PENDING.
- [x] D.8 **Cross + Decoupled — BUILT** (one-way exits over the MIXED Crossed pool).
      Independent π_out over the combined cave+dungeon endpoint space (`Entrance_
      ComputeCrossDecoupledExit`, distinct salt). A given exit target may be a CAVE
      (force the cached branch + replay its baked arrival) or a DUNGEON (force the
      search branch + `exit_room` = its room) — `LoadOverworldFromDungeon` selects the
      branch by TARGET type via a consumed-at-top `cross_kind`, so a cave-loaded
      interior can emerge at a dungeon door and vice versa. `Rando_CrossDecoupledSetExit`
      is authoritative over the coupled exit at the entry hook (falls back to coupled on
      an uncaptured cave target). NO logic edges (coupled-equivalent, like the other
      decoupled modes). Gating: cave/dungeon-decoupled now require `!cross`; the
      normalizer keeps `cross` under `decoupled`. UI re-enables Cross under Decoupled.
      Self-check (11) + corpus `c-entrance-cross-decoupled-open-fast-ganon` (66, all
      byte-identical → no kGenVer bump). Fresh-eyes audit: no HIGH; its GT-wrong-warp
      MEDs were a false premise (GT/TR are multi-inbound ⇒ excluded from the cross
      pool); hardened the uncaptured-cave-target fallback. PLAYTEST-PENDING — esp.
      cross-world exits where a cave door emerges at a dungeon (or vice versa).
- [x] D.9 UI rethink: three groups (Shuffle scope / Exits coupling radio / Cross
      toggle) replacing the redundant coupled+decoupled checkboxes; `coupled` fully
      derived; HelpTooltip wraps; tooltips trimmed. Insanity preset = full decoupled
      (caves+dungeons).

## Presets (once the relevant axes ship)

- [~] P.1 Native-window preset buttons: **None**, **Simple/Restricted** (caves +
      dungeons, coupled — both ALTTPR names converge here), and **Crossed**
      (caves + dungeons + cross_category, coupled) are LIVE; **Insanity** (needs
      decoupled) shown DISABLED so no button lies. Individual axis checkboxes
      (incl. live cross-category) serve as Custom. (UI sugar over the axes — no
      stored enum, per design §5a.)
- [x] P.2 Playtest each preset resolves to the right axis combination. *(Owner playtest-confirmed 2026-06-04 — Simple/Restricted/Crossed + Custom; Insanity shown disabled, carved to `add-rando-entrance-shuffle-insanity`.)*

## Cross-cutting (per `openspec/changes/README.md` conventions)
- [x] X.1 Backward-load: `Rando_ActivateSidecarSlot`→`Entrance_RuntimeInstall` now
      surfaces a version-drift warning for entrance-shuffle slots (via
      `Rando_DetectVersionDrift`). Entrance seeds are version-locked because π is
      regenerated from the build's pool; the placement still loads (only the door
      layout is at risk) → warn + recommend regenerate. (A future full fix would
      store π in the slot for version-independence — noted; deferred.)
- [x] X.2 Registry integrity is guarded at build/selftest time by
      `Entrance_SelfCheck` (unique entrance-ids, region resolution, registry↔logic
      region cross-validation, ≥1 inbound edge, cave room-class). Entrance shuffle
      added NO new location ids (reuses existing), so the append-only-id check is
      N/A. (YAML→C-table is a one-shot `gen_entrance_table.py`; the C table is the
      authority + self-checked.)
- [x] X.3 Updated `docs/randomizer.md` + the change README status checklist.
