## MODIFIED Requirements

### Requirement: Settings canonical serialization order (normative)

The `RandoSettings` struct SHALL be canonically serialized field-by-field in the following 28-byte layout. This serialization is the input to `SHA-256()` for the `settings_hash` computation and to the v2 share-string encoder for the `seed_u64`-adjacent settings portion. The order is **normative spec**.

**Enum value names align with ALTTPR's config strings** (verified against `app/Randomizer.php` and `config/alttp.php` in `alttp_vt_randomizer`). Hand-translation from ALTTPR is mechanical when names match; share-string-to-PHP-config debugging is 1-to-1. Where ALTTPR uses hyphens (e.g., `triforce-hunt`), our CLI surface preserves the exact string; the C struct field substitutes underscore for the hyphen (parser does the translation).

| Byte | Field | Encoding |
|---:|---|---|
| 0 | `mode_state` | uint8 (`open=0`, `standard=1`, `inverted=2`, `retro=3`). ALTTPR key: `mode.state`. |
| 1 | `goal` | uint8 (`ganon=0`, `fast_ganon=1`, `dungeons=2`, `pedestal=3`, `triforce-hunt=4`, `ganonhunt=5`, `completionist=6`). |
| 2 | `crystals_ganon` | uint8, 0..7. |
| 3 | `crystals_tower` | uint8, 0..7. |
| 4 | `tricks` | uint8 bitmask; bit positions are stable across `generator_version` bumps. |
| 5 | `item_pool` | uint8 (`easy=0`, `normal=1`, `hard=2`, `expert=3`). |
| 6 | `logic` | uint8 (`NoGlitches=0`, `OverworldGlitches=1`, `MajorGlitches=2`, `HybridMajorGlitches=3`, `NoLogic=4`). |
| 7 | `mode_weapons` | uint8 (`randomized=0`, `assured=1`, `swordless=3`; byte value 2 is reserved/invalid). |
| 8 | `accessibility` | uint8 (`items=0`, `locations=1`, `none=2`). |
| 9 | `pyramid_bow_upgrade` | legacy uint8; accepted for compatibility and canonicalized to `0`. |
| 10 | `region_boss_hearts_in_pool` | legacy uint8; accepted for compatibility and canonicalized to `0`. |
| 11 | `dungeon_items_small_keys` | uint8 (`vanilla=0`, `dungeon=1`, `wild=2`), after derived rules. |
| 12 | `dungeon_items_big_keys` | uint8 (`vanilla=0`, `dungeon=1`, `wild=2`), after derived rules. |
| 13 | `dungeon_items_maps` | uint8 (`vanilla=0`, `dungeon=1`, `wild=2`). |
| 14 | `dungeon_items_compasses` | uint8 (`vanilla=0`, `dungeon=1`, `wild=2`). |
| 15 | `prize_shuffle` | uint8 boolean. |
| 16 | `medallion_shuffle` | uint8 boolean. |
| 17 | `race_mode` | uint8 boolean. |
| 18 | `pieces_required` | uint16 LE low byte. |
| 19 | `pieces_required` | uint16 LE high byte. |
| 20 | `pieces_placed` | uint16 LE low byte. |
| 21 | `pieces_placed` | uint16 LE high byte. |
| 22 | `hints` | uint8 boolean (`off=0`, `on=1`; aliases `sahasrahla`/`full` resolve to `on`). |
| 23 | `boss_shuffle` | uint8 boolean. |
| 24 | `drop_shuffle` | uint8 boolean. |
| 25 | entrance axes | bit-packed: bit0 `shuffle_cave_entrances`, bit1 `shuffle_dungeon_entrances`, bit2 `coupled`, bit3 `cross_category`, bit4 `decoupled`, bit5 `shuffle_ganons_tower_entrance`; bits6-7 reserved. |
| 26 | misc axes | bit-packed: bit0 `enemy_shuffle`, bit1 `customizer_active`, `traps` is a **non-contiguous 3-bit field** — low 2 bits at bits2-3 + high bit at **bit5** (`off=0`, `low=1`, `medium=2`, `high=3`, `insanity=4`); the split keeps `off`/`low`/`medium`/`high` byte-identical and bit4 untouched, bit4 `instant_flute` (inverse: `1` = manual activation; default on ⇒ `0`); bits6-7 reserved. |
| 27 | door + trap-category axes | bit-packed: bits0-1 `door_shuffle` (`vanilla=0`, `basic=1`); bits2-6 `trap_categories` enable mask (bit2 HAZARD, bit3 IMPAIR, bit4 DRAIN, bit5 SCARE, bit6 DISPLACE; the mask is meaningful only when `traps > 0`, and a `0` mask while `traps > 0` means all categories enabled, so the default serializes all-zero); bit7 reserved. |

