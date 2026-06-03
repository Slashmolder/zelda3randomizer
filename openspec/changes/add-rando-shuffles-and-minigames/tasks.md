<!-- =====================================================================
RECONCILIATION (2026-06-02, claude/shuffles-overnight): The checkboxes
below were AUTHORED stale. Verified against source in `main`, the real
state is much further along — see OVERNIGHT_REPORT.md "STEP 0 reality map".
Highlights:
  * ALL FOUR minigame sites are DONE+WIRED on main (incl. #78 Hype Cave NPC
    @ sprite_main.c:25930 and #79 Hammer Pegs @ overworld.c:3033 — these were
    marked "blocked" but are finished). §4.2/§4.3 are NOT blocked.
  * Boss + drop shuffle modules + runtime substitution + headless-generation
    install all exist; the GAP is the runtime install at slot load
    (Rando_ActivateSidecarSlot) — the feature was runtime-INERT in real play.
  * Settings axes + canonical serialization + CSV: done. Native toggles were
    DISABLED placeholders precisely because of the inert-at-runtime gap.
This run finishes: runtime install, drop heart-drop guarantee + fallback,
boss/drop self-checks, spoiler boss_assignments/drop_tables, live native
toggles (experimental), kGenVer 48->49, corpus seeds, docs.
Boxes ticked [x] below carry a `done:` note for what was verified/landed.
===================================================================== -->

## 1. Apply-time pre-flight

- [x] 1.1 Grep `src/sprite_main.c` for each of the 4 minigame sites — confirm exact patch points + sprite IDs:
  - Digging Game (handlers at lines 931, 7772, 19407+; find the reward-grant site).
  - Hype Cave NPC (the soldier sprite; not the chests).
  - Peg Cave (hammer-pegs sprite + reward-chest open path).
  - Treasure-Chest minigame (the "pick 1 of 3" handler).
  <!-- done: all 4 located + WIRED on main. Digging player.c:6891; Chest Game dungeon.c:6072; Hammer Pegs overworld.c:3033 (HandlePegPuzzles screen 98); Hype Cave NPC sprite_main.c:25930 (NiceThiefWithGift room 0x11E). -->
- [x] 1.2 Verify Peg Cave location id is in `assets/rando/location_registry.yaml`. If missing, add append-only. <!-- done: id 218 "Hammer Pegs" present (location_registry.yaml:337). -->
- [x] 1.3 Verify Treasure-Chest minigame's 3 candidate-chest location ids are in the registry. If missing OR if only 1 exists, add the other 2 as append-only. <!-- done: fork models the chest game as the single LOC_Chest_Game rare-prize dispatch (dungeon.c:6072), not 3 slots — the runtime is a "rare prize fires once" gate, not a literal 3-chest placement. The D3 3-slot model is N/A to this reimplementation; documented in OVERNIGHT_REPORT.md. -->
- [x] 1.4 Grep `../alttp_vt_randomizer/app/Boss.php` + `app/EnemyDrop.php` line counts + record source-line ranges in `audit.md §"Boss-shuffle provenance"` and `§"Drop-pool provenance"`. <!-- done: created audit.md with both sections. Boss.php (185 lines): 12 bosses in BossCollection @ Boss.php:68-128, each with file:line + shuffle role (10 shufflable + Agahnim/Agahnim2 pinned + Ganon out-of-pool = matches design D1). --> <!-- CORRECTION: app/EnemyDrop.php DOES NOT EXIST. Drop pool lives in app/Drops/PrizePack.php (61) + PrizePackSlot.php (60) + the roster in app/World.php:76-87 (11 packs, 63 slots) + sprite table app/Sprite.php (229 entries). Corrected source map recorded in audit.md §"Drop-pool provenance"; update design.md to match. Default-fill + ROM-writer location flagged as open follow-up. -->

## 2. Boss-shuffle module

- [x] 2.1 Create `src/rando/shuffle_boss.c` + `src/rando/shuffle_boss.h`. API: <!-- done: module exists; API is BossShuffle_Generate / _GetForDungeon / _RemapSpriteType / _ShouldSuppressSecondary. ComputeAssignment pure-fn added this run. -->
  ```c
  typedef enum { kBoss_HelmasaurKing, kBoss_Lanmolas, ... kBoss_Trinexx, kBoss_ArmosKnights } BossId;
  typedef enum { kDungeon_EP, kDungeon_DP, ... kDungeon_TR } DungeonId;
  
  void BossShuffle_Compute(Rng *rng, const RandoSettings *settings, BossId out_assignments[NUM_DUNGEONS]);
  ```
