# Design: fix-grant-refusal-contract

## The contract as it stands

`Rando_PrepareGrant` builds a `RandoGrantPlan` whose `disposition` is one of:

| disposition | caller meaning | set by |
| --- | --- | --- |
| `Receive` | deliver via the vanilla receipt | a receive code that fits |
| `Direct` | write the state directly | the direct opcodes |
| `AcceptedNoOp` | nothing to gain; consume the check | `rando_plan_receive_is_at_cap` |
| `RetryableFailure` | **try again later** | `rando_plan_bottle_has_slot` == false |
| `Invalid` | malformed | validation |

`Rando_CommitPreparedGrant` maps `RetryableFailure` → `kRandoGrantResult_Retryable`
and `Invalid` → `kRandoGrantResult_Invalid`. Sites treat both as "not delivered,
keep the source alive, come back next frame".

## The defect

`rando_plan_bottle_has_slot` answers two different questions with one boolean
(`rando.c`):

- **bottle PICKUP** codes (`0x16, 0x2b, 0x2c, 0x2d, 0x3d, 0x3c, 0x48`): needs a
  slot with `bottle[i] < 2`, i.e. an *unowned* slot. Bottle count is monotonic
  — nothing in the game ever reduces it — so once four are owned this is
  **permanently false**.
- **bottle CONTENT** codes (`0x2e, 0x2f, 0x30, 0x0e`): needs `bottle[i] == 2`,
  an *empty* bottle. The player can create one by drinking. **Transient.**

Both route to `RetryableFailure`. The permanent one turns every "re-arm and
retry" site into an infinite loop, and the sites that immobilize while retrying
remove the only means of recovery.

The distinction already exists elsewhere: `rando_plan_receive_is_at_cap` sends
"no useful state left to gain" to `AcceptedNoOp` (heart containers at max
capacity, refills when full). A fifth bottle is that, not a temporary shortage.

## Decisions

**D1 — Permanent refusals join the at-cap class; transient ones stay retryable.**
Split the bottle predicate into `permanent` and `transient` outcomes. Permanent
⇒ `AcceptedNoOp` (check consumed, nothing granted, no loop). Transient ⇒
`RetryableFailure`, unchanged. This is the smallest change that removes the
infinite-loop mode without weakening the lossless-retry guarantee that motivated
the transaction rework — potions still wait for a free bottle.

Rejected: making *all* refusals terminal (loses the lossless-retry property the
rework exists for); fixing each looping site individually (four sites today,
unbounded tomorrow, and it leaves the contract itself lying).

**D2 — A pending retry must not block its own recovery.** Even a legitimately
transient refusal is unrecoverable if the site holds an input lock while
waiting. No site may hold `flag_is_link_immobilized` / `flag_block_link_menu`
across frames pending a refusal the player must act to clear. Sites that need to
immobilize during delivery must preflight with the side-effect-free
`Rando_PrepareGrant` *before* locking — the pattern `sprite_main.c` already uses
correctly at the Master Sword, Magic Bat, Smithy and Old Man.

**D3 — A refusal leaves caller state consistent, and "returned" never means
"delivered".** Concretely: every out-parameter is written on every return path;
sprite lifecycle either completes or fully rewinds; and helpers that report
`Accepted` after falling back to a vanilla call must verify that call actually
delivered (the `NotActive` fallback in `Sprite_GrantAnimatedOrVanilla` reports
`Accepted` unconditionally, while `Link_ReceiveItem` gained a rando early-return
in the same commit that can silently do nothing).

**D4 — Sweep the slot-ordering class.** `a5ef5eb0` fixed boss prizes: free the
ancilla slot *before* delivering, because the receipt allocates from the same
five-slot pool and a full pool degrades `Link_ReceiveItem` to a quiet fallback.
The tablet and flute-spot sites have the identical inversion. Fixing one
instance of a pattern and leaving its siblings is exactly how the crystal-bit
permutation survived four years of review; the pattern gets a guard.

**D5 — Runtime-only, corpus byte-identical.** Nothing here touches generation.
`kGeneratorVersion` is not bumped, and the claim is verified by a corpus regen
rather than asserted.

**D6 — Accepting the cost of a consumed fifth bottle.** Under D1 a fifth bottle
marks its location checked and grants nothing — a real loss of one check. It is
strictly better than the current behavior (softlock), and the proper fix is at
fill time, where the placer should not put a fifth bottle in the pool at all.
That is separately tracked and out of scope here; this change is the runtime
safety net, and it must remain correct even after the placer is fixed.

## Risks

- **The audit findings are claims.** Six agents produced them; one finding in an
  earlier round this session was a false positive and one of my own was
  over-broad. Each fix re-derives its own premise from source before landing,
  and anything that does not reproduce is recorded as refuted rather than
  quietly "fixed".
- **Reachability varies.** Some findings need `enemy_drop_checks=all`, a
  customizer exclusion, or a full ancilla pool. Low reachability is not a reason
  to skip a softlock, but it *is* a reason to rank, and severity here is ranked
  by consequence-if-hit, not by probability.
- **`AcceptedNoOp` marks the location checked.** Confirm no surface (tracker,
  goal completion, hint) treats a no-op'd location as an item the player holds.
