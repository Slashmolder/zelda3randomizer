# randomizer-shuffles

## ADDED Requirements

### Requirement: The cave-entrance pool's identity unit is the overworld door row

A cave-entrance pool entry SHALL correspond to a group of overworld **door
rows**, not to an interior room, and the source-side lookup that maps a door to
its pool entry SHALL be keyed by the door row rather than by the door's vanilla
entrance id.

Several pool entries MAY therefore share a room and an entrance id, because they
are the same physical interior reached through different overworld doors. Four
Dark World shop doors load room `0x10F` through entrance id `0x60`, and two load
room `0x112` through `0x58`; keyed by entrance id they are indistinguishable and
move through the shuffle as one unit, which is what prevented shops from
composing with cave-entrance shuffle.

The entrance-id keyed lookup SHALL be retained for the fall-hole path alone,
which has no door row available because a fall writes `which_entrance` directly.
Its soundness SHALL be enforced rather than assumed: no entrance id claimed by
more than one pool entry may lie in the fall-hole range `0x76`-`0x81`.

Pool entries SHALL be **appended** when the pool grows, never inserted, because
two independent structures are positionally keyed to the entry index: the
generated cave source predicates (`kRandoCaveSourcePreds`, emitted in
`interiors:` order by a different generator than the interior table) and
hard-coded interior indices in the entrance self-check.

The baked cave-arrival table (`cave_arrival_baked.h`) SHALL be keyed by
`interior_id` rather than by position. It was positional, and the two interior
splits that predate this change shifted every later row onto the wrong door —
eight interiors silently carried another cave's overworld arrival in decoupled
mode, which never crashes because a wrong arrival is still a valid overworld
position. A row naming an interior that does not exist SHALL fail the self-check
rather than degrade silently, and an interior with no row SHALL load invalid,
which the runtime already degrades to the coupled exit.

Location lists SHALL be disjoint across entries: the per-location region-override
store is last-write-wins and the location-to-interior lookup is location-keyed,
so a location appearing in two entries would resolve non-deterministically.

A door SHALL be reported as shuffled to the auto-tracker on the basis of its
pool **entry** rather than its entrance id, since a permutation mapping one
entry onto a room-sibling leaves the door table byte unchanged while genuinely
moving the door.

Any consumer that needs the pool entry a door now **loads** — as opposed to the
entry the door **belongs to** — SHALL resolve it through the installed entry
permutation (source endpoint, then assignment, then destination entry), never
through the loaded entrance id. The id names only the physical room, so among
room-siblings it identifies the wrong entry. This binds the destination-side
player-knowledge marking in particular: marking a sibling would credit the
player with having discovered contents they have not seen, which is exactly what
the player-knowledge invariant forbids.

Under cross-category shuffle the permutation runs over a **combined** cave and
dungeon endpoint space, so the same resolution SHALL accept a dungeon door as
the source of a cave destination, and SHALL yield "no cave" when a cave door's
image is a dungeon.

#### Scenario: Doors sharing an entrance id shuffle independently
- **WHEN** a cave-entrance-shuffled seed is generated and two overworld doors
  carry the same vanilla entrance id but belong to different pool entries
- **THEN** each door is redirected according to its own entry's assignment, and
  the two doors may lead to different destinations

#### Scenario: Coupled exits still return to the entered door
- **WHEN** the player enters an interior room through any of the several doors
  that load it and then leaves, with coupled cave-entrance shuffle active
- **THEN** the player emerges at the door they entered, because the exit replays
  the overworld arrival cached at entry rather than a per-entry table

#### Scenario: A door whose destination moved within its room reports as shuffled
- **WHEN** the permutation maps one pool entry of a multi-door room onto another
  entry of the same room, leaving the door table's entrance id unchanged
- **THEN** the auto-tracker still reports that door as a discovered connection
  once the player has walked through it

#### Scenario: Walking through a door reveals only what is behind it
- **WHEN** the player walks through a door whose destination pool entry shares
  its room and entrance id with other entries that carry member locations
- **THEN** only the entry the permutation actually put behind that door is
  marked discovered, and the room-siblings' locations stay outside the
  knowledge-limited view

#### Scenario: Growing the pool does not change any persisted format
- **WHEN** the cave pool's entry count grows
- **THEN** no sidecar field, TLV or share-string width changes, because the
  assignment is regenerated from the seed, axes and attempt rather than stored;
  pre-existing entrance-shuffle slots fail the entrance digest drift check,
  which the accompanying `generator_version` bump accounts for
