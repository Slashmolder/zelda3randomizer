# randomizer-shopsanity

## MODIFIED Requirements

### Requirement: Shopsanity composition rules

The axis SHALL compose without derived-rule normalization against every other
axis, including cave-entrance shuffle: door shuffle, dungeon chains,
pot/terrain/enemy-check families, souls (no shopkeeper is soul-gated), and
boss/drop/enemy shuffles. Shop slots SHALL be excluded from the trap-masquerade
eligible set. Customizer manifests SHALL be able to pin shop slots when the axis
is on and SHALL refuse shop-slot pins when it is off. The spoiler SHALL emit
shop rows with their prices (JSON `shops[]` carries `"price"`; the text `Shops:`
section prints the price per slot) and SHALL emit the effective `shopsanity`
value in its settings echo, in every world state where shop-class placements
exist, leaving axis-off non-Retro spoilers byte-identical.

Eight of the nine shops live in cave interiors that are members of the
cave-entrance shuffle pool. Each such shop SHALL occupy its **own** pool entry,
keyed by its overworld door row, and SHALL carry its three slot ids as that
entry's locations, so that `Entrance_ApplyRegionOverrides` rebinds those slots to
the region of whichever door now reaches them. The ninth shop (Light World Death
Mountain, room `0x0FF`) is not a cave interior and never shuffles.

That binding SHALL be enforced rather than assumed: an entry that names a shop
without listing the shop's three slot ids SHALL fail the build, both in the
table generator and in the runtime self-check, because such an entry would hand
the player a slot whose logic region was never rebound. The rebinding itself
SHALL be asserted directly against the override store, since spoilers report
each location's static logic region for every location type and therefore
cannot distinguish a rebound slot from an unbound one.

The earlier normalization that forced the axis off under cave-entrance shuffle
SHALL be removed, together with its predicate; a single predicate SHALL remain
as the consumer-facing test for whether shop slots are live fill locations.

#### Scenario: Cave-entrance shuffle forces the axis off
NOTE: this scenario's TITLE is obsolete and retained only because the archiver
cannot express a scenario rename inside a MODIFIED requirement — it reads a
missing title as an accidental drop and refuses. The canonical spec's copy is
renamed to "The axes compose" in the same change; the assertion below is
authoritative.
- **WHEN** a seed requests both `shopsanity=true` and
  `shuffle_cave_entrances=true` in a world state where cave-entrance shuffle is
  effective
- **THEN** the seed generates with all 27 shop slots as fill locations, the
  canonical settings preserve `shopsanity=true`, and each shop slot's logic
  region is the region of the overworld door that reaches it in that seed

#### Scenario: No derived-rule coupling
- **WHEN** `shopsanity=true` is combined with any supported axis combination,
  effective cave-entrance shuffle included
- **THEN** canonical serialization preserves `shopsanity=true` (no
  normalization), and generation succeeds or refuses on the other axes' existing
  rules only

#### Scenario: Spoiler carries prices
- **WHEN** a `shopsanity=true` seed writes its spoiler
- **THEN** every shop slot row includes the derived price, and identity-placed
  Capacity Upgrade rows remain flagged without a derived price

#### Scenario: Customizer pin respects the axis
- **WHEN** a customizer manifest pins location 240 (`Dark World Forest Shop - 0`)
  with `shopsanity=true`
- **THEN** the pin is honored like any open location; the same manifest with
  `shopsanity=false` is refused with a clear error

### Requirement: Shop identity is keyed by the entered overworld door

A shop SHALL be identified at runtime by the pair (interior room, entered
overworld door id), where the door id is the overworld door row index plus one —
the same quantity ALTTPR stores in `PreviousOverworldDoor`. The engine's
`which_entrance` SHALL NOT be used as the shop key: four shops share entrance id
`0x60` and two share `0x58`, so it cannot separate them.

