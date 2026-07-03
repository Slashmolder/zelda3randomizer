# All Enemy Checks - tasks

## Current implementation status

- [x] Foundation: reserve `enemy_drop_checks=all` as value `3`, remove the legacy
  text alias that made `all` mean `dungeon`, update canonical serialization,
  validation, CSV/share decode, generator version, docs, and corpus manifest.
- [x] Foundation: apply derived rules for vanilla keys, door shuffle, enemy
  shuffle, boss shuffle, entrance shuffle, and missing registry.
- [x] Foundation: fail generation before placement retries when pure effective
  `all` would require the missing complete all-enemy registry.
- [ ] Full all-enemy gameplay registry/runtime/logic remains pending below.

## 1. Audit and scope

- [ ] 1.1 Create a complete local enemy-source audit across dungeon rooms,
  overworld areas, bosses/minibosses, and finite scripted spawn groups.
- [ ] 1.2 Classify every source as included or excluded with a stable reason.
- [ ] 1.3 Add freshness guards for unclassified sources, duplicate identities, stale
  source metadata, and unsupported killable finite sources.
- [x] 1.4 Implement the concrete compatibility matrix for vanilla keys, door shuffle,
  enemy shuffle, boss shuffle, pot shuffle, cave entrance shuffle, and missing or
  stale registries.
- [x] 1.5 Treat unsupported finite killable authored sources as blockers or explicit
  effective downgrades/rejections, not as quiet exclusions from an active `all` tier.

## 2. Identity and runtime

- [ ] 2.1 Add stable source identity for overworld enemy spawns.
- [ ] 2.2 Add stable parent/child identity for finite scripted spawn groups.
- [ ] 2.3 Add boss/miniboss death-event identity where a separate enemy check can
  coexist with existing boss prizes and heart/prize behavior.
- [ ] 2.4 Implement death-time direct grant for all ordinary emitted enemy rows.
- [ ] 2.5 Suppress checked overworld, boss, and scripted-spawn sources on reload while
  preserving later source identities.
- [ ] 2.6 Keep forced enemy-drop rows on the existing pickup-time path.

## 3. Logic and placement

- [ ] 3.1 Emit all-enemy locations and grouping metadata for every included source.
- [ ] 3.2 Add per-source reachability predicates for overworld, boss, and scripted
  sources.
- [ ] 3.3 Add per-source kill-route predicates using effective type and HP.
- [ ] 3.4 Add counted thrown-object routes only when enough reachable pots, rocks, or
  equivalent throwables exist.
- [ ] 3.5 Add thrown-object branch metadata that proves consumed throwable sources do
  not double-count required pot-sanity item checks.
- [x] 3.6 Normalize requested `all` under enemy shuffle to the highest lower tier
  allowed by existing derived rules until a future placement-affecting enemy-shuffle
  contract exists.
- [ ] 3.7 Add door-shuffle non-key enemy bridges with digest/replay support before
  keeping `all` active under door shuffle; otherwise normalize requested `all` to
  `keys`.
- [ ] 3.8 Normalize or exclude all-enemy boss rows under boss shuffle until assigned
  boss-room identity and reward interactions are modeled.

## 4. Capacity and persistence

- [ ] 4.1 Measure worst-case all-enemy location count with pot sanity and other
  expansion features active.
- [ ] 4.2 Raise location capacity and migrate placement/checked-state storage if the
  emitted registry exceeds current limits.
- [ ] 4.3 Update sidecar and snapshot payloads for any expanded checked bitmap or
  source-identity metadata.
- [ ] 4.4 Add fail-closed snapshot restore handling for missing or malformed
  all-enemy metadata.
- [ ] 4.5 Include all-enemy door bridge rows, digest, and effective tier in
  DoorShuffleLayout activation and snapshot replay before supporting `all` with door
  shuffle.

## 5. UI, marker, and output

- [x] 5.1 Add `enemy_drop_checks=all` to CSV and share/settings decode as a distinct
  reserved value; native UI intentionally hides the selector choice until a complete
  all-enemy registry exists.
- [x] 5.2 Display effective downgrades or generation rejection reasons clearly.
- [ ] 5.3 Group spoiler, tracker, reachability, and autotracker output by dungeon
  room, overworld area/screen, boss arena, or scripted parent source.
- [ ] 5.4 Reuse enemy marker modes, including generic glints and item markers with
  multi-icon fallback when available.
- [ ] 5.5 Verify dense all-enemy marker screens do not corrupt OAM, palettes, pot
  glints, or item receipt graphics.
- [ ] 5.6 Add marker candidate metadata for every all-tier domain that renders
  in-world markers, or explicitly mark that domain as marker-suppressed while
  keeping tracker/spoiler output complete.

## 6. Validation

- [x] 6.1 Run `openspec validate add-rando-all-enemy-checks --strict`.
- [x] 6.2 Run source-audit/codegen freshness checks.
- [x] 6.3 Run Release build and `--rando-selftest`.
- [ ] 6.4 Run corpus rows for `all` and every supported/degraded interaction.
- [ ] 6.5 Runtime-test dungeon, overworld, boss/miniboss, and finite scripted-spawn
  checks through death, reload, save/load, snapshot, and transition cases.
- [ ] 6.6 Test thrown-pot kill logic with insufficient and sufficient pot counts,
  plus pot-sanity ordering cases where a required pot item cannot also be a weapon.
- [ ] 6.7 Test door-shuffle bridge digest drift, enemy-shuffle normalization, and
  boss-shuffle normalization.
- [ ] 6.8 Playtest dense screens with generic markers and item markers.
