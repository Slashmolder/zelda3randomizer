# Tasks: add-rando-bonk-sanity

On `feature/rando-shopsanity` (owner-directed convergence branch). Every
phase ends buildable (MSVC + WSL gcc `-Werror`), selftest green. Standing
traps: `make clean` after header edits; register nothing new in vcxproj
unless a new .c appears; mirror new gitignored artifacts in
setup_worktree.py (stale-artifact lesson); absolute worktree paths.

## 1. Measure (dump first — no ID decisions before counts)

- [x] 1.1 `--dump-bonk-table`: headless walk of OW screens per SPRITE
      PHASE (prog 1/2/3 -> kOverworldSpriteOffs base 0/1/2 via
      GetOverworldSpritePtr — NOT the terrain profiles, which never visit
      base 1); emit `(stage, screen, type, x, y)` for placed sprites
      `0x79`/`0xAC` (the SpritePrep_OverworldBonkItem types).
- [x] 1.2 `gen_bonk_tables.py --emit`: register only objects whose
      (screen,x,y,type) is IDENTICAL across all three stages (exclude the
      rest — missable-check rule), ids from 7168, gitignored
      `assets/rando/bonk.gen.yaml`; plain bounds tripwire (count in
      [1,200], per-stage + grouped counts reported — ALTTPR's 62-address
      table is the ABSORBABLE family, not this population; no comparison);
      record counts for the morning report (owner scope call D8/Q1).
- [x] 1.3 setup_worktree.py mirrors bonk.gen.yaml.

## 2. Registry + logic

- [x] 2.1 `rando_logic_gen.py` loads bonk.gen.yaml → `LOCTYPE_Bonk`
      locations. AS-BUILT (review F2 reconcile): regions come from the
      reviewed OVERWORLD_REGIONS map, re-validated by load_bonk against the
      logic graph (unresolvable region = codegen hard-fail, the under-gate
      guard); NO bonk_logic_overrides.yaml was needed — all 10 areas
      resolve cleanly (the generator supports an optional overrides file
      for future splits). Lookup header emitted sorted (area, block) with a
      selfcheck sortedness prong.
- [x] 2.2 `macros.yaml`: `CanBonk()` = `HAS_ITEM(Boots)`; row predicates
      `TerrainWorldGate{LW|DW}() AND CanBonk()`.

## 3. Settings + placement

- [x] 3.1 `bonk_shuffle` (TerrainShuffle values) in byte [29] bits 5-6:
      struct field, serialize/deserialize/refused-mask, validate, CSV,
      Settings_SelfCheck vector; native-window EnumCombo + tooltip.
- [x] 3.2 Placement: junk-class + open-locations per tier; skip-triple
      extension; fail-closed generation when the registry is absent/empty
      (`settings_need_bonk_registry`); Placement_SelfCheck prongs (pool
      accounting per tier, junk-only proof for `junk`, fail-closed on
      empty registry).

## 4. Runtime + persistence

- [x] 4.1 Wake hook in `Entity_ApplyRumbleToSprites`. AS-BUILT REVISION
      (owner playtest decision 2026-07-18): originally DASH-ORIGIN ONLY
      (scoped flag + quake-latch exclusion); now BOTH rumble origins collect
      — Boots dash AND Quake-medallion (the only two callers of
      Prepare_ApplyRumbleToSprites) — because Quake is the other vanilla
      way to wake these sprites and the split read as inconsistent in
      playtest. The dash-marker flag/wrapper was removed. One Quake may
      collect several on-screen checks (each via the quiet path — no
      receipt ancilla, multi-grant safe). SECOND owner decision (kGen 149):
      `CanBonk()` itself widened to Boots OR (HasSword(1) AND Quake) — the
      canGetGoodBee shape — so placement logic matches the runtime's two
      wake origins. Guard kept: `!player_is_indoors`. Resolve
      (screen,x,y,type) → active+unchecked → quiet grant + mark checked +
      same-frame despawn (burst suppressed); else vanilla. Exclusions
      never resolve (Lumberjack tree is sprite 0x3B — not in the table).
- [x] 4.2 Sidecar ext `bonk_registry_digest/count` (ext version bump) +
      activation count>0 digest check; snapshot registry TLV extended to
      format_version 4 (v3 = pot+terrain+enemy-check is TAKEN);
      `Rando_StampSlotRegistries` carries the new pair (single-helper
      rule).
