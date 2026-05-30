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
- [ ] 1.1 Enumerate cave/single-interior entrances; group entrance-ids by interior
      (entrance-id is NOT 1:1 with room — design §3a).
- [ ] 1.2 `shuffle_entrance.{c,h}`: compute π over the cave pool from the seed RNG.
- [ ] 1.3 Coupled pairing (enter A → exit A as the baseline — design §3b).

### Logic half  (caves = REGION reassignment, NOT edges — design §2a)
- [ ] 1.4 Drive cave-location `region_override` **per-seed from π** (reuse the shipped
      `region_override` field at `rando_logic.c:477` / `rando_placement.c:530` /
      `rando_spoiler.c:647`, today keyed by world_state). A cave-location's effective
      region becomes the overworld region of the door that now leads to it. NOTE: a
      swap within one overworld region is a logic no-op — only cross-region swaps move
      reachability.
- [ ] 1.5 `none` (all axes off) ⇒ no π-driven override ⇒ byte-identical reachability
      (corpus invariant). (Dungeon edge-overlay is Stage 2, design §2b — NOT here.)
- [ ] 1.6 Goal-reachable reject-and-retry around `Place_AssumedFill` /
      `Goal_IsCompletable` (design §4).

### Runtime half
- [ ] 1.7 Door overlay: shadow copy of `kOverworld_Entrance_Id`, repoint
      `g_asset_ptrs[126]`; install/teardown on slot lifecycle (design §3a).
- [ ] 1.8 Coupling: capture source door at the entry hook (reuse the TakeAny
      `g_rando_takeany_door_id` idiom) + cache source-door exit props for
      `LoadCachedEntranceProperties` (design §3b/§3c).
- [ ] 1.9 Compose with TakeAny's redirect at `overworld.c:~3342` (design §7).

### Settings / save / spoiler / UI
- [ ] 1.10 Wire the `shuffle_cave_entrances` + `coupled` axes (added in 0.4) into the
      generator; other axes stay off until their stage.
- [ ] 1.11 `TAIL_ENTRANCE_MAP` save TLV write/read (per 0.3 decision).
- [ ] 1.12 Spoiler `entrance_mapping` section (JSON + text).
- [ ] 1.13 Native settings window: cave-shuffle + coupled toggles (PC). Switch
      in-game screen picker if applicable.

### Verify
- [ ] 1.14 `kGeneratorVersion` bump + corpus regen; confirm `none` digests
      byte-identical to baseline.
- [ ] 1.15 `--rando-selftest` + corpus green; audit-guard/determinism/codegen checks.
- [ ] 1.16 **Playtest** (the only reliable net): enter shuffled cave → correct
      interior → exit returns to the SOURCE door (coupled). Save/load round-trips π.
- [ ] 1.17 Fresh-eyes audit pass (per CLAUDE.md cadence) before declaring Stage 1 done.

## Stage 2 — `shuffle_dungeon_entrances` (room-keyed exit class)

Single-entrance dungeons FIRST (EP↔PoD — low risk), then multi-entrance.

- [ ] 2.1 Room-keyed exit remap: parallel overlay for `kExitDataRooms` / `kExitData_*`
      so a shuffled dungeon exits to the source overworld door (design §3c).
- [ ] 2.2 **Single-entrance dungeons** (EP, PoD, …): one door-edge rewrite each —
      same machinery as caves. The low-risk subset; land + playtest this first.
- [ ] 2.3 **Multi-entrance dungeon** consistency (Hyrule Castle / Skull Woods etc. —
      their doors move as a unit / stay mutually consistent).
- [ ] 2.4 Link's House (room 0x104) special-case folded into the room-keyed class.
- [ ] 2.5 Logic: per-seed edge overlay — dungeon door-edges' `to_region` rewritten
      per π (design §2b); internal dungeon edges + event gates stay fixed. Watch
      prize/medallion gates (prize tied to dungeon, not door).
- [ ] 2.6 Playtest dungeon entrance/exit round-trips + fresh-eyes audit.

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

- [ ] P.1 Bundle the axes into named modes: Simple / Restricted / Crossed / Insanity
      + Custom (design §5a). Add to the settings-preset enum + UI.
- [ ] P.2 Playtest each preset resolves to the right axis combination.

## Cross-cutting (per `openspec/changes/README.md` conventions)
- [ ] X.1 Backward-load: a slot from `generator_version = N` loads on `N+1` with a
      one-time informational warning; no regeneration required.
- [ ] X.2 Append-only registry check if location/entrance ids grew.
- [ ] X.3 Update `docs/randomizer.md` + the change README status checklist.
