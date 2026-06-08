<!-- =====================================================================
RECONCILIATION (2026-06-02): The checkboxes
below were AUTHORED stale. Verified against source in `main`, the real
state is much further along (grounded against the as-built source in `main`).
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

CLOSE-OUT STATUS (2026-06-06): Drop shuffle + all 4 minigames are
DONE end-to-end (gen + runtime + spoiler + UI). Boss shuffle ships GEN +
SPOILER only; its runtime substitution was implemented then DEACTIVATED
(see §2.4) because a pure sprite-type swap renders garbage without per-boss
GFX loading. Boss-shuffle RUNTIME is carved to a FOLLOW-UP change
(add-rando-boss-shuffle-runtime-gfx, not yet authored). Remaining before
archive: §10 playtest (drops + 4 minigames; boss-runtime playtest moves to
the follow-up), then §11. The change archives as "drops + minigames + boss
generation"; the boss-runtime-rendering requirement is satisfied by the
follow-up.

UPDATE (2026-06-07, boss-shuffle runtime work — branch claude/boss-shuffle-runtime):
All three boss-runtime prerequisites LANDED (2026-06-07). Boss shuffle is now
playable (experimental); the render is playtest-only validated.
  * BEATABILITY LOGIC — LANDED (kGenVer 56). The boss-kill predicate override
    (design.md D6 correction): a new VM op OP_CAN_KILL_BOSS gates each dungeon's
    `- Boss`/`- Prize` on the SHUFFLED boss's kill predicate so no item-gated boss
    can strand its prize. Headless-validated (Logic + Placement self-checks,
    corpus 110/110 — only boss-on entries moved). Gated-off-safe: with the runtime
    sprite swap still off, boss_shuffle is UI-clamped off, so no strand can occur
    in play; the logic is correct + ready for when rendering lands. See §2.4b.
  * RENDER (GFX + spawn count) — LANDED via the Enemizer pointer-redirect model
    (the per-entry sprite-type swap was scrapped as the wrong architecture). When
    a shuffled boss room loads, BossShuffle_RenderHomeRoom redirects BOTH its
    sprite-data list AND its sprite-graphics index to the assigned boss's vanilla
    HOME boss room (dungeon.c / sprite.c), so the home room's correct formation +
    gfx render in the new room. Rando_ActivateSidecarSlot installs the assignment;
    the ShuffleInstall self-check is INVERTED (asserts install + non-passthrough
    redirect); the UI toggle is live. Runtime-only -> corpus 110/110 byte-identical.
    PLAYTEST-ONLY validated (could not be confirmed headless). KNOWN RISKS: Blind
    (maiden spawn), Trinexx/Kholdstare in non-home rooms (room-shell tiles).
  * PLAYTEST OUTCOME (2026-06-07): the 7 redirect-clean bosses (Armos, Lanmolas,
    Moldorm, Helmasaur, Arrghus, Mothula, Vitreous) confirmed working after two
    render fixes — spawn-coord alignment (boss shifts to the dungeon's OWN boss spot,
    not the home coords; Mothula->DP spawned behind a wall before) + sprite-palette
    redirect (boss draws with its own colors; Lanmolas->EP had garbled mounds before).
    The 3 KNOWN-RISK bosses are CONFIRMED broken (Blind->EP empty room; Kholdstare->DP
    un-encased) and now PINNED (kGenVer 57 Blind, 58 Kholdstare+Trinexx): each needs
    home-room environment the redirect can't carry. Pool is 10->7. Per-boss un-pin
    requirements: design.md D7 + randomizer-shuffles/spec.md "deferred special-case
    bosses".
===================================================================== -->

## 1. Apply-time pre-flight

- [x] 1.1 Grep `src/sprite_main.c` for each of the 4 minigame sites — confirm exact patch points + sprite IDs:
  - Digging Game (handlers at lines 931, 7772, 19407+; find the reward-grant site).
  - Hype Cave NPC (the soldier sprite; not the chests).
  - Peg Cave (hammer-pegs sprite + reward-chest open path).
  - Treasure-Chest minigame (the "pick 1 of 3" handler).
  <!-- done: all 4 located + WIRED on main. Digging player.c:6891; Chest Game dungeon.c:6072; Hammer Pegs overworld.c:3033 (HandlePegPuzzles screen 98); Hype Cave NPC sprite_main.c:25930 (NiceThiefWithGift room 0x11E). -->
