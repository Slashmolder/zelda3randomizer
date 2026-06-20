## ADDED Requirements

### Requirement: Pot-shuffle tier selector and pot tracker presentation

The native settings window (`src/rando/rando_window/`) SHALL expose `pot_shuffle`
as a four-value selector — `Off` / `Keys` / `Contents` / `All` — defaulting to
`Off`, with a tooltip stating the durable player-facing fact (e.g. "Randomize the
items hidden in pots; un-checked pots are recolored"). On PC this is an ImGui
panel control (the in-game SNES settings screen is compiled out on PC per the
project layout; the Switch path uses the in-game screen). The un-checked-pot
recolor (`randomizer-pot-sanity / Un-checked in-scope pots are recolored`) is a
client-side cosmetic that does not affect placement; as built it is **always on**
for un-checked in-scope pots (a single alternate sub-palette applied in
`RoomDraw_SinglePot` — there is no per-seed toggle). A 2-state tint (real item vs
empty/junk) or a "recolor non-empty pots only" sub-toggle MAY be added later so it
stays a useful signal at the `All` tier (where recoloring every pot would otherwise
read as noise), but is not required for this change.

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

#### Scenario: Recolor is cosmetic and placement-neutral
- **WHEN** the recolor cosmetic is toggled
- **THEN** the seed's `placement_digest` and `settings_hash` are unaffected (the
  recolor is client-side, like other cosmetic axes)
