# Tasks: add-rando-ow-warp-shuffle

Phases are ordered so every later phase has a validated substrate under it.
Phase 1 gates everything: no ids, caps, or spec numbers are final until the
generator reports real counts (terrain-feature precedent).

## 1. Upstream graph extraction (gates all sizing)

- [x] 1.1 `assets/scripts/gen_ow_graph_tables.py`: parse
      `OverworldShuffleDev` via `git show` from the sibling checkout
      (`--upstream` path flag, `--ref` defaulting to
      `origin/OverworldShuffleDev`): screens, NAME-KEYED `OWTileRegions`
      components (no geometry exists or is needed), `one_way_ledges` as
      DIRECTED source→landing component drop edges, vanilla inter-screen
      component adjacency, the LW↔DW overworld TELEPORTER hosting
      components (predicates come from the FORK's hand-written teleporter
      zone edges via codegen cross-reference, NOT from upstream),
      `default_whirlpool_connections`, `flute_data` candidates incl. the
      FORCED (Desert/Mire guarantee) entries AND their per-candidate
      arrival-tableau columns (icon columns extracted only as a
      relative-placement cross-check — blips are DERIVED, upstream icon
      space ≠ fork map space); REPRODUCE (not extract) the
      sector partition as a topological flood over vanilla component
      adjacency (no-starting-Flippers branch — water components excluded).
- [x] 1.2 Emit gitignored `assets/rando/ow_graph.gen.yaml` + an FNV identity
      digest (activation-guard input, terrain pattern).
- [x] 1.3 Generator self-asserts (D2 list: 8+8 counts, LW/DW whirlpool split,
      coverage, component-name uniqueness, symmetry-except-drop-edges, every
      flute candidate names a component of its screen — walk-in-less spots
      allowed and flagged reachability-critical with portal edges present —
      pseudo-screens excluded).
- [x] 1.4 Report counts to owner (components, adjacency edges, flute
      candidates) BEFORE binding region ids — capacity check vs the 512 cap.
- [x] 1.5 `setup_worktree.py`: mirror `ow_graph.gen.yaml`.

## 2. Logic substrate

- [x] 2.1 `rando_logic_gen.py`: load `ow_graph.gen.yaml` as a new LAST-loaded
      region group (after `logic_parts/inverted/**`); emit component regions
      + membership edges, DIRECTED drop edges (source→landing component),
      adjacency edges, and PORTAL edges (predicate reused from the fork's
      matching teleporter zone edge); components form a contiguous
      id-suffix.
- [x] 2.2 Raise `kReachabilityMaxRegions` 256 → 512 (rando_logic.c): resize
      `region_bitset`, `g_inverted_pair_set`, audit the hardcoded expansion
      count + every `kReachabilityMaxRegions` use; C static assert tying
      generated region count (+ door dynamic-region ceiling) to the cap;
      `make clean` note (Makefile has no header deps).
- [x] 2.3 Codegen asserts (D3): region budget ≤ `kReachabilityMaxRegions`
      with printed figures; entrance-edge-override-eligible ids < 64; pinned
      name→id snapshot of ALL pre-existing regions from the generated table.
- [x] 2.4 Quotient cross-check (D1): component adjacency collapsed by zone
      membership vs hand-written zone walk edges, both directions; committed
      sorted allowlist whose entries NAME the carrying predicate/location
      and are validated to resolve; hard-fail otherwise.
- [x] 2.5 Reachability inertness: expansion skips the component suffix when
      no OW axis active (terrain fast-path pattern);
      `Rando_OwWarpActive()`-style predicate lives in one place.
- [x] 2.6 Static hub wiring: `OW_FluteNet` region + `zone → hub` edges for
      LW surface zones using a NEW macro whose body is `CanFly`'s full
      current body verbatim (possession + activation — CanFly has no
      destination term to strip; destinations are emergent from its edge
      references), with the Standard rescue-transitivity comment (D5);
      data, not per-seed.
