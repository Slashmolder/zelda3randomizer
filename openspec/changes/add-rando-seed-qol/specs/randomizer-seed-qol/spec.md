## ADDED Requirements

### Requirement: Seed QoL features are placement-neutral, replay-safe, and RAM-compare-clean

Every feature in this bundle SHALL be implemented as a runtime feature bit
(`kFeatures0_*`, in the free bit-20..31 range), a local `zelda3.ini`
keybind/value, or client-side rendering — and SHALL NOT change item placement,
the settings hash, the canonical settings length, the on-disk save format, or
`kGeneratorVersion`. Each new per-slot preference bit SHALL be added to
`kFeatures0_RandoSeedQolMask` so a shared seed can recommend it via the existing
`recommended_features0` slot field (format_version ≥ 3) without pinning it. Like
the existing Seed QoL bits, each behavior SHALL be no-op or suppressed under
side-by-side emulation (`ZeldaIsEmulatorAttached()`) so the per-frame RAM/SRAM/VRAM
comparator stays clean, and rando-only behaviors SHALL additionally gate on
`(enhanced_features1 & kFeatures1_RandomizerActive)`.

#### Scenario: Corpus and generator version are unaffected
- **WHEN** the full bundle is implemented and the regression corpus is regenerated
- **THEN** every existing seed's `placement_digest`, `sphere_digest`, and
  `settings_hash` are byte-identical, `kGeneratorVersion` is unchanged, and
  `--rando-selftest` passes without a new subsystem check

#### Scenario: Disabled features are byte-identical to vanilla behavior
- **WHEN** every new Seed QoL bit is off (the default global state) and no new
  keybind is pressed
- **THEN** the game behaves exactly as before this change on both the vanilla and
  randomizer paths, and a side-by-side run against the original ROM produces no new
  RAM/SRAM/VRAM divergence

#### Scenario: Per-slot preferences ride the existing recommended-features field
- **WHEN** a slot is generated with a per-slot Seed QoL bit recommended on
- **THEN** the bit is stored in `recommended_features0` masked by
  `kFeatures0_RandoSeedQolMask` with no save-format-version change, and loading the
  slot on a build that supports the bit applies it as a recommendation the player
  can override

### Requirement: Dungeon check-info on the pause map

Under an active randomizer slot, the pause dungeon map SHALL display, per dungeon,
how many of that dungeon's randomizer checks remain versus its total (a
`remaining` or `checked/total` readout), computed by a new cached accessor that
iterates the location registry filtered by dungeon over the existing
`Rando_IsLocationChecked` state (the trackers derive reachability, not a per-dungeon
remaining count) — not a separate placement read. This is rando-only, defaults on,
and draws no item names (counts are not a spoiler). A second phase MAY additionally mark the located remaining checks with
dots on the dungeon map. The render SHALL be client-side (HUD/OAM/PPU) and MUST
NOT affect `placement_digest` or `settings_hash`.

#### Scenario: Counts reflect collected checks
- **WHEN** a dungeon has 3 in-logic checks and the player has collected 2, and the
  pause dungeon map for that dungeon is shown
- **THEN** the map shows that dungeon's remaining count as 1 (or `2/3`), updating as
  further checks in that dungeon are collected

#### Scenario: Counts are not a spoiler and are safe under race mode
- **WHEN** the active slot is a race-mode seed and the dungeon check-info is shown
- **THEN** only counts (not item names) are displayed, so no placement information
  is leaked

#### Scenario: Disabled or non-rando play shows nothing new
- **WHEN** no randomizer slot is active, or the feature bit is off
- **THEN** the pause map renders exactly as vanilla with no counts or dots

### Requirement: Message and fanfare speed is configurable and hint-safe

The game SHALL expose a configurable message text speed (at least
`normal` / `fast` / `instant`) plus a fast-advance for the item-pickup fanfare and
the recurring dungeon small-key / map / compass "get" holds. Text speed SHALL be a
speed *level*, never a plain on/off skip, and the fastest level SHALL remain
**hint-safe**: hint / telepathy / sign text the player is expected to read MUST NOT
auto-advance past the player, so an instant setting still requires an input to
dismiss readable text. The instant level SHALL speed character draw, embedded waits,
initial/post-page input latches, and text-box scroll commands; it SHALL NOT auto-close
generic readable text. Because text speed is a global (non-rando) setting, its
draw-cadence override SHALL be suppressed under side-by-side emulation
(`ZeldaIsEmulatorAttached()`) on both the vanilla and randomizer paths so the RAM/timing
comparator stays clean.

#### Scenario: Instant text speeds dialogue draw
- **WHEN** text speed is `instant` and an NPC dialogue box opens
- **THEN** the box's characters render without per-character delay, page-scroll
  commands complete without visible dead time, and the player advances/closes generic
  text with input

#### Scenario: Hint text stays readable at the fastest setting
- **WHEN** text speed is `instant` and the player reads a hint tile / telepathy /
  rando hint
- **THEN** the hint text does not auto-advance or auto-close before the player can
  read it — dismissal still requires an input

#### Scenario: Item and dungeon-item gets advance faster
- **WHEN** the fast-fanfare option is on and the player collects an item, small key,
  map, or compass
