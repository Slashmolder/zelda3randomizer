# audit.md — add-rando-retro-generic-keys

Grounding, apply-time decisions, and the fresh-eyes review for the
`rom.genericKeys` (Retro shared small-key pool) change. Every external claim is
grounded against the sibling checkouts (`../alttp_vt_randomizer/` PHP, MIT;
`../../z3randomizer/` ROM asm) and this fork's source — line numbers re-greped
at apply time, never from memory.

## §0 Grounding (task 1.1)

### §0.1 ALTTPR placement swap (KeyGK)
`app/Location.php:201` and `:268` (`getItem` / `setItem`): when
`rom.genericKeys` is set and the item `instanceof Item\Key`, the location's item
is replaced by `Item::get('KeyGK', $world)` (ROM byte **0xAF**). Confirmed the
same swap appears in `app/Location/Drop/{Bombos,Ether}.php` and
`app/Location/Pedestal.php`. So under genericKeys there are no per-dungeon key
*items* — the pool holds N generic keys, placed wild (`region.wildKeys`, which
Retro also pins, `app/World/Retro.php:18-23`).

### §0.2 ALTTPR logic collapse (the key fact)
`app/Support/ItemCollection.php:271-273` (`has()`):

```php
if (($this->item_counts["ShopKey:$this->checks_for_world"] ?? false) && strpos($key, 'Key') === 0) {
    return true;
}
```

