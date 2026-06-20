# Medallion Plaque Custom Art Derivation

`src/rando/medallion_icons.c` contains sparse byte deltas for the shuffled
Misery Mire and Turtle Rock entrance plaques. These deltas are derived from
MIT-licensed custom z3randomizer art, not from the vanilla ROM.

- Source repository: https://github.com/KatDevsGames/z3randomizer
- Source art blobs:
  - `data/99ff1_bombos.gfx`
  - `data/99ff1_quake.gfx`
  - `data/a6fc4_bombos.gfx`
  - `data/a6fc4_ether.gfx`
- Runtime consumer: `src/rando/medallion_icons.c`
- License notice: `NOTICE`, section "z3randomizer (KatDevsGames) - custom randomizer art"

The upstream patcher swaps compressed BG-sheet pointers. This fork instead
copies only the changed source-tile bytes into unused characters and rewrites
the plaque's map8 words. That keeps the custom glyph art small and avoids
patching broad terrain chars shared by Turtle Rock.

Do not replace these deltas with full decompressed sheets or ROM-extracted byte
tables. If the art changes, regenerate the sparse delta from the MIT source art
and keep the derivation note and NOTICE attribution in sync.
