# Tasks — door shuffle

Restructured to the as-built stages (see `design.md §13` for the deviation
changelog). Stages 0/1/3/4 are in the tree; Stage 2 (stitcher/prover) and
Stage 1b (door-KIND overlay) are the in-flight remainder, followed by the
version/corpus close-out and the verification gates.

## Stage 0 — Static topology codegen ✓ DONE

- [x] 0.1 `assets/scripts/gen_door_tables.py` — builds the reference's vanilla world
  IN-PROCESS (imports ALttPDoorRandomizer's own modules, monkeypatches the rule
  primitives to capture lambda sources) and emits the door catalog, regions, edges,
  rule blobs, locations, events, drop keys, dungeons, paths, portals, room-door
  lists (`src/rando/door_tables.gen.{c,h}`, `assets/rando/door_predicates.gen.json`).
- [x] 0.2 Artifact lifecycle DECIDED (design §9.7): tables are **COMMITTED**
  reference-derived artifacts (CI lacks the reference checkout);
  `assets/scripts/check_door_tables.py` guards consistency (missing/empty, header
  counts vs data vs registry, predicate-manifest count) in `rando_ci.yaml`.
- [x] 0.3 `assets/rando/door_registry.yaml` — frozen append-only door-stub ids
  (the layout digest hashes ids; re-numbering invalidates saves).
- [x] 0.4 Vanilla-connectivity cross-check (Normal == ±1/±0x10; spiral == header) —
  surfaced the HC vanilla-teleport pairs (`kDoorTblFlag_VanillaTeleport`); palace-
  boundary and room `< 0x100` asserts; rule translator at 0 errors.
- [x] 0.5 `assets/rando/door_portals.yaml` — hand-curated per-portal gate rows
  (fork region + optional predicate), compiled to `kDoorPortalGates`.

## Stage 1 — Runtime redirect layer ✓ DONE (identity RAM-compare run still open, see 6.2)

- [x] 1.1 `kFeatures1_DoorShuffleActive` (`src/features.h`).
- [x] 1.2 `src/rando/door_runtime.{c,h}` — `g_door_link[]` sparse override +
  `kDoorRt_NoOverride`; install/teardown; exit-door resolution (room, dir, layer,
  perpendicular-coordinate slot match); `DoorRt_Arrive` (both axes + slot correction
  + quadrant + layer from dest record + virtual-neighbor `index2`/`_prev` +
  tagalong resync); vcxproj registration.
- [x] 1.3 `src/dungeon.c` hooks — `Rando_DoorTransOverride(dir)` guarding the
  positional line inside all four supertile-boundary branches (after the early
  returns); `Rando_DoorTransConsumedToggles()` skipping the layer/palace toggles on
  the override arm (dest record = layer authority; palace index untouched);
  `Rando_DoorStaircaseContext` bracketing in `Dungeon_DetectStaircase`;
  `Rando_DoorSpiralDest(room, slot, attr, vanilla)` at the header-dest read;
  `Rando_DoorSpiralFixup()` from `Dungeon_InitializeRoomFromSpecial` (intra-room
  delta + dest-layer). Hole/teleport/straight-stair sites untouched (follow-on).

## Stage 1b — Door-KIND overlay (committed scope, IN FLIGHT)

- [ ] 1b.1 Per-(room, doorListPos) kind override consulted at the THREE raw
  door-list reader seams: the door-object draw path (`RoomData_DrawObject_Door`),
  `Dungeon_LoadHeader`'s raw door-word copy + current-room scan, and
  `Dungeon_LoadAdjacentRoomDoors` (keyed by its room argument); everything
  downstream of the parsed door type inherits.
- [ ] 1b.2 Stateful-position constraint: port the reference's door-list
  position-swap so every chosen key door (BOTH halves, per-half feasibility)
  occupies a pos<4 entry; the constraint lives in the key-door candidate search
  (it shapes the digest).
- [ ] 1b.3 Partner open-bit mirror via the door pairing + suppression of
  physical-neighbor open-bit propagation for shuffled edge slots.
- [ ] 1b.4 Skull Pinball WS trap→Normal mutation + runtime consequence; vanilla key
  doors whose kind moved away render/behave as Normal.
- [ ] 1b.5 Selftest asserts: overlay entries == prover's key-door set; every overlay
  key door at pos<4 post-swap; paired-open invariant.

## Stage 2 — Stitcher + key prover (IN FLIGHT, against the `shuffle_doors.h` contract)

- [ ] 2.1 Replace the `shuffle_doors.c` stub: explorer `DoorExplore_Run` (dual
  blue/orange crystal states, events, drop-key economy, rule-blob eval) — the ONE
  reachability model shared by stitcher, prover, and the Stage-3 oracle.
- [ ] 2.2 Live stitcher port (`source/dungeon/DungeonStitcher.py generate_dungeon`,
  NOT the deprecated `DungeonGenerator` sibling): `create_random_proposal` →
  `explore_proposal` → `check_valid` (+ required paths) → `modify_proposal` (≤10
  local repairs then reshuffle), iteration-capped ⇒ bump `door_attempt`; portal
  analysis from the shared table (incl. the Desert Back intensity-1 waiver),
  origins, split-dungeon handling.
- [ ] 2.3 Key prover (`door_keylogic`): candidates (incl. the 1b.2 pos<4
  constraint), `find_valid_combination` (deterministic integer-only `ncr`
  sampling, no doubles, id-sorted draws), `validate_key_layout` (worst-case
  memoized search, drop keys, big-chest exclusion, self-locking rejection) ⇒
  per-door worst-case thresholds + `bk_restricted`. No std-HC code (HC pinned).
