## ADDED Requirements

### Requirement: Enemy shuffle runtime install and type substitution

When the `enemy_shuffle` axis is enabled for a slot, the system SHALL randomize **which enemy sprite spawns** in each dungeon room and overworld area with a deterministically-chosen replacement, applied at the two distinct load paths (which store the type differently — the spec must not conflate them):

- **Dungeon** (`Dungeon_LoadSingleSprite`, `src/sprite.c:3807`): the type id is `src[2]`, written to `sprite_type[k]`. Substitution rewrites the chosen type into `sprite_type[k]` (a type-byte swap; the bit-packed `y`/`x` bytes are NOT touched).
- **Overworld** (`Overworld_LoadSprites`, `src/sprite.c:3895`): there is no `sprite_type` write at load — the type is stored as `sprite_where_in_overworld[...] = src[2] + 1` (`:3910`) and the sprite is spawned lazily by `Overworld_LoadProximaSpriteIfAlive` (`:3973`). Substitution rewrites the stored value as `pick + 1` (preserving the `+1` bias). The mechanism is a map rewrite, not a `sprite_type` swap.

The substitution SHALL be a self-contained module (`src/rando/shuffle_enemies.{c,h}`) that, like boss shuffle and drop-pool shuffle, is generated deterministically from `(settings, seed_u64)` (a dedicated RNG stream forked off the seed per `randomizer-core / RNG family`), installed at slot activation (`Rando_ActivateSidecarSlot`) **only on the settings-valid path** (mirroring boss/drop shuffle, which `*_Deactivate()` on the invalid / snapshot-restore / v1-slot path so a stale substitution is never leaked — `rando.c:1904-1960`), and torn down at deactivation. When `enemy_shuffle` is off, the load path SHALL take the vanilla branch and behavior SHALL be byte-identical to a non-shuffled slot.

Enemy shuffle SHALL NOT touch item placement: it draws no fill RNG and adds no logic predicate, so `placement_digest_hex` SHALL be byte-identical for every seed regardless of `enemy_shuffle`. (The `generator_version` bump it triggers version-locks the live axis and reflects the per-seed pad-bit `settings_hash` change; the canonical length stays 28 — see `randomizer-core`.)

#### Scenario: Enemy shuffle off is vanilla-identical
- **WHEN** `enemy_shuffle` is off for a slot
- **THEN** no substitution is installed, every room/area spawns its vanilla enemy set, and the slot is byte-identical to the same slot generated without the axis

#### Scenario: Placement is unaffected by enemy shuffle
- **WHEN** two slots share an identical `share_string` except one has `enemy_shuffle` on and one off
- **THEN** their `placement_digest_hex` is byte-identical (enemy shuffle is orthogonal to item placement, like boss/drop shuffle)

#### Scenario: Substitution is deterministic
- **WHEN** the same `(settings, seed_u64)` is activated on any platform
- **THEN** each room/area receives the same replacement enemy set (cross-platform self-consistency)

### Requirement: Enemy shuffle GFX-sheet reshuffle and replacement safety

Every replacement enemy SHALL be drawn only from the set of enemies whose graphics-sheet requirement is satisfied by the room/area's **actually-loaded** sprite sheets and whose sprite type is observed in the same vanilla loader context (dungeon or overworld). For overworld areas, the replacement SHALL also require the current area's `overworld_sprite_palettes` value to be one that vanilla uses with that sprite type, using the same light/dark/special-area index that vanilla uses when it uploads the sprite palette. The loaded set derives from `sprite_graphics_index` → `kSpriteTilesets[index][0..3]` (`src/load_gfx.c:59`), **but a `0` subgroup entry does NOT load a sheet — it retains the previously-loaded `sprite_gfx_subset_N`** (`Gfx_LoadSpritesInner` loads each subset only `if (p[N])`, `load_gfx.c:627-640`), so the live loaded set depends on load history. The constraint check SHALL resolve safety against the live `sprite_gfx_subset_0..3` values. A replacement whose required sheets are not all present in the actually-loaded set, whose type is not present in vanilla sprite data for the current loader context, or whose overworld palette is not observed with that type in vanilla overworld data, SHALL NOT be selected.

