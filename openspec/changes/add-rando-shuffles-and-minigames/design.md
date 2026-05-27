## Context

Phase A drafted boss-shuffle + drop-pool-shuffle contracts at `randomizer-shuffles/spec.md:97-119` (both marked "Phase B"). Phase A1 audit confirmed `§6.8` minigame sites (digging game, Hype Cave NPC, peg cave, treasure-chest minigame) are non-instrumented Phase B follow-ons per `audit_phase_a1.md:65-66`. Grep at chunking time confirmed:

- Hype Cave **chests** (4) already wired via §6.3 universal chest hook (`src/rando/chest_lookup.h:203-206`).
- **Digging Game** (`LOC_Digging_Game = 228`): sprite handlers at `sprite_main.c:931, 7772, 19407+` — NOT instrumented.
- **Hype Cave NPC** (`LOC_Hype_Cave_NPC = 227`) — NOT instrumented.
- **Peg Cave** — location id may not exist in registry; verify at apply-time.
- **Treasure-Chest minigame** — special handling (3 candidate chests, 1 reward).

This change bundles all three workstreams because they share `src/sprite_main.c` surface.

## Goals / Non-Goals

**Goals**:
- Boss shuffle activated with goal-required-boss exception list.
- Drop-pool shuffle activated with heart-drop early-game guarantee.
- 4 minigame dispatch sites wired.
- Default-disabled (both shuffle flags + minigame dispatch is identity-placed for non-Retro / non-shuffle seeds — preserves default-settings digest).
- `kGeneratorVersion` bumps; corpus regenerates.

**Non-Goals**:
- Cosmetic shuffles (Phase D `add-rando-cosmetic-shuffles`).
- Entrance shuffle (Phase C `add-rando-entrance-shuffle`).
- Shop-related dispatch (Phase B #4b Retro covers shops).
- Custom drop tables outside the 8 ALTTPR tiers (Phase D may extend).

## Decisions

### D1: Boss-shuffle pool composition

ALTTPR's `app/Boss.php` defines the boss pool. Per Phase A spec scenario "Required boss is preserved" + Phase A draft:

- **Always-pinned**: Agahnim 1 (Hyrule Castle Tower) for Standard mode goals; Agahnim 2 (Ganon's Tower top) for Defeat Ganon / Fast Ganon goals; Ganon (Pyramid).
- **Shuffleable**: Helmasaur King, Lanmolas, Moldorm, Arrghus, Mothula, Blind, Kholdstare, Vitreous, Trinexx, Armos Knights (10 bosses).

**Decision**: 10-boss permutation. Agahnim 1/2 + Ganon stay at their slots regardless of `boss_shuffle == true`.

### D2: Drop-pool tiers

ALTTPR's `app/EnemyDrop.php` defines 8 tiered drop tables. The shuffle randomizes which items each tier contains.

**Decision**: per-tier drop-table permutation. Heart-drop guarantee per Phase A spec: at least one tier reachable in spheres 0-2 contains a heart entry. Apply-time prototype settles the algorithm — likely a constraint-based generation that retries the permutation if the heart-drop constraint fails.

### D3: Minigame dispatch — Treasure-Chest minigame

The Treasure-Chest minigame at Village of Outcasts has 3 candidate chests; the player picks one and the dispatch fires for the picked chest's `LOC_<id>`.

**Decision**: each of the 3 candidate chests gets its own `LOC_<...>` id (`LOC_TreasureChestMinigame_Slot1`, `_Slot2`, `_Slot3`). The pick-handler sprite logic dispatches only the picked slot; the other two are not dispatched in that play-through (they appeared in the placement table but the player just didn't pick them this time).

The placement table still includes all 3 slots; player will see different choices on subsequent plays of the same seed (though they only get 1 per playthrough).

### D4: Peg Cave location id

The hammer-pegs cave reward chest may or may not have an existing `LOC_<...>` in Phase A's registry. Verify at apply-time. If missing, add as an append-only location.

### D5: Post-sphere ordering for drop-pool

Per Phase A spec scenario "Drop-pool runs after item placement": drop-pool shuffle runs AFTER `Place_AssumedFill` so sphere data is available. The shuffle algorithm consults sphere data to enforce the heart-drop guarantee.

**Decision**: implementation order in the generation pipeline:
1. Prize shuffle (Phase A).
2. Medallion shuffle (Phase A).
3. `Place_AssumedFill` (Phase A) — placement table populated.
4. Sphere computation (Phase A) — sphere data available.
5. **Boss shuffle** (Phase B #7) — permutes boss assignments; doesn't affect placement.
6. **Drop-pool shuffle** (Phase B #7) — permutes drop tables; consults sphere data for constraint check.

### D6: Boss-shuffle predicate interaction

Phase A's `CanKillMostThings` macro (etc.) uses boss-class identity macros, not per-boss IDs. So boss shuffle doesn't break existing predicates: the "can kill Helmasaur King" macro asks for the right items regardless of which dungeon Helmasaur is in.

**Decision**: no predicate changes required. Boss shuffle is purely a runtime-substitution; the logic graph stays vanilla.

## Risks / Trade-offs

| Risk | Mitigation |
|---|---|
| Boss shuffle moves Helmasaur to dungeon without bomb access → un-completable | Goal-completability check (Phase A) runs against post-shuffle assignments; refusal fires if any required dungeon is unreachable post-shuffle |
| Drop-pool heart-drop guarantee fails for hard seeds | Constraint-retry loop in the shuffle algorithm; if exhausted, fall back to identity drop-pool with a fallback_warning |
| Treasure-Chest minigame UX confusion (player sees 3 items in spoiler but only gets 1) | Spoiler annotates the minigame slots: `"choice_group": "treasure_chest"` marks the 3 slots as a single choice |
| Minigame dispatch sites have surprising sprite-handler behavior | Per-site grep at apply-time; instrument carefully |

## Migration Plan

No user data migration. Default-settings slots (boss_shuffle=false, drop_pool_shuffle=false) remain valid. New Phase B seeds with shuffles on get the runtime-substituted state.

## Open Questions

1. Treasure-Chest minigame UX: should the 3 chests in the placement table be marked specially in the JSON spoiler so external tools know they're a "pick 1 of 3" group? **Recommendation**: yes, annotate.
2. Peg Cave location id: present in registry? Apply-time verify.
3. Drop-pool tier authoring: 8 ALTTPR-canonical tiers or fewer? Verify at apply-time.
4. Drop-pool shuffle and Retro shop interaction — when Retro is active, do shop "drops" (sub-purchase rewards) participate? Probably not; orthogonal.
