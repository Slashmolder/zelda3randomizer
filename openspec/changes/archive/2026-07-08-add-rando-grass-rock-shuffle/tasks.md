# Tasks: add-rando-grass-rock-shuffle

Phases match design.md's Migration Plan. Every phase ends buildable (MSVC +
WSL gcc `-Werror`) with the corpus green at the then-current
`kGeneratorVersion`. Reminder traps that apply throughout: `make clean` after
ANY header edit (no header-dep tracking in the Makefile); register every new
`.c` in `zelda3.vcxproj` (lowercase tracked name); read dumps/logs via
absolute worktree paths.

## 1. Measure (dump tool — no ID/capacity decisions before this lands)

- [x] 1.1 Add `--dump-terrain-table` CLI mode: `LoadAssets`, headless-load
      each overworld area `0x00..0x7F` (tilemap/attr build only — mirror the
      `Dungeon_DrawRoomObjectsHeadless` "replay only what's needed" approach;
      full frame paths crash headless), for each relevant world/event state.
      (As-built: `Overworld_DumpTerrainTable` in overworld.c, 5 state
      profiles — vanilla / +events / inverted / inverted+events /
      pre-rescue; the event-overlay switch's assert(0) screens are skipped,
      keep-in-sync note in place.)
- [x] 1.2 Emit per map16 cell with a liftable/cuttable class: screen, pos,
      map16 id, attr class (map16 → map8 → `kMap8DataToTileAttr`), glove
      level, joined `kOverworldSecrets` code (if any). Also emit per-screen
      secret entries NOT matched to an object (reconciliation input for
      1.4). (A/S/T/U line format, header-documented in the dump.)
- [x] 1.3 Verify the engine's reveal-helper map16 set is exactly covered —
      RESULT: 0 unknown-liftable (U) lines across all profiles; the id sets
      are complete. Ground truth (dump attrs + `kTile50data` +
      `kGetBestActionToPerformOnTile_a`): `0x20f`/`0x239` are
      LIGHT(Glove)/HEAVY(Mitt) small rocks (not LW/DW variants), `0x36d`
      pile = Glove, `0x23b` pile = Mitt, both bush ids bare-handed, sign
      attr `0x54` = bare, bonk pile attr `0x57` dash-only.
- [x] 1.4 Reconcile dump vs. extracted secrets — RESULT: 351 engine secret
      entries; 327 on consumable objects, 8 structural off-object, 16 item
      secrets on non-consumable tiles (dig-spot class; stay vanilla). Zero
      unexplained.
- [x] 1.5 Underworld: no third reveal path exists — indoor liftables route
      exclusively through `Dungeon_LiftAndReplaceLiftable` →
      `RevealPotItem` (pot-sanity's hooked domain; replacement-state
      machinery), and `Overworld_RevealSecret` is unreachable indoors. No
      underworld bush/rock secret path to cover.
- [x] 1.6 OWNER GATE — counts reported (registrable after the ORIGINAL
      present-in-every-state intersection: 3943 total = grass 3687 [bushes
      1404 + thick grass 2283] + rock 256 [light 196 / heavy 60]); owner
      locked full scope + capacity 8192. **SUPERSEDED by the world-state
      grouping fix (see 7.4): the all-profile intersection wrongly required
      presence across the Inverted boundary; the as-built per-world-state-group
      rule registers 3981 (3943 universal + 28 non-inverted-only + 10
      inverted-only).** CORRECTION during Phase-2
      validation: the "enemy block tops at id 1999" reading was a grep bug
      (`id: 1[0-9]*` cannot match ids ≥ 2000) — the enemy-check block really
      runs 1400..2633, and the originally-locked base 2048 COLLIDED with
      live enemy-check ids (corpus caught it: enemy-drop-all digests moved;
      duplicate ids corrupt every id-keyed consumer). As-built base = 3072
      (terrain 3072..7014 < 8192, ~438 ids of enemy-family headroom), and
      rando_logic_gen.py now HARD-FAILS on any duplicate location id.

## 2. Registry + logic codegen

- [x] 2.1 `gen_terrain_tables.py --emit` → gitignored
      `assets/rando/terrain.gen.yaml` DONE: 3981 rows (3943 universal + 28
      `[open,standard,retro]` + 10 `[inverted]`; originally 3943 under the
      all-profile intersection — see 7.4), ids 3072..7052 by
      `(screen,pos)` sort rank (base corrected from 2048 — see 1.6), class /
      axis / secret code / region / can_reach / world_state_filter per row, all
      D2/D3/D4 invariants asserted (per-world-state-group presence, structural +
      sign + bonk exclusions, unbound-screen hard-fail). The activation
      digest is computed by rando_logic_gen's emit_terrain_lookup (FNV over
      sorted (screen,pos,id), mirroring the pot digest), emitted as
      `kRandoTerrainRegistryDigest/Count` in terrain_lookup.h.
