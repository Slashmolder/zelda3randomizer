## Context

Phase A drafted boss-shuffle + drop-pool-shuffle contracts at `randomizer-shuffles/spec.md:97-119` (both marked "Phase B"). Phase A1 audit confirmed `§6.8` minigame sites (digging game, Hype Cave NPC, peg cave, treasure-chest minigame) are non-instrumented Phase B follow-ons per `audit_phase_a1.md:65-66`. Grep at chunking time confirmed:

- Hype Cave **chests** (4) already wired via §6.3 universal chest hook (`src/rando/chest_lookup.h:203-206`).
- **Digging Game** (`LOC_Digging_Game = 228`): sprite handlers at `sprite_main.c:931, 7772, 19407+` — NOT instrumented.
- **Hype Cave NPC** (`LOC_Hype_Cave_NPC = 227`) — NOT instrumented.
- **Peg Cave** — location id may not exist in registry; verify at apply-time.
- **Treasure-Chest minigame** — special handling (3 candidate chests, 1 reward).

This change bundles all three workstreams because they share `src/sprite_main.c` surface.

### Status (2026-05-27 post-implementation)

- Boss + drop shuffle algorithms + per-site sprite-handler instrumentation: **landed** (`72c3a93`, `d4b3b7d`).
- `boss_shuffle` / `drop_shuffle` settings axes in canonical serialization: **landed** (`4d9d85e`, kGenVer 14).
- §6.8 minigame dispatch: **2 of 4 sites wired** (`e97669a`):
  - ✅ Digging Game — `player.c:6725` in `DiggingGameGuy_AttemptPrizeSpawn` case 4.
  - ✅ Treasure-Chest minigame — `dungeon.c:5950` in `OpenMiniGameChest` t==7 branch.
  - ⏳ Hype Cave NPC (#78) — sprite handler not located in the
    reimplementation. ALTTPR uses `Location\Npc("Hype Cave - NPC",
    [0x180011])` (`app/Region/Standard/DarkWorld/South.php:37`). The
    ROM-byte-patch at 0x180011 has no corresponding sprite-handler entry
    in this fork's `kSpriteHandlers[]` table (`sprite_main.c:480-716`).
    Candidates checked + rejected: Sprite_28_DarkWorldHintNPC (storyteller,
    not item-grant), Sprite_BC_Drunkard, Sprite_C8_BigFairy, Cukeman.
    **Next step**: `git log -S "0x180011"` against any vendored asar
    patches; audit `overlord.c` for an Hype-Cave overlord handler. The
    vanilla item is `Rupee100` (not "Bee in a Bottle" as the original
    task brief claimed — verified against `location_registry.yaml:350`).
  - ⏳ Hammer Pegs (#79) — `LOC_Hammer_Pegs = 218`, vanilla PoH (lttp
    code 0x26). ALTTPR uses `Location\Standing("Hammer Pegs",
    [0x180006])`. The reimplementation's hammer-peg tile handler
    (`dungeon.c:1021-1085`, `RoomDraw_HammerPegSingle`) is tile-rendering
    only; no `RoomTag_*` handler in `dungeon.c:205-269` is wired to the
    Peg-Cave room. **Next step**: locate the post-all-pegs-hammered
    heart-piece spawn (likely an overlord or sprite handler not located
    by the discovery agent), OR document as "unreachable in fork; falls
    back to vanilla" if the grant logic was elided in the reimplementation.

Audit M1 fix (`d4b3b7d`) landed alongside: orphan boss-segment suppression for Trinexx arms (0xCC/0xCD) + KholdstareShell (0xA3). Cluster-audit follow-ups (LOW-3 Agahnim reverse-map poison, LOW-4 load-order assumption, LOW-5 confirmation truncation defense) landed in `bbf7ea9`.

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

> **Superseded by D7 (as-built):** the shuffleable pool is **7**, not 10. Blind,
> Kholdstare, and Trinexx are also pinned — they depend on home-room environment the
> runtime redirect can't supply. See D7 for the per-boss requirements to un-pin them.

### D2: Drop-pool tiers

ALTTPR's drop pool is **not** in an `app/EnemyDrop.php` (that file does not exist). It is modelled by `app/Drops/PrizePack.php` (61) + `app/Drops/PrizePackSlot.php` (60), with the pack **roster** declared in `app/World.php:76-87`: **11 prize packs / 63 slots** — 7 numbered enemy-drop packs `'0'..'6'` (8 slots each) plus the special packs `'pull'` (3), `'crab'` (2), `'stun'` (1), `'fish'` (1). The droppable sprite ids live in `app/Sprite.php` (229 `new Sprite(...)` entries). The shuffle randomizes which prize sprites fill each pack's slots.

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

Phase A's `CanKillMostThings` macro (etc.) uses boss-class identity macros, not per-boss IDs. So boss shuffle doesn't break *general-enemy* predicates: the "can kill most things" macro asks for the right items regardless of which dungeon a boss is in.

**Original decision (CONTRADICTED by the as-built — see correction below)**: no predicate changes required. Boss shuffle is purely a runtime-substitution; the logic graph stays vanilla.

**Correction (2026-06-07, boss-shuffle runtime workstream 2).** The original D6 reasoning was wrong for the *per-dungeon boss-kill* gate. Each dungeon's `"- <Dungeon> - Boss"` (and `- Prize`, which gates on Boss access) location is gated on its VANILLA boss's kill predicate — `Eastern Palace - Boss` requires `CanKillArmosKnights`, `Turtle Rock - Boss` requires `CanKillTrinexx` (FireRod+IceRod), etc. (`assets/rando/logic_parts/*`). The moment runtime substitution is active, a dungeon whose boss got shuffled is still gated on the *wrong* boss's kill predicate — e.g. Kholdstare (needs a fire source) shuffled into a fireless-reachable dungeon, but gated as if it still held the easy vanilla boss → **strand**. So per-seed predicate changes ARE required for beatability-safe boss shuffle.

As built (kGeneratorVersion 56): a new VM op **`OP_CAN_KILL_BOSS(dungeon_id)`** (macro `CanKillBoss(<Dungeon>)`, `assets/rando/op_registry.yaml` id 19) resolves the dungeon's *currently assigned* boss from the per-seed boss assignment (`PredicateContext.boss_assignment`, installed by the placer from the base seed in `Place_AssumedFill`; NULL→vanilla via `kRandoDungeonVanillaBoss`) and re-enters the evaluator on that boss's kill predicate (`kRandoBossKillPred[]`, each entry reusing the canonical `CanKill<Boss>` macro). The 10 shuffleable dungeons' Boss/Prize locations (Standard + Inverted) were rewired from the inline `CanKill<VanillaBoss>` to `CanKillBoss(<Dungeon>)`. With `boss_shuffle` off the assignment is the vanilla identity, so placement is byte-identical (corpus: only the boss-on entries move). GT's internal miniboss gauntlet + the pinned Agahnim 1/2 keep their direct `CanKill<Boss>` calls (never shuffled). This logic is **landed and gated-off-safe** even though the runtime *sprite* substitution stays deactivated (see §2.4 / the boss-runtime spike) — it is correct and ready for when the GFX/spawn work lands. Headless guard: `Logic_SelfCheck` (direct op resolution) + `Placement_SelfCheck` (boss_shuffle=1 seeds across goals are completable with 0 unreachable).

### D7: Runtime render (as-built) + special-case bosses deferred

**Correction to D1 (2026-06-07, boss-shuffle runtime workstream 1).** D1's "10-boss
permutation" is superseded: the shuffleable pool is **7** bosses. The runtime render
is the **Enemizer pointer-redirect model** (`../Enemizer/EnemizerLibrary/BossRandomizer/`,
MIT) — for a shuffled boss room the engine loads the *assigned* boss's vanilla HOME
boss-room data instead of this room's:

- **sprite list** (`Dungeon_LoadSprites`) — the boss's full formation + trigger
  overlords, so spawn-count is correct (Armos = 6 entries, Lanmolas = 3, etc.);
- **sprite-graphics index** (room-header load, `hdr[3]+0x40`) — correct tiles;
- **sprite palette** (`palette_sp0l/sp5l/sp6l` from `kDungPalinfos[home_hdr[1]]`) —
  correct colors; the room BG palette (`palette_main_indoors`) stays this room's;
- **coordinate alignment** — the formation is shifted by `(this dungeon's vanilla-boss
  anchor) − (the assigned boss's home-room anchor)` so it spawns where the dungeon's
  own boss did (reachable) instead of at the home room's coords (which can land behind
  a wall / one screen over).

