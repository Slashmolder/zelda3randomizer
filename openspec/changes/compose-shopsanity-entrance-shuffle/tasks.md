# Tasks

Phases are ordered so each one ends at a buildable, testable state. Phase 2 is
the risky one; everything before it is mechanical.

## 1. Registry + generator (data)

- [x] 1.1a Committed `assets/scripts/gen_entrance_door_rows.py`, which derives
      every entry's door rows from the vanilla asset tables and reports the
      multi-row entries. Confirms design D1 exactly: entry 21 = rows
      0x57/0x59/0x6D, entry 23 = rows 0x56/0x5F/0x6E/0x73, entry 32 = rows
      0x45/0x75. It also caught a transcription trap — ALTTPR door 0x6E is row
      0x6D, while *entrance id* 0x6E belongs to thief_hideout (row 0x79).
      NOTE: many non-shop entries are also multi-row (pond_of_happiness has 8);
      only the shop entries need splitting, the rest stay grouped.
- [x] 1.1b Added the derived `door_rows` to all 40 entries of
      `entrance_registry.yaml` (70 rows total), inserted programmatically and
      bounded to the `interiors:` block so the `excluded:` /
      `overworld_surface_not_caves:` / `dungeons:` blocks are untouched. Inert
      until the generator reads it.
- [x] 1.1c Derived each split door's region and VALIDATED the method: all eight
      cave-resident shop doors resolve to exactly the region
      `location_registry.yaml` gives that shop. Table in design D9. The sweep
      also surfaced pre-existing region drift on other entries — spun out as its
      own audit, deliberately not touched here.
- [x] 1.2b Added `shop_loc_base` to the shop entries. DOCUMENTATION ONLY in
      phase 2a: nothing emits or reads it. It records which shop each split door
      hosts so 3.1 can derive identity from it. Cross-checked against the
      binary's own `--rando-shop-doorwalk`, which resolves each of those door
      rows to exactly the recorded base.
- [x] 1.2 Split entries 21, 23, 32 by door row per design D1, **appending** the
      new entries at indices 40-45 so 0-39 keep their positions (design D2).
      Each split entry has its own `region_name` (design D9) and `can_enter`.
      DEVIATION: the shop `location_ids` are NOT bound yet. Binding them is what
      makes anything depend on shop identity, and phase 2a deliberately leaves
      shops forced off so that an entrance-shuffle regression has an unambiguous
      cause. Task 3.1 binds them. Every split entry is location-less exactly like
      the entry it split from, so `Entrance_SelfCheck` (2) still skips them —
      which also means D9's "the split is guarded going in" only takes effect at
      3.1; until then the per-door regions rest on the D9 derivation plus the
      `gen_entrance_door_rows.py --check` world-consistency guard.
      Each `can_enter` is the CAVE-DOOR gate — can the player reach this door and
      act on whatever the shuffle put behind it — which is a different question
      from the shop's own access predicate (a bunny can buy from a shop but
      cannot open a chest). Where they differ the stricter cave gate is used, and
      every new gate is equal to or stronger than the one the pre-split entry
      applied to that door.
- [x] 1.3 `gen_entrance_table.py` emits `door_rows` and asserts: every entry has
      at least one row; no row is claimed twice; location lists are disjoint;
      entries sharing an entrance id declare the same room; and no SHARED
      entrance id lies in the fall-hole range `0x76`-`0x81` (design D3).
      `shop_loc_base` is deliberately not emitted (see 1.2b).
      "None dropped" needs the vanilla asset table, so it lives in
      `gen_entrance_door_rows.py --check`, which now VALIDATES the registry's
      declared partition against `kOverworld_Entrance_Id` rather than deriving
      it — derivation by entrance id is no longer possible now that ids are
      shared, and that is the one fact the asset tables cannot express.
- [x] 1.4 Regenerated `kCaveInteriors`: count 46, `kEntranceMaxInteriors` (64)
      still bounds it. `--dump-cave-doors` confirms 70 rows -> 46 interiors.

## 2. Door-row keying (the risky phase)

- [x] 2.1 Added `entry_of_door_row(lx)` / `Entrance_InteriorOfDoorRow(lx)` and
      switched `Entrance_BuildDoorOverlay` to it. ALSO switched
      `Entrance_BuildCrossOverlay` and `cross_source_endpoint` (reached through
      `Entrance_CrossDecoupledExit`, which now takes `lx`): both resolved the
      SOURCE by entrance id and would have collapsed the four room-0x10F doors
      onto one entry under Crossed and cross-decoupled. Not in the original task
      list; found by auditing every `interior_of_entrance` caller.
