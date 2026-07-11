# Enemy Drop Sanity - tasks

Ordered so the forced-key tier can land independently, with the dungeon-enemy
tier supplied by `add-rando-dungeon-enemy-checks`.

- [x] 0.1 Audit current enemy-drop status: `drop_shuffle` is prize-pack only; forced
  small-key/big-key drops bypass it through `sprite_die_action`; pot-sanity patterns
  are the relevant precedent.
- [x] 0.2 Run three fresh-eyes review rounds over the plan (runtime, placement/logic,
  and OpenSpec/settings). Fold the clean third-round decisions into this change.

## 1. Settings and effective rules

- [x] 1.1 Add `enemy_drop_checks` to `RandoSettings` with numeric values
  `Off=0`, `Keys=1`, `Dungeon=2`.
- [x] 1.2 Append one canonical settings byte, bump `kSettingsCanonicalLen`,
  `kGeneratorVersion`, expected canonical/hash selfchecks, share-string settings
  length/CRC handling, sidecar settings replay, snapshot settings TLV replay,
  suppressed-spoiler fixed settings length, UI persistence, and corpus manifest.
- [x] 1.3 Implement effective accessors and derived normalization:
  vanilla keys normalize to `Off`, Dungeon keys including door shuffle are active,
  `Dungeon` is active only for vanilla-door supported key modes, and `enemy_shuffle`
  composes by source slot.
- [x] 1.4 Add tests that old share strings/canonical blobs decode as `Off` and that
  default/off seeds are placement-byte-identical.

## 2. Generated registry and codegen

- [x] 2.1 Add `assets/scripts/gen_enemy_drop_tables.py` to parse vanilla dungeon
  sprite data and emit local gitignored `assets/rando/enemy_drops.gen.yaml`.
- [x] 2.2 Emit `type=0xe4,y=0xfe` forced small-key markers whose previous entry
  is a stable real enemy source, plus the single reviewed `y=0xfd` Hyrule Castle
  big-key marker as a one-shot check.
- [x] 2.3 Hard-fail on marker-at-first-entry, consecutive markers, absent source,
  control/overlord/non-real source, non-killable/key-banned source, room mismatch,
  duplicate source identity, and runtime marker-to-carrier mismatch.
- [x] 2.4 Record reviewed door identity metadata (`door_dungeon`, `door_region`) for
  each mapped row, plus exact key-depth DROP metadata
  (`door_drop_index`/`key_depth`/`key_mindepth`) for small-key rows.
- [x] 2.5 Wire generated lookup/header outputs and codegen freshness guards. Public
  assetless builds emit empty tables; active generation with missing local registry
  fails closed.
- [x] 2.6 Add a new `EnemyDrop` location type across schema, codegen, C enums,
  tracker/spoiler grouping, customizer checks, and selftests.

## 3. Placement and key economy

- [x] 3.1 Add the shared `enemy_drop_keys_active(settings)` predicate and use it in
  open-location collection, junk padding, item-pool construction, selfchecks,
  spoiler/tracker emission, and logic generation.
- [x] 3.2 Pool mapped enemy small keys when active: world pool in Wild mode and
  `GenericKey` under Retro, and own-dungeon pools under Dungeon mode.
- [x] 3.3 Add Dungeon-key free-drop accounting: seed remaining free drops as
  `door_drop_total - active key pots - active enemy key drops`, and keep active
  assetless generation fail-closed.
- [x] 3.4 Add `Placement_SelfCheck` invariants: generated `EnemyDrop` locations
  match runtime lookup rows, Dungeon-key mode is active with vanilla doors and
  door shuffle, and pot+enemy free-drop accounting cannot drift.
- [x] 3.5 Define customizer and trap eligibility for key-tier enemy-drop locations;
  reject trap classes that cannot be delivered through the staged pickup path.

## 4. Runtime forced-key dispatch

- [x] 4.1 Carry the source slot through forced-key conversion into the absorbable key
  sprite and recover location identity from the generated `(room, source_slot,
  drop_kind)` lookup.
- [x] 4.2 Keep the source-slot identity snapshot-safe through existing
  snapshot-persisted sprite state; require `g_ram`/snapshot-tail storage only if a
  later phase adds a separate pending table.
- [x] 4.3 Preserve the no-durable-death-before-pickup invariant for active forced-key
  checks. If the player leaves before pickup, the unchecked source must be
  reattemptable on re-entry/save-reload.
