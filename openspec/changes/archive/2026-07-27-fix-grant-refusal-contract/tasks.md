# Tasks: fix-grant-refusal-contract

Every task re-derives its own premise from source before landing. The audit
findings are agent-produced claims; anything that does not reproduce is recorded
as REFUTED in this file rather than quietly "fixed".

## 1. The systemic fix (do first — it defuses SEVERE 1 and 2)

- [x] 1.1 Split the bottle predicate in `rando.c` into permanent (pickup at four
      bottles — monotonic) vs transient (content with no empty bottle). Permanent
      ⇒ `AcceptedNoOp`; transient ⇒ `RetryableFailure`
- [x] 1.2 Confirm no surface treats an `AcceptedNoOp` location as an item held
      (tracker item view, goal completion, hints, live reachability)
- [x] 1.3 Self-check matrix over (permanent, transient, at-cap, unsatisfiable) ×
      expected disposition, driving `Rando_PrepareGrant` itself

## 2. SEVERE sites

- [x] 2.1 `sprite.c` `SpriteExplode_SpawnEA` — no `flag_is_link_immobilized`
      held across frames pending a refusal; preflight before locking
- [x] 2.2 `sprite.c` `Sprite_DoTheDeath` / `Sprite_ManuallySetDeathFlagUW` —
      DOWNGRADED by 1.1 and closed. The permanent refusal that made the
      undespawned-enemy state unrecoverable can no longer occur; the remaining
      transient refusal leaves the enemy alive and re-killable with the menu
      available, which is the recoverable arm this task asked for. Re-derived:
      this site never immobilizes, so it does not block its own recovery
- [x] 2.3 Fixed at the CALLER, not the fallback. Clearing the flag inside
      `Link_ReceiveItem`'s saturated fallback broke `ItemReceipt_LosslessSelfCheck`,
      which correctly requires that fallback to leave caller action state
      untouched — the flag belongs to whoever set it. `Sprite_HeartContainer`
      (the site whose comment names method 0 "so their receipt clears
      immobilization") now releases Link on its terminal path. RESIDUAL: the
      other `flag_is_link_immobilized = 1` sites in `sprite_main.c` were not
      swept for the saturated-fallback case
- [x] 2.4 `dungeon.c` `Dungeon_LiftAndReplaceLiftable` — write `*pt` on the
      retry return path (and/or make the caller honor the return code)

## 3. MODERATE sites

- [x] 3.1 `ancilla.c` tablet (`Ancilla29_CommitStoredRandoGrant`) — free the
      ancilla slot before delivering, restore when not delivered (the `a5ef5eb0`
      pattern)
- [x] 3.2 `ancilla.c` flute spot (`Ancilla36_Flute`) — same inversion; also
      ensure `Invalid` does not leave a permanently floating ancilla
- [x] 3.3 `sprite.c` ToH basement cage — restore the `!Rando_IsLocationChecked`
      term so a later non-enemy-drop key in room 0x87 is not swallowed
- [x] 3.4 `sprite.c` `SpriteDeath_Func4` — DOCUMENTED AT THE SITE, not
      restructured. Confirmed: parking re-enters `Sprite_DoTheDeath` from the
      top, whose prize roll can rewrite `sprite_type[k]` and destroy the `0xa2`
      arm. Resuming at the branch needs per-sprite pending state in a boss death
      path no automated check can exercise, so patching it blind is the worse
      trade. Reachability is now transient-refusal-only
- [x] 3.5 `misc.c` `AncillaAdd_ItemReceipt` — move the `ItemReceipt_GrantInventory`
      reject above `Ancilla_AddAncilla` / the immobilize set, so a live failure
      cannot leak a half-built receipt with Link locked
- [x] 3.6 `misc.c` `ItemReceipt_GrantWithoutAnimation` — stop forcing
      `item_receipt_method = 0` in a way that defeats vanilla's method-2
      capacity suppression
- [x] 3.7 `misc.c` `ItemReceipt_RestoreActionState` — restore the carried-object
      ancilla and `link_cape_mode` alongside the carry-state bytes, or narrow
      what the 0x20 branch destroys
- [x] 3.8 `sprite_main.c` `Sprite_GrantAnimatedOrVanilla` — do not report
      `Accepted` for the `NotActive` fallback without verifying
      `Link_ReceiveItem` delivered
- [x] 3.9 `player.c` tablet `*_StartCutscene` — do not return inside a
      half-applied caller tableau; preflight or complete
- [x] 3.10 `overworld.c` lift/smash retry — do not return a value
      indistinguishable from success while leaving the tile in place
- [x] 3.11 Agahnim and Freezor now HONOR the return (Agahnim also releases Link
      before retrying, per D2). The big-key absorb site DOCUMENTS why it cannot:
      its vanilla state bits are already committed and the RAM compare depends
      on them

## 4. Guards

- [x] 4.1 A guard for the slot-ordering pattern so D4's class cannot reappear
- [x] 4.2 Extend the grant self-checks to exercise a REFUSAL and assert caller
      state after it — the existing ones only assert result codes and inventory
      bytes, which is why none of this was caught

## 5. Validation

- [x] 5.1 MSVC + WSL `gcc -Werror` clean
- [x] 5.2 `--rando-selftest` all groups OK, including the new checks
- [x] 5.3 `run_rando_validation.py full` PASS
- [x] 5.4 Corpus regen: 0 digest changes (runtime-only) — no `kGeneratorVersion`
      bump
- [x] 5.5 Each fix negative-tested: revert it, its check must fail

## 6. Close-out

- [x] 6.1 Independent fresh-eyes review of the whole diff
- [x] 6.2 Owner playtest PASSED (2026-07-26, build at 4008f691 + the oracle
      hardening): crystal dungeon start-to-finish incl. cutscene and warp-out;
      pendant dungeon (fanfare, no eject, correct); pot lifts; pause map showing
      the right pendants and, after one crystal, that crystal plus red X's;
      BOTH medallion tablets granted with Link controllable afterward; and the
      adversarial case — a bottle collected at four bottles showed its cue and
      moved on with no loop. NOT covered: the flute/dig spot field-item path
- [x] 6.3 Reconciled and archived on the branch
