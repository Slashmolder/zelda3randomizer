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

- [x] 3.0 Bound each split entry's three shop `location_ids` (deferred from 1.2),
      which is what switches `Entrance_SelfCheck` (2)'s registry<->logic region
      cross-validation on for those entries. All eight cave-resident shops'
      derived per-door regions passed it unchanged — the D9 derivation was
      right, so the guard confirmed rather than corrected. The generator and a
      new self-check (2b) additionally assert base <-> locations agreement, so a
      base declared without its slots (a rebinding that silently would not
      happen) fails the build in both places.
- [x] 3.1 `shop_loc_base` added to `RandoCaveInterior` and emitted by
      `gen_entrance_table.py` (0xFFFF = no shop). `shop_lookup` resolves door
      row -> source entry -> installed permutation -> destination entry ->
      `shop_loc_base`, with the DESTINATION entry's room as a fail-closed check.
      DEVIATION from D4 as written, and it is load-bearing: the chain is
      `Entrance_DestCaveOfDoorRow(net, n, cross, lx, vanilla_id)`, which also
      handles CROSS-category, where the permutation runs over the combined
      cave+dungeon endpoint space. D4 described only the cave pool; under
      Crossed a shop's entry can land behind a DUNGEON door, and the placer
      already models that (`Entrance_ApplyCrossOverrides` binds the destination
      cave's locations to the dungeon source's door-edge region), so a
      cave-only resolution would have made that shop reachable in logic and
      unobtainable at runtime. Confirmed live: the doorwalk's `crossed` arm
      shows Desert Palace's door row 0x08 selling the Dark World Death Mountain
      shop.
- [x] 3.2 `kRandoShopSlots` reduced to the single room-only LW Death Mountain
      row. `Rando_DumpShopCheckDebug` iterated that table, so it was reworked to
      iterate the 27 slot ids and recover each shop's LIVE (room, door) by
      walking the door table through the resolver — which also makes the dump
      show the shuffled door rather than a vanilla one.
- [x] 3.3 `Settings_ShopsanityForcedOff` and its `apply_derived_rules`
      normalization removed; `Settings_ShopsanityActive` is now just
      `shopsanity != 0`. PC ImGui checkbox re-enabled (the disable + explanatory
      caption are gone, not merely inverted).
- [x] 3.4 `--rando-shop-doorwalk` now runs seven arms: `vanilla`, five generated
      layouts (three cave-only across Open/Standard, one decoupled, one
      Crossed), and `post-teardown`. The shuffled arms go through the real
      `Entrance_RuntimeInstall` via a new `Rando_EntranceInstallLayoutForAudit`
      hook, so they walk the actual overlay and the actual `g_entry_net` — a
      re-derived model would only have validated itself, which is the failure
      the vanilla arm was built to avoid in the first place. Added a third
      invariant beyond reachable/not-room-aliased: a door's three columns must
      resolve to ONE shop's consecutive slots. 0 violations on all seven arms.
      The `post-teardown` arm exists because the audit hook installs an overlay
      with no slot behind it; it proves teardown restores the pristine table.

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
### Phase 2b

- [x] 5.1b `--rando-selftest`, `--rando-grant-check`, `--door-selftest` green.
      Two new self-checks added rather than relying on the corpus:
      `Entrance_SelfCheck` (2b) rejects a `shop_loc_base` whose three slots are
      not bound, and (6b) asserts the shop rebinding against the override store
      via a forced 2-swap. (6b) exists because reading the composed seed's
      spoiler — the first thing tried — proved NOTHING: the spoiler prints every
      location's STATIC logic region, for all location types, so a rebound slot
      and an unbound one look identical there.
- [x] 5.2b MSVC Release x64 + WSL `gcc -O2 -Werror` both clean, and the two
      binaries agree on the moved digest (cross-platform determinism).
- [x] 5.3b `kGeneratorVersion` 161 -> **162**. Re-grepped every local branch
      before picking: 161 is main/2a, 160 is claimed by `codex/hints-v2` and
      `codex/bomb-grass-logic`, nothing claims 162.
      **Corpus moved exactly as predicted: 3 of 245 rows, all carrying BOTH
      axes** (the relabelled compose row + the two new ones). Every
      cave-shuffle-only row and every shopsanity-only row is byte-identical.
      That was the load-bearing measurement, not a formality: adding shop
      `location_ids` to cave entries sets region overrides on locations that are
      NOT fill locations when shopsanity is off, and "that should be inert" was
      an assumption until the regen showed zero movement. Spot-checked the same
      rows against the pre-bump manifest BEFORE bumping, so the inertness was
      established independently of the regen that rewrote the file.
