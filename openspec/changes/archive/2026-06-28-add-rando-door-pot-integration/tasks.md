# Door shuffle x Pot shuffle integration - tasks

## 1. Spec and review

- [x] 1.1 Draft the OpenSpec change for settings, logic, placement, door shuffle,
  and UI/docs behavior.
- [x] 1.2 Send the draft to an adversarial reviewer and iterate until no
  correctness-blocking feedback remains.

## 2. Generated metadata

- [x] 2.1 Extend the key-depth/pot-depth artifact to expose exact key-pot door
  regions, matching drop-key row indices, and room door-region lists for non-key
  pots.
- [x] 2.2 Extend `rando_logic_gen.py` to emit generated pot-door rows and region
  lists into `logic_data.c`.
- [x] 2.3 Add generated-data declarations to `rando_logic.h`.
- [x] 2.4 Add fail-closed codegen validation for stale bridge artifacts and
  missing key-pot door/drop joins; leave non-key pots with no door-table room row
  on the vanilla predicate path.
- [x] 2.5 Emit a stable bridge digest over generated pot-door rows and bridge
  readiness; include it in door+pot generation and activation drift checks.
- [x] 2.6 Preserve and compile a base pot predicate before static `POT_KEYS_*`
  wrapping for use by the active door-oracle branch.

## 3. Door oracle and key prover

- [x] 3.1 Extend `OP_DOORS_LOC_REACHABLE` to resolve generated pot-door rows after
  the existing static table lookup misses.
- [x] 3.2 Pass the active pot tier into `DoorShuffle_Generate()` and persist it in
  `DoorShuffleLayout`/digest input.
- [x] 3.3 Count active non-empty pot rows as itemized key-source locations in
  `DoorKeys_ShuffleDungeon()`; do not count `ITEM_Nothing` empty pots.
- [x] 3.4 Exclude itemized key-pot drop rows from free drop-key accounting through
  one shared helper used by the prover, oracle, self-tests, and key-depth dump;
  leave non-pot drop rows free.
- [x] 3.5 Split small-key-source counts from big-key-availability counts, and keep
  placement's big-key rejection exactly synchronized with prover big-key counts.

## 4. Settings, placement, UI, docs

- [x] 4.1 Make `Settings_PotShuffleForcedOff()` cave-entrance-only and update all
  comments/self-checks that described door shuffle as forced off.
- [x] 4.2 Keep door shuffle's effective small/big key mode as Dungeon and verify
  pot-key gates share `Settings_PotKeysActive()`.
- [x] 4.3 Update placement self-checks so door+pot activates the requested tier,
  while cave+pot still activates zero pots.
- [x] 4.4 Update spoiler effective settings, native UI disabled text, and
  `docs/randomizer.md`.
- [x] 4.5 Add or update corpus entries for active door+pot generation.

## 5. Verification and adversarial implementation review

- [x] 5.1 Run source-level guards and OpenSpec validation.
- [x] 5.2 Build Release x64 into a separate output directory and run
  `--rando-selftest` and `--door-selftest`.
- [x] 5.3 Prepare local pot artifacts, rerun codegen, and run the local pot checks
  when the binary is available.
- [x] 5.4 Run focused door+pot smoke generation and full corpus when artifacts are
  available.
- [x] 5.5 Add self-checks for bridge digest drift, drop exclusion, empty-pot
  exclusion, big-key/prover agreement, and assetless/partial-artifact refusal.
- [x] 5.6 Compare implementation against this plan.
- [x] 5.7 Send the implementation to an adversarial reviewer and iterate until no
  correctness-blocking feedback remains.
