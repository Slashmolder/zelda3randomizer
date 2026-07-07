# randomizer-shuffles Specification (delta)

## ADDED Requirements

### Requirement: Enemy shuffle pins kill-gated and forced-drop rooms under the enemy-souls tier
When `souls_shuffle=bosses+enemies` (effective) and enemy shuffle are both active, the enemy-shuffle picker SHALL return the vanilla species for every room in the generated pin set (`kRandoSoulPinRooms`, baked from `soul_rooms.gen.yaml`): the kill-gated rooms with enemy-souled residents and, whole-room, the forced enemy-drop rooms — so generated soul requirements and the runtime drop-source exemption (both computed from vanilla species/slots) remain correct at runtime. All other rooms shuffle freely and suppression keys on the final substituted species; soul-less kill rooms need no pin because enemy shuffle only substitutes ESF_RANDOMIZABLE species, all of which carry souls. (Ordinary enemy-check locations never coexist with enemy shuffle — `enemy_drop_checks` already coerces Dungeon/All to Keys under enemy shuffle.)

#### Scenario: Kill room keeps vanilla species
- **WHEN** a `bosses+enemies` seed with enemy shuffle on loads a room from the kill-gated set
- **THEN** the room's enemies are the vanilla species, and the kill-gate hold/requirements match the generated soul table

#### Scenario: Key-drop guard keeps vanilla species
- **WHEN** a `bosses+enemies` seed with enemy shuffle on loads a room containing a forced enemy-drop source slot
- **THEN** that slot's enemy is the vanilla species, so its generated soul requirement matches the enemy actually present

#### Scenario: Enemy shuffle stays placement-neutral
- **WHEN** two seeds differ only in the enemy-shuffle toggle (same `souls_shuffle` tier and seed)
- **THEN** their `placement_digest` values are identical (the existing enemy-shuffle digest-neutrality invariant holds under all souls tiers)

### Requirement: Boss shuffle composes with boss souls via species-keyed suppression
Boss-soul suppression SHALL act on the sprite species loaded through the boss-shuffle render redirect, so the soul that gates a dungeon is the soul of the boss actually assigned there; room-data secondary sprites (Trinexx arms, Kholdstare shell) SHALL suppress together with their parent boss.

#### Scenario: Redirected boss room gates on assigned boss
- **WHEN** boss shuffle assigns Arrghus to Skull Woods and the player lacks the Arrghus Soul
- **THEN** entering the Skull Woods boss room spawns neither Arrghus nor its puffs, and the fight starts normally once the Arrghus Soul is owned

#### Scenario: Compound boss suppresses completely
- **WHEN** the player enters a boss room for Trinexx or Kholdstare without the matching soul
- **THEN** the parent and all room-data secondary sprites (arms, shell) are suppressed together
