# add-rando-enemy-shuffle

A randomizer **enemy shuffle** axis: randomize **which enemy spawns** in each dungeon room and overworld area, GFX-sheet-constrained so nothing crashes, as a per-seed deterministic **slot axis** (reproducible for races). The constraint tables are generated from Enemizer's MIT-licensed C# sources; implementation code is native to this project.

## Status

**Implemented; render/crash/softlock playtest pending.** Sprite-type substitution, all-slot sheet-group reshuffle, dungeon-overlord-aware pinning, and per-seed enemy HP/contact-damage randomization are live under the default-off `enemy_shuffle` axis. Bosses are exempt from substitution and stat scaling. Remaining deferred axes: killable-thief, bush enemies, absorbables, and randomize-on-hit (`tasks.md` §7).

## Read these in order

| File | Purpose |
|---|---|
| [proposal.md](proposal.md) | Why, the module, the constraint table, the settings axis + UI, impact |
| [design.md](design.md) | Install model (copies boss shuffle), the substitution algorithm, the constraint model (from Enemizer), zelda3 hazards, logic-free verification |
| [specs/randomizer-shuffles/spec.md](specs/randomizer-shuffles/spec.md) | ADDED — enemy-shuffle behavior, all-slot GFX-sheet reshuffle, conservative beatability invariants, HP/contact-damage randomization |
| [specs/randomizer-core/spec.md](specs/randomizer-core/spec.md) | ADDED — `enemy_shuffle` canonical axis (byte `[26]` bit0, length stays 28, placement byte-identical) |
| [specs/randomizer-native-window/spec.md](specs/randomizer-native-window/spec.md) | ADDED — enemy-shuffle live checkbox (was a "coming soon" placeholder) |
| [tasks.md](tasks.md) | Implementation checklist (constraint table → logic sweep → module → axis → UI → validation) |

## Key facts

- **Module**: `src/rando/shuffle_enemies.{c,h}` — mirrors `shuffle_boss.{c,h}` (deterministic `Generate`, install at `Rando_ActivateSidecarSlot`, teardown at `Rando_DeactivateSlot`).
- **Mechanism** (two paths differ): **dungeon** rewrites `sprite_type[k]` from `src[2]` (`Dungeon_LoadSingleSprite` `src/sprite.c:3807`); **overworld** rewrites `sprite_where_in_overworld = pick+1` (`Overworld_LoadSprites` `:3895`, lazy spawn at `:3973`). Constrained to the **actually-loaded** sheets — `kSpriteTilesets` (`load_gfx.c:59`) with `0` entries resolved against live `sprite_gfx_subset_N` (`0` inherits the prior sheet). Never mutates the bit-packed y/x coords.
- **Correctness surface**: the generated per-enemy constraint table (`gen_enemy_shuffle_tables.py` from Enemizer's MIT `SpriteRequirement.cs` / `SpriteConstants.cs`). GFX-sheet match = anti-crash; every dungeon replacement is `killable && !cannot_have_key`; hard-excluded/flying-excluded rooms and directional bans are enforced. Water capability is tabled but not yet classified by room, so water stranding remains a playtest watch item. This table is the **sole** beatability enforcer (logic models no per-room kill-clear) — a bug ships an unbeatable seed the corpus can't catch.
- **Placement byte-identical for ALL seeds** (sweep-confirmed: no non-boss `CanKill` predicate). `enemy_shuffle` packs into canonical byte `[26]` bit0 — `kSettingsCanonicalLen` stays **28**, default `settings_hash` byte-identical. The generator-version bump version-locks the live axis; corpus digests stay unchanged.
- **Bosses excluded** (boss shuffle owns them; the env-dependent ones — Blind/Kholdstare/Trinexx — must never be substituted).
- **Validation is playtest-dominated** — corpus + `--rando-selftest` are generation-only; they do NOT cover sprite rendering, room crashes, or softlocks.

## Dependencies

- Builds on **`add-rando-shuffles-and-minigames`** (boss/drop shuffle): reuses the install pattern.
- Phase A archived (baseline `randomizer-*` capabilities).