Off / non-boss-room → 0xFFFF redirect → byte-identical to vanilla. **Seven bosses
render + fight correctly with this pure redirect** (playtest-confirmed): Armos Knights,
Lanmolas, Moldorm, Helmasaur King, Arrghus, Mothula, Vitreous. They shuffle across
EP, DP, ToH, PoD, SP, SW, MM.

**Three bosses CANNOT use the pure redirect and are PINNED to their home dungeon**
(kGeneratorVersion 56→57 Blind, 57→58 Kholdstare+Trinexx). Each depends on home-room
*environment* (a maiden sequence, a room "effect", a BG2 object) that the
sprite/gfx/palette redirect does not carry. To make any of them shuffleable, a
follow-up must additionally supply that environment, in BOTH directions (the boss into
another room, AND another boss into this boss's home room). Per-boss requirements
(grounded in F12 dumps + the fork code + Enemizer):

1. **Blind — Thieves' Town (dungeon 8).** TT's boss room has **no Blind sprite
   (0xCE)**. Blind is produced by a maiden-follower sequence: the player rescues the
   maiden (`0xB7` `SpritePrep_BlindMaiden`) earlier in TT; she follows Link into the
   boss room; a TT trigger sets `dung_savegame_state_bits & 0x2000`; and
   `Sprite_CE_Blind` / `SpritePrep_Blind_PrepareBattle` only materializes the boss
   when that bit is set (otherwise it sets `sprite_state = 0` and despawns).
   Playtest-confirmed strand: Blind → Eastern Palace = empty room, no killable boss.
   **Required to un-pin:**
   - *Forward (Blind into another dungeon):* inject a synthetic `0xCE` Blind at the
     dest room's boss anchor (Enemizer `BossSpriteArray {0x05,0x09,0xCE}`) **and**
     force `dung_savegame_state_bits |= 0x2000` on boss-room entry so PrepareBattle
     takes the spawn branch (Enemizer `RemoveBlindSpawnCode`), with `follower_indicator
     != 6` (no maiden present).
   - *Reverse (another boss into TT):* suppress the maiden so she doesn't follow into
     TT's boss room and transform on top of the substituted boss — remove the `0xB7`
     from her basement spawn when TT's assigned boss ≠ Blind (Enemizer
     `RemoveMaidenFromThievesTown`).