- [x] 2.7 `CanFly(world)` neutralization: NEW settings-reading predicate
      opcode (reads NORMALIZED flute_shuffle) registered across all five
      op surfaces — op_registry.yaml, rando_logic.h op enum,
      rando_logic.c operand/skip/dispatch/eval, rando_logic_gen.py parser —
      plus a Settings_SelfCheck/selftest vector; CanFly gains
      `AND NOT <op>` (nothing else changes); selftest proves — by
      INSTALLING a per-seed hub→spot overlay layout via
      `Rando_AddEntranceEdge` before the differential reachability run
      (the existing settings-differential selftest pattern varies inventory
      only, so the edge-install step is new) — that a vanilla flute-gated
      region is not flute-reachable under a shuffle that moved its spot,
      and is reachable when a spot lands there; axis-off compilation
      byte-identical.
- [x] 2.8 WSL `make clean` + corpus spot-run: existing entries byte-identical
      with axes absent from settings (substrate-inert proof, pre-axes).

## 3. Settings axes

- [x] 3.1 `RandoSettings` fields + defaults; canonical [30] bit 3 whirlpool,
      bits 4-5 flute; deserialize refuses undefined bits; relocate the
      `Settings_SelfCheck` [30] undefined-bit probe from bit 3 to bit 6.
- [x] 3.2 `apply_derived_rules`: both axes → 0 under Inverted — condition is
      `world_state == Inverted` ONLY (do NOT copy the entrance axes'
      "not Open/Standard" guard, which would wrongly kill Retro warps).
- [x] 3.2b Customizer composition (D5): axes apply under customizer; warp
      pinning not honored (documented); no normalization needed.
- [x] 3.3 Fail-closed: generation preflight refuses axis-on without graph
      data; `Placement_SelfCheck` prong for the empty-graph refusal.
- [x] 3.4 Selftest: canonical round-trip vectors for the new bits; settings
      window ↔ share-string ↔ hash stability at defaults.

## 4. Generation

- [x] 4.1 `src/rando/shuffle_ow_warp.{c,h}`: derivation from (seed, attempt,
      salt) on the rando xoshiro stream; port upstream flute selection
      semantics for BOTH modes (sector distribution + FORCED Desert/Mire
      entries + balanced ignore-set incl. its rare escape); whirlpool LW
      perfect matching; pure functions.
- [x] 4.2 Per-attempt wiring in BOTH generators (`rando_generate.c` +
      headless `main.c` path): regenerate layout per attempt; reset any
      per-attempt state at attempt top (entrance audit lesson);
      acceptance via the standard tier-scoped Accessibility_SeedAcceptable
      gate, evaluated against the installed warp layout, when any OW axis
      on.
- [x] 4.3 Per-seed edge injection through the existing overlay (8 spot
      edges + ≤12 whirlpool edges; hub feeders are static data, no overlay
      budget); raise `kEntranceAddedEdgeMax` 64 → 128: decoupled (40) +
      warp (~20) = 60 alone leaves ≤4 slots for cross-mode edges and any
      future consumer, and overflow today drops SILENTLY; selfcheck asserts
      the combined all-consumer injected count < cap (loud, not silent),
      incl. a decoupled+cross+both-warp-axes composition vector.
- [x] 4.4 `ow_digest24` fold; `g_generate_profile` counters for warp
      attempts/rejections.
- [x] 4.5 Spoiler `ow_warps` section — emitted ONLY when ≥1 warp axis is
      active (axes-off spoiler stays byte-identical); race-gated;
      `--reveal-spoiler` covered; whirlpool pairs in whirlpool-id space.
- [x] 4.6 Determinism vector in `--rando-selftest` (fixed seed → fixed
      layout bytes, both axes).

## 5. Sidecar & snapshot

