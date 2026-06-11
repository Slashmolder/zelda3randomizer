## ADDED Requirements

### Requirement: Runtime door-redirect layer (sparse override, intensity-1 hook set)

The engine SHALL support redirecting a dungeon room-to-room transition to an
arbitrary destination room and arrival door, driven by a per-seed door-link table
(`door_runtime.{c,h}`: `g_door_link[door_id]`, `kDoorRt_NoOverride` sentinel),
gated behind a `kFeatures1_DoorShuffleActive` runtime flag. The redirect SHALL be a
**sparse override**: a table that is entirely `NO_OVERRIDE` (or the flag clear)
SHALL reproduce vanilla behavior byte-for-byte.

The committed (intensity-1) hook set SHALL be exactly:

- **Normal edge doors** — `Rando_DoorTransOverride(dir)` consulted inside the
  supertile-boundary branch of the four `Dungeon_StartInterRoomTrans_*` functions,
  guarding the vanilla positional `room ± 1 / ± 0x10` line (the line runs iff the
  hook returns false). The exiting door is resolved at runtime from
  (room, direction, layer, Link's perpendicular coordinate vs the cataloged slot
  centers); non-catalog rooms and non-catalog exits fast-path vanilla.
- **Spiral staircases** — `Rando_DoorSpiralDest(room, slot, attr, vanilla_byte)` at
  the header-destination read in `Dungeon_DetectStaircase`, keyed by (source room,
  `which_staircase_index & 3`, tile attribute); only spiral head attrs (0x5e/0x5f,
  submodule 14) redirect — the straight inter-room stairs (0x38/0x39) and fat
  stairs (0x26) sharing the site do not. The engine stair slot is resolved to its
  door record via the staircase-list bijection (direction, quadrant, x-rank — the
  catalog `door_index` is the reference's own table ordering, not the engine's
  attr2 slot), and `cur_staircase_plane` is substituted with the shuffled
  destination's plane class (`Rando_DoorSpiralPlane`). The `…Trans_Up/Down`
  hook is a no-op in staircase context (`Rando_DoorStaircaseContext`), so the
  header override is the sole spiral authority.

Hole/pit, vanilla-teleport-door, and straight-stair sites are NOT hooked in the
committed scope; a follow-on MAY extend the redirect there (intensity 2+). Hyrule
Castle's two 1F↔2F hall door pairs are the engine's `0x89` teleport doors despite
being Normal-typed in the reference model; they are flagged
`kDoorTblFlag_VanillaTeleport` in the door catalog (the exit resolver skips them)
and Hyrule Castle is pinned, so no committed-scope layout touches them.

#### Scenario: Identity — flag ON, all NO_OVERRIDE

- **WHEN** `kFeatures1_DoorShuffleActive` is set and every door link is
  `NO_OVERRIDE`
- **THEN** the exit resolver executes at every hooked transition, returns
  no-override, and the vanilla room-index line and layer/palace toggles run
  unchanged — zero RAM-compare divergence side-by-side against the ROM, and the
  flag-off path is separately corpus byte-identical

#### Scenario: Shuffled normal door redirects to an arbitrary room

- **WHEN** a normal edge door's link carries a destination door in a non-adjacent
  room
- **THEN** the transition lands Link at the destination door (not the positional
  neighbor), with room index, camera/bounds, quadrant, and layer fully applied by
  the arrival routine

#### Scenario: Internal quadrant boundary is not a redirect point

- **WHEN** Link crosses an internal quadrant boundary of a 2×2-supertile room (a
  scroll that does NOT change the room index in vanilla)
- **THEN** no redirect fires — the hook sits inside the supertile-boundary branch
  where the room index actually changes, and Link scrolls within the same room as
  in vanilla

#### Scenario: Override arm clears the transition toggles without applying them

- **WHEN** a redirect fires at a normal edge door
- **THEN** the vanilla `room_transitioning_flags` layer/palace toggles are
  **skipped** (`Rando_DoorTransConsumedToggles`) — they encode the POSITIONAL
  partner's relationship, not the shuffled one — and the flags variable is still
  cleared on exit, leaving no stale flag; the destination door record is the layer
  authority, and `cur_palace_index_x2` stays untouched (basic shuffle never crosses
  a palace boundary, enforced at codegen)

#### Scenario: Spiral staircase redirect with intra-room fixup

- **WHEN** a spiral-stair link is shuffled and door shuffle is active
- **THEN** the staircase destination read returns the destination door's room
  (keyed by the specific staircase slot and spiral attribute; an unshuffled
  staircase reads its vanilla header byte), and after the destination room loads,
  `Rando_DoorSpiralFixup` (called from `Dungeon_InitializeRoomFromSpecial`, after
  the vanilla grid-granular adjust) locates the destination staircase head via
  the staircase OBJECT list (`dung_inter_starcases` — filled synchronously by
  `Dungeon_LoadRoom`; the derived attr table is stamped too late) and translates
  the whole mid-walk tableau (Link, camera, quadrant, and the position-anchored
  walk-choreography targets `tiledetect_which_y_pos[0/1]`) by the head-to-head
  delta; the arrival layer is the destination stair's plane half bit from that
  list (the spiral record's `layer` field is the reference's HTH/HTL/LTH/LTL
  transition signature, with bit1 as the pre-load fallback plane)

