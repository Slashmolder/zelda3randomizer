## ADDED Requirements

### Requirement: Retro world-state item pool

When `settings.world_state == Retro`, `BuildItemPool` SHALL include shop-purchase locations in addition to the Open item pool. Retro inherits Open's region graph and item set, but adds purchasable shop slots from the 42 enumerated shop entities in `../alttp_vt_randomizer/app/Region/Standard/**/*.php` (the same regions are shared across Standard / Open / Retro upstream; only the world-class differs).

Shop entities fall into three classes:
1. **`Shop`** (standard shops with purchasable inventory) — Kakariko Shop, Lake Hylia Shop, Dark World Potion Shop, Dark World Outcasts Shop, four DM shops, etc.
2. **`Shop\Upgrade`** (capacity upgrades) — bomb capacity, arrow capacity. These SHALL be identity-placed (their dispatch fires for uniformity, but the upgrade is the player's vanilla capacity-buy interaction, not a shuffleable item).
3. **`Shop\TakeAny`** (Take-Any caves) — `20 Rupee Cave`, `50 Rupee Cave`, `Bonk Fairy (Light)`, `Bonk Fairy (Dark)`, etc. These SHALL be in the Retro placement pool only (in Open they are not enterable; in Retro `region.takeAnys = true` makes them accessible).

The Retro item pool SHALL be authored hand-translated from the shop-instantiation sites in `app/Region/Standard/LightWorld/{East,NorthEast,NorthWest,South,DeathMountain/East}.php` and `app/Region/Standard/DarkWorld/{East,NorthEast,NorthWest,South,DeathMountain/East}.php`. Per-shop source-line citations SHALL be recorded in `audit.md §"Retro shop provenance"`.

#### Scenario: Retro pool includes shop purchases
- **WHEN** a seed is generated with `settings.world_state == Retro`
- **THEN** the placement table contains shop-purchase location entries; the spoiler lists them grouped under per-shop region headings

#### Scenario: Take-Any caves only in Retro pool
- **WHEN** a seed is generated with `settings.world_state == Open` (NOT Retro)
- **THEN** Take-Any-cave shop locations are NOT in the placement pool (Take-Any entry is gated by `region.takeAnys = false` in Open)

#### Scenario: Capacity-upgrade shops are identity-placed
- **WHEN** a Retro seed includes the `Capacity Upgrade` shop in `Light World Lake Hylia`
- **THEN** the bomb-capacity / arrow-capacity slots are dispatched (uniformity) but identity-placed to their vanilla capacity-upgrade outcomes — the player buys a capacity upgrade just as in vanilla

#### Scenario: Non-Retro seeds unchanged
- **WHEN** an Open seed is generated with otherwise-identical settings to a Retro seed
- **THEN** `BuildItemPool` produces the Open pool with no shop-purchase locations; `placement_digest_hex` for the Open seed is byte-identical to pre-Retro-change baseline

### Requirement: Retro world-state config-flag pinning

When `settings.world_state == Retro`, the seed's effective settings SHALL have these four flags pinned per ALTTPR's `app/World/Retro.php` (44 lines, verified):
- `rupeeBow = true` — Pyramid Fairy trade-in / bow ammo uses rupees instead of arrows.
- `genericKeys = true` — small keys are inter-dungeon transferable.
- `takeAnys = true` — Take-Any caves are enterable.
- `wildKeys = true` — small keys can appear in the wild pool, not pinned to their dungeon.

These flags SHALL NOT be exposed as separate user-controllable settings axes in Phase B. The decision (pin vs. expose) is recorded in design.md; Phase C MAY expose them if a use case emerges.

The flags participate in canonical-serialization via `settings.world_state` alone — i.e., setting `world_state = Retro` implicitly pins all four. No new bytes are added to the settings struct.

#### Scenario: Retro flags applied at generation
- **WHEN** a Retro seed is generated
- **THEN** the generator's effective settings reflect all four pinned Retro flags; pool composition, dungeon-key shuffle behavior, and shop-handler dispatch all honor them

#### Scenario: User cannot override pinned flags in Phase B
- **WHEN** a user invokes `--generate-seed --settings=mode.state=retro,rupeeBow=false` in Phase B
- **THEN** the CSV parser ignores the override (or rejects with a clear error — implementer's choice in tasks.md); the generated seed has `rupeeBow = true`

#### Scenario: Open seed does not have Retro flags
- **WHEN** an Open seed is generated
- **THEN** none of the four Retro flags is set in the effective settings; `wildKeys=false`, `takeAnys=false`, `genericKeys=false`, `rupeeBow=false`

### Requirement: Junk-pool padding accommodates Retro shop locations

`BuildItemPool` already pads junk to fill the `|locations|` count (per Phase A pool-construction). The Retro branch SHALL produce a junk-padded pool whose size matches `|Open locations| + |Retro shop locations|`. The junk-pool rotation is the same as Phase A — items are drawn from `SmallMagic / Arrow1 / Arrow10 / Bombs1 / Bombs3 / Bombs10`, with `Rupoor` added when `item_pool_difficulty ∈ {hard, expert}`.

#### Scenario: Pool size matches expanded location count
- **WHEN** a Retro seed is generated
- **THEN** `|pool|` equals `|locations|` after junk-pad; no over- or under-fill; every location has exactly one placement-table entry
