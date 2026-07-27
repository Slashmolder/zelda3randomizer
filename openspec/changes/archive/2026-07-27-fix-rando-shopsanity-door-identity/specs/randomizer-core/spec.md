# randomizer-core

## MODIFIED Requirements

### Requirement: Settings canonical serialization order (normative)

The `RandoSettings` struct SHALL be canonically serialized field-by-field in the following 31-byte layout. This serialization is the input to `SHA-256()` for the `settings_hash` computation and to the v2 share-string encoder for the `seed_u64`-adjacent settings portion. The order is **normative spec**.

**Enum value names align with ALTTPR's config strings** (verified against `app/Randomizer.php` and `config/alttp.php` in `alttp_vt_randomizer`). Hand-translation from ALTTPR is mechanical when names match; share-string-to-PHP-config debugging is 1-to-1. Where ALTTPR uses hyphens (e.g., `triforce-hunt`), our CLI surface preserves the exact string; the C struct field substitutes underscore for the hyphen (parser does the translation).

| Byte | Field | Encoding |
|---:|---|---|
| 0 | `mode_state` | uint8 (`open=0`, `standard=1`, `inverted=2`, `retro=3`). ALTTPR key: `mode.state`. |
| 1 | `goal` | uint8 (`ganon=0`, `fast_ganon=1`, `dungeons=2`, `pedestal=3`, `triforce-hunt=4`, `ganonhunt=5`, `completionist=6`). |
| 2 | `crystals_ganon` | uint8, 0..8 (`8` = the requested `random` sentinel, add-rando-random-crystals; the effective 0..7 count resolves deterministically from the seed). |
| 3 | `crystals_tower` | uint8, 0..8 (`8` = the requested `random` sentinel; the effective count also drives the GT logic edges via OP_TOWER_CRYSTALS_MET). |
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
| 25 | entrance axes | bit-packed: bit0 `shuffle_cave_entrances`, bit1 `shuffle_dungeon_entrances`, bit2 `coupled`, bit3 `cross_category`, bit4 `decoupled`, bit5 `shuffle_ganons_tower_entrance`, bit6 `dungeon_chains`; bit7 reserved. (bit6 reconciled to as-built `kEntranceAxis_DungeonChains = 1<<6`, `rando_settings.h`; the prior table text predated dungeon chains.) |
| 26 | misc axes | bit-packed: bit0 `enemy_shuffle`, bit1 `customizer_active`, `traps` is a **non-contiguous 3-bit field** — low 2 bits at bits2-3 + high bit at **bit5** (`off=0`, `low=1`, `medium=2`, `high=3`, `insanity=4`); bit4 `instant_flute` (inverse: `1` = manual activation; default on ⇒ `0`); bits6-7 carry the low 2 bits of `pot_shuffle`. |
| 27 | door + trap-category axes | bit-packed: bits0-1 `door_shuffle` (`vanilla=0`, `basic=1`); bits2-6 `trap_categories` enable mask (bit2 HAZARD, bit3 IMPAIR, bit4 DRAIN, bit5 SCARE, bit6 DISPLACE; the mask is meaningful only when `traps > 0`, and a `0` mask while `traps > 0` means all categories enabled, so the default serializes all-zero); bit7 carries the high bit of `pot_shuffle`. |
| 28 | drop-check + souls axes | bit-packed: bits0-1 `enemy_drop_checks` (`off=0`, `keys=1`, `dungeon=2`, `all=3`, after derived rules); bits2-3 `souls_shuffle` (`off=0`, `bosses=1`, `bosses_enemies=2`); bit4 `npc_souls` (boolean); bits5-7 refused-undefined. (Souls fields reconciled to as-built `kSoulsShuffleAxis_*` / `kNpcSoulsAxis_*`, `rando_settings.h` — the prior table text predated the souls merge.) |
| 29 | terrain + shop axes | bit-packed: bits0-1 `grass_shuffle` (`off=0`, `junk=1`, `all=2`); bits2-3 `rock_shuffle` (`off=0`, `junk=1`, `all=2`); **bit4 `shopsanity` (boolean, added by add-rando-shopsanity — takes the first reserved bit; no length change, so a default-settings blob is bit-identical to the pre-shopsanity build)**; bits5-6 `bonk_shuffle` (`off=0`, `junk=1`, `all=2`, added by add-rando-bonk-sanity); bit7 refused-undefined. (Byte appended by add-rando-grass-rock-shuffle, `kSettingsCanonicalLen` 29→30; all fields default `0` so the byte serializes `0x00` at defaults.) |
| 30 | key-item axes | bit-packed: bits0-1 `key_rings` (`off=0`, `random=1`, `all=2` — the REQUESTED policy, deliberately not normalized away under Vanilla or Retro/Generic Keys); bit2 `skeleton_key` (boolean); bits3-7 refused-undefined (`kKeyRingsAxis_DefinedMask`). Appended by add-rando-key-rings-skeleton-key (`kSettingsCanonicalLen` 30→31); defaults `0` so the byte serializes `0x00` at defaults. |

