# Tasks — door shuffle

Staged functional-first. **Milestones A + B are the committed scope of this change.**
Milestones C–E are enumerated for completeness but ship as separate follow-on changes
(see `design.md §7`). Each committed milestone ends at a verifiable gate.

## Stage 0 — Static topology codegen (Milestone A)

- [ ] 0.1 `assets/rando/door_registry.yaml` — enumerate every shuffleable door-stub with
  a **frozen stable id** ← `(room, edge, slot, layer)`, mirroring `entrance_registry.yaml`.
  Scope ids to Normal + Spiral for MVP but reserve the full 1201-stub space.
- [ ] 0.2 `assets/scripts/gen_door_tables.py` — parse the reference `Doors.py`,
  `RoomData.py`, `Dungeons.py`; emit gitignored C tables: door-stub catalog
  (`{id,room,edge,slot,layer,DoorType,default_partner_id,DoorKind}`), per-room env +
  `dungeon_id` + region membership (the room-granular `dungeon_regions`), vanilla
  `door_type_counts`, required-path targets, crystal-barrier annotations. (Pattern: the
  in-tree `assets/scripts/gen_entrance_table.py` / `gen_inverted_maps.py`.)
- [ ] 0.3 `assets/scripts/check_door_tables.py` — build guard: **fail the build** if the
  codegen artifact is absent/empty (the fails-open trap, `design.md §10`). Wire into
  `rando_ci.yaml`. Decide the artifact lifecycle (committed-but-gitignored vs
  regenerated-at-build, `design.md §9.7`); if committed-but-gitignored, extend
  `setup_worktree.py`'s mirror allowlist (the fresh-worktree fails-open trap).
- [ ] 0.4 Verify the default-partner table reproduces vanilla connectivity for every
  Normal+Spiral stub (cross-check positional `±1`/`±0x10` against `default_door_connections`).
- [ ] 0.5 **Grep-confirm** the research-time counts/symbols before relying on them
  (`Doors.py` stub totals, `dungeon_regions` count, `DoorKind` byte values) — the spec-rot
  discipline; design.md flags these as research-agent figures.

## Stage 1 — Runtime redirect layer + identity gate (Milestone A)

- [ ] 1.1 `src/features.h` — add `kFeatures1_DoorShuffleActive` bit; reserve runtime RAM
  in the `0x662..0x66f` free block if needed.
- [ ] 1.2 `src/rando/door_runtime.{c,h}` — the sparse-override table + `Rando_DoorRedirect`
  (`NO_OVERRIDE` sentinel) + install/teardown (mirror `Entrance_RuntimeInstall`).
- [ ] 1.3 `src/dungeon.c` — hook the 4 `Dungeon_StartInterRoomTrans_*` functions **per the
  §2b control-flow contract**: place the redirect *inside* the supertile-boundary branch
  (`if(!link_quadrant_x)` etc.), **replace the `room ± 1/± 0x10` line**, fall through to
  consume+clear `room_transitioning_flags` (+ layer/floor toggles), and set
  `dungeon_room_index2`/`_prev` so `Dungeon_AdjustAfterSpiralStairs` does not fire. Also
  hook the spiral/straight-staircase site (`Dungeon_DetectStaircase`), **both** hole sites
  in `LinkState_Pits`, and the teleport-door L/R branches. **Spiral disambiguation (§2d):**
  the `…Trans_*` hook MUST be a no-op in staircase context — the header-dest override (keyed
  on `(room, which_staircase_index & 3)`, written as a full uint16) is the SOLE spiral
  authority; zero-delta BOTH `Dungeon_AdjustAfterSpiralStairs` sites (the `…Trans` one AND
  `Dungeon_InitializeRoomFromSpecial`). Place the normal-door hook AFTER the Up/Down
  overworld-exit/credits early-returns. Vanilla path untouched when the flag is clear.
- [ ] 1.4 `Rando_DoorArrive` — generalize `Dungeon_AdjustForTeleportDoors` to land Link at
  an arbitrary `(dest_room, dest_edge, dest_slot, dest_layer)`; **take a `uint16` room**
  (do NOT route arbitrary dests through the `uint8` travel-dest bytes); set the
  perpendicular-axis coordinate from the partner slot's tilemap position + inward step;
  reuse `Dungeon_AdjustForRoomLayout`. Arrival edge is model-guaranteed opposite the exit
  edge ⇒ the destination scrolls in normally (no fade needed).