- [x] 2.2 Region binding DONE (mechanism + review round 1): complete
      80-parent `SCREEN_REGIONS` map + committed
      `terrain_logic_overrides.yaml` with ALL 19 worklist screens resolved
      (worklist now emits 0). Grounded in object-position analysis + ALTTPR
      region PHP: 0x05 area rebound to LW_DME (the 2x2 IS East DM); 0x4A →
      DW_NW (Bumper entrance pile); 0x68 AND 0x69 → DarkWorld_South (the DW
      fence-line band — the LW-mirror NW derivation was wrong, caught by
      the Digging Game grounding); desert 0x30 pos-range gates
      (Checkerboard rocks = Mirror+Mire-reachable, forecourt + west-edge
      bushes = Book OR Mirror+Mire per ALTTPR South.php:150-163); TR 0x47
      plateau diamond = Hammer+Mitt (left rock wall stays plain DME+class);
      smith 0x62 rows≥17 = Mitt. PLAYTEST-flagged residuals (all over-gated,
      safe direction): desert west-edge bushes, 0x62 south walk-in, 0x30
      range boundaries.
- [x] 2.3 Predicates DONE: `CanLiftRocks()`/`CanLiftDarkRocks()` wired per
      rock class; NEW macros in macros.yaml — `CanCutGrass()` =
      `HasSword(1) OR CanBombThings()` (cutter set re-verified: hammer
      branch = pegs only, powder-in-hand skips the 0x37e case),
      `TerrainWorldGateLW/DW()` world-state-aware bunny gates (single entry
      serves standard+inverted).
- [x] 2.4 Codegen DONE: `load_terrain()` + merge (mirrors load_pots),
      `emit_terrain_lookup()` → sorted terrain_lookup.h + digest/count,
      `_location_type_id` + logic.schema.yaml + rando_logic.h enum gain
      Grass=20/Rock=21, Makefile `RANDO_GEN_OUTPUTS` + `.gitignore` +
      `zelda3.vcxproj` RandoCodegenInput/Output wired. NEW registry-wide
      duplicate-location-id hard-fail in rando_logic_gen (the base-2048
      collision lesson). Phase-2 inertness: `terrain_active()` stub (always
      false) + guards at the skip-triple (open-loc loop, junk-pad target,
      Placement_SelfCheck) + the reachability expansion skips inert terrain
      rows (a door-shuffle corpus entry crossed the 120 s timeout without
      it). Tracker/auto-tracker/spoiler/customizer/hints type-table entries
      = Phase 5 with the rest of the UI work.
- [x] 2.5 `setup_worktree.py` DONE: TERRAIN_ARTIFACT_RELS complete-set
      mirroring (dump + gen.yaml) with a loud fail-closed warning.
- [x] 2.6 Capacity bump DONE (owner locked base 2048 + 8192 at gate 1.6):
      `kRandoLocationCapacity` 4096→8192 (rando_logic.h) + the codegen
      assert in rando_logic_gen.py in lockstep. Literal-array audit came
      back CLEAN (every `[512]`/`[1024]` hit is a char path/string buffer or
      the unrelated door pool — pot/enemy work already unified location
      arrays onto the constant). NEW `Rando_SelfCheckCapacityABI`
      (--rando-selftest, runs first): 10 per-TU capacity probes
      (`RANDO_DEFINE_CAPACITY_PROBE`, incl. the two extern-"C" UI TUs) catch
      the Makefile no-header-deps mixed-ABI hazard loudly. WSL `make clean`
      rebuild green; MSVC verify pending with the next MSBuild pass.

## 3. Settings + placer

- [x] 3.1 `RandoSettings` DONE: `grass_shuffle`/`rock_shuffle` fields +
      shared `TerrainShuffle` enum (off/junk/all) + axis bit constants;
      `parse_terrain_shuffle` CLI grammar (`grass_shuffle=junk|all`) + two
      KEY_ enum entries; defaults off; deserialize rejects value 3 + any set
      bit above the two fields.
- [x] 3.2 Canonical byte [29] APPENDED DONE (grass bits 0-1, rock bits 2-3);
      `kSettingsCanonicalLen` 29→30 with ALL coupled sites updated:
      serialize/deserialize, the re-pinned default-canonical bytes + SHA-256
      reference hash, the `settings_canonical` `_Static_assert`, the
      offset-table doc comment, AND the ZRSR suppressed-spoiler block
      (`kRandoSuppressedSpoilerSettingsLen` 29→30, size 139→140, CRC offset
      135→136, its two `_Static_assert`s, and the `{bump,run}_rando_corpus.py`
      ZRSR constants). Empirically confirmed placement-neutral: a kGen-130
      binary matched all 177 non-drift entries' kGen-129 digests.
