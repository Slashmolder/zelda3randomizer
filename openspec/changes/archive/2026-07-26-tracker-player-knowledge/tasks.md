# Tasks: tracker-player-knowledge

## 1. Phase 0 — audit closure (gates everything else)

- [x] 1.1 Re-verify design.md's audit matrix (producers, consumers, and the
      recorded in-game sweep verdicts) against CURRENT source at implementation
      start — the initial sweeps were run at proposal time and rows may have
      drifted; any new LEAK row gets a fix bucket or an owner-confirmed
      accepted-rationale BEFORE Phase 1 lands
- [x] 1.2 Re-run the producer sweep (every `RandoSettings` axis × every logic-store
      installer) against the final matrix; record the affirmative non-topology
      classification for absent axes in design.md
- [x] 1.3 Reconcile spec deltas against the closed matrix (claim-grounding pass)

## 2. Discovery state + persistence (randomizer-save v13)

- [x] 2.1 Define the v13 slot-extension discovery block (dungeons u16, cave
      interior bits, whirlpool-pair u8, reserved door bits, spare) + size
      constants; serializer + deserializer + snapshot-tail TLV in ONE commit
- [x] 2.2 Backfill on load for v12-and-older sidecars and TLV-less snapshots:
      checked-location → dungeon (via `RandoRegionDef.dungeon_id`) and → cave
      interior; never from placement/settings/layout
- [x] 2.3 Runtime discovery marking: per-frame dungeon observation (module +
      `cur_palace_index_x2`, HC-proper folded to HCE); persist-upgrade the
      existing cave entrance-discovery mark; whirlpool pair mark at the ride
      hook; every new bit bumps the reachability counter
- [x] 2.4 Extend the sidecar round-trip selfchecks to the new block; snapshot
      cold-replay (M4) discovery restore covered

## 3. Knowledge-limited flood (randomizer-logic)

- [x] 3.1 Add the optional knowledge-exclusion input to the reachability compute
      (excluded region never enters the fixpoint; suppressed warp edges never
      fire); explicit parameter, no process-global state, default = full
      knowledge
- [x] 3.2 Derive the hidden-identity set from the ACTIVE layout tables
      (entrance dun_assign incl. GT opt-in; chains pool); identity-mapped
      assignments stay hidden
- [x] 3.3 Rewire `Rando_GetLiveReachability()` as the knowledge-limited view;
      fold the discovery generation counter into the memo key; rename the
      full-knowledge path to its explicit internal name and update generation/
      selftest callers
- [x] 3.4 Selftest group: masked ⊆ full; excluded-dungeon locations absent;
      set-bit → included after memo invalidation; generation flood after a
      masked live flood is unaffected; empty-mask short-circuit byte-identical

## 4. Tracker window display (randomizer-native-window)

- [x] 4.1 Check Tracker: "(unexplored)" marker on hidden-identity dungeon region
      headers + "N dungeons not yet entered" summary line
- [x] 4.2 Verify cave contents render grey pre-discovery and light up on entry
      across all three surfaces (no display code needed — the flood carries it;
      this task is the seed-independence check from the native-window scenario)
- [x] 4.3 (dissolved by D7 correction — names/grouping are static and never
      leaked; no search hardening required. Kept as a record, nothing to do)
- [x] 4.4 Auto-tracker: `reachable` inherits the knowledge-limited bridge
      (no call-site change expected); restate the spoiler-safety comment to the
      invariant; verify `discovered_connections`/`discovered_doors` unchanged

## 5. Guards

- [x] 5.1 `assets/scripts/check_knowledge_consumers.py` (allowlist + 
      `knowledge-guard: allow <reason>` escape) wired into rando CI
- [x] 5.2 CLAUDE.md: add this bug class ("full-knowledge state surfaced to
      player-facing surfaces") next to the vanilla-proxy class, with the
      one-line invariant and the guard script pointer

## 6. Validation (before any archive)

- [x] 6.1 MSVC + WSL gcc `-Werror` builds clean; `run_rando_validation.py quick`
      during iteration, `ci` source-phase guards green, and the `full`
      pre-review profile PASS (needs dumpbin on PATH; see build-commands memory)
- [x] 6.2 Corpus regen A/B against unmodified main built fresh in the same env
      (`rm src/rando/logic_data.c` first): byte-identical manifests required —
      the proof the placer path is mask-free; no `kGeneratorVersion` bump
- [x] 6.3 Fresh-eyes independent audit of the final diff against the D1
      invariant (prompt: find any surface statement not true under all
      assignments consistent with player observations); new findings only
- [ ] 6.4 Owner playtest matrix: dungeon-entrance seed + chains seed sphere-0
      dark (incl. the Desert-ledge pass-through probe); first entry lights up
      live; save/reload + snapshot cold-replay retain discovery; cave seed
      contents grey→lit on entry; whirlpool seed hides destination side until
      ridden; race seed unchanged except topology-silent; no-topology seed
      pixel-identical

## 7. FINAL phase — door-shuffle knowledge gating (deferred; owner re-confirms scope first)

- [ ] 7.1 OWNER GATE: re-confirm scope and UX ("avail" = reachable via mapped
      doors) before any implementation
- [ ] 7.2 Persist `DoorRt` discovery into the reserved v13 bits; gate door-oracle
      edges in the live flood on discovery
- [ ] 7.3 Selftest + playtest for the door view; then re-run 6.2/6.3