Changing this order — or the field widths, or the enum value assignments — is a `generator_version` bump trigger (per `tasks.md §13.6`).

Serialization applies derived rules before writing bytes: Completionist forces `accessibility=locations`; retired bytes 9 and 10 canonicalize to `0`; Retro and active door shuffle normalize key modes; unsupported entrance and door-shuffle combinations normalize to the runtime-effective axes. Deserialization masks only the defined bits of bytes 25..27 and leaves undefined bits forward-compatible, but range-checks the scalar enum/count fields.

#### Scenario: Reordering fields breaks settings_hash
- **WHEN** the canonical serialization order changes (e.g., swap fields 4 and 5)
- **THEN** the resulting `settings_hash` differs for the same logical settings, `generator_version` MUST advance, and the regression corpus MUST be regenerated

#### Scenario: Phase A defaults
- **WHEN** the user opens the settings screen and has not changed any field
- **THEN** the default values are: `mode_state=open`, `goal=fast_ganon`, `crystals_ganon=7`, `crystals_tower=7`, `tricks=none`, `item_pool=normal`, `logic=NoGlitches`, `mode_weapons=randomized`, `accessibility=items`, `pyramid_bow_upgrade=silvers` (legacy/no-op), `region_boss_hearts_in_pool=false` (legacy/no-op), `dungeon_items_*=vanilla`, `prize_shuffle=true`, `medallion_shuffle=true`, `race_mode=false`, `pieces_required=20`, `pieces_placed=30`, `hints=on`, `boss_shuffle=false`, `drop_shuffle=false`, all entrance shuffle axes inactive in canonical bytes, `enemy_shuffle=false`, `customizer_active=false`, `traps=off`, `instant_flute=on`, `door_shuffle=vanilla`, and `trap_categories=0` (all categories — meaningful only when traps are enabled)

#### Scenario: Retired boss-heart axis canonicalizes to shuffled
- **WHEN** settings are built from defaults, CSV, or a v2 share string
- **THEN** `region_boss_hearts_in_pool` canonicalizes to `0`, so the settings hash and placement both reflect shuffled boss-heart drops

#### Scenario: Trick bitmask non-zero changes settings hash
- **WHEN** a seed is generated with `settings.tricks` having any bit set
- **THEN** the `settings_hash` differs from the equivalent `tricks=0` seed; CI corpus regenerates accordingly

