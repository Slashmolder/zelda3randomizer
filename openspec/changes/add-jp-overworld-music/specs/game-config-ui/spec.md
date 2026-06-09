## ADDED Requirements

### Requirement: JP overworld music checkbox

The native settings window's gameplay-feature panel SHALL expose a "JP 1.0 overworld music" checkbox bound to the `kFeatures0_JpOverworldMusic` bit of `features0`, following the existing gameplay-feature toggle conventions: it persists as a **new named boolean key under `[Features]`** (`JpOverworldMusic`, parsed via `ParseBoolBit` and round-tripped through the aligned `kFeatKeys[]`/`kFeatMasks[]` tables — there is no `[Features] features0` integer mask), written via the comment-preserving in-place writer; it live-applies as a `features0` bit (no restart) and is compiled only on PC (`Z3R_NATIVE_SETTINGS_WINDOW`) with the rest of the game-config panels. Its tooltip SHALL be a single durable player-fact (e.g. "Use the Japanese 1.0 overworld track/ambient after mirror warps and screen transitions"), with no status or implementation detail.

#### Scenario: Toggle live-applies and persists
- **WHEN** the player checks "JP 1.0 overworld music" and applies
- **THEN** the `kFeatures0_JpOverworldMusic` bit is set in the running game without a restart and the new `JpOverworldMusic` boolean key is written to the loaded INI, preserving all other bytes of the file

#### Scenario: Excluded on Switch
- **WHEN** the project is built for the Switch target (no `Z3R_NATIVE_SETTINGS_WINDOW`)
- **THEN** the checkbox is compiled out with the rest of the PC game-config panels; the `JpOverworldMusic` INI key still parses