When `enemy_shuffle` is active during a dungeon or overworld room/area sheet load, `EnemyShuffle_ReshuffleCurrentRoomSheets(row)` SHALL resolve the room/area's loaded sprite subgroup sheets and snapshot that resolved set before sprite graphics are decompressed. Runtime sheet widening MAY rewrite **owned, unpinned** subgroup slots among slots `0,1,2,3` only when the implementation also preserves the required sprite palettes for the chosen sheets. Until palette requirements are modeled, all subgroup slots SHALL resolve to the room/area's vanilla-resolved sheets. The hook SHALL be runtime-only and SHALL use the same parent `enemy_shuffle` axis (no additional canonical setting). For each subgroup slot:

- The current room/area's vanilla-resolved sheet is the row's non-zero sheet, or the per-slot inherited vanilla shadow when the row has `0`.
- A slot may reshuffle only if the row owns that slot, no present sprite/control object pins it, and the chosen sheet's palette requirements are satisfied by the room/area.
- The chosen sheet SHALL be deterministic from `(seed, room_or_area, slot)`, SHALL be drawn from that slot's generated dungeon/overworld pool plus the vanilla-resolved sheet, and SHALL never be `0`; while palette-aware widening is disabled, the chosen sheet SHALL be the vanilla-resolved sheet.
- Inherited or pinned slots SHALL restore the vanilla-resolved sheet so a prior room's reshuffle cannot leak through `0`-inheritance.
- The resolved sheet set SHALL be snapshotted with the room/area key that produced it. The sprite-type picker SHALL substitute only when that snapshot key matches the sprite list being loaded and still matches the live `sprite_gfx_subset_0..3`; if the snapshot is missing, stale, or overwritten by a later transition step, the picker SHALL leave the source sprite unchanged.

The generated tables SHALL be sourced from Enemizer's MIT `SpriteRequirement.cs` / `SpriteConstants.cs` through `assets/scripts/gen_enemy_shuffle_tables.py`, not from hand summaries. The generated data SHALL include:

- `kEnemyTable[256]` with every randomizable enemy's complete required-sheet set; multi-slot enemies SHALL list every required subgroup slot, not only one representative sheet.
- `kSheetNeed[256]` with `KNOWN` and per-slot pin bits for every known type; group-level/NPC/object/boss/unknown types SHALL pin conservatively.
- Per-slot dungeon and overworld sheet pools whose members are disjoint by slot, preserving the position-unaware picker invariant.
- `kOverlordNeed[32]` for dungeon overlord spawn-slot needs; a known spawning overlord pins only the slots its spawned sprite needs, while unknown/no-spawn/boss-spawning overlords pin all slots. Overworld overlords still pin all slots.

The runtime SHALL also derive a compact context allowlist from the shipped vanilla sprite blobs (`kDungeonSprites`/`kDungeonSpriteOffs` and all stages of `kOverworldSprites`/`kOverworldSpriteOffs`). During the overworld scan it SHALL record the `kOverworldSpritePalettes` value for each list and derive a per-type overworld palette allowlist. Candidate selection SHALL require those allowlists in addition to generated sheet requirements and manual `never_use_*` bans, so a sprite that merely has compatible sheets is not enough to appear in the other loader context or under the wrong overworld sprite palette.

The module SHALL NOT substitute entries flagged boss (or boss secondary) / object / NPC / do-not-randomize in the per-enemy constraint table, nor the per-context control and overlord markers — which differ between load paths:
- **Dungeon**: control entry `type == 0xe4`; overlord `x >= 0xe0` (`Dungeon_LoadSingleSprite`).
- **Overworld**: count/control marker `src[2] == 0xf4` (`Overworld_LoadSprites` `src/sprite.c:3903`); overlord `src[2] >= 0xf3` (`Overworld_LoadProximaSpriteIfAlive` `:3985-3992`) — NOT `x >= 0xe0`.

