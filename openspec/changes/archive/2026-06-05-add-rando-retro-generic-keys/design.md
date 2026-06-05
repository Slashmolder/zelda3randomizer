# add-rando-retro-generic-keys — design

Grounding for the `rom.genericKeys` follow-up. Every external claim below is
grounded against the sibling checkouts (ALTTPR PHP `../alttp_vt_randomizer/`,
the ROM-asm `../../z3randomizer/`) and this fork's source. Read
`add-rando-retro-world-state/design.md` §8 (Risk 8) first — it is where this
work was carved out.

## 0. Upstream model (grounded)

**ALTTPR placement.** `app/Location.php:201` and `:268`, plus
`app/Location/Drop/{Bombos,Ether}.php:33` and `Pedestal.php:238`: whenever
`rom.genericKeys` is set, each `Item\Key` is replaced by `Item::get('KeyGK')`
(ROM byte **0xAF**) at fill time. So under genericKeys there are no per-dungeon
key *items* — the pool holds N generic keys, placed wild (`region.wildKeys`).
`app/Filler/RandomAssumed.php:102` shows wildKeys removes the per-dungeon
placement restriction; genericKeys makes the items themselves fungible.

**ALTTPR runtime** (`../../z3randomizer/` asm). A single shared counter
`CurrentGenericKeys` (`$7EF38B`) backs all dungeons when `GenericKeys`
(`$B08172`) is set. On dungeon entry `LoadKeys` copies the shared counter into
the live small-key counter; on exit `SaveKeys` copies it back; a locked door
decrements the live counter; a key pickup increments both. (Confirmed in
`inventory.asm` LoadKeys/SaveKeys + `newitems.asm` key-grant.)

## 1. Fork starting point (grounded)

- Live counter `link_num_keys` @ `0xF36F`; per-dungeon store
  `link_keys_earned_per_dungeon[]` @ `0xF37C` (16 bytes, dungeon index `>>1`).
- Locked-door consume: `src/dungeon.c:5238` (`link_num_keys -= 1`).
- Dungeon-enter load: `src/dungeon.c:6586` and `:8003`
  (`link_num_keys = link_keys_earned_per_dungeon[...]`).
- Dungeon-exit save: `src/dungeon.c:8075`
  (`link_keys_earned_per_dungeon[idx>>1] = link_num_keys`).
- Overworld/leave-dungeon sentinel: `link_num_keys = 0xff`
  (`src/hud.c:1412`, `src/overworld.c:435`).
- Key grants: rando direct-grant `src/rando/rando.c:289-304` (already
  dungeon-context-aware — credits the destination dungeon's slot); vanilla enemy
  drop `src/sprite.c:1408`; dash drop `src/sprite_main.c:7394`.
- **Logic models keys per dungeon**: `assets/rando/logic.yaml` and
  `logic_parts/*` gate doors on `HAS_ITEM(SmallKey_<Dungeon>)` /
  `HAS_ITEM_COUNT(...)`. `assets/rando/macros.yaml:49-50` already flags that
  "ShopKey retro-mode generic-key wildcard … retro-mode handles small-key
  fungibility separately" — i.e. this work was anticipated and left open.
- `ITEM_GenericKey` (id 125, ROM 0xAF) already exists in the registry.

## 2. The three coupled pieces

### 2a. Placement (low risk)
In `BuildItemPool` under Retro, substitute every `SmallKey_<Dungeon>` (ids 53-65)
with `ITEM_GenericKey` (125). The count is the sum of all dungeons' key counts.
Mirrors ALTTPR's per-Location KeyGK swap, done once at pool build. wildKeys
already places them wild. Headless-checkable: pool composition + `goal_completable`.

### 2b. Logic (THE HARD PART — the reason this is its own change)
Key-door predicates must evaluate against the **shared GenericKey count**, not a
per-dungeon item. Options to evaluate at apply-time:

1. **Count-substitution.** Rewrite each `HAS_ITEM_COUNT(SmallKey_<D>, n)` so that,
   under genericKeys, it reads `HAS_ITEM_COUNT(GenericKey, n)`. Simple to express
   but **wrong in general**: two dungeons each needing 3 keys would both pass at
   `GenericKey >= 3` while the player only has 3 total — the assumed-fill could
   place progression behind doors the player can't actually all open. This is the
   classic shared-key-logic stranding bug.
2. **Assumed-fill-native (preferred, matches ALTTPR).** Let `Place_AssumedFill`
   treat GenericKey as ordinary progression and rely on assumed reachability:
   when testing a location, assume all *other* items (incl. all other generic
   keys) are held. This is how ALTTPR's `RandomAssumed` avoids stranding. Requires
   confirming the fork's assumed-fill expands key-gated regions correctly when the
   gating item is a single fungible pool rather than per-dungeon.
3. **Logic-free doors + total-count floor.** Make key-doors free in reachability
   but guarantee `|GenericKey placed| >= total doors` and front-load via
   assumed-fill. Simplest but the least safe (can still strand mid-dungeon).

**Decision deferred to apply-time prototyping**, but option 2 is the intended
path. Whichever is chosen, the acceptance bar is the same: a real playtest of
each goal at hard pool with no key-strand.

### 2c. Runtime (medium risk — must match the logic)
Add `Rando_IsGenericKeysActive()` (= `Rando_IsRetroActive()` today; a distinct
name documents intent and lets a future standalone genericKeys axis diverge).
Add a shared small-key counter that **persists in SRAM**. Candidate storage: a
currently-unused index of `link_keys_earned_per_dungeon[16]` (indices ≥13 appear
unused — verify) so it is saved with the game for free, OR a `kRam_*` byte that
is added to the persisted set. Intercept, gated on `Rando_IsGenericKeysActive()`:
- enter-load (`dungeon.c:6586/:8003`): `link_num_keys = shared`.
- exit-save (`dungeon.c:8075`): `shared = link_num_keys`.
- door-consume (`dungeon.c:5238`): unchanged decrement of `link_num_keys`, which
  is shared-backed; **write-through** `shared = link_num_keys` immediately to
  remove the exit-save dependency (robustness).
- grant: a `GenericKey` (0xAF) pickup increments `shared` (and `link_num_keys`
  when in a dungeon). The rando grant (`rando.c:289-304`) already special-cases
  dungeon context — extend it for the shared pool.

## 3. Risks

- **R1 (HIGH, playtest-only): logic/runtime desync → soft-lock.** If logic
  assumes a door is openable but the shared count at runtime is short (or vice
  versa), a dungeon strands. No headless test covers key-strand beatability.
  **Mitigation:** option-2 logic + write-through runtime + an in-game run of all
  7 goals at hard pool before merge.
- **R2 (MED): SRAM storage for the shared counter.** Must persist across
  save-and-quit and not collide with a real dungeon slot. Verify the chosen
  index/byte with an F12 dump.
- **R3 (LOW): determinism.** keys → GenericKey changes Retro placement →
  kGeneratorVersion bump + corpus regen; non-Retro must stay byte-identical.

## 4. Acceptance

1. Headless: corpus regenerates, only Retro digests move, `goal_completable` for
   all Retro goals, 0 unreachable, `--rando-selftest` green.
2. **Playtest (the gate):** in a Retro seed, a key found anywhere opens any
   locked door; clear at least two dungeons drawing keys from a shared pool with
   no strand; full clear of a hard-pool Retro seed.
