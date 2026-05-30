# Tasks — entrance shuffle (Phase C)

Staged per `design.md §6`. The engine is built once in Stage 1; later stages widen
the pool + exit class. Each stage is independently playtestable. The change archives
when Stage 1 (minimum) is shippable; Stages 2–4 may complete here or split into
follow-on changes.

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
- [ ] 2.3 **Deferred**: Skull Woods (truly many separate entrances — needs a
      multi-entrance move-as-a-unit mechanism) + Ganon's Tower (crystal-tower gate
      travels with the door → circular reachability; needs the gate to travel with
      the dungeon). Documented in `shuffle_entrance.c` + `entrance_registry.yaml`.
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

## Stage 3 — `cross_category` ("Crossed" feel)

- [ ] 3.1 Allow caves↔dungeons to mix in the permutation pool.
- [ ] 3.2 Constraint wiring on the existing engine (mostly generator-side).
- [ ] 3.3 Playtest + fresh-eyes audit.

## Stage 4 — `decoupled` / per-endpoint ("Insanity") — optional / may split

- [ ] 4.1 Decoupled pairing (per-endpoint independent shuffle; implies !coupled).
- [ ] 4.2 Constrained-construction retry (random π rarely leaves goal reachable —
      design §4).
- [ ] 4.3 Spoiler/tracker support for decoupled (one-way) doors.
- [ ] 4.4 Playtest + fresh-eyes audit.

## Presets (once the relevant axes ship)

- [~] P.1 Native-window preset buttons: **None** + **Simple/Restricted** (caves +
      dungeons, coupled — both ALTTPR names converge here until cross-category
      ships) are LIVE; **Crossed** (needs cross_category) + **Insanity** (needs
      decoupled) shown DISABLED so no button lies. Individual axis checkboxes serve
      as Custom. (UI sugar over the axes — no stored enum, per design §5a.)
- [ ] P.2 Playtest each preset resolves to the right axis combination.

## Cross-cutting (per `openspec/changes/README.md` conventions)
- [ ] X.1 Backward-load: a slot from `generator_version = N` loads on `N+1` with a
      one-time informational warning; no regeneration required.
- [ ] X.2 Append-only registry check if location/entrance ids grew.
- [ ] X.3 Update `docs/randomizer.md` + the change README status checklist.
