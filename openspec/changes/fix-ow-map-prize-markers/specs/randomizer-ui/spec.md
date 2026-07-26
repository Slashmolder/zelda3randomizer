# randomizer-ui Specification (delta)

## ADDED Requirements

### Requirement: Overworld pause-map prize markers follow the seed

The overworld pause map's pendant/crystal markers SHALL identify each marker by
the DUNGEON it points at rather than by a fixed prize, so that no marker asserts
a prize the seed did not place there. For the map-icon states that point at
prize dungeons — the three light-world pendant dungeons, the single
"first dark-world dungeon" state, and the seven dark-world crystal dungeons —
each marker SHALL resolve its icon and its crystal number from that dungeon's
placed prize and that dungeon's prize-location checked state, and SHALL NOT
consult the global pendant/crystal ownership masks for any purpose. Marker
positions SHALL stay at their vanilla overworld coordinates. Map-icon states
that point at non-prize destinations (Hyrule Castle, Sahasrahla, the Master
Sword pedestal, Ganon's Tower) SHALL be untouched.

Marker VISIBILITY under a shuffled slot SHALL be unconditional: a prize
dungeon's marker SHALL remain drawn for the whole game, with the icon alone
carrying the collected/uncollected state. Vanilla's hide-on-obtain behavior is
deliberately NOT reproduced, because it is keyed to the prize TYPE and so both
erases markers the player still needs and preserves markers for dungeons they
have cleared.

When no randomizer slot is active, or the active slot has `prize_shuffle`
disabled, the markers SHALL render exactly as vanilla — including vanilla's
prize-type-keyed hide-on-obtain behavior.

#### Scenario: Shuffled prize draws its true icon after collection

- **WHEN** a `prize_shuffle` slot places a crystal at Eastern Palace, the player
  has collected Eastern Palace's prize, and the light-world pause map is shown
  in a map-icon state that marks the pendant dungeons
- **THEN** the Eastern Palace marker draws the crystal icon and that crystal's
  number, not a pendant

#### Scenario: Owning a prize type does not clear another dungeon's marker

- **WHEN** the player owns the green pendant obtained from some other dungeon
  and has not collected Eastern Palace's prize
- **THEN** Eastern Palace still shows a marker

#### Scenario: Vanilla and identity-settings seeds are unchanged

- **WHEN** no slot is active, or the active slot has `prize_shuffle` disabled
- **THEN** every marker's position, icon, blink timing, and hide-on-obtain
  behavior is identical to the pre-change build

#### Scenario: Collected marker persists

- **WHEN** a `prize_shuffle` slot is active and the player has collected a
  dungeon's prize
- **THEN** that dungeon's marker is still drawn, showing the collected prize,
  rather than disappearing as it would in vanilla

#### Scenario: Marker data mapping is pinned to vanilla by an oracle

- **WHEN** the randomizer `ui` self-check group runs
- **THEN** driving the marker resolver with the VANILLA prize assignment
  reproduces, for every prize marker slot in every affected map-icon state,
  exactly the vanilla icon word and crystal-number tile, and the check fails the
  process otherwise

#### Scenario: Oracle is anchored and non-circular

- **WHEN** the self-check runs
- **THEN** it additionally pins each marker slot's dungeon against the vanilla
  prize BIT tables — a source independent of the icon tables, so that a
  permutation applied consistently to both the slot→dungeon and prize→icon
  tables still fails — exercises the resolver's shuffle, observation, missing
  assignment, and out-of-range-prize gates directly rather than re-implementing
  them, and asserts no two prizes render as the same icon-and-number pair
