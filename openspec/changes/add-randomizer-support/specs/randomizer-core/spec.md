## ADDED Requirements

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

The `RandoSettings` struct SHALL be canonically serialized field-by-field in the following order with the pinned widths. This serialization is the input to `SHA-256()` for the `settings_hash` computation and to the share-string encoder for the `seed_u64`-adjacent settings portion. The order is **normative spec**, not deferred to `audit.md`.

**Enum value names align with ALTTPR's config strings** (verified against `app/Randomizer.php` and `config/alttp.php` in `alttp_vt_randomizer`). Hand-translation from ALTTPR is mechanical when names match; share-string-to-PHP-config debugging is 1-to-1. Where ALTTPR uses hyphens (e.g., `triforce-hunt`), our CLI surface preserves the exact string; the C struct field substitutes underscore for the hyphen (parser does the translation).

1. `mode_state` — uint8 LE (CLI/share-string: `open=0`, `standard=1`, `inverted=2`, `retro=3`). ALTTPR key: `mode.state`.
2. `goal` — uint8 LE (CLI: `ganon=0`, `fast_ganon=1`, `dungeons=2`, `pedestal=3`, `triforce-hunt=4` (hyphenated per ALTTPR), `ganonhunt=5`, `completionist=6`). ALTTPR key: `goal`.
3. `crystals_ganon` — uint8 LE (0..7). ALTTPR key: `crystals.ganon`.
4. `crystals_tower` — uint8 LE (0..7). ALTTPR key: `crystals.tower`.
5. `tricks` — uint8 LE (Phase A: `none=0`; future trick bitmask reserved).
6. `item_pool` — uint8 LE (`easy=0`, `normal=1`, `hard=2`, `expert=3`). ALTTPR key: `item.pool` (also referenced as `item_pool` in `World.php:993`).
7. `logic` — uint8 LE (`NoGlitches=0`, `OverworldGlitches=1`, `MajorGlitches=2`, `HybridMajorGlitches=3`, `NoLogic=4`; Phase A pins `NoGlitches`). PascalCase preserved per ALTTPR convention at `Randomizer.php:122`. ALTTPR key: `logic`.
8. `mode_weapons` — uint8 LE (`randomized=0`, `assured=1`, `vanilla=2`, `swordless=3`; Phase A supports `randomized`/`assured`). ALTTPR key: `mode.weapons`.
9. `accessibility` — uint8 LE (`items=0`, `locations=1`, `none=2`; Phase A supports `items`/`locations`). ALTTPR key: `accessibility`.
10. `region_pyramid_bow_upgrade` — uint8 LE (**boolean**: `0=false` granting BowAndArrows, `1=true` granting BowAndSilverArrows per `Randomizer.php:150-152`). ALTTPR key: `region.pyramidBowUpgrade`.
11. `region_boss_hearts_in_pool` — uint8 LE (boolean; Phase A pinned to `1=true` for identity placement of the 10 boss-heart slots). ALTTPR key: `region.bossHeartsInPool`.
12. `dungeon_items_small_keys` — uint8 LE (`vanilla=0`, `dungeon=1`, `wild=2`).
13. `dungeon_items_big_keys` — uint8 LE (same).
14. `dungeon_items_maps` — uint8 LE (same).
15. `dungeon_items_compasses` — uint8 LE (same).
16. `prize_shuffle` — uint8 LE (boolean).
17. `medallion_shuffle` — uint8 LE (boolean).
18. `race_mode` — uint8 LE (boolean).
19. `pieces_required` — uint16 LE (Triforce Hunt / Ganon Hunt).
20. `pieces_placed` — uint16 LE.
21. Trailing zero-padding to a multiple of 4 bytes (reserved).

Changing this order — or the field widths, or the enum value assignments — is a `generator_version` bump trigger (per `tasks.md §13.6`).

#### Scenario: Reordering fields breaks settings_hash
- **WHEN** the canonical serialization order changes (e.g., swap fields 4 and 5)
- **THEN** the resulting `settings_hash` differs for the same logical settings, `generator_version` MUST advance, and the regression corpus MUST be regenerated

#### Scenario: Phase A defaults
- **WHEN** the user opens the settings screen and has not changed any field
- **THEN** the default values are: `mode_state=open`, `goal=fast_ganon`, `crystals_ganon=7`, `crystals_tower=7`, `tricks=none`, `item_pool=normal`, `logic=NoGlitches`, `mode_weapons=randomized`, `accessibility=items`, `region_pyramid_bow_upgrade=true`, `region_boss_hearts_in_pool=true`, `dungeon_items_*=vanilla`, `prize_shuffle=true`, `medallion_shuffle=true`, `race_mode=false`. `pieces_required` and `pieces_placed` defaults are pinned in `audit.md` against ALTTPR's `item.Goal.Required` and corresponding placed-count config; earlier drafts asserted 20/30 from memory — actual ALTTPR defaults to be confirmed during Phase 0 by reading `config/alttp.php`.

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

