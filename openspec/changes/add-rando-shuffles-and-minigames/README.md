# add-rando-shuffles-and-minigames

Phase B Slice 7 + Slice 8. Activates boss shuffle (`§7.1`) + drop-pool shuffle (`§7.2`) (both currently grayed out per Phase A) AND wires the 4 unwired §6.8 minigame dispatch sites (digging game, Hype Cave NPC, peg cave, treasure-chest minigame). Bundled because all three workstreams touch `src/sprite_main.c`.

## Status

**Proposal-only stub.** Authored: 2026-05-26. Specs deltas + `tasks.md` deferred to `/openspec-explore` + `/openspec-propose` at apply-time.

**Stub-only because**: each minigame site needs `src/sprite_main.c` grep to confirm the exact patch point; boss-shuffle's goal-required-boss exception list needs ALTTPR PHP grep; drop-pool's heart-drop-guarantee algorithm needs a prototype to settle.

## Read these in order

| File | Purpose | Status |
|---|---|---|
| [proposal.md](proposal.md) | Why, what changes, capabilities, impact | ✅ authored |
| [specs/randomizer-shuffles/spec.md](specs/randomizer-shuffles/spec.md) | Boss + drop-pool un-defer | 🪵 minimal stub deltas |
| [specs/randomizer-placement/spec.md](specs/randomizer-placement/spec.md) | 4 minigame dispatch sites | 🪵 minimal stub deltas |
| `design.md` | Boss-shuffle pool + heart-drop guarantee algorithm + Peg Cave location-id decision | ⏳ deferred (apply-time) |
| `tasks.md` | Implementation checklist | ⏳ deferred (apply-time) |

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

- `../alttp_vt_randomizer/app/Boss.php` (boss-shuffle pool — source-line range TBD at apply-time)
- `../alttp_vt_randomizer/app/EnemyDrop.php` (drop-pool — source-line range TBD)
- `../alttp_vt_randomizer/app/Region/Standard/...` and `app/Region/Open/...` for the minigame-site placement predicates.

## Dependencies

- **Phase A archived first.** Deltas multiple specs post-archive.
- **Helpful: #2 trackers** for boss-shuffle visualization. Not strict.
- **Independent of #4a/4b world-states.**

## When work starts

1. `/openspec-explore add-rando-shuffles-and-minigames` — grep `src/sprite_main.c` for each minigame patch site; grep ALTTPR for boss-pool definition + drop-pool tier definitions; verify Peg Cave location id.
2. `/openspec-propose` to finalize spec deltas + design.md (boss-pool, heart-drop guarantee algorithm).
3. `/openspec-apply` to walk through tasks.
4. `/openspec-archive` when done.
