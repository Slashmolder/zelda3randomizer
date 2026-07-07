# Delta: randomizer-logic (NPC souls)

## ADDED Requirements

### Requirement: NPC-driven checks require the involved souls

Every roster-gated check's reachability predicate SHALL AND in `(NOT OP_NPC_SOULS_ACTIVE) OR HAS_ITEM(Soul_<person>)` for EVERY person involved in obtaining the check, injected by the logic codegen from the committed `npc_souls.yaml` gate table (never via logic_parts duplicate entries). Multi-person checks: Maze Race requires both race NPCs' souls; Blacksmith requires Home Smith + Frog; Purple Chest requires Home Smith + Frog + Middle-Aged Man; Pyramid Fairy checks require Pyramid Fairy + Bomb Shop dealer.

#### Scenario: Maze Race gated on both people
- **WHEN** `npc_souls=on` and the player owns the Maze Game Lady Soul but not the Maze Game Guy Soul
- **THEN** the Maze Race check is out of logic, and the placer never requires it before both souls are reachable

#### Scenario: Off-tier collapse
- **WHEN** `npc_souls=off`
- **THEN** every injected soul term evaluates true and reachability is identical to the pre-feature graph (placement-digest-proven)

### Requirement: Kiki's soul gates Palace of Darkness entry

With `npc_souls=on`, the Palace of Darkness entry edge SHALL require the Kiki Soul in world states where vanilla PoD entry is opened by Kiki's payoff; world states (or routes) that legitimately enter PoD without Kiki (e.g. a dungeon-chains seam entry, or Inverted access if the inverted graph does not route through Kiki) SHALL NOT carry the term on those routes.

#### Scenario: PoD locked without Kiki
- **WHEN** an Open-state seed has `npc_souls=on` and the Kiki Soul is not yet reachable
- **THEN** no Palace of Darkness location (including its Boss and Prize) is in logic, and the assumed fill never strands PoD-mandatory progression behind the Kiki Soul's own gate

#### Scenario: Chains seam bypass stays consistent
- **WHEN** dungeon chains route Palace of Darkness as a successor dungeon and the seam teleports the player into its lobby
- **THEN** the seam entry requires no Kiki Soul (matching runtime, where the overworld entrance is not used)

### Requirement: The new predicate op is structurally complete

`OP_NPC_SOULS_ACTIVE` SHALL be appended to the op registry, evaluated by the VM, and covered by the skip walker's operand table; the structural selfcheck walk over all generated predicate blobs SHALL pass with the new wraps emitted.

#### Scenario: Selfcheck catches a skip mismatch
- **WHEN** the op is wired into `eval()` but its operand layout is missing or wrong in `skip_pred`
- **THEN** `Logic_SelfCheck`'s blob walk fails before the build ships
