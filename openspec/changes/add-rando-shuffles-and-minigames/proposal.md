## Why

Phase A's `randomizer-shuffles` spec scaffolds five shuffle modules — prize, medallion, boss, entrance, drop-pool, cosmetic — with prize + medallion shipped and the rest deferred. Phase A1 finalized prize shuffle + medallion shuffle (per `Phase A1 status` in `docs/randomizer.md:7-23`). **Boss shuffle + drop-pool shuffle remain Phase B work** per `tasks.md §7.1` and `§7.2`.

§6.8 minigame dispatch is the matching placement-side gap. Phase A1's audit confirmed (`audit_phase_a1.md:65-66`): "§6.8 minigame sites (digging, hype cave, peg cave, treasure chest minigame) are the Phase B follow-on per task 71." Grep against `src/sprite_main.c` and `src/rando/chest_lookup.h` confirms:
- **Hype Cave chests** (4 of them) are already wired via the §6.3 universal chest hook (rows 203-206 of `chest_lookup.h`).
- **Digging Game** (`LOC_Digging_Game = 228`), **Hype Cave NPC** (`LOC_Hype_Cave_NPC = 227`), **Peg Cave**, and **Treasure-Chest minigame** are defined location IDs but NOT instrumented — vanilla code paths still grant their inline items.

This change bundles boss + drop shuffles + the minigame dispatch because all three workstreams touch the same surface area: sprite handlers in `src/sprite_main.c`. Per `docs/randomizer_phase_b_chunking.md`, bundling preserves authorship efficiency (one focused PR rather than three).

## What Changes

### §7.1 Boss shuffle

- **New module**: `src/rando/shuffle_boss.c` + `src/rando/shuffle_boss.h`.
- **Randomize boss-room assignments** from a configurable pool. Dungeon → reward binding stays stable (EP's boss-prize stays with EP regardless of which boss is in the room). Goal-required bosses (Agahnim 1, Agahnim 2, Ganon) stay at their canonical slots.
- ALTTPR provenance: `../alttp_vt_randomizer/app/Boss.php`. PHP source-line range to be recorded in `audit.md` §"Boss-shuffle provenance" at apply-time.
- **Settings axis**: `boss_shuffle` (boolean, default false). When false, boss assignments are vanilla; the dispatcher still fires for uniformity.

### §7.2 Drop-pool shuffle

- **New module**: `src/rando/shuffle_drops.c` + `src/rando/shuffle_drops.h`.
- **Randomize sprite-death drop tables** (which drops fall from which enemies).
- **Heart-drop guarantee**: low-HP zones still drop at least some hearts; the shuffle preserves a minimum heart-drop rate so the player doesn't get HP-starved at the start.
- **Post-sphere ordering**: drop-pool runs AFTER item placement so sphere data is available; uses sphere data to gate aggressive shuffles (e.g., don't dry up hearts in the player's sphere 0 location set).
- ALTTPR provenance: `../alttp_vt_randomizer/app/EnemyDrop.php`. PHP source-line range to be recorded at apply-time.
- **Settings axis**: `drop_pool_shuffle` (boolean, default false).

### §6.8 Minigame dispatch

- **Wire dispatch at 4 minigame sites**:
  - **Digging Game** (`LOC_Digging_Game = 228`) — `SpritePrep_DiggingGameGuy_bounce` and related handlers at `src/sprite_main.c:931, 7772, 19407+`. Dispatch needs to fire when the player wins a dig.
  - **Hype Cave NPC** (`LOC_Hype_Cave_NPC = 227`) — the soldier NPC, not the 4 chests (chests already wired via §6.3).
  - **Peg Cave** — hammer pegs reward chest opening (location ID needs verification at apply-time — may need to be added to `assets/rando/location_registry.yaml`).
  - **Treasure-Chest minigame** — the "pick one of three chests" game in Village of Outcasts. Special handling needed because the game has 3 candidate chests but only 1 reward.
- Per `audit_phase_a1.md:65-66`: each is a non-standard pickup site needing per-site instrumentation; none blocking Phase A1's seed generation or playability.
- `kGeneratorVersion` advances; locations may be added; corpus regenerates.

## Capabilities

### New Capabilities

(none)

### Modified Capabilities

- `randomizer-shuffles`: MODIFIED Requirements on the Phase A boss-shuffle and drop-pool-shuffle entries — un-defer them; spec the contract for each shuffle module's runtime behavior, the goal-required boss exception list, and the heart-drop guarantee.
- `randomizer-placement`: ADDED Requirements for the 4 minigame dispatch sites — each enumerated with its `LOC_<id>` and the sprite-handler patch site.

## Impact

- **New modules**: `src/rando/shuffle_boss.{c,h}`, `src/rando/shuffle_drops.{c,h}`.
- **Wiring into placement**: post-`Place_AssumedFill` pass for both shuffles (they run AFTER item placement).
- **Sprite handlers**: 4 minigame sites in `src/sprite_main.c` — per-site instrumentation per the audit-discovered code paths.
- **Settings struct**: `boss_shuffle` + `drop_pool_shuffle` bool axes. Both un-pinned from `0`.
- **Effort**: **2-3 weeks of focused work.** Boss-shuffle is medium; drop-pool adds another week because of the post-sphere ordering; minigame dispatch is per-site discovery + instrumentation work.
- **Regression risk**: `kGeneratorVersion` bumps; corpus regenerates. Default-settings seeds (boss_shuffle=false, drop_pool_shuffle=false) should remain byte-identical in `placement_digest_hex`.
- **Dependency on Slice 1 trackers**: helpful for visualizing boss-shuffle (player tracks which boss is in which dungeon) and Slice 4 trick logic (some tricks depend on which boss is where). Not strict.

## Status (stub)

This is a **proposal-only stub**. Specs deltas and `tasks.md` are deferred to `/openspec-explore` + `/openspec-propose` at apply-time.

**Stub-only because**: each of the 4 minigame sites needs grep against `src/sprite_main.c` to confirm the exact patch point; boss-shuffle's goal-required-boss exception list needs ALTTPR PHP grep; drop-pool shuffle's heart-drop guarantee algorithm needs prototyping.

Read the [README.md](README.md) for the stub's status.