### Requirement: Generalized arbitrary-room arrival

When a transition is redirected, the engine SHALL place Link and the camera at the
**arrival door's** edge/slot/layer in the destination room — not merely at the same
intra-room offset. Arrival (`DoorRt_Arrive`, generalizing the vanilla teleport-door
routine `Dungeon_AdjustForTeleportDoors` to both axes) SHALL set Link's coordinates
(scroll axis re-based to just inside the arrival edge by whole-supertile tableau
translation; perpendicular axis corrected to the destination door's slot center —
the fine slot delta is deferred and panned in during the transition scroll so the
pre-upload frames never expose stale VRAM), camera/room-bounds (clamped into the
destination's true legal window — full-size vs quadrant-confined, computed
pre-load from the room-data layout byte), quadrant flags, and the
layer from the destination door record, and SHALL set
`dungeon_room_index2`/`dungeon_room_index_prev` to the arrival edge's virtual
positional neighbor so the room load resynchronizes them (and the spiral-adjust
sites see a zero delta). Followers are carried (tagalong y-high resync).

#### Scenario: Arrival lands at the partner door facing inward

- **WHEN** a redirect sends Link through a south door to room B's north door,
  slot 2, upper layer
- **THEN** Link spawns just inside B's north-edge slot-2 doorway on the upper
  layer, with camera and room bounds aligned to B's grid cell and the quadrant
  flags matching the slot position

#### Scenario: Arrival layer comes from the destination door record

- **WHEN** a redirect connects normal edge doors on different BG layers
- **THEN** `link_is_on_lower_level` (and its mirror) are set from the destination
  door's layer field — not from the source door's transition toggles (spiral
  arrivals derive the plane from the staircase object list instead; see the
  spiral scenario)

### Requirement: Environment coherence under redirect

A redirect SHALL load the destination room's full environment (palette, sprite
graphics, the room effect byte, collision, lights-out, BG2) by routing through the
normal room-header load for the destination room — the redirect changes
`dungeon_room_index` before the load, so this holds by construction.

#### Scenario: Redirected room renders with its own environment

- **WHEN** a redirect lands Link in a room with a different palette / effect byte
  (e.g. a flooded or icy room)
- **THEN** that room renders and behaves with its own environment, not the source
  room's

### Requirement: Door-kind overlay for relocated key doors (committed Stage-1b scope)

The engine SHALL apply a per-seed **door-KIND overlay** keyed by (room, door-list
position) — `door_type_mode = original` re-chooses which doors are small-key doors,
so relocated key doors must be physically real and vanilla key doors whose kind
moved away must render and behave as Normal. The overlay SHALL be consulted at all
**three** raw door-list reader seams: (a) the door-object draw path
(`RoomData_DrawObject_Door`'s kind byte), (b) `Dungeon_LoadHeader`'s raw door-word
copy + current-room scan, and (c) `Dungeon_LoadAdjacentRoomDoors` (keyed by its
room argument). Everything downstream of the parsed door type (key prompt and
decrement, attributes, shutters) inherits the override from parse time.

The overlay SHALL enforce the engine's **stateful-position constraint**: door-open
state persists only for door-list slots 0–3 (slots 4–7 are force-opened at load),
so a key door SHALL only be placed where BOTH halves of the pair occupy (possibly
after a ported door-list **position swap**) a pos<4 entry in their own rooms; the
candidate search rejects pairs where either half cannot. On key-door open, the
**logical partner room's open bit SHALL be mirrored** (vanilla syncs halves only
via the physical-adjacency scan, which under shuffle never reaches the logical
partner — double-spend re-lock) and physical-neighbor open-bit propagation SHALL be
suppressed for shuffled edge slots (the unconditional-open branch can otherwise
pre-open a relocated key door unpaid). The reference's Skull Pinball WS
trap→Normal mutation SHALL be ported with its runtime consequence.

#### Scenario: Relocated key door is physically real

- **WHEN** the layout relocates a small-key door onto a connection whose vanilla
  doors are Normal
- **THEN** the door renders as a key door, prompts for and decrements a small key,
  and persists its opened state across room re-entry — and the vanilla key door it
  replaced renders and behaves as Normal

#### Scenario: Paid-open state reaches the logical partner, not the positional neighbor

- **WHEN** Link opens a relocated key door whose logical partner room is not the
  positional neighbor
- **THEN** the partner room's door-open bit is set via the door pairing (the door
  does not re-lock from the other side), and the positional neighbor's matching
  slot is NOT pre-opened by the physical-adjacency scan

#### Scenario: Stateful-position constraint bounds the candidate search

- **WHEN** the key-door candidate search considers a pair where one half's room
  admits no pos<4 door-list entry (even after a swap)
- **THEN** the pair is rejected as a key-door candidate (the constraint lives in
  the candidate search and therefore participates in the layout digest)
