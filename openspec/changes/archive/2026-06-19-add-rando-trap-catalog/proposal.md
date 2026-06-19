## Why

The randomizer already ships a **working masquerade-trap framework**, but with only **two**
effects (`TrapDamage`, `TrapFreeze`) and **no player control** over them. The whole framework
is there — placement injection into junk slots (`inject_traps_into_junk_placements`,
`src/rando/rando_placement.c`), a per-collection trigger (`rando_trigger_trap`,
`src/rando/rando.c`), a per-frame effect tick (`Rando_TickTrapEffects`), the
"You are a fool!" reveal dialogue, and a decoy-icon masquerade that makes any trap item draw
as a fake good item — but the *catalog* is thin and the *type* is chosen by a positional
`(injected & 1)` alternation that isn't even seed-deterministic.

A from-source C reimplementation can do real trap *effects* a ROM-hack randomizer cannot bolt
on cheaply: it owns the full engine, so it can fire bombs, spawn enemies, remap input, drain
resources, force a safe warp, or black out a room — all from one collection hook. Four
read-only research passes over the engine confirmed the hooks exist and which are safe (and
which famously are **not** — the Quake/Bombos medallion spells damage *enemies only*, so as
"traps" they would *help* the player; they are rejected).

This change turns the two-effect stub into a **full, player-configurable trap catalog** with a
**deterministic, seed-stable** type selector — within the existing 28-byte canonical settings
(no length-coupling cascade).

## What Changes

- **Effect catalog — ~15 effects in 5 categories** (the existing two are re-bucketed, not
  removed):
  - **HAZARD** — Damage*, Bomb, Ambush (spawn whitelisted enemies), Cucco swarm.
  - **IMPAIR** — Freeze*, Reversed controls, Scrambled/"drunk" controls, Disarmed (no attack).
  - **DRAIN** — Rupee drain, Magic drain, Ammo drain.
  - **SCARE** — Screen-shake, Darkness, Fake-teleport, Fake low-health alarm.
  - **DISPLACE** — Teleport (real safe-start warp). *(\* = exists today.)*
- **Player control — a `trap_categories` enable mask** (5 bits) packed into free canonical
  byte [27] bits 2-6. **A zero mask while `traps>0` means "all categories enabled,"** so the
  default (traps off, all bits zero) stays byte-identical and "traps on, untouched" = every
  category. `kSettingsCanonicalLen` stays **28** — no cascade. Exposed as category checkboxes
  in the native settings window under the existing Traps frequency combo.
- **Deterministic weighted selector** — replace the positional `(injected & 1)` with a
  per-`(seed, location_id)` mix (domain-separated from the decoy-icon mix) that picks an
  enabled category, then an effect within it. `traps==0` still short-circuits, so default /
  traps-off seeds stay byte-identical; the three traps-on corpus seeds move (intended, scoped).
- **Arm-in-trigger / apply-in-gated-tick dispatch** — `rando_trigger_trap` only *arms* the
  effect (sets state + a pending flag + shows the reveal); the effect's one-shot *onset* and
  sustained behavior run in `Rando_TickTrapEffects` under the existing `{7,9,11}` module gate,
  **plus `submodule_index==0`** for any effect that spawns sprites, warps, or writes PPU
  registers. This is required because the trigger fires from many grant paths in any
  module/submodule.
- **Masquerade made id-agnostic** — `rando_is_trap_item` becomes a contiguous-range check over
  the trap id block, so every new effect id auto-resolves a decoy icon and routes through the
  trap dispatch with no per-id edits.
- **Leak-safe teardown** — effects that write g_ram-backed PPU registers (Darkness → COLDATA,
  Screen-shake → `bg1_x/y_offset`) restore them on stun expiry and are re-derived on a
  rando-active room load, so a save / Ctrl+F1 snapshot taken mid-effect can never strand a dark
  room or a frozen camera (the effect timer is file-static and resets on load, but the PPU
  bytes are in the saved image).
- **Version lock + corpus regen** — bump `kGeneratorVersion`; regenerate the three traps-on
  corpus entries (`open/fast_ganon` low/medium/high); every traps-off seed stays byte-identical.

## Capabilities

### New Capabilities

- `randomizer-traps`: the trap catalog, category enable mask + zero-sentinel semantics,
  deterministic selector, arm/apply dispatch model, per-effect safety contracts (harm model,
  context-fallback, leak-safe teardown, the proxy-byte exclusions), the masquerade range gate,
  and the native-window category UI. Formalizes the existing two-effect behavior and extends it.

### Modified Capabilities

- `randomizer-core`: MODIFY the "Settings canonical serialization order (normative)"
  requirement — register `trap_categories` in canonical byte [27] bits 2-6 (previously
  reserved), update the byte-27 row and the Phase A defaults scenario (`trap_categories=0`,
  i.e. all categories). `kSettingsCanonicalLen` is unchanged; the `generator_version`
  *semantics* requirement is unchanged, so the bump is an inline note, not a delta target
  (matching the enemy-sheet-widening convention). **Incidental correction:** because OpenSpec
  replaces the whole requirement, the reproduced text also corrects the byte-[26] row and the
  defaults scenario to reflect `instant_flute` (canonical [26] bit4, inverse, default on),
  which the live spec omits — verified against `rando_settings.h` (`kInstantFluteAxis_ManualActivation = 1u << 4`).
  This is a pre-existing drift fix, not a behavior change of this proposal.

## Impact

- **Code**: `src/rando/rando.c` (range-check `rando_is_trap_item`; `kRandoTrapEffect_*` per
  effect; table-driven arm/onset/sustain/teardown dispatch; per-effect reveal sfx; new
  static effect state; `rando_clear_trap_effect` + room-load re-derive for PPU effects),
  `src/rando/rando_placement.c` (deterministic category→effect selector; update the placement
  self-check that today asserts a Damage/Freeze balance), `assets/rando/item_registry.yaml`
  (contiguous trap id block), `src/rando/rando_settings.{h,c}` (`trap_categories` field +
  canonical pack/unpack + defaults + the `kExpectedCanonical`/`kExpectedHash` are unchanged
  because defaults stay zero), `src/rando/rando_window/rando_window.cpp` (category checkboxes),
  `src/rando/rando.h` (`kGeneratorVersion`). Possibly a small enemy GFX-sheet-safe whitelist for
  Ambush and a boss-room exclusion guard for spawn effects.
- **Generated data**: none — trap ids are structural registry entries (codegen emits
  `ITEM_Trap*` + names); no new asset blob. `_Static_assert(ID_TrapX == ITEM_TrapX)` guards the
  hand-mirrored placement enum against registry drift.
- **Corpus / determinism**: the three traps-on seeds' `placement_digest` move (the type at each
  trap slot changes); their sphere digests and every traps-off seed stay byte-identical.
  Regenerate via `bump_rando_corpus.py --apply` and 3-way diff vs `main` to confirm only those
  three moved.
- **Build traps**: editing `kGeneratorVersion` in `rando.h` requires `make clean` (no header-dep
  tracking under the Makefile); registering each new `.c` is unnecessary (no new TU). If the
  `RandoSettings` struct gains the `trap_categories` field, every TU including `rando_settings.h`
  must rebuild — `make clean` covers it.
- **Verification**: corpus regen (3 expected movers), extended `--rando-selftest` (per-effect
  arm/onset assertions; masquerade-decoy resolves for every id in the trap range), and — the
  load-bearing net — **owner end-to-end playtest** across all five categories, both worlds,
  indoors/outdoors, and a save/snapshot-mid-effect, since runtime trap dispatch is invisible to
  the corpus and selftests (generation-only).
