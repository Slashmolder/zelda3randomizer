# Tasks: fix-grant-refusal-contract

Every task re-derives its own premise from source before landing. The audit
findings are agent-produced claims; anything that does not reproduce is recorded
as REFUTED in this file rather than quietly "fixed".

## 1. The systemic fix (do first — it defuses SEVERE 1 and 2)

- [ ] 1.1 Split the bottle predicate in `rando.c` into permanent (pickup at four
      bottles — monotonic) vs transient (content with no empty bottle). Permanent
      ⇒ `AcceptedNoOp`; transient ⇒ `RetryableFailure`
- [ ] 1.2 Confirm no surface treats an `AcceptedNoOp` location as an item held
      (tracker item view, goal completion, hints, live reachability)
- [ ] 1.3 Self-check matrix over (permanent, transient, at-cap, unsatisfiable) ×
      expected disposition, driving `Rando_PrepareGrant` itself

## 2. SEVERE sites

- [ ] 2.1 `sprite.c` `SpriteExplode_SpawnEA` — no `flag_is_link_immobilized`
      held across frames pending a refusal; preflight before locking
- [ ] 2.2 `sprite.c` `Sprite_DoTheDeath` / `Sprite_ManuallySetDeathFlagUW` — a
      refused death either completes the despawn or leaves the enemy fully
      re-killable; never a state where screen-clear can't be satisfied
- [ ] 2.3 `player.c` `Link_ReceiveItem` quiet fallback — clear
      `flag_is_link_immobilized` for receipt methods 0/1 as the vanilla receipt
      teardown does (method 2 deliberately excluded)
- [ ] 2.4 `dungeon.c` `Dungeon_LiftAndReplaceLiftable` — write `*pt` on the
      retry return path (and/or make the caller honor the return code)

## 3. MODERATE sites

- [ ] 3.1 `ancilla.c` tablet (`Ancilla29_CommitStoredRandoGrant`) — free the
      ancilla slot before delivering, restore when not delivered (the `a5ef5eb0`
      pattern)
- [ ] 3.2 `ancilla.c` flute spot (`Ancilla36_Flute`) — same inversion; also
      ensure `Invalid` does not leave a permanently floating ancilla
- [ ] 3.3 `sprite.c` ToH basement cage — restore the `!Rando_IsLocationChecked`
      term so a later non-enemy-drop key in room 0x87 is not swallowed
- [ ] 3.4 `sprite.c` `SpriteDeath_Func4` — make the Kholdstare retry idempotent
      (resume at the boss branch; do not re-roll the prize and lose the
      `type == 0xa2` arm)
- [ ] 3.5 `misc.c` `AncillaAdd_ItemReceipt` — move the `ItemReceipt_GrantInventory`
      reject above `Ancilla_AddAncilla` / the immobilize set, so a live failure
      cannot leak a half-built receipt with Link locked
- [ ] 3.6 `misc.c` `ItemReceipt_GrantWithoutAnimation` — stop forcing
      `item_receipt_method = 0` in a way that defeats vanilla's method-2
      capacity suppression
- [ ] 3.7 `misc.c` `ItemReceipt_RestoreActionState` — restore the carried-object
      ancilla and `link_cape_mode` alongside the carry-state bytes, or narrow
      what the 0x20 branch destroys
- [ ] 3.8 `sprite_main.c` `Sprite_GrantAnimatedOrVanilla` — do not report
      `Accepted` for the `NotActive` fallback without verifying
      `Link_ReceiveItem` delivered
- [ ] 3.9 `player.c` tablet `*_StartCutscene` — do not return inside a
      half-applied caller tableau; preflight or complete
- [ ] 3.10 `overworld.c` lift/smash retry — do not return a value
      indistinguishable from success while leaving the tile in place
- [ ] 3.11 `sprite.c` / `sprite_main.c` ignored `bool` returns
      (`Sprite_ManuallySetDeathFlagUW` at 3 sites) — honor or document

## 4. Guards

- [ ] 4.1 A guard for the slot-ordering pattern so D4's class cannot reappear
- [ ] 4.2 Extend the grant self-checks to exercise a REFUSAL and assert caller
      state after it — the existing ones only assert result codes and inventory
      bytes, which is why none of this was caught

## 5. Validation

- [ ] 5.1 MSVC + WSL `gcc -Werror` clean
- [ ] 5.2 `--rando-selftest` all groups OK, including the new checks
- [ ] 5.3 `run_rando_validation.py full` PASS
- [ ] 5.4 Corpus regen: 0 digest changes (runtime-only) — no `kGeneratorVersion`
      bump
- [ ] 5.5 Each fix negative-tested: revert it, its check must fail

## 6. Close-out

- [ ] 6.1 Independent fresh-eyes review of the whole diff
- [ ] 6.2 Owner playtest of the reachable paths (bottle at capacity, a boss
      whose drop is a check, a pot lift)
- [ ] 6.3 Reconcile deltas against as-built, then archive on the branch