- [x] 4.4 Resolve placement with `vanilla_registry_id=0xFFFF` after guarding checked
  locations before dispatch; use the existing dispatch path's checked-intent
  semantics.
- [x] 4.5 On successful pickup, suppress the vanilla case-12 key increment and future
  forced-key behavior for the checked source. Identity placements must not double
  grant or increment the current dungeon key through vanilla code.
- [x] 4.6 Draw an unchecked indicator on live carriers and the spawned forced-key drop.

## 5. Logic and UI surfaces

- [x] 5.1 Emit enemy-drop locations with generated region data and reviewed
  source-region predicates.
- [x] 5.2 Add VM/accessor support for active enemy-drop key gates under Wild and
  Retro key modes plus Dungeon min-depth gates.
- [x] 5.3 Expose the settings UI as a selector with `Off` and `Enemy key drops`;
  the follow-up dungeon-enemy change expands it to include `Dungeon`.
- [x] 5.4 Disable or normalize the selector when small keys are vanilla, but allow
  door shuffle and `enemy_shuffle` composition.
- [x] 5.5 Group tracker, reach-panel, auto-tracker, and spoiler entries by
  dungeon/room and hide inactive enemy-drop locations.
- [x] 5.6 Add door x enemy-drop bridge rows, door-layout bridge digesting, and
  prover key-source accounting so door shuffle composes with active enemy drops.

## 6. Verification

- [x] 6.1 OpenSpec validation: `openspec validate add-rando-enemy-drop-sanity --strict`
  and `openspec validate --changes`.
- [x] 6.2 Codegen guards: registry freshness, door/prover digest checks, duplicate
  source checks, unmapped enemy marker fail, one-shot big-key marker check, and
  assetless fail-closed behavior.
- [x] 6.3 Build and selftest: Release x64 build and `--rando-selftest`.
- [x] 6.4 Corpus: default/off behavior, `drop_shuffle` only unchanged, active keys
  under Wild/Retro/Dungeon, active door-shuffle composition, pot-shuffle combinations,
  and active `enemy_shuffle` composition.
- [x] 6.5 Runtime playtests: kill then leave before pickup; save/reload before pickup;
  snapshot before and after pickup; pickup then re-enter no duplicate; placed non-key
  item at an enemy-key location; checked visual clears; `drop_shuffle` plus active
  key check; door/pot key accounting seed.
  <!-- owner confirmation 2026-07-11: enemy and pot runtime combinations were
  previously covered. -->
- [x] 6.6 Fresh-eyes implementation review after the patch, with specific attention
  to staged dispatch, pending-state snapshot safety, and key accounting.

## 7. Lone forced big-key one-shot

- [x] 7.1 Decide the item model for the room `0x080` Ball-n-chain / Morning Star
  forced big-key marker: modeled castle big key vs pure one-shot check with vanilla
  big key pinned/free.
- [x] 7.2 Do not add a modeled castle big-key item; use the pure one-shot model.
- [x] 7.3 If pure one-shot, prove the vanilla big key remains available exactly once
  and that the placed check cannot duplicate or suppress the current-dungeon big-key
  grant incorrectly.
- [x] 7.4 Reuse the Phase 1 registry/pending-state/staged-dispatch/visual path and
  add targeted selftests/corpus coverage. Owner runtime playtest remains tracked in
  6.5.

## 7b. Deferred modeled big-key alternative

- [x] 7b.1 Defer the modeled castle-big-key alternative to a future change. The
  reviewed pure one-shot model in 7.1-7.4 is the shipping behavior; an optional
  alternate item model is not an archive gate for this change.

## 8. Dungeon-enemy tier

- [x] 8.1 Create a separate follow-up OpenSpec change for `Dungeon`.
- [x] 8.2 Generate a static dungeon spawn registry and exclude bosses,
  NPCs, objects, overlords, spawners, transient child sprites, dynamic spawns, and
  non-killable sources.
- [x] 8.3 Audit capacity and keep the emitted dungeon-only registry within the
  existing limit.
- [x] 8.4 Add killability/reachability predicates or conservative exclusions.
- [x] 8.5 Define death-time direct-grant semantics, visuals, and tracker grouping for
  ordinary dungeon enemy checks.
- [x] 8.6 Move overworld ordinary enemy checks behind stable overworld source
  identity into the separate `add-rando-all-enemy-checks` follow-up; they are not
  part of this forced-key/dungeon-tier change's shipping scope.
