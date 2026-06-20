# Pot Sanity — tasks

Ordered so each phase ends at a buildable, verifiable checkpoint. The off-path MUST
stay byte-identical until the feature is wired; the load-bearing net is the owner
playtest in §6 (the runtime grant hook + gated-room logic are invisible to the
corpus and `--rando-selftest`). Decision labels (D1…) reference `design.md`.

> **As-built reconciliation (2026-06-19):** phases 1–5 are built + verified (kGen 82,
> corpus 127/127); checked boxes mark goals MET. Some task TEXT describes the original
> plan the implementation refined — notably **3.1** (`pot_shuffle` packs canonical
> `[26]` 6-7 + `[27]` 7, NOT `[27]` 2-3 which `trap_categories` took), **2.2**
> (committed `pots.gen.yaml`, not a gitignored `pot_table.gen.bin`), **3.5/3.2 door×pot**
> (pots normalize fully Off under door shuffle — the "pin key-pots + reduce pool"
> design was dropped; full integration deferred), and **4.2** (no "granted" flag —
> suppression is `dung_secrets_unk1==0` early-return + an `is_pot` arg). The reconciled
> `specs/` deltas and the design "As-built" notes are authoritative. Still open:
> **4.5** (offline render — playtest substitutes) and **6.3** (owner playtest).

## 1. Capacity foundation — typed audit (feature OFF, byte-identical) — D5

- [x] 1.1 **Typed audit of ALL location-id-keyed arrays/constants/guards** (NOT a
  `512` grep — `1163` exceeds BOTH 512 and 1024 caps). Introduce one
  `kRandoLocationCapacity` (2048) and route every site to it:
  - 512 sites: `kRandoCheckedBitmapBytes`; placer arrays
    (`pool`/`open_loc_idx`/`placement_at`/`eligible`/`junk`/`candidates`/`junk_consumed`/
    `junk_filled`/`best_entries`/`trap_entries`) + `BuildItemPool(..., 512)`;
    `kDigestLocalCap` + `buf[512*4]`; `kReachabilityMaxLocations` + `location_bitset`
    + fail-closed `id >= …` guard; `RandoSpheres.sphere_index_by_placement`;
    `kRando_SessionPlacementCapacity` + `g_session_placements`; in-memory
    `RandoSidecarSlot.checked_bitmap`/`placements`.
  - 1024 sites: `auto_tracker.c s_loc_type[1024]`; `customizer.c kCustomizerLocIdProbeMax`;
    `tracker_windows.cpp` + `rando_reach_panel.cpp` `s_loc_region`/`s_loc_type[1024]`;
    `rando_window.cpp kSpoilerMaxRows`; `rando_snapshot_tail.c raw[1024]` + the
    `> 1024` reject.
  - Re-grep at implementation; treat the lists as non-exhaustive.
- [x] 1.2 Add a `_Static_assert` per raised site tying it to `LOC__COUNT` (≥), so a
  future registry past capacity is a build break, not a silent overflow/drop/truncate.
- [x] 1.3 `make clean` + WSL `make -j zelda3` (`-Werror`) + `--rando-selftest` green;
  **corpus byte-identical** (no registry change, no version bump yet). Checkpoint.

## 2. Pot enumeration generator — D2, D8, D11

- [x] 2.1 Write `assets/scripts/gen_pot_tables.py`: parse `zelda3_assets.dat` (asset 3
  room objects + asset 50 secrets). **Expand the fake-pot run opcodes (`0x95`/`0xBC`)
  into final liftable positions**; compute `tile_position` exactly as
  `RoomDraw_SinglePot` (full 16-bit `pos4` incl. the `0x2000` BG-half bit); **verify
  each emitted pot maps to a `0x1010`-class liftable replacement-tile entry**.
  Cross-reference secret records ↔ pot positions to COMPUTE per-tier counts. Exclude
  Fairy Pots, structural-secret pots, and the excluded-room allowlist (Chris-Houlihan,
  boss arenas, pinned-boss env rooms, cutscene/triggered rooms). Assert invariants
  (unique `(room,pos)` across ALL pots; nested tiers; exhaustive classification; no
  excluded room emits a LOC; every pot has a region). **Per-content-byte policy:** map
  item drops → registry id; EXCLUDE creature-spawn pots (Cucco / RockCrab / Bee /
  soldiers) and structural / Random; ASSERT any content byte not classifiable as
  loot/empty never appears in an in-scope pot (else fail the build).
