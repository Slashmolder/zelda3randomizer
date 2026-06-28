## ADDED Requirements

### Requirement: Pot-shuffle tier selector and pot tracker presentation

The native settings window (`src/rando/rando_window/`) SHALL expose `pot_shuffle`
as a four-value selector — `Off` / `Keys` / `Contents` / `All` — defaulting to
`Off`, with a tooltip stating the durable player-facing fact (as shipped:
"Turns dungeon pots into randomizer checks. Keys = key pots only; Contents adds
loot pots; All adds the empty pots too."). On PC this is an ImGui
panel control (the in-game SNES settings screen is compiled out on PC per the
project layout; the Switch path uses the in-game screen). The un-checked-pot check
marker (`randomizer-pot-sanity / Un-checked in-scope pots are marked with a gold
check glint`) is a client-side cosmetic that does not affect placement; as built it
draws an **animated gold glint** (a sprite-layer overlay, always on) over
un-checked in-scope pots — there is no per-seed toggle, and the pot's background
tile / floor are not recolored. A 2-state variant (real item vs empty/junk) or a
"mark non-empty pots only" sub-toggle MAY be added later so it stays a useful
signal at the `All` tier (where marking every pot would otherwise read as noise),
but is not required for this change.

Because pot shuffle can add up to ~799 locations, **every location-listing surface**
SHALL present pots so non-pot checks stay legible: the native location tracker AND
reach panel group pots by room and/or gate them behind a "show pots" toggle; the
**SNES HUD location tracker (`src/hud.c`), which loops over all locations, SHALL
hide / page / summarize pots** (an 800+-row flat list is unusable there); and the
auto-tracker export SHALL define explicit pot metadata, with `ITEM_Nothing` pots
counted in the completion denominator. These surfaces also depend on the capacity
audit (`randomizer-placement`) raising their 1024-sized location-id tables
(`s_loc_*[1024]`, `kSpoilerMaxRows`) to the 2048 ceiling, or pots with id ≥ 1024 are
silently dropped from the view.

#### Scenario: Tier selector round-trips through settings
- **WHEN** the player sets `pot_shuffle = Contents` in the native window and
  generates a seed
- **THEN** the generated slot's canonical settings carry the `Contents` value and
  the seed shuffles exactly the contents-tier pots

#### Scenario: Tracker stays legible with pots enabled
- **WHEN** `pot_shuffle = All` and the location tracker is shown
- **THEN** pots are grouped (by room) or gated behind a show-pots toggle so the
  ~328 non-pot checks remain readable, not buried under ~799 pot rows

#### Scenario: SNES HUD location tracker handles pots
- **WHEN** `pot_shuffle = All` and the in-game SNES HUD location tracker
  (`src/hud.c`) is shown
- **THEN** pots are hidden/paged/summarized (not enumerated as 800+ flat rows), and
  no location with id ≥ 1024 is dropped by an un-raised tracker table

#### Scenario: Pot check marker is cosmetic and placement-neutral
- **WHEN** the gold check glint is drawn over un-checked in-scope pots
- **THEN** the seed's `placement_digest` and `settings_hash` are unaffected (the
  marker is client-side OAM + PPU-CGRAM only, like other cosmetic axes)

#### Scenario: Pot selector remains editable under door shuffle
- **WHEN** the native settings window has door shuffle enabled and cave-entrance shuffle
  is not effectively forcing pots off
- **THEN** the `pot_shuffle` selector remains editable and does not show a door-shuffle
  forced-Off note, because door+pot integration is part of the baseline behavior

#### Scenario: Pot selector disabled when cave-entrance shuffle forces pots off
- **WHEN** the native settings window has effective cave-entrance shuffle enabled
- **THEN** the `pot_shuffle` selector is greyed out with a one-line note, using the SAME
  `Settings_PotShuffleForcedOff` predicate generation uses to normalize `pot_shuffle` to
  Off, so the UI is honest about what will actually generate

#### Scenario: Tracker filter toggles persist across restarts
- **WHEN** the player toggles the Check Tracker's "Hide checked", "Only available",
  "Show pots", or "Show items (spoiler)" filters and restarts
- **THEN** all four are restored from `saves/rando_window.ini` (`g_rando_window_prefs`),
  EXCEPT "Show items (spoiler)" is force-off on load for a race seed so a replay cannot
  leak item names

#### Scenario: Spoiler reports cave-forced Off
- **WHEN** effective cave-entrance shuffle forces pots off but `pot_shuffle` was requested
- **THEN** the spoiler emits the EFFECTIVE value (0/Off via `Settings_PotShuffleForcedOff`),
  matching the canonical hash and the actually-generated pot-less seed, not the raw request

#### Scenario: Spoiler keeps requested pot tier under door shuffle
- **WHEN** door shuffle is enabled, cave-entrance shuffle is not effectively forcing pots
  off, and `pot_shuffle` was requested
- **THEN** the spoiler emits the effective requested tier rather than Off, matching the
  canonical hash and the door+pot placement that actually generated
