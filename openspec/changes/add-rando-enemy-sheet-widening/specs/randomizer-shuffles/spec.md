## MODIFIED Requirements

### Requirement: Enemy shuffle GFX-sheet reshuffle and replacement safety

Every replacement enemy SHALL be drawn only from the set of enemies whose graphics-sheet requirement is satisfied by the room/area's **actually-loaded** sprite sheets and whose sprite type is observed in the same vanilla loader context (dungeon or overworld). For overworld areas, the replacement SHALL also require the current area's `overworld_sprite_palettes` value to be one that vanilla uses with that sprite type, using the same light/dark/special-area index that vanilla uses when it uploads the sprite palette. For dungeon rooms, when palette-aware widening is enabled, the replacement SHALL also require the room's resolved sprite-palette signature — the `(palette_sp0l, palette_sp5l, palette_sp6l)` triple from `kDungPalinfos[GetRoomHeaderPtr(room)[1]]` — to be one that vanilla uses with that sprite type. The loaded set derives from `sprite_graphics_index` → `kSpriteTilesets[index][0..3]` (`src/load_gfx.c:59`), **but a `0` subgroup entry does NOT load a sheet — it retains the previously-loaded `sprite_gfx_subset_N`** (`Gfx_LoadSpritesInner` loads each subset only `if (p[N])`, `load_gfx.c:627-640`), so the live loaded set depends on load history. The constraint check SHALL resolve safety against the live `sprite_gfx_subset_0..3` values. A replacement whose required sheets are not all present in the actually-loaded set, whose type is not present in vanilla sprite data for the current loader context, whose overworld palette is not observed with that type in vanilla overworld data, or — in a dungeon with widening enabled — whose dungeon palette signature is not observed with that type in vanilla dungeon data, SHALL NOT be selected.

When `enemy_shuffle` is active during a dungeon or overworld room/area sheet load, `EnemyShuffle_ReshuffleCurrentRoomSheets(row)` SHALL resolve the room/area's loaded sprite subgroup sheets and snapshot that resolved set before sprite graphics are decompressed. Runtime sheet widening rewrites **owned, unpinned** subgroup slots among slots `0,1,2,3` to palette-safe sheets, and SHALL commit a widened slot only when the resulting loaded set still admits a valid forced-substitution target for the room/area (the verify-then-commit guarantee below); otherwise that slot SHALL revert to the vanilla-resolved sheet. Widening is gated by a build flag (`ES_ENABLE_SHEET_WIDENING`) for rollback; the shipped default SHALL be enabled, and when the flag is disabled the dungeon palette gate and the widening both go inert so all subgroup slots resolve to the room/area's vanilla-resolved sheets (byte-identical to the pre-widening behavior). The hook SHALL be runtime-only and SHALL use the same parent `enemy_shuffle` axis (no additional canonical setting). For each subgroup slot:

- The current room/area's vanilla-resolved sheet is the row's non-zero sheet, or the per-slot inherited vanilla shadow when the row has `0`.
- A slot may reshuffle only if the row owns that slot, no present sprite/control object pins it, and the chosen sheet is palette-safe for the room/area (its candidate enemies render under a sprite palette this room/area loads).
- The chosen sheet SHALL be deterministic from `(seed, room_or_area, slot)`, SHALL be drawn from that slot's generated dungeon/overworld pool plus the vanilla-resolved sheet, and SHALL never be `0`; a widened sheet SHALL be committed only if it passes the forced-substitution verify, otherwise the slot SHALL resolve to the vanilla-resolved sheet.
- Inherited or pinned slots SHALL restore the vanilla-resolved sheet so a prior room's reshuffle cannot leak through `0`-inheritance.
- The resolved sheet set SHALL be snapshotted with the room/area key that produced it. The sprite-type picker SHALL substitute only when that snapshot key matches the sprite list being loaded and still matches the live `sprite_gfx_subset_0..3`; if the snapshot is missing, stale, or overwritten by a later transition step, the picker SHALL leave the source sprite unchanged.

**Verify-then-commit (forced-substitution fillability).** Widening a slot from its vanilla-resolved sheet removes that sheet from the loaded set, so every randomizable enemy the room/area carries on that slot is *forced* to substitute (its own sheet is no longer present). The implementation SHALL NOT commit a widened sheet for a slot unless, over the resulting live 4-slot set, the room/area still admits at least one valid substitute under the same constraints the picker applies — for dungeons `killable && !cannot_have_key` and palette-compatible with all required sheets loaded (and a water-capable such substitute when the room has a water source); for overworld, palette-compatible with all required sheets loaded (and water-capable when the area has a water source). When no such substitute exists, the slot SHALL revert to the vanilla-resolved sheet, which by construction restores fillability. This guarantee SHALL hold for every reachable widened set, so widening can never strand a forced substitution into an unloaded-sheet (garbage) render or render a key/shutter room unclearable.

The generated tables SHALL be sourced from Enemizer's MIT `SpriteRequirement.cs` / `SpriteConstants.cs` through `assets/scripts/gen_enemy_shuffle_tables.py`, not from hand summaries. The generated data SHALL include:

- `kEnemyTable[256]` with every randomizable enemy's complete required-sheet set; multi-slot enemies SHALL list every required subgroup slot, not only one representative sheet.
- `kSheetNeed[256]` with `KNOWN` and per-slot pin bits for every known type; group-level/NPC/object/boss/unknown types SHALL pin conservatively.
- Per-slot dungeon and overworld sheet pools whose members are disjoint by slot, preserving the position-unaware picker invariant.
- `kOverlordNeed[32]` for dungeon overlord spawn-slot needs; a known spawning overlord pins only the slots its spawned sprite needs, while unknown/no-spawn/boss-spawning overlords pin all slots. Overworld overlords still pin all slots.