Changing this order — or the field widths, or the enum value assignments — is a `generator_version` bump trigger (per `tasks.md §13.6`).

Serialization applies derived rules before writing bytes: Completionist forces `accessibility=locations`; retired bytes 9 and 10 canonicalize to `0`; Retro and active door shuffle normalize key modes; unsupported entrance and door-shuffle combinations normalize to the runtime-effective axes; `enemy_drop_checks=dungeon` degrades to `keys` under enemy shuffle and normalizes to `off` when small keys are vanilla; `enemy_drop_checks=all` is distinct from `dungeon` and either remains `all`, normalizes visibly to a lower supported tier such as entrance shuffle's `dungeon`, or generation rejects if the complete all-enemy registry is unavailable. `grass_shuffle` and `rock_shuffle` have no derived-rule couplings (they compose freely, including under door and cave-entrance shuffle). `shopsanity` composes freely EXCEPT under effective cave-entrance shuffle, which normalizes it off: four shop interiors are cave-pool members whose pool entries omit the shop slot ids, so a shuffled seed would evaluate each slot from its vanilla overworld region while the runtime reaches that interior through a different door (see the `randomizer-shopsanity` capability for why rebinding the region cannot repair it). Deserialization masks only the defined bits of bytes 25..30 — each bit-packed field is masked to its own width (`enemy_drop_checks` reads bits 0-1 of byte 28 only; `shopsanity` reads bit 4 of byte 29 only) — and range-checks the scalar enum/count fields including each packed byte-28/29/30 field (`grass_shuffle`/`rock_shuffle` value 3 is invalid and rejected).

#### Scenario: Reordering fields breaks settings_hash
- **WHEN** the canonical serialization order changes (e.g., swap fields 4 and 5)
- **THEN** the resulting `settings_hash` differs for the same logical settings, `generator_version` MUST advance, and the regression corpus MUST be regenerated

#### Scenario: Phase A defaults
- **WHEN** the user opens the settings screen and has not changed any field
- **THEN** the default values are: `mode_state=open`, `goal=fast_ganon`, `crystals_ganon=7`, `crystals_tower=7`, `tricks=none`, `item_pool=normal`, `logic=NoGlitches`, `mode_weapons=randomized`, `accessibility=items`, `pyramid_bow_upgrade=silvers` (legacy/no-op), `region_boss_hearts_in_pool=false` (legacy/no-op), `dungeon_items_*=vanilla`, `prize_shuffle=true`, `medallion_shuffle=true`, `race_mode=false`, `pieces_required=20`, `pieces_placed=30`, `hints=on`, `boss_shuffle=false`, `drop_shuffle=false`, all entrance shuffle axes (including `dungeon_chains`) inactive in canonical bytes, `enemy_shuffle=false`, `customizer_active=false`, `traps=off`, `instant_flute=on`, `door_shuffle=vanilla`, `trap_categories=0` (all categories — meaningful only when traps are enabled), `enemy_drop_checks=off`, `souls_shuffle=off`, `npc_souls=off`, `grass_shuffle=off`, `rock_shuffle=off`, `shopsanity=off`, `key_rings=off`, and `skeleton_key=off`

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

#### Scenario: Appending byte 29 is a generator-version bump, placement-neutral at defaults
- **WHEN** a build that includes the terrain axes serializes any settings combination
- **THEN** the canonical blob is at least 30 bytes (byte 29 = `0x00` when its axes are off), so every `settings_hash` — defaults included — differs from pre-terrain builds because the SHA-256 input length changed; `generator_version` advances with this change and the corpus regenerates, while default-settings PLACEMENT stays byte-identical to the pre-terrain build (proven by the corpus 3-way diff)

#### Scenario: Shopsanity takes a reserved bit without a length change
- **WHEN** a build that includes shopsanity serializes default settings
- **THEN** the canonical blob length and every default-settings `settings_hash` are identical to the immediately-prior baseline (bit 4 of byte 29 serializes `0` at default), the previously-refused bit is now accepted on deserialize, and only `shopsanity=true` blobs hash differently; `generator_version` still advances with this change because ON-axis placement output is new

#### Scenario: Shopsanity normalizes off under cave-entrance shuffle
- **WHEN** settings requesting both `shopsanity=true` and
  `shuffle_cave_entrances=true` are canonically serialized in a world state
  where cave-entrance shuffle is effective (Open or Standard)
- **THEN** byte 29 bit 4 serializes `0`, and the `settings_hash`, placement and
  spoiler are identical to the same seed requested without `shopsanity`
