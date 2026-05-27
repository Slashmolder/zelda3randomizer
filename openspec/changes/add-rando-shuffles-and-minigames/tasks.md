## 1. Apply-time pre-flight

- [ ] 1.1 Grep `src/sprite_main.c` for each of the 4 minigame sites — confirm exact patch points + sprite IDs:
  - Digging Game (handlers at lines 931, 7772, 19407+; find the reward-grant site).
  - Hype Cave NPC (the soldier sprite; not the chests).
  - Peg Cave (hammer-pegs sprite + reward-chest open path).
  - Treasure-Chest minigame (the "pick 1 of 3" handler).
- [ ] 1.2 Verify Peg Cave location id is in `assets/rando/location_registry.yaml`. If missing, add append-only.
- [ ] 1.3 Verify Treasure-Chest minigame's 3 candidate-chest location ids are in the registry. If missing OR if only 1 exists, add the other 2 as append-only.
- [ ] 1.4 Grep `../alttp_vt_randomizer/app/Boss.php` + `app/EnemyDrop.php` line counts + record source-line ranges in `audit.md §"Boss-shuffle provenance"` and `§"Drop-pool provenance"`.

## 2. Boss-shuffle module

- [ ] 2.1 Create `src/rando/shuffle_boss.c` + `src/rando/shuffle_boss.h`. API:
  ```c
  typedef enum { kBoss_HelmasaurKing, kBoss_Lanmolas, ... kBoss_Trinexx, kBoss_ArmosKnights } BossId;
  typedef enum { kDungeon_EP, kDungeon_DP, ... kDungeon_TR } DungeonId;
  
  void BossShuffle_Compute(Rng *rng, const RandoSettings *settings, BossId out_assignments[NUM_DUNGEONS]);
  ```
- [ ] 2.2 Algorithm: 10-boss permutation per design.md D1. Goal-required bosses (Agahnim 1, Agahnim 2, Ganon) pinned at canonical slots.
- [ ] 2.3 Implement `BossShuffle_Run` that:
  - Runs after `Place_AssumedFill` + sphere computation (per design.md D5 ordering).
  - Calls `BossShuffle_Compute`.
  - Stores result in slot state for runtime substitution.
- [ ] 2.4 Runtime boss substitution: when a dungeon's boss room loads, the sprite-handler consults the boss-assignment table and substitutes the correct boss sprite. Patch site: `src/dungeon.c` boss-room load path (verify exact site at apply-time).

## 3. Drop-pool shuffle module

- [ ] 3.1 Create `src/rando/shuffle_drops.c` + `src/rando/shuffle_drops.h`. API:
  ```c
  void DropPoolShuffle_Compute(Rng *rng, const RandoSettings *settings, const PlacementTable *pt, const SphereData *sd, DropTable out_tables[NUM_TIERS]);
  ```
- [ ] 3.2 Algorithm: per-tier drop-table permutation with heart-drop constraint per Phase A spec.
- [ ] 3.3 Heart-drop guarantee: post-shuffle, verify at least one tier reachable in spheres 0-2 contains a heart drop. If violated, retry (bounded budget).
- [ ] 3.4 Wire into runtime sprite-drop path: existing `Sprite_DropItem` logic consults the shuffled drop tables when `kFeatures1_RandomizerActive && drop_pool_shuffle`.
- [ ] 3.5 Forward-fill fallback: if heart-drop guarantee retries exhaust, fall back to identity drop-pool with a spoiler `fallback_warnings` entry.

## 4. §6.8 minigame dispatch

- [ ] 4.1 **Digging Game**: wire `Rando_OnLocationCheck(LOC_Digging_Game, vanilla_item)` at the dig-reward grant site in `src/sprite_main.c`. The sprite handler currently grants vanilla items inline; replace with the dispatcher call.
- [ ] 4.2 **Hype Cave NPC**: wire dispatch at the Hype Cave soldier-NPC handler. The 4 chests in Hype Cave are already wired (`chest_lookup.h:203-206`); this site is the 5th, NPC-based.
- [ ] 4.3 **Peg Cave**: wire dispatch at the hammer-pegs reward-chest open path. Add `// rando-exempt: state-shuffle — hammer peg state` comments at the peg-state mutation sites.
- [ ] 4.4 **Treasure-Chest minigame**: wire dispatch at the pick-1-of-3 handler. The picked chest's `LOC_<...>` dispatches; the other 2 chests are not dispatched in that play-through. Spoiler annotates the 3 slots as `"choice_group": "treasure_chest"` per design.md D3.
- [ ] 4.5 Vanilla-mode regression: each new dispatch site preserves byte-identical behavior when `kFeatures1_RandomizerActive` is clear.

