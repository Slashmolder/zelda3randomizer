# Design

## D1. Grounding — what the vanilla tables actually say

Established by reading the shipped assets, not from memory. `dump_entrances.py`
parses `zelda3_assets.dat` the way `LoadAssets()` does and was cross-validated
against two independent anchors before any conclusion was drawn from it:

- `src/dungeon.c` (`Dungeon_LoadEntrance`) comments that overworld door `0x5A`
  loads room `0x112`; the table agrees (`kEntranceData_rooms[0x5A] == 0x112`).
- `z3randomizer/doorframefixes.asm` comments that the tavern door "has index
  0x42 (saved off value is incremented by one)"; the table agrees
  (`kOverworld_Entrance_Id[0x42] == 0x43`, room `0x103`).

The chain the engine walks is
`lx` (overworld door row) -> `which_entrance = kOverworld_Entrance_Id[lx]` ->
`room = kEntranceData_rooms[which_entrance]` (`src/overworld.c`,
`src/dungeon.c`). ALTTPR's shop door is a *fourth* quantity, `lx + 1`.

For the 9 shops (PHP `new Shop(...)` args, verified in
`../alttp_vt_randomizer/app/Region/Standard/**`):

| shop | room | ALTTPR door (`lx+1`) | `which_entrance` at that door | locs |
|---|---|---|---|---|
| DW Potion Shop | 0x10F | 0x6F | 0x60 | 237-239 |
| DW Forest Shop | 0x110 | 0x75 | 0x57 | 240-242 |
| DW Lumberjack Hut | 0x10F | 0x57 | 0x60 | 243-245 |
| DW Outcasts Shop | 0x10F | 0x60 | 0x60 | 246-248 |
| DW Lake Hylia Shop | 0x10F | 0x74 | 0x60 | 249-251 |
| DW Death Mountain Shop | 0x112 | 0x6E | 0x58 | 252-254 |
| LW Death Mountain Shop | 0x0FF | (room-only) | n/a | 255-257 |
| LW Kakariko Shop | 0x11F | 0x46 | 0x46 | 258-260 |
| LW Lake Hylia Shop | 0x112 | 0x58 | 0x58 | 261-263 |

The binary's own `--dump-cave-doors` independently confirms the collapse: cave
23 (`general_store_3`) appears at four distinct dark-world screens (0x02, 0x18,
0x16, 0x35) all with `id=0x60`; cave 21 (`general_store_2`) at two with
`id=0x58`.

**Door is the only key that separates them.** Room alone cannot (4 and 2 share
a room); `which_entrance` cannot (same). This is why ALTTPR keys on
`PreviousOverworldDoor`.

## D2. Why not "add the shop ids to `kCaveInteriors[].location_ids`"

The obvious fix for the reported entrance-shuffle gap — option (a) in the report
— is to let the existing region-override machinery rebind the shop slots. The
shop ids (237-263) do fit under `kEntranceRegionOverrideMax` (512), so unlike
the cave/house pot ids there is no range obstacle.

It still cannot work, because region-rebinding only makes the *logic* honest
about a slot the runtime can actually serve. Under cave shuffle a destination
interior is reached through the door rows of exactly **one** source interior, so
room `0x10F` can present at most one shop's three slots however it is keyed —
9 of its 12 slots are structurally unservable, and 3 of room `0x112`'s 6 are.
Rebinding regions for slots the runtime can never hand over would trade an
unsound region for an unreachable location.

ALTTPR avoids the ambiguity by construction: each shop is its own region with
its own entrance, so its writer can re-derive the door byte per seed from the
post-shuffle connection (`ALttPDoorRandomizer/BaseClasses.py` `Shop.get_bytes`:
`door_id = door_addresses[entrances[0].name][0] + 1`, falling back to
`door_id = 0; config |= 0x40` when the region has no single entrance). This
fork's entrance registry is coarser — one pool entry per interior *room* — so
the four room-`0x10F` doors move as a unit and cannot receive distinct doors.

