# randomizer-core Specification

## Purpose
TBD - created by archiving change add-randomizer-support. Update Purpose after archive.
## Requirements
### Requirement: Deterministic seed generation

The seed generator SHALL produce an identical placement table when given the same `(generator_version, settings, seed_u64)` tuple, regardless of host platform (Linux, macOS, Windows, Switch), build configuration, or wall-clock time.

#### Scenario: Same inputs yield identical placements across platforms
- **WHEN** the regression corpus is generated on each supported platform with the same generator binary commit
- **THEN** every (settings, seed_u64) pair produces a byte-identical placement table on every platform and every JSON spoiler hashes to the same SHA-256 digest

#### Scenario: Generator-version change invalidates determinism
- **WHEN** the generator version is incremented and the same settings and seed_u64 are submitted
- **THEN** the system makes no claim of placement equality across versions; the regression corpus is regenerated and its manifest is updated to record the new generator version

### Requirement: Byte-order pin

All multi-byte integer values that affect generator output, hashes, share strings, save format, or wire/snapshot layout SHALL be **little-endian** on disk and in memory-mapped form. Includes: `seed_u64`, `settings_hash[16]` (treated as a byte array; no integer interpretation), `share_string` raw form, `placement_table` uint16 entries, slot-header integer fields, snapshot-tail TLV `type[4]` and `length[4]`, xoshiro256\*\* state when serialized.

The implementation SHALL NOT use `htobe*`, `be*toh`, or other big-endian conversion macros inside `src/rando/`. A grep-based CI check enforces this.

#### Scenario: Big-endian conversion macros are rejected
- **WHEN** the build runs against `src/rando/`
- **THEN** any use of `htobe16`, `htobe32`, `htobe64`, `be16toh`, `be32toh`, `be64toh`, or `__builtin_bswap*` rejects the build

#### Scenario: Cross-platform byte sequence stability
- **WHEN** the regression corpus is generated on a little-endian platform (Linux x86_64) and a big-endian platform (hypothetical ARM BE build)
- **THEN** the on-disk byte sequences for share strings, settings hashes, and slot headers are byte-identical because all fields are explicitly LE-serialized

### Requirement: Settings canonical serialization order (normative)

The `RandoSettings` struct SHALL be canonically serialized field-by-field in the following 30-byte layout. This serialization is the input to `SHA-256()` for the `settings_hash` computation and to the v2 share-string encoder for the `seed_u64`-adjacent settings portion. The order is **normative spec**.

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
| 25 | entrance axes | bit-packed: bit0 `shuffle_cave_entrances`, bit1 `shuffle_dungeon_entrances`, bit2 `coupled`, bit3 `cross_category`, bit4 `decoupled`, bit5 `shuffle_ganons_tower_entrance`, bit6 `dungeon_chains`; bit7 reserved. (bit6 reconciled to as-built `kEntranceAxis_DungeonChains = 1<<6`, `rando_settings.h`; the prior table text predated dungeon chains.) |
| 26 | misc axes | bit-packed: bit0 `enemy_shuffle`, bit1 `customizer_active`, `traps` is a **non-contiguous 3-bit field** — low 2 bits at bits2-3 + high bit at **bit5** (`off=0`, `low=1`, `medium=2`, `high=3`, `insanity=4`); bit4 `instant_flute` (inverse: `1` = manual activation; default on ⇒ `0`); bits6-7 carry the low 2 bits of `pot_shuffle`. |
| 27 | door + trap-category axes | bit-packed: bits0-1 `door_shuffle` (`vanilla=0`, `basic=1`); bits2-6 `trap_categories` enable mask (bit2 HAZARD, bit3 IMPAIR, bit4 DRAIN, bit5 SCARE, bit6 DISPLACE; the mask is meaningful only when `traps > 0`, and a `0` mask while `traps > 0` means all categories enabled, so the default serializes all-zero); bit7 carries the high bit of `pot_shuffle`. |
| 28 | drop-check + souls axes | bit-packed: bits0-1 `enemy_drop_checks` (`off=0`, `keys=1`, `dungeon=2`, `all=3`, after derived rules); bits2-3 `souls_shuffle` (`off=0`, `bosses=1`, `bosses_enemies=2`); bit4 `npc_souls` (boolean); bits5-7 refused-undefined. (Souls fields reconciled to as-built `kSoulsShuffleAxis_*` / `kNpcSoulsAxis_*`, `rando_settings.h` — the prior table text predated the souls merge.) |
| 29 | terrain drop axes | bit-packed: bits0-1 `grass_shuffle` (`off=0`, `junk=1`, `all=2`); bits2-3 `rock_shuffle` (`off=0`, `junk=1`, `all=2`); bits4-7 reserved. Appended by add-rando-grass-rock-shuffle (`kSettingsCanonicalLen` 29→30, the same append-only move that created byte [28]); both fields default `0` so the appended byte serializes `0x00` at defaults. |

Changing this order — or the field widths, or the enum value assignments — is a `generator_version` bump trigger (per `tasks.md §13.6`).

Serialization applies derived rules before writing bytes: Completionist forces `accessibility=locations`; retired bytes 9 and 10 canonicalize to `0`; Retro and active door shuffle normalize key modes; unsupported entrance and door-shuffle combinations normalize to the runtime-effective axes; `enemy_drop_checks=dungeon` degrades to `keys` under enemy shuffle and normalizes to `off` when small keys are vanilla; `enemy_drop_checks=all` is distinct from `dungeon` and either remains `all`, normalizes visibly to a lower supported tier such as entrance shuffle's `dungeon`, or generation rejects if the complete all-enemy registry is unavailable. `grass_shuffle` and `rock_shuffle` have no derived-rule couplings (they compose freely, including under door and cave-entrance shuffle). Deserialization masks only the defined bits of bytes 25..29 — each bit-packed field is masked to its own width (`enemy_drop_checks` reads bits 0-1 of byte 28 only) — and range-checks the scalar enum/count fields including each packed byte-28/29 field (`grass_shuffle`/`rock_shuffle` value 3 is invalid and rejected).

#### Scenario: Reordering fields breaks settings_hash
- **WHEN** the canonical serialization order changes (e.g., swap fields 4 and 5)
- **THEN** the resulting `settings_hash` differs for the same logical settings, `generator_version` MUST advance, and the regression corpus MUST be regenerated

#### Scenario: Phase A defaults
- **WHEN** the user opens the settings screen and has not changed any field
- **THEN** the default values are: `mode_state=open`, `goal=fast_ganon`, `crystals_ganon=7`, `crystals_tower=7`, `tricks=none`, `item_pool=normal`, `logic=NoGlitches`, `mode_weapons=randomized`, `accessibility=items`, `pyramid_bow_upgrade=silvers` (legacy/no-op), `region_boss_hearts_in_pool=false` (legacy/no-op), `dungeon_items_*=vanilla`, `prize_shuffle=true`, `medallion_shuffle=true`, `race_mode=false`, `pieces_required=20`, `pieces_placed=30`, `hints=on`, `boss_shuffle=false`, `drop_shuffle=false`, all entrance shuffle axes (including `dungeon_chains`) inactive in canonical bytes, `enemy_shuffle=false`, `customizer_active=false`, `traps=off`, `instant_flute=on`, `door_shuffle=vanilla`, `trap_categories=0` (all categories — meaningful only when traps are enabled), `enemy_drop_checks=off`, `souls_shuffle=off`, `npc_souls=off`, `grass_shuffle=off`, and `rock_shuffle=off`

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
- **THEN** the canonical blob is 30 bytes (byte 29 = `0x00` when both axes are off), so every `settings_hash` — defaults included — differs from pre-terrain builds because the SHA-256 input length changed; `generator_version` advances with this change and the corpus regenerates, while default-settings PLACEMENT stays byte-identical to the pre-terrain build (proven by the corpus 3-way diff)

### Requirement: pieces_required must not exceed pieces_placed

The generator SHALL refuse to begin when `pieces_required > pieces_placed`. The settings screen SHALL block the Generate action with an inline error; the CLI SHALL exit non-zero before any pool construction.

#### Scenario: Unwinnable Triforce-Hunt input is refused
- **WHEN** the user submits `goal=triforce-hunt, pieces_required=30, pieces_placed=20`
- **THEN** the system fails fast with a clear error before any pool or placement work runs

### Requirement: Determinism constraints in the rando module

The rando module SHALL NOT call `rand`, `random`, `arc4random`, `time`, `clock_gettime`, or any other non-vendored RNG or wall-clock source; SHALL NOT read uninitialized memory in any decision-affecting path; SHALL NOT use floating-point arithmetic in any decision-affecting path; and SHALL iterate over sets and maps in an explicitly sorted order.

#### Scenario: Static check fails on forbidden symbols
- **WHEN** the build runs against `src/rando/`
- **THEN** a grep-based or compile-time check rejects any reference to the forbidden symbols and the build fails with the offending file/line