The runtime SHALL also derive a compact context allowlist from the shipped vanilla sprite blobs (`kDungeonSprites`/`kDungeonSpriteOffs` and all stages of `kOverworldSprites`/`kOverworldSpriteOffs`). During the overworld scan it SHALL record the `kOverworldSpritePalettes` value for each list and derive a per-type overworld palette allowlist. During the dungeon scan it SHALL record each room's resolved sprite-palette signature (`kDungPalinfos[GetRoomHeaderPtr(room)[1]]`, the `(sp0l,sp5l,sp6l)` triple) and derive a per-type dungeon palette allowlist; two palinfo indices that resolve to the same triple SHALL count as one signature. Candidate selection SHALL require those allowlists in addition to generated sheet requirements and manual `never_use_*` bans, so a sprite that merely has compatible sheets is not enough to appear in the other loader context, under the wrong overworld sprite palette, or — with widening enabled — under a dungeon sprite-palette signature it is not observed with in vanilla.

The module SHALL NOT substitute entries flagged boss (or boss secondary) / object / NPC / do-not-randomize in the per-enemy constraint table, nor the per-context control and overlord markers — which differ between load paths:
- **Dungeon**: control entry `type == 0xe4`; overlord `x >= 0xe0` (`Dungeon_LoadSingleSprite`).
- **Overworld**: count/control marker `src[2] == 0xf4` (`Overworld_LoadSprites` `src/sprite.c:3903`); overlord `src[2] >= 0xf3` (`Overworld_LoadProximaSpriteIfAlive` `:3985-3992`) — NOT `x >= 0xe0`.

Excluded entries pass through unchanged.

Enabling widening SHALL NOT alter item placement: it draws no fill RNG and adds no logic predicate, so `placement_digest_hex` SHALL be byte-identical for every seed and the regression corpus SHALL regenerate byte-identical. The change SHALL version-lock the now-live runtime behavior via a `kGeneratorVersion` bump.

> `kGeneratorVersion` 76→77 (re-grep the live value at implement time per the version-drift convention); only the version field moves — every seed's `placement_digest_hex` is byte-identical and `kSettingsCanonicalLen` is unchanged (widening rides the existing `enemy_shuffle` axis with no new canonical setting).

#### Scenario: Replacement stays within actually-loaded graphics sheets
- **WHEN** enemy shuffle picks a replacement for a room whose loaded sheet set (after resolving `0`-inheritance against live `sprite_gfx_subset_N`) is known
- **THEN** the chosen enemy's required sheet(s) are a subset of that loaded set; no enemy needing an unloaded sheet is ever placed, including in rooms whose `kSpriteTilesets` row has `0` entries

#### Scenario: Replacement stays within vanilla loader context
- **WHEN** enemy shuffle builds the candidate pool for a dungeon room or overworld area
- **THEN** it excludes any enemy type that is not present in vanilla sprite data for that same loader context, even if that enemy's sheets are currently loaded

#### Scenario: Overworld replacement stays within vanilla sprite palette context
- **WHEN** enemy shuffle builds the candidate pool for an overworld area
- **THEN** it excludes any enemy type whose vanilla overworld occurrences do not use the area's current sprite-palette id, even if that enemy's sheets are currently loaded

#### Scenario: Dungeon replacement stays within vanilla sprite palette signature
- **WHEN** enemy shuffle builds the candidate pool for a dungeon room and palette-aware widening is enabled
- **THEN** it excludes any enemy type whose vanilla dungeon occurrences do not use the room's current `(sp0l,sp5l,sp6l)` palette signature, even if that enemy's sheets are currently loaded

#### Scenario: Widened slot keeps the room fillable
- **WHEN** widening would rewrite an owned, unpinned slot to a sheet under which the resulting loaded set has no valid forced-substitution target for the room/area (for a dungeon: no `killable && !cannot_have_key`, palette-compatible, all-sheets-loaded enemy; for a room with a water source: additionally no such water-capable enemy)
- **THEN** that slot reverts to the vanilla-resolved sheet, so the room/area always retains a valid substitute and no forced substitution renders garbage or leaves a key/shutter room unclearable

#### Scenario: Sheet resolver commits only palette-safe, fillable widened slots
- **WHEN** a dungeon/overworld room owns subgroup slot `N`, no present sprite/overlord/object pins that slot, and palette-aware widening is enabled
- **THEN** the runtime may load a deterministic sheet from the generated slot-`N` pool or the vanilla sheet before decompression only if that sheet is palette-safe for the room/area and passes the verify-then-commit fillability check; otherwise the vanilla-resolved sheet is restored

#### Scenario: Widening disabled is vanilla-resolved and byte-identical
- **WHEN** the build flag `ES_ENABLE_SHEET_WIDENING` is disabled
- **THEN** every subgroup slot resolves to the room/area's vanilla-resolved sheet, the dungeon palette gate is inert, and runtime substitution behavior is byte-identical to the pre-widening build

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

#### Scenario: Placement is unaffected by enabling widening
- **WHEN** two seeds are generated with identical `(generator_version, settings, seed_u64)`, one on a build with widening enabled and one disabled
- **THEN** their `placement_digest_hex` is byte-identical (widening is runtime-only and orthogonal to item placement, like boss/drop shuffle)