i.e. **when the player holds ≥1 generic key (ShopKey/KeyGK), ANY small-key
requirement `has('KeyDx', N)` is satisfied for any N** — the per-dungeon count is
ignored. Big keys are unaffected (`strpos('BigKeyD1','Key') !== 0`). This is the
deliberate "really bad assumption … until we can rewrite this class" the upstream
comment flags. `assets/rando/macros.yaml:49-50` in this fork already anticipated
it ("ShopKey retro-mode generic-key wildcard … Phase B retro-mode handles
small-key fungibility separately").

### §0.3 ALTTPR runtime shared counter
`../../z3randomizer/` asm: a single `CurrentGenericKeys` (`$7EF38B`) backs all
dungeons when `GenericKeys` is set — `inventory.asm` `LoadKeys` copies it into the
live small-key counter on dungeon entry, `SaveKeys` copies it back on exit, a
locked door decrements the live counter, a key pickup increments both.

### §0.4 Fork starting points (re-greped)
- Live counter `link_num_keys` @ `0xF36F`; per-dungeon store
  `link_keys_earned_per_dungeon[16]` @ `0xF37C` (`src/variables.h`).
- Door-consume: `Dungeon_HandleDoorOpening` (`src/dungeon.c`, `link_num_keys -= 1`).
- Enter-load: two sites (`Dungeon_LoadRoom` / `Module11_02_LoadEntrance`).
- Exit-save: `SaveDungeonKeys`. **Death-save: `Death_Func15` (`src/messaging.c`)
  — a FOURTH key-save site the proposal's "four touch points" list missed;
  surfaced by the fresh-eyes pass (§2 finding 1) and now gated.**
- Grants: rando direct-grant (`Rando_DispatchVanillaGrant` → `dungeon_item_direct_grant`,
  `src/rando/rando.c`), enemy drop (`src/sprite.c`), dash drop (`src/sprite_main.c`).
- Logic models keys per dungeon via `HAS_ITEM(SmallKey_<D>)` / `HAS_AMOUNT(...)`
  in `assets/rando/logic.yaml` + `logic_parts/*`; VM in `src/rando/rando_logic.c`.
- `ITEM_GenericKey` = 125 (`vanilla:0xAF`); per-dungeon `SmallKey_<D>` = ids 53-65.

## §0.2.x runtime exempt-comment index
The runtime key sites carry `// rando-exempt: …` markers. Section tags used in
those comments (mirroring the project audit.md convention):
- `§0.2.2` state-shuffle restore/save (enter-load, exit-save, death-save).
- `§0.2.4` consumption (door-consume + its genericKeys write-through).

## §1 Apply-time decisions

### §1.1 Shared-counter storage (task 1.2)
Chosen: **`link_keys_earned_per_dungeon[15]`** (g_ram `0xF38B`), exposed as the
macro `link_generic_keys` (`src/variables.h`). Rationale:
- Game-side dungeon indices only reach 13 (Ganon's Tower), so array indices 14/15
  are unused by the per-dungeon path — no collision (verified: the per-dungeon
  loops are `g<14`; the tracker's `kDungeonRows` max index is 13).
- `0xF38B` is **exactly** ALTTPR's `CurrentGenericKeys` (`$7EF38B`) → cross-tool
  consistency for free.
- It is inside the 0x500-byte SRAM save block (`save_dung_info`@`0xF000`,
  `memcpy(... 0x500)` in `SaveGameFile`/`CopySaveToWRAM`) so it persists across
  save-and-quit with no new `kRam_*` byte and no canonical-settings change.

### §1.2 Logic mechanism (task 1.3 — design §2b)
Chosen: **option 2 (assumed-fill-native), implemented as a VM-level collapse** —
a direct port of `ItemCollection::has()` §0.2. `eval_has_item` / `eval_has_amount`
in `rando_logic.c`: when `world_state == Retro` and the queried id is a small key
(53-65), return `by_item_id[ITEM_GenericKey] >= 1` (ignoring the per-dungeon
count). `GenericKey` is classified progression (`is_progression_item`), so the
assumed inventory holds all unplaced generic keys; the placer treats them as
ordinary fungible progression exactly as ALTTPR `RandomAssumed` does.

Option 1 (count-substitution, keep per-dungeon `N`) was rejected per design §2b:
it strands when two dungeons' counts sum past the shared supply, and it is **not**
what ALTTPR does. The collapse is intentionally permissive (1 key opens all doors
in logic); safety comes from key abundance (29-30 keys) + the assumed fill, and
the residual key-strand risk is **playtest-only** (design R1).

Prototyped on a cross-dungeon pair before writing: `Logic_SelfCheck` asserts one
generic key opens BOTH a PoD 5-key door AND a TR 4-key door, that 0 keys opens
neither, that big keys are NOT collapsed, and that non-Retro is unaffected.
`Placement_SelfCheck` generates 3 Retro `goal=ganon` seeds and asserts
`goal_completable` + 0 unreachable + GenericKey present + no leaked SmallKey.

### §1.3 OP_ITEM_IS left untouched (forced-key handling)
`OP_ITEM_IS(SmallKey_X)` is used both positively (Swamp Palace Entrance
`can_place` forces the SP key — 1 site) and negatively (`NOT OP_ITEM_IS(...)`
anti-circular, 22 sites). Making it GenericKey-aware would break the negated uses.
So `eval_item_is` is unchanged; instead the **junk-fill vanilla-fallback**
substitutes a per-dungeon SmallKey → GenericKey under genericKeys
(`rando_placement.c`). Net effect: SP Entrance always holds a generic key (total
30 = 29 pool + 1 SP fallback, matching the fork's Wild-keys baseline which also
double-places the SP key); the negated constraints correctly accept generic keys
(no SmallKey item exists to forbid). Verified: SP Entrance (loc 50) holds item
125 across all tested seeds; 0 leaked per-dungeon keys.

## §2 Fresh-eyes review (task 7.3)

A separate review agent audited the full diff against the focus areas (logic/
runtime desync, the CLAUDE.md dominant bug-class forms, gating, grant edge cases,
placement cardinality, spoiler/tracker). **No HIGH or MED defects.** Two LOW notes:

1. **`Death_Func15` ungated death-time key-save** (`src/messaging.c`). Harmless
   in practice (the write-through invariant keeps slot 15 current; `(i>>1)` never
   reaches 15) but it is a real key-save site the design missed. **FIXED** — now
   write-throughs to `link_generic_keys` under genericKeys, for parity with
   `SaveDungeonKeys`.
2. **In-game per-dungeon key tracker shows 0 per dungeon under Retro** — the
   per-dungeon cells (0-13) are 0 because keys live in slot 15. Cosmetic only:
   the in-dungeon HUD shows the live shared count correctly, the reach panel gets
   the real count via `by_item_id[ITEM_GenericKey]`, and the per-dungeon breakdown
   is meaningless under genericKeys. **Left as-is** (a tracker-UI change is out of
   scope; not misleading given the HUD shows the true count).

Verified-clean focus areas (read, not skipped): the N→1 collapse faithfully ports
`ItemCollection::has()`; big keys correctly excluded; placer and runtime both use a
monotone no-consume reachability model so they agree; slot 0xF38B persists and no
non-genericKeys path touches index 15; every seam is `world_state==Retro`-gated
(corpus confirms zero non-Retro digest movement); grant 0xff-sentinel / 0xfe-cap /
in-dungeon resync correct; only SP Entrance vanilla-falls-back to a key under
Retro; spoiler/tracker fail closed on NULL settings (snapshot-restore / v1 slot).

## §2.5 Placer determinism fix (discovered during the post-merge sweep)

A broad Retro completability sweep (7 goals × 3 pools × many seeds) surfaced a
flaky "uncompletable — refusing to write spoiler" rejection on a hard seed
(`0x4f267`, triforce-hunt/ganonhunt, 30 placed pieces). Root-caused and fixed:

- **Root cause (pre-existing, NOT genericKeys):** the headless `--generate-seed`
  CLI defaulted `budget_seconds = 5` (`src/main.c`), a wall-clock cutoff on
  `Place_AssumedFill`'s retry loop. When no attempt fully completes, the loop
  ships the best-so-far; cutting off early (slow/loaded machine) selects a
  *different* best-so-far than a fast run, so `placement_digest` +
  `goal_completable` become machine-speed-dependent. That (a) caused the flaky
  refusal of a seed that IS completable on the deterministic path, and (b) was a
  latent breach of the corpus's cross-platform byte-identical contract (a slow CI
  runner could record a different digest). A stale comment in `Place_AssumedFill`
  even asserted the budget "does not break placement determinism" — false, and the
  source of the misplaced confidence.
- **Fix:** default `budget_seconds = 0` (deterministic — run to the fixed
  `kAssumedFillMaxAttempts` cap, no wall-clock), matching the in-game
  `Rando_GenerateSlot` generator which already used budget=0. Cheap (<~1s even on
  the hardest seeds — the loop returns the instant an attempt completes). The
  `--budget-seconds` flag remains for batch/debug callers. Corrected the stale
  comment. **No corpus change / no kGeneratorVersion bump:** all 110 recorded
  digests reproduce byte-identically under budget=0 (every corpus seed completes
  early, so its output was never budget-dependent); flaky seeds had no stable
  output to preserve. Verified: hard seed × 5 runs → identical digest; 0 refusals
  across 294 Retro seeds at their natural accessibility tiers.
- **The residual (correct behavior, not a bug):** `0x4f267` triforce-hunt at the
  strict `items` ("100% inventory") tier is deterministically refused under Retro
  (Open/Standard accept it) because the placer's best attempt strands 13 real
  progression items — Retro pins ~50 shop/take-any locations, tightening the fill.
  The seed is still beatable (29/30 pieces reachable ≥ 20 required); the generator
  honestly refuses the strict tier rather than misreport accessibility. This is the
  same documented class as the corpus's `a1-standard-triforce-hunt-beatable` entry
  (hunt goals use `accessibility=none`). It is NOT genericKeys — genericKeys is
  strictly more permissive (head-to-head: 0/120 genericKeys vs 3/120 per-dungeon
  refusals on these goals). No fix warranted.

## §3 Acceptance status
- **Headless (done):** clean `-Werror` build; `--rando-selftest` OK (incl. the new
  genericKeys checks); corpus 110/110 OK with only the 12 Retro entries moved (a
  new hard-pool Retro seed added); `check_audit_guard` / `check_determinism` /
  `check_codegen_wiring` / `check_corpus_version_sync` / `check_rando_slot_path`
  / `check_no_embedded_data` green; Retro slot/CLI placement parity confirmed.
- **Playtest (the gate — owner-only, NOT done):** a key found outside its dungeon
  opens a door in another dungeon; clear ≥2 dungeons from the shared pool with no
  strand; full clear of the hard-pool Retro seed (`mode.state=retro, goal=ganon,
  item_pool=hard, seed=0xAF`) spot-checking all 7 goals; non-Retro + vanilla
  unchanged. No headless test covers key-strand beatability (design R1).