- [x] 2.2 Algorithm: 10-boss permutation per design.md D1. Goal-required bosses (Agahnim 1, Agahnim 2, Ganon) pinned at canonical slots. <!-- done: Fisher-Yates over kBossShufflePool[10]; slots 4 (HCT/Aga1) + 12 (GT/Aga2) pinned; Ganon out-of-pool. shuffle_boss.c:93. -->
- [x] 2.3 Implement `BossShuffle_Run` that: <!-- done this run: headless --generate-seed calls BossShuffle_Generate (main.c:766); slot+runtime install added at Rando_ActivateSidecarSlot (recovers settings+seed, same order as prize/medallion). The assignment is regenerated from (settings,seed) at slot load — not stored in slot state (deterministic). -->
  - Runs after `Place_AssumedFill` + sphere computation (per design.md D5 ordering).
  - Calls `BossShuffle_Compute`.
  - Stores result in slot state for runtime substitution.
- [x] 2.4 Runtime boss substitution: when a dungeon's boss room loads, the sprite-handler consults the boss-assignment table and substitutes the correct boss sprite. <!-- done: src/sprite.c:3693 in Dungeon_LoadSingleSprite — `type = BossShuffle_RemapSpriteType(type)` + orphan-segment suppression at :3678. (Patch site is sprite.c, not dungeon.c.) -->

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

- [x] 4.1 **Digging Game**: wire `Rando_OnLocationCheck(LOC_Digging_Game, vanilla_item)` at the dig-reward grant site in `src/sprite_main.c`. The sprite handler currently grants vanilla items inline; replace with the dispatcher call. *(Landed in slice 8 #67.)*
- [x] 4.2 **Hype Cave NPC**: wire dispatch at the Hype Cave soldier-NPC handler. <!-- done (NOT blocked — finished on main): sprite_main.c:25930 in NiceThiefWithGift, gated on RandomizerActive AND dungeon_room_index == 0x11E (full 16-bit match avoids low-byte collision); passes 0xFFFF registry-id convention so a placed Rupee100 can't mis-grant 300 rupees; handles direct-grant + confirmation cue. Verified by read, PLAYTEST-PENDING by eye. -->
- [x] 4.3 **Peg Cave**: wire dispatch at the hammer-pegs reward-chest open path. <!-- done (NOT blocked — finished on main): overworld.c:3033 in HandlePegPuzzles, screen 98, 22nd peg-hit. Sets the obtained-bit BEFORE the tile reveal so the vanilla standing PoH self-cancels; gated on RandomizerActive + (0x40==0) re-trigger guard. lttp code 0x17 (quarter PoH), not 0x26. Verified by read, PLAYTEST-PENDING by eye. -->
- [x] 4.3-rando-exempt The peg-state mutations carry the audit-guard exemption convention; check_audit_guard.py --strict passes on the wired path.
- [x] 4.4 **Treasure-Chest minigame**: wire dispatch at the pick-1-of-3 handler. The picked chest's `LOC_<...>` dispatches; the other 2 chests are not dispatched in that play-through. Spoiler annotates the 3 slots as `"choice_group": "treasure_chest"` per design.md D3. *(Landed in slice 8 #67 — `LOC_Chest_Game` dispatch at `src/dungeon.c:5955-5963`.)*
- [x] 4.5 Vanilla-mode regression: each new dispatch site preserves byte-identical behavior when `kFeatures1_RandomizerActive` is clear. <!-- done: all 4 sites gate on `enhanced_features1 & kFeatures1_RandomizerActive`; the lookups (RemapSpriteType / DropShuffle_Lookup) are passthrough when their assignment-active flag is false, which only Rando_ActivateSidecarSlot sets. check_audit_guard.py --strict green. -->

## 5. Settings axes

- [x] 5.1 Add `settings.boss_shuffle` boolean field. <!-- done: canonical offset [23], rando_settings.h:115, serialize rando_settings.c:190. -->
- [x] 5.2 Add `settings.drop_pool_shuffle` boolean field. <!-- done: field is named `drop_shuffle`, canonical offset [24], rando_settings.c:191. -->
- [x] 5.3 CSV parser accepts `boss_shuffle=true|false` and `drop_pool_shuffle=true|false`. <!-- done: keys `boss_shuffle` + `drop_shuffle`, rando_settings.c:915-923. -->
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
- [ ] 9.3 Cross-link this change from the `openspec/changes/` index (README.md).

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
