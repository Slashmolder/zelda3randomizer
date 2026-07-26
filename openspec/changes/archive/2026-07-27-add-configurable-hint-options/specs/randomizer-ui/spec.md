## ADDED Requirements

### Requirement: In-game configurable-hints controls

The in-game/Switch randomizer settings screen SHALL expose one compact
`HINTS <profile>` row. Left/right SHALL cycle Off, Sparse, Balanced, and Direct
using the authoritative complete profile tuples. Activating the row SHALL open
an advanced page with exactly these policy actions: Tiles, Paid, Mix, and Reset
to Balanced.

Tiles SHALL select 0/5/10/15; Paid SHALL select 0/1/2/3 per service; Mix SHALL
select Variety/Important/Difficult/WorldInfo. Editing a component SHALL derive
Custom whenever the tuple matches no named profile. Reset SHALL atomically
restore `(enabled,15,3,Variety)`.

The page SHALL show the same normalized values and paid-zero disclosure as the
native surface and SHALL use the same settings normalize/encode/hash path.
Backing out SHALL preserve pending edits; generating SHALL use their exact
canonical representation. The UI SHALL not create another RNG, policy
interpretation, or platform-specific serialization path.

Race mode SHALL be orthogonal to hint policy. The explicit Race-safe utility
preset MAY restore Balanced only with a visible explanation. F12 behavior is
outside this settings screen and SHALL remain unchanged.

#### Scenario: Compact row cycles named profiles
- **WHEN** left/right moves from Sparse to Balanced
- **THEN** all three pending policy components change to the exact Balanced
  tuple and the live settings hash/share follow the canonical API

#### Scenario: Advanced edit derives Custom
- **WHEN** the player changes Direct paid depth from three to two
- **THEN** the profile label becomes Custom while Important mix and 15-tile
  coverage remain selected

#### Scenario: Reset restores zero-default Balanced
- **WHEN** Reset to Balanced is activated from any custom tuple
- **THEN** all six policy bits return to zero with enabled mode 1

#### Scenario: Back preserves advanced edits
- **WHEN** the player edits Tiles/Paid/Mix and backs out to the main settings
  list
- **THEN** the normalized pending tuple remains visible and is used if the seed
  is generated

#### Scenario: Native and in-game paths serialize identically
- **WHEN** the same profile/components are selected on desktop and in-game
- **THEN** canonical settings, settings hash, share string, and generated hint
  plan identity match byte-for-byte

#### Scenario: Race toggle does not rewrite hints
- **WHEN** pending race mode is toggled without applying Race-safe
- **THEN** the selected hint profile/components do not change