- [x] 2.2 Emit append-only registry rows + `pot_table.gen.bin` → `src/rando/pot_lookup.h`
  (**sorted** `(room,pos)→LOC` for binary search). Wire into the build like
  `chest_table.gen.bin`.
- [x] 2.3 **Pot location type migration (D11)** — update ALL: schema enum
  (`logic.schema.yaml`), codegen type map (`rando_logic_gen.py` — currently maps
  unknown→0, a silent trap), shared C enum (`rando_logic.h`), placement type checks,
  tier filter, auto-tracker/native-tracker/reach-panel type tables, spoiler grouping,
  customizer non-customizable rejection, hint eligibility. Add a `--rando-selftest`
  assert that `Pot` round-trips codegen (not silently 0).
- [x] 2.4 Logic entries: bind each pot to its room's region, `can_reach: TRUE()`. For
  **uniform** rooms inherit the room's predicate; for **non-uniform** rooms REQUIRE a
  reviewed gate in `pot_logic_overrides.yaml` (generator fails the build until
  supplied). Hard-error on any pot missing a `region:`.

## 3. Settings axis + placement/logic integration — D1, D6, D7, D9, D10

- [x] 3.1 Add `pot_shuffle` enum to `RandoSettings`; pack canonical byte `[27]` bits
  2-3 (reconcile free bits + add `(in[27]>>2)&3` unpack); `kSettingsCanonicalLen`
  stays 28; update serialize/deserialize/validate + `kExpectedCanonical`/`kExpectedHash`.
- [x] 3.2 **Active filter (D1/D9):** skip out-of-scope pot locations in the
  open-location collection loop (`rando_placement.c:1325`), the junk-pad target loop,
  and the reachability pass — mirroring the inactive-Take-Any `continue`. Accept that
  `kRandoLocationsCount` grows to ~1163; every consumer filters or is sized (§1).
- [x] 3.3 **`ITEM_Nothing` (D6):** add to `item_registry.yaml`; fill empty-pot LOCs in
  a **dedicated pre-pass** (remove from the open set before assumed-fill + junk pad —
  NOT a junk-rotation entry). Wire its behavior: logic no-op; excluded from `items`
  accessibility but counted in the tracker denominator; sphere-0 placement; trap
  shuffle never replaces it / never targets empty pots; customizer can't pin it or pin
  onto empty pots; never a hint source. Add an explicit `if (placed == ITEM_Nothing)
  return kRandoLttpSkip;` branch in `Rando_DispatchVanillaGrant` — else it falls back
  to the pot's vanilla LttP code (`rando.c:622`) and grants real content.
- [x] 3.4 **`Placement_Lookup` binary search (D10)** + sortedness invariant at EVERY
  install boundary (assumed-fill, sidecar deser, snapshot-tail, customizer, reveal,
  tests); `--rando-selftest` sortedness check + sort-on-install fallback.
- [x] 3.5 **Key economy (D7):** pot-key locations follow `dungeon_small_keys_mode` —
  extend the vanilla key pre-seed/pin path (`rando_placement.c:228/280/501`, today
  chest-only) to pin pot-keys in vanilla mode; pool them in shuffled modes (count
  preserved, never an extra key). Under `door_shuffle != vanilla`: in
  `apply_derived_rules`, pin key-pots as fixed vanilla keys AND **reduce the shuffled
  key-pool count per dungeon by the pinned count** (no duplication); non-key pots
  unaffected. Surface in UI.
- [x] 3.6 Bump `kGeneratorVersion`. `make clean` + build + `--rando-selftest`.
  **Corpus regen + 3-way diff vs unmodified `main`** (`rm src/rando/logic_data.c` to
  force codegen): every existing seed byte-identical with `pot_shuffle = Off`. Add
  corpus entries for `Keys`/`Contents`/`All`. Checkpoint.