Excluded entries pass through unchanged.

#### Scenario: Replacement stays within actually-loaded graphics sheets
- **WHEN** enemy shuffle picks a replacement for a room whose loaded sheet set (after resolving `0`-inheritance against live `sprite_gfx_subset_N`) is known
- **THEN** the chosen enemy's required sheet(s) are a subset of that loaded set; no enemy needing an unloaded sheet is ever placed, including in rooms whose `kSpriteTilesets` row has `0` entries

#### Scenario: Replacement stays within vanilla loader context
- **WHEN** enemy shuffle builds the candidate pool for a dungeon room or overworld area
- **THEN** it excludes any enemy type that is not present in vanilla sprite data for that same loader context, even if that enemy's sheets are currently loaded

#### Scenario: Overworld replacement stays within vanilla sprite palette context
- **WHEN** enemy shuffle builds the candidate pool for an overworld area
- **THEN** it excludes any enemy type whose vanilla overworld occurrences do not use the area's current sprite-palette id, even if that enemy's sheets are currently loaded

#### Scenario: Sheet resolver preserves palette-safe subgroup slots
- **WHEN** a dungeon/overworld room owns subgroup slot `N` and no present sprite/overlord/object pins that slot
- **THEN** the runtime may load a deterministic sheet from the generated slot-`N` pool or the vanilla sheet before decompression only if that sheet is palette-safe for the room/area; otherwise the vanilla-resolved sheet is restored

#### Scenario: Multi-slot enemy requirements are complete
- **WHEN** a candidate enemy requires sheets in more than one subgroup slot
- **THEN** every required sheet must be present in the live loaded set before that enemy can be selected

#### Scenario: Stale transition sheet state fails closed
- **WHEN** a dungeon/overworld sprite list is loaded after a different room/area resolved sprite sheets
- **THEN** enemy shuffle does not use the stale sheet set to select replacements; affected entries pass through vanilla unless a matching snapshot exists for the current room/area

#### Scenario: Overlord rooms pin only known spawned-sheet needs
- **WHEN** a dungeon room contains an overlord marker with a generated `kOverlordNeed` entry
- **THEN** only the spawned sprite's required subgroup slots are pinned; unknown, no-spawn, boss-spawning, and overworld overlords pin all subgroup slots

#### Scenario: Excluded entries pass through (per-context markers)
- **WHEN** a loaded entry is a control/overlord marker (dungeon `0xe4` / `x>=0xe0`; overworld `0xf4` / `>=0xf3`), a boss (or boss secondary), an NPC, or a quest object
- **THEN** the module leaves it unchanged (bosses are owned by boss shuffle; NPCs/objects/triggers must stay intact)

### Requirement: Enemy shuffle preserves beatability where logic is blind

Logic does NOT model per-room kill-clear (the rando graph has no `CanKill<X>` predicate on a non-boss enemy — see `randomizer-shuffles` digest invariant), so beatability is enforced **entirely** by the runtime constraint table. A table bug does NOT move `placement_digest_hex` and is invisible to the corpus / `--rando-selftest` — it silently ships an unbeatable seed. The table is therefore the central correctness surface and SHALL conservatively mark any sprite of unknown safety as do-not-randomize. It SHALL preserve:

- **Dungeon-global killable + key-capable replacements**: `killable` and "may carry a key" are **independent** flags (Enemizer's `CannotHaveKey` is separate from `Killable` — e.g. Keese/Buzzblob/Geldman are killable but key-banned, `SpriteRequirement.cs`). As built, every substituted dungeon enemy SHALL satisfy `killable && !cannot_have_key`, not only rooms that are known key/shutter rooms. This over-approximates key/shutter safety and avoids relying on vanilla key-sprite scans, which are unreliable under item shuffle.
- **Room hard excludes**: Mimic Cave and Agahnim's Tower final bridge SHALL never substitute any enemy.
- **Flying restrictions**: rooms in the flying-exclude list SHALL not receive flying replacements.
- **Context safety**: the candidate pool SHALL be constrained by load context using both generated/manual directional bans (`never_use_dungeon` / `never_use_overworld`) and the runtime-derived vanilla-context allowlist. In overworld, it SHALL also be constrained by the runtime-derived per-type sprite-palette allowlist. A sprite whose sheets happen to be loaded is not eligible unless vanilla uses that sprite type in the same dungeon/overworld loader context and, for overworld, under the current sprite palette.
- **Boss / mini-boss / environment-dependent enemies**: bosses, GT mini-bosses, boss secondaries, Agahnim, Ganon, NPCs, quest objects, and unknown-safety sprites SHALL never be substituted or used to free reshuffle slots.

> **As-built note:** `ESF_WATER` is recorded in the table and the picker uses it for water-source sprites, so a vanilla water-capable source (for example Walking Zora) only substitutes to a water-capable replacement. This change does not implement an independent water-only room classifier for rooms whose source sprites are not themselves tagged water-capable; broader water stranding remains a playtest watch item / future refinement.

#### Scenario: Key room stays clearable under item shuffle
- **WHEN** a dungeon room is shuffled and item placement could put a key behind an enemy clear
- **THEN** every substituted dungeon enemy satisfies `killable && !cannot_have_key`, so the key/door remains obtainable independent of where vanilla placed key sprites

#### Scenario: Hard-excluded rooms pass through
- **WHEN** the room is Mimic Cave or Agahnim's Tower final bridge
- **THEN** enemy shuffle leaves every sprite unchanged

#### Scenario: Context restrictions respected
- **WHEN** an enemy is flagged `never_use_overworld` / `never_use_dungeon`, or does not appear in vanilla data for the current loader context
- **THEN** it is excluded from that context's candidate pool

#### Scenario: Overworld palette restrictions respected
- **WHEN** an enemy appears in vanilla overworld data but not with the current area's overworld sprite palette id
- **THEN** it is excluded from that overworld area's candidate pool

#### Scenario: Boss and mini-boss logic remains valid
- **WHEN** a dungeon or GT mini-boss location is gated by `CanKillBoss` or a direct `CanKill<Boss>` macro
- **THEN** enemy shuffle has not substituted the boss/mini-boss sprite that the logic predicate assumes

### Requirement: Enemy shuffle HP and contact-damage randomization

When `enemy_shuffle` is active, the same parent axis SHALL also apply deterministic per-seed, per-sprite-type stat variation to non-boss enemies at `SpritePrep_LoadProperties`:

- Health SHALL be scaled by a deterministic factor in `[0.5x, 2.0x]`, clamped to `[1,255]`.
- `base == 0` health SHALL remain `0`, so NPCs/objects/non-killable sprites are not made killable or otherwise changed.
- Boss and boss-secondary sprite types SHALL keep vanilla health, avoiding boss-specific table-index hazards such as Helmasaur King's `sprite_health >> 2` lookup.
- Plain contact-damage classes `1..8` SHALL be nudged by deterministic `-1/0/+1`, clamped to `[1,8]`.
- Damage values `0`, values with flag bits, and all boss/boss-secondary damage values SHALL pass through unchanged.

This stat variation SHALL be runtime-only under `enemy_shuffle`; it SHALL NOT add a canonical axis, placement predicate, or fill RNG draw.

#### Scenario: HP scaling is bounded and never creates zero-HP enemies
- **WHEN** a non-boss enemy with non-zero base HP is prepared under `enemy_shuffle`
- **THEN** its HP is deterministic for `(seed, sprite_type)`, is at least `1`, and is within the `[0.5x, 2.0x]` scaled bound after clamping

#### Scenario: Boss stats stay vanilla
- **WHEN** a boss, boss secondary, Agahnim, or Ganon sprite is prepared under `enemy_shuffle`
- **THEN** its HP and contact-damage values are unchanged from vanilla

#### Scenario: Damage flags are preserved
- **WHEN** a sprite's vanilla contact-damage byte is `0` or has high flag bits / special semantics
- **THEN** enemy shuffle leaves that byte unchanged
