# randomizer-shopsanity

## ADDED Requirements

### Requirement: Shop identity is keyed by the entered overworld door

A shop SHALL be identified at runtime by the pair (interior room low byte,
entered overworld door id), where the door id is the overworld door row index
plus one — the same quantity ALTTPR stores in `PreviousOverworldDoor`
(`z3randomizer/doorframefixes.asm` `StoreLastOverworldDoorID`) and the same
quantity its `new Shop(...)` door argument holds. The engine's `which_entrance`
SHALL NOT be used as the shop key: four shops share entrance id `0x60` and two
share `0x58`, so it cannot separate them.

The room SHALL be compared as the full 16-bit room index, as the ROM does; the
low byte alone aliases ordinary rooms onto interior rooms (room `0x010` onto
`0x110`).

Only the one shop ALTTPR declares with no door (Light World Death Mountain,
`config = 0x43`) SHALL match on the room alone, mirroring its
`shop_config & 0x40` skip-door-check bit. Every other shop SHALL stay
door-keyed even when its room hosts a single shop, so that a room loaded by
more than one overworld door (room `0x11F` is loaded by entrance ids `0x46` and
`0x6B`, and upstream declares a shop for the first only) keeps vanilla shop
behavior at the undeclared door, and no slot becomes obtainable from a door its
logic region does not bind it to.

The door id SHALL be captured at the overworld entry hook and SHALL persist in
the reserved `g_ram` compat block, so that a snapshot replay-restore — which
restores `g_ram` but not C statics — keeps the active shop resolvable.

#### Scenario: Every shop slot is reachable from exactly one door
- **WHEN** the shop resolver is walked against every row of the vanilla
  overworld entrance table with a `shopsanity=true` slot active
- **THEN** each of the 27 shop slots (ids 237-263) resolves from at least one
  overworld door, and no slot resolves from more than one door

#### Scenario: Shops sharing an interior room stay distinct
- **WHEN** the player enters the Dark World Potion Shop, Lumberjack Hut,
  Village of Outcasts and Lake Hylia shop doors, all of which load room `0x10F`
  with `which_entrance == 0x60`
- **THEN** each door presents its own three slots (237-239, 243-245, 246-248,
  249-251 respectively) rather than all four presenting the same three

#### Scenario: The door-less shop resolves from its room alone
- **WHEN** the player enters the Light World Death Mountain shop, which upstream
  declares with `config = 0x43` and `door_id = 0x00`
- **THEN** its three slots resolve on the room match alone, without consulting
  the door id

#### Scenario: An undeclared door into a shop room stays vanilla
- **WHEN** the player enters room `0x11F` through entrance id `0x6B`, the door
  for which upstream declares no shop
- **THEN** no shop check is offered and the vanilla shop behavior runs

## MODIFIED Requirements

### Requirement: Shopsanity composition rules

The axis SHALL be normalized off when cave-entrance shuffle is in effect, and
SHALL otherwise compose without derived-rule normalization against every other
axis: door shuffle, dungeon chains, pot/terrain/enemy-check families, souls (no
shopkeeper is soul-gated), and boss/drop/enemy shuffles. Shop slots SHALL be
excluded from the trap-masquerade eligible set. Customizer manifests SHALL be
able to pin shop slots when the axis is on and SHALL refuse shop-slot pins when
it is off. The spoiler SHALL emit shop rows with their prices (JSON `shops[]`
gains `"price"`; the text `Shops:` section prints the price per slot) in every
world state where shop-class placements exist, leaving axis-off non-Retro
spoilers byte-identical.

Four shop interiors (rooms `0x10F`, `0x110`, `0x112`, `0x11F`) ARE members of
the cave-entrance shuffle pool, and their `kCaveInteriors` entries do not list
the shop slot ids, so `Entrance_ApplyRegionOverrides` does not rebind them.
Under cave-entrance shuffle a shop slot would therefore be evaluated from its
vanilla overworld region while the runtime reaches that interior through a
different door. Because a shuffled destination interior is reached through the
door rows of a single source interior, the rooms hosting several shops cannot
serve all their slots however they are keyed; rebinding regions alone cannot
make the composition sound. The axis SHALL therefore be forced off under
effective cave-entrance shuffle by a single predicate that canonical
serialization, placement, runtime and the spoiler all consult, so the
`settings_hash` describes the seed actually generated.

#### Scenario: Cave-entrance shuffle forces the axis off
- **WHEN** a seed requests `shopsanity=true` together with
  `shuffle_cave_entrances=true` in a world state where cave-entrance shuffle is
  effective (Open or Standard)
- **THEN** the generated seed contains no shop-class fill placements, the
  canonical settings serialize `shopsanity` as off, and the spoiler reports the
  axis as off

#### Scenario: No derived-rule coupling
- **WHEN** `shopsanity=true` is combined with any supported axis combination
  other than effective cave-entrance shuffle
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
