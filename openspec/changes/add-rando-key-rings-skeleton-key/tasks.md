# Key Rings and Skeleton Key - tasks

## 1. Registry and authoritative key metadata

- [x] 1.1 Append `KeyRing_*` IDs 220..232 and `SkeletonKey` ID 233 to
  `assets/rando/item_registry.yaml`; regenerate item IDs, names, dispatch tables,
  and assert `ITEM__COUNT <= 256`.
- [x] 1.2 Add explicit small-key↔ring↔rando-dungeon↔game-key-slot mapping helpers
  and selfchecks, including Hyrule Castle proper's Escape/Sewers key-slot fold.
- [x] 1.3 Generate/centralize the complete authored key-stock count for all 13
  families from dungeon chest and key-drop metadata; assert every count covers
  authored and door-shuffle thresholds and stays below `0xff`.
- [x] 1.4 Add one shared Key Ring icon and one Skeleton Key icon to the custom item
  gfx generator/blob, palette path, direct-grant icons, field-item sprites, and
  item-name presentation.
- [x] 1.5 Add codegen/build wiring and append-only registry/art freshness guards.

## 2. Settings, selection, provenance, and compatibility

- [x] 2.1 Add `key_rings=off|random|all` and `skeleton_key=false|true` to
  `RandoSettings`, defaults, validation, CSV parsing, presets, and fixed-settings
  fixtures.
- [x] 2.2 Append canonical byte `[30]` (ring bits 0-1, Skeleton bit 2, bits 3-7
  refused), grow `kSettingsCanonicalLen` 30→31, and add round-trip/undefined-bit
  selfchecks.
- [x] 2.3 Implement `Settings_EffectiveKeyRings`: normalize Off for effective
  Vanilla small keys and Retro generic keys without overwriting the persisted
  requested mode; keep Skeleton Key independent.
- [x] 2.4 Implement the centralized pre-collapse eligible-family counter over base,
  pot, enemy, and future registered shuffled key sources.
- [x] 2.5 Implement salted `KeyRings_Select(settings, seed)` without consuming the
  main fill RNG; pin fixed-seed masks and Random's non-empty/non-total property;
  refuse effective-Random configurations with fewer than two eligible families
  while allowing requested Random that resolves Off under Vanilla/Retro.
- [x] 2.6 Bump `kGeneratorVersion` and corpus `generator_version` together; run the
  generator-version and corpus-version sync guards.

## 3. Item pool, assumed fill, and customizer

- [x] 3.1 Refactor `BuildItemPool`/callers so seed-derived ring selection is
  available after all active small-key source contributions and before junk pad.
- [x] 3.2 For each selected family, replace one ordinary small key with one ring,
  remove all other shuffled copies, and let junk pad fill the released slots.
- [x] 3.3 Classify rings as dungeon progression in the restrictive first placement
  tier; preserve Dungeon confinement and Wild placement behavior.
- [x] 3.4 Add exactly one non-progression Skeleton Key before junk padding when
  enabled, preserving total placement cardinality.
- [x] 3.5 Make `OP_ITEM_IS(SmallKey_D)` and `OP_ITEM_IS(KeyRing_D)` candidate-
  equivalent for selected D so forced/forbidden/always-allow key rules remain
  sound.
- [x] 3.6 Reject customizer unselected rings, duplicate rings/Skeleton Keys, and
  ordinary selected-family keys reintroduced through pins; reject every
  `pool_overrides` edit that targets a small key, Key Ring, or Skeleton Key.
- [x] 3.7 Add pool selfchecks for every family and base/pot/enemy/combined source
  matrix; prove one ring + zero shuffled family keys for every selected family.

## 4. Logic and door-shuffle oracle

- [x] 4.1 Add a centralized effective item-count helper: a held family ring
  saturates that family's small-key count; Retro GenericKey remains mutually
  exclusive and unchanged.
- [x] 4.2 Route `HAS_ITEM`, `HAS_AMOUNT`, `HAS_ANY_OF`, and `HAS_ANY_COUNT` through
  the helper, with focused predicate selfchecks.
- [x] 4.3 Route door-shuffle `held_keys[]`, oracle fingerprints/caches, final sphere
  walk, goal verification, and live reachability through the same ring-aware model.
- [x] 4.4 Materialize the derived owned-ring mask into live `RandoCounts` ring-item
  entries; prove numeric key counters alone never synthesize ring ownership.
- [x] 4.5 Audit all direct `SmallKey_*` count reads so none bypass the helper.
- [x] 4.6 Keep Skeleton Key non-progression and absent from every logic/oracle path;
  add an invariant that removing it from counts never changes reachability or goal
  completion.
- [x] 4.7 Validate ring placement in Standard HCE, Inverted dungeon logic,
  door-shuffle layouts, and dungeon chains without self-locks.

## 5. Runtime grants and key-door behavior

- [x] 5.1 Add Key Ring direct grant: max-write the target saved key slot with the
  authoritative full-stock count, mirror the live counter when appropriate, set
  derived ownership, invalidate reachability, refresh HUD/tracker, and confirm.
- [x] 5.2 Add Skeleton Key direct grant and derived ownership state with the same
  direct-grant confirmation contract.
