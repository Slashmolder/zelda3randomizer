# Delta: randomizer-souls (NPC souls)

## ADDED Requirements

### Requirement: NPC souls toggle

The randomizer SHALL provide an `npc_souls` setting (`off`/`on`, default `off`), independent of `souls_shuffle`, serialized in canonical byte [28] bit 4 and covered by the settings hash and share string. It SHALL NOT be degraded or coerced by any other axis (door shuffle included — no gated check is door-oracle-controlled).

#### Scenario: Off is inert
- **WHEN** a seed is generated with `npc_souls=off` (default)
- **THEN** the placement is byte-identical to the same seed before this feature existed, and no NPC spawn behavior changes at runtime

#### Scenario: Composes with souls tiers
- **WHEN** `npc_souls=on` is combined with any `souls_shuffle` tier (off, bosses, all) and any world state
- **THEN** the seed generates with the 23 NPC souls in the progression pool alongside whatever the tier adds, with no derived-rule interaction

#### Scenario: Cross-version share strings
- **WHEN** a share string with `npc_souls=on` (canonical [28] bit 4 set) reaches a pre-feature build, or a pre-feature share string reaches this build
- **THEN** the old build refuses the new string (its bits-4-7 corruption check, the established forward-compat behavior), and the new build accepts the old string with `npc_souls=off` — while continuing to refuse strings with bits 5-7 set

### Requirement: NPC spawn suppression is site-scoped

With `npc_souls=on` and the corresponding soul un-owned, a roster NPC SHALL NOT spawn; suppression SHALL be scoped by spawn site — (sprite type, room/area, world) from the committed site table, never by subtype (subtype is derived after the spawn decision and is unavailable at the hooks) — so actors sharing a sprite id with roster NPCs are unaffected: Aginah (0x16, different room), the light-world flute boy (0x2E, light world), shopkeepers/thieves/take-any hosts (0xBB, different rooms), and the 0xC0 reward-delivery spawns all behave normally. The static-hook NPC branch SHALL consume the sprite slot exactly like the enemy-souls suppression branch (structural slot indices and `sprite_where_in_room` bits stay stable for later entries in the room's list), and SHALL NOT increment the kill-gate suppressed-spawn counter. Room-keyed (interior) sites SHALL be world-agnostic (Inverted swaps interior entering-worlds).

#### Scenario: Suppressed NPC does not shift sibling check slots
- **WHEN** the Mini Moldorm Cave NPC (room 0x123) is suppressed and `enemy_drop_checks` has that room's slot-keyed enemy checks active
- **THEN** the four enemy-check rows at slots 0-3 grant against their original slot indices, unshifted

#### Scenario: Stumpy absent, flute boy unaffected
- **WHEN** `npc_souls=on` and the Stumpy Soul is not owned
- **THEN** the Dark World tree kid does not spawn, while the light-world flute boy (same sprite id 0x2E) spawns normally

#### Scenario: King Zora's grant survives Catfish suppression
- **WHEN** the King Zora Soul is owned but the Catfish Soul is not
- **THEN** King Zora spawns and his check grants normally (the 0xC0-typed reward delivery spawn is never suppressed); the Catfish does not emerge when an item is thrown into the circle of stones

### Requirement: Scripted and contact-granted checks gate at their interaction trigger

The Catfish emergence (item throw), the Witch check (mushroom hand-in + powder-bag grant branch), and the Waterfall/Pyramid Fairy pond grants SHALL be gated at their interaction-trigger sites, not by sprite suppression of shared mechanism sprites; triggers SHALL remain re-armable so acquiring the soul later still allows the interaction.

#### Scenario: Late bat soul is not missable
- **WHEN** the player uses powder on the bat altar without the Magic Bat Soul (the room-static bat is suppressed), then later acquires the soul, re-enters, and uses powder again
- **THEN** the first attempt summons nothing, and the second summons the bat, which grants its check normally

#### Scenario: Potion purchases survive Witch suppression
- **WHEN** `npc_souls=on` and the Witch Soul is not owned
- **THEN** the outside witch is absent and the Potion Shop check cannot be obtained, while the potion shop's green/blue/red potion PURCHASES (sprite 0xE9 cauldrons) work normally — magic-refill routes the logic assumes stay physically available

#### Scenario: Fairy ponds gate without breaking other ponds
- **WHEN** the Pyramid Fairy Soul is not owned
- **THEN** the Pyramid Fairy pond grants nothing, while the wishing/happiness ponds (same pond machinery, other rooms) behave normally

### Requirement: NPC soul ownership persists across the widened flags field

Soul ownership storage SHALL widen to at least 69 bits (46 enemy/boss + 23 NPC souls) with all persistence surfaces carrying the widened field: sidecar extension, snapshot-tail TLV, and slot activation/reset. Pre-widening saves SHALL load with NPC-soul bits zeroed.

#### Scenario: Save round-trip
- **WHEN** a slot with owned NPC souls is saved, the process restarts, and the slot is activated
- **THEN** the owned NPC souls are restored exactly, and suppression reflects them on the next room/area load
