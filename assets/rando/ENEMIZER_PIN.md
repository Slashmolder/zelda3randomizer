# Enemizer Constraint Source Pin

The enemy-shuffle constraint tables in `src/rando/shuffle_enemies.c` are
generated from the MIT-licensed Enemizer source, not from ROM data.

- Source repository: https://github.com/Zarby89/Enemizer
- Source commit used for this import: `09b890d15560cb1c810832bdc97a440ae569c55e`
- Source files:
  - `EnemizerLibrary/EnemyRandomizer/SpriteRequirement.cs`
  - `EnemizerLibrary/EnemyRandomizer/SpriteConstants.cs`
- Generator: `assets/scripts/gen_enemy_shuffle_tables.py`
- License notice: `NOTICE`, section "Enemizer - enemy-shuffle constraint provenance"

The generated C tables are structural sprite-id, sheet-slot, room-safety, boss,
and overlord facts used to keep substitutions render-safe and beatable. They
must not include graphics bytes, room layouts, or byte sequences extracted from
a ROM.

When updating these tables, run the generator against an explicitly selected
Enemizer checkout and update the source commit above in the same change.
