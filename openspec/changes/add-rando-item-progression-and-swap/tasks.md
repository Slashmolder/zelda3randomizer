# Tasks — add-rando-item-progression-and-swap

All implementation landed in commit `b73e9bb` (squash-merged to `main`); docs in
`7e54117`. These tasks are recorded **as-built** for spec reconciliation.

## 1. Magic upgrades → strictly progressive
- [x] 1.1 `magic_upgrade_direct_grant` (`rando.c`): both `HalfMagic` and `QuarterMagic` advance `link_magic_consumption` by one tier (`0→1→2`, capped at 2).
- [x] 1.2 Magic Bat identity grant (`sprite_main.c`): advance one tier instead of `= 1`, so a Magic Bat upgrade stacks onto one found elsewhere.
- [x] 1.3 `Rando_SelfCheck` magic assertions updated for progressive (incl. `QuarterMagic`-from-0 → half).
- [x] 1.4 Confirm placement-neutral: the magic macro is satisfied at ≥ half, so no logic distinguishes ¼; 79/79 corpus digests unchanged, no `kGeneratorVersion` bump.

## 2. Boomerang progressive / bow never-downgrade + swap
- [x] 2.1 `Rando_GrantBoomerang` (`rando.c`): progressive (1st = blue, 2nd = red, color ignored). Verified no `assets/rando` predicate requires a boomerang.
- [x] 2.2 `Rando_GrantBow` (`rando.c`): never-downgrade, item identity kept (wood/silver), arrow-bit parity preserved.
- [x] 2.3 `misc.c` receive path: route boomerang (`0x0c`/`0x2a`) / bow (`0x0b`/`0x3a`/`0x3b`) to the helpers; clamp every other absolute byte-write to never-downgrade (`v > *p`); EXEMPT `link_arrow_filler` (`0x43`/`0x44`).
- [x] 2.4 `Hud_NormalMenu` (`hud.c`): Press-A swap for boomerang (blue↔red) + bow (wood↔silver), gated on owning both; `kHudItem_Bow` / `_Boomerang` ids added (`hud.h`).
- [x] 2.5 Ownership persistence: `g_rando_boomerang_owned` / `g_rando_bow_owned` populated/restored/reset; slot-header `@73`/`@74` in the additive reserved tail; round-trip in `RandoSave_SelfCheck`. No save `format_version` bump.
- [x] 2.6 Reachability snapshots (`by_item_id` + informational) read ownership OR raw byte.
- [x] 2.7 Debug panel: "give all" + the Bow/Boomerang combos sync ownership.

## 3. Trigger-based location re-collect safety
- [x] 3.1 Magic Bat (`sprite_main.c`): gate the `link_magic_consumption >= 2` summon guard on non-rando; under rando, `Rando_IsLocationChecked(LOC_Magic_Bat)` is the sole re-grant gate (missable fix).
- [x] 3.2 Flute Spot (`ancilla.c` `Ancilla36_Flute`): gate the grant on `!Rando_IsLocationChecked(LOC_Flute_Spot)` (re-grant / dupe fix), mirroring the Ether/Bombos tablet guard.
- [x] 3.3 Both: non-rando path byte-identical (RAM-compare preserved).

## 4. Spec reconciliation + verification
- [x] 4.1 Spec delta MODIFIES `randomizer-placement` "Item types receivable via dispatcher" (magic bullet + scenario: progressive, `0/1/2` convention) and ADDS the two requirements above — authored here, not hand-edited into the published spec.
- [x] 4.2 Build clean (`-Werror`); `--rando-selftest` all subsystems OK; `check_audit_guard --strict` clean; corpus 79/79 unchanged.
- [x] 4.3 Fresh-eyes audit (two parallel agents) — found + fixed: the `arrow_filler` clamp regression (2.3) and the debug-combo ownership desync (2.7).

## 5. Playtest (owner — slot grant path has no automated test)
- [ ] 5.1 Magic progressive: collect the two magic upgrades in BOTH orders → ½ then ¼; `QuarterMagic`-first gives ½.
- [ ] 5.2 Boomerang progressive: 1st pickup = blue, 2nd = red regardless of placed item; Press-A swaps blue↔red.
- [ ] 5.3 Bow: a silver pickup gives silver, a later wood pickup does not downgrade; Press-A swaps wood↔silver arrows.
- [ ] 5.4 Magic Bat is collectable when already at ¼ magic; no re-grant after collection.
- [ ] 5.5 Flute Spot grants once; toggling back to shovel and re-digging the tile yields nothing.