- [x] 4.3 Sprite-coord glint collector feeding the existing overworld
      glint draw; cap shared with terrain.
- [x] 4.4 Spoiler ordinary rows; tracker rows (no gating toggle);
      auto-tracker type tag; docs/randomizer.md row + section.

## 5. Validation

- [x] 5.1 Corpus rows: bonk-junk, bonk-all (open/fast_ganon), bonk-all ×
      inverted, bonk-all × shopsanity (branch composition); recapture;
      all pre-existing rows byte-identical.
- [x] 5.2 Both compilers clean; selftest green both binaries; slot-path (COMPLETED: MSVC + WSL gcc -Werror at every kGen through 149; parity spot-checked)
      guard; MSVC==WSL digest parity on a bonk seed.
- [x] 5.3 Self-check implementation against this plan + design.md (COMPLETED via the impl-review round + two external reviews; as-built deviations recorded above)
      decision list; reconcile deltas to as-built.

## As-built deviations (self-check vs plan, 2026-07-17)

- 1.1 AS-BUILT: no C dump mode — gen_bonk_tables.py parses the packed OW
  sprite asset directly (kOverworldSprites/kOverworldSpriteOffs, the
  gen_npc_soul_tables/enemy-check precedent), equivalently engine-ground-
  truth with less machinery.
- 1.2 AS-BUILT: the three-stage identity rule measured ZERO survivors
  (per-stage counts [1,12,15] — the tables genuinely differ); refined to
  stages 1+2 identity (the collectable window; stage 0 is the pre-rescue
  escape where bonk checks are logic-unreachable). Result: 10 locations,
  7 stage-variant exclusions.
- IDs AS-BUILT: base 2816 (0xB00) in the free 2634..3071 gap BELOW terrain —
  the plan's 7168 would have broken the kRandoNonTerrainLocationsCount
  contiguous-suffix fast-path (terrain must stay the id-sorted suffix).
- Key AS-BUILT: (area, block) with block = the engine's own
  sprite_where_in_overworld packing, recovered at wake from
  sprite_N_word[k] — the OW enemy-check identity mechanism, strictly
  stabler than raw (x,y).
- 4.3 AS-BUILT: the glint sources coordinates from LIVE dormant sprites on
  the current screen (joining the existing overworld marker glint mask via
  the same wake resolver) — no block->coordinate math, marks exactly what
  is on screen.
- 4.2 AS-BUILT: sidecar is format_version 11 / ext V11 @51-57 (v10 was the
  keyrings blob widen); snapshot PotRegistry TLV payload v4 (25 bytes).
- Hardening bonus: _location_type_id's silent return-0 for unknown types
  (which emitted the first Bonk build's rows as Chest ordinal, invisible to
  every LOCTYPE_Bonk consumer) is now a codegen hard-fail.
- Discovery for the owner: the committed registry already carries
  LOC_Bonk_Fairy_* Standing locations (the absorbable-family fairies) —
  disjoint from this axis (different sprite types); relevant to the Q1 v2
  scope call.
- Review F3 (LOW, playtest note): a genuine Boots dash inside the
  post-Quake byte_7E0FC6 latch window takes the vanilla wake (no grant) —
  recoverable on re-entry (farmable-not-missable), listed for the playtest
  matrix.
- Review F4 — SUPERSEDED, the "cannot strand" conclusion was WRONG
  (external-review P1): rescue is NOT item-free in Standard — Zelda's cell
  requires the Lamp in logic — so Lamp placed ON the stage-0-absent HCE
  apple tree was a deterministic hardlock the placer accepted (reproduced:
  Standard bonk_shuffle=all, Boots@Uncle + Lamp@2820, seed 1). FIXED at
  kGen 148: gen_bonk_tables.py appends HAS_ITEM(RescuedZelda) to every
  stage-0-absent row (all 10 current rows), a negative customizer selfcheck
  pins the exact reproducer as a REFUSE, and the
  "bonk-all-standard-rescue-gated" corpus row pins the gated spheres.
- Review F6 (LOW, latent): the wake key is (current area, origin block);
  no aliasing exists in the frozen 10-row data (reviewer-verified
  disjoint), noted for any future population growth.
