## Why

Follow-up to `add-rando-retro-world-state`. That change pinned three of ALTTPR
Retro's four flags — `rupeeBow`, `takeAnys`, `wildKeys` — and deliberately
**deferred `rom.genericKeys`** (its scope note carves it here). Retro today is
beatable and faithful except that small keys keep **per-dungeon identity**:
under `wildKeys` a key for dungeon B can be *found* anywhere, but it only opens
B's doors. ALTTPR Retro is more forgiving: **one shared key pool, any key opens
any locked door** (`rom.genericKeys`). This change closes that gap so Retro is
comprehensive.

`genericKeys` was deferred (not skipped) because it is the project's dominant
bug-class landmine: the fork models small keys in *logic* per dungeon
(`HAS_ITEM(SmallKey_<Dungeon>)`), so a single pool cannot be wired as a blind
runtime intercept — the **logic reachability** must change in lockstep or seeds
soft-lock (a door that can't open) or strand keys, and **none of that is
headless-validatable** beyond gross unreachability. This change does the
coordinated placement + logic + runtime work and gates merge on an end-to-end
playtest.

## What Changes

When `settings.world_state == Retro` (i.e. `genericKeys` is pinned on, alongside
the already-pinned `wildKeys`):

### Placement — keys become one fungible item
- Every small-key item in the pool is substituted with **`GenericKey`** (id 125,
  ROM 0xAF) at pool-build time (mirrors ALTTPR `app/Location.php:201,268`, which
  swaps each `Item\Key` for `KeyGK` when `rom.genericKeys`). Per-dungeon
  `SmallKey_<Dungeon>` items no longer enter the pool under Retro.
- Placement stays wild (the `Settings_EffectiveSmallKeysMode` = Wild override
  from the prior change already routes small keys through the general pool).

### Logic — key-door reachability against a shared count
- Key-door predicates that today read `HAS_ITEM(SmallKey_<Dungeon>)` /
  `HAS_ITEM_COUNT(SmallKey_<Dungeon>, N)` evaluate, under `genericKeys`, against
  the **shared `GenericKey` count** instead. This is the hard part — the
  assumed-fill must guarantee enough generic keys are reachable, in an order that
  never strands the player, across the *whole* dungeon graph. (ALTTPR solves this
  inside its `RandomAssumed` filler; the fork's `Place_AssumedFill` must do the
  same for the merged pool.)

### Runtime — one shared counter
- A single shared small-key counter (SRAM-persisted) replaces the per-dungeon
  counters when `Rando_IsGenericKeysActive()` (= Retro). The four runtime touch
  points — locked-door consume, dungeon-enter load, dungeon-exit save, key grant
  — read/write the shared counter instead of `link_keys_earned_per_dungeon[]`.
  `GenericKey` (0xAF) grants increment it; any locked door decrements it.

### Corpus + version
- `kGeneratorVersion` bumps (Retro placement changes: keys → `GenericKey`).
  Corpus regenerates; only Retro entries' digests move; non-Retro byte-identical.

## Capabilities

### Modified Capabilities

(none — to avoid an archive-sequencing conflict with the not-yet-archived
parent change, this proposal uses ADDED requirements rather than MODIFYing the
parent's "Retro world-state config-flag pinning" requirement.)

### New Capabilities

- `randomizer-core`: ADDED "Retro generic small-key pool" — placement substitutes
  `GenericKey` and pins `genericKeys` on for Retro.
- `randomizer-logic`: ADDED "Generic small-key door reachability" — key-door
  predicates evaluate against the shared pool under `genericKeys`.

## Impact

- **Code**: `src/rando/rando_placement.c` (key → GenericKey substitution under
  Retro), the logic codegen + key-door predicates (`assets/rando/logic*.yaml` +
  `assets/rando_logic_gen.py`), `src/rando/rando_logic.c` (reachability key
  counting), the runtime key sites (`src/dungeon.c:5238/6586/8003/8075`,
  `src/rando/rando.c` grant, `src/sprite.c:1408`, `src/sprite_main.c:7394`) +
  a shared-counter accessor `Rando_IsGenericKeysActive()` and a persisted byte.
- **Regression risk**: HIGH and **playtest-gated** — the logic/runtime must stay
  in lockstep or dungeons soft-lock. The corpus catches gross unreachables only;
  key-strand beatability needs an in-game run of each goal at hard pool.
- **Dependency**: builds on `add-rando-retro-world-state` (wildKeys + the
  `Settings_EffectiveSmallKeysMode` seam). That change must archive first.
