## 1. Logic-safety verification (the one real risk)

- [x] 1.1 Confirm each of the 10 `<Dungeon> - Boss` Drop locations (ids 16, 23, 30,
  48, 59, 68, 77, 86, 95, 108) has a `can_reach` predicate that gates on defeating
  the dungeon boss — not `TRUE()` — in both Standard (`assets/rando/logic_parts/*.yaml`)
  and Inverted (`assets/rando/logic_parts/inverted/**`) graphs. *(Done — all 10
  verified to include a `CanKill<Boss>()` macro plus reach/open-room items; no
  weak predicates, no last-wins clobber. Inverted overrides differ only by
  MoonPearl/bunny-bypass terms, never dropping the boss-kill gate.)*

## 2. UI toggle (PC native settings window)

- [x] 2.1 In `Panel_General` (`src/rando/rando_window/rando_window.cpp`), add a
  checkbox to the existing "Toggles" section labeled **"Shuffle boss heart
  containers"**. Map with the inversion applied at the UI layer:
  `bool v = (s->region_boss_hearts_in_pool == 0)` and on toggle
  `s->region_boss_hearts_in_pool = v ? 0 : 1; changed = true;`. Add a `HelpTooltip`
  explaining the inversion ("On = the 10 boss heart containers join the general
  item pool; other items can land at boss kills"). *(Done — checkbox added after
  Hints in the "Toggles" group; `changed=true` matches sibling toggles so the
  live settings hash refreshes.)*
- [x] 2.2 Remove the now-redundant read-only "Region boss hearts in pool: %s" line
  from the "Locked settings (fixed in this version)" block — it is no longer locked.
  *(Done — line removed; surrounding CollapsingHeader block + HelpTooltip intact.)*

## 3. Comment / doc hygiene

- [x] 3.1 Fix the stale comment at `src/rando/rando_placement.c:346` — it states the
  pinning happens "when bossHeartsInPool is false (Phase A default)", which is
  backwards. Default is `1`; `1` (non-zero) = identity-placed/pinned; `0` = in pool.
  *(Done — rewrote to state the inversion and that the pool always gets
  `bossheart_cap` BHC items regardless; dropped the inaccurate "land at their
  _Boss slots" mechanism claim the spoiler data contradicted.)*
- [x] 3.2 Tighten the field comment at `src/rando/rando_settings.h:91` to spell out
  the value inversion (1 = pinned/identity-placed default, 0 = shuffled into pool)
  so future readers don't trip on the name. *(Done.)*

## 4. Verification

- [x] 4.1 Build the PC target (MSBuild Release|x64 via the worktree recipe:
  SDL2 junction + generated `vanilla_assets_hash.h` + `SolutionDir`/`OutDir`
  overridden to the worktree `verify\` dir so main's binary is never clobbered).
  Built clean (`-Werror`-equivalent NDEBUG config) → `verify/zelda3.exe`.
- [x] 4.2 Headless: generated seed `0xABCD` (open/fast_ganon) at both field values.
  **`region_boss_hearts_in_pool=1` (default): 10/10 boss Drop slots hold
  BossHeartContainer (item 51).** **`=0`: 0/10 boss slots hold a heart container**
  (they hold items 105, 50, 3, 101, 107, 42, 100, 101, 101, 100) and the heart
  containers are placed at other locations. `placement_digest` differs between the
  two (`c0175d64…` vs `a92d3d8e…`) — the toggle demonstrably changes placement.
- [x] 4.3 `--rando-selftest` green — all subsystems OK incl. `[Placement_SelfCheck]`,
  `[Settings...]`, `[Shuffles_SelfCheck]`, `[RandoGenerate_SelfCheck]`.
- [x] 4.4 Placement corpus: `run_rando_corpus.py --binary=./verify/zelda3.exe` →
  **all 62 entries OK** (every default-settings digest matches the manifest →
  zero baseline churn, no regen / no `kGeneratorVersion` bump needed).
- [ ] 4.5 Playtest (no automated coverage on the slot grant path): generate a slot
  with the toggle ON, kill a boss whose slot was assigned a non-heart item, and
  confirm you receive that item — not a heart container. **(User manual step — the
  slot grant path is the one surface no headless test covers.)**

## 5. Audit + archive

- [x] 5.1 Fresh-eyes audit pass (parallel agent) on the diff — **clean, no new
  bugs.** Confirmed inversion mapping, `changed=true` refresh, no orphaned
  braces/tooltips after the read-only line removal, comment/code consistency, and
  no `-Werror` risk.
- [x] 5.2 `openspec validate add-rando-boss-heart-pool-toggle --strict` → green.
