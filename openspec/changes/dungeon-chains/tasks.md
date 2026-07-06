# Tasks: dungeon-chains

Ordering note: group 1 resolves the design's UNVERIFIED items and open questions
BEFORE broad implementation; group 2 (logic restructure) lands first as an inert,
corpus-identity-proven change; the runtime spike (task 5.1) gates the rest of
group 5. Re-grep `kGeneratorVersion` and the corpus manifest from live main
immediately before any bump/commit (concurrent-drift discipline).

## 1. Grounding and verification spikes

- [x] 1.1 Write `assets/scripts/gen_chain_seam_tables.py`: derive, from
      `zelda3_assets.dat` room headers + `door_registry.yaml` + entrance data,
      the per-pool-dungeon boss-seam table (every transition — door/stair/hole —
      whose destination is one of the 9 pool `kBossRoom[]` ids, with source room
      and class) and every OUTBOUND transition from each boss room. Commit the
      generator and its emitted C table; assert completeness (each pool dungeon
      has ≥1 seam; IP's drop-seam class resolved here).
- [x] 1.2 Read the entrance-shuffle Stage-2 main-door edge keying for DP/TR
      (multi-inbound entry regions) in `shuffle_entrance.c` and record the
      mechanism chains will reuse for chain-start door edges (design.md D6
      UNVERIFIED item).
- [x] 1.3 F12-verify boss-shutter behavior after a boss kill in vanilla (does the
      entry shutter reopen before the warp?) and record it in design.md — it
      determines whether D4 rule 2 ever fires pre-warp (ram-bit truth rule).
- [x] 1.4 Audit follower handling across entrance loads (`follower_indicator`
      consumers) and decide the hop-boundary follower policy (design open
      question 3); record the decision in design.md.
- [x] 1.5 Trace the pendant (module 19) vs crystal (module 22) post-boss
      choreography from `PrepareDungeonExitFromBossFight` to
      `LoadOverworldFromDungeon` and confirm both reach the exit-door
      resolution the origin substitution keys on; note any path that bypasses
      it.
- [x] 1.6 Ground DP/TR aux geometry: whether DP's W/E side doors are reachable
      from overworld ground level (chain-pure vs partially open-world — flavor
      only) and that TR's balcony ledges are re-enterable pockets; record the
      answers in design.md D1/D5.

## 2. Logic restructure (inert, lands first)

- [x] 2.1 In `assets/rando_logic_gen.py`: emit per-pool-dungeon `<D>_BossRoom`
      regions, the single inbound edge carrying the derived `boss_approach(D)`
      (strip exactly one `CanKillBoss(D)` conjunct from the Boss predicate;
      hard-fail codegen on zero or multiple occurrences), and re-home Boss/Prize
      locations to the new regions with `CanKillBoss(D)` predicates.
- [x] 2.2 Regenerate the corpus (fresh WSL build, `rm src/rando/logic_data.c`
      first, `make clean` after header edits) and 3-way diff against unmodified
      main built fresh in the same environment: require byte-identical digests
      for ALL existing entries (spec scenario "Off is placement-identical"). Any
      movement blocks this task, not a version bump.
- [x] 2.3 Extend `Logic_SelfCheck` to assert the factoring invariants (each
      `<D>_BossRoom` has exactly one inbound edge; Boss/Prize homed there).

## 3. Settings and normalization

- [x] 3.1 Add `dungeon_chains` to `RandoSettings` + canonical byte `out[25]` bit
      6 (`rando_settings.{c,h}`); parser key `dungeon_chains=` for
      `--settings=`.
- [x] 3.2 Implement the one-directional normalization in `apply_derived_rules`
      (chains off unless: no entrance axes, door vanilla, boss off, world_state
      ∈ {Open, Standard}, NoGlitches); keep it a pure computation (no recursion
      via derived rules — door-shuffle lesson).
- [x] 3.3 Settings self-check: default packs to 0x00; hash stability vs a
      pre-axis canonical blob; normalization truth table.

## 4. Chain construction + generation

- [x] 4.1 `src/rando/shuffle_chains.{c,h}`: `Chains_Compute(seed, attempt)`
      (Fisher-Yates + uniform composition + pinned TT→Blind / ToH→Moldorm
      adjacencies, dedicated RNG salt), the three output tables, and the 24-bit
      digest. Register both files in `zelda3.vcxproj` (MSBuild does not glob).
- [x] 4.2 `Chains_SelfCheck` (partition validity, pins, recompute stability) and
      wire it into `--rando-selftest`; digest must match MSVC ↔ gcc.
- [x] 4.3 Integrate into `Rando_PlaceWithEntrances`: per-attempt
      `Chains_Compute` + edge-override install (clear ALL override state at
      attempt top — the accumulation bug class), full-reachability acceptance
      gate; update the `main.c` / `rando_generate.c` path-selection conditions
      so chains-only settings route through `Rando_PlaceWithEntrances`.
- [x] 4.4 Spoiler: chains section (JSON + text) in `rando_spoiler.c` after the
      entrance sections.
- [x] 4.5 Corpus: add chains entries (chains-open-ganon, chains-standard,
      chains + prize_shuffle, chains with forced in-dungeon keys — locks the
      chain-order key-placement scenario — and a hunt-goal entry with
      accessibility=none) via
      `bump_rando_corpus.py --apply` (absolute `--binary` path; restore CRLF on
      the manifest); single `kGeneratorVersion` bump at the end of the group.

## 5. Runtime

- [x] 5.1 SPIKE FIRST (highest risk): mid-dungeon entrance-style hop load — new
      consumed-at-top hop flag that suppresses the `*_exit` re-cache in
      `Dungeon_LoadEntrance` (dedicated flag; NOT death_var4/5), module
      transition into `Module_PreDungeon` from inside module 7 with fade.
      Playtest ONE hop (EP boss seam → another lobby) with the whole decision
      chain instrumented before building anything else on it. <!-- owner
      playtest 2026-07-04: Debug > Watch EP->DP seam arm landed in Desert main
      with the hop flag consumed; tracker big-key false-positive was a separate
      live-count bug fixed in 5b3c7e8f. -->
- [x] 5.2 Synthetic boss-room entrance records: extend
      `assets/compile_resources.py` (+ extractor parity) to append the 9
      records (spawn/camera/quadrants derived; palace = home dungeon; music =
      home theme) generated by a committed script; asset-version bump; C-side
      size assert. Fallback if derivation proves insufficient: a
      `ZELDA3_CAPTURE_ARRIVALS`-style capture pass (user-driven). <!--
      done 2026-07-04: gen_chain_boss_entrances.py emits ids 133..141; asset
      signature bumped to Zelda3_v1; local blob has 142 entrance rows; runtime
      rejects synthetic hops unless rows, palace, and music match. -->
- [x] 5.3 Seam interception INTO boss rooms: hook door/stair/hole destination
      resolution (door site adjacent to `Rando_DoorTransOverride`; stair/hole
      sites from task 1.1's table); pinned adjacencies take the vanilla path
      untouched; divert triggers the 5.1 hop load of `chain_successor[D]`. <!--
      done 2026-07-04: door/stair/hole hooks call table-driven
      Chains_TryBossSeamHop; generated main_entrance_id maps dungeon successors;
      identity successors fall through vanilla; hooks stay dormant until 5.6
      installs a runtime layout. Release build + --rando-selftest green.
      Follow-up headless coverage asserts boss-seam hook behavior for dungeon
      successor hops, synthetic boss successor hops, and identity successors. -->
- [x] 5.4 Terminal containment: outbound transitions from a terminal boss room
      divert to the chain exit (origin door); verify cleared-terminal re-entry
      is exitable. <!-- done 2026-07-05: door/stair/hole outbound seams match
      kChainBossOutboundSeams while terminal state is armed and the terminal prize
      location is checked; pre-reward transitions stay vanilla so Moldorm fall-out
      keeps the retry loop. Terminal exits route through the normal module-15 ->
      module-8 overworld loader using chains-owned origin_exit_room. Release
      build + --rando-selftest green. Follow-up headless coverage asserts
      pre-reward terminal outbound seams do not fire, stay armed, and then
      consume origin only after the terminal prize location is checked; end-to-end
      cleared-terminal owner playtest remains tracked in 7.3. -->
- [x] 5.5 Chain-start doors + origin coupling: extend the
      `kOverworld_Entrance_Id` overlay install for the 9 main doors; add the
      chains-owned origin session state (armed at chain entry — NOT the
      one-shot `g_rando_entrance_exit_room` global, which any aux exit would
      consume) and the main-door-keyed substitution applied after the
      room-keyed exit search resolves a door; confirm the post-boss warp and
      mid-chain main-door walkouts resolve to origin while DP/TR aux exits
      stay vanilla (task 1.5 trace). <!-- done 2026-07-05: chain overlay builder
      remaps only the 9 generated main-door source ids, Overworld_UseEntrance
      arms chains-owned origin state, and LoadOverworldFromDungeon consumes that
      origin only after the resolved exit room is a chain main exit. Generated
      main_exit_room metadata keeps the code table-driven. Chains_RuntimeSelfCheck
      covers overlay install/restore, origin arming, non-main preservation,
      main-exit consumption, and direct terminal-boss arming. Release build +
      --rando-selftest green; DP/TR owner playtest remains tracked in 7.3. -->
- [x] 5.6 `Chains_RuntimeInstall` / teardown wired into
      `Rando_ActivateSidecarSlot` / `Rando_DeactivateSlot`: regenerate from
      (seed, attempt), hard-fail on digest mismatch, fail closed if synthetic
      entrance records are absent; M4 cold-replay rebuild path included.
      <!-- done 2026-07-05: activation regenerates from the saved
      chains_attempt/digest24 identity, refuses missing/drifted identities, and
      installs the chain runtime overlay only after synthetic entrance records
      validate. Deactivation and snapshot replay clear the chain asset-126 owner,
      logic-overlay replay restores chain edge overrides from the same digest
      gate, and snapshot type-7 carries chain attempt/digest plus origin/terminal
      session state for cold replay. RandoSnapshotTail_SelfCheck covers restore,
      in-flight session restore, re-save perpetuation, missing ChainLayout
      fail-closed, and bad digest fail-closed with a synthetic asset fixture.
      Chains_RuntimeSelfCheck rejects malformed restored origin/terminal sessions.
      git diff --check, openspec validate dungeon-chains --strict, Release build,
      and --rando-selftest green. -->
- [x] 5.7 Sidecar persistence: new additive extension block {present, attempt,
      digest24}, `kRandoSidecar_FileFormatVersion` bump, old-file
      compatibility, `RandoSave_SelfCheck` update. <!-- done 2026-07-05:
      format v5 adds a 24-byte extension tail carrying dungeon chain
      present/attempt/digest24, generation persists the accepted chain layout
      identity, and deserialization forces v1-v4 chain fields absent. The save
      selfcheck covers v5 layout bytes, round-trip, and v1-v4 compatibility.
      git diff --check, openspec validate dungeon-chains --strict, Release
      build, and --rando-selftest green. -->
- [x] 5.8 One-shot re-trigger audit: sweep every boss-room tag / prize grant /
      heart grant a chain re-traversal can re-reach (CLAUDE.md re-enabled
      one-shot corollary); gate any find on `Rando_IsLocationChecked`.
      <!-- done 2026-07-05: audited RoomTag_PrizeTriggerDoorDoor and
      RoomTag_GetHeartForPrize, both already gate on Rando_IsLocationChecked
      for the boss-prize location. Sprite_HeartContainer was the unguarded
      boss-heart grant site, so it now suppresses already-checked boss-heart
      locations before draw/grant and sets the boss-done bit defensively.
      git diff --check, openspec validate dungeon-chains --strict, Release
      build, and --rando-selftest green. -->

## 6. UI

- [x] 6.1 `rando_window.cpp`: `dungeon_chains` checkbox in the world-structure
      panel, normalization-grayed under conflicts; tooltip with 1–2 durable
      player-facts only.
      <!-- done 2026-07-05: added the checkbox beside structural shuffle
      controls, using Settings_EffectiveDungeonChains through a probe copy so
      the UI disables and normalizes the pending bit under the same conflicts as
      generation. Tooltip limited to chain-door and boss-pool player facts. git
      diff --check, openspec validate dungeon-chains --strict, Release build,
      and --rando-selftest green. -->

## 7. Verification and close-out

- [x] 7.1 Build matrix green: MSVC (worktree recipe with explicit
      /p:SolutionDir) + WSL gcc `-Werror` (`make clean` first); CI guard
      scripts pass (`check_no_embedded_data`, `check_placer_determinism`,
      corpus version sync).
      <!-- done 2026-07-05: MSVC Release build green with explicit
      /p:SolutionDir and OutDir=bin\x64-Release-dcverify\; WSL
      `make clean && make zelda3` green with default -O2 -Werror; guards green:
      check_no_embedded_data, check_placer_determinism --source-only,
      check_placer_determinism --binary=bin\x64-Release-dcverify\zelda3.exe,
      and check_corpus_version_sync. -->
- [x] 7.2 Corpus regen final confirmation at shipped `kGeneratorVersion`; 3-way
      diff vs fresh main: only chains entries new, all pre-existing digests
      byte-identical.
      <!-- done 2026-07-05: full corpus green at kGeneratorVersion 113
      (`python assets\scripts\run_rando_corpus.py
      --binary=bin\x64-Release-dcverify\zelda3.exe`, 141/141 entries OK).
      Refreshed origin/main, compared against local main integration baseline:
      main generator_version 112 with 136 entries; branch generator_version 113
      with 141 entries; added labels are exactly chains-open-ganon,
      chains-standard-fast-ganon, chains-prize-open-fast-ganon,
      chains-forced-keys-items, and chains-hunt-none; removed labels 0; existing
      settings/seed changes 0; existing placement/sphere digest changes 0. -->
- [ ] 7.3 Playtest matrix (merge gate; corpus is blind to both seams): chain-0
      boss door (pendant boss AND crystal boss); 3+ hop chain; DP as a chain
      element (side-door exit → ledge → back-door boss approach, then the aux
      round-trip still couples the later main-door exit to origin); TR as a
      chain element (balcony exit vanilla, Mimic Cave route intact); IP hole
      seam; TT→Blind and ToH→Moldorm pinned seams incl. Moldorm fall-out;
      death-continue mid-chain; S&Q + reload; mirror inside a hop; post-boss
      warp from a Mire-home terminal; cleared-terminal re-entry; SW
      fully-vanilla spot-check; sram_rando.dat diff on the slot path.
      <!-- owner playtest 2026-07-05 found Eastern chain-start -> Mire terminal
      boss spawned Link on the south shutter-door collision tile. F12 RAM showed
      synthetic entrance 140 at room 0x090, x=0x0078, y=0x13D8, with the live
      attr tile at relative y=472 equal to 0xF0. Fixed by making door-based
      synthetic boss entrances land at relative y=424 with doorway state cleared,
      plus a runtime override so existing local asset blobs are safe. Owner
      retest confirmed Link can fight Vitreous and death mid-fight respawns in
      a safe place instead of the shutter doorway.

      Owner playtest 2026-07-05 found ToH/Moldorm terminal falling after the boss
      item but before the visible pendant left Link invisible but movable. F12 RAM
      showed room 0x077, link_visibility_status=0x0C, link_item_flippers=1, so the
      terminal redirect reached the origin room with pit-fall visibility state still
      armed. Fixed by clearing pit-fall state in Chains_RequestTerminalExit, with
      runtime selfcheck coverage.

      Owner playtest 2026-07-05 found re-entering a cleared terminal boss room could
      seal Link inside with no boss/reward left. F12 RAM showed Mire boss room 0x090
      via synthetic entrance 0x8C with normal Link visibility/control state, so the
      issue was post-kill shutter containment rather than another player-state leak.
      Fixed by arming a one-shot cleared-terminal escape when the synthetic boss
      entrance loads an already-checked terminal, then consuming it at the top of the
      dungeon loop before shutters can trap the player. Runtime selfcheck now covers
      unchecked vs checked terminal re-entry.

      Owner playtest 2026-07-05 found ToH/Moldorm terminal falling after collecting
      the boss-heart replacement but before visibly receiving the pendant immediately
      exited and granted the pendant. A vanilla comparison F12 was not a clean prize
      reference because the cheat state already had link_which_pendants=0x07 and
      link_has_crystals=0x7F, but source review confirmed vanilla grants pendant/
      crystal bits on falling-prize receipt. Fixed rando boss prizes to dispatch at
      falling-prize receipt instead of spawn, so falling before pickup leaves the
      prize unchecked and preserves the ToH retry loop. Owner retest on the same
      seed confirmed the fix: falling after the boss-heart replacement but before
      touching the pendant now takes Link down a floor and leaves the pendant
      retry intact.

      Owner playtest 2026-07-05 covered a Swamp overworld-door chain into Mire,
      then PoD, then an Armos terminal. Confirmed PoD boss-door hop to Armos,
      mirror and S&Q from Armos both restore Armos, S&Q reaches the expected
      select screen, mirror in PoD restores PoD, death + save/continue in PoD
      restores PoD, and killing Armos exits to the Swamp entrance. PoD main-door
      exit reached overworld as expected for chain-origin exit behavior.

      Owner playtest also confirmed chain-0 boss-door behavior works. Re-entering
      the same cleared PoD direct-terminal door immediately returned to overworld;
      F12 during the kick-out showed player_is_indoors=0, dungeon_room=0x004A
      (PoD main exit room), which_entrance=0x88 (PoD/Helmasaur synthetic
      entrance), and the rando slot active. This matches the cleared-terminal
      escape added to avoid trapping Link in an already-cleared boss room.

      Owner playtest also covered DP entrance -> Swamp -> DP, with DP's outside
      entrances resolving to their expected locations and the DP boss door tested.
      This exercises the DP auxiliary/outside routing and boss-approach
      requirements while DP is a chain element. Follow-up playtest found vanilla
      small/big key modes could leave DP one physical key short after all three
      pot keys were spent; chains now normalize small and big keys to dungeon
      mode so the non-pot key is represented as a placed in-dungeon item.
      Follow-up playtest reached Desert Palace's back/boss section through a
      Dark World mirror route, then crossed DP's boss seam with no armed chain
      session. Runtime fell through to Lanmolas while the spoiler/logic expected
      DP's chain successor; chains now synthesize the matching DP aux exit room
      as the origin at that unarmed DP boss seam. -->
- [x] 7.4 Remove all bring-up diagnostics (g_ram counters) before merge.
      <!-- done 2026-07-05: removed the EP->DP spike hook, public
      ChainsRuntimeDebug API, Debug-tab dungeon-chain spike controls/readout,
      runtime debug counters/reasons, and normal-hop stderr traces. Kept the
      production seam hooks and selfcheck coverage. git diff --check, openspec
      validate dungeon-chains --strict, targeted debug-symbol rg, MSVC Release
      build, WSL make zelda3, and --rando-selftest green. -->
- [x] 7.5 Independent fresh-eyes review with a self-contained prompt (new
      findings only, response capped); fix and re-verify. <!-- done 2026-07-05:
      Kant found two concrete blockers: terminal outbound seams redirected
      Moldorm fall-outs before reward, and snapshots restored only chain layout
      identity, not the in-flight origin/terminal session. Fixed terminal
      containment to require the terminal prize location checked, extended
      type-7 snapshots with origin/terminal session bytes, and added
      RandoSnapshotTail_SelfCheck coverage for in-flight session restore.
      git diff --check, openspec validate dungeon-chains --strict, MSVC Release
      build, --rando-selftest, and WSL make zelda3 green. -->
- [ ] 7.6 Reconcile design.md + spec deltas against as-built source; update
      `docs/randomizer.md`; `openspec archive dungeon-chains --yes` on the
      branch; squash-merge. <!-- partial 2026-07-05: design.md, spec deltas,
      and docs/randomizer.md are reconciled to the as-built terminal reward gate,
      chain snapshot session restore, settings UI, sidecar/snapshot persistence,
      and corpus/version evidence. Archive and squash-merge remain gated on 7.3
      owner playtest. -->