- [ ] 2.4 `DoorShuffle_LayoutDigest` (real implementation) + determinism: identical
  layout/digest for `(seed, settings, door_attempt)` cross-platform (WSL vs MSVC).
- [ ] 2.5 `--door-selftest` (CLI already wired in `src/main.c`): per dungeon × N
  seeds — connectivity, prover acceptance, determinism, full-inventory
  oracle≡stitcher agreement, prover-key-math completability.

## Stage 3 — Logic + placement wiring ✓ DONE

- [x] 3.1 Static ops `OP_DOORS_ACTIVE`/`OP_DOORS_LOC_REACHABLE` (op_registry ids
  20/21; `rando_logic.{c,h}`); per-dungeon active via `shuffled_mask`.
- [x] 3.2 Codegen wrap of every door-controlled location's `can_reach`
  (`rando_logic_gen.py`, fed by `door_predicates.gen.json`); Vm-pred + portal-gate
  tables compiled into the static stream.
- [x] 3.3 Oracle: portal seeding from `kDoorPortalGates` under the live region
  bitset; drop-key economy; memo per (dungeon, fixed-point pass) with
  invalidation each pass; `g_in_door_oracle` no-recursion guard;
  `Rando_SetDoorLogicLayout` install/teardown.
- [x] 3.4 `bk_restricted` big-key placement ban next to `dungeon_mode_accepts_item`
  (`rando_placement.c`, `DoorShuffle_BkRestricted`).
- [x] 3.5 MVP pins in `apply_derived_rules`: Open/Standard + NoGlitches only;
  entrance-shuffle mutual exclusion (door yields); in-dungeon small+big keys
  forced; HC + Swamp pinned via `kDoorShuffle_MvpDungeonMask`.

## Stage 4 — Settings / save / generation / UI ✓ DONE

- [x] 4.1 `door_shuffle` axis in canonical `[27]` bits 0-1 (`kDoorShuffleAxis_Mask`);
  `kSettingsCanonicalLen` stays 28; CSV key `door_shuffle`;
  `Settings_EffectiveDoorShuffle`.
- [x] 4.2 Sidecar: `door_attempt` @76 + 24-bit layout digest @77-79 (claims the
  `reserved[4]` tail; `rando_save.{c,h}`).
- [x] 4.3 Generation door phase in BOTH pipelines (`Rando_PlaceWithEntrances` arm in
  `rando_generate.c` AND the hand-rolled headless path in `src/main.c`):
  `door_attempt` retry loop (16) around `DoorShuffle_Generate` + placement +
  accessibility; accepted attempt+digest persisted (`Rando_GetDoorGeneration`).
- [x] 4.4 Activation (`Rando_ActivateSidecarSlot`): regenerate from (share-string
  seed, settings, `door_attempt`) BEFORE installing slot state; digest mismatch or
  generation failure ⇒ HARD-FAIL (deactivate + diagnostic, non-destructive);
  install = logic layout + `DoorRt_*` links + feature flag; symmetric teardown.
- [x] 4.5 Spoiler `door_shuffle` section (pairings + key doors, race-gated);
  settings-window "Door shuffle" checkbox + tooltip.

## Stage 5 — Version / corpus close-out (OPEN — after Stage 2 lands)

- [ ] 5.1 `kGeneratorVersion` bump (re-grep the live value at commit — the worktree
  base is 65; concurrent branches contend) + one-line rationale.
- [ ] 5.2 Corpus: add a `door_shuffle=basic` seed; `bump_rando_corpus.py --apply`
  (absolute `--binary`, fresh build, `rm src/rando/logic_data.c` to force codegen);
  restore CRLF manifest; `check_corpus_version_sync` green.
- [ ] 5.3 **3-way corpus regen** proving `door_shuffle == vanilla` byte-identical
  (fresh `main` vs change, codegen forced) — a regen, not a code-review claim.
- [ ] 5.4 `RandoGenerate_SelfCheck`: door catalog non-empty when door shuffle active.

## Stage 6 — Verification gates (OPEN)

- [ ] 6.1 `--door-selftest` green (Stage 2 gate).
- [ ] 6.2 **Flag-ON, all-`NO_OVERRIDE` RAM-compare run** side-by-side against the
  ROM (user-assisted; the only check that executes the hooks) + flag-off corpus
  byte-identical.
- [ ] 6.3 **Playtest matrix** (user): per shuffled dungeon class — beatable, no
  softlock, arrival per edge/layer, relocated key doors + un-keyed vanilla doors,
  spirals, TT maiden escort, Skull Pinball, shutters.
- [ ] 6.4 Fresh-eyes review pass per CLAUDE.md cadence before declaring done.
- [ ] 6.5 Update `docs/randomizer.md` (door shuffle = basic/intensity-1 as built;
  note the "dungeon map shows room positions, not connections" caveat + follow-on
  scope).

## Follow-on changes (designed in `design.md §7-8`, NOT built here)

- [ ] C — intensity 2/3 (open-edges, straight-stairs, ladders, hole/teleport
  redirect sites, lobby/portal shuffle).
- [ ] D — `door_type_mode` big/all/chaos, trap-door shuffle/removal, decouple,
  self-loops, strict/dangerous key logic.
- [ ] E — crossed/partitioned (polarity/sector-distribution engine + cross-dungeon
  environment porting + wild/universal keys).