- **THEN** the "you got the X!" hold / key jingle pause is shortened or
  auto-advanced, granting the item with identical effect to vanilla

### Requirement: Cutscene and transition fast-forward preserves all flags

The game SHALL provide options to skip or auto-advance recurring animated
sequences — the crystal/pendant prize-get, the Ganon's-Tower crystal barrier, the
pyramid-opening, the post-Agahnim defeat transition, the Zelda escort dialogue, and the death /
game-over fade — and to speed (not re-time the destination of) the mirror-warp and
flute-travel animations. Each fast-path SHALL preserve **every** progression flag,
SRAM byte, and side effect the vanilla sequence performs (skip the *animation*,
never the *flag*): at a common settled checkpoint, a fast-forwarded sequence MUST
leave the live save block, serialized SRAM, randomizer checked bitmap, progression
flags, and destination/player state identical to the full sequence. Frame counters,
the feature bit, animation/audio timers, and render scratch are excluded from that
comparison. The overworld/dungeon **screen-scroll** transition
timing SHALL NOT be changed by this feature. Story-dialogue fast-forward SHALL be
allowlisted to known Standard intro / Uncle / Zelda escort / Sanctuary / post-Agahnim
messages and SHALL NOT auto-select choices or apply to randomizer hint-tile messages.

#### Scenario: Prize-get fast-path sets the prize identically
- **WHEN** the prize-get fast-forward is on and the player earns a dungeon
  crystal/pendant
- **THEN** the crystal/pendant is recorded in SRAM exactly as the full animation
  would record it, and any dependent progression (e.g. barrier/access state) is set
  identically — only the animation is shortened

#### Scenario: Flag-setting story cutscenes keep their flags
- **WHEN** the post-Agahnim defeat transition or Zelda escort fast-path runs
- **THEN** every progression bit the vanilla sequence sets is set, so downstream
  triggers behave identically to an un-skipped playthrough

#### Scenario: Allowlisted story text auto-advances without choosing
- **WHEN** cutscene fast-forward is on and an allowlisted Standard story dialogue
  reaches a page break
- **THEN** the page advances without requiring a button press, but choice prompts
  still wait for player input

#### Scenario: Mirror/flute speed leaves the destination unchanged
- **WHEN** the mirror-warp or flute-travel animation is sped up
- **THEN** the destination screen, Link position, and camera are byte-identical to
  vanilla — only the animation duration changes

#### Scenario: Screen-scroll timing is untouched
- **WHEN** any cutscene fast-forward option is on
- **THEN** overworld and dungeon screen-scroll transition timing is unchanged from
  vanilla

### Requirement: Quick reset / warp-to-spawn with a race toggle

The game SHALL provide a "warp to spawn" action that returns the player to the
active slot's start point without the Save-and-Quit → file-select round-trip. The
destination SHALL be the slot's start point
(`which_starting_point` via the `kStartingPoint_*[]` load), the same spawn a fresh load
uses. The warp SHALL **reuse the existing Save-and-Quit spawn path** rather than a
re-derived branch, leaving every progression flag that path sets/clears in its post-S&Q
state so a subsequent transition is not corrupted by a stale flag. Because an instant
reposition is a routing tool that a race ruleset may forbid, the feature SHALL be cleanly
toggleable (off = the vanilla Save-and-Quit behavior).

#### Scenario: Warp returns to the slot start point
- **WHEN** warp-to-spawn is enabled and the player triggers it mid-run
- **THEN** the player is returned to the active slot's start entrance without
  visiting the file-select screen, with save state consistent with a Save-and-Quit

#### Scenario: Disabled leaves Save-and-Quit vanilla
- **WHEN** the warp-to-spawn feature is off (e.g. a race ruleset bans it)
- **THEN** Save-and-Quit behaves exactly as vanilla and no warp hotkey is active

### Requirement: Auto / hold-to-dash option

The game SHALL provide an option to trigger the Pegasus Boots dash without the
vanilla charge-up delay (dash on hold / auto-Pegasus). When off, boots behavior is
vanilla. The option SHALL compose with the existing `kFeatures0_TurnWhileDashing`
without conflict.

#### Scenario: Hold-to-dash removes the charge delay
- **WHEN** auto-dash is on and the player holds the run input while wearing boots
- **THEN** the dash begins without the vanilla charge-up wait

#### Scenario: Disabled dash is vanilla
- **WHEN** auto-dash is off
- **THEN** the Pegasus Boots charge and dash behave exactly as vanilla

### Requirement: Entrance/door connection feed to the auto-tracker

The auto-tracker export SHALL emit discovered entrance-shuffle and door-shuffle
connections over the existing tracker protocol as the player traverses them, so an
external whole-graph tracker can render them — this change adds NO in-game
connection overlay. Emission SHALL be observation-only (no gameplay effect) and
carry no data under a non-shuffled seed.

#### Scenario: Traversed entrance is emitted
- **WHEN** entrance shuffle is active and the player walks through a shuffled
  entrance for the first time
- **THEN** the auto-tracker feed reports that entrance's discovered destination in
  its export format, and no in-game overlay is drawn

#### Scenario: No connection data without a shuffle
- **WHEN** the active slot has neither entrance nor door shuffle
- **THEN** the auto-tracker emits no connection entries