The room SHALL be compared as the full 16-bit room index, as the ROM does; the
low byte alone aliases ordinary rooms onto interior rooms (room `0x010` onto
`0x110`).

For the eight shops that are cave interiors, the live door SHALL be **derived
from the entrance layout** rather than read from a static column: the shop is the
destination pool entry, so its doors are the door rows of the source entry the
permutation assigned to it. With cave-entrance shuffle inactive the assignment is
the identity and this SHALL reduce to the vanilla door column. This mirrors
ALTTPR, whose ROM writer re-derives each shop's door byte per seed from the
post-shuffle connection.

A source entry MAY own several door rows, in which case all of them reach that
entry's shop — they are one pool slot, so this is the same shop, not an alias.
Under cross-category shuffle the source MAY be a **dungeon** door, which the
resolution SHALL accept (dungeon entrance ids are unique, so a dungeon source
resolves by id where a cave source resolves by row); otherwise a shop landing
behind a dungeon door would be modelled as reachable by the placer and be
unobtainable at runtime.

The room comparison SHALL be against the room of the **destination entry** the
door resolves to, so that a captured door id which does not belong to the room
the player is standing in — a mirror warp, a fall-hole, a stale byte — yields no
shop rather than naming the shop that door's destination happens to host.

Only the one shop ALTTPR declares with no door (Light World Death Mountain,
`config = 0x43`) SHALL match on the room alone, mirroring its
`shop_config & 0x40` skip-door-check bit. Every other shop SHALL stay door-keyed
even when its room hosts a single shop, so that a room loaded by more than one
overworld door keeps vanilla shop behavior at the door upstream does not declare.

The door id SHALL be captured at the overworld entry hook and SHALL persist in
the reserved `g_ram` compat block, so that a snapshot replay-restore — which
restores `g_ram` but not C statics — keeps the active shop resolvable.

#### Scenario: Every shop slot is reachable from exactly one door
NOTE: this scenario's TITLE is obsolete — "exactly one door" was true only while
each shop had exactly one, which the pool split ended (a source entry may own
several door rows, and they are one pool slot reaching one shop). Retained only
because the archiver cannot express a scenario rename inside a MODIFIED
requirement. The canonical spec's copy is renamed to "Every shop slot is
reachable from exactly one room" in the same change; the assertion below is
authoritative.
- **WHEN** the shop resolver is walked against every row of the overworld
  entrance table, both with no entrance shuffle and against generated
  cave-entrance-shuffled layouts including decoupled and cross-category ones
- **THEN** each of the 27 shop slots resolves from at least one overworld door,
  no slot resolves from more than one room, and no single door's three columns
  resolve to different shops

#### Scenario: The door-less shop resolves from its room alone
- **WHEN** the player enters the Light World Death Mountain shop, which upstream
  declares with `config = 0x43` and `door_id = 0x00`
- **THEN** its three slots resolve on the room match alone, without consulting
  the door id — it is also the one shop that is not a cave interior, so it never
  shuffles and keeps the static table row

#### Scenario: An undeclared door into a shop room stays vanilla
- **WHEN** the player enters room `0x11F` through entrance id `0x6B`, the door
  for which upstream declares no shop
- **THEN** no shop check is offered and the vanilla shop behavior runs; that
  door is its own pool entry with no shop, so under cave-entrance shuffle it
  offers whatever the permutation put behind it and still never the room's shop

#### Scenario: Shops sharing an interior room stay distinct
- **WHEN** the player enters the Dark World Potion Shop, Lumberjack Hut, Village
  of Outcasts and Lake Hylia shop doors, all of which load room `0x10F` with
  `which_entrance == 0x60`
- **THEN** each door presents its own three slots rather than all four
  presenting the same three

#### Scenario: A shuffled door sells the destination's shop
- **WHEN** cave-entrance shuffle sends an arbitrary overworld door to a shop's
  pool entry
- **THEN** that door sells that shop's three slots at that shop's prices, and
  those slot ids are bound in logic to the region of the door the player used
