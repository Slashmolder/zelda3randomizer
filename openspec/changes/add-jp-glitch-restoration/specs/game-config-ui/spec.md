## ADDED Requirements

### Requirement: JP-1.0 glitch toggle in the gameplay-feature panel

The native settings window's gameplay-feature panel SHALL expose a "Restore JP 1.0 glitches" checkbox bound to the `kFeatures0_RestoreJpGlitches` bit of `features0`. The checkbox SHALL follow the existing gameplay-feature toggle conventions: it persists as a **new named boolean key under `[Features]`** (each `features0` bit is its own key, parsed via `ParseBoolBit` and round-tripped through the aligned `kFeatKeys[]`/`kFeatMasks[]` tables — there is no `[Features] features0` integer mask), written via the comment-preserving in-place writer; it live-applies as a `features0` bit (no restart) and is compiled only on PC (`Z3R_NATIVE_SETTINGS_WINDOW`) with the rest of the game-config panels. Its tooltip SHALL be a single durable player-fact (e.g. "Re-enables glitches removed in the US 1.0 release"), with no status or implementation detail.

#### Scenario: Toggle persists and live-applies
- **WHEN** the player checks "Restore JP 1.0 glitches" and applies
- **THEN** the `kFeatures0_RestoreJpGlitches` bit is set in the running game without a restart and the new `[Features]` boolean key is written to the loaded INI, preserving all other bytes of the file

#### Scenario: Excluded on Switch
- **WHEN** the project is built with `Z3R_NATIVE_SETTINGS_WINDOW` undefined
- **THEN** the checkbox is excluded with the other PC-only panels and the build succeeds (the bit still parses from `zelda3.ini` on Switch)