- [ ] 1.5 **Identity gate (the REAL Milestone A check):** with `kFeatures1_DoorShuffleActive`
  **ON** and the table all-`NO_OVERRIDE`, run side-by-side against the ROM and confirm
  **RAM-compare clean** (this is the only path that executes the hooks). The all-`NO_OVERRIDE`
  path MUST flow through the **rewritten branch** (same room-index/flag-consume/`index2`
  code a real redirect uses, differing only in the dest value) — NOT a preserved vanilla
  fast-path, or the gate is hollow (§2b.5). Separately confirm the off-path is inert via a
  **corpus byte-identical** run (flag off). Do not proceed until both green.
- [ ] 1.6 Optional offline "spawn at door X" harness (`design.md §9.2`) to validate
  `Rando_DoorArrive` per edge/slot/layer without playtest.

## Stage 2 — Basic stitcher + key-prover (Milestone B, generation)

- [ ] 2.1 `src/rando/shuffle_doors.{c,h}` — `convert_to_sectors` + `ExplorationState`
  (dual blue/orange, both list-backed ⇒ deterministic) reachability port.
- [ ] 2.2 The stitcher: `create_random_proposal` → `explore_proposal` → `check_valid` →
  `modify_proposal` loop (the live `generate_dungeon` in `source/dungeon/DungeonStitcher.py`,
  NOT `generate_dungeon_find_proposal_old`), single-dungeon `simple_dungeon_builder` (skips
  the cross-dungeon *distribution* engine; still port/prove-trivial its `GlobalPolarity` +
  `assign_sector`). Iteration-bounded; `budget=0`. Every RNG draw from an id-sorted list
  (no hash-ordered iteration into output — §3e).
- [ ] 2.3 `src/rando/door_keylogic.{c,h}` — port the **full** `analyze_dungeon` +
  `validate_key_layout` (KeyCounter closure + worst-case memoized DFS +
  `invalid_self_locking_key` + std-mode HC special-casing). "MVP = partial" is a
  runtime-eval choice, NOT a port-scope reducer — the same machinery is needed. Output:
  per-door worst-case key thresholds (`DoorRules`) **and** `bk_restricted` (big-key
  placement bans).
- [ ] 2.4 `door_type_mode = original` relocation of existing small/big-key doors onto the
  new connections (`shuffle_small_key_doors` original branch).
- [ ] 2.5 Determinism: door-specific RNG salt off the seed; reject-and-retry `door_attempt`
  index. Verify same `(seed, settings, attempt)` ⇒ identical layout cross-platform.

## Stage 3 — Logic + placement integration (Milestone B) — the largest net-new piece

- [ ] 3.0 **Spike first** (`design.md §9.1`): prototype one dungeon's §4c graph end-to-end
  and confirm the reachability fixed-point reproduces the prover's intended sphere, BEFORE
  building the rest of Stage 3.
- [ ] 3.1 Codegen: add the reference's **per-room dungeon regions** to the static region
  table (inert when door shuffle off) + precompile the `HAS_AMOUNT(SmallKey_<dgn>, k)`
  threshold family (and big-key predicate) into `kRandoPredicateStream`. **Bump
  `kReachabilityMaxRegions`** past 256 to cover 31 + the grep-confirmed ~579 room regions
  (uint16 ids; no static-assert couples the cap); re-prove off-path byte-identical AFTER the
  bump and re-measure the Switch reachability budget (§4c).
- [ ] 3.2 `src/rando/rando_logic.c` — add a **door-shuffle per-seed door-graph** (its own
  arrays modeled on entrance shuffle's `g_entrance_added_edges{from,to,pred_off,pred_len}` +
  a **separate** `g_door_region_override[]` to avoid the entrance-shuffle namespace
  collision; sized for the door graph; walked in `Logic_ComputeReachability`): reassign each
  dungeon location to its room region; wire room-region connectivity per layout `D`, each
  key-door edge's `pred_off` = the precompiled worst-case-N threshold. The arbitrary-predicate
  seam is **infeasible** — do NOT use it (`design.md §4b`).
