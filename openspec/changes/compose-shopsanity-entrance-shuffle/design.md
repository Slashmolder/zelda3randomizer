# Design

All facts below were read from source in this worktree, not recalled. Line
references are anchors to symbols, not pinned line numbers.

## D1. The exact split

From the shipped assets (`kOverworld_Entrance_Id` + `kEntranceData_rooms`,
cross-validated against the binary's own `--dump-cave-doors`), the four
shop-hosting pool entries carry these overworld door rows. Door = ALTTPR's
`PreviousOverworldDoor` = row index + 1.

| pool entry | room | door rows (ALTTPR door) | becomes |
|---|---|---|---|
| 20 `general_store_1` | `0x110` | `0x75` | unchanged (already one door) — gains shop 240-242 |
| 21 `general_store_2` | `0x112` | `0x58`, `0x5A`, `0x6E` | 3 entries: LW Lake Hylia 261-263, Dark Chapel door (no shop), DW Death Mountain 252-254 |
| 23 `general_store_3` | `0x10F` | `0x57`, `0x60`, `0x6F`, `0x74` | 4 entries: Lumberjack 243-245, Outcasts 246-248, Potion 237-239, Lake Hylia 249-251 |
| 32 `kakariko_lame_shop` | `0x11F` | `0x46`, `0x76` | 2 entries: Kakariko 258-260, entrance id `0x6B` door (no shop) |

Pool: 40 -> 46. `kEntranceMaxInteriors` is **64**
(`src/rando/shuffle_entrance.h`), so every fixed array bounded by it already
fits — no bound raise needed.

The Light World Death Mountain shop (255-257) is **not** part of this: its room
is `0x0FF`, below the `0x100` cached-exit class, so it is not a cave interior
and never shuffles. It keeps the static room-only table row.

Each split entry's `region_name` is the vanilla logic region of **its own
door**, which is why the split is load-bearing rather than cosmetic: the four
`0x10F` doors sit in three different regions (Potion in
`DarkWorld_NorthEast`, Lumberjack and Outcasts in `DarkWorld_NorthWest`, Lake
Hylia in `DarkWorld_South`), and the single pre-split entry claimed
`DarkWorld_NorthWest` for all four.

`Entrance_SelfCheck` already cross-validates every entry's `region_name`
against each of its `location_ids`' logic `region_id` and exits(2) on drift, so
a wrong per-entry region fails the build rather than shipping.

## D2. Append, never insert

New entries take indices 40-45; the existing 0-39 keep their positions. This is
not cosmetic tidiness — `cave_arrival_baked.h` is a committed, hand-captured
table **indexed positionally by interior** (`kCaveArrivalBaked[38]`, loaded with
`g_cave_capture[i] = kCaveArrivalBaked[i]`). Inserting would silently shift
every later entry onto the wrong overworld arrival position, which is worse than
missing data because it is wrong rather than absent.

## D3. Door-row keying

`Entrance_BuildDoorOverlay` resolves each door row's pool entry with
`interior_of_entrance(vanilla[d])`. That is a lookup by *entrance id*, and it is
exactly why the four `0x10F` doors move as one unit today: they all carry
entrance id `0x60`, so they all resolve to the same entry. It becomes a door-row
-> entry map, which is what makes the entries independently shufflable. The
destination side already writes `kCaveInteriors[j].entrance_ids[0]` per door
row, so it needs no change — several destination entries legitimately share
room `0x10F` and entrance id `0x60`, because they are the same physical room.

Audited consumers of the entrance-id -> interior direction:

- `Rando_RecordEnteredDoorForCapture(lx)` and `Rando_DecoupledSetEnteredDoor(lx)`
  (`src/rando/rando.c`) — both already receive `lx`; switch to the door-row map.
- `--dump-cave-doors` (`src/main.c`) — iterates door rows; switch.
- `Rando_RecordEnteredFallhole` — keys on `which_entrance` with no door row
  available, but it handles *fall-hole* caves only (entrance ids `0x76`-`0x81`),
  none of which are split. It stays entrance-id-keyed, with an assertion that no
  split entry's entrance id falls in that range.

## D4. Shop identity is derived, not tabulated

A shop is a *destination entry*. The player reaches destination entry `j`
through the door rows of the source entry `ix` with `assign[ix] == j`, so the
shop's live door set is that source entry's door rows. Runtime resolution
becomes: door row -> source entry -> `assign` -> destination entry -> its
`shop_loc_base`. With the axis off, `assign` is the identity and this reduces
exactly to the vanilla door column, which the door-walk audit already pins.

This is ALTTPR's `Shop.get_bytes` re-derivation expressed against our own
assignment instead of a rewritten ROM table. The static `kRandoShopSlots` table
keeps only the one row that is not a cave interior (LW Death Mountain).

Consequence worth stating: the shop's *identity* travels with the destination,
so a door that now leads to the Potion Shop's interior entry sells the Potion
Shop's three slots — and `Entrance_ApplyRegionOverrides` rebinds those three
slot ids to that door's region in the same pass. Placement and runtime therefore
agree by construction rather than by a second table that could drift.

## D5. Region binding falls out

Once each shop entry carries its three slot ids in `location_ids`,
`Entrance_ApplyRegionOverrides` binds them to the source door's region and
predicate with no new code — it already loops `dst->location_count` and calls
`Rando_SetEntranceRegionOverridePred`. The shop ids (237-263) are below
`kEntranceRegionOverrideMax` (512), so unlike the cave/house pot ids they fit
the per-location override range. This is the option (a) from the previous
change's D2, which only became possible once splitting gave each shop its own
door.

## D6. Known degradation: decoupled arrival for the six new doors

`g_cave_capture[]` is filled from `kCaveArrivalBaked[38]` and holds the
overworld position to emerge at in **decoupled** mode. Capture is keyed by
interior (`Rando_CaptureArrivalForBake`), so for a multi-door interior today the
*last door walked* overwrites the entry — the pre-split table already holds only
one of room `0x10F`'s four door positions, and which one is not recorded.

After the split the six new entries have no baked arrival. The loader already
degrades safely: `valid == 0` means the decoupled exit falls back to the coupled
behavior (emerge at the door you entered). That is never a softlock. The four
pre-existing shop entries keep their baked block, which may correspond to a
different one of their doors than the entry now names.

Decision: mark the arrival entries of all split rooms invalid rather than keep a
block we cannot attribute to a specific door, so decoupled mode is
*conservatively coupled* for those ten doors until an owner capture walkabout
(`ZELDA3_CAPTURE_ARRIVALS`) fills them in. Honest-but-degraded beats
confidently-wrong; coupled mode — the default — is unaffected either way.

## D7. Persistence needs no format change

Checked rather than assumed, and it overturned the first draft of this design.
The cave assignment is **never stored**. `Entrance_ComputeLayout` regenerates it
from `(seed, entrance_axes, entrance_attempt)`; the sidecar keeps only
`entrance_axes`, `entrance_attempt` and a 24-bit `entrance_digest24` drift
check, and the share string carries nothing entrance-specific (only the settings
blob's axis bits and `seed_u64`). The runtime layout arrays are already
`kEntranceMaxInteriors`-wide (64), and the discovery bitmaps are
`(64+7)/8 = 8` bytes, so 46 fits everywhere.

So: no sidecar format bump, no share-string width change, no TLV. Every
pre-existing entrance-shuffle slot will simply fail the `entrance_digest24`
drift check on activation, which is the correct behavior across a
`kGeneratorVersion` bump — the seed genuinely regenerates differently now.

## D8. Additional breakages the data-flow map surfaced

These are not obvious from the shuffle code alone and each needs handling:

- **`Entrance_SelfCheck` check (1)** asserts
  `interior_of_entrance(entrance_ids[k]) == i` for every entry — it hard-fails
  the instant two entries share an entrance id. Must be re-expressed per door
  row (the split is legitimate, the invariant is not).
- **Self-check (7)** builds a synthetic door table in `uint8 van[80]` with **no
  bounds check**; today `sum(entrance_count) == 57`. The split raises it to
  about 61, still under, but the missing bound is a latent stack overflow —
  add it while touching this.
- **Self-check (6)** hard-codes interior indices (`sw[10] = 8; sw[8] = 10`) and
  literal location ids. Append-only ordering keeps these valid; another reason
  not to insert.
- **`kRandoCaveSourcePreds` is positional** in `interiors:` order and is emitted
  by a *different* generator (`assets/rando_logic_gen.py` from the yaml
  `can_enter:` field) than the C interior table. It regenerates automatically at
  46, but the positional keying is a third independent reason the split must
  append rather than insert.
- **`Rando_EntranceConnection` / auto-tracker** returns `false` when
  `to == from`. Once several entries share an entrance id, a permutation that
  maps one `0x10F` shop entry to another reports "unshuffled" and never marks
  the connection discovered. Compare entries, not entrance ids.
- **Region-override store is last-write-wins** across entries sharing a location
  id, and `Entrance_CaveInteriorOfLocation` is keyed by location. The split must
  keep location lists disjoint (it does — each shop entry takes its own three
  slot ids), and the generator should assert it.
- **`Entrance_AddedEdgeWorstCase`** returns `cave_count + dungeon_count`, checked
  against `Rando_EntranceAddedEdgeCapacity()` in `OwWarp_SelfCheck`. It goes
  51 -> 57; confirm the capacity covers it.

## D9. Per-door regions, and a region-drift finding

Each split entry's `region_name` is derived, not guessed. Method: take the
entry's door row, read its overworld area from `kOverworld_Entrance_Area`, and
look the screen up in `assets/rando/ow_graph.gen.yaml`
(`components[].screen -> zone`).

The method was **validated before being trusted**: all eight cave-resident shop
doors resolve to exactly the region `location_registry.yaml` already assigns
that shop's slots, single-valued, with no mismatch. So the derived values for
the split are:

| door row | ALTTPR door | screen | region |
|---|---|---|---|
| `0x57` | `0x58` | `0x35` | `LightWorld_South` (LW Lake Hylia shop) |
| `0x59` | `0x5A` | `0x53` | `DarkWorld_NorthWest` (no shop) |
| `0x6D` | `0x6E` | `0x45` | `DarkWorld_DeathMountain_East` (DW Death Mountain shop) |
| `0x56` | `0x57` | `0x42` | `DarkWorld_NorthWest` (Lumberjack) |
| `0x5F` | `0x60` | `0x58` | `DarkWorld_NorthWest` (Outcasts) |
| `0x6E` | `0x6F` | `0x56` | `DarkWorld_NorthEast` (Potion) |
| `0x73` | `0x74` | `0x75` | `DarkWorld_South` (DW Lake Hylia) |
| `0x45` | `0x46` | `0x18` | `LightWorld_NorthWest` (Kakariko shop) |
| `0x75` | `0x76` | `0x02` | `LightWorld_NorthWest` (no shop) |
| `0x74` | `0x75` | `0x5A` | `DarkWorld_NorthWest` (DW Forest shop) |

This exposes that the pre-split entries were already carrying regions their own
doors do not support: entry 21 declares `LightWorld_NorthWest` while its three
doors sit in `LightWorld_South`, `DarkWorld_NorthWest` and
`DarkWorld_DeathMountain_East`; entry 20 declares `LightWorld_NorthWest` for a
door in `DarkWorld_NorthWest`. That was invisible because
`Entrance_SelfCheck`'s region cross-validation only runs for entries that have
locations, and all four shop entries had none. Giving them their shops' slot ids
brings them under that check, so the split is guarded going in.

**Out of scope but recorded:** the same sweep flags roughly a dozen further
entries, including several SINGLE-door ones (`archery_game`,
`warped_pond_of_wishing`, both `refill_cave_1_*`, `kakariko_library`,
`mimic_cave`) whose declared region matches none of their doors. If real, those
mis-bind locations under cave-entrance shuffle today, independently of shops.
The screen-to-zone lookup is only a hypothesis for non-shop doors — an overworld
screen can host several logic regions (ledges, the Death Mountain east/west
split) — so they are NOT changed here; they are spun out as their own audit.
Multi-door entries whose region matches one of their doors are a known
approximation of the coarse pool, not necessarily a bug.

## D10. Coupled exits are safe by construction

The cave "cached exit" is not rando state: `Dungeon_LoadEntrance` copies the
live overworld arrival (area, Link x/y, camera, scroll targets) into the `*_exit`
shadow block on **every** interior entry, and `LoadOverworldFromDungeon` restores
it for rooms in `[0x100,0x180)\{0x104}`. That replays where Link physically
stood, so four doors into room `0x10F` each return to their own door with no
per-entry table. Coupled cave shuffle — the default — is therefore safe under
the split, and the risk is confined to the decoupled arrival data in D6.

## D8. Why not pin the shop doors vanilla instead

Considered and rejected by the owner. Excluding the four shop interiors from the
pool when shopsanity is on would have kept all 27 slots sound with a small,
low-risk change (36 of 40 interiors still shuffling, 60 of 70 cave door rows),
and moved digests only for seeds with both axes. It was rejected because it
costs the ALTTPR behavior of finding a shop behind a random door, which is the
point of running the two axes together. Recorded here so the tradeoff is not
re-litigated from scratch.
