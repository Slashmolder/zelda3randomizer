## Why

`enemy_drop_checks=keys` has made the forced enemy key drops into first-class
checks. The next requested tier is the more extreme option: every safe static
enemy can become a check.

That still cannot be a blind enum unlock. Unlike forced key drops, ordinary
enemies do not all share pickup-time semantics, many enemies are spawned or
non-killable, and overworld source identity is not currently carried through
runtime. This change promotes the reserved value into a conservative first
implementation: dungeon-only ordinary enemy checks with generated proof,
death-time direct grants, and fail-closed placement/runtime tables.

The emitted tier is intentionally narrower than "all enemies in the game". The
local registry scans 464 eligible underworld sprite-table sources, emits the 270
dungeon candidates with conservative room predicates as ordinary `Enemy`
locations, keeps 158 key-depth-only candidates audit-only until room reach can be
modeled, and leaves 36 no-key-depth underworld sources outside the dungeon-only
scope. Overworld candidates remain audit-only until the runtime has stable
overworld source identity.

## What Changes

- Expose `enemy_drop_checks=all` as value 2 in CSV, share/settings decoding,
  file select, and the native window.
- Keep `all` active only when effective small keys are Wild/Retro or Dungeon.
  Vanilla small keys normalize to `off`; door shuffle and enemy shuffle degrade
  requested `all` to effective `keys`.
- Add a generated local audit, `assets/rando/enemy_check_candidates.audit.yaml`,
  produced by `assets/scripts/audit_enemy_check_candidates.py`.
- Add `assets/scripts/gen_enemy_check_tables.py`, which scans local dungeon
  sprite assets, reuses the enemy-shuffle curated safety table, excludes existing
  forced key-drop checks, and emits gitignored
  `assets/rando/enemy_checks.gen.yaml`.
- Generate 270 ordinary dungeon `Enemy` locations plus
  `src/rando/enemy_check_lookup.h` from the local registry.
- Add source-type kill predicates for ordinary enemy checks, including counted
  thrown-pot routes derived from engine damage tables and reachable room pot
  counts.
- Use death-time direct grant for ordinary enemy checks and checked-state
  suppression on room reload.
- Reuse the `[Graphics] EnemyDropMarker` preference for ordinary enemy carriers,
  with `item` and `generic` marker modes; item mode falls back to generic when
  multiple active room markers need different item icons.
- Add corpus rows for all-enemy Wild keys, all-enemy Dungeon keys, and the
  door-shuffle degradation path.

## Non-Goals

- Do not include overworld enemies in the emitted registry until runtime carries
  source stage/list-slot identity or an equivalent stable lookup key.
- Do not enable ordinary all-enemy checks under door shuffle until non-key enemy
  checks have a reviewed door-region bridge.
- Do not enable ordinary all-enemy checks under enemy shuffle until placement can
  consume the actual shuffled enemy type and HP scaling for each source slot.
- Do not raise `kRandoLocationCapacity`; the dungeon-only registry fits the
  current capacity with existing pot-sanity and enemy-drop rows.