- [ ] 3.3 `src/rando/rando_placement.c` — assumed-fill consumes the per-seed door-graph;
  enforce `bk_restricted` as a **big-key placement ban** (extend `dungeon_mode_accepts_item`
  / a per-location forbid); keep in-dungeon containment consistent with the layout.
- [ ] 3.4 `apply_derived_rules` — door shuffle ⇒ force `dungeon_small_keys_mode == Dungeon`
  **and** `dungeon_big_keys_mode == Dungeon` for MVP (coerce + spoiler note, or refuse);
  coerce/refuse **Retro** and **Wild/Universal** keys too (they collapse the per-door
  thresholds — `HAS_AMOUNT(SmallKey,N)` → `GenericKey ≥ 1` under Retro, §4d).
- [ ] 3.5 `doorShuffle == vanilla` ⇒ **byte-identical reachability + placement**, proven by
  a **3-way corpus regen** (fresh `main` vs change, codegen forced), not a code-review claim
  (`design.md §10`).

## Stage 4 — Settings / save / UI / version / corpus (Milestone B)

- [ ] 4.1 `rando_settings.{c,h}` — add `doorShuffle` (+ `intensity` pinned 1) packed into
  pad byte `[27]`; `kSettingsCanonicalLen` stays 28; update `kExpectedCanonical`/
  `kExpectedHash` + round-trip self-test; CSV parser KEY + `handle_kv`.
- [ ] 4.2 `rando_save.{c,h}` — `door_attempt` byte at `@76` (first byte of the `reserved[4]`
  tail); regen layout from `(seed, settings, door_attempt)` in `Rando_ActivateSidecarSlot`.
  Make version-drift **blocking** for door-shuffle slots via a **persisted layout digest**
  (primary route — recompute from the regenerated layout at activation, hard-fail on
  mismatch; cleaner than threading a refuse-return through the `void`
  `Rando_ActivateSidecarSlot`). A drifted layout can make the certified placement unbeatable
  (`design.md §6`).
- [ ] 4.3 `rando_generate.c` / `rando.c` — generation-side install + slot-load regen +
  runtime overlay install/teardown.
- [ ] 4.4 `rando_window/*` — UI: `doorShuffle` enum (vanilla/basic) on the settings panel;
  tooltip per `tooltip_brevity` (durable player-facts only).
- [ ] 4.5 `kGeneratorVersion` bump (re-grep live value at commit per the version-drift
  rule; live base is 61, but `62`/`64` are already contended by concurrent unmerged branches
  — sheet-reshuffle/customizer/major-glitch — so expect to land higher). One-line rationale.
- [ ] 4.6 Corpus: add a `basic` door-shuffle seed; `bump_rando_corpus.py --apply`
  (absolute `--binary`, fresh build, `rm src/rando/logic_data.c` to force codegen);
  restore CRLF manifest; `check_corpus_version_sync` green.
- [ ] 4.7 `RandoGenerate_SelfCheck` — assert the door catalog is non-empty when door
  shuffle is active (fails-open self-check, `design.md §10`).

## Stage 5 — Verification (Milestone B gate)

- [ ] 5.1 **Playtest** a `basic` intensity-1 seed per dungeon class: beatable, no softlock,
  correct shutters/keys, correct `Rando_DoorArrive` landing, correct palettes/effects.
- [ ] 5.2 Audit the shutter/door-open-bit sync (`design.md §2f`).
- [ ] 5.3 Fresh-eyes review pass per CLAUDE.md cadence before declaring done.
- [ ] 5.4 Update `docs/randomizer.md` (door shuffle = basic/intensity-1 as-built; note the
  follow-on scope).

## Follow-on changes (designed in `design.md §7-8`, NOT built here)

- [ ] C — intensity 2/3 (open-edges, straight-stairs, ladders, lobby/portal shuffle).
- [ ] D — `door_type_mode` big/all/chaos, trap-door shuffle/removal, decouple, self-loops,
  strict/dangerous key logic.
- [ ] E — crossed/partitioned (polarity/sector-distribution engine + cross-dungeon
  environment porting + wild/universal keys).
