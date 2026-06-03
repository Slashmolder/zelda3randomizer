# add-rando-shuffles-and-minigames — audit

Provenance + grounding notes, grounded against the sibling `../alttp_vt_randomizer/`
checkout (license MIT). Per `CLAUDE.md` claim-grounding discipline: source over
memory. **Upstream path correction noted below — task 1.4 cited a file that does
not exist.**

---

## Boss-shuffle provenance

**Task 1.4 deliverable (boss half).** Source: `app/Boss.php` (185 lines). The boss
roster is the `BossCollection` built in `Boss::all()` at `app/Boss.php:68-128`.
Each entry is `new static($name, $enemizer_name, $can_beat_closure)`.

**12 bosses defined (file:line of each constructor):**

| # | Boss name | enemizer name | `app/Boss.php` line | shuffle role |
|---|---|---|---|---|
| 1 | Armos Knights | Armos | 69 | shufflable |
| 2 | Lanmolas | Lanmola | 75 | shufflable |
| 3 | Moldorm | Moldorm | 80 | shufflable |
| 4 | Agahnim | Agahnim | 84 | **pinned** (goal-required, Aga 1) |
| 5 | Helmasaur King | Helmasaur | 87 | shufflable |
| 6 | Arrghus | Arrghus | 93 | shufflable |
| 7 | Mothula | Mothula | 98 | shufflable |
| 8 | Blind | Blind | 106 | shufflable |
| 9 | Kholdstare | Kholdstare | 112 | shufflable |
| 10 | Vitreous | Vitreous | 118 | shufflable |
| 11 | Trinexx | Trinexx | 124 | shufflable |
| 12 | Agahnim2 | Agahnim2 | 126 | **pinned** (goal-required, Aga 2) |

Ganon is NOT in `BossCollection` (the final boss is handled outside the boss
shuffle). This grounds design.md D1's "**10-boss permutation** with Agahnim 1,
Agahnim 2, Ganon pinned": the 10 shufflable dungeon bosses are rows 1-3, 5-11
above; Agahnim/Agahnim2 stay pinned and Ganon is out-of-pool. ✓ matches design.

The `$can_beat` closures (e.g. Armos at `Boss.php:69-74`) encode each boss's
kill-requirement predicate; these are the ALTTPR source for any "is this boss
beatable with current items" logic if the shuffle ever needs assumed-fill-style
reachability over reassigned bosses (design.md D1 currently pins goal-required
bosses, so a full predicate port may not be needed for slice 1 — flag for the
algorithm author).

## Drop-pool provenance

**Task 1.4 deliverable (drop half) — WITH PATH CORRECTION.**

> **Correction.** Task 1.4 says *"Grep `../alttp_vt_randomizer/app/EnemyDrop.php`
> line counts."* **`app/EnemyDrop.php` does not exist** in the checkout. ALTTPR
> models droppable prizes under `app/Drops/` plus the sprite table in
> `app/Sprite.php`. The corrected source map is below. Update task 1.4 / design.md
> to point at these files.

**Drop-pool source map (grounded):**

| Concern | File | lines | what it holds |
|---|---|---|---|
| Prize-pack model | `app/Drops/PrizePack.php` | 61 | `PrizePack` class — a named set of N `PrizePackSlot`s |
| Prize-pack slot model | `app/Drops/PrizePackSlot.php` | 60 | one fillable slot (holds a `Sprite`) |
| Prize-pack **roster** | `app/World.php:76-87` | 12 | the 11 prize packs + slot counts (see below) |
| Droppable sprite table | `app/Sprite.php:93+` | 556 (229 `new Sprite(...)` entries) | the `SpriteCollection` — every enemy/prize sprite id |

**Prize-pack roster (`app/World.php:76-87`) — 11 packs, 63 total slots:**

| Pack key | slots | line |
|---|---|---|
| '0' | 8 | `World.php:77` |
| '1' | 8 | `World.php:78` |
| '2' | 8 | `World.php:79` |
| '3' | 8 | `World.php:80` |
| '4' | 8 | `World.php:81` |
| '5' | 8 | `World.php:82` |
| '6' | 8 | `World.php:83` |
| 'pull' | 3 | `World.php:84` |
| 'crab' | 2 | `World.php:85` |
| 'stun' | 1 | `World.php:86` |
| 'fish' | 1 | `World.php:87` |

