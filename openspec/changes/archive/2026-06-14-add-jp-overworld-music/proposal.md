## Why

The two overworld routines that finalize a mirror-warp / long-screen-transition destination — `MirrorWarp_FinalizeAndLoadDestination` (`$82B260`) and `Overworld_FinalizeEntryOntoScreen` (`$82C242`) — select the destination screen's music + ambient sound differently between JP 1.0 and US 1.0. The JP ROM derives the post-warp track from world state + screen index + game progress; US 1.0 re-reads the `overworld_music[]` scratch table. Some players (practice-ROM / JP-faithfulness users) want the JP-1.0 overworld audio.

This was discovered while investigating whether the `mirror-wrap` / `transition-wrapped` glitches differ between ROM versions. A line-by-line JP-vs-US disassembly showed they do **not** for gameplay — the warp/transition destination, Link position, screen index, and camera math are byte-identical JP↔US. The sole JP-vs-US delta in those two routines is this music/ambient selection (the `$7F5B00,x` lookup an earlier pass mistook for a "destination table" is `overworld_music[]`, a WRAM scratch buffer; the differing writes are `$12C`/`$12D`). So there is no gameplay glitch to restore here — only the JP overworld audio, exposed as its own setting.

## What Changes

- **New feature flag** `kFeatures0_JpOverworldMusic` (`features0` bit 19 = `524288`) in `src/features.h`. Default **off**.
- **Gated JP behavior** in both routines (`src/overworld.c`): `if (JpOwMusicEnabled()) { <JP-1.0 music selection> } else { <unchanged US-1.0 code> }`, where `JpOwMusicEnabled() = (enhanced_features0 & kFeatures0_JpOverworldMusic) && !ZeldaIsEmulatorAttached()` (mirrors `player.c`'s `JpGlitchEnabled()`). The else-branch is byte-identical to the prior US code; only the `music_control` (`$12C`) / `sound_effect_ambient` (`$12D`) writes change. Destination, Link position, screen index, and camera math are untouched.
- **One checkbox** "JP 1.0 overworld music" in the native settings window's gameplay-feature panel, persisted as a new `[Features]` boolean key `JpOverworldMusic` (`ParseBoolBit` + aligned `kFeatKeys[]`/`kFeatMasks[]`).
- **No randomizer impact**: it does not touch `RandoSettings`, the canonical serialization, `settings_hash`, `kGeneratorVersion`, or the corpus (default-off `features0` gate; the headless placement path never runs the gated routines).

## Capabilities

### New Capabilities

- `jp-overworld-music`: the JP-1.0 overworld-music selection toggle — the master-flag contract (default-off, byte-identical when off), the faithful-to-JP-disassembly behavior in the two routines, the default-off + side-by-side-off RAM-compare invariant, and the no-randomizer-impact guarantee.

### Modified Capabilities

- `game-config-ui`: ADDED Requirement for the "JP 1.0 overworld music" checkbox — live-applies as a `features0` bit, persists to the INI, excluded on Switch.

## Impact

- **Code**: `src/features.h` (new bit), `src/overworld.c` (gated JP logic + `JpOwMusicEnabled` helper), `src/config.c` (named-key parse + `kFeatKeys`/`kFeatMasks`), `src/rando/rando_window/game_config_panels.cpp` (checkbox). Implemented and merged to main as commit `2dc0439`.
- **Determinism**: default-off ⇒ side-by-side RAM compare stays clean; vanilla play byte-identical unless opted in; corpus byte-identical; no `kGeneratorVersion` bump.
- **Effort**: small, self-contained — the JP music logic is fully grounded in a JP-vs-US disassembly (recorded in this proposal + the commit message).

## Status

**Implemented and merged to local main (`2dc0439`); spec authored as-built.** The JP music logic is disassembly-verified for both routines; the build is 0-warning, `--rando-selftest` passes, and the corpus is byte-identical. In-game audio is **playtest-pending** (a headless build cannot run the game). Do NOT archive until the owner confirms the JP tracks/ambients play correctly after a mirror warp and a long screen transition.
