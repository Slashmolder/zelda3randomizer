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
- [ ] 1.1b Add the derived `door_rows` and an optional `shop_loc_base` to
      `entrance_registry.yaml`.
- [ ] 1.2 Split entries 21, 23, 32 by door row per design D1, **appending** the
      new entries at indices 40-45 so 0-39 keep their positions (design D2).
      Give each split entry its own `region_name` and, for shop entries, the
      three shop `location_ids`.
- [ ] 1.3 Update `assets/scripts/gen_entrance_table.py` to emit `door_rows` and
      `shop_loc_base`, and to assert: door rows are a partition of the cave door
      rows (no row in two entries, none dropped), and no split entry's entrance
      id lies in the fall-hole range `0x76`-`0x81` (design D3).
- [ ] 1.4 Regenerate `kCaveInteriors` in `src/rando/shuffle_entrance.c`; confirm
      the count is 46 and `kEntranceMaxInteriors` (64) still bounds it.

## 2. Door-row keying (the risky phase)

- [ ] 2.1 Add `Entrance_EntryOfDoorRow(uint16 lx)` backed by the generated
      `door_rows`, and switch `Entrance_BuildDoorOverlay` to it.
- [ ] 2.2 Switch `Rando_RecordEnteredDoorForCapture`,
      `Rando_DecoupledSetEnteredDoor` and `--dump-cave-doors` to the door-row
      map (all three already have `lx`).
- [ ] 2.3 Leave `Rando_RecordEnteredFallhole` entrance-id-keyed; add the
      assertion from 1.3 so the assumption is enforced, not assumed.
- [ ] 2.4 Update `Entrance_SelfCheck`: the door-overlay assertion currently
      expects `kCaveInteriors[assign[slot_interior[d]]].entrance_ids[0]` keyed
      by entrance id; re-express per door row. Add a check that entries sharing
      a room also share an entrance id (they are the same physical room).
- [ ] 2.5 Invalidate the baked arrival entries for the split rooms (design D6)
      and confirm the loader's `valid == 0` path degrades to coupled.

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

- [ ] 4.1 Re-express `Entrance_SelfCheck` check (1) per door row — it currently
      asserts `interior_of_entrance(entrance_ids[k]) == i` and hard-fails as
      soon as two entries share an entrance id.
- [ ] 4.2 Add the missing bounds check to self-check (7)'s `uint8 van[80]`
      synthetic door table (latent stack overflow; sum goes 57 -> ~61).
- [ ] 4.3 Fix `Rando_EntranceConnection`'s `to == from` early-out so a
      shop-entry -> shop-entry mapping is not reported as unshuffled to the
      auto-tracker; compare entries, not entrance ids.
- [ ] 4.4 Assert in the generator that location lists are disjoint across
      entries (the region-override store is last-write-wins, and
      `Entrance_CaveInteriorOfLocation` is location-keyed).
- [ ] 4.5 Confirm `Rando_EntranceAddedEdgeCapacity()` covers
      `Entrance_AddedEdgeWorstCase()` at 46 + 11 = 57.
- [ ] 4.6 Confirm `kRandoCaveSourcePreds` regenerates at 46 entries and stays
      positionally aligned (append-only ordering).

## 5. Validation

- [ ] 5.1 `--rando-selftest`, `--rando-grant-check`, `--door-selftest` green.
- [ ] 5.2 MSVC + WSL `gcc -Werror` clean.
- [ ] 5.3 Bump `kGeneratorVersion`; regenerate the corpus. Expected movement:
      EVERY cave-entrance-shuffle row moves (the pool grew); every seed without
      cave-entrance shuffle stays byte-identical. Verify that split — a
      non-cave-shuffle seed moving means something leaked.
- [ ] 5.4 Add corpus rows for `shopsanity + shuffle_cave_entrances` in Open and
      Standard, and one with `decoupled` on.
- [ ] 5.5 All CI guards, including `check_init_order.py` and the shop door-walk.
- [ ] 5.6 Independent fresh-eyes review — this touches entrance/exit caching,
      which has historically been the costliest area in this project.

## 6. Docs & spec

- [ ] 6.1 `docs/randomizer.md`: the axes now compose; document the decoupled
      arrival degradation for the six new doors.
- [ ] 6.2 Reconcile deltas against as-built source, then archive on the branch.

## 7. Owner-gated (cannot be automated)

- [ ] 7.1 Playtest: enter a shuffled shop door and confirm the shop that appears
      is the destination's shop, with its own three slots and prices.
- [ ] 7.2 Playtest: confirm exiting a shuffled shop returns to the door entered
      (coupled), in both a shop-behind-random-door and random-thing-behind-shop-
      door case.
- [ ] 7.3 Arrival capture walkabout (`ZELDA3_CAPTURE_ARRIVALS`) for the ten
      split doors, then re-bake `cave_arrival_baked.h` to restore full decoupled
      fidelity (design D6). Until then decoupled is conservatively coupled there.
