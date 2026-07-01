# All Enemy Checks - tasks

## 1. Proof and generated audit

- [x] 1.1 Create a separate OpenSpec follow-up for the all-enemy tier.
- [x] 1.2 Add `assets/scripts/audit_enemy_check_candidates.py` to scan local
  sprite assets and emit gitignored `assets/rando/enemy_check_candidates.audit.yaml`.
- [x] 1.3 Review the audit output and start with dungeon-only emission; overworld
  stays audit-only until runtime carries stable source stage/list-slot identity.
- [x] 1.4 Add `assets/scripts/gen_enemy_check_tables.py` to emit the ordinary
  dungeon enemy-check registry from local ROM assets and key-depth metadata.

## 2. Identity and runtime model

- [x] 2.1 Emit only dungeon candidates with reviewed reachability/key-depth
  coverage; exclude uncovered candidates from the generated registry.
- [ ] 2.2 Add stable runtime source identity for overworld enemies before any
  overworld candidates are emitted.
- [x] 2.3 Implement death-time direct-grant dispatch for ordinary enemy checks.
- [x] 2.4 Preserve vanilla forced-key pickup behavior for `enemy_drop_checks=keys`.
- [x] 2.5 Add checked-state visuals on live ordinary carriers and clear them after
  collection.
- [x] 2.6 Suppress checked ordinary enemies on room reload while preserving source
  slot identity for later sprites in the same room.

## 3. Location capacity and persistence

- [x] 3.1 Prove the emitted dungeon-only registry fits the existing
  `kRandoLocationCapacity`; do not raise capacity.
- [x] 3.2 Reuse the existing checked-location bitmap and snapshot payload size.
- [x] 3.3 Add generated-location count selfchecks for ordinary enemy rows.

## 4. Logic, placement, and UI

- [x] 4.1 Generate ordinary enemy locations only for sources with sound reach
  predicates and key-depth metadata.
- [x] 4.2 Add placement pool/junk/trap/customizer policy for ordinary `Enemy`
  checks.
- [x] 4.3 Expose `enemy_drop_checks=all` in CSV, file select, and the native UI,
  with door shuffle and enemy shuffle degrading requested `all` to effective
  `keys`.
- [x] 4.4 Group spoiler/tracker/autotracker output through the normal dungeon room
  location model rather than a separate flat runtime-only list.
- [x] 4.5 Add per-source kill predicates for ordinary enemy checks, including
  counted thrown-pot alternatives from engine damage tables and room pot counts.
- [x] 4.6 Degrade requested `all` to effective `keys` under enemy shuffle until
  placement can model shuffled type/HP for ordinary enemy checks.

## 5. Verification

- [ ] 5.1 OpenSpec strict validation.
- [ ] 5.2 Candidate/registry freshness checks.
- [ ] 5.3 Release build and `--rando-selftest`.
- [ ] 5.4 Corpus rows for keys, all-Wild, all-Dungeon, enemy shuffle degradation,
  pot shuffle, door shuffle, and pot+enemy+door interactions.
- [ ] 5.5 Fresh-eyes review.
- [ ] 5.6 Runtime playtest: death direct grant, leave/re-enter, save/reload,
  snapshot before/after death, checked visual clear, enemy shuffle interaction,
  and high-density tracker/spoiler usability.