Total = (7 × 8) + 3 + 2 + 1 + 1 = **63 droppable slots**. The 7 numbered packs
are the standard enemy-kill drop tables; 'pull'/'crab'/'stun'/'fish' are the
special-source packs (pulled-bush, crab, stunned-enemy, speared-fish).

**Heart-drop constraint (design D3 / task 3.3).** The default pack *contents*
(which prize sprites fill each slot) and the per-tier shuffle are applied at
ROM-write time, not in the model classes above — the `PrizePack`/`PrizePackSlot`
classes are empty containers populated later (`PrizePack::__construct` just
allocates N blank `PrizePackSlot`s, `app/Drops/PrizePack.php:23-27`). Before
implementing the "at least one heart drop reachable in spheres 0-2" guarantee,
ground the *default fill* + the ROM writer (grep for `prizepacks`/`writePrize`
in the ROM/Rom writer; not located in this pass — flagged as an open follow-up).

### Open follow-ups (not closed by this audit)

1. Locate the default prize-pack *fill* (which sprites populate '0'..'6' etc.) and
   the ROM writer that emits the packs — needed for task 3.x heart-drop guarantee.
   *(Resolved differently: this fork does not use the ALTTPR PrizePack model. The
   default fill IS `kPrizeItems[56]` in `src/sprite.c` and the runtime "writer" is
   `ForcePrizeDrop`. The heart floor is implemented against pack 0 of that flat
   table — see `Shuffles+minigames as-built` below.)*
2. Update task 1.4 + design.md to replace the nonexistent `app/EnemyDrop.php`
   citation with the `app/Drops/` + `app/World.php` + `app/Sprite.php` map above.
3. Decide whether boss-shuffle needs the `$can_beat` predicates ported or whether
   pinning goal-required bosses (design D1) makes assumed-reachability unnecessary.
   *(Decision: deferred. Pinning the goal-required bosses is NOT sufficient — the
   logic graph gates each dungeon's `"- Boss"` location on its VANILLA boss-kill
   predicate, so a shuffled item-gated boss can strand a non-goal dungeon's prize.
   Honoring design D6 (no predicate changes), boss shuffle ships experimental +
   documented; the `$can_beat` port is the proper follow-up. See the as-built
   note.)*

## Shuffles+minigames as-built (2026-06-03)

Grounded reconciliation after finishing the runtime install + hardening. The
earlier "Status (2026-05-27)" block above is partly superseded:

- **All 4 minigame sites are DONE+WIRED**, including the two it lists as ⏳/blocked:
  Hype Cave NPC (#78) at `sprite_main.c:25930` (`NiceThiefWithGift`, room `0x11E`)
  and Hammer Pegs (#79) at `overworld.c:3033` (`HandlePegPuzzles`, screen 98). Both
  were completed on `main` after this audit was written. No new minigame wiring was
  needed.
- **Drop model**: the fork uses the flat `kPrizeItems[56]` table (7 packs × 8
  slots), not the ALTTPR PrizePack/PrizePackSlot model. The drop shuffle is a
  permutation over 0..55; the heart floor pins ≥1 heart (id `0xD8`) into pack 0.
- **Boss/drop are orthogonal to item placement** — the corpus carries 10 shuffle-on
  entries whose placement/sphere digests equal their shuffle-off twins (verified
  byte-identical). Boss/drop *assignment* determinism is pinned by
  `BossShuffle_SelfCheck` / `DropShuffle_SelfCheck`.

### Shuffles+minigames benchmark (task 9.5.4)

Generation wall-clock over 30 seeds each (desktop Release x64; the value is
the spoiler's `generation_wall_clock_ms`):

| Settings | p50 | p95 | p99 | max |
|---|---|---|---|---|
| default (shuffles off) | 1 ms | 3 ms | 3 ms | 3 ms |
| boss + drop both on    | 1 ms | 3 ms | 3 ms | 3 ms |

Both-on is indistinguishable from default — boss shuffle is an O(10) permutation
and drop shuffle is an O(56) permutation with a bounded (≤16) heart-floor re-roll,
all dwarfed by placement. Far within the 2 s desktop / 5 s Switch budget (§9.5.1).
The heart-floor retry loop (§9.5.2) never approached its budget in any sampled
seed; the identity fallback did not fire.
