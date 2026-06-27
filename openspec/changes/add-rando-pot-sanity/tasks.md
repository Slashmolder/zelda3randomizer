# Pot Sanity — tasks

Ordered so each phase ends at a buildable, verifiable checkpoint. The off-path MUST
stay byte-identical until the feature is wired; the load-bearing net is the owner
playtest in §6 (the runtime grant hook + gated-room logic are invisible to the
corpus and `--rando-selftest`). Decision labels (D1…) reference `design.md`.

> **As-built reconciliation (2026-06-20):** phases 1–5 are built + verified (kGen 84 —
> bumped 82→83 for the DW-pot Moon Pearl gate, then 83→84 to UN-exclude room 0x104 /
> Link's House per owner decision; corpus 127/127). Checked boxes mark goals MET. Some
> task TEXT describes the original plan the implementation refined — notably **2.1**
> (room 0x104, sharing the glitch-only Chris-Houlihan room, is now INCLUDED — its 3
> heart pots are Contents-tier checks, sphere-0 reachable from the start), **3.1**
> (`pot_shuffle` packs canonical
> `[26]` 6-7 + `[27]` 7, NOT `[27]` 2-3 which `trap_categories` took), **2.2**
> (`pots.gen.yaml`, not `pot_table.gen.bin`; now gitignored local ROM-derived data),
> **3.5/3.2 door×pot**
> (pots normalize fully Off under door shuffle — the "pin key-pots + reduce pool"
> design was dropped; full integration deferred), and **4.2** (no "granted" flag —
> suppression is `dung_secrets_unk1==0` early-return + an `is_pot` arg). The reconciled
> `specs/` deltas and the design "As-built" notes are authoritative. Owner playtest
> has now covered an Open full-clear with all shuffles wild and confirmed the gold
> glint looks good; Retro/Inverted and a dungeon-keys-specific pot seed remain tabled.

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
- [x] 2.2 Emit local gitignored registry rows in `pots.gen.yaml` →
  `src/rando/pot_lookup.h` (**sorted** `(room,pos)→LOC` for binary search).
  Wire the generated lookup into the build like `chest_lookup.h`.
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
  pin pot-keys in vanilla mode; pool them in shuffled modes. Under
  `door_shuffle != vanilla`, normalize `pot_shuffle → Off` (door×pot disabled in v1).
  Surface in UI.
  - **AS-BUILT NOTE (superseded by §7, task #25):** the "count preserved, never an
    extra key" framing here was WRONG — `kVanillaSmallKeyCounts` is chest-only, so a
    pot key is an ADDITIONAL pooled item; under shuffled keys it vanished (a strand)
    until §7 made it first-class. The door×pot "reduce the shuffled key-pool count"
    half-measure was REJECTED (the prover doesn't model pots) in favour of normalizing
    to Off. See §7 for the as-built wild + dungeon economy + logic gating.
- [x] 3.6 Bump `kGeneratorVersion`. `make clean` + build + `--rando-selftest`.
  **Corpus regen + 3-way diff vs unmodified `main`** (`rm src/rando/logic_data.c` to
  force codegen): every existing seed byte-identical with `pot_shuffle = Off`. Add
  corpus entries for `Keys`/`Contents`/`All`. Checkpoint.

## 4. Runtime grant hook + check glint — D3, D4

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
- [x] 4.4 Check glint (animated gold sprite overlay; supersedes the interim BG
  palette swap, now removed): `RoomDraw_SinglePot` registers in-scope un-checked pots
  into a per-room list (reset at room load in `Dungeon_LoadRoom`); `Module07_Dungeon`
  draws an animated glint after `Sprite_Main` (`RandoPot_DrawGoldOverlay`, world→screen
  via the `ManipBlock_Something` idiom, `Garnish_SparkleCommon` glyphs); `nmi.c` injects
  the gold ramp into a sprite sub-palette of the PPU CGRAM copy. Gated rando + tier +
  `!checked`; OAM + PPU-CGRAM only (no `g_ram`) → non-rando byte-identical.
- [x] 4.5 Playtest the glint across dungeons (sprite-palette-row collision + on-screen
  feel — `kRandoPotOverlayPalette` / `RandoPot_DrawGoldOverlay` tunables). The interim
  approach's cross-theme BG-palette offline render is obsolete — the glint's gold is
  injected and theme-independent. Owner playtest says the glint looks good.

## 5. UI / trackers — D11, D12, D13

- [x] 5.1 `pot_shuffle` four-value selector in the native settings window + 1-2 line
  tooltip; wire through the slot generator. Surface the door-shuffle key-pot
  restriction (D7).
- [x] 5.2 **SNES HUD location tracker (`hud.c:1863`)**: hide/page/summarize pots (it
  loops over locations and breaks with 800+ rows) — NOT just the native window.
- [x] 5.3 Native/auto tracker + reach panel: group pots by room / "show pots" toggle;
  completion denominator counts `ITEM_Nothing`; spoiler groups pots + omits
  `ITEM_Nothing`; define auto-tracker export pot metadata. Check glint cosmetic
  (optional 2-state variant / non-empty-only sub-toggle), placement-neutral.

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
  gold glint + clears-on-check (and the sprite-palette-row collision check); a
  non-direct-grant pot item (bottle) delivered not
  dropped; **door-shuffle + pot-shuffle** seed (key-pot restriction holds); Retro +
  `All` (capacity); a gated-room pot (Swamp flood / dark room — beatable, not
  falsely-in-logic). Confirm the loaded slot matches the share string before
  debugging any anomaly.
  - **Partial owner coverage recorded 2026-06-27:** Open seed cleared 100% with all
    shuffles wild; no key-pot duplicate was observed, and the gold glint looked good.
    Retro/Inverted pot seeds and a dungeon-keys-specific pot seed are intentionally
    tabled for later playtest.
  - [ ] **task #25 (§7) addendum:** a **dungeon-keys + `pot_shuffle = all`** seed — beat
    a couple of dungeons; key pots grant SHUFFLED items, the dungeon's keys can sit in
    other in-dungeon locations, and nothing strands (the dungeon under-gate is invisible
    to `--generate-seed`). Plus a **wild-keys + pot** seed.
- [x] 6.4 Reconcile spec deltas + design vs as-built (Phases 1-5); update
  `docs/randomizer.md` and a `pot-sanity-asbuilt` memory; sync corpus manifest (restore
  CRLF); run version-sync / placer-determinism / embedded-data guards.

## 7. Pot-key shuffle — wild + dungeon + binding fix (task #25)

Make a dungeon's POT keys first-class shuffled checks under shuffled key modes
(superseding the §3.5 "count-preserving / pinned" model). Gated by the prover key-door
depth; pots-off / vanilla / door byte-identical.

- [x] 7.1 **`--dump-key-depth` prover dump** (`door_keylogic.c`): per region/location/
  room/key-drop, emit the WORST-CASE `depth=` AND the SHORTEST-PATH `mindepth=` over
  the vanilla door graph (DoorExplore_Core; frontier-pruned min-popcount).
- [x] 7.2 **`gen_pot_key_depth.py` → `pot_key_depth.gen.yaml` (format_version 2):**
  per-location `full`(wild, capped chest+pot_keys) + `dungeon`(min); per-room `pot_rooms`
  full + room-MAX dungeon; per-key-pot `pot_keys` EXACT min-depth (door-rando
  `key_drop_data` DROP-region join, floor-bit reconciled) + the orphan Waterway `full`.
  Cross-check vs a reviewed 17-entry table (fails build on join drift).
- [x] 7.3 **Logic ops + wrap:** `OP_POT_KEYS_ON/WILD/DUNGEON` (op_registry.yaml id 23/24/
  25 + rando_logic.h/.c eval+dispatch + DSL parser); `rando_logic_gen.py` `_pot_key_terms`
  / `_apply_pot_key_terms` two-term wrap (key pot EXACT, loot/empty room-MAX, location
  LOC depth).
- [x] 7.4 **Economy** (`rando_placement.c`): pool pot keys under `!= Vanilla`;
  `seed_pot_nonpot_drops` free-grants the non-pot drops (EP+1/IP+3/MM+1/GT+1) into the
  assumed inventory under dungeon+pots (both seed paths; runtime SRAM overwrites — no
  double count); revert the dungeon-pin; `Placement_SelfCheck` drift guard.
- [x] 7.5 **Binding fix:** rebind the 12 mislabeled Desert Palace pots (rooms 0x53/0x43)
  through `pot_logic_overrides.yaml` so regenerated `pots.gen.yaml` carries the
  correct region + key `vanilla_item` + `can_reach`.
- [x] 7.6 `kGeneratorVersion` → 91; `make clean` + build (`-Werror`) + selftests; corpus
  regen (130/130, only the 8 pots-on non-door seeds move; door+pots and non-pot
  byte-identical); 0-refuse matrix at `accessibility=items` across keys/all × goals ×
  worlds; all CI guards green.
- [x] 7.7 Fresh-eyes independent review (0 findings, end-to-end re-validated).
- [ ] 7.8 **Owner playtest** a dungeon+pot seed — see 6.3 addendum (the only correctness
  gate the generator cannot cover).

## 8. Post-ship audit fixes (owner playtest + sibling-class sweeps; kGen 92→95)

Surfaced after Phases 1-7 shipped, by owner playtest + self-spawned audit workflows. All
corpus-validated; pots-off / non-pot seeds byte-identical, only pot-ACTIVE seeds move.

- [x] 8.1 **Cave-entrance × pot forced-off** (kGen 92): cave/house pot loc-ids sit above
  the entrance region-override range, so cave+pot certified progression against the
  vanilla overworld region. `Settings_PotShuffleForcedOff` = door OR cave-entrance shuffle
  (used by apply_derived_rules + pot_active + spoiler). Guard seed added.
- [x] 8.2 **Raw-vs-normalized settings → Effective accessors** (kGen 93-94): the placer
  consumes RAW settings (canonical-serialize normalizes only a private copy for the hash).
  Routed the pot-key gates (`Settings_PotKeysActive`), the accessibility acceptance gate +
  spoiler (`Settings_EffectiveAccessibility`, Completionist→Locations — fixed an
  accept-bad-seed via `goal=completionist,accessibility=none`), and the cave-forced-off
  check (`Settings_EffectiveShuffleCaveEntrances`, inert under Inverted/Retro) through
  derived accessors. `Placement_SelfCheck` Case 4 guards the completionist case.
- [x] 8.3 **Runtime pot-grant completeness**: the quiet-grant path (`Rando_PotQuietReceive`
  kills the receipt ancilla to skip the animation) lost the DEFERRED grants — heart
  container +8 capacity, rupees, PoH rollover, heart/magic fillers — now replicated
  rando-gated; the progressive-item icon re-pops from the PRE-grant code (was showing the
  next tier).
- [x] 8.4 **Snapshot pot-checked TLV** (`randomizer-save`): the F-key snapshot persists
  `g_rando_checked_bitmap` (outside `g_ram`) via a type-3 CheckedBitmap tail TLV.
- [x] 8.5 **Tracker toggle persistence** (`randomizer-ui`): Show pots / Show items persist
  via `rando_window.ini` (Show items still race-force-off on load).
- [x] 8.6 **Cave/house pot-room region MISLABELS** (kGen 95): the grid-adjacency region
  flood mis-bound standalone cave/house interiors (>= 0x100) to a neighbor's region,
  making them falsely sphere-0. Owner-F12 + fork-location-grounded overrides — 0x114 pond→
  DarkWorld_Mire, 0x11a storyteller→DarkWorld_NorthEast, 0x119 Blind's Hideout + 0x11f
  Lumberjack's House→LightWorld_NorthWest, 0x10c Mimic Cave + mirror-from-TR gate, 0x11b
  refill cave SPLIT per entrance (new `pot_room_split`). ALSO made the task-#25 Desert
  Palace fix (0x43/0x53) generator-reproducible (it had been a hand-edit a regen reverted).
- [x] 8.7 **Dungeon-room region audit** (kGen 96): an audit workflow found a residual
  in-dungeon region-mislabel class (same root as the 0x53 bug — the grid-adjacency flood
  leaking across a dungeon boundary, or following a supertile drop that is NOT a real door).
  A floor-bit-aware re-verification (door_tables is DOOR-RANO-indexed; engine room ±0x40 for
  multi-floor rooms — naive `door_tables[engine_index]` confounds it, and key-pot data is
  authoritative for dungeon membership) confirmed exactly **2** real mislabels and cleared
  **5** false positives. Applied via `pot_logic_overrides.yaml`: **0x04b** (2 PoD Jelly-Hall/
  Mimics LOOT pots) GanonsTower→**PalaceOfDarkness**, gated `(CanBombThings() OR HAS_ITEM(Boots))`
  (key-free warp route, door_tables dungeon=5); **0x096** (8 GT Staredown/Torch-Cross LOOT/EMPTY
  pots) TowerOfHera→**GanonsTower_Lobby**, gate `BigKey_TowerOfHera`→`BigKey_GanonsTower` (behind
  the GT big-key door; small-key economy stays on the separate POT_KEYS wrap). Both rooms hold no
  key pots, so the key economy + Keys-tier/Off/non-pot seeds are byte-identical; only Contents/All
  pot seeds move. The 5 false positives (0x031/0x054/0x056/0x05b/0x064) were re-verified CORRECT
  and left as-is (e.g. engine 0x031 = door-rano 0x71 = HC sewers = genuinely HCE, not ToH — this
  also corrected an earlier static trace that had wrongly "corroborated" 0x031→ToH).
- [x] 8.8 **Snapshot bitmap clean-restore on older snapshots** (`randomizer-save`, no kGen — save/load
  correctness): the type-1 `RandoState` branch of `RandoSnapshotTail_Load` installed placement but
  never cleared `g_rando_checked_bitmap` (a C global OUTSIDE the `g_ram` dump); only the type-3
  `CheckedBitmap` branch cleared it. So an older snapshot (type-1 [+type-2], no type-3 — written
  before pot-sanity added type-3) restored placement and KEPT whatever checked bits were live from
  the current slot/session — contradicting the spec scenario "Older snapshot without the TLV restores
  cleanly" (all-clear, no stale state) and suppressing/re-granting unrelated pots/checks. FIX: clear
  the bitmap when a valid type-1 is accepted (after `Placement_Install`, before context reinstall);
  a current-binary snapshot's type-3 TLV (emitted right after type-1) re-memsets+restores the real
  bitmap, so the clear is load-bearing only for the type-3-absent case. Added an `absent-type-3`
  selfcheck (hand-writes a lone non-empty type-1 TLV, pre-seeds the bitmap to 0xFF, asserts all-clear
  + the placement still installs). 3-skeptic adversarial verify (one compiled a standalone repro
  proving the selfcheck fails pre-fix / passes post-fix): 0 issues. The spec already described the
  correct behavior — code-to-spec fix, no delta change.
- [x] 8.9 **Debug warp-to-room picks the pot side of shared interiors** (debug-UI only, no kGen):
  `dbg_warp.cpp`'s `EntranceForRoom` returned the FIRST entrance whose dest room matched, but shared
  interiors have multiple entrances and only one side holds the pots — Pond of Wishing room 0x114 is
  reached by both 0x5C (LW fairy side, pot-LESS) and 0x62 (DW-mire pot side), and refill cave 0x11b
  is split (entrances 0x51/0x52). Warping to "the first match" could land on the pot-less/wrong half
  → false playtest evidence for exactly the pot rooms this branch validates. FIX: `EntrancesForRoom`
  collects ALL matches (asset 11 decoded: max 3 per room); the UI renders one "via 0xNN" button per
  entrance when >1, a single "Warp to room" when ==1, disabled when 0. Adversarial verify decoded
  asset 11 directly and confirmed 0x114→{0x5C,0x62} and 0x11b→{0x51,0x52}: 0 issues.
