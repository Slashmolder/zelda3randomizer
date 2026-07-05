# randomizer-dungeon-chains — delta for dungeon-chains

## ADDED Requirements

### Requirement: Chain pool, partition, and pinned adjacencies

When `dungeon_chains` is active, the generator SHALL partition the chain pool —
the 9 pool dungeons (Eastern Palace, Desert Palace, Tower of Hera, Palace of
Darkness, Swamp Palace, Thieves' Town, Ice Palace, Misery Mire, Turtle Rock) —
into 9 ordered, possibly empty, chains, one per pool dungeon's MAIN overworld
door, and SHALL assign each chain a terminal boss such that the 9 pool dungeons'
vanilla bosses form a bijection with the chains. Every pool dungeon and every
pool boss SHALL appear exactly once across all chains. Hyrule Castle, Castle
Tower, Ganon's Tower, Ganon, Skull Woods (including Mothula and all SW overworld
doors), and all non-main (auxiliary) overworld doors SHALL remain vanilla. Two
adjacencies SHALL be pinned: Thieves' Town must be the last dungeon of its chain
with Blind as its terminal, and Tower of Hera must be the last dungeon of its
chain with Moldorm as its terminal.

#### Scenario: Partition validity

- **WHEN** a seed is generated with `dungeon_chains` on
- **THEN** each of the 9 pool dungeons appears in exactly one chain position and
  each of the 9 pool bosses terminates exactly one chain, and the self-check
  (`--rando-selftest`) fails the build if either count or the pinned adjacencies
  are violated

#### Scenario: Degenerate single-chain seed is legal

- **WHEN** the construction yields 8 empty chains and one chain containing all 9
  dungeons
- **THEN** the seed is accepted, with 8 doors leading directly to boss terminals
  and 1 door chaining all 9 dungeons

#### Scenario: Excluded content stays vanilla

- **WHEN** `dungeon_chains` is on
- **THEN** Hyrule Castle, Castle Tower, Ganon's Tower, their bosses, Skull Woods
  (all its overworld doors, interior, and Mothula), and every auxiliary
  overworld door (Desert Palace west/east/back, Turtle Rock contained doors)
  behave exactly as they would with `dungeon_chains` off

### Requirement: Deterministic chain construction

Chain construction SHALL be a pure function of (seed, attempt) using the
randomizer RNG, producing identical chains and an identical chain digest across
platforms and builds at the same `kGeneratorVersion`.

#### Scenario: Reproducibility

- **WHEN** the same seed and attempt are used on two platforms (MSVC and gcc)
- **THEN** the resolved chains and the chain digest are identical

### Requirement: Chain traversal uses full entrance-style loads

Every chain arrival SHALL load the next chain element via a full entrance load.
A chain arrival is entering a chain-start overworld door, or crossing a
dungeon's vanilla boss seam (the door, staircase, or hole transition whose
vanilla destination is that dungeon's boss room). A full entrance load
reinitializes palettes, tilesets, music, sprites, room bounds, and the palace
index; a raw door-transition redirect SHALL NOT be used across chain elements.
Boss terminals SHALL load via fork-added synthetic entrance records
whose palace index, music, and environment are the boss's home dungeon's, so the
boss fights in its vanilla home room. Door-based synthetic boss entrances SHALL
land inside the room, clear of the south shutter-door collision tile, rather
than at the raw doorway threshold. A pinned adjacency's boss seam SHALL take the
unmodified vanilla transition. Hop loads SHALL preserve the overworld-return
state captured at the chain's origin door (no re-capture from in-dungeon state).

#### Scenario: Chain-start door leads to first element

- **WHEN** the player enters a chain-start door whose chain begins with a dungeon
- **THEN** that dungeon's lobby loads with its own palace index, music, and
  palettes, exactly as if entered from its own vanilla door

#### Scenario: Boss seam leads to successor

- **WHEN** the player crosses dungeon D's vanilla boss seam and D's chain
  successor is another dungeon
