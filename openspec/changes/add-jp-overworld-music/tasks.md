# Tasks — add-jp-overworld-music

As-built: behavior shipped and merged to main (commit `2dc0439`) before this
spec; implementation tasks are `[x]` (cite the commit), playtest is `[ ]`.
Archive-gate: owner playtest.

## 1. Flag + behavior

- [x] 1.1 `kFeatures0_JpOverworldMusic = 524288` (bit 19) in `src/features.h`, with
  a neighbor-style comment + the point-of-use gate documented.
- [x] 1.2 JP-vs-US disassembly of both routines (romdiff spike): the only delta is
  the `$12C`/`$12D` music/ambient writes; destination/position/screen/camera are
  byte-identical JP↔US. Recorded in the commit message.
- [x] 1.3 `src/overworld.c`: `JpOwMusicEnabled()` helper; both routines gated
  `if (JP) { <JP music> } else { <US unchanged> }`. JP branches reproduce the JP
  disassembly faithfully; else-branches byte-identical to the prior code; only
  `music_control`/`sound_effect_ambient` change.

## 2. Config + UI

- [x] 2.1 `src/config.c`: INI key `"JpOverworldMusic"` (`ParseBoolBit`) + aligned
  `kFeatKeys[]`/`kFeatMasks[]` entries (the `_Static_assert` still passes).
- [x] 2.2 `src/rando/rando_window/game_config_panels.cpp`: `FeatureCheckbox`
  "JP 1.0 overworld music" with a one-line player-fact tooltip.

## 3. Validation

- [x] 3.1 MSVC Release x64 builds 0-warning; `--rando-selftest` all subsystems OK.
- [x] 3.2 Corpus byte-identical (default-off `features0` gate); no
  `kGeneratorVersion` bump. Verified the JP branch actually changes the gated
  routines' `$12C`/`$12D` writes vs the US branch (disassembly-grounded).

## 4. Playtest (archive-gate)

- [ ] 4.1 In-game: with the checkbox ON, confirm the JP-1.0 track + ambient play
  on the destination screen after (a) a mirror warp and (b) a long screen
  transition; with it OFF, confirm US-1.0 audio is unchanged.
