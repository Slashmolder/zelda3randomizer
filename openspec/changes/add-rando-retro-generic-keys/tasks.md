# Tasks — add-rando-retro-generic-keys

Prereq: `add-rando-retro-world-state` archived (wildKeys + the
`Settings_EffectiveSmallKeysMode` seam are in the baseline). See `design.md`.

## 1. Grounding / prototype

- [x] 1.1 Re-confirm the ALTTPR KeyGK swap sites (`app/Location.php:201,268`;
  `Location/Drop/{Bombos,Ether}.php`; `Pedestal.php`) and the asm shared-counter
  model (`inventory.asm` LoadKeys/SaveKeys, `$7EF38B`). Record in an audit.md.
  → `audit.md §0`. The decisive grounding is `ItemCollection::has():271-273` (the
  ShopKey wildcard) — the collapse mechanism, not just the swap.
- [x] 1.2 Verify which `link_keys_earned_per_dungeon[16]` indices are unused to
  pick the shared-counter storage slot. → index **15** (`0xF38B`) — unused by the
  per-dungeon path (max game index 13) AND exactly ALTTPR `$7EF38B`; inside the
  0x500 SRAM block so it persists for free. Macro `link_generic_keys`. `audit.md §1.1`.
- [x] 1.3 Prototype the logic approach (design §2b option 2) — VM-level collapse.
  Confirmed via `Logic_SelfCheck` (1 key opens a PoD-5 AND a TR-4 door
  cross-dungeon; big keys excluded; non-Retro unaffected) + `Placement_SelfCheck`
  (3 Retro `goal=ganon` seeds: goal_completable + 0 unreachable, no strand).

## 2. Placement (keys → GenericKey)

- [x] 2.1 In `BuildItemPool`, under Retro, substitute `SmallKey_<Dungeon>`
  (ids 53-65) with `ITEM_GenericKey` (125), same per-dungeon counts/slots (pool
  size unchanged). Gated on `Settings_GenericKeysActive` (`world_state == Retro`).
- [x] 2.2 wildKeys placement confirmed — generic keys route into the wild pool
  (29 in pool + 1 forced at Swamp Palace Entrance = 30; verified across 5 seeds).
- [x] 2.3 Spoiler shows `GenericKey` (30 entries); `shops[]` (38) + `sphere_data`
  still render; no per-dungeon `SmallKey_<D>` leaks into a Retro placement.

## 3. Logic (shared-pool reachability)

- [x] 3.1 Implemented the VM-level collapse in `src/rando/rando_logic.c`
  (`eval_has_item`/`eval_has_amount`): under Retro, any small-key requirement →
  `by_item_id[ITEM_GenericKey] >= 1` (a port of ALTTPR `ItemCollection::has()`
  271-273). No YAML/codegen change needed — small keys only appear in HAS_ITEM /
  HAS_AMOUNT, and `GenericKey` is now `is_progression_item`.
- [x] 3.2 Non-Retro logic byte-identical (the collapse is `world_state==Retro`
  gated; corpus confirms zero non-Retro placement/sphere digest movement).
- [x] 3.3 Selftests: `Logic_SelfCheck` (cross-dungeon: one key opens PoD-5 AND
  TR-4; 0 keys opens neither; big keys excluded; non-Retro off) + `Placement_SelfCheck`
  (3 Retro `goal=ganon` seeds → goal_completable + 0 unreachable + no leak).

## 4. Runtime (shared counter)

- [x] 4.1 `Rando_IsGenericKeysActive()` added (rando.c/.h) — true iff Retro slot
  active (distinct name from `Rando_IsRetroActive`).
- [x] 4.2 SRAM-persisted shared counter `link_generic_keys`
  (`link_keys_earned_per_dungeon[15]` = `0xF38B` = ALTTPR `$7EF38B`) — `variables.h`.
- [x] 4.3 Intercepts (all gated): enter-load (2 sites, `dungeon.c`), exit-save
  (`SaveDungeonKeys`), **death-save (`Death_Func15`, `messaging.c` — 4th site
  found by audit)**, door-consume write-through, GenericKey grant
  (`rando_grant_generic_key`), enemy drop (`sprite.c`), dash drop (`sprite_main.c`),
  live-counts/tracker bridge. Vanilla path byte-identical when not genericKeys.
- [x] 4.4 Audit-guard green — every new `link_num_keys` / `link_generic_keys`
  write carries a `// rando-exempt:` comment directly above it.

## 5. Determinism + corpus

- [x] 5.1 `kGeneratorVersion` 53 → 54 with a descriptive comment (`rando.h`).
- [x] 5.2 `bump_rando_corpus.py --apply` — ONLY the 11 pre-existing Retro entries
  moved; Open/Standard/Inverted byte-identical. `check_corpus_version_sync` green.
- [x] 5.3 Added hard-pool Retro seed `b-retro-genericKeys-hardpool-ganon`
  (`mode.state=retro, goal=ganon, item_pool=hard, seed=0xAF`).

## 6. Testing

- [x] 6.1 Headless: clean `-Werror` build, `--rando-selftest` OK, corpus 110/110,
  all guards green, Retro slot/CLI placement parity confirmed. A broad
  completability sweep (7 goals × 3 pools × many seeds) surfaced and fixed a
  PRE-EXISTING placer non-determinism bug — `--generate-seed` defaulted to a 5s
  wall-clock budget that made placement machine-speed-dependent; fixed to budget=0
  (deterministic, matches the slot generator). No corpus/version change (all 110
  digests reproduce). See `audit.md §2.5`.
- [ ] 6.2 **PLAYTEST (the gate — OWNER, NOT done):** a key found outside its
  dungeon opens a door in another dungeon; clear two dungeons from a shared pool
  with no strand; full clear of the hard-pool Retro seed (all 7 goals spot-checked).
- [ ] 6.3 **Regression (owner playtest portion):** non-Retro placement unchanged
  is corpus-proven; vanilla/non-Retro runtime unchanged is gate-guaranteed but
  confirm in-game on the playtest pass.

## 7. Docs + archive  (POST-playtest — do NOT do until owner confirms 6.2)

- [ ] 7.1 Update `docs/randomizer.md` "Retro world-state": flip `genericKeys`
  from deferred to implemented; note the single-pool behavior.
- [ ] 7.2 Update the parent change's spec note (genericKeys now shipped).
- [x] 7.3 Fresh-eyes audit on the diff (logic/runtime key-strand desync focus) —
  no HIGH/MED; 2 LOW (death-save FIXED; tracker per-dungeon-column cosmetic).
  Recorded in `audit.md §2`.
- [ ] 7.4 `openspec archive add-rando-retro-generic-keys`.
