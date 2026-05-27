## MODIFIED Requirements

### Requirement: Settings canonical serialization order (normative)

The `RandoSettings` struct SHALL be canonically serialized field-by-field in the following order with the pinned widths. This serialization is the input to `SHA-256()` for the `settings_hash` computation and to the share-string encoder for the `seed_u64`-adjacent settings portion. The order is **normative spec**, not deferred to `audit.md`.

**Enum value names align with ALTTPR's config strings** (verified against `app/Randomizer.php` and `config/alttp.php` in `alttp_vt_randomizer`). Hand-translation from ALTTPR is mechanical when names match; share-string-to-PHP-config debugging is 1-to-1. Where ALTTPR uses hyphens (e.g., `triforce-hunt`), our CLI surface preserves the exact string; the C struct field substitutes underscore for the hyphen (parser does the translation).

1. `mode_state` — uint8 LE (CLI/share-string: `open=0`, `standard=1`, `inverted=2`, `retro=3`). ALTTPR key: `mode.state`.
2. `goal` — uint8 LE (CLI: `ganon=0`, `fast_ganon=1`, `dungeons=2`, `pedestal=3`, `triforce-hunt=4` (hyphenated per ALTTPR), `ganonhunt=5`, `completionist=6`). ALTTPR key: `goal`.
3. `crystals_ganon` — uint8 LE (0..7). ALTTPR key: `crystals.ganon`.
4. `crystals_tower` — uint8 LE (0..7). ALTTPR key: `crystals.tower`.
5. `tricks` — uint8 LE. **Phase B change**: width retained at uint8; the bitmask shape is preserved. Phase A pinned to `none=0`; Phase B un-pins user input. The set of trick bits SHALL be enumerated in `assets/rando/op_registry.yaml` `tricks:` table; bit positions are stable across `generator_version` bumps. Trick names follow ALTTPR convention (kebab-case: `boots-clip`, `fake-flippers`, `bunny-revival`, etc.). Note: this is **8 bits, capping trick count at 8 in Phase B**. Future widening to uint16 or uint64 is a `generator_version` bump trigger and a settings-order surgery — out of scope for this change.
6. `item_pool` — uint8 LE (`easy=0`, `normal=1`, `hard=2`, `expert=3`). ALTTPR key: `item.pool` (also referenced as `item_pool` in `World.php:993`).
7. `logic` — uint8 LE (`NoGlitches=0`, `OverworldGlitches=1`, `MajorGlitches=2`, `HybridMajorGlitches=3`, `NoLogic=4`; **Phase A pinned `NoGlitches`; Phase B un-pins user input to allow `OverworldGlitches` and `MajorGlitches`**). `HybridMajorGlitches` and `NoLogic` remain reserved for Phase C+. PascalCase preserved per ALTTPR convention at `Randomizer.php:122`. ALTTPR key: `logic`.
8. `mode_weapons` — uint8 LE (`randomized=0`, `assured=1`, `vanilla=2`, `swordless=3`; **Phase A supported `randomized`/`assured`; Phase B un-pins user input to allow `swordless`**). `vanilla` remains reserved for later phases. ALTTPR key: `mode.weapons`.
9. `accessibility` — uint8 LE (`items=0`, `locations=1`, `none=2`; **Phase A supported `items`/`locations`; Phase B un-pins user input to allow `none`**). ALTTPR key: `accessibility`.
10. `region_pyramid_bow_upgrade` — uint8 LE (**boolean**: `0=false` granting BowAndArrows, `1=true` granting BowAndSilverArrows per `Randomizer.php:150-152`). **Phase A pinned to `1=true`; Phase B un-pins user input to allow `0=false`** (Pyramid Fairy trade-in yields Bow+Arrows). ALTTPR key: `region.pyramidBowUpgrade`.
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

**Phase B note**: This change does NOT change the order, widths, or enum value assignments. The Phase B values (`logic >= OverworldGlitches`, `mode_weapons = swordless`, `accessibility = none`, `region_pyramid_bow_upgrade = false`, and any non-zero `tricks` bit) are already part of the Phase A canonical-serialization spec — Phase A pinned the user-facing input to a subset, not the byte layout. Default-settings seeds (all Phase A pin values: `tricks=none`, `logic=NoGlitches`, etc.) SHALL produce a `settings_hash` byte-identical to Phase A's hash for the same axis values.