- [x] 5.3 At the sole small-key-door payment site, bypass the regular/GenericKey
  decrement when Skeleton Key is owned; preserve the already-open and door mirror
  behavior.
- [x] 5.4 Prove the big-key-door branch is unchanged and Skeleton Key never sets or
  satisfies a Big Key bit.
- [x] 5.5 Add runtime selfchecks for in/out-of-dungeon ring grants, HC folding,
  max-not-lower behavior, regular key payment, GenericKey payment, Skeleton
  no-spend with zero/nonzero keys, and big-key refusal.

## 6. Persistence, share strings, snapshots, and race mode

- [x] 6.1 Build the 13-bit ring-owned and Skeleton-owned hot cache from installed
  placements plus checked bitmap; update on grant and clear on deactivation.
- [x] 6.2 Rebuild derived ownership after sidecar activation, bitmap load, snapshot
  cold replay, placement reinstall, race reveal, and customizer slot activation.
- [x] 6.3 Bump sidecar format 9→10 for the 31-byte settings blob; read v1..v9 with
  versioned shorter layouts and zero-extension; make the v10 reader refuse future
  versions/trailing bytes and document v10 as unsupported by pre-v10 binaries.
- [x] 6.4 Update snapshot RandoState settings parsing, suppressed-spoiler layout and
  size, native settings persistence, and every `kSettingsCanonicalLen`-coupled
  assertion/buffer.
- [x] 6.5 Update v2 share-string capacity and exact-length fixtures (31-byte
  canonical → 47 raw bytes → 76 base32 chars); retain the 50-char v1 wire format
  while testing its intentional token-value change, and retain old-short v2
  zero-extension.
- [x] 6.6 Add save/load and snapshot round trips for unowned/owned rings, owned
  Skeleton Key, spent counters, and slot deactivation/reactivation.

## 7. UI, spoiler, tracker, autotracker, and docs

- [x] 7.1 Add native-window `Key rings` selector and `Skeleton Key` checkbox near
  dungeon small-key settings, including effective-Off explanation and “bonus only”
  copy.
- [x] 7.2 Update settings summaries, CLI help, file-select/native persistence, and
  `docs/randomizer.md` with interactions and non-goals.
- [x] 7.3 Emit spoiler JSON/text fields for requested/effective mode, eligible and
  selected masks/names, selection version, and Skeleton enabled state.
- [x] 7.4 Keep ring selection/ownership out of tracker/map panels so Random mix
  cannot leak; expose only the live numeric remaining-key counters, while allowing
  Skeleton ownership to remain visible as a normal bonus item.
- [x] 7.5 Keep selected/owned ring families out of autotracker output; prove both
  ordinary-key and ring families begin at zero and a collected ring updates the
  existing numeric key field to full stock.
- [x] 7.6 Make rings progression-hintable and Skeleton bonus-hintable without
  letting Skeleton enter progression-only accessibility/hint selection.

## 8. Automated validation

- [x] 8.1 Run `openspec validate add-rando-key-rings-skeleton-key --strict` and
  `openspec validate --all --strict`.
- [x] 8.2 Run `git diff --check`, codegen strict/freshness/wiring checks, registry
  append-only checks, audit guard, settings/share/save selfchecks, and corpus schema
  validation.
- [x] 8.3 Build Release x64 and run `--rando-selftest` plus `--door-selftest`,
  including the HCE Key Ring composition regression proving Zelda's Cell and the
  Zelda rescue remain gated by `Soul_Soldier` rather than guard kill state.
- [x] 8.4 Prove feature-Off placement tables are byte-identical to the pre-change
  generator for exact seeds; rebaseline only the intentional provenance/hash
  changes.
- [x] 8.5 Run corpus rows for Off, Random Dungeon, Random Wild, All, pot keys,
  enemy keys, pot+enemy keys, door shuffle, dungeon chains, Inverted, Standard
  escape, Retro normalization, Skeleton-only, and rings+Skeleton.
- [x] 8.6 Run exact-seed determinism/reload checks: selected mask, placement table,
  spoiler fields, sidecar activation, and snapshot replay must agree.

## 9. Owner playtests and closeout

- [ ] 9.1 Play a Random-mix seed and confirm at least one ring family and one
  regular-key family, correct names/art, and one ring check per selected family.
- [ ] 9.2 Play an All seed with pot and enemy key checks; verify every randomized
  family key source collapsed and released checks contain ordinary junk.
- [ ] 9.3 Collect rings inside and outside their home dungeon; verify saved/live
  counters, HUD, room transitions, save/reload, and no counter lowering.
- [ ] 9.4 Collect Skeleton Key with zero and nonzero regular keys; open multiple
  vanilla and relocated small-key doors without decrement, then confirm a big-key
  door still requires its Big Key.
- [ ] 9.5 Save/load and snapshot-replay after collecting and spending keys; confirm
  ring/Skeleton ownership, no duplicate grants, and tracker/autotracker state.
- [ ] 9.6 Complete one door-shuffle or dungeon-chain dungeon with rings and Skeleton
  enabled, checking mirrored door-open persistence and exit behavior.
- [ ] 9.7 Reconcile as-built spec/tasks/docs, archive the change, validate archived
  baselines, and complete the durable merge/push workflow requested for
  implementation.