## 5. Settings axes

- [ ] 5.1 Add `settings.boss_shuffle` boolean field. Un-pinned (default false; un-pinned in this change). Add to canonical-serialization order.
- [ ] 5.2 Add `settings.drop_pool_shuffle` boolean field. Same shape.
- [ ] 5.3 CSV parser accepts `boss_shuffle=true|false` and `drop_pool_shuffle=true|false`.
- [ ] 5.4 Settings-screen widget: per-toggle.

## 6. Spoiler integration

- [ ] 6.1 Boss assignments: JSON spoiler emits `boss_assignments: {<DungeonId>: <BossId>}` mapping. Text spoiler under a `Boss Assignments` section.
- [ ] 6.2 Drop-pool: JSON spoiler emits `drop_tables: [<8 tier objects>]`. Text spoiler under a `Drop Tables` section.
- [ ] 6.3 Treasure-Chest minigame annotation: each of the 3 slots gets `"choice_group": "treasure_chest"` in its JSON entry.
- [ ] 6.4 When the shuffle is disabled (`boss_shuffle=false` etc.), omit the corresponding spoiler sections.

## 7. Determinism + CI

- [ ] 7.1 Bump `kGeneratorVersion`.
- [ ] 7.2 Regenerate corpus. Add at least 4 boss-shuffle seeds (across goals) + 4 drop-pool-shuffle seeds + 2 both-on seeds.
- [ ] 7.3 Verify default-settings digests (`boss_shuffle=false`, `drop_pool_shuffle=false`) remain byte-identical to pre-change baseline.
- [ ] 7.4 Verify minigame-dispatch wiring doesn't change default-settings digests (sites dispatch to identity per the dispatcher fall-back contract).

## 8. Audit-guard

- [ ] 8.1 Run `assets/scripts/check_audit_guard.py` after wiring each minigame site. Each new `link_item_*` write must dispatch OR be exempt.
- [ ] 8.2 Confirm boss-runtime-substitution doesn't write to tracked inventory cells.

## 9. Documentation

- [ ] 9.1 Update `docs/randomizer.md` settings reference: document `boss_shuffle=` and `drop_pool_shuffle=` axes.
- [ ] 9.2 Add a "Shuffle modules" subsection covering boss + drop-pool behavior.
- [ ] 9.3 Update `docs/randomizer_phase_b.md` Slices 7 + 8 status: mark complete.

## 9.5. Performance budget verification

- [ ] 9.5.1 **Generation budget bench**: both shuffles run AFTER `Place_AssumedFill` + sphere computation (per design.md D5). Each adds a post-placement pass. Bench: a seed with both shuffles on SHALL stay within Phase A's 2s desktop / 5s Switch budget.
- [ ] 9.5.2 Drop-pool's heart-drop-guarantee constraint loop is the long pole — if the retry budget exhausts often, fall back to identity drop-pool with a spoiler `fallback_warnings` entry (per design.md D5 risk).
- [ ] 9.5.3 Boss-shuffle is O(10) permutation; should add <10ms.
- [ ] 9.5.4 Record final p50/p95/p99 in `audit.md §"Shuffles+minigames benchmark"`.

## 10. Playtest

- [ ] 10.1 Generate a boss_shuffle=true seed; play through Eastern Palace; verify the boss is randomized but the prize stays with EP.
- [ ] 10.2 Generate a drop_pool_shuffle=true seed; kill a low-tier enemy multiple times; verify drops match the shuffled tier table.
- [ ] 10.3 Heart-drop early-game smoke: in a drop_pool_shuffle seed, kill 20 enemies in the first 30 minutes; verify at least 1 heart drops (heart-drop guarantee).
- [ ] 10.4 Treasure-Chest minigame: play the minigame; verify dispatch fires for the picked chest only.
- [ ] 10.5 Digging Game: play the dig minigame; verify dispatch fires for the reward.

## 11. Archive readiness

- [ ] 11.1 CI green; corpus matches.
- [ ] 11.2 Manual playtest covers all 4 minigame sites + both shuffles on/off.
- [ ] 11.3 Fresh-eyes audit per `[[cluster-audit-cadence]]`.
- [ ] 11.4 `openspec archive add-rando-shuffles-and-minigames` runs cleanly.
