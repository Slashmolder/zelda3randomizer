## ADDED Requirements

### Requirement: Generic small-key door reachability

When `genericKeys` is in effect (Retro), key-door reachability SHALL evaluate
against the **shared `GenericKey` count** rather than per-dungeon
`SmallKey_<Dungeon>` possession. The placer SHALL guarantee that the generic-key
pool is reachable in an order that never strands the player behind a locked door
they cannot open (the assumed-fill shared-key invariant — see ALTTPR
`app/Filler/RandomAssumed.php`). A predicate that today reads
`HAS_ITEM_COUNT(SmallKey_<Dungeon>, n)` SHALL, under `genericKeys`, be satisfied
by the shared generic-key count subject to that no-strand guarantee.

When `genericKeys` is NOT in effect, key-door predicates SHALL be byte-identical
to the per-dungeon behavior (the generated logic for non-Retro seeds is
unchanged).

> **Stub status**: the exact mechanism (logic-graph count substitution vs.
> assumed-fill-native fungible progression vs. logic-free-doors-with-floor) is
> chosen at apply-time after a prototype; `design.md §2b` favors the
> assumed-fill-native path. The acceptance bar is identical regardless: a
> playtest of each goal at hard pool with no key-strand.

#### Scenario: Any key opens any door at runtime
- **WHEN** the player holds one or more generic keys in a Retro seed and reaches
  any small-key door in any dungeon
- **THEN** the door opens and the shared key count decreases by one, regardless of
  which dungeon the key was found in

#### Scenario: Placer never strands a Retro seed
- **WHEN** a Retro (genericKeys) seed is generated
- **THEN** every locked door on the path to the goal is reachable with keys the
  player can collect first; the seed is `goal_completable` with no unreachable
  placements across the corpus's Retro entries (including a hard-pool seed)

#### Scenario: Non-Retro reachability unchanged
- **WHEN** logic is generated for a non-Retro seed
- **THEN** key-door predicates gate on per-dungeon `SmallKey_<Dungeon>` exactly as
  before this change
