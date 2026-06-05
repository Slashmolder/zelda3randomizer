# Tasks — add-rando-retro-generic-keys

Prereq: `add-rando-retro-world-state` archived (wildKeys + the
`Settings_EffectiveSmallKeysMode` seam are in the baseline). See `design.md`.

## 1. Grounding / prototype

- [ ] 1.1 Re-confirm the ALTTPR KeyGK swap sites (`app/Location.php:201,268`;
  `Location/Drop/{Bombos,Ether}.php`; `Pedestal.php`) and the asm shared-counter
  model (`inventory.asm` LoadKeys/SaveKeys, `$7EF38B`). Record in an audit.md.
- [ ] 1.2 Verify which `link_keys_earned_per_dungeon[16]` indices are unused (an
  F12 dump of a mid-dungeon save) to pick the shared-counter storage slot; OR add
  a persisted `kRam_*` byte. Confirm it survives save-and-quit.
- [ ] 1.3 Prototype the logic approach (design §2b option 2) on one multi-key
  dungeon + a cross-dungeon pair; confirm the assumed-fill does not strand.

## 2. Placement (keys → GenericKey)

- [ ] 2.1 In `BuildItemPool`, under Retro, substitute `SmallKey_<Dungeon>`
  (ids 53-65) with `ITEM_GenericKey` (125); total count = sum of per-dungeon key
  counts. Gate on `world_state == Retro` (genericKeys pinned).
- [ ] 2.2 Confirm wildKeys placement still routes the generic keys into the wild
  pool (no per-dungeon restriction).
- [ ] 2.3 Spoiler: generic keys show as `GenericKey`; verify the shops[]/Shops
  sections and sphere data still render.

## 3. Logic (shared-pool reachability)

- [ ] 3.1 Implement the chosen approach (design §2b — intended: assumed-fill
  treats GenericKey as fungible progression). Touches `assets/rando/logic*.yaml`
  + `assets/rando_logic_gen.py` + `src/rando/rando_logic.c` key counting.
- [ ] 3.2 Ensure non-Retro logic is byte-identical (per-dungeon predicates
  unchanged when `genericKeys` is off).
- [ ] 3.3 Selftest: a Retro reachability check that exercises a cross-dungeon key
  dependency and asserts `goal_completable` + 0 unreachable on ≥3 seeds.

## 4. Runtime (shared counter)

- [ ] 4.1 Add `Rando_IsGenericKeysActive()` (rando.c/.h) — true iff Retro slot
  active. (Distinct name from `Rando_IsRetroActive` to document intent.)
- [ ] 4.2 Add the SRAM-persisted shared counter + accessor.
- [ ] 4.3 Intercept (gated): enter-load (`dungeon.c:6586/:8003`), exit-save
  (`:8075`), door-consume write-through (`:5238`), and the grant sites
  (`rando.c` direct-grant, `sprite.c:1408`, `sprite_main.c:7394`). Vanilla path
  byte-identical when not genericKeys.
- [ ] 4.4 Audit-guard: any new `link_num_keys` / shared-counter writes carry a
  `// rando-exempt:` comment or dispatch correctly.

## 5. Determinism + corpus

- [ ] 5.1 Bump `kGeneratorVersion`; document the bump (Retro placement: keys →
  GenericKey).
- [ ] 5.2 `bump_rando_corpus.py --apply`; confirm ONLY Retro digests move,
  non-Retro byte-identical.
- [ ] 5.3 Add a Retro corpus seed at hard pool (most key-pressure).

## 6. Testing

- [ ] 6.1 Headless: build clean, `--rando-selftest` OK, corpus green, guards green.
- [ ] 6.2 **PLAYTEST (the gate):** a key found outside its dungeon opens a door in
  another dungeon; clear two dungeons from a shared pool with no strand; full
  clear of a hard-pool Retro seed (all 7 goals spot-checked).
- [ ] 6.3 Regression: non-Retro (per-dungeon keys) unchanged; vanilla unchanged.

## 7. Docs + archive

- [ ] 7.1 Update `docs/randomizer.md` "Retro world-state": flip `genericKeys`
  from deferred to implemented; note the single-pool behavior.
- [ ] 7.2 Update the parent change's spec note (genericKeys now shipped).
- [ ] 7.3 Fresh-eyes audit on the diff (focus: logic/runtime key-strand desync).
- [ ] 7.4 `openspec archive add-rando-retro-generic-keys`.
