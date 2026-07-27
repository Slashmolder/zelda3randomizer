# Tasks

## 1. Guard first (red before green)

- [x] 1.1 Add `--rando-shop-doorwalk`: walk every `kOverworld_Entrance_Id` row
      through the engine's `lx -> which_entrance -> room` chain and ask the live
      `Rando_ShopSlotCheckInfo` what it yields; fail non-zero on any unreachable
      or multiply-reached shop slot.
- [x] 1.2 Register the shop probe + door-walk flags as headless so a setup
      failure cannot pop a modal dialog on the developer's desktop.
- [x] 1.3 Confirm the guard FAILS on unmodified main (recorded: 15/27
      unreachable, 6 collided).

## 2. Shop identity keyed by the overworld door

- [x] 2.1 Add `kRam_RandoOverworldDoor` (0x66d) to the `features.h` reserved
      compat block and document it in the layout comment.
- [x] 2.2 Capture the entered door id (`lx + 1`) at the overworld entry hook in
      `src/overworld.c`, alongside the existing take-any capture; clear it on
      the fall-hole / non-door paths that already clear the take-any id.
- [x] 2.3 Rewrite `shop_lookup` to take the door id; mark rooms `0x0FF`,
      `0x110`, `0x11F` `room_only`; keep `0x10F` and `0x112` door-keyed.
- [x] 2.4 Update the shop call sites in `src/sprite_main.c` to pass the captured
      door id instead of `which_entrance`.
- [x] 2.5 Correct the provenance comment above `kRandoShopSlots` — record that
      the door column is ALTTPR's `PreviousOverworldDoor` (`lx + 1`), NOT the
      entrance id, and cite `doorframefixes.asm`.
- [x] 2.6 Re-run the door-walk: expect 0 unreachable, 0 collided.

## 3. Normalize the axis off under cave-entrance shuffle

- [x] 3.1 Add `Settings_ShopsanityForcedOff` to `rando_settings.{c,h}`,
      parallel to `Settings_PotShuffleForcedOff`.
- [x] 3.2 Clear `s->shopsanity` in `apply_derived_rules` when it fires, with a
      comment recording why region-rebinding alone cannot fix the composition.
- [x] 3.3 Verify the repro seed
      (`shopsanity=True,shuffle_cave_entrances=True`) now generates with no
      shop-class placements and serializes the axis off.

## 4. Validation

- [x] 4.1 `--rando-selftest` green.
- [x] 4.2 MSVC build clean; WSL `gcc -Werror` build clean.
- [x] 4.3 Bump `kGeneratorVersion`; regenerate the corpus and confirm the
      movement is SCOPED — shopsanity seeds move, `shopsanity=false` seeds stay
      byte-identical.
- [x] 4.4 CI guards: `check_audit_guard.py --strict`, `check_determinism.py`,
      `check_codegen_wiring.py`, `check_grant_consumers.py`.
- [x] 4.5 Independent fresh-eyes review before declaring done. Found 5 new
      issues beyond the self-caught init-order guard; all fixed:
      door-walk not wired into any validation profile (it needed a sidecar —
      made sidecar-free via a gate-free resolver and wired into
      `run_rando_validation.py`), the shop debug dump still printing the
      superseded key, the PC settings UI missing the forced-off coupling, the
      spoiler settings echo missing `shopsanity`, and save-and-quit not
      retiring the door byte.

## 5. Docs & spec

- [x] 5.1 Update `docs/randomizer.md` for the new composition rule.
- [x] 5.2 Reconcile the spec delta against as-built source (room-only scope
      corrected to the one door-less shop after self-review).
- [ ] 5.3 Archive the change on the branch, then squash-merge to main
      (deliberately left for the owner — gated on the playtest below).

## 6. Owner playtest (cannot be automated)

Owner-playtested 2026-07-27; all three pass.

- [x] 6.1 Village of Outcasts and Lumberjack Hut (both room `0x10F`) offer
      DIFFERENT items. Before the fix all four room-`0x10F` doors served the
      Outcasts' slots and Lumberjack's own group (243-245) was one of the
      fifteen unreachable ones, so buying from it at all is the fix confirmed
      at runtime.
- [x] 6.2 Dark World Death Mountain and Light World Lake Hylia (both room
      `0x112`) show DIFFERENT items. Before the fix both doors served Lake
      Hylia's 261-263 and Death Mountain's 252-254 were unreachable, so this is
      the second shared-room group confirmed separated. Note both shops have
      identical VANILLA contents (Red Potion / Heart / 10 Bombs), so distinct
      inventories here can only come from the door key resolving correctly.
- [x] 6.3 Snapshot replay (`Ctrl+F1`) inside a Dark World shop keeps the slots
      resolving — the case the `g_ram`-resident door key exists for, since a
      replay-restore restores RAM but not C statics. Save-and-quit separately
      behaves correctly: the purchased slot reverts to its vanilla item
      (location 246's vanilla item IS RedPotion) while the two unbought slots
      still show their randomizer items, which is the buy-once-then-vanilla-
      restock contract rather than a lost check. Worth recording because a
      restocked slot and a slot that failed to reload look identical if you
      only inspect the one you bought — the unbought slots are the
      discriminator.