- [x] 3.3 Placer DONE: `terrain_active(loc, s)` (axis non-off) at the
      skip-triple; `terrain_junk_only(loc, s)` new junk-only class excluded
      from BOTH the reachable-candidate loop and the two forward-fill
      fallbacks; `Placement_SelfCheck` terrain block asserts default-inert,
      per-axis activation counts, junk-only coverage, AND a junk-tier fill
      with `is_progression_item` verifying no progression on a junk-only
      slot.
- [x] 3.4 `kGeneratorVersion` 129→130 DONE (share-string version static
      assert still holds — 130 < 255).
- [x] 3.5 Spoiler settings JSON emits `grass_shuffle`/`rock_shuffle` from
      canon[29] (the 91a2aef lesson). Hints: junk auto-excluded via
      `item_is_junk`; progression under terrain (all tier) stays hintable —
      terrain LOCTYPEs are not junk-typed. Customizer accepts terrain
      location ids (no by-value exclusion; vanilla_item=Nothing pin is
      pot-type-scoped). NOTE: sidecar v8 (format_version 7→8, ext 37→44 +
      settings blob 29→30) + the terrain registry activation guard
      (`Rando_*TerrainRegistry*` mirroring the pot guard) also landed here.

## 4. Runtime

- [x] 4.1 `Rando_TerrainRevealHook(screen, pos)` DONE — invoked from the
      FOUR consuming call sites (`Overworld_LiftingSmallObj`,
      `SmashRockPile_fromLift` [keyed on the reassigned pile-origin pos], the
      `check_secret` label of `Overworld_ToolAndTileInteraction` [excludes the
      shovel/dig `yv==0xdc9` path], the bush/grass consume branch of
      `Overworld_BombTile` [NOT `label_a`]) — NOT inside
      `Overworld_RevealSecret`. Suppress = skip RevealSecret + set
      `dung_secrets_unk1 = 0xFF` (every downstream
      `Sprite_SpawnThrowableTerrain`/`SmashedTerrain` reads 255 as no-spawn;
      a ZERO would trigger `Overworld_SubstituteAlternateSecret`). Checked →
      vanilla replay (no one-shot suppression — no shared key byte to dup).
      Inert without `kFeatures1_RandomizerActive` (non-rando byte-identical).
- [x] 4.2 Quiet-receive REUSED: the terrain hook calls the existing
      `Rando_PotQuietReceive(lttp, item)` directly (already generic — takes a
      code + item, keeps the no-free-ancilla retry + shared icon resolver).
      Multi-grant-per-frame (bomb 3x3) robustness is a PLAYTEST item (7.1).
- [x] 4.3 Sidecar v8 DONE (landed with Phase 3): format_version 7→8, ext
      37→44 with `terrain_registry_digest`/`count`/`present` @37-43, settings
      blob 29→30 (`LegacySettingsCanonicalLen29` for v4..v7 read), activation
      guard `Rando_*TerrainRegistry*` mirroring the pot guard; save selfcheck
      v4-compat block fixed to construct the 29-byte legacy blob.
- [x] 4.4 `--rando-selftest` DONE: terrain lookup strict-sortedness +
      round-trip + non-object→0xFFFF + registry-count self-agreement (empty
      registry caught loudly); junk-only placement invariant (Phase 3's
      `is_progression_item` check in Placement_SelfCheck).
- [x] 4.5 Snapshot path: checked-bitmap + placement table already size from
      `kRandoLocationCapacity` (Phase 2.6 audit found no literal-`[1024]`
      survivors); no RandoSnapshotTail change needed — the M4 cold-replay
      selfcheck already exercises the capacity-sized buffers.

## 5. UI / reporting

- [x] 5.1 Native settings window DONE: "Grass shuffle" / "Rock shuffle"
      EnumCombos (`kTerrainShuffleLabels` off/junk/all), 1-2-fact tooltips,
      no disable coupling.
- [x] 5.2 SNES HUD grid DONE: skips `LOCTYPE_Grass`/`LOCTYPE_Rock` before
      the row/col bookkeeping (with `LOCTYPE_Pot`).
- [x] 5.3 Check tracker + reach panel DONE: session/persisted "Show terrain"
      toggle (default off, only when terrain locs exist), counts include
      terrain, filter-aware "+N terrain checks in this region" note, map-
      tracker hover tooltip caps terrain to a "+N terrain checks" line. New
      `check_tracker_show_terrain` pref persisted via config.c/.h.