- [x] 2.2 Switched `Rando_RecordEnteredDoorForCapture` (source side),
      `Rando_DecoupledSetEnteredDoor` and `--dump-cave-doors` to the door-row
      map. The DESTINATION-side knowledge marking inside
      `Rando_RecordEnteredDoorForCapture` necessarily stays id-keyed — the live
      table byte is all it has. That is inert today because every id-sharing
      entry is location-less, so naming the wrong sibling reveals nothing; the
      comment records that 3.1 makes it load-bearing and that resolving it then
      needs the live assignment.
- [x] 2.3 `Rando_RecordEnteredFallhole` stays entrance-id-keyed. The shared-id
      fall-hole-range assertion is enforced in BOTH the generator and
      `Entrance_SelfCheck` (1), so a hand-edit of the generated block cannot slip
      past the data guard.
- [x] 2.4 `Entrance_SelfCheck` (1) re-expressed per door row (rows partition the
      pool; each row resolves to its own entry) and (7) rebuilt as a
      door-row-indexed synthetic table — plus a direct assertion that two entries
      sharing a room resolve independently by row. (7)'s missing bound is fixed:
      it was a hand-counted `uint8 van[80]` with no check, now the full u8
      door-row space with an explicit span assert.
      CORRECTION to the task as written: "entries sharing a room also share an
      entrance id" is NOT an invariant. Rows 0x57 and 0x59 both reach room 0x112
      through DIFFERENT ids (0x58 / 0x5A), and the pre-existing refill_cave_1 and
      fairy_cave_5 splits do the same. The sound direction is the converse, and
      that is what is asserted: entries sharing an entrance ID must declare the
      same ROOM (the id determines the room via `kEntranceData_rooms`).
- [x] 2.5 Baked arrivals handled better than "invalidate" (design D6). Each
      captured block records its own overworld area, so all 38 rows were
      re-attributed to the door they were actually captured at, and the table is
      now keyed by `interior_id` instead of position.
      This FIXED a live pre-existing bug that D6 had not accounted for: the two
      earlier mid-list splits (refill_cave_1, fairy_cave_5) had shifted every row
      from index 30 on, so eight interiors were loading another cave's arrival in
      decoupled mode — worse than absent, and invisible because a wrong arrival
      is still a valid overworld position. 38 of the 46 doors are now baked and
      correct; the other 8 load `valid == 0` and degrade to the coupled exit.
      Guarded permanently by a new `Rando_CaveArrivalBakeSelfCheck` (every row
      binds, no two rows claim one interior).

## 3. Shop identity from the layout

- [ ] 3.1 Add `shop_loc_base` to `RandoCaveInterior`; resolve a shop as
      door row -> source entry -> `assign` -> destination entry -> `shop_loc_base`
      (design D4), falling back to identity when no layout is active.
- [ ] 3.2 Reduce the static `kRandoShopSlots` to the one non-cave row (LW Death
      Mountain, room `0x0FF`, room-only).
- [ ] 3.3 Drop `Settings_ShopsanityForcedOff`; keep `Settings_ShopsanityActive`
      as the single consumer predicate. Re-enable the PC UI checkbox.
- [ ] 3.4 Extend `--rando-shop-doorwalk` to walk a SHUFFLED layout too, not just
      vanilla: for a generated cave-shuffle seed, every shop slot must still be
      reachable from exactly one room. This is the guard for the whole change.

## 4. Collateral the data-flow map surfaced (design D8)

No persistence work: the assignment is regenerated, not stored (design D7).

- [x] 4.1 Done as part of 2.4.
- [x] 4.2 Done as part of 2.4 — `van[80]` (hand-counted, unchecked) replaced by
      the full u8 door-row space with an explicit span assertion.
- [x] 4.3 Fixed. `Rando_EntranceConnection` now decides "unshuffled" on the pool
      ENTRY: the installed entry permutation is kept at activation
      (`g_entry_net`) because the door-table byte can no longer answer the
      question. Non-cave doors keep the id compare, which is exact for them
      (dungeon entrance-ids are unique).
      The same bug existed one layer up and was NOT in the task list: the
      discovery bit itself was only set when the overlay byte CHANGED, so an
      entry-to-sibling mapping would never have been marked discovered and 4.3
      alone would not have fixed the report. Discovery is now marked whenever the
      player walks through a door under an installed overlay — which is sound,
      since walking through it is how they learn where it goes.
- [x] 4.4 Asserted in `gen_entrance_table.py` (see 1.3).
- [x] 4.5 Confirmed: `kEntranceAddedEdgeMax` is 128 vs a worst case of
      46 + 11 = 57, and `OwWarp_SelfCheck`'s combined budget assert passes.
- [x] 4.6 Confirmed: strict codegen regenerates `kRandoCaveSourcePreds` at 46,
      and `Entrance_SelfCheck` hard-fails on any count mismatch.