- [x] 1.2 Verify Peg Cave location id is in `assets/rando/location_registry.yaml`. If missing, add append-only. <!-- done: id 218 "Hammer Pegs" present (location_registry.yaml:337). -->
- [x] 1.3 Verify Treasure-Chest minigame's 3 candidate-chest location ids are in the registry. If missing OR if only 1 exists, add the other 2 as append-only. <!-- done: fork models the chest game as the single LOC_Chest_Game rare-prize dispatch (dungeon.c:6072), not 3 slots — the runtime is a "rare prize fires once" gate, not a literal 3-chest placement. The D3 3-slot model is N/A to this reimplementation. -->
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
- [x] 2.4 Runtime boss substitution (RENDER): when a dungeon's boss room loads, spawn the assigned boss with correct GFX + spawn count. <!-- IMPLEMENTED 2026-06-06, PAUSED, then LANDED 2026-06-07 (render live, experimental, via the Enemizer pointer-redirect model — BossShuffle_RenderHomeRoom redirects sprite-data + gfx to the assigned boss's home room; the per-entry RemapSpriteType hooks were removed). The historical pause notes below are retained for context: the per-entry sprite-type swap exists at src/sprite.c (BossShuffle_RemapSpriteType + orphan-segment suppression), but slot install calls BossShuffle_Deactivate() unconditionally (rando.c ~:1929) and the Rando_ShuffleInstallSelfCheck tsc_die()s if it ever installs (rando.c ~:3523) — a pure sprite-type swap renders GARBAGE (the boss room loads the VANILLA boss's GFX sheet) AND mis-spawns formation bosses. 2026-06-07 spike verdict: the per-entry-swap is the WRONG ARCHITECTURE and is enemizer-class to fix: Armos (sprite 0x53) hard-indexes sprite slots 0-5 (Sprite_53_ArmosKnight: for j=5..0 sprite_health[j]) and counts entries via byte_7E0FF8++ per SpritePrep; Lanmolas (0x54) indexes kLanmola_InitDelay[k] at slots 0-2 — so a formation boss CANNOT be spawned from a single-entry room (and a single boss substituted into EP's 6 entries spawns 6 copies). z3randomizer asm only RESERVES space for enemizer (grep: no boss-gfx/spawn table). The correct model is a port of Enemizer's BossRandomizer (MIT, ../Enemizer/EnemizerLibrary/BossRandomizer/): per boss a BossGraphics byte (Armos=9, Lanmola=11, Moldorm=12, Helmasaur=21, Arrghus=20, Mothula=26, Blind=32, Kholdstare=22, Vitreous=22, Trinexx=23) → written to room-header byte 3 (the fork's sprite_graphics_index = hdr_ptr[3]+0x40 at dungeon.c ~:3715), PLUS a BossSpriteArray of literal {y,x,type} room entries (Armos = 6 knights + 1 trigger overlord 0x19; Lanmolas/Kholdstare/Trinexx = 3; the rest = 1) that REPLACES the room's boss sprite-data, PLUS Trinexx/Kholdstare special-room cleanup (BossRandomizer.cs:249/256). This render work can ONLY be validated by an F12 VRAM/OAM dump or playtest, which an autonomous run can't do → runtime install stays DEACTIVATED; no dormant render code committed. The native-window toggle stays a disabled "Coming soon" placeholder. -->
- [x] 2.4b Boss-kill predicate override (BEATABILITY) — the hard co-requisite for §2.4. <!-- done 2026-06-07 (kGenVer 56): new VM op OP_CAN_KILL_BOSS(dungeon) (macro CanKillBoss; op_registry id 19) resolves the dungeon's ASSIGNED boss (PredicateContext.boss_assignment, installed by Place_AssumedFill from the base seed; NULL→vanilla via kRandoDungeonVanillaBoss) then re-enters the evaluator on kRandoBossKillPred[boss] (each entry reuses the canonical CanKill<Boss> macro). The 10 shuffleable dungeons' Boss/Prize locations (Standard logic_parts + Inverted overrides) gate on CanKillBoss(<Dungeon>) instead of the inline CanKill<VanillaBoss>, so a fire/ice-gated boss can't strand a fireless-reachable dungeon's prize (corrects design.md D6). GT minibosses + pinned Agahnim 1/2 keep direct CanKill calls. boss_shuffle OFF → vanilla identity → byte-identical placement (corpus: only boss-on entries moved). Headless guards: Logic_SelfCheck (direct op resolution) + Placement_SelfCheck (boss_shuffle=1 across goals → completable + 0 unreachable) + BossShuffle_SelfCheck codegen↔C cross-check. Gated-off-safe until §2.4 render lands. -->
- [x] 2.4c Re-activate runtime install + invert ShuffleInstall self-check + enable UI toggle. <!-- done 2026-06-07: rando.c install flipped Deactivate()->Generate() + Rando_SetBossAssignment (base seed); ShuffleInstall self-check INVERTED (asserts install matches ComputeAssignment + non-passthrough render redirect + teardown passthrough); rando_window.cpp boss Checkbox live, defensive clamp dropped. Runtime-only -> corpus 110/110 byte-identical. -->
- [x] 2.4-rando-exempt The boss-kill predicate override touches no tracked g_ram cell (OP_CAN_KILL_BOSS is a pure logic-VM op + a borrowed assignment pointer); check_audit_guard.py --strict green.

## 3. Drop-pool shuffle module