- [x] 5.5c **KNOWN RED, owner-accepted 2026-07-27:**
      `check_hint_version_policy.py`. It lists `src/rando/shuffle_entrance.c` in
      `ENTRANCE_REGION_PATH` and treats ANY edit to that file as hint-sensitive
      (unconditionally — unlike the codegen path, which requires a content
      match), then demands BOTH `kRandoHintPlanAlgorithmVersion` and
      `kRandoHintTextSchemaVersion` advance. It rejects this commit AND
      `cfa0d2c5`, which is main's HEAD — so main is already red on this gate.
      The guard was introduced by `df72b4a0`, 2a's own parent, so no entrance
      change has ever satisfied it; 2a's "5.5 full PASSES end to end" is not
      reproducible today.
      The concern is real in one direction — hints DO consume
      `Rando_GetEntranceRegionOverride` (`hint_effective_region`,
      `rando_hints.c`), so shop rebinding changes hint text on composed seeds.
      But the two axes identify the plan ALGORITHM and the TEXT SCHEMA for
      loading a persisted plan, and neither changed; input drift is caught by
      the persisted plan digest plus `kGeneratorVersion`, and entrance seeds
      additionally by `entrance_digest24`, both of which hard-refuse across this
      bump. Advancing the axes would assert a schema change that did not happen
      and would downgrade old saves from an accurate "digest mismatch" to a
      wrong "unsupported". Owner chose to accept the red rather than bump or
      silently narrow the guard. Spun out as its own change.
- [x] 5.5b `run_rando_validation.py full` green end to end **up to that gate**,
      which short-circuits the profile; the 24 gates it would otherwise have run
      were then executed individually and ALL PASS (including the 246-row
      regression corpus, the slot-path matrix, the invariant sweep, the runtime
      placer-determinism canary and all three hint guards), plus
      `gen_entrance_door_rows.py --check` (70 rows / 46 interiors, world
      consistent), `check_knowledge_consumers.py`, `check_grant_consumers.py`
      and `check_no_embedded_data.py` run explicitly.
      TOOLING NOTE: `full` cannot run under WSL in a Windows-created worktree
      without `GIT_DIR` + `GIT_WORK_TREE` exported — WSL git cannot follow the
      worktree's `.git` file (it holds a Windows path), and the profile
      hard-fails at its generator-version diff guard. `--base-sha`/`--head-sha`
      do not work around it; the guard still has to `cat-file` those refs.

- [x] 5.6b Independent fresh-eyes review of the 2b commit. **0 HIGH**, 2 MED +
      2 LOW; all four FIXED. It also independently re-derived and confirmed
      D11's conservative-direction claim, the player-knowledge gating, and the
      Retro/Inverted/fall-hole/mirror no-change analysis.
      - **MED (fixed).** The canonical `randomizer-core` spec still MANDATED the
        deleted normalization — both in the serialization requirement's body and
        in a scenario asserting byte 29 bit 4 serializes `0` with a hash equal to
        the shopsanity-absent seed. The change shipped no `randomizer-core`
        delta, so archiving would have made a provably-false *serialization*
        requirement the permanent baseline, and `openspec validate` cannot catch
        it (structure, not truth) — exactly the delta-rot trap CLAUDE.md warns
        about, one capability over from where I was looking. Added a
        `randomizer-core` MODIFIED delta, extracted PROGRAMMATICALLY from the
        canonical file and then edited in two places, so the 31-byte
        serialization table could not pick up transcription drift (diffed back
        against canonical to prove only the two intended hunks differ).
      - **MED (fixed).** No corpus row covered `cross_category + shopsanity` —
        the one composition whose PLACEMENT-side binding comes from a different
        function (`Entrance_ApplyCrossOverrides`, over the combined endpoint
        space) than every other row's `Entrance_ApplyRegionOverrides`. The
        doorwalk's `crossed` arm only pins the RUNTIME half, and self-check (6b)
        only exercised the cave-pool function, so a cross path that bound
        nothing would have passed everything while the runtime still sold shops
        — the placement/runtime disagreement this change exists to prevent.
        Added self-check (6c) (the same forced 2-swap through the cross path)
        and corpus row `shopsanity-entrance-crossed`. The cross binding turned
        out to be CORRECT; the gap was in coverage, not behavior.
      - **LOW-MED (fixed).** `shop_lookup` could not distinguish "no permutation
        installed" from "identity permutation", and the destination-room guard
        is blind among room-siblings — so a frame reaching a shop with the door
        byte live but `g_entry_net_n == 0` would resolve the door's VANILLA
        shop, pass the room compare (four DW shops share room 0x10F), and hand
        over a real but WRONG shop: wrong item, wrong price, wrong location
        marked checked. The reviewer traced every teardown path and found no
        live repro, but the resolver had no positive "layout expected but
        absent" test. Added `Entrance_EntryNetExpected()` (keyed on
        `Settings_EffectiveShuffleCaveEntrances`, so world-state normalization
        is honored) and made that case return "no shop". No shop beats the
        wrong shop.
      - **LOW (fixed).** `Rando_EntranceInstallLayoutForAudit` is exported
        unguarded but routes through `Entrance_RuntimeInstall`, which re-Begins
        the shared override stores and wipes `g_entrance_discovered`. Harmless
        from the doorwalk (which `exit()`s), but a future in-process caller —
        e.g. adding it to `Rando_RunAllSelfChecks` — would silently clear an
        active slot's tracker overrides and persisted discovery bitmap. Now
        refuses while `g_rando_slot_active`.