## 5. Validation

- [x] 5.1 `--rando-selftest`, `--rando-grant-check`, `--door-selftest` green.
- [x] 5.2 MSVC (Release x64) + WSL `gcc -Werror` both clean.
- [x] 5.3 `kGeneratorVersion` 159 -> **161** (160 is claimed by the unmerged
      `codex/bomb-grass-logic` branch). Corpus regenerated: 15 of 243 rows moved,
      and the split is exactly as predicted — all 15 carry
      `shuffle_cave_entrances`, and ZERO rows without it moved. The two
      cave-axis rows that did NOT move are the Inverted and Retro ones, where
      `Entrance_IsActive` normalizes the axis off by world state, so
      byte-identical is correct there.
- [ ] 5.4 Corpus rows for `shopsanity + shuffle_cave_entrances` — DEFERRED to
      phase 2b with the rest of the composition; shops are still forced off under
      cave shuffle, so such a row would only re-pin the forced-off behavior the
      existing `shopsanity-entrance-caves-forced-off` row already covers.
- [x] 5.5 All CI guards green: `run_rando_validation.py full` PASSES end to end
      (243/243 corpus, slot-path matrix, invariant sweep, placer determinism,
      benchmarks), plus `check_init_order.py`, `check_no_embedded_data.py` and
      `--rando-shop-doorwalk` (0/27 unreachable, 0 room-aliased) run explicitly.
- [x] 5.6 Independent fresh-eyes review of the split commit. 1 HIGH + 2 MED + 3
      LOW; the HIGH and both MEDs are FIXED in the follow-up commit. Findings:
      - **HIGH (fixed).** The baked arrival row for
        `general_store_2_dw_death_mountain` carried world flags saying LIGHT
        (`is_dark`/`save_dark` = 0/0) while the same block's own recorded
        overworld area is `0x0045` — bit `0x40` set, dark screen `0x05`. All 37
        other rows agree with their area, so it was a bad capture, not a
        convention. `Rando_ReplayCaveArrival` writes both verbatim, so a
        decoupled exit onto it would set `is_in_dark_world = 0` AND the
        SRAM-persisted `savegame_is_darkworld = 0` while on Dark World Death
        Mountain. Pre-existing (it was the old positional index 21), but the
        re-attribution moved it somewhere it looked plausible. Row DROPPED
        rather than repaired — fabricating two flags onto a capture whose
        positional data is equally suspect is the "confidently-wrong" failure
        D6 warns against. Coverage 38/46 -> 37/46.
      - **MED (fixed).** `Rando_CaveArrivalBakeSelfCheck` asserted name-binding
        and no-duplicates but nothing that could have caught the above. It now
        also asserts each row's recorded area is one of its interior's door
        rows' areas, and that its world flags agree with that area's dark bit.
      - **MED (fixed).** `cave_arrival_capture.bin`, the dev resume sidecar, is
        still a raw POSITIONAL dump whose length does not change when the pool
        grows. A pre-split copy would land on the wrong interiors, override the
        correct name-keyed rows, and then be re-emitted under the NEW names —
        corrupting the table during the very re-capture walkabout (task 7.3)
        meant to fix it. Now stamped with magic + pool size + interior-name
        hash; a mismatch is reported and ignored.
      - **LOW (accepted, verified).** The new fall-hole-range assertions can
        never fire: `kFallHole_Entrances` holds exactly `0x76`-`0x81` and no
        registry entry declares an id in that range, so
        `Rando_RecordEnteredFallhole`'s id lookup already always returns -1.
        The retained id lookup is therefore justified by a path that is dead
        rather than merely rare. Harmless and pre-existing — the assertion still
        documents the constraint that would matter if a fall-hole cave were ever
        added — but the reasoning in 2.3 is weaker than it reads.
      - **LOW (accepted by design).** `Rando_EntranceConnection` now emits
        `from == to` self-loops for entry->room-sibling mappings, so a tracker
        may render "General Store 3 -> General Store 3". That is the intended
        consequence of 4.3: the ids genuinely carry no distinguishing
        information, and the JSON's `door` field (the row) is the disambiguator.
        It becomes meaningful once 3.1 gives those entries distinct shops.
      - **LOW (partially fixed).** `entry_of_door_row` is a committed constant
        with no runtime tie to the live asset table, and the offline validator is
        skipped under `--allow-missing-assets`. Added the runtime assertion that
        every door row's LIVE entrance id is one its interior declares.

## 6. Docs & spec

- [ ] 6.1 `docs/randomizer.md`: the axes now compose; document the decoupled
      arrival degradation. DEFERRED to 2b — nothing player-visible changed in
      2a (shops are still forced off under cave shuffle and coupled mode, the
      default, is unaffected), so there is no user-facing behavior to document
      yet. The decoupled gap is 8 doors, not six, and is recorded in
      `cave_arrival_baked.h` and task 7.3.