#### Scenario: CI corpus cross-platform diff
- **WHEN** CI generates the regression corpus on Linux, macOS, Windows, and Switch
- **THEN** the resulting placement-table digests are byte-identical across platforms and the job fails on any mismatch

### Requirement: Share-string format

The system SHALL accept and emit share strings as single base32 tokens (RFC 4648 uppercase alphabet, no padding) in two wire formats, both with a 4-byte magic prefix unique to this port, and SHALL dispatch decoding on the decoded **magic bytes** (not on input length):

**v1 (legacy — decoded forever, no longer the exchange emission):**
`magic "ZRSS"[4] | generator_version[1] | settings_hash[16] | seed_u64[8] (LE) | crc16[2] (LE)` = 31 bytes → exactly 50 base32 chars. The CRC is CRC-16-CCITT-FALSE over bytes [0..28]. `settings_hash` is one-way; a v1 string can restore only the seed.

**v2 (the exchange format — emitted by all copy/distribution surfaces):**
`magic "ZRS2"[4] | generator_version[1] | settings_len[1] | settings_canonical[settings_len] | seed_u64[8] (LE) | crc16[2] (LE)`, where `settings_len = kSettingsCanonicalLen` (29) at encode time and `settings_canonical` is the verbatim `Settings_CanonicalSerialize` output. Total `16 + settings_len` bytes -> `ceil((16 + settings_len) * 8 / 5)` base32 chars = exactly **72** for the current 29-byte canonical layout. The CRC is CRC-16-CCITT-FALSE over all bytes before it. `settings_hash` is NOT embedded — decoders recompute it from the canonical bytes. A v2 string SHALL fully restore `(settings, seed)`.

Decode rules: after base32 decode, magic `ZRSS` SHALL require exactly 31 bytes and parse as v1; magic `ZRS2` SHALL require exactly `16 + settings_len` bytes and parse as v2. A v2 string whose `settings_len` exceeds the binary's `kSettingsCanonicalLen` SHALL be refused with a distinct "newer version" decode status (no partial application). A v2 string whose `settings_len` is smaller (an older binary's string after a future canonical growth) SHALL zero-extend the canonical tail (zero is the append-only default for later-added axes). Explicit rejects (alttpr.com format, corrupted base32, wrong length, wrong magic, checksum mismatch) SHALL apply to both formats.

The v1 31-byte raw blob SHALL remain the internal **seed identity**: the sidecar slot header `share_string` field, the suppressed-spoiler (ZRSR) share-string field, the spoiler filename and the spoiler JSON `meta.share_string`, the 5-icon visual-hash input, and the race-reveal share-string comparisons SHALL continue to use the v1 form unchanged. Emitting v2 SHALL NOT change placement, `settings_hash`, the race-mode stamp, the sidecar or ZRSR layouts, the regression corpus, or `kGeneratorVersion`.

The headless CLI SHALL emit the v2 string as the distribution artifact: `--out-share-string=<path>` writes the v2 string (single line, no trailing newline) and the `--generate-seed` summary prints both forms; the spoiler JSON's `meta.share_string` stays the v1 identity string. When the settings carry `customizer_active`, all emission surfaces (native-window copy and the CLI) SHALL fall back to the v1 string — customizer placements depend on a local manifest file that no share string can carry until the deferred `customizer_seed` encoding lands (design D5).

#### Scenario: Round-trip encoding (v2)
- **WHEN** a v2 share string is generated from `(settings, seed_u64)` and then parsed
- **THEN** the decoded canonical settings bytes and `seed_u64` exactly match the originals, the recomputed `settings_hash` matches `Settings_HashShort` of the original settings, and the checksum validates

#### Scenario: v1 strings still decode
- **WHEN** a 50-char v1 share string (including one minted by an earlier release) is pasted
- **THEN** it decodes as v1 (seed + settings_hash); the seed is adopted and the settings-mismatch warning path applies — v1 decoding is never removed

#### Scenario: v2 length is exactly 72 chars for the 29-byte canonical layout
- **WHEN** a v2 share string is encoded while `kSettingsCanonicalLen == 29`
- **THEN** the encoded token is exactly 72 base32 chars (45 bytes = 360 bits → 72 chars), and the encoder buffer constant (`kShareStringBase32MaxLen`) accommodates it with a compile-time assert coupling it to `kSettingsCanonicalLen`

#### Scenario: Magic-based dispatch, not length-based
- **WHEN** a token base32-decodes to bytes whose magic is `ZRSS` but whose length is not 31, or whose magic is `ZRS2` but whose length is not `16 + settings_len`
- **THEN** the decoder rejects it (no partial parse); v1 vs v2 is never inferred from string length alone

#### Scenario: Newer-version v2 string is refused, not truncated
- **WHEN** a v2 string carries `settings_len` greater than the binary's `kSettingsCanonicalLen`
- **THEN** the decoder returns the distinct "newer version" reject status and no settings or seed are applied (silently dropping unknown settings axes would reproduce the silent-different-seed failure this format exists to prevent)

#### Scenario: External ALTTPR hash is rejected
- **WHEN** the user enters a hash in the alttpr.com format
- **THEN** the parser detects the absent magic prefix, rejects the input, and displays an error explicitly naming the format mismatch

#### Scenario: Corrupted share string
- **WHEN** the user enters a v1 or v2 share string with an altered character
- **THEN** the parser rejects it with a checksum-failure (or base32/magic) error and does not begin generation

#### Scenario: Identity surfaces are byte-identical
- **WHEN** a seed is generated by a binary with v2 support
- **THEN** the sidecar slot's stored raw blob, the ZRSR file bytes, the spoiler filename, `meta.share_string`, the 5-icon hash, and all race stamps are byte-identical to the pre-v2 binary's output for the same `(settings, seed)` — verified by a corpus run with zero digest changes and no `kGeneratorVersion` bump

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

### Requirement: Phase A goal set with independent Ganon's-Tower and Ganon-vulnerability crystal counts

The system SHALL support seven Phase A goals: Defeat Ganon, Fast Ganon, All Dungeons, Pedestal, Triforce Hunt (pedestal-end), **Ganon Hunt** (Triforce Hunt where the final action is killing Ganon — distinct from pedestal-end), **Completionist** (every location must be reachable; sets accessibility = locations per `Randomizer.php:99-101`).