- [x] 3.1 Create `src/rando/shuffle_drops.c` + `src/rando/shuffle_drops.h`. <!-- done: module exists; API = DropShuffle_Generate (install) / _ComputeAssignment (pure) / _Lookup / _Deactivate / _SelfCheck. The fork models the pool as the flat 56-entry kPrizeItems table, not NUM_TIERS DropTables — permutation over 0..55. -->
- [x] 3.2 Algorithm: per-tier drop-table permutation with heart-drop constraint per Phase A spec. <!-- done: Fisher-Yates over the 56-entry flat prize table + heart-floor constraint (shuffle_drops.c). -->
- [x] 3.3 Heart-drop guarantee: post-shuffle, verify at least one tier reachable in spheres 0-2 contains a heart drop. If violated, retry (bounded budget). <!-- done: pack 0 (flat 0..7, the heart-heavy starter pack weak overworld enemies draw from -> sphere-0 reachable) must keep >=1 heart (0xD8); bounded re-roll (16) on the same RNG stream. §3.4 sphere-ordering is N/A — enemy->pack binding is static, not sphere-indexed (documented). -->
- [x] 3.4 Wire into runtime sprite-drop path: existing `Sprite_DropItem` logic consults the shuffled drop tables when `kFeatures1_RandomizerActive && drop_pool_shuffle`. <!-- done: ForcePrizeDrop (sprite.c:3009) calls DropShuffle_Lookup; install happens at slot load (Rando_ActivateSidecarSlot). Lookup is passthrough until installed. -->
- [x] 3.5 Forward-fill fallback: if heart-drop guarantee retries exhaust, fall back to identity drop-pool with a spoiler `fallback_warnings` entry. <!-- done: out_used_fallback flag -> spoiler `drop_heart_floor_fallback` entry (JSON fallback_warnings + text WARNINGS). -->

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
- [x] 5.4 Settings-screen widget: per-toggle. <!-- done: live EXPERIMENTAL toggles in the PC native settings window ("Shuffles (experimental)", rando_window.cpp) with caveat tooltips. The in-game SNES screen is compiled out on PC; PC rando settings live in the ImGui window. -->

## 6. Spoiler integration

- [x] 6.1 Boss assignments: JSON spoiler emits `boss_assignments` mapping. Text spoiler under a `Boss Assignments` section. <!-- done: JSON `boss_assignments` array of {dungeon, dungeon_name, boss, boss_name}; text BOSS ASSIGNMENTS section. rando_spoiler.c. -->
- [x] 6.2 Drop-pool: JSON spoiler emits `drop_tables`. Text spoiler under a `Drop Tables` section. <!-- done: JSON `drop_tables` = 7 packs × 8 resolved drop item ids; text DROP TABLES section. -->
- [~] 6.3 Treasure-Chest minigame annotation: each of the 3 slots gets `"choice_group": "treasure_chest"` in its JSON entry. <!-- N/A: the fork models the chest game as a single LOC_Chest_Game rare-prize dispatch (dungeon.c:6072), not 3 placement slots, so there is no 3-slot choice group to annotate. -->
- [x] 6.4 When the shuffle is disabled (`boss_shuffle=false` etc.), omit the corresponding spoiler sections. <!-- done: boss_assignment/drop_map pointers are NULL when off -> sections omitted (verified: OFF seed has neither key). -->

## 7. Determinism + CI

- [x] 7.1 Bump `kGeneratorVersion`. <!-- done: 48->49 with rationale (rando.h). -->
- [x] 7.2 Regenerate corpus. Add at least 4 boss-shuffle seeds (across goals) + 4 drop-pool-shuffle seeds + 2 both-on seeds. <!-- done: 10 entries added (4 boss / 4 drop / 2 both-on); corpus now 79/79 OK. -->
- [x] 7.3 Verify default-settings digests remain byte-identical to pre-change baseline. <!-- done: bump_rando_corpus --apply reported 0 digest changes across all 69 existing entries (boss/drop orthogonal to placement). -->
- [x] 7.4 Verify minigame-dispatch wiring doesn't change default-settings digests. <!-- done: all 4 minigame dispatches gate on RandomizerActive; default corpus byte-identical (the dispatch sites landed earlier; this run did not touch them). -->

## 8. Audit-guard

- [x] 8.1 Run `assets/scripts/check_audit_guard.py` after wiring each minigame site. <!-- done: check_audit_guard.py --strict green (29 files, no non-exempt writes). -->
- [x] 8.2 Confirm boss-runtime-substitution doesn't write to tracked inventory cells. <!-- done: BossShuffle_RemapSpriteType only rewrites sprite_type[k] (the spawned boss sprite); no link_item_* / tracked-cell writes. DropShuffle_Lookup only remaps a prize-table index. -->

## 9. Documentation

- [x] 9.1 Update `docs/randomizer.md` settings reference: document `boss_shuffle=` and `drop_shuffle=` axes. <!-- done: added to the Settings reference table + a 48->49 bump case study. -->
- [x] 9.2 Add a "Shuffle modules" subsection covering boss + drop-pool behavior. <!-- done: "Boss & drop shuffle (experimental)" subsection incl. the boss beatability-limitation warning + the drop heart-floor explanation. -->
- [ ] 9.3 Cross-link this change from the `openspec/changes/` index (README.md). <!-- the change folder's own README.md exists; the openspec changes index cross-link is deferred to archive time (openspec archive updates indexes). -->

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