- **THEN** the successor's lobby loads via a full entrance load, and D's boss
  room is not entered

#### Scenario: Boss terminal fights at home

- **WHEN** a chain terminal is reached (from a door or a boss seam)
- **THEN** the terminal boss's vanilla home room loads with home-dungeon palace
  index and environment, the player lands inside the room rather than embedded in
  the south shutter door, and the boss fight triggers as in vanilla

#### Scenario: Pinned seam is vanilla

- **WHEN** the player crosses Thieves' Town's or Tower of Hera's boss seam
- **THEN** the vanilla transition into Blind's or Moldorm's room occurs unchanged

### Requirement: Terminal boss-room containment

Outbound transitions from a terminal boss room SHALL divert to the chain exit
after the terminal prize location is checked: every door, stair, and hole
transition out of a cleared boss room reached as a chain terminal returns the
player to the origin door instead of entering the boss's home dungeon. Before
the terminal reward is collected, boss-room outbound transitions SHALL stay
vanilla so boss-specific retry loops remain intact. A cleared terminal boss room
revisited through its chain SHALL always be exitable.

#### Scenario: No walk-back into the home dungeon after reward

- **WHEN** the player attempts to leave a terminal boss room through any room
  transition after the terminal prize location is checked
- **THEN** they emerge at the chain's origin overworld door, and the boss's home
  dungeon interior is not entered

#### Scenario: Pre-reward retry remains vanilla

- **WHEN** Moldorm is the chain terminal and the player falls out before the
  Tower of Hera prize location is checked
- **THEN** the fall uses the vanilla Tower of Hera retry destination rather than
  consuming the chain's origin exit

#### Scenario: Cleared terminal is not a sealed box

- **WHEN** the player re-enters an already-cleared terminal boss room via its
  chain
- **THEN** they can leave through a room transition (diverted to the origin
  door) regardless of shutter state

### Requirement: Main-door chain exits return to the origin door

Chain-session exits through a dungeon's MAIN overworld door SHALL place the
player at the chain's origin door, and the post-boss pendant/crystal warp and
terminal-containment diverts SHALL resolve to the origin door the same way.
Exits through auxiliary doors (Desert Palace's ledge side doors, Turtle Rock's
balcony doors) SHALL resolve vanilla and SHALL NOT consume the chain session's
origin coupling — the coupling survives an auxiliary round-trip and is consumed
only when an origin substitution fires (or overwritten by the next chain-start
entry). Death-continue SHALL respawn at the current chain element's entrance.
Entries through auxiliary doors SHALL NOT arm chain coupling; with no coupling
armed, all exits resolve vanilla.

#### Scenario: Post-boss warp cannot strand

- **WHEN** the player defeats a chain-terminal boss whose home dungeon is
  Misery Mire and collects the prize
- **THEN** the warp-out places them at the chain's origin door, not at the Mire
  overworld pad

#### Scenario: Mid-chain walkout

- **WHEN** the player walks out the main door of the third dungeon in a chain
- **THEN** they emerge at the chain's origin overworld door

#### Scenario: Death mid-chain

- **WHEN** the player dies in a chained dungeon and selects continue-from-entrance
- **THEN** they respawn at that dungeon's lobby (the current hop), and the
  chain's origin-door return state is preserved

#### Scenario: Auxiliary exits preserve vanilla routes

- **WHEN** the player, mid-chain inside Desert Palace or Turtle Rock, exits
  through a ledge side door or balcony door
- **THEN** they emerge on the vanilla ledge/balcony (enabling the DP back-door
  boss approach and the TR Mimic Cave route) and can re-enter through the same
  door into the same interior

#### Scenario: Auxiliary round-trip keeps the chain coupled

- **WHEN** the player mid-chain exits Desert Palace's side door, re-enters via
  the back door, and later exits through a main door or defeats the chain's
  terminal boss
- **THEN** that exit still resolves to the chain's origin door

#### Scenario: Auxiliary entry without a session is fully vanilla

