## Why

The randomizer shuffles items, prizes, medallions, bosses, drops, and entrances, but the **enemies in each room/area are still vanilla**. Enemy randomization ("Enemizer") is a staple of the SNES ALTTPR scene and a frequently-requested axis. We have a strong precedent to copy — **boss shuffle** (`src/rando/shuffle_boss.c`) already does per-seed, deterministic, install-at-slot-load room redirection — and a good reference in Enemizer's MIT-licensed C#/asm sources. We are our **own C implementation** and are not bound to Enemizer's approach; we mine it for the *constraints* (which are load-bearing) rather than its mechanism.

**Original scope (owner decision):** the first change was **sprite-type substitution only** — randomize *which enemy* spawns in each dungeon room and overworld area, constrained so nothing crashes — as a **randomizer slot axis** (deterministic from the seed, reproducible for races). **As built, this same change was extended** with the sheet-group variety unlock and per-seed HP/contact-damage randomization under the same `enemy_shuffle` axis. Killable-thief / bush-enemy / absorbable / randomize-on-hit axes remain deferred.

## What Changes

### New module: `src/rando/shuffle_enemies.{c,h}`

Mirror the boss-shuffle install pattern exactly:
- `EnemyShuffle_Generate(settings, seed_u64)` builds the per-seed substitution deterministically (xoshiro256\*\* fork off the seed, like `BossShuffle_Generate` / `DropShuffle_Generate`); installed from `Rando_ActivateSidecarSlot` (`src/rando/rando.c`), torn down by `EnemyShuffle_Deactivate` in `Rando_DeactivateSlot`.
- At sprite load, when the axis is active and the entry is a **randomizable enemy**, substitute a deterministic pick from the enemies whose GFX sheets are actually loaded for the room/area. The two paths store the type differently (verified): **dungeon** `Dungeon_LoadSingleSprite` (`src/sprite.c:3807`) writes `src[2]` → `sprite_type[k]` (a type-byte swap); **overworld** `Overworld_LoadSprites` (`:3895`) stores `sprite_where_in_overworld = src[2] + 1` (`:3910`) and spawns lazily via `Overworld_LoadProximaSpriteIfAlive` (`:3973`), so the swap rewrites that map value as `pick + 1`. The loaded-sheet set derives from `sprite_graphics_index` → `kSpriteTilesets` (`src/load_gfx.c:59`) **but `0` subgroup entries inherit the prior sheet** (`load_gfx.c:627-640`) — resolve against live `sprite_gfx_subset_N`, not the static row. Off / non-enemy / marker entries → no change → byte-identical to vanilla.

### Per-enemy constraint table (the bulk of the work)

There is **no `SpriteRequirement` analog in zelda3 today**. The generated tables cross-check Enemizer's `SpriteRequirement.cs` against this fork's `Sprite_HEX_*` ids. Per enemy type they record which sheet(s) it needs, plus flags — `killable`, **`cannot_have_key` (independent of `killable`)**, `is_water`, `never_use_dungeon` / `never_use_overworld` (directional bans), `is_overlord`/`is_object`/`is_npc`/`is_boss` (exclude), floor-vs-flying, and room/pin constraints. This table is the **sole beatability enforcer** (logic models no per-room kill-clear — see Impact), so a bug ships an unbeatable seed the corpus can't catch.

### Settings axis + UI

- New `enemy_shuffle` (uint8 bool) field in `RandoSettings`. In the canonical serialization it is **bit-packed into a reserved pad bit** (e.g. byte `[26]` bit 0) — NOT appended as a new byte. `kSettingsCanonicalLen` stays **28** (no size-coupling cascade), following the entrance-shuffle precedent that packed into pad byte `[25]` (`rando_settings.c:210-219`). Default off ⇒ default-settings `settings_hash` byte-identical.
- `kGeneratorVersion` advances to version-lock the new live runtime axis; an `enemy_shuffle=on` seed flips a pad bit, changing only *that* seed's `settings_hash`.
- Checkbox in the rando-settings panel (`src/rando/rando_window/rando_window.cpp`, where `boss_shuffle`/`drop_shuffle` are wired). PC native window only; in-game screen compiled out on PC.

## Capabilities

### New Capabilities

(none)

### Modified Capabilities

- `randomizer-shuffles`: ADDED Requirement **"Enemy shuffle (sprite-type substitution)"** — the deterministic install model, the GFX-sheet constraint, the exclusion set (overlords/control/NPC/object/boss), the killable-in-key/shutter-room + water-room beatability invariants, and the all-off no-op guarantee.
- `randomizer-core`: ADDED Requirement (enemy-shuffle canonical axis) — `enemy_shuffle` packs into canonical byte `[26]` bit0 (length stays 28; default hash byte-identical).
- `randomizer-native-window`: ADDED Requirement for the enemy-shuffle live checkbox.

## Impact

- **New module**: `src/rando/shuffle_enemies.{c,h}` + the per-enemy constraint table (source table or codegen).
- **Patch sites**: `Dungeon_LoadSingleSprite` / `Overworld_LoadSprites` (`src/sprite.c`), `Rando_ActivateSidecarSlot` / `Rando_DeactivateSlot` (`src/rando/rando.c`, install only on the settings-valid path — mirror boss/drop's fail-closed `*_Deactivate()`), `RandoSettings` + the pad-bit in the canonical serializer (`src/rando/rando_settings.{h,c}`), `kGeneratorVersion` (`src/rando/rando.h`), rando-settings UI (`rando_window.cpp`), and register `EnemyShuffle_SelfCheck` in `Rando_RunAllSelfChecks`.
- **Placement is untouched (sweep-confirmed).** The logic sweep verified the rando graph has no `CanKill<X>` predicate on a non-boss enemy (only `OP_CAN_KILL_BOSS` + player-firepower macros), so enemy shuffle draws no fill RNG and changes no predicate: **`placement_digest_hex` is byte-identical for every seed**, including `enemy_shuffle=on`, and **default-settings `settings_hash` is unchanged too** (the pad bit is 0 by default; length stays 28). The `kGeneratorVersion` bump + corpus regen only advance the corpus manifest version (digests unchanged) and version-lock the new live axis. **Corollary**: because logic models no per-room kill-clear, beatability rests *entirely* on the runtime constraint table — a table bug is invisible to the corpus/`--rando-selftest`.
- **Save**: no new sidecar field — the substitution regenerates from `(seed, settings)` at activation (like boss shuffle). Backward-load: an N-written slot loads on N+1 with the one-time informational warning (`randomizer-save / upgrade safety`).
- **Effort**: the constraint table is the bulk (243 sprite types cross-checked against Enemizer). The install/patch plumbing is small (boss-shuffle is the template). Validation is **playtest-dominated** — no headless test covers sprite rendering or room crashes (corpus/`--rando-selftest` are generation-only).
- **Regression risk**: a GFX-sheet mismatch crashes/garbles; a non-killable enemy in a key/shutter room softlocks. Both are prevented by the constraint table — the central correctness surface.

## Status

**Implemented, then widened in-place.** Sprite-type substitution is live as a default-off rando slot axis. The variety unlock has landed under the same axis: runtime sheet-group reshuffle of subgroup slots `0..3`, generated Enemizer-sourced tables, dungeon-overlord spawned-slot decode, and per-seed HP/contact-damage randomization (bosses exempt). Render/crash/softlock playtest remains pending; water-only room classification and OAM-footprint modelling are not yet guaranteed. See [design.md](design.md) for the algorithm and hazards; [README.md](README.md) for the file index.