## 4. Runtime grant hook + recolor — D3, D4

- [x] 4.1 `Dungeon_GetPotLocation(room, pos4) → LOC` (binary search over `pot_lookup.h`).
- [x] 4.2 Hook at the **TOP of `RevealPotItem`** (before the secret scan; covers all
  THREE callers — lift `:5791`, ThievesAttic `:5805` is inert via lookup-gating,
  sword-break `:5832`). Three explicit branches: (a) no-LOC / rando-off / out-of-scope
  → **pure vanilla, NO suppression**; (b) checked → §4.3; (c) in-scope & unchecked →
  `lttp = Rando_DispatchVanillaGrant(loc, pot_vanilla_registry_id, pot_vanilla_lttp)`
  (marks checked internally, before lookup; incl. the `ITEM_Nothing → kRandoLttpSkip`
  branch) → `Rando_ReceiveOrConfirm(lttp, item)` (or "nothing" cue for `ITEM_Nothing`)
  → suppress vanilla secret → RETURN. **Reset the one-lift "granted" flag at the top of
  every call; consume it only in the sword-break `0x80` block (`:5836-5838`).**
- [x] 4.3 **Checked-pot behavior:** item-pots re-drop vanilla content; **key-pots (and
  any one-shot content) are EXPLICITLY suppressed to empty** (vanilla has no per-pot
  key-taken flag → dup risk). Verify the vanilla-content branch from the pot table.
- [x] 4.4 Recolor in `RoomDraw_SinglePot`: **mask out palette bits 10-12 of the four
  tilemap words and set** the alt sub-palette row (clear-then-set, NOT OR) for in-scope
  un-checked pots; vanilla otherwise; gated rando + tier + `!checked`; non-rando
  byte-identical.
- [ ] 4.5 Offline-render verify the alt sub-palette across dungeon themes (re-render a
  known room from `zelda3_assets.dat`; cross-check vs a known-good screen).

## 5. UI / trackers — D11, D12, D13

- [x] 5.1 `pot_shuffle` four-value selector in the native settings window + 1-2 line
  tooltip; wire through the slot generator. Surface the door-shuffle key-pot
  restriction (D7).
- [x] 5.2 **SNES HUD location tracker (`hud.c:1863`)**: hide/page/summarize pots (it
  loops over locations and breaks with 800+ rows) — NOT just the native window.
- [x] 5.3 Native/auto tracker + reach panel: group pots by room / "show pots" toggle;
  completion denominator counts `ITEM_Nothing`; spoiler groups pots + omits
  `ITEM_Nothing`; define auto-tracker export pot metadata. Recolor cosmetic (2-state
  tint / non-empty-only sub-toggle), placement-neutral.

## 6. Verification, audit, hand-off

- [x] 6.1 `--rando-selftest`: `(room,pos)→LOC` round-trip, tier nesting, off-graph
  byte-identical, `ITEM_Nothing` logic no-op, `Pot`-type codegen round-trip,
  placement-table sortedness.
- [x] 6.2 Fresh-eyes audit (independent reviewer) over the committed diff, briefed on
  the dominant bug class + D3 (key-pot dup) + D9 determinism + D5 capacity + D8
  falsely-in-logic; ask for NEW findings. Do not self-review as the final pass.
- [ ] 6.3 **Owner end-to-end playtest (load-bearing):** each tier — key-pot (key
  shuffles; **re-enter room + re-break → NO duplicate key** regression); item-pot
  re-break (vanilla drop, no re-grant); empty-pot (`All`, Literally Nothing);
  recolor + revert-on-check; a non-direct-grant pot item (bottle) delivered not
  dropped; **door-shuffle + pot-shuffle** seed (key-pot restriction holds); Retro +
  `All` (capacity); a gated-room pot (Swamp flood / dark room — beatable, not
  falsely-in-logic). Confirm the loaded slot matches the share string before
  debugging any anomaly.
- [x] 6.4 Reconcile spec deltas + design vs as-built; update `docs/randomizer.md` and a
  `pot-sanity-asbuilt` memory; sync corpus manifest (restore CRLF); run version-sync /
  placer-determinism / embedded-data guards.