- [x] 5.4 Auto-tracker DONE: `,"grass":true` / `,"rock":true` catalog tags.
- [x] 5.5 Spoiler: terrain placements emit as ordinary rows (no ITEM_Nothing
      pins → no empty-row class to filter); settings JSON emits both axes
      (Phase 3). In-window row handling verified via the grass+rock-all
      corpus entry generation.
- [x] 5.6 `docs/randomizer.md` DONE: settings-table rows + a "Grass & rock
      shuffle" section (axes, tiers, glove gating, vanilla-replay farming,
      structural exclusions, compose-with-everything, Show-terrain toggle,
      fail-closed).

## 6. Validation

- [x] 6.1 Corpus entries DONE: 11 rows added (terrain-grass-junk/-all,
      terrain-rock-junk/-all, terrain-grass-rock-all, terrain-retro,
      terrain-inverted, terrain-door-basic-composes,
      terrain-cave-entrance-composes, terrain-enemy-drop, terrain-pot-shuffle,
      terrain-hunt-all-none) — all green.
- [x] 6.2 `bump_rando_corpus.py --apply` DONE (kGen 129→130, 14 digests):
      empirically proven placement-neutral — a kGen-130 binary matched ALL
      177 pre-existing placement digests against the kGen-129 manifest (the
      placer seeds from seed_u64, not settings_hash), so only the 11 new
      terrain rows + the 2 pre-existing souls-drift rows moved. Full 3-way
      diff vs. a fresh-built unmodified main is the remaining MSVC-adjacent
      confirmation (WSL side done).
- [x] 6.3 CI DONE: `--skip-terrain-shuffle` corpus flag +
      `entry_uses_terrain_shuffle` (mirrors `--skip-pot-shuffle` for
      registry-absent public CI); all guards green (embedded-data,
      placer-determinism static+3-run canary, corpus-version-sync=130,
      audit-guard, codegen-wiring=11, generator-version, rando-invariants,
      byte-order, logic-overrides).
- [~] 6.4 WSL gcc `-Werror` from `make clean` GREEN + all selftests green;
      MSVC verify is owner-gated (worktree MSBuild recipe exists) — remaining.

## 7. Playtest + review + archive (owner-gated)

- [ ] 7.1 Playtest matrix (the load-bearing net — the grant path is
      corpus-blind): bush lift / bush cut / bush bomb / bush powder /
      thick-grass cut / small light rock / small dark rock / big pile both
      strengths / dash-smash pile; clustered bomb consuming several bushes
      at once (no grant lost); bomb ADJACENT to a registered rock grants
      nothing (speculative-probe non-consume); super-bomb walk past
      registered objects grants nothing; checked fairy-bush vanilla replay;
      structural portal/stairs rock stays vanilla; swordless + grass-all;
      cave-entrance shuffle + terrain-all; inverted DW and LW sides; each
      `terrain_logic_overrides.yaml` split screen; junk vs all tier grants;
      sidecar guard refusal (empty-registry binary + terrain slot).
- [~] 7.2 Independent fresh-eyes review (self-contained prompt, new findings
      only, capped response) after the surface is complete — per the audit
      cadence this WILL find bugs; budget a fix round. (Two rounds done pre-F12;
      a review of the 7.4 world-state grouping fix is the last outstanding pass.)
- [ ] 7.3 Reconcile spec deltas against as-built source (deltas rot; validate
      checks structure, not truth), then `openspec archive
      add-rando-grass-rock-shuffle --yes` as the last branch commit;
      squash-merge to main. (design D4 + sanity registry requirement + tasks 1.6/2.1
      reconciled to the per-world-state-group rule; other deltas swept clean.)
- [x] 7.4 F12 PLAYTEST FIX (commit 28dd3d2, kGen 130→131): LW bushes near
      Hyrule Castle / the Kakariko library were not randomized on non-inverted
      seeds. The all-profile registry intersection required presence across the
      Inverted boundary, but Inverted is a different overworld, not a within-seed
      latch — ~28 non-inverted-only LW objects were dropped. Fix: register per
      WORLD-STATE GROUP (non-inverted {0,1,4} / inverted {2,3}) with a
      `world_state_filter` (universal / `[open,standard,retro]` / `[inverted]`)
      that placement + reachability already honor. Registry 3943→3981; only the
      12 terrain corpus digests move (180 non-terrain byte-identical);
      terrain-hunt-all-none seed 000A→000C (RNG shift tipped a marginal hunt
      seed). Corpus 192/192 on WSL gcc + MSVC (identical digest), all guards
      green, playtest-confirmed by owner.