- [x] 5.1 Slot extension: `ow_attempt` (ext @58) + `ow_digest24` (ext
      @59-61); ext size 58 → 62; `format_version` 11 → 12 (confirm 12
      unused — history skips numbers); both-direction gating; serializer +
      deserializer + `RandoSave_SelfCheck`.
- [x] 5.2 Activation: regenerate layout from (seed, settings, ow_attempt);
      recompute digest; HARD-FAIL mismatch or empty graph (D7); install
      per-seed edges + runtime tables.
- [x] 5.3 Snapshot cold-replay TLV: carry the two fields in the existing
      settings-restore TLV payload (count-stable extension, terrain trick);
      cold replay reconstructs or refuses.
- [x] 5.4 Deactivation teardown symmetric (no warp state leaks into vanilla
      or the next slot).

## 6. Runtime

- [x] 6.1 `Rando_OwWarp_FluteSpot(k)` resolver (D4: serves the PORTED
      flute_data arrival tableau + a DERIVED map-space blip x/y); selftest
      oracles: vanilla 8 ported rows == `kBirdTravel_*` asset rows
      byte-equal AND vanilla 8 derived blips == the `messaging.c` const
      blip tables (`kBirdTravel_x_lo/x_hi/y_lo/y_hi`).
- [x] 6.2 Hook `FluteMenu_LoadTransport` (destination install) — axis+slot
      gated, vanilla path untouched.
- [x] 6.3 Flute map UI (`Module0E_0A_FluteMenu` family, `messaging.c`): blip
      positions (fed from the resolver instead of the hardcoded
      `kBirdTravel_x_lo/...` const tables when the axis is active,
      via the `WorldMap_AddSprite` call in `FluteMenu_HandleSelection`),
      cursor cycling order, and selection→slot mapping follow the per-seed
      spots.
- [x] 6.4 Whirlpool partner remap hook at `FindPartnerWhirlpoolExit`
      (index π before the +9 row load) — axis+slot gated.
- [x] 6.5 Audit `rando_map.c` (never-audited) for vanilla flute-blip
      assumptions; fix or file.
- [x] 6.6 F12 dump lines for active warp layout (spot list + pairs) — the
      runtime-debugging convention.

## 7. UI & docs

- [x] 7.1 Native window: "Overworld" group with the two controls
      (EnumCombo flute off/balanced/random; checkbox whirlpool); HelpTooltip
      text follows the 1-2 durable-player-facts rule; Inverted normalization
      reflected (controls disabled with reason under Inverted).
- [x] 7.2 `docs/randomizer.md` section (axes, semantics, DW-pair honesty,
      race behavior); share-string doc row for [30] bits.
- [x] 7.3 Register new sources in `zelda3.vcxproj` + Makefile + Switch
      makefile; codegen-wiring CI guard list updated.

## 8. Validation & close-out

- [x] 8.1 WSL `make clean` gcc `-Werror` + MSVC Release x64 (isolated OutDir
      + `/p:SolutionDir=<wt>\` — worktree build recipe), both green;
      selftests green on BOTH binaries.
- [x] 8.2 Corpus: 3-way diff ritual — this branch fresh vs main fresh, `rm
      src/rando/logic_data.c` both sides, absolute `--binary`, CRLF restore;
      existing entries byte-identical; add the 6 new entries (D9, incl. the
      customizer+warp composition); single `kGeneratorVersion` bump,
      re-grepped against LIVE main at commit time.
- [x] 8.3 CI guards all green (`check_no_embedded_data`,
      `check_placer_determinism`, `check_corpus_version_sync`,
      codegen-wiring).
- [x] 8.4 Playtest matrix (owner): D9 item 6 list; confirm axes-off vanilla
      behavior byte-identical in RAM-compare side-by-side run.
- [x] 8.5 Independent fresh-eyes review (self-contained prompt, new-findings
      focus); fix; repeat per saturation model.
- [x] 8.6 Reconcile spec deltas against as-built source; `openspec archive
      add-rando-ow-warp-shuffle --yes` as the last branch commit;
      squash-merge.
