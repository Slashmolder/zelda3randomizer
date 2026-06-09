## ADDED Requirements

### Requirement: Runtime door-redirect layer (sparse override)

The engine SHALL support redirecting a dungeon room-to-room transition to an arbitrary
destination room and arrival door, driven by a per-seed door-link table, gated behind a
`kFeatures1_DoorShuffleActive` runtime flag. The redirect SHALL be a **sparse override**:
each door-stub either carries an explicit destination or a `NO_OVERRIDE` sentinel, and a
table that is entirely `NO_OVERRIDE` (or the flag clear) SHALL reproduce vanilla behavior
byte-for-byte.

The redirect SHALL be consulted at the normal edge-door transition sites
(`Dungeon_StartInterRoomTrans_{Right,Left,Up,Down}`) and the spiral-staircase /
straight-staircase / hole / teleport-door sites. For the table-driven transitions
(staircase/hole/teleport), the redirect MAY override the room-header travel-destination
bytes (`dung_hdr_travel_destinations[0..4]`) at load time rather than the asset.

#### Scenario: Identity table reproduces vanilla

- **WHEN** door shuffle is inactive (flag clear) or the door-link table is entirely
  `NO_OVERRIDE`
- **THEN** every dungeon transition takes its vanilla destination (positional `room ± 1`/
  `± 0x10` for normal doors; header bytes for staircases/holes), with zero RAM-compare
  divergence and byte-identical regression-corpus digests

#### Scenario: Shuffled normal door redirects to an arbitrary room

- **WHEN** a normal edge door's stub carries a destination override to a
  non-adjacent room
- **THEN** the transition loads the override's destination room (not the positional
  neighbor) and Link arrives at the override's arrival door

#### Scenario: Spiral staircase redirect via header override

- **WHEN** a spiral-stair stub is shuffled and door shuffle is active
- **THEN** the staircase transition reads the overridden destination (keyed by the specific
  staircase slot, not a blanket header overwrite), and an unshuffled staircase reads its
  vanilla header destination

#### Scenario: Internal quadrant boundary is not a redirect point

- **WHEN** Link crosses an internal quadrant boundary of a 2×2-supertile room (a scroll
  that does NOT change the room index in vanilla)
- **THEN** no redirect fires — the redirect is consulted only at the supertile-boundary
  branch where the room index actually changes, and Link scrolls within the same room as
  in vanilla

#### Scenario: Redirect preserves transition bookkeeping

- **WHEN** a redirect fires at a normal edge door
- **THEN** the layer/floor toggles (`room_transitioning_flags`) are consumed and cleared
  exactly as the vanilla path would, leaving no stale flag to corrupt the next transition

### Requirement: Generalized arbitrary-room arrival

When a transition is redirected, the engine SHALL place Link and the camera at the
**arrival door's** edge/slot/layer in the destination room — not merely at the same
intra-room offset. Arrival SHALL set Link's coordinates, camera/room-bounds, quadrant
flags, lower-level/layer state, and (when the destination differs) the floor/plane,
generalizing the vanilla teleport-door routine (`Dungeon_AdjustForTeleportDoors`).

#### Scenario: Arrival lands at the partner door facing inward

- **WHEN** a redirect sends Link through a south door to room B's north door, slot 2,
  upper layer
- **THEN** Link spawns just inside B's north-edge slot-2 doorway on the upper layer,
  facing into the room, with the camera and room bounds aligned to B's grid cell

#### Scenario: Cross-floor redirect sets layer and camera coherently

- **WHEN** a redirect crosses BG layers or floors within a dungeon
- **THEN** `link_is_on_lower_level` and the camera/quadrant-fullsize flags match the
  destination room's layout (no mis-scrolled or wrong-layer arrival)

### Requirement: Environment and shutter coherence under redirect

A redirect SHALL load the destination room's full environment (palette, sprite graphics,
the room effect byte, collision, lights-out, BG2) by routing through the normal
room-header load for the destination room. Shutter/trap-door open-state and the per-room
"door opened" bits SHALL remain consistent with the redirected topology: adjacency peeks
that drive shutter opening SHALL use the redirected neighbor, not the positional one.

#### Scenario: Redirected room renders with its own environment

- **WHEN** a redirect lands Link in a room with a different palette / effect byte (e.g. a
  flooded or icy room)
- **THEN** that room renders and behaves with its own environment, not the source room's

#### Scenario: Shutter opens for the redirected neighbor

- **WHEN** Link approaches a shuffled shutter/key door whose logical neighbor differs
  from the positional one
- **THEN** the shutter open-state is computed against the redirected neighbor's door list,
  and the door's open/locked state persists correctly across re-entry