Matching ALTTPR would mean splitting interiors 23 and 21 into 4 and 2 pool
entries (the registry already supports interiors sharing a room — 30/31 share
`0x11B`, 37/38 share `0x126`). That changes the cave pool from 40 to 44 entries
and therefore every cave-shuffle permutation and digest. It is the right
long-term path and is recorded as such, but it is out of scope here.

So: **fix the identity bug (which is not entrance-shuffle-specific), and
normalize the axis off under cave-entrance shuffle** — option (b), the
`Settings_PotShuffleForcedOff` precedent.

## D3. Where the door id lives

`g_rando_takeany_door_id` already captures `lx + 1` at the same hook for Retro
take-any, and is a plain C global documented as transient — it is consumed by
the redirect on the same entry. A shop needs its door for as long as the player
stands in the room, across arbitrarily many frames, and must survive a snapshot
replay-restore (which restores `g_ram` but not C statics — the reason
`kRam_RandoSwordless` lives in `g_ram`). ALTTPR likewise keeps
`PreviousOverworldDoor` in RAM (`$7F5099`).

So the door id goes in the reserved compat block as
`kRam_RandoOverworldDoor` (`0x66d`, first of the three forward-compat bytes at
`0x66d-0x66f`), written at the overworld entry hook next to the existing
take-any capture. Fall-hole and other non-door entries clear it, matching the
take-any clears already there.

## D4. Room width, and who matches on the room alone

The room must be compared at its full 16 bits, as the ROM does
(`shopkeeper.asm` compares `ShopTable+1,X` against `RoomIndex` in 16-bit mode).
The shipped code kept only the low byte, which aliases: an early iteration of
this change briefly matched ordinary room `0x010` onto shop room `0x110`, and
the door-walk caught it. Interior rooms are `0x1xx`, so the low byte collides
with plenty of ordinary rooms.

Only the Light World Death Mountain shop matches on the room alone. It is the
one shop ALTTPR constructs with no door (`config = 0x43` = `0x03 | 0x40`,
`door_id = 0x00`), and `0x00` is also the "no entrance" reset value, so keying
it on the door would silently fail.

It is tempting to extend room-only matching to the other rooms that host a
single shop (`0x110`, `0x11F`) — it costs no ambiguity. Rejected: room `0x11F`
is loaded by TWO overworld doors (entrance ids `0x46` and `0x6B`) and upstream
declares a shop for `0x46` only, so the `0x6B` door must keep vanilla shop
behavior. Room-only matching would also hand the slot over from a door the
logic never bound it to, putting runtime reachability ahead of the modeled
region. Staying door-keyed keeps logic and runtime exactly congruent.

## D5. Normalization shape

`Settings_ShopsanityForcedOff(s)` returns
`s != NULL && Settings_EffectiveShuffleCaveEntrances(s)`, exactly parallel to
`Settings_PotShuffleForcedOff`. `apply_derived_rules` clears `s->shopsanity`
when it fires, so the canonical bytes — and therefore `settings_hash` — describe
the seed that is actually generated. Every consumer already reads
`s->shopsanity` after normalization, so no second predicate is needed at the
placement/runtime/spoiler sites.

`Settings_EffectiveShuffleCaveEntrances` is already scoped to Open/Standard, so
Retro and Inverted shopsanity are untouched. That also answers the take-any
question: Retro take-any doors are keyed by door index already
(`Rando_TakeAnyHostByDoorIndex(lx)`) and cave shuffle is inert in Retro, so the
two cannot co-occur. Capacity Upgrade slots (264/265) are identity-placed and
dispatched by their own site, so they carry no shuffled-fill soundness burden.

## D6. The guard

`--rando-shop-doorwalk` walks every row of `kOverworld_Entrance_Id`, reproduces
the engine's `lx -> which_entrance -> room` chain, and asks the live
`Rando_ShopSlotCheckInfo` what that door yields. It exits non-zero if any shop
slot is unreachable or reachable from more than one door. It needs
`zelda3_assets.dat`, so it belongs to the asset-bearing validation profile, not
the assetless corpus run. It is deliberately independent of
`--rando-shop-probe`, whose self-referential dump is what let this ship.