- [x] 6.2a Reconciled the `randomizer-shuffles` delta against as-built source:
      the append-only rationale named three positionally-keyed structures, but
      the baked arrival table is now name-keyed, so it is two plus an explicit
      requirement for the arrival table's keying and failure mode. Added the
      fall-hole shared-id constraint and the entry-vs-id tracker rule.
      The `randomizer-shopsanity` delta is untouched: it describes phase 2b.
- [ ] 6.2b Re-reconcile after 2b, then archive on the branch.

## 7. Owner-gated (cannot be automated)

### Phase 2a playtest — cave-entrance shuffle only (shops still off)

The generator side is pinned by the corpus; what no automation covers is the
runtime door/exit behavior. Open or Standard, `shuffle_cave_entrances` on,
`shopsanity` off (it normalizes off anyway).

- [x] 7.0a **PASSED, owner 2026-07-26.** Light-world doors of two different split
      rooms walked: room `0x11F`'s Lumberjack House door (ALTTPR door 0x76) led
      to `bomb_shop`, room `0x112`'s LW Lake Hylia shop door (0x58) led to
      `hype_cave`, and every door returned the player to the door they entered.
      F12 dump inside the Lumberjack House door's destination confirms it at the
      RAM level: `kRam_RandoOverworldDoor` = `0x76` (door row 0x75 + 1 — so the
      SPLIT entry 45 resolved, not entry 32, which is what the door-row keying
      exists to do), `which_entrance` = `0x53` and `dungeon_room_index` = `0x11C`
      (bomb_shop), `overworld_area_index` = `0x0002` (the door's own screen), and
      `overworld_area_index_exit` = `0x0002` — i.e. the cached exit block that
      design D9 argues is "safe by construction" was directly observed holding
      the entered door's arrival. Room `0x10F`'s four dark-world doors are the
      same code path and were deferred to the post-2b retest.
- [x] 7.0b Settled by construction rather than by walking: the permutation is a
      bijection over pool entries, so once the Lumberjack House door was observed
      resolving to entry 45 (dump above), no other entry can share its
      destination. Two doors of one room can only collide if the split failed to
      take effect, which the dump rules out.
- [ ] 7.0c Enter a door whose destination is a room-sibling (the overlay byte is
      unchanged) and confirm the auto-tracker lists it as a discovered
      connection — that is the 4.3 fix, and it cannot be reached from the corpus.
- [ ] 7.0d Sanity: with `shuffle_cave_entrances` OFF, cave doors behave exactly
      as vanilla (the identity assignment path).
- [ ] 7.0e Decoupled on: confirm no softlock at the 9 unbaked doors — they should
      emerge at the door entered (coupled fallback), not somewhere wrong. Also
      confirm the previously-misattributed doors (everything from
      `refill_cave_1_*` onward) now emerge at their OWN position.
- [ ] 7.0f Confirm the Graveyard Ledge and Cave 45 doors behave correctly under
      cave-entrance shuffle specifically (their entrance ids were crossed in the
      registry until 2026-07-26; owner-confirmed by warp test, but the SHUFFLED
      path was never exercised).

### Phase 2b playtest (after shop identity lands)

- [ ] 7.1 Playtest: enter a shuffled shop door and confirm the shop that appears
      is the destination's shop, with its own three slots and prices.
- [ ] 7.2 Playtest: confirm exiting a shuffled shop returns to the door entered
      (coupled), in both a shop-behind-random-door and random-thing-behind-shop-
      door case.
- [ ] 7.3 Arrival capture walkabout (`ZELDA3_CAPTURE_ARRIVALS=1`) for the 9
      unbaked doors, then re-bake `cave_arrival_baked.h` to restore full
      decoupled fidelity. Until then decoupled is conservatively coupled there.
      The doors: `general_store_2`, `general_store_3`,
      `refill_cave_1_graveyard`, `fairy_cave_5_checkerboard`,
      `general_store_2_dark_sanctuary`, `general_store_2_dw_death_mountain`,
      `general_store_3_outcasts`, `general_store_3_potion`,
      `kakariko_lame_shop_lumberjack_house`.
      DELETE any existing `cave_arrival_capture.bin` first — it is positional and
      a pre-split copy is now rejected with a message rather than silently
      applied. The capture tool emits name-keyed rows, so a partial walkabout can
      be pasted in as-is.
      `general_store_2_dw_death_mountain` is the one that needs care: its
      previous capture had world flags contradicting its own area, so verify
      after re-walking that its emitted row reads `0x01, 0x40` (dark).
