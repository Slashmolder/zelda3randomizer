## ADDED Requirements

### Requirement: Insanity (decoupled) entrance mode

The entrance-shuffle module SHALL expose the **Insanity** preset via the `decoupled` axis: any overworld entrance MAY map to any interior, including cross-mappings between cave and dungeon interiors, and entrances are decoupled per endpoint (entering door A and exiting need not return to A). The permutation SHALL remain deterministic from `(share_string, generator_version)` and SHALL preserve goal reachability per `randomizer-logic`. Insanity composes on top of the `shuffle_cave_entrances` / `shuffle_dungeon_entrances` axes from `add-rando-entrance-shuffle`.

> **Stub status**: Insanity's deterministic permutation + logic landed with
> `add-rando-entrance-shuffle` (generation-only). This requirement covers the
> **runtime** that makes it playable — the cave-arrival asset fork (per-cave
> arrival positional data, not derivable from the existing exit tables) plus the
> arrival replay. Carved here because that runtime is blocked on the asset fork.
> Apply-time design TBD.

#### Scenario: Insanity permits cave-to-dungeon mappings
- **WHEN** entrance shuffle is Insanity
- **THEN** any overworld entrance may map to any interior, including cross-mappings between cave and dungeon interiors

#### Scenario: Decoupled arrival deposits the player correctly
- **WHEN** a decoupled cave entrance is entered and the player arrives in the mapped interior
- **THEN** the runtime places the player at that interior's arrival position (via the cave-arrival fork data), with no softlock or out-of-bounds arrival

#### Scenario: Insanity preserves goal reachability
- **WHEN** an Insanity seed is generated
- **THEN** the goal-reachability predicate (per `randomizer-logic`) passes for the resulting entrance map, identically to generation time (runtime never diverges from the model)
