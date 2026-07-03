## ADDED Requirements

### Requirement: Enemy item-marker mode supports multiple simultaneous icons

`[Graphics] EnemyDropMarker=item` SHALL render real placed-item icons for active
enemy markers when a bounded marker-icon pool can represent those icons safely.
Enemy markers SHALL NOT rely on unowned OBJ cells or on a single shared receive-item
tile slot in a way that lets the last loaded icon overwrite other markers.

Each marker icon slot SHALL reserve the complete OBJ tile footprint required by the
icon, including multi-entry 16x16 or custom-art layouts. The implementation SHALL
document the base charnum mapping for every reserved marker slot. When only the
shared receive-item slot is safe, the pool capacity is one distinct icon key and the
renderer SHALL use it only after final OAM proves the slot is not already visible.

The renderer SHALL treat the neutral gold glint as the fallback for any marker whose
exact placed item cannot be rendered safely due to tile-slot capacity, palette
availability, missing art, receipt/direct-grant conflict, field-item conflict, or
OAM pressure. In item mode, the runtime SHALL show either the exact placed item or
the neutral glint; it SHALL NOT show a key, rupee, or other stand-in for a different
placed item.

#### Scenario: Distinct unchecked carriers get distinct icons
- **WHEN** two active unchecked live enemy checks in the same room hold different
  placed items and both icon keys fit in a marker-owned marker-icon pool
- **THEN** each carrier renders its own placed item icon
- **AND** neither carrier renders the other carrier's icon

#### Scenario: Shared receive slot cannot represent distinct icons
- **WHEN** two active unchecked enemy markers hold different placed items and the
  only safe exact-item slot is the shared receive-item slot
- **THEN** at most one distinct icon key renders exactly
- **AND** the other marker falls back to the neutral glint instead of showing stale
  or corrupted item art

#### Scenario: Identical icons share one marker slot
- **WHEN** multiple active enemy markers resolve to the same icon key
- **THEN** the renderer MAY load that icon once and draw all matching markers from
  the same marker tile slot

#### Scenario: Marker capacity is exhausted
- **WHEN** more distinct active enemy marker icons are visible than the marker-icon
  pool can safely represent
- **THEN** lower-priority markers fall back to the neutral gold glint or suppress
  cleanly according to the documented fallback policy
- **AND** no marker draws corrupt or stale item graphics

#### Scenario: Multi-entry icon slot is incomplete
- **WHEN** an active enemy marker's placed item requires a multi-entry OBJ footprint
  and the marker pool cannot reserve every required tile cell
- **THEN** that marker draws the neutral gold glint instead of a partial item icon

#### Scenario: Exact item cannot be represented
- **WHEN** an active enemy marker's placed item requires missing art, an unavailable
  palette row, or an unsafe custom draw policy
- **THEN** that marker draws the neutral gold glint instead of a substitute item

### Requirement: Spawned enemy-drop pickups have item-marker priority

Spawned forced-drop pickups SHALL have higher marker priority than live enemy
carriers. Once a carrier has been killed and its active forced drop is visible, the
spawned pickup SHALL render the real placed item when the marker-icon pool can
represent it safely. Other live enemy markers MAY still render their own real item
icons if resources remain; otherwise they SHALL fall back to the neutral gold glint
or suppress cleanly.

#### Scenario: Spawned pickup and live carrier coexist
- **WHEN** an active forced enemy-drop pickup is visible and another unchecked live
  enemy marker remains in the room
- **THEN** the spawned pickup receives allocation priority for its real placed item
- **AND** the live carrier renders its own real placed item only if resources remain

#### Scenario: Spawned pickup cannot render exact item
- **WHEN** the spawned forced-drop pickup's real placed item cannot be rendered
  safely
- **THEN** the spawned pickup draws the neutral gold glint instead of a substitute
  item

### Requirement: Generic marker mode remains non-spoiler

`[Graphics] EnemyDropMarker=generic` SHALL continue to draw the neutral gold glint
for live active enemy carriers without resolving or revealing their placed item
icons. This mode SHALL NOT allocate item-icon marker slots for live carriers.

#### Scenario: Generic live marker hides placement
- **WHEN** a live active enemy carrier holds a non-key placed item and
  `EnemyDropMarker=generic`
- **THEN** the live carrier draws the neutral gold glint and does not draw the
  placed item icon

### Requirement: Enemy marker multi-icon rendering is visual-only

The multi-icon marker renderer SHALL be client-local presentation only. It SHALL NOT
change placement, logic, share strings, canonical settings, `settings_hash`,
`kGeneratorVersion`, checked-location persistence, sidecar format, snapshot format,
spoiler output, tracker reachability, or auto-tracker messages.

#### Scenario: Same seed with different local marker resources
- **WHEN** two clients load the same share string but one can allocate multiple enemy
  item marker icons and the other falls back to glints
- **THEN** their item placement, checked state, grants, spoiler digests, and save
  data remain identical

### Requirement: Enemy marker rendering composes with other visual systems

Enemy marker item icons SHALL coexist safely with pot-sanity glints, field-item
sprites, item receipts, direct-grant confirmations, enemy sprites, spawned pickups,
HUD/tracker overlays, and text boxes. If a required graphics, palette, DMA, or OAM
resource is already owned by a higher-priority visual path, enemy item markers SHALL
fall back to the neutral gold glint or suppress cleanly for that frame.

Marker palette writes SHALL be frame-scoped or confined to a proven marker-owned
palette row. A row shared with Link, enemies, pot glints, receipts, or field items
SHALL be restored before reuse. Multiple custom marker icons that need incompatible
palette contents SHALL NOT alternate or recolor each other; lower-priority markers
SHALL fall back to the neutral gold glint or suppress cleanly.

Before writing OAM for an item marker or glint fallback, the renderer SHALL reserve
the full OAM footprint in the correct sorted region. Multi-entry icons SHALL draw all
required OAM entries or none.

#### Scenario: Pot glints remain visible
- **WHEN** a room contains active pot-sanity glints and active enemy item markers
- **THEN** drawing enemy markers does not hide, recolor, or corrupt the pot glints

#### Scenario: Receipt animation keeps priority
- **WHEN** an item receipt, direct-grant confirmation, or equivalent placed-item
  presentation is active
- **THEN** enemy item markers do not overwrite its graphics or palette state

#### Scenario: OAM pressure is handled safely
- **WHEN** the room lacks enough OAM capacity for all requested enemy markers
- **THEN** lower-priority enemy markers fall back or suppress cleanly
- **AND** no partial, stale, or offscreen garbage marker is drawn

#### Scenario: Custom marker palettes conflict
- **WHEN** two active custom-art enemy markers require incompatible contents in the
  same non-marker-owned palette row
- **THEN** at most one uses that palette row and the other draws the neutral glint or
  suppresses cleanly

### Requirement: Dense-room marker conflicts are explicitly tested

The implementation SHALL include targeted verification for rooms with multiple
active enemy checks whose placed items resolve to different icons. The Hyrule Castle
room `0x72` case, where the map-guard enemy-drop check and another ordinary enemy
check can hold different placed items, SHALL be covered by F12/OAM/VRAM inspection
or an equivalent automated visual dump.

#### Scenario: Room 0x72 does not show last-icon-wins corruption
- **WHEN** room `0x72` has at least two active enemy markers with different placed
  items and `EnemyDropMarker=item`
- **THEN** each allocated marker draws its own placed item icon or the neutral glint
- **AND** no marker draws a stale icon loaded for another enemy check
