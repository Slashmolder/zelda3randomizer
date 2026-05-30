# add-rando-shuffles-and-minigames

Phase B Slice 7 + Slice 8. Activates boss shuffle (`§7.1`) + drop-pool shuffle (`§7.2`) (both currently grayed out per Phase A) AND wires the 4 unwired §6.8 minigame dispatch sites (digging game, Hype Cave NPC, peg cave, treasure-chest minigame). Bundled because all three workstreams touch `src/sprite_main.c`.

## Status

**Fully authored.** Authored: 2026-05-26. Promoted with design.md (boss-shuffle pool composition, drop-pool tiers, Treasure-Chest minigame slot handling, post-sphere ordering decision).

## Read these in order

| File | Purpose | Status |
|---|---|---|
| [proposal.md](proposal.md) | Why, what changes, capabilities, impact | ✅ authored |
| [design.md](design.md) | Boss pool + drop-pool tier + Treasure-Chest slot grouping + post-sphere ordering | ✅ authored |
| [specs/randomizer-shuffles/spec.md](specs/randomizer-shuffles/spec.md) | Boss + drop-pool un-defer | ✅ authored |
| [specs/randomizer-placement/spec.md](specs/randomizer-placement/spec.md) | 4 minigame dispatch sites | ✅ authored |
| [tasks.md](tasks.md) | Implementation checklist (11 sections, ~45 tasks) | ✅ authored |

## Effort

**2-3 weeks of focused work.** Boss-shuffle is medium; drop-pool adds another week for post-sphere ordering; minigame dispatch is per-site discovery + instrumentation work.

## Verified findings at chunking time

Per grep against `src/sprite_main.c` + `src/rando/chest_lookup.h`:
- ✅ Hype Cave **chests** (4) are already wired via §6.3 universal chest hook (`chest_lookup.h:203-206`).
- ⏳ Digging Game (`LOC_Digging_Game = 228`): handlers at `sprite_main.c:931, 7772, 19407+` — NOT dispatched.
- ⏳ Hype Cave NPC (`LOC_Hype_Cave_NPC = 227`) — NOT dispatched.
- ⏳ Peg Cave — location id may not exist in `location_registry.yaml`; verify at apply-time.
- ⏳ Treasure-Chest minigame — special handling needed (3 candidate chests, 1 reward).

## Key upstream references

- `../alttp_vt_randomizer/app/Boss.php:68-128` (boss-shuffle pool — 12 bosses; see `audit.md §"Boss-shuffle provenance"`)
- drop-pool: `../alttp_vt_randomizer/app/Drops/PrizePack.php` + `PrizePackSlot.php` + roster `app/World.php:76-87` + sprite table `app/Sprite.php` (NOT `app/EnemyDrop.php`, which does not exist; see `audit.md §"Drop-pool provenance"`)
- `../alttp_vt_randomizer/app/Region/Standard/...` and `app/Region/Open/...` for the minigame-site placement predicates.

## Dependencies

- **Phase A archived first.** Deltas multiple specs post-archive.
- **Helpful: #2 trackers** for boss-shuffle visualization. Not strict.
- **Independent of #4a/4b world-states.**

## When work starts

1. `/openspec-apply add-rando-shuffles-and-minigames` — Section 1 (pre-flight grep) settles per-site patch points first.
2. Author per-shuffle module independently (boss first → drop-pool); minigame dispatch sites can interleave.
3. `/openspec-archive` when done.
