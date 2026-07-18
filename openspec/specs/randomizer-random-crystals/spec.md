# randomizer-random-crystals Specification

## Purpose
TBD - created by archiving change add-rando-random-crystals. Update Purpose after archive.
## Requirements
### Requirement: Random crystal-count sentinel

The settings axes `crystals_ganon` and `crystals_tower` SHALL accept the requested value `random` (canonical byte value 8, `kCrystalsRandom`) independently per axis, preserved verbatim through canonical serialization, `settings_hash`, share strings, and sidecar persistence — never normalized by derived rules — while parsing rejects values above 8 and pre-feature binaries refuse the sentinel as an unknown future value. CSV SHALL accept the keyword `random` for either axis — and ONLY the keyword: numeric 8 stays refused, preserving the historical 0..7 numeric contract (self-check-pinned); defaults remain fixed 7.

#### Scenario: Sentinel round-trips and hashes distinctly
- **WHEN** a seed is generated with `crystals.ganon=random,crystals.tower=5`
- **THEN** the canonical blob carries bytes [2]=8, [3]=5, the settings_hash differs from every fixed-count combination, and the share string reproduces the sentinel on import

#### Scenario: Fixed counts are byte-identical
- **WHEN** any seed uses only fixed 0..7 values
- **THEN** generation output, settings_hash, and every runtime surface are byte-identical to the pre-feature build

### Requirement: Deterministic seed-derived resolution

A single shared resolver (`Crystals_Resolve(settings, seed_u64, out_ganon, out_tower)`) SHALL map each `random` axis to an effective count uniformly in 0..7, drawn from a dedicated per-axis salted RNG stream (the ganon draw independent of whether tower is random and vice versa), with fixed axes passed through unchanged. Generation and every slot activation SHALL obtain effective counts only through this resolver, and a self-check SHALL pin determinism, range, per-axis independence, and a fixed vector.

#### Scenario: Generation and runtime agree
- **WHEN** a `crystals.ganon=random` seed is generated and later loaded (including snapshot cold-replay)
- **THEN** the spoiler's resolved count, the runtime Ganon-vulnerability threshold, and a fresh resolver call all yield the same value

#### Scenario: Per-axis independence
- **WHEN** two seeds share `seed_u64`, one with only ganon random and one with both axes random
- **THEN** the resolved ganon count is identical in both

### Requirement: Resolved values drive gates, certification, and reveal surfaces

The generator SHALL certify ganon/fast_ganon completability against the RESOLVED ganon count (never the sentinel). At slot activation the runtime SHALL cache the resolved counts and route every consumer through the cached getters: the Ganon's Tower entry gate, the Ganon vulnerability gate, the zero-crystal tower pre-open, the Ganon crystal-warning dialogue (msg 0x16F — the in-world reveal of the rolled ganon requirement), and the auto-tracker settings emission (which reports resolved values). The spoiler SHALL keep the requested bytes and add `crystals_ganon_resolved` / `crystals_tower_resolved` plus a `crystals_salt_version` when either axis is random.

#### Scenario: Completability uses the rolled count
- **WHEN** a `goal=ganon` seed rolls a resolved ganon requirement of N
- **THEN** the generator certifies N reachable crystals (not 8), and the seed generates successfully

#### Scenario: Ganon reveals the rolled count in-world
- **WHEN** the player reaches Ganon below the rolled threshold on a random-ganon seed
- **THEN** msg 0x16F names the resolved count, matching the spoiler and tracker

### Requirement: UI presentation of the sentinel

The native settings window SHALL present each crystal axis as its existing slider extended to 8, rendering the sentinel as "Random" (tooltip: the seed decides the count; Ganon tells you his in-game). The in-game (Switch-path) settings rows SHALL cycle through 8 and render a "RAND" token. Neither surface changes behavior or rendering for fixed 0..7 values.

#### Scenario: Native round-trip
- **WHEN** the user sets Crystals: Ganon to "Random" and generates a slot
- **THEN** the slot's canonical settings carry the sentinel, and reopening the window shows "Random"

