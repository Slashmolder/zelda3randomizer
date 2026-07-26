# randomizer-ui Specification (delta)

## ADDED Requirements

### Requirement: Overworld pause-map prize markers follow the seed

The overworld pause map's pendant/crystal markers SHALL identify each marker by
the DUNGEON it points at rather than by a fixed prize, so that no marker asserts
a prize the seed did not place there. For the map-icon states that point at
prize dungeons — the three light-world pendant dungeons, the single
"first dark-world dungeon" state, and the seven dark-world crystal dungeons —
each marker SHALL resolve its icon, its crystal number, and its visibility from
that dungeon's placed prize and that dungeon's prize-location checked state, and
SHALL NOT consult the global pendant/crystal ownership masks. Marker positions
SHALL stay at their vanilla overworld coordinates. Map-icon states that point at
non-prize destinations (Hyrule Castle, Sahasrahla, the Master Sword pedestal,
Ganon's Tower) SHALL be untouched.

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

#### Scenario: Marker data mapping is pinned to vanilla by an oracle

- **WHEN** the randomizer `ui` self-check group runs
- **THEN** resolving the VANILLA prize assignment through the re-keyed marker
  path reproduces, for every prize marker slot in every affected map-icon state,
  exactly the vanilla icon word and crystal-number tile, and the check fails the
  process otherwise
