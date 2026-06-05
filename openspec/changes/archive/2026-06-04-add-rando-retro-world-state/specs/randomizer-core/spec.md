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

When `settings.world_state == Retro`, the seed's effective settings SHALL pin the Retro gameplay flags per ALTTPR's `app/World/Retro.php` (44 lines, verified). The flags are NOT stored bytes — they are *computed* from `world_state == Retro` at the point of use (no new settings-struct fields; `kSettingsCanonicalLen` unchanged). `world_state = Retro` implicitly pins them; they SHALL NOT be exposed as separate user-controllable axes (Phase C MAY expose them if a use case emerges).

Three of the four flags are pinned by this change:
- `rupeeBow = true` — firing the bow spends rupees (10 wood / 50 silver) instead of arrows; gated at runtime by `Rando_IsRetroActive()`.
- `takeAnys = true` — Take-Any caves are enterable (delivered by `add-rando-retro-takeany`; gated on `world_state == Retro`).
- `wildKeys = true` — small keys are placed in the general/wild pool rather than pinned to their dungeon, via `Settings_EffectiveSmallKeysMode()` (pins `dungeon_small_keys_mode = Wild` for Retro). Keys retain per-dungeon identity; the fork's cross-dungeon key-credit runtime makes a key found outside its dungeon usable inside it.

> **Scope note — `genericKeys` deferred to a follow-up.** ALTTPR's `rom.genericKeys` (one shared key pool; *any* key opens *any* locked door) is NOT pinned by this change. The fork models small keys in *logic* per dungeon (`HAS_ITEM(SmallKey_<Dungeon>)`), so collapsing to a single pool requires rewriting every key-door predicate into a shared-pool count (the key-logic reachability problem) plus a shared-counter runtime — work with its own playtest gate and softlock surface. Under this change keys keep dungeon identity (wildKeys-only), which is fully beatable but stricter than ALTTPR. The single-pool collapse lands in the follow-up change `add-rando-retro-generic-keys`, which will MODIFY this requirement to add `genericKeys = true`.

#### Scenario: Retro flags applied at generation
- **WHEN** a Retro seed is generated
- **THEN** the generator's effective settings reflect the pinned `rupeeBow`, `takeAnys`, and `wildKeys` flags; pool composition (wild small keys), shop-handler dispatch, and the runtime bow-cost all honor them

#### Scenario: User cannot override pinned flags in Phase B
- **WHEN** a user invokes `--generate-seed --settings=mode.state=retro,rupeeBow=false`
- **THEN** the override is ignored (the flags are not settings keys — `mode.state=retro` pins them implicitly); the generated seed has `rupeeBow = true`

#### Scenario: Open seed does not have Retro flags
- **WHEN** an Open seed is generated
- **THEN** none of the Retro flags is in effect; `wildKeys` (small keys stay vanilla-placed), `takeAnys`, and `rupeeBow` are all off

#### Scenario: wildKeys places small keys in the wild pool
- **WHEN** a Retro seed is generated
- **THEN** `Settings_EffectiveSmallKeysMode` reports `Wild`, the small-key items enter the general pool (placeable outside their dungeon), and the seed remains `goal_completable` with no unreachable placements

#### Scenario: settings UI reflects the forced small-keys mode
- **WHEN** `world_state == Retro` is selected in the settings UI
- **THEN** the small-keys control is shown locked/disabled at "wild" with a "forced by Retro" reason (mirroring the Completionist→accessibility lock); the user's underlying small-keys choice is left untouched and is restored if they switch off Retro

### Requirement: Junk-pool padding accommodates Retro shop locations

`BuildItemPool` already pads junk to fill the `|locations|` count (per Phase A pool-construction). The Retro branch SHALL produce a junk-padded pool whose size matches `|Open locations| + |Retro shop locations|`. The junk-pool rotation is the same as Phase A — items are drawn from `SmallMagic / Arrow1 / Arrow10 / Bombs1 / Bombs3 / Bombs10`, with `Rupoor` added when `item_pool_difficulty ∈ {hard, expert}`.

#### Scenario: Pool size matches expanded location count
- **WHEN** a Retro seed is generated
- **THEN** `|pool|` equals `|locations|` after junk-pad; no over- or under-fill; every location has exactly one placement-table entry