#### Scenario: Swordless mode removes swords from the pool
- **WHEN** a seed has `settings.mode_weapons == swordless`
- **THEN** the item pool contains no sword items (the `RandomAssumed` pool builder emits the Randomized equipment set minus `ProgressiveSword`, plus a guaranteed silver-arrow source), so no sword can be placed at any location; the swordless logic + runtime (op `OP_MODEWEAPONS_EQ`, `Rando_IsSwordlessActive`) let the Hammer / Bug-Catching Net stand in for the sword. (The original draft's `LOC_Pyramid_Fairy_Sword` `can_place` mechanism is OBSOLETE — that slot was retired by `add-rando-fairy-chest-model`; sword-removal is done at pool-build time instead.)

#### Scenario: pyramid_bow_upgrade=false — NOT shipped (obsolete under the fairy-chest model)
- **WHEN** the `region_pyramid_bow_upgrade=false` (`arrows`) variant is considered
- **THEN** it is refused or normalized away: the fairy-chest-model change deleted the Pyramid Fairy bow trade-in this axis controlled (`Sprite_WishPond3` now grants the Pyramid Fairy chests directly and nothing reads `pyramid_bow_upgrade`), so exposing it would expose a no-op setting

#### Scenario: accessibility=none allows un-completable seeds
- **WHEN** a seed has `settings.accessibility == none`
- **THEN** the generator does NOT enforce "every progression item is reachable"; un-reachable junk in the pool is permitted; `--allow-broken-seed` semantics overlap but accessibility=none is the principled axis

### Requirement: Item pool construction with progressive items, bottles, and junk padding

The system SHALL construct the item pool from a base set that includes:

- **Progressive items**: `ProgressiveSword`, `ProgressiveShield`, `ProgressiveArmor`, `ProgressiveGlove`, `ProgressiveBow`. The `mode.weapons` setting selects whether progressive or absolute weapon items populate the pool (mirroring ALTTPR's split bucket lists in `Randomizer.php:183-198`).
- **Absolute items**: all vanilla `link_item_*` items individually addressable. Includes `SilverArrowUpgrade` as a standalone item used when `mode.weapons = absolute` (per `Randomizer.php:178-179`); when `mode.weapons` uses `ProgressiveBow`, `SilverArrowUpgrade` is absent from the pool.
- **Bottles** as distinct IDs by contents: `BottleEmpty`, `BottleWithFairy`, `BottleWithBee`, `BottleWithGoodBee`, `BottleWithRedPotion`, `BottleWithGreenPotion`, `BottleWithBluePotion`. **Bottle count is capped at 4 total** (pool + starting bottle combined — `link_bottle_info` has 4 slots, so total acquisition cannot exceed 4). If a starting bottle is granted at new-game init, the pool contains at most 3 bottle items.
- **Magic upgrades**: `HalfMagic`, `QuarterMagic` — modify `link_magic_consumption`, granted by witch's hut in vanilla; ALTTPR shuffles them as items.
- **Triforce piece**: `TriforcePiece`. Required by Triforce Hunt and Ganon Hunt goals; pool-padded based on `pieces_placed`.
- **Heart-related items** as distinct IDs: `PieceOfHeart` (4 = 1 heart container of effect, overworld PoH) and `BossHeartContainer` (full container drop from boss kills, +1 max HP directly). The dispatcher routes accordingly.
- Small/big keys per dungeon, maps, compasses, multi-tier rupees (`Rupee1/5/20/100/300`).
- **Junk pool**: `SmallMagic`, `Arrow1`, `Arrow10`, `Bombs1`, `Bombs3`, `Bombs10`. `Rupoor` is included only when `item_pool_difficulty ∈ {hard, expert}`.
- **Traps**: when `settings.traps != off`, the placer SHALL replace eligible final junk-filled placements with masquerade trap items after junk fill. The per-slot effect is selected **deterministically per `(seed, location_id)`** from the 16-effect catalog and filtered by the `trap_categories` mask (see the `randomizer-traps` capability for the catalog, selector, dispatch, and per-effect location requirements). This preserves placement cardinality and never replaces progression, dungeon items, prizes, Triforce pieces, pinned/event items, or fallback/identity placements.

The system SHALL junk-pad the pool so that its cardinality equals the active world-state's **fillable** location count after applying dungeon-item shuffle modes (which expand or contract the location pool per `randomizer-shuffles`). Pre-pinned identity slots — prize/event/medallion slots, Retro shop and capacity-upgrade slots, and vanilla-mode dungeon items — never consume a pool item and SHALL be excluded from the junk-pad target; one shared pre-pin predicate drives both the pad target and the placer's pre-place pin pass so the two cannot drift (counting pinned slots oversizes the pool, and the junk-fill surplus drop then silently discards a random subset of it). TakeAny slots are likewise excluded (their rewards are role-pinned outside the pool).

#### Scenario: TriforcePiece is in the pool for Triforce Hunt and Ganon Hunt
- **WHEN** the goal is Triforce Hunt or Ganon Hunt with `pieces_placed = N`
- **THEN** the pool contains exactly N `TriforcePiece` items and the goal predicate fires correctly when the player collects ≥ `pieces_required`

#### Scenario: SilverArrowUpgrade only when absolute bow
- **WHEN** `mode.weapons = absolute` and bow upgrade is present in the pool
- **THEN** `SilverArrowUpgrade` is a distinct item in the pool; conversely when `mode.weapons` uses `ProgressiveBow`, `SilverArrowUpgrade` is absent

#### Scenario: Rupoor gated by item_pool_difficulty
- **WHEN** `item_pool_difficulty = normal`
- **THEN** `Rupoor` is not present in the constructed pool

#### Scenario: After padding, pool cardinality matches location count
- **WHEN** generation begins with any combination of valid settings
- **THEN** after junk-padding, the item pool contains exactly as many items as there are fillable (non-pre-pinned, non-TakeAny) locations in the active world-state

#### Scenario: Triforce Hunt junk-padding
- **WHEN** the goal is Triforce Hunt with `pieces_placed = N` and the unpadded pool is smaller than the location pool
- **THEN** the pool is padded with junk items (small rupee, single bomb, single arrow, small heart) until cardinality matches

#### Scenario: Trap frequency is count-preserving
- **WHEN** `traps = low`, `medium`, `high`, or `insanity`
- **THEN** the placement count remains equal to the active fillable location count, and exactly 4, 8, 16, or (at `insanity`) **every** eligible junk-filled placement respectively is replaced by a deterministically-selected trap item (per `(seed, location_id)` over the enabled categories) when enough eligible junk exists

#### Scenario: Default traps are inert
- **WHEN** `traps = off`
- **THEN** the constructed pool contains no trap items and final placement output matches the pre-traps default output for the same settings and seed

#### Scenario: Item-pool difficulty downgrade
- **WHEN** the item-pool difficulty setting is "hard"
- **THEN** every item in the hard-pool replacement table is downgraded in the constructed pool (silver arrows → wooden arrows, mirror shield → red shield, all four bottles → small hearts, etc.) and the post-padding pool cardinality is unchanged

### Requirement: Traps settings axis in canonical serialization

The traps axis SHALL join `RandoSettings` canonical serialization in reserved byte `[26]` as a **non-contiguous 3-bit field**: the low 2 bits at **bits 2-3** and the high bit at **bit 5**, encoding `traps ∈ {off=0, low=1, medium=2, high=3, insanity=4}`. The split keeps `off`/`low`/`medium`/`high` byte-identical to the original 2-bit layout and leaves byte `[26]` bit4 (`instant_flute`) untouched. Byte `[26]` bit0 remains `enemy_shuffle`, bit1 remains `customizer_active`; `kSettingsCanonicalLen` stays 28. The CSV parser SHALL accept `traps` and `trap_frequency` as aliases for the axis, and `insanity` (aliases `max`/`all`) for the maximum tier.

#### Scenario: Default traps keep settings_hash byte-identical
- **WHEN** a default-settings seed (`traps == off`) is generated
- **THEN** byte `[26]` bits 2-3 and bit 5 pack to zero, `kSettingsCanonicalLen` stays 28, and the default `settings_hash` is byte-identical to the pre-traps value

#### Scenario: Trap frequency changes the per-seed settings_hash
- **WHEN** a seed sets `traps != off` (including `insanity`)
- **THEN** byte `[26]` carries the frequency across bits 2-3 (and bit 5 for `insanity`), the `settings_hash` differs from the `off` seed's, and serialize → deserialize round-trips the same frequency
