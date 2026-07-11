## Why

`enemy_drop_checks=keys` has made the forced enemy key drops into first-class
checks. The next requested tier is the more extreme dungeon option: every safe
static dungeon enemy can become a check.

That still cannot be a blind enum unlock. Unlike forced key drops, ordinary
enemies do not all share pickup-time semantics, many enemies are spawned or
non-killable, and overworld source identity is not currently carried through
runtime. This change promotes the reserved value into a conservative first
implementation: dungeon-only ordinary enemy checks with generated proof,
death-time direct grants, and fail-closed placement/runtime tables.

The emitted tier is intentionally narrower than "all enemies in the game". The
shipping generated registry emits 350 reviewed dungeon candidates with conservative
room predicates as ordinary `Enemy` locations. Overworld and reviewed non-dungeon
underworld domains were subsequently added behind stable identity by the separate
`add-rando-all-enemy-checks` follow-up.

## What Changes

- Expose `enemy_drop_checks=dungeon` as value 2 in CSV, share/settings decoding,
  file select, and the native window.
- Keep `dungeon` active only when effective small keys are Wild/Retro or Dungeon.
  Vanilla small keys normalize to `off`; door shuffle composes through the generated
  enemy-check bridge; enemy shuffle degrades requested `dungeon` to effective `keys`.
- Add a generated local audit, `assets/rando/enemy_check_candidates.audit.yaml`,
  produced by `assets/scripts/audit_enemy_check_candidates.py`.
- Add `assets/scripts/gen_enemy_check_tables.py`, which scans local dungeon
  sprite assets, reuses the enemy-shuffle curated safety table, excludes existing
  forced key-drop checks, and emits gitignored
  `assets/rando/enemy_checks.gen.yaml`.
- Generate 350 ordinary dungeon `Enemy` locations plus
  `src/rando/enemy_check_lookup.h` from the local registry.
- Add source-type kill predicates for ordinary enemy checks, including counted
  thrown-pot routes derived from engine damage tables and reachable room pot
  counts.
- Use death-time direct grant for ordinary enemy checks and checked-state
  suppression on room reload.
- Reuse the `[Graphics] EnemyDropMarker` preference for ordinary enemy carriers,
  with `item` and `generic` marker modes; item mode falls back to generic when
  multiple active room markers need different item icons.
- Add corpus rows for dungeon-enemy Wild keys, dungeon-enemy Dungeon keys, and
  door-shuffle bridge composition.

## Non-Goals

- Do not include overworld enemies in the emitted registry until runtime carries
  source stage/list-slot identity or an equivalent stable lookup key.
- Do not enable ordinary dungeon-enemy checks under enemy shuffle until placement can
  consume the actual shuffled enemy type and HP scaling for each source slot.
- Do not raise `kRandoLocationCapacity`; the dungeon-only registry fits the
  current capacity with existing pot-sanity and enemy-drop rows.