#### Scenario: Reordering fields breaks settings_hash
- **WHEN** the canonical serialization order changes (e.g., swap fields 4 and 5)
- **THEN** the resulting `settings_hash` differs for the same logical settings, `generator_version` MUST advance, and the regression corpus MUST be regenerated

#### Scenario: Phase A defaults
- **WHEN** the user opens the settings screen and has not changed any field
- **THEN** the default values are: `mode_state=open`, `goal=fast_ganon`, `crystals_ganon=7`, `crystals_tower=7`, `tricks=none`, `item_pool=normal`, `logic=NoGlitches`, `mode_weapons=randomized`, `accessibility=items`, `region_pyramid_bow_upgrade=true`, `region_boss_hearts_in_pool=true`, `dungeon_items_*=vanilla`, `prize_shuffle=true`, `medallion_shuffle=true`, `race_mode=false`. `pieces_required` and `pieces_placed` defaults are pinned in `audit.md` against ALTTPR's `item.Goal.Required` and corresponding placed-count config; earlier drafts asserted 20/30 from memory — actual ALTTPR defaults to be confirmed during Phase 0 by reading `config/alttp.php`.

#### Scenario: Default settings hash preserved across Phase B un-pin
- **WHEN** a Phase A default-settings seed is generated after this change
- **THEN** the `settings_hash` is byte-identical to the Phase A-generated value for the same axis values — un-pinning user input does not change the canonical-serialization byte sequence for the default tuple

#### Scenario: Trick bitmask non-zero changes settings hash
- **WHEN** a seed is generated with `settings.tricks` having any bit set
- **THEN** the `settings_hash` differs from the equivalent `tricks=0` seed; CI corpus regenerates accordingly

#### Scenario: Swordless mode rejects sword placement at Pyramid Fairy Sword slot
- **WHEN** a seed has `settings.mode_weapons == swordless`
- **THEN** the `LOC_Pyramid_Fairy_Sword` slot's `can_place` predicate rejects all sword items; the slot is filled with a non-sword item from the pool

#### Scenario: pyramid_bow_upgrade=false (Phase B Bow+Arrows variant)
- **WHEN** a seed has `settings.region_pyramid_bow_upgrade == false`
- **THEN** the Pyramid Fairy trade-in grants Bow+Arrows rather than Bow+SilverArrows; spoiler reflects this

#### Scenario: accessibility=none allows un-completable seeds
- **WHEN** a seed has `settings.accessibility == none`
- **THEN** the generator does NOT enforce "every progression item is reachable"; un-reachable junk in the pool is permitted; `--allow-broken-seed` semantics overlap but accessibility=none is the principled axis

### Requirement: Assumed-fill placement

The placement algorithm SHALL use assumed fill — placing progression items into locations reachable under the assumption that all remaining unplaced items are temporarily available — and SHALL retry placement with bounded rewind when no valid location exists for the current item.

**Phase B implementation alignment (Bug #7 fix)**: the "bounded rewind" SHALL be per-item, not whole-attempt. Phase A1's implementation uses whole-attempt retry with `kAssumedFillMaxAttempts=8`; the SHALL above (already present in Phase A spec at `randomizer-core/spec.md:344`) describes per-item rewind. This change brings the implementation in line with the existing spec.

Per-item rewind algorithm: when the current item has no valid placement, rewind the last N placements (N is the per-item rewind budget, configurable; default 10), recompute the simulated inventory state, and retry placing the current item. If the per-item rewind budget exhausts, escalate to whole-attempt retry (existing `kAssumedFillMaxAttempts` path). If both budgets exhaust, surface a clear error.

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
- **WHEN** assumed fill exceeds the 5-second wall-clock budget
- **THEN** the generator falls back to forward fill (placing items into reachable locations in order) and surfaces a warning in the spoiler `fallback_warnings` array

#### Scenario: Same-seed determinism across budgets
- **WHEN** the same seed is generated under different `--budget-seconds` values that all succeed within budget
- **THEN** the resulting `placement_digest_hex` values are byte-identical — the budget is wall-clock fail-safe, not a determinism input
