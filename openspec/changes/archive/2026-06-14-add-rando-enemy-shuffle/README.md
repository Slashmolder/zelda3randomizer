# add-rando-enemy-shuffle

A randomizer **enemy shuffle** axis: randomize **which enemy spawns** in each dungeon room and overworld area, GFX-sheet-constrained so nothing crashes, as a per-seed deterministic **slot axis** (reproducible for races). Reference: the MIT-licensed Enemizer source checkout — mined for constraints, not copied.

## Status

**Archived 2026-06-14.** Sprite-type substitution is live (default-off):
build-verified, `--rando-selftest` green, corpus byte-identical, and
runtime-confirmed. Per-seed enemy HP/contact-damage randomization has shipped
too (kGen 64→65; bosses exempt from stat scaling). **The sheet-group reshuffle
machinery (`design.md` D4 / `tasks.md` §7.4 — the variety unlock, all 4 subgroup
slots, dungeon-overlord-aware) is palette/context gated after F12 playtest;
runtime preserves vanilla-resolved sheets where sprite palette requirements are
not yet modeled.** Remaining deferred axes: killable-thief/bush/absorbables
(`tasks.md` §7).

## Read these in order

| File | Purpose |
|---|---|
| [proposal.md](proposal.md) | Why, the module, the constraint table, the settings axis + UI, impact |
| [design.md](design.md) | Install model (copies boss shuffle), the substitution algorithm, the constraint model (from Enemizer), zelda3 hazards, logic-free verification |
| [specs/randomizer-shuffles/spec.md](specs/randomizer-shuffles/spec.md) | ADDED — enemy-shuffle behavior, palette-gated GFX-sheet resolver, conservative beatability invariants, HP/contact-damage randomization |
| [specs/randomizer-core/spec.md](specs/randomizer-core/spec.md) | ADDED — `enemy_shuffle` canonical axis (pad-bit-packed, length stays 28, kGen +1 = 60→61 as-built, placement byte-identical) |
| [specs/randomizer-native-window/spec.md](specs/randomizer-native-window/spec.md) | ADDED — enemy-shuffle live checkbox (was a "coming soon" placeholder) |
| [tasks.md](tasks.md) | Implementation checklist (constraint table → logic sweep → module → axis → UI → validation) |

## Key facts

- **Module**: `src/rando/shuffle_enemies.{c,h}` — mirrors `shuffle_boss.{c,h}` (deterministic `Generate`, install at `Rando_ActivateSidecarSlot`, teardown at `Rando_DeactivateSlot`).
- **Mechanism** (two paths differ): **dungeon** rewrites `sprite_type[k]` from `src[2]` (`Dungeon_LoadSingleSprite` `src/sprite.c:3807`); **overworld** rewrites `sprite_where_in_overworld = pick+1` (`Overworld_LoadSprites` `:3895`, lazy spawn at `:3973`). Constrained to the **actually-loaded** sheets — `kSpriteTilesets` (`load_gfx.c:59`) with `0` entries resolved against live `sprite_gfx_subset_N` (`0` inherits the prior sheet). Never mutates the bit-packed y/x coords.
- **Correctness surface**: the generated per-enemy constraint table (`gen_enemy_shuffle_tables.py` from Enemizer's MIT `SpriteRequirement.cs` / `SpriteConstants.cs`). GFX-sheet match = anti-crash; every dungeon replacement is `killable && !cannot_have_key`; hard-excluded/flying-excluded rooms and directional bans are enforced. Water-capable source sprites stay water-capable, but there is no independent room classifier yet, so broader water stranding remains a playtest watch item. This table is the **sole** beatability enforcer (logic models no per-room kill-clear) — a bug ships an unbeatable seed the corpus can't catch.
- **Placement byte-identical for ALL seeds** (sweep-confirmed: no non-boss `CanKill` predicate). `enemy_shuffle` packs into a **reserved canonical pad bit** — `kSettingsCanonicalLen` stays **28**, default `settings_hash` byte-identical. kGen +1 (60→61 as-built) + corpus regen only advance the manifest version (digests unchanged) and version-lock the live axis.
- **Bosses excluded** (boss shuffle owns them; the env-dependent ones — Blind/Kholdstare/Trinexx — must never be substituted).
- **Validation is playtest-dominated** — corpus + `--rando-selftest` are generation-only; they do NOT cover sprite rendering, room crashes, or softlocks.

## Dependencies

- Builds on **`add-rando-shuffles-and-minigames`** (boss/drop shuffle): reuses the install pattern. That change and this one are both archived, so the baseline `randomizer-core`, `randomizer-shuffles`, and `randomizer-native-window` specs now include the shipped fields and UI parity notes.
- Phase A archived (baseline `randomizer-*` capabilities).