The system SHALL accept and emit a share string that encodes `(magic, generator_version, settings_hash, seed_u64, checksum)` as a single base32 token with a 4-byte magic prefix unique to this port.

#### Scenario: Round-trip encoding
- **WHEN** a share string is generated and then parsed
- **THEN** the decoded fields exactly match the originals and the checksum validates

#### Scenario: External ALTTPR hash is rejected
- **WHEN** the user enters a hash in the alttpr.com format
- **THEN** the parser detects the absent magic prefix, rejects the input, and displays an error explicitly naming the format mismatch

#### Scenario: Corrupted share string
- **WHEN** the user enters a share string with an altered character
- **THEN** the parser rejects it with a checksum-failure error and does not begin generation

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

The system SHALL junk-pad the pool so that its cardinality equals the active world-state's location-pool cardinality after applying dungeon-item shuffle modes (which expand or contract the location pool per `randomizer-shuffles`).

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
- **THEN** after junk-padding, the item pool contains exactly as many items as there are placeable locations in the active world-state

#### Scenario: Triforce Hunt junk-padding
- **WHEN** the goal is Triforce Hunt with `pieces_placed = N` and the unpadded pool is smaller than the location pool
- **THEN** the pool is padded with junk items (small rupee, single bomb, single arrow, small heart) until cardinality matches

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

The `RandoSettings` struct SHALL include the following axes with the documented Phase A value set; each appears in the canonical-serialization order documented in `audit.md`:

- `world_state`: Open / Standard / Inverted / Retro
- `goal`: `ganon` / `fast_ganon` / `dungeons` / `pedestal` / `triforce-hunt` / `ganonhunt` / `completionist` (snake_case + hyphenated per ALTTPR convention)
- `crystals.ganon`: 0..7 (default 7)
- `crystals.tower`: 0..7 (default 7)
- `tricks`: pinned to `none` in Phase A
- `item_pool_difficulty`: `easy` / `normal` (default) / `hard` / `expert`
- `logic`: pinned to `NoGlitches` in Phase A
- `mode.weapons`: `randomized` (default) / `assured` in Phase A; `vanilla` and `swordless` reserved for Phase B
- `accessibility`: `items` (default) / `locations` (auto-set when goal is Completionist); `none` reserved for Phase B
- `pyramid_bow_upgrade`: pinned to `silvers` in Phase A; `arrows` reserved for Phase B
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

#### Scenario: Budget override extends the generation budget
- **WHEN** `--budget-seconds=30` is passed and the configuration would otherwise exhaust the 5-second default budget
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

#### Scenario: Budget override extends the generation budget
- **WHEN** `--budget-seconds=30` is passed and the configuration would otherwise exhaust the 5-second default budget
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

#### Scenario: Progression items placed only in reachable locations
- **WHEN** a progression item is placed
- **THEN** at least one path through the logic graph reaches the chosen location using only items that precede it in the placement order plus all later-placed items

#### Scenario: Bounded retry on dead-end
- **WHEN** no location is reachable for the currently selected progression item
- **THEN** the placer rewinds the last N placements and retries, and generation eventually succeeds or fails with a clear error after the rewind budget is exhausted

#### Scenario: Forward-fill fallback after timeout
- **WHEN** assumed fill exceeds the 5-second wall-clock budget
- **THEN** the system falls back to forward fill, surfaces a warning in the spoiler header, and still produces a valid placement

### Requirement: Spoiler-log emission

The generator SHALL emit a spoiler log in two forms: a human-readable text file grouped by region, and a machine-readable JSON file with stable field names suitable for parsing by external tools. Spoiler files SHALL be written to a configurable directory keyed by share string. The JSON schema SHALL define `fallback_warnings` as an array of objects each with `code` (string enum, e.g., `"forward_fill_fallback"`, `"rewind_budget_exceeded_recovered"`) and `detail` (human-readable string).

#### Scenario: Both JSON and text spoilers are emitted
- **WHEN** a seed generates successfully
- **THEN** both `<spoiler_dir>/<share_string>.json` and `<spoiler_dir>/<share_string>.txt` are written to the configured spoiler directory

#### Scenario: fallback_warnings records forward-fill fallback
- **WHEN** the forward-fill fallback fires (per `Forward-fill fallback after timeout` in the assumed-fill requirement above)
- **THEN** the JSON spoiler's `fallback_warnings` array contains an object whose `code` field equals `"forward_fill_fallback"` and whose `detail` field is a human-readable string explaining the fallback

#### Scenario: Text spoiler is grouped by region
- **WHEN** the text spoiler file is read
- **THEN** placements are organised into one section per region (one region heading followed by that region's location/item lines), making the file scannable without external tooling

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
