# Delta: randomizer-core (NPC souls)

## ADDED Requirements

### Requirement: NPC souls join the progression pool

With `npc_souls=on`, the pool SHALL contain exactly 23 NPC soul items (the contiguous registry block immediately following the enemy-souls block — ids 196-218 at authoring time), classified as progression and displacing junk; with `npc_souls=off` the pool SHALL contain none of them. Pool self-checks SHALL assert both counts and that the total progression count stays within the placer's fixed-capacity arrays.

#### Scenario: Pool counts per setting
- **WHEN** `Placement_SelfCheck` builds pools with `npc_souls` on and off
- **THEN** the on-pool carries 23 NPC souls as progression and the off-pool carries 0

### Requirement: NPC souls activate the collecting fill model

The assumed-fill placer SHALL use the placed-item collecting reachability model (fix-point collection, own-id excluded) whenever `npc_souls=on`, in addition to its existing enemies-tier trigger; seeds with `npc_souls=off` SHALL keep their existing model selection unchanged.

#### Scenario: Cross-gated souls place soundly
- **WHEN** `npc_souls=on` and two NPC souls could otherwise mutually lock (each placed behind the check the other gates)
- **THEN** the per-turn collecting reachability prevents certifying either placement against a phantom grant, and the accepted seed's sphere walk confirms both souls collectable

#### Scenario: Model selection is inert when off
- **WHEN** `npc_souls=off`
- **THEN** the fill-model choice, attempt acceptance, and placement digest are byte-identical to pre-feature behavior for every settings combination