2. **Kholdstare — Ice Palace (dungeon 9).** The redirect *does* bring the shell sprite
   (`0xA3`), Kholdstare (`0xA2`), and sub-parts (`0xA4`), but the encase-and-melt
   fight needs IP's room **environment**: the room "effect" byte
   (`dung_hdr_collision_2` = `$00AD` = header byte 4) **plus** a BG2 ice-block object.
   Playtest-confirmed (F12, Kholdstare → Desert Palace): shell present but **un-encased**
   because `$00AD` was DP's `0`, not IP's, so the freeze sequence never initialized and
   the fire-rod-melt couldn't proceed. **Required to un-pin:**
   - Set the dest room's "effect" (`$00AD` / header byte 4) to IP's value (Enemizer
     writes header byte 4 = `01`).
   - Add/move the BG2 ice-block object into the dest room (Enemizer
     `AddShellAndMoveObjectData` + header byte 0 BG2 properties).
   - Verify the `0xA3`↔`0xA2`↔`0xA4` state machine initializes correctly against the
     foreign room's geometry.
   - Enemizer ref: `BossRandomizer.cs:238-260`.

3. **Trinexx — Turtle Rock (dungeon 11).** Same class as Kholdstare: needs TR's room
   "effect" + BG2 object (the lava/ice floor + rock setup). NOT individually
   playtest-confirmed; pinned **by association** — Enemizer special-cases it with the
   identical mechanism (`AddShellAndMoveObjectData` + header bytes, `BossRandomizer.cs:226-235`).
   **Required to un-pin:** as Kholdstare, with TR's shell object id + effect value.

**General gate for un-pinning any of the three:** the redirect must additionally carry
the home room's relevant HEADER bytes (`$00AD` effect, BG2 properties) and/or inject
room OBJECTS — room-environment surgery whose only validation is end-to-end **playtest**
(no headless test covers boss rendering or fight mechanics — the corpus + `--rando-selftest`
cover only placement/predicate). Until that lands, the supported set is the 7-boss
shuffle; `BossShuffle_SelfCheck` asserts the three (plus Agahnim 1/2) stay pinned and
the 7 shuffleable dungeons hold a permutation of the 7-boss pool.

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
