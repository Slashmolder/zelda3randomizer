# add-rando-shuffles-and-minigames

Phase B Slice 7 + Slice 8. Activates boss shuffle (`§7.1`) + drop-pool shuffle (`§7.2`) (both currently grayed out per Phase A) AND wires the 4 unwired §6.8 minigame dispatch sites (digging game, Hype Cave NPC, peg cave, treasure-chest minigame). Bundled because all three workstreams touch `src/sprite_main.c`.

## Status

**Archived 2026-06-14.** Boss shuffle, drop-pool shuffle, and the §6.8
minigame dispatch sites are shipped. See `tasks.md` and the archived spec
deltas for the final as-built scope and deferred follow-ups.

## Read these in order

| File | Purpose | Status |
|---|---|---|
| [proposal.md](proposal.md) | Why, what changes, capabilities, impact | ✅ authored |
| [design.md](design.md) | Boss pool + drop-pool tier + Treasure-Chest slot grouping + post-sphere ordering | ✅ authored |
| [specs/randomizer-shuffles/spec.md](specs/randomizer-shuffles/spec.md) | Boss + drop-pool un-defer | ✅ authored |
| [specs/randomizer-placement/spec.md](specs/randomizer-placement/spec.md) | 4 minigame dispatch sites | ✅ authored |
| [tasks.md](tasks.md) | Implementation checklist (11 sections, ~45 tasks) | ✅ complete / archived |

## Effort

Complete; historical estimate retained only in git history.

## Historical findings at chunking time

These were the pre-implementation findings used to start the change; see
`tasks.md` and the archived specs for final shipped behavior.

Per grep against `src/sprite_main.c` + `src/rando/chest_lookup.h`:
- ✅ Hype Cave **chests** (4) are already wired via §6.3 universal chest hook (`chest_lookup.h:203-206`).
- ⏳ Digging Game (`LOC_Digging_Game = 228`): handlers at `sprite_main.c:931, 7772, 19407+` — NOT dispatched.
- ⏳ Hype Cave NPC (`LOC_Hype_Cave_NPC = 227`) — NOT dispatched.
- ⏳ Peg Cave — location id may not exist in `location_registry.yaml`; verify at apply-time.
- ⏳ Treasure-Chest minigame — special handling needed (3 candidate chests, 1 reward).

## Key upstream references

- `../alttp_vt_randomizer/app/Boss.php:68-128` (boss-shuffle pool — 12 bosses)
- drop-pool: `../alttp_vt_randomizer/app/Drops/PrizePack.php` + `PrizePackSlot.php` + roster `app/World.php:76-87` + sprite table `app/Sprite.php` (NOT `app/EnemyDrop.php`, which does not exist)
- `../alttp_vt_randomizer/app/Region/Standard/...` and `app/Region/Open/...` for the minigame-site placement predicates.

## Dependencies

- **Phase A archived first.** Deltas multiple specs post-archive.
- **Helpful: #2 trackers** for boss-shuffle visualization. Not strict.
- **Independent of #4a/4b world-states.**

## Historical apply notes

This change has already been applied and archived. The original apply plan is
retained in git history rather than repeated here as current instruction.
