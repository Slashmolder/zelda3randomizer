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
      chains + prize_shuffle, chains + wild dungeon keys — locks the
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
      installs a runtime layout. Release build + --rando-selftest green. -->
- [x] 5.4 Terminal containment: outbound transitions from a terminal boss room
      divert to the chain exit (origin door); verify cleared-terminal re-entry
      is exitable. <!-- done 2026-07-05: door/stair/hole outbound seams match
      kChainBossOutboundSeams while terminal state is armed; terminal exits route
      through the normal module-15 -> module-8 overworld loader using chains-owned
      origin_exit_room. Release build + --rando-selftest green; end-to-end
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
      gate, and snapshot type-7 carries chain attempt/digest for cold replay.
      RandoSnapshotTail_SelfCheck covers restore, re-save perpetuation, missing
      ChainLayout fail-closed, and bad digest fail-closed with a synthetic asset
      fixture. git diff --check, openspec validate dungeon-chains --strict,
      Release build, and --rando-selftest green. -->
- [x] 5.7 Sidecar persistence: new additive extension block {present, attempt,
      digest24}, `kRandoSidecar_FileFormatVersion` bump, old-file
      compatibility, `RandoSave_SelfCheck` update. <!-- done 2026-07-05:
      format v5 adds a 24-byte extension tail carrying dungeon chain
      present/attempt/digest24, generation persists the accepted chain layout
      identity, and deserialization forces v1-v4 chain fields absent. The save
      selfcheck covers v5 layout bytes, round-trip, and v1-v4 compatibility.
      git diff --check, openspec validate dungeon-chains --strict, Release
      build, and --rando-selftest green. -->
- [ ] 5.8 One-shot re-trigger audit: sweep every boss-room tag / prize grant /
      heart grant a chain re-traversal can re-reach (CLAUDE.md re-enabled
      one-shot corollary); gate any find on `Rando_IsLocationChecked`.

## 6. UI

- [ ] 6.1 `rando_window.cpp`: `dungeon_chains` checkbox in the world-structure
      panel, normalization-grayed under conflicts; tooltip with 1–2 durable
      player-facts only.

## 7. Verification and close-out

- [ ] 7.1 Build matrix green: MSVC (worktree recipe with explicit
      /p:SolutionDir) + WSL gcc `-Werror` (`make clean` first); CI guard
      scripts pass (`check_no_embedded_data`, `check_placer_determinism`,
      corpus version sync).
- [ ] 7.2 Corpus regen final confirmation at shipped `kGeneratorVersion`; 3-way
      diff vs fresh main: only chains entries new, all pre-existing digests
      byte-identical.
- [ ] 7.3 Playtest matrix (merge gate; corpus is blind to both seams): chain-0
      boss door (pendant boss AND crystal boss); 3+ hop chain; DP as a chain
      element (side-door exit → ledge → back-door boss approach, then the aux
      round-trip still couples the later main-door exit to origin); TR as a
      chain element (balcony exit vanilla, Mimic Cave route intact); IP hole
      seam; TT→Blind and ToH→Moldorm pinned seams incl. Moldorm fall-out;
      death-continue mid-chain; S&Q + reload; mirror inside a hop; post-boss
      warp from a Mire-home terminal; cleared-terminal re-entry; SW
      fully-vanilla spot-check; sram_rando.dat diff on the slot path.
- [ ] 7.4 Remove all bring-up diagnostics (g_ram counters) before merge.
- [ ] 7.5 Independent fresh-eyes review with a self-contained prompt (new
      findings only, response capped); fix and re-verify.
- [ ] 7.6 Reconcile design.md + spec deltas against as-built source; update
      `docs/randomizer.md`; `openspec archive dungeon-chains --yes` on the
      branch; squash-merge.
