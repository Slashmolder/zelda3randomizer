# Proposal: fix-grant-refusal-contract

## Why

`39e35832` ("Harden randomizer validation and grant transactions", +4197/−1527
across nine engine files) moved the randomizer from *substituting a value into
the vanilla code path* to *owning delivery and returning a result*. Call sites
gained a three-way contract — terminal (`Accepted`/`AlreadyChecked`), refused
(`Retryable`/`Invalid`), or `NotActive` — and typically respond to a refusal by
re-arming and trying again next frame.

**That contract assumes a refusal is transient. The dominant refusal is
permanent.** `rando_plan_bottle_has_slot` refuses a bottle *pickup* when every
`bottle[i] >= 2` — the player already owns four. Bottle count never decreases,
so the refusal can never clear, and "retry next frame" is an infinite loop. It
is reachable: the placer can still place a fifth bottle (the deferred
"5th-bottle fill-time refusal" item).

Several retry paths make it unrecoverable rather than merely stuck: they set
`flag_is_link_immobilized`, which locks out movement *and* the menu — and the
menu is the only way a player could ever change bottle state. The recovery
action is blocked by the retry itself.

A six-file audit of all 62 result-branching grant sites found four SEVERE
consequences, three of them sharing that single root:

1. `sprite.c` `SpriteExplode_SpawnEA` — a blocked grant sets
   `flag_is_link_immobilized = 1` and oscillates on a 33-frame delay forever.
2. `sprite.c` `Sprite_DoTheDeath` / `Sprite_ManuallySetDeathFlagUW` — a blocked
   grant returns before `sprite_state[k] = 0`, so the enemy never despawns,
   `Sprite_CheckIfScreenIsClear` never returns true, and every kill-gated door,
   chest and boss prize in the room stops firing.
3. `player.c` `Link_ReceiveItem` quiet fallback — clears
   `flag_is_link_immobilized` for receipt method 3 only; methods 0/1 bank the
   item and leave Link immobilized with no receipt to release him. (Pool
   exhaustion, not a refusal — independent root, same symptom class.)
4. `dungeon.c` `Dungeon_LiftAndReplaceLiftable` — the retry path returns without
   ever writing `*pt`, and `Link_APress_LiftCarryThrow` uses it regardless,
   spawning a throwable at uninitialized stack coordinates. Undefined behavior.

## What Changes

- **A permanently-unsatisfiable plan resolves TERMINALLY, never retryably.** The
  codebase already has this class: `rando_plan_receive_is_at_cap` routes
  "no useful state left to gain" to `AcceptedNoOp`. A fifth bottle belongs to
  it. Refusals that the player *can* clear (a potion needing an empty bottle —
  drink one) stay `Retryable`. This defuses roots 1 and 2 at the source: the
  loops never engage.

- **A pending retry SHALL NOT block its own recovery.** No site may hold
  `flag_is_link_immobilized`, `flag_block_link_menu`, or an equivalent input
  lock across frames while waiting on a refusal the player must act to clear.

- **A refused grant SHALL leave caller state consistent.** Out-parameters are
  written on every return path (`*pt`), sprite lifecycle either completes or is
  fully rewound, and no caller may treat "returned" as "delivered".

- **Sweep the slot-ordering class, not one instance.** Freeing the ancilla slot
  *before* delivering was fixed for boss prizes in `a5ef5eb0`; the tablet and
  flute-spot sites have the identical inversion and are fixed here in the same
  pass, with a guard so the pattern cannot reappear.

- **The remaining audit findings** (ToH basement-cage lost `!IsLocationChecked`,
  Kholdstare's non-idempotent retry losing its prize permanently, a dead
  early-return in `AncillaAdd_ItemReceipt` that went live, method-2 capacity
  suppression defeated by the quiet path, the overworld lift duplicating the
  carried object) are fixed and enumerated in `tasks.md`.

- **Self-checks that drive the real paths.** Every fix lands with a check that
  fails when the fix is reverted. The refusal contract gets a matrix over
  (permanent, transient, at-cap, unsatisfiable) × (terminal, retryable), because
  the existing grant self-checks assert result codes and inventory bytes and
  never exercised a refusal loop or a caller's post-refusal state.

## Impact

- Affected specs: `randomizer-placement` (grant dispatch contract).
- Affected code: `src/rando/rando.c` (plan disposition), `src/sprite.c`,
  `src/sprite_main.c`, `src/ancilla.c`, `src/player.c`, `src/misc.c`,
  `src/dungeon.c`, `src/overworld.c`.
- **Runtime-only**: no generation path changes, so `kGeneratorVersion` is NOT
  bumped and the corpus must stay byte-identical (verified by regen).
- The vanilla (`kFeatures1_RandomizerActive` clear) path is untouched throughout.
- A fifth bottle becomes a consumed check that grants nothing. That is a real
  cost, and the honest one: the alternative today is a softlock. Preventing the
  placement at fill time remains the separate, already-tracked follow-up.