### Phase 2a

- [x] 5.3 `kGeneratorVersion` 159 -> **161** (160 is claimed by the unmerged
      `codex/bomb-grass-logic` branch). Corpus regenerated: 15 of 243 rows moved,
      and the split is exactly as predicted — all 15 carry
      `shuffle_cave_entrances`, and ZERO rows without it moved. The two
      cave-axis rows that did NOT move are the Inverted and Retro ones, where
      `Entrance_IsActive` normalizes the axis off by world state, so
      byte-identical is correct there.
- [x] 5.4 Done in 2b. `shopsanity-entrance-caves-forced-off` relabelled
      `-compose` (same settings/seed, digest moves), plus new
      `shopsanity-entrance-caves-standard` and
      `-decoupled` rows. The decoupled row exists because the one-way exit net is
      an INDEPENDENT permutation layered on the entry shuffle: if a regression
      ever resolved a shop through the exit net, that row moves while the coupled
      row does not.
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

- [x] 6.1 `docs/randomizer.md` done in 2b. The Shopsanity section's
      "forced off by cave-entrance shuffle" block became a Composition block
      (with the old reasoning kept as a parenthetical so the removal is not
      mysterious later), the settings-table row lost its normalization note, and
      the runtime-identity paragraph now says identity is derived per seed and
      describes the doorwalk's two arms. Added generator-version rows 160→161
      and 161→162 — 2a never wrote its own, so the table skipped from 160 to a
      gap. ALSO corrected a claim 2a made stale: the entrance-shuffle changelog
      row still said the baked arrival table makes "every door one-way from
      launch"; it is now 37 of 46, with the other 9 degrading to the coupled
      exit until task 7.3's walkabout.
- [x] 6.2a Reconciled the `randomizer-shuffles` delta against as-built source:
      the append-only rationale named three positionally-keyed structures, but
      the baked arrival table is now name-keyed, so it is two plus an explicit
      requirement for the arrival table's keying and failure mode. Added the
      fall-hole shared-id constraint and the entry-vs-id tracker rule.
      The `randomizer-shopsanity` delta is untouched: it describes phase 2b.
- [x] 6.2b Re-reconciled both deltas against as-built source, then archived on
      the branch. Three things the deltas asserted or omitted that the code
      settled differently:
      - `randomizer-shopsanity` said the live door is derived "door row ->
        source entry". It now also covers the CROSS-category source (a dungeon
        door can be a shop's source; dungeon ids are unique so that side resolves
        by id), states that one source entry's several door rows are the same
        shop rather than an alias, and states the room compare is against the
        DESTINATION entry's room as a fail-closed guard. See design D11.
      - The delta required each shop to "carry its three slot ids"; nothing said
        who enforces it. Added: an entry naming a shop without binding its slots
        must fail the build, and the rebinding must be asserted against the
        override store — because the spoiler prints every location's STATIC
        logic region, so a spoiler read cannot tell a rebound slot from an
        unbound one. (That is not hypothetical: reading the composed seed's
        spoiler was the first check tried, and it showed the vanilla regions.)
      - `randomizer-shuffles` covered the tracker's entry-vs-id rule but not the
        general one. Generalized: any consumer needing the entry a door LOADS
        (as opposed to the entry it BELONGS to) must resolve through the
        assignment, with the destination-side knowledge marking named
        explicitly, plus the cross-category endpoint-space rule and a scenario.

      ARCHIVER LIMITATION, worked around deliberately — worth knowing before the
      next spec change. `openspec archive` refuses a MODIFIED requirement whose
      block omits ANY scenario title the canonical spec has, reading it as an
      accidental drop; there is no scenario-level RENAME, `--no-validate` does
      not skip that merge check, and REMOVED+ADDED of one requirement name is
      rejected by `validate`. The refusal is a GOOD guard: running the same
      scenario-set diff by hand across all three deltas found that the
      `randomizer-shopsanity` delta (authored in phase 1) silently dropped FIVE
      canonical scenarios — three of them still true and simply not carried over
      (`The door-less shop resolves from its room alone`, `An undeclared door
      into a shop room stays vanilla`, `No derived-rule coupling`). Archiving an
      earlier draft would have deleted them from the baseline. Two scenarios
      genuinely needed renaming (their titles asserted the old behavior), so the
      deltas keep the OLD titles with corrected bodies plus an inline NOTE, and
      the rename is applied to `openspec/specs/` directly in the same commit.
      The canonical specs are the coherent artifact; the archived deltas record
      why they differ.

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