- **WHEN** the player enters Desert Palace via its back lobby door with no chain
  session armed and exits
- **THEN** they emerge at that same back-lobby door as in vanilla

### Requirement: Chain-aware reachability logic

The logic graph SHALL model each pool dungeon's boss room as a distinct region
whose single inbound edge carries the dungeon's derived boss-approach predicate
(the Boss location predicate with its `CanKillBoss` term stripped), with the Boss
and Prize locations homed to that region gated on `CanKillBoss`. With
`dungeon_chains` off this restructure SHALL be placement-identical to the
previous model. With chains on, per-seed edge overrides SHALL retarget each
dungeon's boss edge to its chain successor and each chain's terminal boss-room
region to the chain's last element (or its start door for empty chains), so that
reaching chain element i+1 requires reaching element i's region and satisfying
its boss-approach predicate, and a terminal's Boss/Prize locations additionally
require the terminal boss's kill predicate. Generation SHALL accept only seeds
whose every location is reachable (full-reachability gate), retrying with a new
attempt otherwise.

#### Scenario: Off is placement-identical

- **WHEN** the regression corpus is regenerated with the boss-room region
  restructure in place and `dungeon_chains` off everywhere
- **THEN** every corpus placement digest is byte-identical to the pre-restructure
  baseline

#### Scenario: Successor gated on predecessor's boss approach

- **WHEN** chain element i is Ice Palace and element i+1 is Eastern Palace
- **THEN** Eastern Palace's locations are unreachable in logic until the Ice
  Palace boss-approach predicate (path, small keys, big key — without the kill
  predicate) is satisfiable

#### Scenario: Terminal prize requires the kill

- **WHEN** a chain terminates in Kholdstare
- **THEN** the Ice Palace Boss and Prize locations require reaching the chain's
  end AND Kholdstare's kill predicate

#### Scenario: Dungeon items cannot be placed past their own boss seam

- **WHEN** a chain is [Eastern Palace, Tower of Hera] with Moldorm as its
  terminal and wild dungeon keys are enabled
- **THEN** Eastern Palace's big key is never placed in Tower of Hera or the
  terminal boss location, because those locations are reachable only through the
  Eastern Palace boss-approach predicate, which requires that key

#### Scenario: Unreachable seed is refused

- **WHEN** an attempt's chains yield any unreachable location under the goal
  settings
- **THEN** the generator retries with the next attempt rather than shipping the
  seed

### Requirement: Persistence and regeneration

The sidecar slot SHALL persist the chains activation, attempt, and a 24-bit chain
digest in a new additive extension block; slot activation SHALL regenerate the
chains from (seed, attempt) and SHALL refuse the slot on digest mismatch.
Existing sidecar files without the extension SHALL load unchanged.

#### Scenario: Reload reproduces the chains

- **WHEN** a chains slot is saved and the game is restarted
- **THEN** slot activation regenerates identical chains and the run continues

#### Scenario: Digest mismatch fails closed

- **WHEN** the regenerated chain digest differs from the persisted digest
- **THEN** the slot is refused rather than activated with wrong redirects

### Requirement: Synthetic entrance records fail closed

Slot activation with chains SHALL verify the fork-added boss-room entrance
records are present and well-formed, refusing activation otherwise (no silent
fall-back to vanilla entrance data).

#### Scenario: Missing records refuse activation

- **WHEN** the assets file lacks the synthetic boss-room entrance records
- **THEN** chains slot activation fails with a diagnosable error instead of
  loading vanilla entrances

### Requirement: Spoiler and UI surface

The spoiler SHALL include a chains section (JSON and text) listing, per
chain-start door, the ordered dungeons and the terminal boss. The native settings
window SHALL expose a single `dungeon_chains` toggle that reflects normalization
(disabled/off when excluded settings are active).

#### Scenario: Spoiler lists chains

- **WHEN** a chains seed writes its spoiler
- **THEN** each of the 9 chain-start doors appears with its ordered dungeon list
  and terminal boss
