# randomizer-placement Specification (delta)

## ADDED Requirements

### Requirement: Grant refusals are classified permanent or transient

A grant plan that cannot be satisfied SHALL be classified by whether the player
can ever change the condition that refuses it. A **permanently** unsatisfiable
plan — one whose blocking state is monotonic, such as a bottle pickup when four
bottles are already owned — SHALL resolve to a terminal disposition
(`AcceptedNoOp`), consuming the check and granting nothing. Only a **transient**
refusal — one the player can clear through ordinary play, such as a potion
needing an empty bottle — SHALL resolve to `RetryableFailure`.

No grant site SHALL be required to distinguish these; the classification belongs
to plan construction, so every present and future caller inherits it.

#### Scenario: A fifth bottle terminates instead of looping

- **WHEN** a bottle pickup is delivered while the player already owns four
  bottles
- **THEN** the grant resolves terminally, the location is marked checked, no
  retry is scheduled, and the delivering site completes its normal teardown

#### Scenario: A potion still waits for a free bottle

- **WHEN** a bottle-content item is delivered with no empty bottle available
- **THEN** the grant is refused as retryable, the source is preserved, and the
  item is delivered on a later attempt once a bottle is emptied

### Requirement: A pending retry never blocks its own recovery

While a grant refusal is pending retry, the delivering site SHALL NOT hold
`flag_is_link_immobilized`, `flag_block_link_menu`, or any equivalent input lock
across frames. A site that must lock the player during delivery SHALL determine
acceptance first using the side-effect-free preparation API, and lock only on a
path that will complete.

#### Scenario: A refused grant leaves the player in control

- **WHEN** a grant is refused at a site that would immobilize the player to
  deliver it
- **THEN** the player retains movement and menu access, so any state the refusal
  depends on can still be changed

### Requirement: A refused grant leaves caller state consistent

A grant site that returns without delivering SHALL leave its caller's observable
state consistent: every out-parameter written on every return path, sprite
lifecycle either completed or fully rewound, and no persistent flag half-set. A
helper that reports acceptance after delegating to a vanilla delivery path SHALL
verify that path actually delivered rather than reporting acceptance
unconditionally.

#### Scenario: No caller consumes an unwritten out-parameter

- **WHEN** a liftable-object grant is refused and its handler returns early
- **THEN** any position or attribute out-parameter the caller will read has been
  written, and the caller does not spawn or position an object from
  uninitialized memory

#### Scenario: Two bounded exceptions are named, not implied

- **WHEN** a transient refusal reaches the big-key absorb site or Kholdstare's
  death handler
- **THEN** the enemy check is dropped rather than retried, because at the first
  the vanilla state bits are already committed and the side-by-side RAM compare
  depends on them, and at the second the retry re-enters the death handler from
  the top and its prize roll can rewrite the sprite type. Both are recorded at
  the source and are bounded losses of one check — never a softlock, never a
  lost dungeon prize on the shipped path

#### Scenario: A refused kill does not strand the room

- **WHEN** an enemy's death-time grant is refused
- **THEN** either the enemy's despawn completes normally or the enemy remains
  fully alive and re-killable — never a state where the room's
  screen-clear check can no longer be satisfied

### Requirement: Receipt delivery frees its ancilla slot before delivering

A site that delivers a randomizer grant from within an ancilla SHALL release
that ancilla's slot before invoking delivery, because the vanilla receipt
allocates from the same fixed slot pool and an exhausted pool silently degrades
delivery to a quiet fallback — losing the receipt, its presentation, and any
state its teardown owns. The slot SHALL be restored when delivery did not occur,
so a retryable refusal still leaves the source in the world.

#### Scenario: Delivery under slot pressure still produces a receipt

- **WHEN** a grant is delivered from an ancilla while the remaining slots are
  occupied by non-evictable ancillas
- **THEN** the receipt is created, because the delivering ancilla's own slot was
  released first

#### Scenario: Refused delivery restores the source

- **WHEN** delivery from an ancilla is refused as retryable
- **THEN** the ancilla is restored so the item remains collectable