Fast Ganon and Ganon Hunt SHALL expose two independent crystal-count settings: `crystals.ganon` (how many crystals make Ganon vulnerable) and `crystals.tower` (how many open Ganon's Tower). Both are routing-relevant; defaults are 7/7.

#### Scenario: Ganon Hunt is distinct from Triforce Hunt
- **WHEN** the goal is Ganon Hunt with `pieces_required = N`
- **THEN** the goal predicate is `(has ≥ N triforce pieces) AND (Ganon defeated)`, not `(has ≥ N triforce pieces AND on the pedestal)`

#### Scenario: Completionist requires every location reachable
- **WHEN** the goal is Completionist
- **THEN** the generator's solvability check requires that every location in the active world-state is reachable under the placed inventory accumulation, not just the goal predicate

#### Scenario: Independent crystals.ganon and crystals.tower
- **WHEN** Fast Ganon is configured with `crystals.ganon = 5, crystals.tower = 4`
- **THEN** Ganon's Tower opens after 4 crystals are collected and Ganon becomes vulnerable after 5; the spoiler header records both counts and the predicates evaluate accordingly

### Requirement: Phase A setting axes (pinned values)

The `RandoSettings` struct SHALL include the following axes with the documented Phase A value set; each appears in the canonical-serialization order documented by `randomizer-core / Settings canonical serialization order`.

- `world_state`: Open / Standard / Inverted / Retro
- `goal`: `ganon` / `fast_ganon` / `dungeons` / `pedestal` / `triforce-hunt` / `ganonhunt` / `completionist` (snake_case + hyphenated per ALTTPR convention)
- `crystals.ganon`: 0..7 (default 7)
- `crystals.tower`: 0..7 (default 7)
- `tricks`: pinned to `none` in Phase A
- `item_pool_difficulty`: `easy` / `normal` (default) / `hard` / `expert`
- `logic`: pinned to `NoGlitches` in Phase A
- `mode.weapons`: `randomized` (default) / `assured` in Phase A; `vanilla` and `swordless` reserved for Phase B
- `accessibility`: `items` (default) / `locations` (auto-set when goal is Completionist); `none` reserved for Phase B
- `pyramid_bow_upgrade`: legacy/no-op, canonicalized to `silvers`
- `dungeon_items.small_keys`: Vanilla / Dungeon / Wild
- `dungeon_items.big_keys`: Vanilla / Dungeon / Wild
- `dungeon_items.maps`: Vanilla / Dungeon / Wild
- `dungeon_items.compasses`: Vanilla / Dungeon / Wild
- `prize_shuffle`: bool (default true)
- `medallion_shuffle`: bool (default true)
- `race_mode`: bool (Phase B feature, but the bit is reserved in `settings_hash` from Phase A)
- `pieces_required`, `pieces_placed`: uint16 (for `triforce-hunt` and `ganonhunt` goals)

#### Scenario: Settings serialization order is stable
- **WHEN** the same `RandoSettings` is serialized twice
- **THEN** the canonical byte sequence is identical and `settings_hash` matches

#### Scenario: Completionist forces accessibility = locations
- **WHEN** the user selects `goal=completionist`
- **THEN** `accessibility` is auto-set to `locations` in the settings struct before `settings_hash` is computed

### Requirement: Race-mode bit is part of settings_hash

The `settings_hash` SHALL include the race-mode toggle as part of the canonical settings serialization. A race-mode-enabled seed SHALL have a different settings hash than an otherwise-identical non-race seed.

#### Scenario: Race-mode toggle changes settings hash
- **WHEN** race mode is toggled with all other settings held constant
- **THEN** the resulting `settings_hash` differs

### Requirement: Spoiler JSON schema mirrors ALTTPR field names

The spoiler JSON SHALL use ALTTPR-compatible field names where the data shape matches. The intent is defensive design: any external tooling in the ALTTPR ecosystem that consumes ALTTPR-shaped JSON has a reasonable chance of parsing our spoilers without modification, at near-zero implementation cost to us. At minimum: `placements`, `regions`, `meta`, `playthrough` SHALL use ALTTPR's names; our additions (e.g., `goal_completable`, `fallback_warnings`, `generation_wall_clock_ms`) are documented as our extensions. We do not promise specific external-tool compatibility (we have not verified which tools parse what); we just don't gratuitously diverge.

The `meta` block SHALL contain at least: `spoiler_format_version`, `generator_version`, `settings_hash_hex` (16-byte hash in 32-char hex), `share_string` (base32 encoded), `seed_u64`, `world_state`, `goal`, `generation_wall_clock_ms`, `fallback_warnings[]`. Tooling that consumes ALTTPR's `meta` block recognizes the field shape; our additions sit alongside without colliding with ALTTPR-reserved names.

#### Scenario: meta block contains all documented fields
- **WHEN** a spoiler is written
- **THEN** the `meta` object contains every documented field with the documented type; missing fields fail the schema validator

#### Scenario: Spoiler JSON parses with ALTTPR-shaped tooling
- **WHEN** an external tool that consumes ALTTPR spoiler JSON parses our spoiler
- **THEN** the `placements`, `regions`, `meta`, and `playthrough` fields are recognized with the same semantics as ALTTPR; our extension fields appear under documented names and do not collide with ALTTPR field names

### Requirement: CLI / headless generation mode

The host SHALL support a CLI entry point that generates seeds without running the game. Two forms:

- **Single seed**: `./zelda3 --generate-seed --settings=<key=value,...> --seed=<u64> --out-spoiler=<path> [--out-share-string=<path>] [--budget-seconds=<n>] [--assets-must-be-vanilla]`
- **Batch from manifest**: `./zelda3 --generate-seed --manifest=<path> [--budget-seconds=<n>] [--out-dir=<path>] [--assets-must-be-vanilla]`

**`--settings=k=v,...` grammar**:
- Keys are the canonical setting-axis names from `randomizer-core / Settings canonical serialization order`. **CLI surface uses dot-separated names** (`world_state`, `crystals.ganon`, `dungeon_items.small_keys`); the corresponding C struct field substitutes underscore for dot (`crystals_ganon`, `dungeon_items_small_keys`). The CLI parser performs the dot→underscore translation. The canonical-serialization byte order operates on the C-struct field order.
- Values are integers in decimal (`crystals.ganon=7`); booleans as `true` / `false` (`prize_shuffle=true`); enums as the exact canonical string from the canonical-serialization spec (`mode.state=open`, `goal=fast_ganon`, `logic=NoGlitches`).
- Enum value names are **case-sensitive** and match the spec exactly (`fast_ganon`, not `FastGanon` or `Fast_Ganon`; `triforce-hunt` with the hyphen, not `triforce_hunt`; `NoGlitches` retains PascalCase because that's what ALTTPR uses for `logic` values).
- Unknown keys SHALL reject with non-zero exit.
- Multiple `--settings` pairs are comma-separated at the top level; settings whose values contain commas (none exist in Phase A) would require a different surface — for now, no comma-bearing values are valid.
- Duplicate keys in `--settings=` SHALL reject with non-zero exit (no last-wins).

**`--manifest=<path>` YAML schema**:

```yaml
seeds:
  - settings: { mode.state: open, goal: fast_ganon, crystals.ganon: 7, crystals.tower: 7, ... }
    seed: "0xDEADBEEFCAFEBABE"     # MUST be a quoted string; see note below
    label: "all-bosses-12-min"     # optional; if present, used in output filename
```

- The top-level key is `seeds:`, a list.
- Each entry SHALL include `settings` (mapping; same keys/values as `--settings=`) and `seed` (uint64 as a quoted string — hex `"0x..."` or decimal); `label` is optional.
- **`seed` MUST be a YAML string, not an integer literal**, for two reasons: (a) YAML 1.1 parses `0xDEADBEEFCAFEBABE` as a hex int but YAML 1.2 (PyYAML default in some configs) does not; (b) uint64 values with the high bit set may overflow signed-int parsing. The CLI parser performs the hex/decimal-to-uint64 conversion explicitly so the parse behavior does not depend on the YAML library's integer-handling.
- Output filename: `<out-dir>/<label or share_string>.json`.
- Duplicate `(settings, seed)` pairs SHALL reject with non-zero exit (catch corpus authoring mistakes).
- Unknown settings keys in any entry SHALL reject with non-zero exit before any seed runs.

The process SHALL exit zero on success, non-zero on generation failure or (when `--assets-must-be-vanilla` is set) on asset-hash mismatch.

#### Scenario: CLI mode does not open a window
- **WHEN** `--generate-seed` is passed
- **THEN** no SDL window is created, no game frame is run, and the process exits after writing the requested output files

#### Scenario: Budget override sets a wall-clock cutoff
- **WHEN** `--budget-seconds=30` is passed (the default is `0` — no wall-clock cutoff, so the placer's bounded retry loop runs to its deterministic end; see the placer-determinism guard)
- **THEN** generation completes (or fails) within 30 seconds and the actual wall-clock used is written to stderr

#### Scenario: --assets-must-be-vanilla refuses non-vanilla blobs
- **WHEN** `--assets-must-be-vanilla` is passed and the loaded asset blob does not match `kVanillaAssetsHash`
- **THEN** the process exits non-zero with a message naming the hash mismatch, before any generation work begins

#### Scenario: Unknown CLI setting key rejected
- **WHEN** `--settings=` contains an unknown key
- **THEN** the process exits non-zero with an error naming the offending key, before any generation work begins

#### Scenario: Duplicate settings key in --settings rejected
- **WHEN** `--settings=` lists the same key twice
- **THEN** the process exits non-zero with an error

#### Scenario: Duplicate (settings, seed) in manifest rejected
- **WHEN** the manifest YAML contains two entries with identical `settings` and `seed`
- **THEN** the process exits non-zero with an error naming the offending entries before any seed runs

#### Scenario: --out-share-string writes share string only
- **WHEN** `--out-share-string=<path>` is passed
- **THEN** the base32-encoded share string is written to `<path>` as a single line, with no trailing newline; the file is suitable for `cat` and pipe into tournament tooling that does not need the full spoiler

#### Scenario: CLI mode does not open a window
- **WHEN** `--generate-seed` is passed
- **THEN** no SDL window is created, no game frame is run, and the process exits after writing the requested output files

#### Scenario: Budget override sets a wall-clock cutoff
- **WHEN** `--budget-seconds=30` is passed (the default is `0` — no wall-clock cutoff, so the placer's bounded retry loop runs to its deterministic end; see the placer-determinism guard)
- **THEN** generation completes (or fails) within 30 seconds and the actual wall-clock used is written to stderr

#### Scenario: --assets-must-be-vanilla refuses non-vanilla blobs
- **WHEN** `--assets-must-be-vanilla` is passed and the loaded asset blob does not match `kVanillaAssetsHash`
- **THEN** the process exits non-zero with a message naming the hash mismatch, before any generation work begins

### Requirement: Forward-fill fallback is surfaced prominently

When forward-fill fallback occurs (per "Forward-fill fallback after timeout" scenario), the spoiler header SHALL include a `forward_fill_fallback` entry in `fallback_warnings` and the file-select banner for that slot SHALL display a one-character marker (e.g., a `!`) indicating the seed is fallback-quality.

#### Scenario: Fallback marker on the banner
- **WHEN** a slot was generated under forward-fill fallback
- **THEN** the file-select rando banner for that slot includes a marker distinct from a non-fallback banner

### Requirement: Generation rejects un-completable seeds

The generator SHALL evaluate the goal-completion predicate against the final placement table after every successful fill. If the predicate evaluates false (i.e., the goal cannot be reached under any inventory accumulation from this placement), the generator SHALL fail with a clear error and SHALL NOT write a spoiler file or sidecar slot.

#### Scenario: Un-completable seed rejected
- **WHEN** assumed fill produces a placement that does not satisfy the active goal predicate
- **THEN** the generator returns an error naming the goal predicate and the rewind/retry budget consumed, the spoiler is not written, and the calling settings screen surfaces the error to the user

#### Scenario: Spoiler header records completability
- **WHEN** a seed is successfully generated
- **THEN** the spoiler JSON's `goal_completable` field is `true` and the goal predicate was verified before write

### Requirement: Simulated-inventory model for assumed fill

The placer SHALL maintain a **simulated inventory** during fill, represented as `uint16 counts[N]` indexed by item registry ID. Predicates evaluate against this array (and against the static settings struct).

Counting semantics:

- **Absolute items** (`L1Sword`, `Hookshot`, `MirrorShield`, etc.): `counts[id] ∈ {0, 1}`; placing the item sets to 1.
- **Progressive items** (`ProgressiveSword`, `ProgressiveShield`, `ProgressiveArmor`, `ProgressiveGlove`, `ProgressiveBow`): `counts[id]` increments by 1 per placement; `HAS_AMOUNT ProgressiveSword 2` evaluates to `counts[ProgressiveSword] >= 2`.
- **Bottle-with-contents** (`BottleEmpty`, `BottleWithFairy`, …): each contents-ID is a distinct counter. `HAS_ANY_COUNT [Bottle*] N` sums all bottle-contents counts.
- **Heart items**: `PieceOfHeart` and `BossHeartContainer` are distinct counters. `HAS_ANY_COUNT [PieceOfHeart, BossHeartContainer, StartingHeart] N` is the canonical "≥ N hearts total" predicate.
- **Baseline starting state**: the placer pre-populates virtual items not in the placement pool, including `counts[StartingHeart] = 3` (Link starts with 3 hearts). `StartingHeart` is a registry ID with no grant path and is excluded from the dispatcher; it exists only so logic predicates can evaluate hearts-total uniformly via `HAS_ANY_COUNT`.

**Bottle-cap enforcement during fill**: the placer SHALL refuse to add a bottle to the simulated inventory when `counts[Bottle*]` sums to 4 — guaranteeing the 5th-bottle dispatcher fallback (per `randomizer-placement / "5th-bottle grant is refused at generation time"`) never fires for valid seeds. If the algorithm would otherwise pick a bottle for a location whose simulated inventory is bottle-saturated, the bottle is treated as un-placeable from that slot and forwarded to the next iteration.

**Progressive item caps during fill**: the placer SHALL refuse to add a progressive item whose count would exceed the registry-documented maximum (`ProgressiveSword` ≤ 4, `ProgressiveShield` ≤ 3, `ProgressiveArmor` ≤ 2, `ProgressiveGlove` ≤ 2, `ProgressiveBow` ≤ 2). Pool construction ensures the pool contains exactly the right number; this guard is a fill-time safety net for hand-edited pools (customizer / Phase D).

#### Scenario: Progressive sword counter increments
- **WHEN** the placer adds `ProgressiveSword` to a simulated inventory already holding two `ProgressiveSword` items
- **THEN** `counts[ProgressiveSword]` becomes 3 and `HAS_AMOUNT ProgressiveSword 3` evaluates true

#### Scenario: Bottle cap holds at 4
- **WHEN** the placer would add a 5th bottle item to a simulated inventory with `counts[Bottle*]` summing to 4
- **THEN** the placer refuses the add for that slot, forwards to next iteration, and the placement never produces a 5th-bottle grant at runtime

#### Scenario: Starting hearts visible to logic
- **WHEN** the placer initializes a simulated inventory
- **THEN** `counts[StartingHeart] = 3` and `HAS_ANY_COUNT [PieceOfHeart, BossHeartContainer, StartingHeart] 3` evaluates true even before any heart items are placed

### Requirement: Assumed-fill placement
The placement algorithm SHALL use assumed fill — placing progression items into locations reachable under the assumption that all remaining unplaced items are temporarily available — and SHALL retry placement with bounded rewind when no valid location exists for the current item.

**Enemies-tier fill model (add-enemy-souls)**: for seeds whose EFFECTIVE `souls_shuffle` is the bosses+enemies tier, the per-turn reachability SHALL be computed from the assumed (unplaced) inventory PLUS a fix-point collection of items already committed to locations — both items placed on earlier turns and pre-placed pins (prize assignments, event grants, TakeAny rewards, customizer pins): any committed item whose location is reachable under the current set joins the set, and reachability recomputes until stable (the upstream `RandomAssumed` "fix-point reachability expansion" contract). Pins are then NOT assumed unconditionally — a pinned grant whose location the current set cannot reach does not count (unconditionally-assumed prize pins let GT-entry certify open through crystals whose Prize locations were themselves soul-blocked, failing every attempt's validation). Collection exclusions: vanilla-MODE dungeon items are pre-granted wholesale (the ROM grants them in place; their pinned slots are skipped to avoid double-counting key thresholds), and placed copies of the item id currently being placed are never collected (a key must not sit in a slot justified by its own placed siblings — the final sphere walk cannot order the copies).

All other seeds SHALL keep the conservative pre-souls model — placed items leave the assumed set permanently and pin grants are assumed unconditionally — which the worst-case key-threshold models (pot key depths, the door-key oracle) are calibrated against; applying the collection model to them empirically degraded their fills (deep-stacked dungeon keys; door-oracle source double-counting). Under either model, every attempt is validated by the sphere walk before acceptance, so the fill model is quality guidance, never a soundness input.

**Attempt acceptance bar (add-enemy-souls)**: an attempt SHALL be accepted when it satisfies the seed's EFFECTIVE accessibility tier — `locations`: every placement reachable; `items` (default): every progression placement reachable; `none` ("beatable only"): the goal is completable — plus zero forward-fill fallbacks and the Standard-mode escape weapon/lamp constraints. Demanding unconditional full reach (the old bar) is unattainable for combos with modeling-stranded junk placements (e.g. wild keys × dungeon enemy checks permanently strand a fixed set of junk-holding checks) and burned the whole attempt budget on seeds whose first attempt already satisfied the acceptance gate.

**Phase B implementation alignment (Bug #7 fix)**: the "bounded rewind" SHALL be per-item, not whole-attempt. Phase A1's implementation uses whole-attempt retry with `kAssumedFillMaxAttempts=8`; the SHALL above (already present in Phase A spec at `randomizer-core/spec.md:344`) describes per-item rewind. This change brings the implementation in line with the existing spec.

Per-item rewind algorithm: when the current item has no valid placement, rewind the last N placements (N is the per-item rewind budget, configurable; default 10), recompute the simulated inventory state, and retry placing the current item. If the per-item rewind budget exhausts, escalate to whole-attempt retry (existing `kAssumedFillMaxAttempts` path). If both budgets exhaust, surface a clear error.

#### Scenario: Placed souls keep gating locations open (enemies tier)
- **WHEN** an enemies-tier seed placed a soul on an earlier turn at a reachable location, and a later turn evaluates a kill-gated location requiring that soul
- **THEN** the per-turn reachability collects the soul through the fix-point and the gated location remains a valid candidate (it does not go permanently dead the moment the soul leaves the assumed set)

#### Scenario: Pins are collected, not assumed (enemies tier)
- **WHEN** an enemies-tier seed's pinned grant location (e.g. a dungeon Prize holding a crystal) is unreachable under the current assumed-plus-collected set
- **THEN** the pinned item does not count toward reachability that turn, so the placer cannot certify a placement against a grant the player could not yet have

#### Scenario: Non-enemies-tier seeds keep the conservative model
- **WHEN** a seed's effective souls tier is off or bosses
- **THEN** the fill uses the pre-souls conservative reachability (placed items vanish, pins assumed) unchanged

#### Scenario: Attempt acceptance matches the accessibility tier
- **WHEN** an attempt strands only non-progression placements and the seed's effective accessibility is `items`
- **THEN** the attempt is accepted without burning the remaining attempt budget, and the stranded placements surface in the spoiler's unreachable list

#### Scenario: Per-item rewind preserves earlier valid placements
- **WHEN** the placer hits an item with no valid location and the per-item rewind budget is non-zero
- **THEN** the placer rewinds the last N placements, retries the current item, and earlier valid placements (those outside the N-rewind window) are preserved

#### Scenario: Per-item rewind budget exhausts → whole-attempt retry
- **WHEN** per-item rewind exhausts for the current item
- **THEN** the placer falls back to whole-attempt retry (Phase A1 behavior); `kAssumedFillMaxAttempts` bounds whole-attempt retries

#### Scenario: Both budgets exhausted → clear error
- **WHEN** both per-item rewind and whole-attempt budgets exhaust for a given seed
- **THEN** generation fails with an error message naming the offending item and the budgets consumed; the spoiler is not written; the CLI exits non-zero

#### Scenario: Forward-fill fallback after timeout
- **WHEN** assumed fill exceeds an explicitly-passed positive wall-clock budget (the default budget is `0` = no cutoff)
- **THEN** the generator falls back to forward fill (placing items into reachable locations in order) and surfaces a warning in the spoiler `fallback_warnings` array

#### Scenario: Same-seed determinism across budgets
- **WHEN** the same seed is generated under different `--budget-seconds` values that all succeed within budget
- **THEN** the resulting `placement_digest_hex` values are byte-identical — the budget is wall-clock fail-safe, not a determinism input

### Requirement: Spoiler-log emission

The generator SHALL emit a spoiler log in two forms: a human-readable text file grouped by region, and a machine-readable JSON file with stable field names suitable for parsing by external tools. Spoiler files SHALL be written to a configurable directory keyed by share string. The JSON schema SHALL define `fallback_warnings` as an array of objects each with `code` (string enum, e.g., `"forward_fill_fallback"`, `"rewind_budget_exceeded_recovered"`) and `detail` (human-readable string).

**Race-mode suppression**: when `race_mode == 1` in the settings serialization, the generator SHALL NOT emit the full JSON or text spoilers at generation time. Instead, the generator SHALL emit a single suppressed-spoiler file at `<spoiler_dir>/<share_string>.json` containing exactly:
- 4-byte magic header `ZRSR` (Zelda Rando Spoiler Race — distinct from the full-spoiler form which is parseable JSON without magic).
- 2-byte `generator_version` (LE).
- 32-byte SHA-256 `spoiler_stamp` of the full-spoiler JSON the generator *would have emitted* with `race_mode` cleared in the canonical settings object (the stamp is over placement, not over the race-mode flag).
- 4-byte length-prefix + 64-byte zero-padded UTF-8 `share_string` for round-trip convenience.
- 29-byte canonical settings blob (`kSettingsCanonicalLen`) with `race_mode` cleared.
- 4-byte CRC32 over the previous 135 bytes for tamper detection.

The total file size SHALL be exactly 139 bytes. No `.txt` text-spoiler companion is emitted.

#### Scenario: Both JSON and text spoilers are emitted (non-race seed)
- **WHEN** a seed generates successfully with `race_mode == 0`
- **THEN** both `<spoiler_dir>/<share_string>.json` and `<spoiler_dir>/<share_string>.txt` are written to the configured spoiler directory

#### Scenario: Race-mode suppression writes only the stamp file
- **WHEN** a seed generates successfully with `race_mode == 1`
- **THEN** only the suppressed-spoiler file at `<spoiler_dir>/<share_string>.json` is written; no `.txt` companion is emitted; the file is exactly 139 bytes (4-byte magic `ZRSR` + 2-byte generator_version LE + 32-byte SHA-256 stamp + 4-byte share-string-length LE + 64-byte share-string zero-padded + 29-byte settings_canonical with `race_mode` cleared + 4-byte CRC32 LE). The settings_canonical field carries the original `RandoSettings` bytes (with `race_mode` cleared to 0 in the canonical form) because the sidecar slot stores only `settings_hash` in the fixed header. The reveal pipeline needs the original settings to regenerate the placement deterministically.

#### Scenario: Stamp algorithm is canonical
- **WHEN** the same `race_mode == 1` seed is generated twice on different platforms (Linux, macOS, Windows, Switch)
- **THEN** the resulting `spoiler_stamp` bytes are byte-identical — the canonical JSON serialization SHALL be deterministic (sorted keys, no trailing whitespace, normalized number representation per `randomizer-core / Settings canonical serialization order`)

#### Scenario: fallback_warnings records forward-fill fallback
- **WHEN** the forward-fill fallback fires (per `Forward-fill fallback after timeout` in the assumed-fill requirement above)
- **THEN** the JSON spoiler's `fallback_warnings` array contains an object whose `code` field equals `"forward_fill_fallback"` and whose `detail` field is a human-readable string explaining the fallback

#### Scenario: Text spoiler is grouped by region
- **WHEN** the text spoiler file is read
- **THEN** placements are organised into one section per region (one region heading followed by that region's location/item lines), making the file scannable without external tooling

#### Scenario: Race-mode fallback_warnings still stamped
- **WHEN** a race-mode seed generates and a forward-fill fallback fires
- **THEN** the stamp covers a full-spoiler form that includes the `fallback_warnings` entry; reveal will surface the warning to the player

### Requirement: Sphere semantics

A **sphere** is the set of locations reachable using only items collected from previous spheres (sphere 0 = locations reachable with the starting inventory alone; sphere N+1 = locations newly reachable after collecting everything in spheres 0..N). The generator SHALL compute spheres via iterative fixed-point expansion against the simulated inventory model.

The spoiler JSON SHALL emit `sphere_data` as an array of arrays:

```json
"sphere_data": [
  [ { "location": "...", "item": "..." }, ... ],   // sphere 0
  [ { "location": "...", "item": "..." }, ... ],   // sphere 1
  ...
]
```

Sphere data SHALL **not** be part of the regression-corpus placement digest (the digest covers only the placement table); it is informational, useful for tooling and route planning. A separate optional `sphere_digest` field is included in the spoiler `meta` block so corpus tooling can detect sphere-computation regressions independently of placement regressions.

#### Scenario: Sphere 0 is starting-inventory reachable
- **WHEN** a seed is generated
- **THEN** the locations in `sphere_data[0]` are exactly those whose `can_reach` predicate evaluates true under the starting-inventory simulated state, before any item is collected

#### Scenario: Spheres are strictly monotonic
- **WHEN** sphere N (with N ≥ 1) is computed
- **THEN** every location in sphere N has `can_reach` evaluate **true** against the inventory accumulated from spheres `0..N-1`, and the same location has `can_reach` evaluate **false** against the inventory accumulated from spheres `0..N-2` (i.e., the location became newly reachable exactly because of items collected in sphere N-1). Sphere-N items are *rewards at* sphere-N locations, not prerequisites — they unlock sphere N+1, not sphere N.

#### Scenario: Sphere data partitions the placement table
- **WHEN** spheres are computed
- **THEN** every placement appears in exactly one sphere; the union of `sphere_data[i].location` across all `i` equals the full set of placement-table keys; no location appears in two spheres

#### Scenario: JSON spoiler matches placement table
- **WHEN** a seed is generated
- **THEN** the JSON spoiler lists every location with its placed item, the world-state, the goal, all settings, the share string, the generator version, the generation wall-clock, and any fallback warnings

#### Scenario: Spoiler is written at slot creation
- **WHEN** a new randomizer slot is created
- **THEN** both the JSON and text spoilers are written to the configured spoiler directory; the path is **not** stored in the slot (the runtime re-derives it from `spoiler_dir + share_string` whenever needed, per `randomizer-save / Spoiler-log persistence per slot`)

### Requirement: Generation performance budget

Seed generation under Phase A default settings (item shuffle + dungeon-item Vanilla + prize/medallion shuffle, Phase A pinned logic axes — `tricks=none`, `item_pool_difficulty=normal`, `logic=NoGlitches`) SHALL complete within 2 seconds on the supported reference desktop hardware and within 5 seconds on the Switch build, measured wall-clock from user confirmation to placement-table availability. Higher `item_pool_difficulty` values may push wall-clock higher; the spoiler header records the actual time and the CLI `--budget-seconds` overrides the default for batch corpus runs.

#### Scenario: Default-settings benchmark
- **WHEN** a Phase A default-settings seed is generated on reference hardware
- **THEN** the measured generation wall-clock is recorded in the spoiler header and is under 2 seconds

#### Scenario: Switch budget
- **WHEN** a Phase A default-settings seed is generated on the Switch build
- **THEN** the measured generation wall-clock is recorded in the spoiler header and is under 5 seconds

### Requirement: Accessibility tier acceptance (ALTTPR three-way)

The generator SHALL evaluate seed acceptance against the `accessibility` axis
using a single nested-strictness predicate. **Every** tier SHALL require the
goal-completion predicate (`Goal_IsCompletable`) to hold — i.e. all three tiers
produce a beatable seed. The tiers SHALL additionally require:

- `locations` (`kAccessibility_Locations`, value 1) — every placed location is
  reachable (`unreachable_count == 0`). Strictest. (`kGoal_Completionist`
  already implies this via its own goal predicate, which iterates every
  placement; selecting `locations` with a looser goal makes that bar explicit.)
- `items` (`kAccessibility_Items`, value 0; the default) — every **progression**
  item's location is reachable (per the `is_progression_item` classifier).
  Non-progression items — junk/consumables (rupees, arrows, bombs), maps,
  compasses, and heart pieces/containers — MAY be at unreachable locations.
- `none` (`kAccessibility_None`, value 2; UI label "beatable only") — goal
  completability is the whole bar; items and locations MAY be unreachable.

Strictness SHALL nest: a placement accepted by `locations` is accepted by
`items`, and a placement accepted by `items` is accepted by `none`.

The "ship a literally unwinnable seed" behavior SHALL NOT be reachable from the
`accessibility` axis: `none`/"beatable only" still guarantees beatability. The
CLI `--allow-broken-seed` flag remains the only way to emit a diagnostic
non-completable spoiler.

This predicate SHALL gate seed acceptance on every generation path: the headless
`--generate-seed` path (including its entrance-shuffle per-permutation accept),
and the shared playable-slot path used by both the PC native settings window and
the in-game settings screen (including its entrance-shuffle per-permutation
accept and its non-entrance path).

The spoiler's `goal_completable` field SHALL remain the pure reachability
predicate (`Goal_IsCompletable`), independent of the accessibility tier. The
generator SHALL NOT emit an `accessibility_none_seed` warning; when a tier leaves
locations unreachable by design, the existing `unreachable_placements`
`fallback_warnings` entry SHALL surface that count.

#### Scenario: Beatable-only accepts a stranded non-progression item

- **WHEN** `accessibility = none` and assumed fill produces a completable
  placement in which a junk/heart/map/compass item is at an unreachable location
- **THEN** the generator accepts the seed, writes the spoiler, and (if the
  unreachable count is non-zero) records an `unreachable_placements` warning

#### Scenario: 100% Inventory rejects a stranded progression item

- **WHEN** `accessibility = items` and the placement strands a progression item
  (a weapon/utility/bottle/key/prize/triforce-piece) at an unreachable location
- **THEN** the seed is refused (unless `--allow-broken-seed`), even though the
  goal predicate alone would pass

#### Scenario: 100% Locations requires every location reachable

- **WHEN** `accessibility = locations` and the placement has any unreachable
  location (`unreachable_count > 0`)
- **THEN** the seed is refused (unless `--allow-broken-seed`)

#### Scenario: Every tier still requires beatability

- **WHEN** assumed fill produces a placement whose goal is NOT completable
- **THEN** the seed is refused for all three accessibility tiers (the prior
  `none`-opts-into-unwinnable behavior no longer applies)

#### Scenario: CSV accepts the beatable alias

- **WHEN** settings are parsed from `accessibility=beatable`
- **THEN** the value resolves to `kAccessibility_None` (identical to
  `accessibility=none`)

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

> **Scope note — `genericKeys` SHIPPED in the follow-up.** ALTTPR's `rom.genericKeys` (one shared key pool; *any* key opens *any* locked door) was NOT pinned by *this* change — it kept per-dungeon key identity (wildKeys-only), fully beatable but stricter than ALTTPR. The single-pool collapse landed in the follow-up `add-rando-retro-generic-keys` (archived 2026-06-05), which — to avoid an archive-sequencing conflict — ADDED a separate requirement ("Retro generic small-key pool", below) rather than modifying this one. That requirement is now the authority for genericKeys: under Retro, `BuildItemPool` substitutes each per-dungeon `SmallKey_<Dungeon>` with the fungible `GenericKey`, the predicate VM collapses any small-key requirement onto "hold ≥1 `GenericKey`", and a single SRAM-persisted shared counter backs the live small-key count. See also the `randomizer-logic` requirement "Generic small-key door reachability".

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

`BuildItemPool` already pads junk to fill the fillable-location count (per Phase A pool-construction). The Retro branch SHALL produce a junk-padded pool whose size matches the Retro **fillable** count: Retro's identity-pinned shop / capacity-upgrade slots and its role-pinned TakeAny slots add placement-table entries but consume no pool items, so they are excluded from the pad target. The junk-pool rotation is the same as Phase A — items are drawn from `SmallMagic / Arrow1 / Arrow10 / Bombs1 / Bombs3 / Bombs10`, with `Rupoor` added when `item_pool_difficulty ∈ {hard, expert}`.

#### Scenario: Pool size matches expanded location count
- **WHEN** a Retro seed is generated
- **THEN** `|pool|` equals the fillable `|locations|` after junk-pad (identity-pinned shop/upgrade and TakeAny slots excluded); no over- or under-fill; every location has exactly one placement-table entry

### Requirement: Race-mode reveal action

The randomizer SHALL expose `Rando_RevealSpoiler(slot_index)` that:

1. Locates the suppressed-spoiler file at `<spoiler_dir>/<share_string>.json` using the slot's share string.
2. Parses the file's magic + version + stamp + share-string + CRC32; rejects on CRC mismatch.
3. Verifies the parsed share-string matches the slot's share-string.
4. Regenerates the full spoiler in-memory using the same placement pipeline the CLI generator uses (`--generate-seed --settings=... --seed=...`), with `race_mode == 0` substituted in the in-memory settings copy for stamp recomputation.
5. Computes SHA-256 of the regenerated full-spoiler JSON canonical form.
6. **If the computed stamp matches the stored stamp**: overwrites the suppressed file with the full JSON; also writes the `.txt` companion; returns success.
7. **If the computed stamp does NOT match**: leaves the suppressed file unmodified; returns a `kRandoReveal_StampMismatch` error.
8. **If the slot's `generator_version` differs from the runtime's**: returns `kRandoReveal_VersionMismatch` and does NOT attempt regeneration (cross-version reveal is not supported in Phase B; a future change can refine this).

#### Scenario: Reveal of a freshly-generated seed succeeds
- **WHEN** a race-mode seed is generated and `Rando_RevealSpoiler` is invoked immediately afterward on the same binary
- **THEN** the suppressed file at `<spoiler_dir>/<share_string>.json` is overwritten with the full JSON spoiler; the `.txt` companion is created; the action returns success

#### Scenario: Reveal of a tampered suppressed file fails closed
- **WHEN** an attacker modifies the on-disk suppressed file's stored stamp byte and `Rando_RevealSpoiler` is invoked
- **THEN** the CRC32 check fails before regeneration begins; the action returns `kRandoReveal_CrcMismatch` and the file is unchanged

#### Scenario: Reveal with mismatched share-string fails
- **WHEN** the slot's share-string differs from the suppressed file's stored share-string (e.g., user copy-renamed the file)
- **THEN** the action returns `kRandoReveal_ShareStringMismatch` without regeneration

#### Scenario: Reveal across binary versions refuses
- **WHEN** the suppressed file's `generator_version` differs from the runtime's
- **THEN** the action returns `kRandoReveal_VersionMismatch`; the player is advised to use a binary matching the stored version

#### Scenario: CLI counterpart `--reveal-spoiler`
- **WHEN** the CLI is invoked as `./zelda3 --reveal-spoiler=<path-to-suppressed-file>`
- **THEN** the process runs the reveal action against the supplied path and exits zero on success / non-zero on any failure (with the specific failure code printed to stderr)

### Requirement: Retro generic small-key pool

When `settings.world_state == Retro`, ALTTPR's `rom.genericKeys` SHALL be in
effect: small keys form a single shared pool and any small key opens any locked
door. At pool construction, every per-dungeon small-key item (`SmallKey_<Dungeon>`)
SHALL be substituted with the fungible `GenericKey` item (registry id 125, ROM
0xAF), matching ALTTPR `app/Location.php` (`Item\Key` → `KeyGK` under
`rom.genericKeys`). The generic keys SHALL be placed in the general/wild pool
(the `wildKeys` placement from `add-rando-retro-world-state` already routes small
keys there); per-dungeon `SmallKey_<Dungeon>` items SHALL NOT enter the Retro
pool. `genericKeys` is computed from `world_state == Retro` (no new settings
bytes; `kSettingsCanonicalLen` unchanged).

This change supersedes the deferral recorded in `add-rando-retro-world-state`'s
"Retro world-state config-flag pinning" requirement (which pinned `rupeeBow` /
`takeAnys` / `wildKeys` and explicitly left `genericKeys` to this follow-up).

> **As built (archived 2026-06-05):** the placement substitution is gated on
> `world_state == Retro` in `BuildItemPool`; it is coupled to the "Generic
> small-key door reachability" requirement (`randomizer-logic`) — implemented as a
> predicate-VM collapse onto the shared `GenericKey` count — and to a single
> SRAM-persisted shared counter (`link_keys_earned_per_dungeon[15]` = ALTTPR
> `$7EF38B`) backing the live small-key count under `Rando_IsGenericKeysActive()`.
> `kGeneratorVersion` 53→54; only Retro corpus digests moved.

#### Scenario: Small keys become a single fungible pool
- **WHEN** a Retro seed is generated
- **THEN** the placement pool contains `GenericKey` items (count = the sum of the
  per-dungeon small-key counts) and no `SmallKey_<Dungeon>` items; the seed is
  `goal_completable` with no unreachable placements

#### Scenario: Non-Retro key placement unchanged
- **WHEN** a non-Retro seed is generated (Open / Standard / Inverted)
- **THEN** small keys retain per-dungeon identity per the seed's
  `dungeon_small_keys_mode`; `placement_digest` is byte-identical to the
  pre-change baseline

#### Scenario: Determinism bump scoped to Retro
- **WHEN** the corpus is regenerated after this change
- **THEN** only Retro entries' `placement_digest` / `sphere_digest` move; every
  non-Retro entry is byte-identical, and `kGeneratorVersion` advances

### Requirement: Door-shuffle settings axis in canonical serialization

The door-shuffle axis SHALL join the `RandoSettings` canonical serialization in the
reserved zero-pad byte that existed when the axis was introduced:
`door_shuffle ∈ {vanilla, basic}` packs into byte `[27]` **bits 0-1**
(`kDoorShuffleAxis_Mask`; `intensity` is pinned to 1 and not serialized), so the
door-shuffle change itself did not grow `kSettingsCanonicalLen`, no
size-coupling cascade fired for that change, and a default-settings
(`door_shuffle == vanilla`) `settings_hash` stayed
byte-identical to the pre-change value (the entrance-axes `[25]` / enemy-shuffle
`[26]` precedent). The CSV settings key is `door_shuffle` (`vanilla|basic`), and
the deserializer unpacks the axis from `[27]`. `Settings_EffectiveDoorShuffle`
SHALL report the normalized (post-`apply_derived_rules`) value — definitionally
canonical byte `[27]`'s axis bits.

`apply_derived_rules` SHALL normalize incompatible combinations on the private
copy before serialization (door shuffle coerces to `vanilla` off Open/Standard +
NoGlitches or under entrance shuffle; otherwise it forces in-dungeon small + big
keys — see `randomizer-shuffles`), so the default tuple still packs `[27]` to
zero.

The follow-on axes (`intensity 2/3`, `door_type_mode`, `trap_door_mode`,
`decoupledoors`, `door_self_loops`, `partitioned`/`crossed`) exceed byte `[27]`'s
8 bits and SHALL grow `kSettingsCanonicalLen`, which is a `generator_version` bump
trigger and triggers the coupled-site cascade (the `kSettingsCanonicalLen`
`_Static_assert`s in `rando_settings.h`, `rando_spoiler.{h,c}`, `rando_save.c`,
`main.c`, plus the corpus constants). This change pins the MVP into `[27]`
precisely to defer that cascade until the follow-on axes are real.

#### Scenario: Default (vanilla) door shuffle keeps settings_hash byte-identical

- **WHEN** a default-settings seed (`door_shuffle == vanilla`) is generated after
  this change
- **THEN** byte `[27]` packs to zero and the `settings_hash` is byte-identical to
  the pre-change value for the same axis tuple

#### Scenario: Basic door shuffle changes the per-seed settings_hash

- **WHEN** a seed sets `door_shuffle == basic` (and the pins permit it)
- **THEN** byte `[27]`'s bits 0-1 carry the axis, the `settings_hash` differs from
  the `vanilla` seed's, and the round-trip
  (serialize → deserialize → `Settings_EffectiveDoorShuffle`) reports `basic`

#### Scenario: Follow-on axes trigger the canonical-length cascade

- **WHEN** a follow-on change adds axes that exceed byte `[27]`
- **THEN** `kSettingsCanonicalLen` grows, all coupled `_Static_assert` sites +
  corpus constants are updated together, and `generator_version` advances

### Requirement: Traps settings axis in canonical serialization

The traps axis SHALL join `RandoSettings` canonical serialization in byte `[26]` as a **non-contiguous 3-bit field**: the low 2 bits at **bits 2-3** and the high bit at **bit 5**, encoding `traps ∈ {off=0, low=1, medium=2, high=3, insanity=4}`. The split keeps `off`/`low`/`medium`/`high` byte-identical to the original 2-bit layout and leaves byte `[26]` bit4 (`instant_flute`) untouched. Byte `[26]` bit0 remains `enemy_shuffle`, bit1 remains `customizer_active`; the traps change itself did not grow `kSettingsCanonicalLen`. The CSV parser SHALL accept `traps` and `trap_frequency` as aliases for the axis, and `insanity` (aliases `max`/`all`) for the maximum tier.

#### Scenario: Default traps keep settings_hash byte-identical
- **WHEN** a default-settings seed (`traps == off`) is generated
- **THEN** byte `[26]` bits 2-3 and bit 5 pack to zero, and the default `settings_hash` is byte-identical to the pre-traps value

#### Scenario: Trap frequency changes the per-seed settings_hash
- **WHEN** a seed sets `traps != off` (including `insanity`)
- **THEN** byte `[26]` carries the frequency across bits 2-3 (and bit 5 for `insanity`), the `settings_hash` differs from the `off` seed's, and serialize → deserialize round-trips the same frequency

### Requirement: Hints settings axis

The settings struct SHALL include a binary `hints` axis (`uint8`, `off | on`) occupying canonical-serialization byte 22. The CSV parser SHALL accept `off | 0 | false | none` as off and `on | 1 | true | sahasrahla | full` as on; `sahasrahla` and `full` are accepted aliases for `on` and do NOT select a distinct mode. The axis SHALL participate in the settings hash. The default value SHALL be `on` unconditionally (not goal-dependent).

- `off`: no hint generation; spoiler omits the `hints` section; telepathic tiles play vanilla text.
- `on`: telepathic-tile hints are generated and surfaced in-game; the spoiler emits the `hints` array.

> **As-built note**: an earlier draft specified a tri-state `off | sahasrahla | full` axis with a goal-aware default (`full` for Triforce/Ganon Hunt, `sahasrahla` otherwise). The implementation collapsed this to binary on/off (`sahasrahla`/`full` are CSV aliases) and made the default unconditionally `on`. A true tri-state is deferred. Murahdahla still emits only on Triforce/Ganon Hunt, but that is a generation-time goal check, not a settings default.

#### Scenario: hints axis participates in settings_hash
- **WHEN** the same seed is generated with `hints=off` and then with `hints=on`
- **THEN** the resulting `settings_hash` values differ

#### Scenario: hints default is on
- **WHEN** a seed is generated without an explicit `hints=` override
- **THEN** the resolved setting is `hints=on`; telepathic-tile hint generation runs

#### Scenario: tri-state aliases collapse to on
- **WHEN** a seed is generated with `hints=sahasrahla` or `hints=full`
- **THEN** the resolved setting is `hints=on` (the alias selects no distinct mode)

### Requirement: Hints spoiler section

The JSON spoiler SHALL include a top-level `hints` array, populated only when `settings.hints == on`. Each entry SHALL be an object with the fields:
- `npc` (string): the ALTTPR-compatible string id (e.g. `telepathic_tile_eastern_palace`, `murahdahla`); fork-extension ids are prefixed `fork_`.
- `dialogue_id` (integer): the spoiler-label dialogue id `kRandoHintDialogueBase (0x200) + (npc_index - 1)`. This is a label only — it is NOT a runtime dialogue-table key.
- `text` (string): the rendered hint text.

The text spoiler SHALL mirror the JSON content under a `Hints:` heading with one line per entry, and SHALL be omitted entirely when no hint entry is populated. The settings block of the JSON spoiler SHALL additionally carry `"hints": <0|1>`. The spoiler's `meta` block SHALL also carry a `hints_count` integer — the number of populated hint NPCs (0 when `hints=off`) — for tooling.

> **As-built note**: an earlier draft specified `source`/`kind` fields and a `goal-progress` Murahdahla shape with one entry per piece location. The implementation emits `{npc, dialogue_id, text}` and a single Murahdahla *region-summary* entry (region count, not per-piece/per-sphere). Full per-location flavor text and joke filler are deferred.

#### Scenario: Hints array deterministic across runs
- **WHEN** the same `(settings, seed)` is generated twice
- **THEN** the spoiler's `hints` array contents are byte-identical between runs (same `npc`, `dialogue_id`, `text` values in the same order)

#### Scenario: Triforce Hunt surfaces a Murahdahla summary
- **WHEN** a seed with `goal ∈ {triforce-hunt, ganon-hunt}` is generated with `hints=on`
- **THEN** the `hints` array contains a single entry with `npc=murahdahla` whose `text` summarizes how many Triforce pieces are placed across how many regions

#### Scenario: hints off omits the section
- **WHEN** a seed is generated with `hints=off`
- **THEN** the JSON `hints` array is empty and the text spoiler `Hints:` section is omitted

### Requirement: Settings canonical serialization order — Phase D major-glitch un-pin

In addition to the byte order pinned at the post-archive `randomizer-core / Settings canonical serialization order (normative)` baseline (and any Phase B extensions, in particular `add-rando-trick-logic-and-axes`'s un-pinning of values 1-2 for the `logic` field at position #7), Phase D `add-rando-major-glitch` SHALL un-pin user input for `logic` field values **3 (`HybridMajorGlitches`)** and **4 (`NoLogic`)**.

The byte layout, enum value assignments, and field widths SHALL remain unchanged from the Phase A baseline — Phase A's spec already defines values 3 and 4. Phase D opens user-controllable access; it does NOT change the canonical-serialization byte sequence for any specific tuple of values.

Default-settings seeds (`logic = NoGlitches`) SHALL remain byte-identical in `placement_digest_hex` to Phase A and Phase B baselines.

This requirement is **ADDED** (not MODIFIED) to avoid a multi-change archive-sequencing conflict on the Phase A `Settings canonical serialization order (normative)` requirement. Multiple changes can ADD their own per-axis extensions without colliding on the baseline.

#### Scenario: HybridMG value participates in settings_hash
- **WHEN** a seed is generated with `logic=hybrid_major_glitches`
- **THEN** the resulting `settings_hash` differs from the equivalent `logic=major_glitches` seed

#### Scenario: NoLogic value participates in settings_hash
- **WHEN** a seed is generated with `logic=no_logic`
- **THEN** the resulting `settings_hash` differs from any other `logic=...` seed

#### Scenario: Default NoGlitches preserved
- **WHEN** a Phase A default seed (`logic=NoGlitches`) is generated post-this-change
- **THEN** the `settings_hash` is byte-identical to the Phase A baseline

#### Scenario: Phase B un-pin still active
- **WHEN** a Phase B seed with `logic=overworld_glitches` is generated AFTER this Phase D change archives
- **THEN** the un-pin extension from `add-rando-trick-logic-and-axes` continues to apply; Phase D adds values 3-4 without removing Phase B's coverage of values 1-2

### Requirement: Enemy-shuffle canonical settings axis

The `RandoSettings` struct SHALL gain a boolean axis `enemy_shuffle`. In the canonical serialization (the input to `SHA-256()` for `settings_hash` and to the v2 share-string encoder) it SHALL be packed as canonical byte `[26]` bit0. Byte `[26]` also carries `customizer_active` at bit1 and `traps` in bits2-3; door shuffle owns byte `[27]` bits0-1. The enemy-shuffle change itself did not grow `kSettingsCanonicalLen`. No existing field's offset, width, or value changes; no `kSettingsCanonicalLen` size-coupling cascade is triggered by this axis.

Because `enemy_shuffle` defaults off (the bit is 0) and draws no fill RNG / adds no predicate:
- **Default-settings seeds keep a byte-identical `settings_hash`** (the canonical bytes are unchanged for the default tuple), and
- **all seeds keep a byte-identical `placement_digest_hex`** (enemy shuffle is orthogonal to item placement).

Adding the axis SHALL still advance `kGeneratorVersion`: it version-locks a new *live runtime* axis (so an older binary surfaces the upgrade warning rather than silently ignoring an enemy-shuffle slot), and a seed with `enemy_shuffle` on serializes a non-zero pad bit, changing *that* seed's `settings_hash`. The corpus regenerates (manifest `generator_version` advances); because no corpus seed enables `enemy_shuffle`, every regenerated digest SHALL be byte-identical to the pre-change baseline.

#### Scenario: Pad-bit packing keeps default hash and all placement identical
- **WHEN** `enemy_shuffle` is added and a default-settings seed is generated on the new binary
- **THEN** the seed's `settings_hash` AND `placement_digest_hex` are byte-identical to the pre-change baseline (the default pad bit is 0); the corpus regenerates only its manifest version

#### Scenario: kGeneratorVersion bump + backward load
- **WHEN** the axis is added
- **THEN** `kGeneratorVersion` advances by one and a slot written by the prior version loads on the new binary with the one-time informational warning (per `randomizer-save / upgrade safety`), no regeneration required

#### Scenario: Toggling enemy_shuffle changes only that seed's settings hash
- **WHEN** `enemy_shuffle` is toggled on for a seed
- **THEN** that seed's `settings_hash` changes (the packed pad bit flips to 1) while its `placement_digest_hex` is unchanged from the enemy-shuffle-off seed with the same other axes

### Requirement: Customizer-mode generation pipeline

The generator SHALL support customizer mode as a partial-manual-placement layer over assumed-fill (faithful to ALTTPR's customizer: the manifest pins a SUBSET of locations; the standard fill places everything else). When `settings.customizer_active == true`, the generator SHALL:

1. Read the customizer manifest from a path supplied via CLI flag `--customizer=<path>` (on `--generate-seed` and `--generate-slot`) or via the native settings window's manifest field.
2. Validate every `placements:` key against the location registry (`assets/rando/location_registry.yaml`) and every value against the item registry (`assets/rando/item_registry.yaml`), accepting both the symbol form (`Eastern_Palace_Boss`) and the spoiler's human form (`Eastern Palace - Boss`) via normalized name match. Reject (with the offending line) unknown names, duplicate location keys, non-customizable location types (prize/medallion/shop/take-any), already-vanilla-placed slots, and pins of prize/event items.
3. Apply `pool_overrides:` (remove-then-add) to the settings-derived pool before fill; `add` SHALL refuse prize/event items, `remove` is best-effort.
4. PIN each manifest location's slot in the placement table and remove the pinned items from the to-place pool, then run the standard assumed-fill for all remaining locations. With `customizer_active == false` the placer SHALL be byte-for-byte unchanged (regression-corpus invariant).
5. Run the existing goal-completability + accessibility acceptance gates (per `randomizer-core / Generation rejects un-completable seeds`); `--allow-broken-seed` bypasses as for ordinary seeds.
6. Emit `customizer_active` in the spoiler meta block.

The dispatcher SHALL NOT distinguish customizer-built placements from assumed-fill-built placements; both produce identical `LocationDef → ItemDef` mappings.

Customizer mode SHALL be mutually exclusive with race mode: the race reveal regenerates placement from `(seed, settings)` alone and cannot reproduce manifest pins, so generation SHALL refuse the combination (CLI and slot paths both).

> **Deferred**: share-string encoding of `customizer_seed` (reproduce-by-manifest across users) — the value is computed (SHA-256 of manifest bytes, first 8) but not yet carried in the share string. See `tasks.md §6.4`.

#### Scenario: Customizer manifest pins are honored
- **WHEN** `--generate-seed --customizer=manifest.yaml --settings=mode.state=open,goal=fast_ganon` is invoked
- **THEN** every `placements:` entry appears verbatim in the resulting placement table, and every other location is filled by the standard assumed-fill

#### Scenario: Un-completable customizer manifest is rejected
- **WHEN** a customizer manifest places `Hookshot` at a location that no progression path reaches
- **THEN** generation fails with the same un-completable error as assumed-fill seeds; the spoiler is not written; `--allow-broken-seed` bypasses

#### Scenario: Race mode is refused
- **WHEN** generation is requested with both `race_mode` and `customizer_active` set
- **THEN** generation is refused with an error explaining the race reveal cannot regenerate manifest pins

#### Scenario: Dispatcher unchanged
- **WHEN** a customizer seed is loaded and the player opens a chest
- **THEN** the dispatcher fires identically to a standard seed; no dispatcher branch needs to know the placement came from customizer

#### Scenario: Slot path parity
- **WHEN** the same `(settings, seed, manifest)` is generated via `--generate-seed` and via the playable-slot path (`--generate-slot` / the native window)
- **THEN** both produce the identical `placement_digest`

### Requirement: Settings canonical serialization order — customizer extension

`settings.customizer_active` SHALL serialize as canonical byte `[26]` bit1 (`kCustomizerAxis_Active`, sharing the pad byte with `enemy_shuffle`'s bit0; `door_shuffle` owns `[27]` bits 0-1). Default false keeps the default `settings_hash` and the regression corpus byte-identical; the customizer-active axis itself did not grow `kSettingsCanonicalLen`.

`customizer_seed` (uint64 LE) SHALL be the SHA-256 of the manifest contents truncated to 8 bytes, computed at parse time. This makes share-strings reproducible across users who have the same manifest once the deferred share-string encoding lands (`tasks.md §6.4`).

#### Scenario: customizer_active bit participates in settings_hash
- **WHEN** two seeds have identical settings except `customizer_active`
- **THEN** their `settings_hash` values differ (only canonical byte `[26]` moves)

#### Scenario: Identical manifests produce identical customizer_seed
- **WHEN** two users run customizer mode with the same manifest file
- **THEN** the computed `customizer_seed = SHA-256(manifest_bytes)[0..8]` is byte-identical

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

